#include "program_graph_patch.h"
#include "program_graph_handle.h"
#include "program_graph_patch_body.h"
#include "program_graph_patch_builders.h"
#include "type_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void patch_replace_text(char **slot, const char *value) {
  if (!slot) return;
  free(*slot);
  *slot = z_strdup(value ? value : "");
}

static void patch_result_fail(ZProgramGraphPatchResult *result, const char *code, const char *message, const char *expected, const char *actual) {
  if (!result) return;
  result->ok = false;
  snprintf(result->code, sizeof(result->code), "%s", code ? code : "GPH000");
  snprintf(result->message, sizeof(result->message), "%s", message ? message : "program graph patch failed");
  patch_replace_text(&result->expected, expected);
  patch_replace_text(&result->actual, actual);
}

static void patch_op_fail(ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *code, const char *message, const char *expected, const char *actual) {
  char *expected_copy = z_strdup(expected ? expected : "");
  char *actual_copy = z_strdup(actual ? actual : "");
  if (op) {
    op->ok = false;
    snprintf(op->code, sizeof(op->code), "%s", code ? code : "GPH000");
    snprintf(op->message, sizeof(op->message), "%s", message ? message : "program graph patch operation failed");
    patch_replace_text(&op->expected, expected_copy);
    patch_replace_text(&op->actual, actual_copy);
  }
  patch_result_fail(result, code, message, expected_copy, actual_copy);
  free(expected_copy);
  free(actual_copy);
}

static bool patch_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool patch_parse_bool(const char *text, bool *out) {
  if (strcmp(text ? text : "", "true") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(text ? text : "", "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

static bool patch_parse_node_kind(const char *text, ZProgramGraphNodeKind *out) {
  for (int kind = Z_PROGRAM_GRAPH_NODE_MODULE; kind <= Z_PROGRAM_GRAPH_NODE_STATEMENT; kind++) {
    if (patch_text_eq(text, z_program_graph_node_kind_name((ZProgramGraphNodeKind)kind))) {
      *out = (ZProgramGraphNodeKind)kind;
      return true;
    }
  }
  return false;
}

static bool patch_parse_edge_target(const char *text, ZProgramGraphEdgeTarget *out) {
  for (int target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE; target <= Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT; target++) {
    if (patch_text_eq(text, z_program_graph_edge_target_name((ZProgramGraphEdgeTarget)target))) {
      *out = (ZProgramGraphEdgeTarget)target;
      return true;
    }
  }
  return false;
}

static bool patch_node_id_valid(const char *text) {
  if (!text || text[0] != '#' || !text[1]) return false;
  for (const char *cursor = text + 1; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.')) return false;
  }
  return true;
}

static bool patch_edge_kind_valid(const char *text) {
  if (!text || !text[0]) return false;
  for (const unsigned char *cursor = (const unsigned char *)text; *cursor; cursor++) {
    if (!(isalnum(*cursor) || *cursor == '_' || *cursor == '-')) return false;
  }
  return true;
}

static ZProgramGraphNode *patch_find_node(ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (patch_text_eq(graph->nodes[i].id, node_id)) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphNode *patch_find_node_const(const ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (patch_text_eq(graph->nodes[i].id, node_id)) return &graph->nodes[i];
  }
  return NULL;
}

/* Resolves a node handle (full id, segment, or unique segment prefix) and
 * reports GPH003/GPH004 with the nearest existing handle on failure. */
static ZProgramGraphNode *patch_resolve_node_handle(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *handle, const char *role) {
  bool ambiguous = false;
  const ZProgramGraphNode *node = z_program_graph_resolve_handle(graph, handle, &ambiguous);
  if (node) return (ZProgramGraphNode *)node;
  char message[160];
  if (ambiguous) {
    snprintf(message, sizeof(message), "patch %s handle is ambiguous", role ? role : "node");
    patch_op_fail(result, op, "GPH003", message, "a unique node id or handle prefix; zero view --fn <name> --handles prints them", handle);
    return NULL;
  }
  const char *nearest = z_program_graph_nearest_handle(graph, handle);
  if (nearest) snprintf(message, sizeof(message), "patch %s was not found; nearest: %s", role ? role : "node", nearest);
  else snprintf(message, sizeof(message), "patch %s was not found", role ? role : "node");
  patch_op_fail(result, op, "GPH004", message, handle, "");
  return NULL;
}

static bool patch_symbol_exists(const ZProgramGraph *graph, const char *symbol_id) {
  if (!symbol_id || !symbol_id[0]) return false;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].symbol_id && patch_text_eq(graph->nodes[i].symbol_id, symbol_id)) return true;
  }
  return false;
}

static bool patch_type_exists(const ZProgramGraph *graph, const char *type_id) {
  if (!type_id || !type_id[0]) return false;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].type_id && patch_text_eq(graph->nodes[i].type_id, type_id)) return true;
  }
  return false;
}

static bool patch_effect_exists(const ZProgramGraph *graph, const char *effect_id) {
  if (!effect_id || !effect_id[0]) return false;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].effect_id && patch_text_eq(graph->nodes[i].effect_id, effect_id)) return true;
  }
  return false;
}

static bool patch_edge_target_exists(const ZProgramGraph *graph, ZProgramGraphEdgeTarget target, const char *to) {
  switch (target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE: return patch_find_node_const(graph, to) != NULL;
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL: return patch_symbol_exists(graph, to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE: return patch_type_exists(graph, to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT: return patch_effect_exists(graph, to);
  }
  return false;
}

static void patch_reserve_nodes(ZProgramGraph *graph, size_t len) {
  if (graph->node_cap >= len) return;
  size_t next = graph->node_cap ? graph->node_cap * 2 : 8;
  while (next < len) next *= 2;
  graph->nodes = z_checked_reallocarray(graph->nodes, next, sizeof(ZProgramGraphNode));
  for (size_t i = graph->node_cap; i < next; i++) graph->nodes[i] = (ZProgramGraphNode){0};
  graph->node_cap = next;
}

static void patch_reserve_edges(ZProgramGraph *graph, size_t len) {
  if (graph->edge_cap >= len) return;
  size_t next = graph->edge_cap ? graph->edge_cap * 2 : 8;
  while (next < len) next *= 2;
  graph->edges = z_checked_reallocarray(graph->edges, next, sizeof(ZProgramGraphEdge));
  for (size_t i = graph->edge_cap; i < next; i++) graph->edges[i] = (ZProgramGraphEdge){0};
  graph->edge_cap = next;
}

static ZProgramGraphNode *patch_append_node(ZProgramGraph *graph) {
  patch_reserve_nodes(graph, graph->node_len + 1);
  ZProgramGraphNode *node = &graph->nodes[graph->node_len++];
  *node = (ZProgramGraphNode){0};
  return node;
}

static ZProgramGraphEdge *patch_append_edge(ZProgramGraph *graph) {
  patch_reserve_edges(graph, graph->edge_len + 1);
  ZProgramGraphEdge *edge = &graph->edges[graph->edge_len++];
  *edge = (ZProgramGraphEdge){0};
  return edge;
}

static bool patch_duplicate_ordered_edge(const ZProgramGraph *graph, const char *from, const char *kind, ZProgramGraphEdgeTarget target, size_t order) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == target && edge->order == order && patch_text_eq(edge->from, from) && patch_text_eq(edge->kind, kind)) return true;
  }
  return false;
}

