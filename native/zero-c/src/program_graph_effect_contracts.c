#include "program_graph_contracts.h"

#include "std_sig.h"

#include <stdio.h>
#include <string.h>

static bool effect_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool effect_text_present(const char *text) {
  return text && text[0];
}

static const ZProgramGraphNode *effect_node_by_id(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (effect_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphResolutionReference *effect_ref_for_node(const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *node, const char *kind) {
  for (size_t i = 0; resolution && node && i < resolution->reference_len; i++) {
    const ZProgramGraphResolutionReference *ref = &resolution->references[i];
    if ((!kind || effect_text_eq(ref->kind, kind)) && effect_text_eq(ref->node_id, node->id)) return ref;
  }
  return NULL;
}

static const ZProgramGraphResolutionReference *effect_call_ref(const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *call) {
  return effect_ref_for_node(resolution, call, "call");
}

static const ZProgramGraphEdge *effect_owner_edge(const ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && node_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && effect_text_eq(edge->to, node_id)) return edge;
  }
  return NULL;
}

static bool effect_is_under_kind(const ZProgramGraph *graph, const ZProgramGraphNode *node, ZProgramGraphNodeKind kind) {
  const char *current = node ? node->id : NULL;
  for (size_t depth = 0; graph && current && depth < graph->node_len; depth++) {
    const ZProgramGraphEdge *owner = effect_owner_edge(graph, current);
    const ZProgramGraphNode *owner_node = owner ? effect_node_by_id(graph, owner->from) : NULL;
    if (!owner_node) return false;
    if (owner_node->kind == kind) return true;
    current = owner_node->id;
  }
  return false;
}

static const ZProgramGraphNode *effect_child(const ZProgramGraph *graph, const ZProgramGraphNode *node, const char *kind, size_t order) {
  for (size_t i = 0; graph && node && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge->order == order && effect_text_eq(edge->from, node->id) && effect_text_eq(edge->kind, kind)) {
      return effect_node_by_id(graph, edge->to);
    }
  }
  return NULL;
}

static const ZProgramGraphNode *effect_owner_function(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  const char *current = node ? node->id : NULL;
  for (size_t depth = 0; graph && current && depth < graph->node_len; depth++) {
    const ZProgramGraphEdge *owner = effect_owner_edge(graph, current);
    const ZProgramGraphNode *owner_node = owner ? effect_node_by_id(graph, owner->from) : NULL;
    if (!owner_node) return NULL;
    if (owner_node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION) return owner_node;
    current = owner_node->id;
  }
  return NULL;
}

static bool effect_ref_kind(const ZProgramGraphResolutionReference *ref, const char *kind) {
  return ref && effect_text_eq(ref->target_kind, kind);
}

static bool effect_call_fallible(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *call, const ZProgramGraphResolutionReference **ref_out) {
  const ZProgramGraphResolutionReference *ref = effect_call_ref(resolution, call);
  if (ref_out) *ref_out = ref;
  if (!ref || !ref->resolved) return false;
  if (effect_text_eq(ref->qualified_name, "world.out.write")) return true;
  const ZProgramGraphNode *target = effect_node_by_id(graph, ref->target_node);
  if (target && target->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && target->fallible) return true;
  if (effect_ref_kind(ref, "stdlib") || effect_ref_kind(ref, "graphBackedStdlib")) {
    const ZStdHelperInfo *helper = z_std_helper_find(ref->qualified_name);
    return helper && z_std_helper_is_fallible(helper);
  }
  return false;
}

static const char *effect_literal_type(const ZProgramGraphNode *node, char *scratch, size_t scratch_len) {
  const char *value = node ? node->value : NULL;
  if (effect_text_eq(value, "true") || effect_text_eq(value, "false")) return "Bool";
  if (value && value[0] == '"') return "String";
  if (value && (value[0] == '-' || (value[0] >= '0' && value[0] <= '9'))) {
    const char *suffix = strrchr(value, '_');
    if (suffix && (suffix[1] == 'i' || suffix[1] == 'u' || suffix[1] == 'f') && scratch && scratch_len > strlen(suffix + 1)) {
      snprintf(scratch, scratch_len, "%s", suffix + 1);
      return scratch;
    }
    // Unsuffixed numeric literals adapt to the expected type during
    // checking, so the contract treats them as unknown here.
    return "";
  }
  return "";
}

static bool effect_type_var_at(const char *type, const char *p) {
  bool left_boundary = p == type || !((p[-1] >= 'a' && p[-1] <= 'z') || (p[-1] >= 'A' && p[-1] <= 'Z') || (p[-1] >= '0' && p[-1] <= '9') || p[-1] == '_');
  char next = p[1];
  bool right_boundary = !((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || (next >= '0' && next <= '9') || next == '_');
  return left_boundary && right_boundary;
}

static const char *effect_type_var_pos(const char *type) {
  for (const char *p = type; p && *p; p++) {
    if (*p >= 'A' && *p <= 'Z' && effect_type_var_at(type, p)) return p;
  }
  return NULL;
}

static bool effect_type_has_type_var(const char *type) {
  return effect_type_var_pos(type) != NULL;
}

// Unify a stdlib template such as Span<T> against a concrete type such as
// Span<u8> and capture the binding for the single type variable.
static bool effect_bind_type_var(const char *template_type, const char *concrete, char *binding, size_t binding_len) {
  const char *var = template_type ? effect_type_var_pos(template_type) : NULL;
  if (!var || !concrete || !concrete[0] || effect_type_has_type_var(concrete)) return false;
  size_t prefix_len = (size_t)(var - template_type);
  const char *suffix = var + 1;
  size_t suffix_len = strlen(suffix);
  size_t concrete_len = strlen(concrete);
  if (concrete_len <= prefix_len + suffix_len) return false;
  if (strncmp(concrete, template_type, prefix_len) != 0) return false;
  if (strcmp(concrete + concrete_len - suffix_len, suffix) != 0) return false;
  size_t bound_len = concrete_len - prefix_len - suffix_len;
  if (bound_len + 1 > binding_len) return false;
  memcpy(binding, concrete + prefix_len, bound_len);
  binding[bound_len] = '\0';
  return true;
}

static bool effect_substitute_type_var(const char *template_type, const char *binding, char *out, size_t out_len) {
  const char *var = template_type ? effect_type_var_pos(template_type) : NULL;
  if (!var || !binding || !binding[0]) return false;
  size_t prefix_len = (size_t)(var - template_type);
  const char *suffix = var + 1;
  size_t needed = prefix_len + strlen(binding) + strlen(suffix) + 1;
  if (needed > out_len) return false;
  memcpy(out, template_type, prefix_len);
  out[prefix_len] = '\0';
  strcat(out, binding);
  strcat(out, suffix);
  return true;
}

#define EFFECT_EXPR_TYPE_MAX_DEPTH 16

static const char *effect_expr_type_at_depth(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *expr, char *scratch, size_t scratch_len, size_t depth);

// Instantiate a generic stdlib return template (Span<T>, Maybe<T>, ...) by
// unifying the helper's parameter templates against the concrete argument
// types at this call. Returns "" when no binding can be established so the
// caller treats the type as unknown instead of comparing a raw template,
// matching `zero check`, which always typechecks instantiations.
static const char *effect_stdlib_instantiated_type(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *call, const ZStdHelperInfo *helper, char *scratch, size_t scratch_len, size_t depth) {
  const char *template_type = helper && helper->return_type ? helper->return_type : "";
  if (!effect_type_has_type_var(template_type)) return template_type;
  if (!helper || depth >= EFFECT_EXPR_TYPE_MAX_DEPTH) return "";
  char binding[96] = {0};
  for (int i = 0; i < helper->arg_count && i < Z_STD_HELPER_MAX_ARGS; i++) {
    const char *param_template = helper->arg_types[i];
    if (!param_template || !effect_type_has_type_var(param_template)) continue;
    const ZProgramGraphNode *arg = effect_child(graph, call, "arg", (size_t)i);
    char arg_scratch[96] = {0};
    const char *arg_type = effect_expr_type_at_depth(graph, resolution, arg, arg_scratch, sizeof(arg_scratch), depth + 1);
    if (effect_bind_type_var(param_template, arg_type, binding, sizeof(binding))) break;
    binding[0] = '\0';
  }
  if (!binding[0]) return "";
  if (!effect_substitute_type_var(template_type, binding, scratch, scratch_len)) return "";
  return scratch;
}

static const char *effect_expr_type_at_depth(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *expr, char *scratch, size_t scratch_len, size_t depth) {
  if (!expr || depth >= EFFECT_EXPR_TYPE_MAX_DEPTH) return "";
  if (effect_text_present(expr->type) && !effect_type_has_type_var(expr->type)) return expr->type;
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_LITERAL) return effect_literal_type(expr, scratch, scratch_len);
  const ZProgramGraphResolutionReference *ref = effect_ref_for_node(resolution, expr, NULL);
  const ZProgramGraphNode *target = ref ? effect_node_by_id(graph, ref->target_node) : NULL;
  if (target && effect_text_present(target->type) && !effect_type_has_type_var(target->type)) return target->type;
  if (ref && (effect_ref_kind(ref, "stdlib") || effect_ref_kind(ref, "graphBackedStdlib"))) {
    const ZStdHelperInfo *helper = z_std_helper_find(ref->qualified_name);
    if (!helper) return "";
    return effect_stdlib_instantiated_type(graph, resolution, expr, helper, scratch, scratch_len, depth);
  }
  return "";
}