static char **patch_node_text_field(ZProgramGraphNode *node, const char *field) {
  if (!node || !field) return NULL;
  if (strcmp(field, "name") == 0) return &node->name;
  if (strcmp(field, "type") == 0) return &node->type;
  if (strcmp(field, "value") == 0) return &node->value;
  return NULL;
}

static bool *patch_node_bool_field(ZProgramGraphNode *node, const char *field) {
  if (!node || !field) return NULL;
  if (strcmp(field, "public") == 0) return &node->is_public;
  if (strcmp(field, "mutable") == 0) return &node->is_mutable;
  if (strcmp(field, "static") == 0) return &node->is_static;
  if (strcmp(field, "fallible") == 0) return &node->fallible;
  if (strcmp(field, "exportC") == 0) return &node->export_c;
  return NULL;
}

static bool patch_name_operator_valid(const char *text) {
  static const char *operators[] = {
    "+", "-", "*", "/", "%", "&&", "||", "==", "!=", "<", "<=", ">", ">=", "+%", "+|", NULL,
  };
  for (size_t i = 0; operators[i]; i++) {
    if (patch_text_eq(text, operators[i])) return true;
  }
  return false;
}

static bool patch_name_segment_char(char ch) {
  return isalnum((unsigned char)ch) || ch == '_';
}

static bool patch_name_value_valid(const char *text) {
  if (!text || !text[0]) return true;
  if (patch_name_operator_valid(text)) return true;
  const char *cursor = text;
  while (*cursor) {
    if (!(isalpha((unsigned char)*cursor) || *cursor == '_')) return false;
    cursor++;
    while (patch_name_segment_char(*cursor)) cursor++;
    if (*cursor == '.') {
      cursor++;
      if (!*cursor) return false;
      continue;
    }
    return *cursor == '\0';
  }
  return true;
}

static bool patch_identifier_value_valid(const char *text) {
  if (!text || !text[0]) return true;
  const char *cursor = text;
  if (!(isalpha((unsigned char)*cursor) || *cursor == '_')) return false;
  cursor++;
  while (patch_name_segment_char(*cursor)) cursor++;
  return *cursor == '\0';
}

static bool patch_text_has_control(const char *text) {
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    if (*cursor < 0x20 || *cursor == 0x7f) return true;
  }
  return false;
}

static bool patch_type_value_valid(const char *text) {
  if (!text || !text[0]) return true;
  if (patch_text_has_control(text)) return false;
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId type = Z_TYPE_ID_INVALID;
  ZTypeParseError error = {0};
  bool ok = z_type_parse(&arena, text, &type, &error);
  z_type_arena_free(&arena);
  return ok;
}

static bool patch_validate_text_field(const ZProgramGraphNode *node, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *field, const char *value) {
  if (patch_text_eq(field, "name") && !patch_name_value_valid(value)) {
    patch_op_fail(result, op, "GPH003", "patch name value must be a Zero identifier path or operator", "identifier path or operator", value);
    return false;
  }
  if (patch_text_eq(field, "type") && !patch_type_value_valid(value)) {
    patch_op_fail(result, op, "GPH003", "patch type value must be valid Zero type syntax", "Zero type syntax", value);
    return false;
  }
  if (node && node->kind == Z_PROGRAM_GRAPH_NODE_MATCH_ARM && patch_text_eq(field, "value") && !patch_name_value_valid(value)) {
    patch_op_fail(result, op, "GPH003", "patch match payload value must be a Zero identifier path or operator", "identifier path or operator", value);
    return false;
  }
  if (node && node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT && patch_text_eq(field, "value") && !patch_identifier_value_valid(value)) {
    patch_op_fail(result, op, "GPH003", "patch import alias value must be a Zero identifier", "identifier", value);
    return false;
  }
  return true;
}

static bool patch_validate_text_value(const ZProgramGraphNode *node, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  return patch_validate_text_field(node, result, op, op->field, op->value);
}

static bool patch_validate_node_payload(const ZProgramGraphNode *node, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (node->name) {
    if (!patch_validate_text_field(node, result, op, "name", node->name)) return false;
  }
  if (node->type) {
    if (!patch_validate_text_field(node, result, op, "type", node->type)) return false;
  }
  if (node->value) {
    if (!patch_validate_text_field(node, result, op, "value", node->value)) return false;
  }
  return true;
}

static void patch_copy_node_attrs(ZProgramGraphNode *node, const ZProgramGraphPatchOpResult *op) {
  if (op->name) patch_replace_text(&node->name, op->name);
  if (op->type) patch_replace_text(&node->type, op->type);
  if (op->value) patch_replace_text(&node->value, op->value);
  if (op->path) patch_replace_text(&node->path, op->path);
  if (op->has_line_value) node->line = op->line_value;
  if (op->has_column_value) node->column = op->column_value;
  if (op->has_public_value) node->is_public = op->public_value;
  if (op->has_mutable_value) node->is_mutable = op->mutable_value;
  if (op->has_static_value) node->is_static = op->static_value;
  if (op->has_fallible_value) node->fallible = op->fallible_value;
  if (op->has_export_c_value) node->export_c = op->export_c_value;
}

static const char *patch_node_path(const ZProgramGraphNode *node) {
  return node && node->path && node->path[0] ? node->path : "src/main.0";
}

static const char *patch_operator_segment(char ch) {
  switch (ch) {
    case '+': return "plus";
    case '-': return "minus";
    case '*': return "mul";
    case '/': return "div";
    case '%': return "mod";
    case '=': return "eq";
    case '<': return "lt";
    case '>': return "gt";
    case '&': return "and";
    case '|': return "or";
    case '!': return "not";
    default: return NULL;
  }
}

static void patch_append_id_segment(ZBuf *buf, const char *text) {
  bool wrote = false;
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    if (isalnum(*cursor) || *cursor == '_') {
      zbuf_append_char(buf, (char)*cursor);
      wrote = true;
      continue;
    }
    const char *word = patch_operator_segment((char)*cursor);
    if (word) {
      if (wrote && buf->len > 0 && buf->data[buf->len - 1] != '_') zbuf_append_char(buf, '_');
      zbuf_append(buf, word);
      zbuf_append_char(buf, '_');
      wrote = true;
    } else if (wrote && buf->len > 0 && buf->data[buf->len - 1] != '_') {
      zbuf_append_char(buf, '_');
    }
  }
  while (buf->len > 0 && buf->data[buf->len - 1] == '_') buf->data[--buf->len] = 0;
  if (!wrote || buf->len == 0 || (buf->data && buf->data[buf->len - 1] == '#')) zbuf_append(buf, "node");
}

static char *patch_generated_id_base(const char *prefix, const char *first, const char *second, size_t ordinal, bool has_ordinal) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append_char(&buf, '#');
  zbuf_append(&buf, prefix && prefix[0] ? prefix : "node");
  if (first && first[0]) {
    zbuf_append_char(&buf, '_');
    patch_append_id_segment(&buf, first);
  }
  if (second && second[0]) {
    zbuf_append_char(&buf, '_');
    patch_append_id_segment(&buf, second);
  }
  if (has_ordinal) zbuf_appendf(&buf, "_%zu", ordinal);
  return buf.data ? buf.data : z_strdup("#node");
}

static char *patch_generated_unique_id(ZProgramGraph *graph, const char *prefix, const char *first, const char *second, size_t ordinal, bool has_ordinal) {
  char *base = patch_generated_id_base(prefix, first, second, ordinal, has_ordinal);
  if (!patch_find_node(graph, base)) return base;
  for (size_t suffix = 1; suffix < 100000; suffix++) {
    ZBuf candidate;
    zbuf_init(&candidate);
    zbuf_append(&candidate, base);
    zbuf_appendf(&candidate, "_%zu", suffix);
    if (!patch_find_node(graph, candidate.data)) {
      free(base);
      return candidate.data ? candidate.data : z_strdup("#node");
    }
    zbuf_free(&candidate);
  }
  free(base);
  return z_strdup("#node_conflict");
}

static ZProgramGraphNode *patch_append_owned_node(
  ZProgramGraph *graph,
  const char *id,
  ZProgramGraphNodeKind kind,
  const char *path,
  const char *name,
  const char *type,
  const char *value,
  bool is_public,
  bool is_mutable,
  bool is_static,
  bool fallible,
  bool export_c
) {
  ZProgramGraphNode *node = patch_append_node(graph);
  node->id = z_strdup(id);
  node->kind = kind;
  node->path = z_strdup(path && path[0] ? path : "src/main.0");
  if (name) node->name = z_strdup(name);
  if (type) node->type = z_strdup(type);
  if (value) node->value = z_strdup(value);
  node->line = 1;
  node->column = 1;
  node->is_public = is_public;
  node->is_mutable = is_mutable;
  node->is_static = is_static;
  node->fallible = fallible;
  node->export_c = export_c;
  return node;
}

static bool patch_append_owned_edge_checked(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *from, const char *kind, const char *to, size_t order) {
  if (patch_duplicate_ordered_edge(graph, from, kind, Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, order)) {
    patch_op_fail(result, op, "GPH005", "patch edge order is already occupied", "unused ordered edge slot", kind);
    return false;
  }
  ZProgramGraphEdge *edge = patch_append_edge(graph);
  edge->from = z_strdup(from);
  edge->to = z_strdup(to);
  edge->kind = z_strdup(kind);
  edge->target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE;
  edge->order = order;
  return true;
}

static size_t patch_next_edge_order(const ZProgramGraph *graph, const char *from, const char *kind) {
  size_t next = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && patch_text_eq(edge->from, from) && patch_text_eq(edge->kind, kind) && edge->order >= next) next = edge->order + 1;
  }
  return next;
}

static ZProgramGraphNode *patch_find_module_for_op(ZProgramGraph *graph, const ZProgramGraphPatchOpResult *op, ZProgramGraphPatchResult *result) {
  ZProgramGraphNode *found = NULL;
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    if (op && op->path && op->path[0] && !patch_text_eq(node->path, op->path)) continue;
    found = node;
    count++;
  }
  if (count == 1) return found;
  if (count == 0) patch_result_fail(result, "GPH004", "patch module was not found", op && op->path ? op->path : "single Module node", "");
  else patch_result_fail(result, "GPH003", "patch operation needs a module path", "one target module or path attribute", "multiple Module nodes");
  return NULL;
}

static ZProgramGraphNode *patch_find_function_named(ZProgramGraph *graph, const char *name, size_t *count) {
  ZProgramGraphNode *found = NULL;
  size_t matches = 0;
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && patch_text_eq(node->name, name)) {
      found = node;
      matches++;
    }
  }
  if (count) *count = matches;
  return matches == 1 ? found : NULL;
}

static ZProgramGraphNode *patch_require_function(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *name) {
  size_t count = 0;
  ZProgramGraphNode *function = patch_find_function_named(graph, name, &count);
  if (function) return function;
  if (count > 1) {
    patch_op_fail(result, op, "GPH003", "patch function name is ambiguous", name, "");
    return NULL;
  }
  const ZProgramGraphNode *nearest = NULL;
  size_t nearest_distance = 0;
  size_t threshold = (name ? strlen(name) : 0) / 3 + 2;
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || !node->name || !node->name[0]) continue;
    size_t distance = z_program_graph_handle_distance(name, node->name);
    if (distance > threshold) continue;
    if (!nearest || distance < nearest_distance) {
      nearest = node;
      nearest_distance = distance;
    }
  }
  char message[160];
  if (nearest) snprintf(message, sizeof(message), "patch function was not found; nearest: %s %s", nearest->name, nearest->id ? nearest->id : "");
  else snprintf(message, sizeof(message), "patch function was not found");
  patch_op_fail(result, op, "GPH004", message, name, "");
  return NULL;
}

static ZProgramGraphNode *patch_find_child_node(ZProgramGraph *graph, const char *from, const char *kind) {
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, from) || !patch_text_eq(edge->kind, kind)) continue;
    return patch_find_node(graph, edge->to);
  }
  return NULL;
}

static ZProgramGraphNode *patch_require_body(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const ZProgramGraphNode *function) {
  ZProgramGraphNode *body = patch_find_child_node(graph, function ? function->id : NULL, "body");
  if (body && body->kind == Z_PROGRAM_GRAPH_NODE_BLOCK) return body;
  patch_op_fail(result, op, "GPH004", "patch function body was not found", "body Block node", function && function->name ? function->name : "");
  return NULL;
}

static bool patch_function_has_param_named(const ZProgramGraph *graph, const ZProgramGraphNode *function, const char *name) {
  for (size_t i = 0; graph && function && name && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !patch_text_eq(edge->from, function->id) || !patch_text_eq(edge->kind, "param")) continue;
    const ZProgramGraphNode *param = patch_find_node_const(graph, edge->to);
    if (param && param->kind == Z_PROGRAM_GRAPH_NODE_PARAM && patch_text_eq(param->name, name)) return true;
  }
  return false;
}

static bool patch_add_type_ref(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *owner_id, const char *owner_name, const char *edge_kind, const char *type, const char *path) {
  if (!patch_type_value_valid(type)) {
    patch_op_fail(result, op, "GPH003", "patch type value must be valid Zero type syntax", "Zero type syntax", type);
    return false;
  }
  char *id = patch_generated_unique_id(graph, "type", owner_name, edge_kind, 0, false);
  patch_append_owned_node(graph, id, Z_PROGRAM_GRAPH_NODE_TYPE_REF, path, NULL, type, NULL, false, false, false, false, false);
  bool ok = patch_append_owned_edge_checked(graph, result, op, owner_id, edge_kind, id, 0);
  free(id);
  return ok;
}

static bool patch_add_effect_ref(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *owner_id, const char *owner_name, const char *path) {
  char *id = patch_generated_unique_id(graph, "effect", owner_name, "error", 0, false);
  patch_append_owned_node(graph, id, Z_PROGRAM_GRAPH_NODE_EFFECT_REF, path, "error", NULL, NULL, false, false, false, false, false);
  bool ok = patch_append_owned_edge_checked(graph, result, op, owner_id, "effect", id, 0);
  free(id);
  return ok;
}