static const char *effect_expr_type(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *expr, char *scratch, size_t scratch_len) {
  return effect_expr_type_at_depth(graph, resolution, expr, scratch, scratch_len, 0);
}

static bool fail_wrong_return_type(const ZProgramGraphNode *stmt, const char *expected, const char *actual, const char *path, ZDiag *diag) {
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 3007;
    diag->path = stmt && stmt->path && stmt->path[0] ? stmt->path : path;
    diag->line = stmt && stmt->line > 0 ? stmt->line : 1;
    diag->column = stmt && stmt->column > 0 ? stmt->column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "return type does not match function return type");
    snprintf(diag->expected, sizeof(diag->expected), "%s", expected && expected[0] ? expected : "Void");
    snprintf(diag->actual, sizeof(diag->actual), "%s", actual && actual[0] ? actual : "Void");
    snprintf(diag->help, sizeof(diag->help), "return a value compatible with the function signature");
  }
  return false;
}

static bool fail_missing_return_value(const ZProgramGraphNode *fun, const char *path, ZDiag *diag) {
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 3007;
    diag->path = fun && fun->path && fun->path[0] ? fun->path : path;
    diag->line = fun && fun->line > 0 ? fun->line : 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "non-void function must return a value on every path");
    snprintf(diag->expected, sizeof(diag->expected), "%s", fun && fun->type ? fun->type : "non-Void");
    snprintf(diag->actual, sizeof(diag->actual), "function body may fall through");
    snprintf(diag->help, sizeof(diag->help), "add explicit `ret` or `raise` on every path");
  }
  return false;
}

static bool effect_maybe_accepts_present_value(const char *expected, const char *actual) { const char *prefix = "Maybe<"; size_t prefix_len = strlen(prefix), expected_len = expected ? strlen(expected) : 0; if (!expected || !actual || strncmp(expected, prefix, prefix_len) != 0 || expected_len <= prefix_len + 1 || expected[expected_len - 1] != '>') return false; size_t inner_len = expected_len - prefix_len - 1; return strlen(actual) == inner_len && strncmp(expected + prefix_len, actual, inner_len) == 0; }