static bool patch_create_function(
  ZProgramGraph *graph,
  ZProgramGraphPatchResult *result,
  ZProgramGraphPatchOpResult *op,
  const char *name,
  const char *return_type,
  const char *test_name,
  bool is_public,
  bool fallible,
  ZProgramGraphNode **out_function
) {
  if (!patch_identifier_value_valid(name)) {
    patch_op_fail(result, op, "GPH003", "function name must be a Zero identifier", "identifier", name);
    return false;
  }
  if (!patch_type_value_valid(return_type)) {
    patch_op_fail(result, op, "GPH003", "function return type must be valid Zero type syntax", "Zero type syntax", return_type);
    return false;
  }
  size_t duplicate_count = 0;
  patch_find_function_named(graph, name, &duplicate_count);
  if (duplicate_count > 0) {
    patch_op_fail(result, op, "GPH005", "function already exists", "unused function name", name);
    return false;
  }
  ZProgramGraphNode *module = patch_find_module_for_op(graph, op, result);
  if (!module) return false;
  const char *path = op && op->path ? op->path : patch_node_path(module);
  char *module_id = z_strdup(module->id ? module->id : "");
  char *path_copy = z_strdup(path);
  char *fn_id = patch_generated_unique_id(graph, "fn", name, NULL, 0, false);
  char *body_id = patch_generated_unique_id(graph, "block", name, "body", 0, false);
  patch_append_owned_node(graph, fn_id, Z_PROGRAM_GRAPH_NODE_FUNCTION, path_copy, name, return_type, test_name, is_public, false, false, fallible, false);
  size_t function_order = patch_next_edge_order(graph, module_id, "function");
  bool ok = patch_append_owned_edge_checked(graph, result, op, module_id, "function", fn_id, function_order);
  if (ok) ok = patch_add_type_ref(graph, result, op, fn_id, name, "returnType", return_type, path_copy);
  if (ok && fallible) ok = patch_add_effect_ref(graph, result, op, fn_id, name, path_copy);
  patch_append_owned_node(graph, body_id, Z_PROGRAM_GRAPH_NODE_BLOCK, path_copy, "body", NULL, NULL, false, false, false, false, false);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, fn_id, "body", body_id, 0);
  free(module_id);
  free(path_copy);
  free(fn_id);
  free(body_id);
  if (!ok) return false;
  if (out_function) *out_function = patch_require_function(graph, result, op, name);
  return true;
}

static bool patch_apply_add_function(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  const char *return_type = op->type && op->type[0] ? op->type : "Void";
  bool is_public = op->has_public_value && op->public_value;
  bool fallible = op->has_fallible_value && op->fallible_value;
  if (!patch_create_function(graph, result, op, op->name, return_type, NULL, is_public, fallible, NULL)) return false;
  op->ok = true;
  return true;
}

static bool patch_apply_add_main(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  const char *name = op->name && op->name[0] ? op->name : "main";
  const char *return_type = op->type && op->type[0] ? op->type : "Void";
  ZProgramGraphNode *function = NULL;
  if (!patch_create_function(graph, result, op, name, return_type, NULL, true, true, &function)) return false;
  char *function_id = z_strdup(function && function->id ? function->id : "");
  char *path = z_strdup(patch_node_path(function));
  char *param_id = patch_generated_unique_id(graph, "param", name, "world", 0, false);
  patch_append_owned_node(graph, param_id, Z_PROGRAM_GRAPH_NODE_PARAM, path, "world", "World", NULL, false, false, false, false, false);
  bool ok = patch_append_owned_edge_checked(graph, result, op, function_id, "param", param_id, 0);
  if (ok) ok = patch_add_type_ref(graph, result, op, param_id, "main_world", "type", "World", path);
  free(function_id);
  free(path);
  free(param_id);
  if (!ok) return false;
  op->ok = true;
  return true;
}

static bool patch_apply_add_param(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *function = patch_require_function(graph, result, op, op->function);
  if (!function) return false;
  if (!patch_identifier_value_valid(op->name)) {
    patch_op_fail(result, op, "GPH003", "parameter name must be a Zero identifier", "identifier", op->name);
    return false;
  }
  if (patch_function_has_param_named(graph, function, op->name)) {
    patch_op_fail(result, op, "GPH005", "parameter already exists", "unused parameter name", op->name);
    return false;
  }
  if (!patch_type_value_valid(op->type)) {
    patch_op_fail(result, op, "GPH003", "parameter type must be valid Zero type syntax", "Zero type syntax", op->type);
    return false;
  }
  char *function_id = z_strdup(function->id ? function->id : "");
  char *function_name = z_strdup(function->name ? function->name : "");
  char *path = z_strdup(op->path && op->path[0] ? op->path : patch_node_path(function));
  char *param_id = patch_generated_unique_id(graph, "param", function_name, op->name, 0, false);
  patch_append_owned_node(graph, param_id, Z_PROGRAM_GRAPH_NODE_PARAM, path, op->name, op->type, NULL, false, false, false, false, false);
  size_t order = patch_next_edge_order(graph, function_id, "param");
  bool ok = patch_append_owned_edge_checked(graph, result, op, function_id, "param", param_id, order);
  if (ok) ok = patch_add_type_ref(graph, result, op, param_id, op->name, "type", op->type, path);
  free(function_id);
  free(function_name);
  free(path);
  free(param_id);
  if (!ok) return false;
  op->ok = true;
  return true;
}

static bool patch_append_identifier(ZProgramGraph *graph, const char *id, const char *path, const char *name, const char *type) {
  patch_append_owned_node(graph, id, Z_PROGRAM_GRAPH_NODE_IDENTIFIER, path, name, type, NULL, false, false, false, false, false);
  return true;
}

static bool patch_append_literal(ZProgramGraph *graph, const char *id, const char *path, const char *type, const char *value) {
  patch_append_owned_node(graph, id, Z_PROGRAM_GRAPH_NODE_LITERAL, path, NULL, type, value, false, false, false, false, false);
  return true;
}

/* setReturnType fn="<name>" type="<t>": updates the function's declared
 * return type and its returnType TypeRef child; the batch-level revalidation
 * pipeline checks the body against the new type. */
static bool patch_apply_set_return_type(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *function = patch_require_function(graph, result, op, op->function);
  if (!function) return false;
  if (!op->type || !op->type[0] || !patch_type_value_valid(op->type)) {
    patch_op_fail(result, op, "GPH003", "function return type must be valid Zero type syntax", "Zero type syntax", op->type);
    return false;
  }
  patch_replace_text(&op->actual, function->type);
  patch_replace_text(&function->type, op->type);
  ZProgramGraphNode *return_type = patch_find_child_node(graph, function->id, "returnType");
  if (return_type) {
    patch_replace_text(&return_type->type, op->type);
  } else {
    char *fn_id = z_strdup(function->id ? function->id : "");
    char *fn_name = z_strdup(function->name ? function->name : "");
    char *path = z_strdup(patch_node_path(function));
    bool ok = patch_add_type_ref(graph, result, op, fn_id, fn_name, "returnType", op->type, path);
    free(fn_id);
    free(fn_name);
    free(path);
    if (!ok) return false;
  }
  op->ok = true;
  return true;
}

static bool patch_apply_add_return_binary(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *function = patch_require_function(graph, result, op, op->function);
  if (!function) return false;
  ZProgramGraphNode *body = patch_require_body(graph, result, op, function);
  if (!body) return false;
  if (!patch_name_operator_valid(op->name)) {
    patch_op_fail(result, op, "GPH003", "binary return operation must name a Zero operator", "operator", op->name);
    return false;
  }
  if (!patch_identifier_value_valid(op->left) || !patch_identifier_value_valid(op->right)) {
    patch_op_fail(result, op, "GPH003", "binary return operands must name identifiers", "identifier operands", "");
    return false;
  }
  char *function_name = z_strdup(function->name ? function->name : "");
  char *body_id = z_strdup(body->id ? body->id : "");
  char *expr_type_copy = z_strdup(op->type && op->type[0] ? op->type : (function->type && function->type[0] ? function->type : "Unknown"));
  char *path = z_strdup(op->path && op->path[0] ? op->path : patch_node_path(function));
  size_t order = patch_next_edge_order(graph, body_id, "statement");
  char *stmt_id = patch_generated_unique_id(graph, "stmt", function_name, "return", order, true);
  char *call_id = patch_generated_unique_id(graph, "expr", function_name, "return_call", order, true);
  char *left_id = patch_generated_unique_id(graph, "expr", function_name, "return_left", order, true);
  char *right_id = patch_generated_unique_id(graph, "expr", function_name, "return_right", order, true);
  patch_append_owned_node(graph, stmt_id, Z_PROGRAM_GRAPH_NODE_RETURN, path, NULL, NULL, NULL, false, false, false, false, false);
  patch_append_owned_node(graph, call_id, Z_PROGRAM_GRAPH_NODE_CALL, path, op->name, expr_type_copy, NULL, false, false, false, false, false);
  patch_append_identifier(graph, left_id, path, op->left, expr_type_copy);
  patch_append_identifier(graph, right_id, path, op->right, expr_type_copy);
  bool ok = patch_append_owned_edge_checked(graph, result, op, call_id, "left", left_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, call_id, "right", right_id, 1);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, stmt_id, "expr", call_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, body_id, "statement", stmt_id, order);
  free(function_name);
  free(body_id);
  free(expr_type_copy);
  free(path);
  free(stmt_id);
  free(call_id);
  free(left_id);
  free(right_id);
  if (!ok) return false;
  op->ok = true;
  return true;
}

static bool patch_apply_add_check_write(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *function = patch_require_function(graph, result, op, op->function);
  if (!function) return false;
  ZProgramGraphNode *body = patch_require_body(graph, result, op, function);
  if (!body) return false;
  const char *receiver = op->left && op->left[0] ? op->left : "world";
  if (!patch_identifier_value_valid(receiver)) {
    patch_op_fail(result, op, "GPH003", "check write receiver must name an identifier", "identifier", receiver);
    return false;
  }
  char *function_name = z_strdup(function->name ? function->name : "");
  char *body_id = z_strdup(body->id ? body->id : "");
  char *path = z_strdup(op->path && op->path[0] ? op->path : patch_node_path(function));
  size_t order = patch_next_edge_order(graph, body_id, "statement");
  char *stmt_id = patch_generated_unique_id(graph, "stmt", function_name, "check_write", order, true);
  char *call_id = patch_generated_unique_id(graph, "expr", function_name, "write_call", order, true);
  char *write_field_id = patch_generated_unique_id(graph, "expr", function_name, "write_field", order, true);
  char *out_field_id = patch_generated_unique_id(graph, "expr", function_name, "out_field", order, true);
  char *receiver_id = patch_generated_unique_id(graph, "expr", function_name, "write_receiver", order, true);
  char *literal_id = patch_generated_unique_id(graph, "expr", function_name, "write_text", order, true);
  patch_append_owned_node(graph, stmt_id, Z_PROGRAM_GRAPH_NODE_CHECK, path, NULL, NULL, NULL, false, false, false, false, false);
  patch_append_owned_node(graph, call_id, Z_PROGRAM_GRAPH_NODE_METHOD_CALL, path, "write", "Void", NULL, false, false, false, false, false);
  patch_append_owned_node(graph, write_field_id, Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS, path, "write", NULL, NULL, false, false, false, false, false);
  patch_append_owned_node(graph, out_field_id, Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS, path, "out", NULL, NULL, false, false, false, false, false);
  patch_append_identifier(graph, receiver_id, path, receiver, NULL);
  patch_append_literal(graph, literal_id, path, "String", op->value);
  bool ok = patch_append_owned_edge_checked(graph, result, op, out_field_id, "left", receiver_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, write_field_id, "left", out_field_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, call_id, "left", write_field_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, call_id, "arg", literal_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, stmt_id, "expr", call_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, body_id, "statement", stmt_id, order);
  free(function_name);
  free(body_id);
  free(path);
  free(stmt_id);
  free(call_id);
  free(write_field_id);
  free(out_field_id);
  free(receiver_id);
  free(literal_id);
  if (!ok) return false;
  op->ok = true;
  return true;
}

static size_t patch_next_test_index(const ZProgramGraph *graph) {
  size_t next = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || !node->name || strncmp(node->name, "__zero_test_", strlen("__zero_test_")) != 0) continue;
    next++;
  }
  while (true) {
    char name[64];
    snprintf(name, sizeof(name), "__zero_test_%zu", next);
    size_t count = 0;
    patch_find_function_named((ZProgramGraph *)graph, name, &count);
    if (count == 0) return next;
    next++;
  }
}