static bool effect_type_apply_inner(const char *type, const char *name, char *inner, size_t inner_len) {
  size_t name_len = strlen(name);
  size_t type_len = type ? strlen(type) : 0;
  if (!type || type_len < name_len + 3) return false;
  if (strncmp(type, name, name_len) != 0 || type[name_len] != '<' || type[type_len - 1] != '>') return false;
  size_t body_len = type_len - name_len - 2;
  if (body_len + 1 > inner_len) return false;
  memcpy(inner, type + name_len + 1, body_len);
  inner[body_len] = '\0';
  return true;
}

static bool effect_type_array_element(const char *type, char *element, size_t element_len) {
  if (!type || type[0] != '[') return false;
  const char *close = strchr(type, ']');
  if (!close || !close[1]) return false;
  if (strlen(close + 1) + 1 > element_len) return false;
  snprintf(element, element_len, "%s", close + 1);
  return true;
}

static bool effect_type_is_scalar(const char *type) {
  static const char *const names[] = {"Bool", "String", "Void", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "usize", "isize", "f32", "f64", NULL};
  for (size_t i = 0; type && names[i]; i++) {
    if (effect_text_eq(type, names[i])) return true;
  }
  return false;
}

#define EFFECT_TYPE_COMPARE_MAX_DEPTH 8

// The contract only reasons about a closed set of type forms. Anything else
// (aliases, shapes, interfaces, multi-argument generics) is left to the full
// checker so the contract never rejects code that `zero check` accepts.
static bool effect_type_understood(const char *type, size_t depth) {
  if (!type || !type[0] || depth > EFFECT_TYPE_COMPARE_MAX_DEPTH) return false;
  if (effect_type_is_scalar(type)) return true;
  char inner[96];
  if (effect_type_apply_inner(type, "Span", inner, sizeof(inner)) ||
      effect_type_apply_inner(type, "MutSpan", inner, sizeof(inner)) ||
      effect_type_apply_inner(type, "Maybe", inner, sizeof(inner)) ||
      effect_type_array_element(type, inner, sizeof(inner))) {
    return effect_type_understood(inner, depth + 1);
  }
  return false;
}

static bool effect_readable_byte_span_like(const char *type) {
  return effect_text_eq(type, "String") || effect_text_eq(type, "Span<u8>") || effect_text_eq(type, "MutSpan<u8>");
}

// Mirrors the acceptance side of the checker's types_compatible relation for
// the forms the contract understands; returns true (accept) when unsure.
static bool effect_types_compatible(const char *expected, const char *actual, size_t depth) {
  if (depth > EFFECT_TYPE_COMPARE_MAX_DEPTH) return true;
  if (effect_text_eq(expected, actual)) return true;
  if (effect_maybe_accepts_present_value(expected, actual)) return true;
  if (!effect_type_understood(expected, 0) || !effect_type_understood(actual, 0)) return true;
  if ((effect_text_eq(expected, "String") || effect_text_eq(expected, "Span<u8>")) && effect_readable_byte_span_like(actual)) return true;
  char expected_inner[96];
  char actual_inner[96];
  if (effect_type_apply_inner(expected, "Span", expected_inner, sizeof(expected_inner))) {
    if (effect_type_apply_inner(actual, "Span", actual_inner, sizeof(actual_inner)) ||
        effect_type_apply_inner(actual, "MutSpan", actual_inner, sizeof(actual_inner)) ||
        effect_type_array_element(actual, actual_inner, sizeof(actual_inner))) {
      return effect_types_compatible(expected_inner, actual_inner, depth + 1);
    }
    return false;
  }
  if (effect_type_apply_inner(expected, "MutSpan", expected_inner, sizeof(expected_inner)) &&
      effect_type_apply_inner(actual, "MutSpan", actual_inner, sizeof(actual_inner))) {
    return effect_types_compatible(expected_inner, actual_inner, depth + 1);
  }
  if (effect_type_apply_inner(expected, "Maybe", expected_inner, sizeof(expected_inner)) &&
      effect_type_apply_inner(actual, "Maybe", actual_inner, sizeof(actual_inner))) {
    return effect_types_compatible(expected_inner, actual_inner, depth + 1);
  }
  return false;
}