static bool patch_apply_add_test(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!patch_identifier_value_valid(op->call)) {
    patch_op_fail(result, op, "GPH003", "test call target must be a Zero identifier", "identifier", op->call);
    return false;
  }
  const char *value = op->value ? op->value : op->expected;
  const char *expr_type = op->type && op->type[0] ? op->type : "i32";
  size_t test_index = patch_next_test_index(graph);
  char test_function_name[64];
  snprintf(test_function_name, sizeof(test_function_name), "__zero_test_%zu", test_index);
  ZProgramGraphNode *function = NULL;
  if (!patch_create_function(graph, result, op, test_function_name, "Void", op->name, false, false, &function)) return false;
  ZProgramGraphNode *body = patch_require_body(graph, result, op, function);
  if (!body) return false;
  char *function_name = z_strdup(function->name ? function->name : "");
  char *body_id = z_strdup(body->id ? body->id : "");
  char *path = z_strdup(op->path && op->path[0] ? op->path : patch_node_path(function));
  size_t order = patch_next_edge_order(graph, body_id, "statement");
  char *stmt_id = patch_generated_unique_id(graph, "stmt", function_name, "expect", order, true);
  char *expect_call_id = patch_generated_unique_id(graph, "expr", function_name, "expect_call", order, true);
  char *expect_ident_id = patch_generated_unique_id(graph, "expr", function_name, "expect_ident", order, true);
  char *eq_call_id = patch_generated_unique_id(graph, "expr", function_name, "eq_call", order, true);
  char *subject_call_id = patch_generated_unique_id(graph, "expr", function_name, "subject_call", order, true);
  char *subject_ident_id = patch_generated_unique_id(graph, "expr", function_name, "subject_ident", order, true);
  char *arg0_id = patch_generated_unique_id(graph, "expr", function_name, "arg0", order, true);
  char *arg1_id = patch_generated_unique_id(graph, "expr", function_name, "arg1", order, true);
  char *want_id = patch_generated_unique_id(graph, "expr", function_name, "want", order, true);
  patch_append_owned_node(graph, stmt_id, Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT, path, NULL, NULL, NULL, false, false, false, false, false);
  patch_append_owned_node(graph, expect_call_id, Z_PROGRAM_GRAPH_NODE_CALL, path, "expect", "Void", NULL, false, false, false, false, false);
  patch_append_identifier(graph, expect_ident_id, path, "expect", NULL);
  patch_append_owned_node(graph, eq_call_id, Z_PROGRAM_GRAPH_NODE_CALL, path, "==", "Bool", NULL, false, false, false, false, false);
  patch_append_owned_node(graph, subject_call_id, Z_PROGRAM_GRAPH_NODE_CALL, path, op->call, expr_type, NULL, false, false, false, false, false);
  patch_append_identifier(graph, subject_ident_id, path, op->call, NULL);
  patch_append_literal(graph, arg0_id, path, expr_type, op->arg0);
  patch_append_literal(graph, arg1_id, path, expr_type, op->arg1);
  patch_append_literal(graph, want_id, path, expr_type, value);
  bool ok = patch_append_owned_edge_checked(graph, result, op, expect_call_id, "left", expect_ident_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, subject_call_id, "left", subject_ident_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, subject_call_id, "arg", arg0_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, subject_call_id, "arg", arg1_id, 1);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, eq_call_id, "left", subject_call_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, eq_call_id, "right", want_id, 1);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, expect_call_id, "arg", eq_call_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, stmt_id, "expr", expect_call_id, 0);
  if (ok) ok = patch_append_owned_edge_checked(graph, result, op, body_id, "statement", stmt_id, order);
  free(function_name);
  free(body_id);
  free(path);
  free(stmt_id);
  free(expect_call_id);
  free(expect_ident_id);
  free(eq_call_id);
  free(subject_call_id);
  free(subject_ident_id);
  free(arg0_id);
  free(arg1_id);
  free(want_id);
  if (!ok) return false;
  op->ok = true;
  return true;
}

static bool patch_apply_insert_edge(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphEdgeTarget target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE;
  if (!patch_parse_edge_target(op->target, &target)) {
    patch_op_fail(result, op, "GPH003", "patch edge target must name a ProgramGraph target domain", "node, symbol, type, or effect", op->target);
    return false;
  }
  if (!patch_edge_kind_valid(op->edge)) {
    patch_op_fail(result, op, "GPH003", "patch edge kind must be a simple ProgramGraph edge name", "edge identifier", op->edge);
    return false;
  }
  ZProgramGraphNode *from_node = patch_resolve_node_handle(graph, result, op, op->from, "edge source");
  if (!from_node) return false;
  const char *to_id = op->to;
  if (target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) {
    ZProgramGraphNode *to_node = patch_resolve_node_handle(graph, result, op, op->to, "edge target");
    if (!to_node) return false;
    to_id = to_node->id;
  } else if (!patch_edge_target_exists(graph, target, op->to)) {
    patch_op_fail(result, op, "GPH004", "patch edge target was not found", op->to, "");
    return false;
  }
  char *from_id = z_strdup(from_node->id ? from_node->id : "");
  char *to_copy = z_strdup(to_id ? to_id : "");
  if (patch_duplicate_ordered_edge(graph, from_id, op->edge, target, op->order)) {
    patch_op_fail(result, op, "GPH005", "patch edge order is already occupied", "unused ordered edge slot", op->edge);
    free(from_id);
    free(to_copy);
    return false;
  }
  ZProgramGraphEdge *edge = patch_append_edge(graph);
  edge->from = from_id;
  edge->to = to_copy;
  edge->kind = z_strdup(op->edge);
  edge->target = target;
  edge->order = op->order;
  op->ok = true;
  return true;
}

static bool patch_apply_insert(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!patch_node_id_valid(op->node)) {
    patch_op_fail(result, op, "GPH003", "insert node id must be a ProgramGraph node id", "#<id>", op->node);
    return false;
  }
  if (patch_find_node(graph, op->node)) {
    patch_op_fail(result, op, "GPH005", "insert node id already exists", "unused node id", op->node);
    return false;
  }
  ZProgramGraphNode *parent_node = patch_resolve_node_handle(graph, result, op, op->parent, "insert parent node");
  if (!parent_node) return false;
  char *parent_id = z_strdup(parent_node->id ? parent_node->id : "");
  if (!patch_edge_kind_valid(op->edge)) {
    patch_op_fail(result, op, "GPH003", "insert edge kind must be a simple ProgramGraph edge name", "edge identifier", op->edge);
    free(parent_id);
    return false;
  }
  ZProgramGraphNodeKind kind = Z_PROGRAM_GRAPH_NODE_EXPRESSION;
  if (!patch_parse_node_kind(op->kind, &kind)) {
    patch_op_fail(result, op, "GPH003", "insert kind must name a ProgramGraph node kind", "ProgramGraph node kind", op->kind);
    free(parent_id);
    return false;
  }
  if (patch_duplicate_ordered_edge(graph, parent_id, op->edge, Z_PROGRAM_GRAPH_EDGE_TARGET_NODE, op->order)) {
    patch_op_fail(result, op, "GPH005", "insert edge order is already occupied", "unused ordered edge slot", op->edge);
    free(parent_id);
    return false;
  }
  ZProgramGraphNode candidate = {
    .id = op->node,
    .kind = kind,
    .name = op->name,
    .type = op->type,
    .value = op->value,
    .path = op->path,
    .line = op->has_line_value ? op->line_value : 0,
    .column = op->has_column_value ? op->column_value : 0,
    .is_public = op->has_public_value && op->public_value,
    .is_mutable = op->has_mutable_value && op->mutable_value,
    .is_static = op->has_static_value && op->static_value,
    .fallible = op->has_fallible_value && op->fallible_value,
    .export_c = op->has_export_c_value && op->export_c_value,
  };
  if (!patch_validate_node_payload(&candidate, result, op)) {
    free(parent_id);
    return false;
  }
  ZProgramGraphNode *node = patch_append_node(graph);
  node->id = z_strdup(op->node);
  node->kind = kind;
  patch_copy_node_attrs(node, op);
  ZProgramGraphEdge *edge = patch_append_edge(graph);
  edge->from = parent_id;
  edge->to = z_strdup(op->node);
  edge->kind = z_strdup(op->edge);
  edge->target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE;
  edge->order = op->order;
  op->ok = true;
  return true;
}

static bool patch_apply_replace(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_resolve_node_handle(graph, result, op, op->node, "node");
  if (!node) return false;
  patch_replace_text(&op->actual, node->node_hash);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "patch node hash precondition failed", op->expected, op->actual);
    return false;
  }
  ZProgramGraphNodeKind kind = node->kind;
  if (op->kind && !patch_parse_node_kind(op->kind, &kind)) {
    patch_op_fail(result, op, "GPH003", "replace kind must name a ProgramGraph node kind", "ProgramGraph node kind", op->kind);
    return false;
  }
  ZProgramGraphNode candidate = *node;
  candidate.kind = kind;
  if (op->name) candidate.name = op->name;
  if (op->type) candidate.type = op->type;
  if (op->value) candidate.value = op->value;
  if (op->path) candidate.path = op->path;
  if (op->has_line_value) candidate.line = op->line_value;
  if (op->has_column_value) candidate.column = op->column_value;
  if (op->has_public_value) candidate.is_public = op->public_value;
  if (op->has_mutable_value) candidate.is_mutable = op->mutable_value;
  if (op->has_static_value) candidate.is_static = op->static_value;
  if (op->has_fallible_value) candidate.fallible = op->fallible_value;
  if (op->has_export_c_value) candidate.export_c = op->export_c_value;
  if (!patch_validate_node_payload(&candidate, result, op)) return false;
  node->kind = kind;
  patch_copy_node_attrs(node, op);
  op->ok = true;
  return true;
}

static bool patch_apply_rename(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_resolve_node_handle(graph, result, op, op->node, "node");
  if (!node) return false;
  patch_replace_text(&op->actual, node->name);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "patch rename precondition failed", op->expected, op->actual);
    return false;
  }
  if (!patch_validate_text_field(node, result, op, "name", op->value)) return false;
  patch_replace_text(&node->name, op->value);
  op->ok = true;
  return true;
}

static ZProgramGraphNode *patch_require_test_by_display_name(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  const char *name = op && op->name ? op->name : "";
  const char *prefix = "__zero_test_";
  ZProgramGraphNode *match = NULL;
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || !node->name || strncmp(node->name, prefix, strlen(prefix)) != 0 || !patch_text_eq(node->value, name)) continue;
    match = node;
    count++;
  }
  if (count == 1) return match;
  patch_op_fail(result, op, count == 0 ? "GPH004" : "GPH005", count == 0 ? "patch test was not found" : "patch test name is ambiguous", count == 0 ? "existing test display name" : "unique test display name", name);
  return NULL;
}

static bool patch_apply_rename_test(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_require_test_by_display_name(graph, result, op);
  if (!node) return false;
  patch_replace_text(&op->actual, node->value);
  if (!patch_validate_text_field(node, result, op, "value", op->value)) return false;
  patch_replace_text(&node->value, op->value);
  op->ok = true;
  return true;
}

static size_t patch_node_index(const ZProgramGraph *graph, const char *node_id) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (patch_text_eq(graph->nodes[i].id, node_id)) return i;
  }
  return (size_t)-1;
}

static bool patch_node_edge_kind_owns_child(const char *kind) {
  static const char *owned_kinds[] = {
    "alias",
    "arg",
    "arm",
    "body",
    "cImport",
    "case",
    "choice",
    "const",
    "declaredType",
    "default",
    "effect",
    "else",
    "enum",
    "error",
    "expr",
    "field",
    "function",
    "guard",
    "import",
    "interface",
    "left",
    "method",
    "param",
    "rangeEnd",
    "returnType",
    "right",
    "shape",
    "statement",
    "target",
    "then",
    "type",
    "typeArg",
    "typeParam",
    "value",
    NULL,
  };
  for (size_t i = 0; owned_kinds[i]; i++) {
    if (patch_text_eq(kind, owned_kinds[i])) return true;
  }
  return false;
}

static bool patch_edge_owns_child_node(const ZProgramGraphEdge *edge) {
  return edge &&
         edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
         patch_node_edge_kind_owns_child(edge->kind);
}

static bool patch_mark_delete_subtree(const ZProgramGraph *graph, size_t index, bool *marked) {
  if (!graph || index >= graph->node_len || marked[index]) return true;
  marked[index] = true;
  const char *node_id = graph->nodes[index].id;
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!patch_edge_owns_child_node(edge) || !patch_text_eq(edge->from, node_id)) continue;
    size_t child = patch_node_index(graph, edge->to);
    if (child == (size_t)-1) return false;
    if (!patch_mark_delete_subtree(graph, child, marked)) return false;
  }
  return true;
}