static bool effect_function_has_return(const ZProgramGraph *graph, const ZProgramGraphNode *fun) {
  for (size_t i = 0; graph && fun && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_RETURN && effect_owner_function(graph, node) == fun) return true;
  }
  return false;
}

static bool effect_function_return_contract_ok(const ZProgramGraph *graph, const ZProgramGraphNode *fun, const char *path, ZDiag *diag) {
  if (!fun || !effect_text_present(fun->type) || effect_text_eq(fun->type, "Void")) return true;
  const ZProgramGraphEdge *owner = effect_owner_edge(graph, fun->id);
  const ZProgramGraphNode *parent = owner ? effect_node_by_id(graph, owner->from) : NULL;
  if (parent && parent->kind == Z_PROGRAM_GRAPH_NODE_INTERFACE && effect_text_eq(owner->kind, "method")) return true;
  if (effect_function_has_return(graph, fun)) return true;
  return fail_missing_return_value(fun, path, diag);
}

static bool effect_return_contract_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *stmt, const char *path, ZDiag *diag) {
  const ZProgramGraphNode *fun = effect_owner_function(graph, stmt);
  const char *expected = fun && effect_text_present(fun->type) ? fun->type : "Void";
  const ZProgramGraphNode *expr = effect_child(graph, stmt, "expr", 0);
  char actual_scratch[64] = {0};
  const char *actual = effect_expr_type(graph, resolution, expr, actual_scratch, sizeof(actual_scratch));
  if (!expr) actual = "Void";
  if (effect_type_has_type_var(expected) || effect_type_has_type_var(actual)) return true;
  if (!effect_text_present(actual) || effect_types_compatible(expected, actual, 0)) return true;
  return fail_wrong_return_type(stmt, expected, actual, path, diag);
}

static bool effect_call_checked(const ZProgramGraph *graph, const ZProgramGraphNode *call) {
  return effect_is_under_kind(graph, call, Z_PROGRAM_GRAPH_NODE_CHECK) ||
         effect_is_under_kind(graph, call, Z_PROGRAM_GRAPH_NODE_RESCUE);
}

static bool fail_unchecked_fallible_call(const ZProgramGraphNode *call, const ZProgramGraphResolutionReference *ref, const char *path, ZDiag *diag) {
  if (diag) {
    const char *name = ref && ref->qualified_name && ref->qualified_name[0] ? ref->qualified_name : (call && call->name ? call->name : "");
    *diag = (ZDiag){0};
    diag->code = 1003;
    diag->path = call && call->path && call->path[0] ? call->path : path;
    diag->line = call && call->line > 0 ? call->line : 1;
    diag->column = call && call->column > 0 ? call->column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "fallible function call must be checked");
    snprintf(diag->expected, sizeof(diag->expected), "check fallible_call ...");
    snprintf(diag->actual, sizeof(diag->actual), "call to '%s'", name);
    snprintf(diag->help, sizeof(diag->help), "prefix the call with check in a function marked with `raises`");
  }
  return false;
}

static bool effect_node_contract_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZProgramGraphNode *node, const char *path, ZDiag *diag) {
  if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && !effect_function_return_contract_ok(graph, node, path, diag)) return false;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_RETURN && !effect_return_contract_ok(graph, resolution, node, path, diag)) return false;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_CALL || node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) {
    const ZProgramGraphResolutionReference *ref = NULL;
    if (effect_call_fallible(graph, resolution, node, &ref) && !effect_call_checked(graph, node)) {
      return fail_unchecked_fallible_call(node, ref, path, diag);
    }
  }
  return true;
}

bool z_program_graph_effect_contracts_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *path, ZDiag *diag) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (!effect_node_contract_ok(graph, resolution, &graph->nodes[i], path, diag)) return false;
  }
  return true;
}

size_t z_program_graph_effect_contract_violations(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const char *path, ZDiag *out, size_t cap) {
  size_t found = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    ZDiag diag = {0};
    if (effect_node_contract_ok(graph, resolution, &graph->nodes[i], path, &diag)) continue;
    if (out && found < cap) out[found] = diag;
    found++;
  }
  return found;
}