static bool patch_edge_targets_marked_node(const ZProgramGraph *graph, const ZProgramGraphEdge *edge, const bool *marked) {
  if (!graph || !edge || !marked) return false;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!marked[i]) continue;
    const ZProgramGraphNode *node = &graph->nodes[i];
    switch (edge->target) {
      case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
        if (patch_text_eq(edge->to, node->id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL:
        if (node->symbol_id && patch_text_eq(edge->to, node->symbol_id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE:
        if (node->type_id && patch_text_eq(edge->to, node->type_id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT:
        if (node->effect_id && patch_text_eq(edge->to, node->effect_id)) return true;
        break;
    }
  }
  return false;
}

static bool patch_domain_id_survives(const ZProgramGraph *graph, const bool *marked, ZProgramGraphEdgeTarget target, const char *id) {
  for (size_t i = 0; graph && marked && id && i < graph->node_len; i++) {
    if (marked[i]) continue;
    const ZProgramGraphNode *node = &graph->nodes[i];
    switch (target) {
      case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
        if (patch_text_eq(node->id, id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL:
        if (node->symbol_id && patch_text_eq(node->symbol_id, id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE:
        if (node->type_id && patch_text_eq(node->type_id, id)) return true;
        break;
      case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT:
        if (node->effect_id && patch_text_eq(node->effect_id, id)) return true;
        break;
    }
  }
  return false;
}

static bool patch_edge_target_removed_by_delete(const ZProgramGraph *graph, const ZProgramGraphEdge *edge, const bool *marked) {
  return patch_edge_targets_marked_node(graph, edge, marked) && !patch_domain_id_survives(graph, marked, edge->target, edge->to);
}

static const ZProgramGraphEdge *patch_delete_root_parent_edge(const ZProgramGraph *graph, const bool *marked, const char *root_id) {
  const ZProgramGraphEdge *parent_edge = NULL;
  size_t parent_count = 0;
  for (size_t i = 0; graph && marked && root_id && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!patch_edge_owns_child_node(edge) || !patch_text_eq(edge->to, root_id)) continue;
    size_t source = patch_node_index(graph, edge->from);
    if (source != (size_t)-1 && marked[source]) continue;
    parent_edge = edge;
    parent_count++;
  }
  return parent_count == 1 ? parent_edge : NULL;
}

static bool patch_delete_external_reference_allowed(const ZProgramGraph *graph, const ZProgramGraphEdge *edge, const bool *marked, const ZProgramGraphEdge *root_parent_edge) {
  size_t source = patch_node_index(graph, edge->from);
  if (source != (size_t)-1 && marked[source]) return true;
  return edge == root_parent_edge;
}
static bool patch_apply_delete(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *root_node = patch_resolve_node_handle(graph, result, op, op->node, "node");
  if (!root_node) return false;
  size_t root = patch_node_index(graph, root_node->id);
  if (root == (size_t)-1) {
    patch_op_fail(result, op, "GPH004", "patch node was not found", op->node, "");
    return false;
  }
  if (graph->nodes[root].kind == Z_PROGRAM_GRAPH_NODE_MODULE) {
    patch_op_fail(result, op, "GPH003", "patch cannot delete the module root", "non-module node", op->node);
    return false;
  }
  patch_replace_text(&op->actual, graph->nodes[root].node_hash);
  if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
    patch_op_fail(result, op, "GPH005", "patch node hash precondition failed", op->expected, op->actual);
    return false;
  }
  bool *marked = z_checked_calloc(graph->node_len, sizeof(bool));
  if (!patch_mark_delete_subtree(graph, root, marked)) {
    free(marked);
    patch_op_fail(result, op, "GPH006", "delete subtree references a missing node", "valid child edge target", op->node);
    return false;
  }
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!marked[i] || graph->nodes[i].kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    free(marked);
    patch_op_fail(result, op, "GPH003", "patch delete subtree cannot include the module root", "non-module owned subtree", graph->nodes[i].id);
    return false;
  }
  const ZProgramGraphEdge *root_parent_edge = patch_delete_root_parent_edge(graph, marked, graph->nodes[root].id);
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!patch_edge_target_removed_by_delete(graph, edge, marked)) continue;
    if (!patch_delete_external_reference_allowed(graph, edge, marked, root_parent_edge)) {
      free(marked);
      patch_op_fail(result, op, "GPH005", "delete would remove a node referenced outside its subtree", "owned subtree", edge->to);
      return false;
    }
  }
  size_t write_edge = 0;
  for (size_t i = 0; i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    size_t source = patch_node_index(graph, edge->from);
    bool remove = (source != (size_t)-1 && marked[source]) || patch_edge_target_removed_by_delete(graph, edge, marked);
    if (remove) {
      free(edge->from);
      free(edge->to);
      free(edge->kind);
      continue;
    }
    if (write_edge != i) graph->edges[write_edge] = *edge;
    write_edge++;
  }
  for (size_t i = write_edge; i < graph->edge_len; i++) graph->edges[i] = (ZProgramGraphEdge){0};
  graph->edge_len = write_edge;
  size_t write_node = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    ZProgramGraphNode *node = &graph->nodes[i];
    if (marked[i]) {
      free(node->id);
      free(node->name);
      free(node->type);
      free(node->value);
      free(node->path);
      free(node->symbol_id);
      free(node->type_id);
      free(node->effect_id);
      free(node->node_hash);
      continue;
    }
    if (write_node != i) graph->nodes[write_node] = *node;
    write_node++;
  }
  for (size_t i = write_node; i < graph->node_len; i++) graph->nodes[i] = (ZProgramGraphNode){0};
  graph->node_len = write_node;
  free(marked);
  op->ok = true;
  return true;
}

static bool patch_apply_delete_test(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  ZProgramGraphNode *node = patch_require_test_by_display_name(graph, result, op);
  if (!node) return false;
  patch_replace_text(&op->node, node->id);
  return patch_apply_delete(graph, result, op);
}

bool z_program_graph_patch_apply_operation(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (patch_text_eq(op->op, "insert")) return patch_apply_insert(graph, result, op);
  if (patch_text_eq(op->op, "insertEdge")) return patch_apply_insert_edge(graph, result, op);
  if (patch_text_eq(op->op, "replaceExpr")) return z_program_graph_patch_apply_replace_expr(graph, result, op);
  if (patch_text_eq(op->op, "replace")) return patch_apply_replace(graph, result, op);
  if (patch_text_eq(op->op, "delete")) return patch_apply_delete(graph, result, op);
  if (patch_text_eq(op->op, "rename")) return patch_apply_rename(graph, result, op);
  if (patch_text_eq(op->op, "deleteTest")) return patch_apply_delete_test(graph, result, op);
  if (patch_text_eq(op->op, "renameTest")) return patch_apply_rename_test(graph, result, op);
  if (patch_text_eq(op->op, "addFunction")) return patch_apply_add_function(graph, result, op);
  if (patch_text_eq(op->op, "addMain")) return patch_apply_add_main(graph, result, op);
  if (patch_text_eq(op->op, "addParamTo")) return z_program_graph_patch_apply_add_param_to(graph, result, op);
  if (patch_text_eq(op->op, "addParam")) return patch_apply_add_param(graph, result, op);
  if (patch_text_eq(op->op, "setConst")) return z_program_graph_patch_apply_set_const(graph, result, op);
  if (patch_text_eq(op->op, "setReturnType")) return patch_apply_set_return_type(graph, result, op);
  if (patch_text_eq(op->op, "addReturnBinary")) return patch_apply_add_return_binary(graph, result, op);
  if (patch_text_eq(op->op, "addLetLiteral")) return z_program_graph_patch_apply_add_let_literal(graph, result, op);
  if (patch_text_eq(op->op, "addLetBinary")) return z_program_graph_patch_apply_add_let_binary(graph, result, op);
  if (patch_text_eq(op->op, "addReturnValue")) return z_program_graph_patch_apply_add_return_value(graph, result, op);
  if (patch_text_eq(op->op, "addReturnExpr")) return z_program_graph_patch_apply_add_return_expr(graph, result, op);
  if (patch_text_eq(op->op, "appendStmt")) return z_program_graph_patch_apply_append_stmt(graph, result, op);
  if (patch_text_eq(op->op, "addCheckWriteValue")) return z_program_graph_patch_apply_add_check_write_value(graph, result, op);
  if (patch_text_eq(op->op, "addCheckWrite")) return patch_apply_add_check_write(graph, result, op);
  if (patch_text_eq(op->op, "addTest")) return patch_apply_add_test(graph, result, op);
  if (patch_text_eq(op->op, "addTestBody")) return z_program_graph_patch_apply_add_test_body(graph, result, op);
  if (patch_text_eq(op->op, "upsertFunction")) return z_program_graph_patch_apply_upsert_function(graph, result, op);
  if (patch_text_eq(op->op, "replaceFunctionBody")) return z_program_graph_patch_apply_replace_function_body(graph, result, op);
  if (patch_text_eq(op->op, "replaceBlockBody")) return z_program_graph_patch_apply_replace_block_body(graph, result, op);

  ZProgramGraphNode *node = patch_resolve_node_handle(graph, result, op, op->node, "node");
  if (!node) return false;

  char **text_slot = patch_node_text_field(node, op->field);
  if (text_slot) {
    patch_replace_text(&op->actual, *text_slot);
    if (op->has_expected && !patch_text_eq(op->expected, op->actual)) {
      patch_op_fail(result, op, "GPH005", "patch field precondition failed", op->expected, op->actual);
      return false;
    }
    if (!patch_validate_text_value(node, result, op)) return false;
    patch_replace_text(text_slot, op->value);
    op->ok = true;
    return true;
  }

  bool *bool_slot = patch_node_bool_field(node, op->field);
  if (bool_slot) {
    bool next = false;
    if (!patch_parse_bool(op->value, &next)) {
      patch_op_fail(result, op, "GPH003", "patch flag value must be true or false", "true or false", op->value);
      return false;
    }
    const char *actual = *bool_slot ? "true" : "false";
    patch_replace_text(&op->actual, actual);
    if (op->has_expected && !patch_text_eq(op->expected, actual)) {
      patch_op_fail(result, op, "GPH005", "patch field precondition failed", op->expected, actual);
      return false;
    }
    *bool_slot = next;
    op->ok = true;
    return true;
  }

  patch_op_fail(result, op, "GPH003", "patch field is not editable", "name, type, value, public, mutable, static, fallible, or exportC", op->field);
  return false;
}
