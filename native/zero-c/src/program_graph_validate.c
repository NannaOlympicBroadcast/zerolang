#include "program_graph.h"
#include "program_graph_adjacency.h"
#include "program_graph_order.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef const char *(*GraphIdField)(const ZProgramGraphNode *node);

static const char *graph_symbol_id_field(const ZProgramGraphNode *node) { return node->symbol_id; }
static const char *graph_type_id_field(const ZProgramGraphNode *node) { return node->type_id; }
static const char *graph_effect_id_field(const ZProgramGraphNode *node) { return node->effect_id; }

typedef struct {
  const ZProgramGraph *graph;
  ZProgramGraphAdjacency adjacency;
  const char **symbol_ids;
  size_t symbol_id_len;
  const char **type_ids;
  size_t type_id_len;
  const char **effect_ids;
  size_t effect_id_len;
  bool *node_dup_after;
  bool *edge_dup_after;
  bool *edge_sparse_first;
} GraphValidateIndex;

static bool graph_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static int graph_field_key_cmp(const void *left, const void *right) {
  const char *const *a = left;
  const char *const *b = right;
  return strcmp(*a, *b);
}

static size_t graph_collect_field_keys(const ZProgramGraph *graph, const char **out, GraphIdField field) {
  size_t len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    const char *value = field(&graph->nodes[i]);
    if (value && value[0]) out[len++] = value;
  }
  qsort(out, len, sizeof(const char *), graph_field_key_cmp);
  return len;
}

static bool graph_field_keys_contain(const char *const *keys, size_t len, const char *id) {
  if (!id || !id[0]) return false;
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (strcmp(keys[mid], id) < 0) low = mid + 1;
    else high = mid;
  }
  return low < len && strcmp(keys[low], id) == 0;
}

static const ZProgramGraphNode *graph_find_node(const GraphValidateIndex *index, const char *id) {
  return z_program_graph_adjacency_node(&index->adjacency, id);
}

static bool graph_edge_target_exists(const GraphValidateIndex *index, const ZProgramGraphEdge *edge) {
  switch (edge->target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
      return edge->to && edge->to[0] && z_program_graph_adjacency_node_index(&index->adjacency, edge->to) != SIZE_MAX;
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL: return graph_field_keys_contain(index->symbol_ids, index->symbol_id_len, edge->to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE: return graph_field_keys_contain(index->type_ids, index->type_id_len, edge->to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT: return graph_field_keys_contain(index->effect_ids, index->effect_id_len, edge->to);
  }
  return false;
}

static bool graph_edge_target_valid(ZProgramGraphEdgeTarget target) {
  switch (target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL:
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE:
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT:
      return true;
  }
  return false;
}

static bool graph_node_kind_valid(ZProgramGraphNodeKind kind) {
  return kind >= Z_PROGRAM_GRAPH_NODE_MODULE && kind <= Z_PROGRAM_GRAPH_NODE_STATEMENT;
}

static bool graph_node_is_stmt(const ZProgramGraphNode *node) {
  if (!node) return false;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_LET:
    case Z_PROGRAM_GRAPH_NODE_ASSIGNMENT:
    case Z_PROGRAM_GRAPH_NODE_DEFER:
    case Z_PROGRAM_GRAPH_NODE_CHECK:
    case Z_PROGRAM_GRAPH_NODE_RETURN:
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT:
    case Z_PROGRAM_GRAPH_NODE_IF:
    case Z_PROGRAM_GRAPH_NODE_WHILE:
    case Z_PROGRAM_GRAPH_NODE_FOR:
    case Z_PROGRAM_GRAPH_NODE_BREAK:
    case Z_PROGRAM_GRAPH_NODE_CONTINUE:
    case Z_PROGRAM_GRAPH_NODE_MATCH:
    case Z_PROGRAM_GRAPH_NODE_RAISE:
    case Z_PROGRAM_GRAPH_NODE_STATEMENT:
      return true;
    default:
      return false;
  }
}

static bool graph_node_is_expr(const ZProgramGraphNode *node) {
  if (!node) return false;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
    case Z_PROGRAM_GRAPH_NODE_SLICE:
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
    case Z_PROGRAM_GRAPH_NODE_CAST:
    case Z_PROGRAM_GRAPH_NODE_BORROW:
    case Z_PROGRAM_GRAPH_NODE_CHECK:
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
    case Z_PROGRAM_GRAPH_NODE_META:
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION:
      return true;
    default:
      return false;
  }
}

static bool graph_edge_child_allowed(const ZProgramGraphNode *owner, const char *kind, const ZProgramGraphNode *child) {
  if (!owner || !kind || !child) return false;
  switch (owner->kind) {
    case Z_PROGRAM_GRAPH_NODE_MODULE:
      return (graph_text_eq(kind, "import") && child->kind == Z_PROGRAM_GRAPH_NODE_IMPORT) ||
             (graph_text_eq(kind, "cImport") && child->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT) ||
             (graph_text_eq(kind, "const") && child->kind == Z_PROGRAM_GRAPH_NODE_CONST) ||
             (graph_text_eq(kind, "alias") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS) ||
             (graph_text_eq(kind, "shape") && child->kind == Z_PROGRAM_GRAPH_NODE_SHAPE) ||
             (graph_text_eq(kind, "interface") && child->kind == Z_PROGRAM_GRAPH_NODE_INTERFACE) ||
             (graph_text_eq(kind, "enum") && child->kind == Z_PROGRAM_GRAPH_NODE_ENUM) ||
             (graph_text_eq(kind, "choice") && child->kind == Z_PROGRAM_GRAPH_NODE_CHOICE) ||
             (graph_text_eq(kind, "function") && child->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION);
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
      return (graph_text_eq(kind, "typeParam") && child->kind == Z_PROGRAM_GRAPH_NODE_PARAM) ||
             (graph_text_eq(kind, "field") && child->kind == Z_PROGRAM_GRAPH_NODE_FIELD) ||
             (graph_text_eq(kind, "method") && child->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION);
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
      return (graph_text_eq(kind, "typeParam") && child->kind == Z_PROGRAM_GRAPH_NODE_PARAM) ||
             (graph_text_eq(kind, "method") && child->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION);
    case Z_PROGRAM_GRAPH_NODE_ENUM:
      return graph_text_eq(kind, "case") && child->kind == Z_PROGRAM_GRAPH_NODE_ENUM_CASE;
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
      return graph_text_eq(kind, "case") && child->kind == Z_PROGRAM_GRAPH_NODE_CHOICE_CASE;
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
      return (graph_text_eq(kind, "typeParam") && child->kind == Z_PROGRAM_GRAPH_NODE_PARAM) ||
             (graph_text_eq(kind, "param") && child->kind == Z_PROGRAM_GRAPH_NODE_PARAM) ||
             (graph_text_eq(kind, "returnType") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF) ||
             (graph_text_eq(kind, "effect") && child->kind == Z_PROGRAM_GRAPH_NODE_EFFECT_REF) ||
             (graph_text_eq(kind, "error") && child->kind == Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT) ||
             (graph_text_eq(kind, "body") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK);
    case Z_PROGRAM_GRAPH_NODE_PARAM:
    case Z_PROGRAM_GRAPH_NODE_FIELD:
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
      return (graph_text_eq(kind, "type") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF) ||
             (graph_text_eq(kind, "default") && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_CONST:
      return graph_text_eq(kind, "value") && graph_node_is_expr(child);
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
      return graph_text_eq(kind, "target") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF;
    case Z_PROGRAM_GRAPH_NODE_BLOCK:
      return graph_text_eq(kind, "statement") && graph_node_is_stmt(child);
    case Z_PROGRAM_GRAPH_NODE_LET:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "declaredType") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF);
    case Z_PROGRAM_GRAPH_NODE_ASSIGNMENT:
      return (graph_text_eq(kind, "target") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "expr") && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_DEFER:
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT:
    case Z_PROGRAM_GRAPH_NODE_RETURN:
      return graph_text_eq(kind, "expr") && graph_node_is_expr(child);
    case Z_PROGRAM_GRAPH_NODE_CHECK:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "left") && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_IF:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "then") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK) ||
             (graph_text_eq(kind, "else") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK);
    case Z_PROGRAM_GRAPH_NODE_WHILE:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "then") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK);
    case Z_PROGRAM_GRAPH_NODE_FOR:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "rangeEnd") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "then") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK);
    case Z_PROGRAM_GRAPH_NODE_MATCH:
      return (graph_text_eq(kind, "expr") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "arm") && child->kind == Z_PROGRAM_GRAPH_NODE_MATCH_ARM);
    case Z_PROGRAM_GRAPH_NODE_MATCH_ARM:
      return (graph_text_eq(kind, "rangeEnd") && child->kind == Z_PROGRAM_GRAPH_NODE_LITERAL) ||
             (graph_text_eq(kind, "guard") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "body") && child->kind == Z_PROGRAM_GRAPH_NODE_BLOCK);
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
      return graph_text_eq(kind, "typeArg") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF;
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
      return (graph_text_eq(kind, "left") && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "typeArg") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF);
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
      return ((graph_text_eq(kind, "left") || graph_text_eq(kind, "right")) && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_SLICE:
      return ((graph_text_eq(kind, "left") || graph_text_eq(kind, "arg")) && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
      return ((graph_text_eq(kind, "left") || graph_text_eq(kind, "right") || graph_text_eq(kind, "arg")) && graph_node_is_expr(child)) ||
             (graph_text_eq(kind, "typeArg") && child->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF);
    case Z_PROGRAM_GRAPH_NODE_CAST:
    case Z_PROGRAM_GRAPH_NODE_BORROW:
    case Z_PROGRAM_GRAPH_NODE_META:
      return graph_text_eq(kind, "left") && graph_node_is_expr(child);
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
      return ((graph_text_eq(kind, "left") || graph_text_eq(kind, "right")) && graph_node_is_expr(child));
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL:
      return graph_text_eq(kind, "field") && child->kind == Z_PROGRAM_GRAPH_NODE_FIELD_INIT;
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL:
      return graph_text_eq(kind, "arg") && graph_node_is_expr(child);
    case Z_PROGRAM_GRAPH_NODE_FIELD_INIT:
      return graph_text_eq(kind, "value") && graph_node_is_expr(child);
    default:
      return false;
  }
}

static void graph_owner_run(const GraphValidateIndex *index, const char *from, const char *kind, size_t *start, size_t *len) {
  *start = 0;
  *len = 0;
  if (from && kind) z_program_graph_adjacency_owner_run(&index->adjacency, from, kind, start, len);
}

static size_t graph_count_node_edges(const GraphValidateIndex *index, const char *from, const char *kind) {
  size_t start = 0;
  size_t len = 0;
  graph_owner_run(index, from, kind, &start, &len);
  return len;
}

static bool graph_has_node_edge_order(const GraphValidateIndex *index, const char *from, const char *kind, size_t order) {
  size_t start = 0;
  size_t len = 0;
  graph_owner_run(index, from, kind, &start, &len);
  for (size_t i = start; i < start + len; i++) {
    if (z_program_graph_adjacency_owner_edge_at(&index->adjacency, i)->order == order) return true;
  }
  return false;
}

static bool graph_node_has_incoming_node_edge(const GraphValidateIndex *index, const char *to, const char *kind) {
  return z_program_graph_adjacency_has_child_edge(&index->adjacency, to, kind);
}

static bool graph_validation_fail(ZProgramGraphValidation *validation, const char *code, const char *message, const char *node_id, const char *edge_from, const char *edge_to, const char *edge_target) {
  if (validation) {
    validation->ok = false;
    validation->state = Z_PROGRAM_GRAPH_VALIDATION_DECODED;
    snprintf(validation->code, sizeof(validation->code), "%s", code ? code : "GRF000");
    snprintf(validation->message, sizeof(validation->message), "%s", message ? message : "program graph validation failed");
    snprintf(validation->node_id, sizeof(validation->node_id), "%s", node_id ? node_id : "");
    snprintf(validation->edge_from, sizeof(validation->edge_from), "%s", edge_from ? edge_from : "");
    snprintf(validation->edge_to, sizeof(validation->edge_to), "%s", edge_to ? edge_to : "");
    snprintf(validation->edge_target, sizeof(validation->edge_target), "%s", edge_target ? edge_target : "");
  }
  return false;
}

static bool graph_required_text_present(const char *text) { return text && text[0]; }

static bool graph_required_value_present(const char *text) { return text != NULL; }

static bool graph_validate_node_payload(const ZProgramGraphNode *node, ZProgramGraphValidation *validation) {
  if (!node) return true;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_MODULE:
    case Z_PROGRAM_GRAPH_NODE_IMPORT:
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT:
    case Z_PROGRAM_GRAPH_NODE_CONST:
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
    case Z_PROGRAM_GRAPH_NODE_ENUM:
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
    case Z_PROGRAM_GRAPH_NODE_PARAM:
    case Z_PROGRAM_GRAPH_NODE_FIELD:
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
    case Z_PROGRAM_GRAPH_NODE_LET:
    case Z_PROGRAM_GRAPH_NODE_FOR:
    case Z_PROGRAM_GRAPH_NODE_RAISE:
    case Z_PROGRAM_GRAPH_NODE_MATCH_ARM:
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_FIELD_INIT:
    case Z_PROGRAM_GRAPH_NODE_EFFECT_REF:
    case Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT:
      if (!graph_required_text_present(node->name)) return graph_validation_fail(validation, "GRF014", "node is missing required name", node->id, NULL, NULL, NULL);
      break;
    default:
      break;
  }
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT:
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
      if (!graph_required_value_present(node->value)) return graph_validation_fail(validation, "GRF014", "node is missing required value", node->id, NULL, NULL, NULL);
      break;
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
    case Z_PROGRAM_GRAPH_NODE_FIELD:
    case Z_PROGRAM_GRAPH_NODE_TYPE_REF:
      if (!graph_required_text_present(node->type)) return graph_validation_fail(validation, "GRF014", "node is missing required type", node->id, NULL, NULL, NULL);
      break;
    default:
      break;
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_LITERAL && graph_required_text_present(node->name)) {
    return graph_validation_fail(validation, "GRF015", "literal node must not carry a name payload", node->id, NULL, NULL, NULL);
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_CAST && !graph_required_text_present(node->name) && !graph_required_text_present(node->type)) {
    return graph_validation_fail(validation, "GRF014", "cast node is missing target type", node->id, NULL, NULL, NULL);
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_TYPE_REF && (graph_required_text_present(node->name) || graph_required_value_present(node->value))) {
    return graph_validation_fail(validation, "GRF015", "type reference node has illegal payload", node->id, NULL, NULL, NULL);
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_EFFECT_REF && (graph_required_text_present(node->type) || graph_required_value_present(node->value))) {
    return graph_validation_fail(validation, "GRF015", "effect reference node has illegal payload", node->id, NULL, NULL, NULL);
  }
  return true;
}

static bool graph_validate_required_edge_count(const GraphValidateIndex *index, const ZProgramGraphNode *node, const char *kind, size_t min, size_t max, ZProgramGraphValidation *validation) {
  size_t count = graph_count_node_edges(index, node ? node->id : NULL, kind);
  if (count >= min && count <= max) return true;
  char message[160];
  if (min == max && min == 1) snprintf(message, sizeof(message), "node is missing required %s edge", kind ? kind : "child");
  else snprintf(message, sizeof(message), "node has invalid %s edge count", kind ? kind : "child");
  return graph_validation_fail(validation, "GRF016", message, node ? node->id : NULL, node ? node->id : NULL, NULL, "node");
}

static bool graph_validate_optional_edge_count(const GraphValidateIndex *index, const ZProgramGraphNode *node, const char *kind, size_t max, ZProgramGraphValidation *validation) {
  return graph_validate_required_edge_count(index, node, kind, 0, max, validation);
}

static bool graph_validate_edge_order_range(const GraphValidateIndex *index, const ZProgramGraphNode *node, const char *kind, size_t max_exclusive, ZProgramGraphValidation *validation) {
  size_t start = 0;
  size_t len = 0;
  graph_owner_run(index, node ? node->id : NULL, kind, &start, &len);
  const ZProgramGraphEdge *offending = NULL;
  size_t offending_index = SIZE_MAX;
  for (size_t i = start; i < start + len; i++) {
    const ZProgramGraphEdge *edge = z_program_graph_adjacency_owner_edge_at(&index->adjacency, i);
    size_t edge_index = index->adjacency.edges_by_owner[i].edge_index;
    if (edge->order >= max_exclusive && edge_index < offending_index) {
      offending = edge;
      offending_index = edge_index;
    }
  }
  if (!offending) return true;
  char message[160];
  snprintf(message, sizeof(message), "node has invalid %s edge order", kind);
  return graph_validation_fail(validation, "GRF016", message, node->id, offending->from, offending->to, "node");
}

static bool graph_validate_edge_order(const GraphValidateIndex *index, const ZProgramGraphNode *node, const char *kind, size_t order, ZProgramGraphValidation *validation) {
  if (graph_count_node_edges(index, node ? node->id : NULL, kind) == 0) return true;
  if (graph_has_node_edge_order(index, node ? node->id : NULL, kind, order)) return true;
  char message[160];
  snprintf(message, sizeof(message), "node has invalid %s edge order", kind ? kind : "child");
  return graph_validation_fail(validation, "GRF016", message, node ? node->id : NULL, node ? node->id : NULL, NULL, "node");
}

static bool graph_validate_required_edge_at_order(const GraphValidateIndex *index, const ZProgramGraphNode *node, const char *kind, size_t order, ZProgramGraphValidation *validation) {
  return graph_validate_required_edge_count(index, node, kind, 1, 1, validation) &&
         graph_validate_edge_order(index, node, kind, order, validation);
}

static bool graph_validate_optional_edge_at_order(const GraphValidateIndex *index, const ZProgramGraphNode *node, const char *kind, size_t order, ZProgramGraphValidation *validation) {
  return graph_validate_optional_edge_count(index, node, kind, 1, validation) &&
         graph_validate_edge_order(index, node, kind, order, validation);
}

static bool graph_validate_required_edges(const GraphValidateIndex *index, const ZProgramGraphNode *node, ZProgramGraphValidation *validation) {
  if (!node) return true;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_PARAM:
      if (!graph_required_text_present(node->type) &&
          (graph_node_has_incoming_node_edge(index, node->id, "param") || !graph_node_has_incoming_node_edge(index, node->id, "typeParam")))
        return graph_validation_fail(validation, "GRF014", "node is missing required type", node->id, NULL, NULL, NULL);
      return graph_validate_optional_edge_at_order(index, node, "type", 0, validation) &&
             graph_validate_optional_edge_at_order(index, node, "default", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_FIELD:
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
      return graph_validate_optional_edge_at_order(index, node, "type", 0, validation) &&
             graph_validate_optional_edge_at_order(index, node, "default", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_CONST:
      return graph_validate_required_edge_at_order(index, node, "value", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
      return graph_validate_required_edge_at_order(index, node, "target", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
      return graph_validate_optional_edge_at_order(index, node, "returnType", 0, validation) &&
             graph_validate_optional_edge_at_order(index, node, "effect", 0, validation) &&
             graph_validate_required_edge_at_order(index, node, "body", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_LET:
      return graph_validate_required_edge_at_order(index, node, "expr", 0, validation) &&
             graph_validate_optional_edge_at_order(index, node, "declaredType", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_ASSIGNMENT:
      return graph_validate_required_edge_at_order(index, node, "target", 0, validation) &&
             graph_validate_required_edge_at_order(index, node, "expr", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_DEFER:
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT:
      return graph_validate_required_edge_at_order(index, node, "expr", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_CHECK: {
      bool is_statement = graph_node_has_incoming_node_edge(index, node->id, "statement");
      if (is_statement) {
        if (graph_count_node_edges(index, node->id, "left") != 0) {
          return graph_validation_fail(validation, "GRF016", "statement check node must use expr edge", node->id, node->id, NULL, "node");
        }
        return graph_validate_required_edge_at_order(index, node, "expr", 0, validation);
      }
      if (graph_count_node_edges(index, node->id, "expr") != 0) {
        return graph_validation_fail(validation, "GRF016", "expression check node must use left edge", node->id, node->id, NULL, "node");
      }
      if (graph_validate_required_edge_at_order(index, node, "left", 0, validation)) return true;
      return graph_validation_fail(validation, "GRF016", "check node requires exactly one checked expression edge", node->id, node->id, NULL, "node");
    }
    case Z_PROGRAM_GRAPH_NODE_RETURN:
      return graph_validate_optional_edge_at_order(index, node, "expr", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_IF:
      return graph_validate_required_edge_at_order(index, node, "expr", 0, validation) &&
             graph_validate_optional_edge_at_order(index, node, "then", 0, validation) &&
             graph_validate_optional_edge_at_order(index, node, "else", 1, validation);
    case Z_PROGRAM_GRAPH_NODE_WHILE:
      return graph_validate_required_edge_at_order(index, node, "expr", 0, validation) &&
             graph_validate_optional_edge_at_order(index, node, "then", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_FOR:
      return graph_validate_required_edge_at_order(index, node, "expr", 0, validation) &&
             graph_validate_required_edge_at_order(index, node, "rangeEnd", 1, validation) &&
             graph_validate_optional_edge_at_order(index, node, "then", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_MATCH:
      return graph_validate_required_edge_at_order(index, node, "expr", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_MATCH_ARM:
      return graph_validate_optional_edge_at_order(index, node, "rangeEnd", 0, validation) &&
             graph_validate_optional_edge_at_order(index, node, "guard", 0, validation) &&
             graph_validate_required_edge_at_order(index, node, "body", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
    case Z_PROGRAM_GRAPH_NODE_CAST:
    case Z_PROGRAM_GRAPH_NODE_BORROW:
    case Z_PROGRAM_GRAPH_NODE_META:
      return graph_validate_required_edge_at_order(index, node, "left", 0, validation);
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
      return graph_validate_required_edge_at_order(index, node, "left", 0, validation) &&
             graph_validate_required_edge_at_order(index, node, "right", 1, validation);
    case Z_PROGRAM_GRAPH_NODE_SLICE:
      return graph_validate_required_edge_at_order(index, node, "left", 0, validation) &&
             graph_validate_optional_edge_count(index, node, "arg", 2, validation) &&
             graph_validate_edge_order_range(index, node, "arg", 2, validation);
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
      if (!graph_validate_optional_edge_at_order(index, node, "left", 0, validation) ||
          !graph_validate_optional_edge_at_order(index, node, "right", 1, validation)) {
        return false;
      }
      if (graph_count_node_edges(index, node->id, "right") > 0 && graph_count_node_edges(index, node->id, "left") == 0) {
        return graph_validation_fail(validation, "GRF016", "binary call node is missing left operand", node->id, node->id, NULL, "node");
      }
      if (graph_required_text_present(node->name) || graph_count_node_edges(index, node->id, "left") == 1) return true;
      return graph_validation_fail(validation, "GRF016", "call node is missing callee", node->id, node->id, NULL, "node");
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
      return graph_validate_required_edge_at_order(index, node, "left", 0, validation) &&
             graph_validate_required_edge_at_order(index, node, "right", 1, validation);
    case Z_PROGRAM_GRAPH_NODE_FIELD_INIT:
      return graph_validate_required_edge_at_order(index, node, "value", 0, validation);
    default:
      return true;
  }
}

static bool graph_validate_order_groups(const GraphValidateIndex *index, ZProgramGraphValidation *validation) {
  const ZProgramGraph *graph = index->graph;
  for (size_t i = 0; i < graph->edge_len; i++) {
    if (!index->edge_sparse_first[i]) continue;
    const ZProgramGraphEdge *edge = &graph->edges[i];
    return graph_validation_fail(validation, "GRF013", "ordered edge group is sparse", NULL, edge->from, edge->to, "node");
  }
  return true;
}

static void graph_validate_index_mark_node_dups(GraphValidateIndex *index) {
  const ZProgramGraphAdjacency *adjacency = &index->adjacency;
  for (size_t i = 0; i + 1 < index->graph->node_len; i++) {
    if (graph_text_eq(adjacency->nodes_by_id[i].id, adjacency->nodes_by_id[i + 1].id)) {
      index->node_dup_after[adjacency->nodes_by_id[i].node_index] = true;
    }
  }
}

static bool graph_edge_group_eq(const ZProgramGraphEdge *left, const ZProgramGraphEdge *right) {
  return left->target == right->target &&
         graph_text_eq(left->from, right->from) &&
         graph_text_eq(left->kind, right->kind) &&
         left->order == right->order;
}

static void graph_validate_index_mark_edge_dup_run(GraphValidateIndex *index, size_t run_start, size_t run_end) {
  const ZProgramGraphAdjacency *adjacency = &index->adjacency;
  size_t member_count = 0;
  size_t max_index = 0;
  for (size_t i = run_start; i < run_end; i++) {
    const ZProgramGraphEdge *edge = adjacency->edges_by_owner[i].edge;
    if (!edge->from || !edge->kind) continue;
    member_count++;
    if (adjacency->edges_by_owner[i].edge_index > max_index) max_index = adjacency->edges_by_owner[i].edge_index;
  }
  if (member_count < 2) return;
  for (size_t i = run_start; i < run_end; i++) {
    const ZProgramGraphEdge *edge = adjacency->edges_by_owner[i].edge;
    if (!edge->from || !edge->kind) continue;
    if (adjacency->edges_by_owner[i].edge_index != max_index) index->edge_dup_after[adjacency->edges_by_owner[i].edge_index] = true;
  }
}

static void graph_validate_index_mark_edge_dups(GraphValidateIndex *index) {
  const ZProgramGraphAdjacency *adjacency = &index->adjacency;
  size_t edge_len = index->graph->edge_len;
  size_t run_start = 0;
  for (size_t i = 1; i <= edge_len; i++) {
    if (i < edge_len && graph_edge_group_eq(adjacency->edges_by_owner[i - 1].edge, adjacency->edges_by_owner[i].edge)) continue;
    graph_validate_index_mark_edge_dup_run(index, run_start, i);
    run_start = i;
  }
}

static void graph_validate_index_mark_sparse_run(GraphValidateIndex *index, size_t run_start, size_t run_end) {
  const ZProgramGraphAdjacency *adjacency = &index->adjacency;
  const ZProgramGraphEdge *first = adjacency->edges_by_owner[run_start].edge;
  const ZProgramGraphNode *owner = z_program_graph_adjacency_node(adjacency, first->from);
  if (!z_program_graph_order_must_be_contiguous(owner, first->kind)) return;
  bool sparse = false;
  size_t min_index = SIZE_MAX;
  for (size_t i = run_start; i < run_end; i++) {
    if (adjacency->edges_by_owner[i].edge->order != i - run_start) sparse = true;
    if (adjacency->edges_by_owner[i].edge_index < min_index) min_index = adjacency->edges_by_owner[i].edge_index;
  }
  if (sparse) index->edge_sparse_first[min_index] = true;
}

static void graph_validate_index_mark_sparse_groups(GraphValidateIndex *index) {
  const ZProgramGraphAdjacency *adjacency = &index->adjacency;
  size_t node_edge_len = adjacency->node_edge_len;
  size_t run_start = 0;
  for (size_t i = 1; i <= node_edge_len; i++) {
    if (i < node_edge_len &&
        graph_text_eq(adjacency->edges_by_owner[i - 1].edge->from, adjacency->edges_by_owner[i].edge->from) &&
        graph_text_eq(adjacency->edges_by_owner[i - 1].edge->kind, adjacency->edges_by_owner[i].edge->kind)) {
      continue;
    }
    graph_validate_index_mark_sparse_run(index, run_start, i);
    run_start = i;
  }
}

static void graph_validate_index_init(GraphValidateIndex *index, const ZProgramGraph *graph) {
  *index = (GraphValidateIndex){.graph = graph};
  z_program_graph_adjacency_init(&index->adjacency, graph);
  size_t node_cap = graph->node_len ? graph->node_len : 1;
  size_t edge_cap = graph->edge_len ? graph->edge_len : 1;
  index->symbol_ids = z_checked_calloc(node_cap, sizeof(const char *));
  index->type_ids = z_checked_calloc(node_cap, sizeof(const char *));
  index->effect_ids = z_checked_calloc(node_cap, sizeof(const char *));
  index->symbol_id_len = graph_collect_field_keys(graph, index->symbol_ids, graph_symbol_id_field);
  index->type_id_len = graph_collect_field_keys(graph, index->type_ids, graph_type_id_field);
  index->effect_id_len = graph_collect_field_keys(graph, index->effect_ids, graph_effect_id_field);
  index->node_dup_after = z_checked_calloc(node_cap, sizeof(bool));
  index->edge_dup_after = z_checked_calloc(edge_cap, sizeof(bool));
  index->edge_sparse_first = z_checked_calloc(edge_cap, sizeof(bool));
  graph_validate_index_mark_node_dups(index);
  graph_validate_index_mark_edge_dups(index);
  graph_validate_index_mark_sparse_groups(index);
}

static void graph_validate_index_free(GraphValidateIndex *index) {
  if (!index) return;
  z_program_graph_adjacency_free(&index->adjacency);
  free(index->symbol_ids);
  free(index->type_ids);
  free(index->effect_ids);
  free(index->node_dup_after);
  free(index->edge_dup_after);
  free(index->edge_sparse_first);
  *index = (GraphValidateIndex){0};
}

static bool graph_validate_nodes(const GraphValidateIndex *index, ZProgramGraphValidation *validation) {
  const ZProgramGraph *graph = index->graph;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (!graph_node_kind_valid(graph->nodes[i].kind)) return graph_validation_fail(validation, "GRF011", "node kind is invalid", graph->nodes[i].id, NULL, NULL, NULL);
    if (!graph->nodes[i].id || !graph->nodes[i].id[0]) return graph_validation_fail(validation, "GRF002", "node is missing required identity", graph->nodes[i].id, NULL, NULL, NULL);
    if (!z_program_graph_node_id_valid(graph->nodes[i].id)) return graph_validation_fail(validation, "GRF010", "node identity is malformed", graph->nodes[i].id, NULL, NULL, NULL);
    if (!graph->nodes[i].node_hash) return graph_validation_fail(validation, "GRF007", "node is missing content hash", graph->nodes[i].id, NULL, NULL, NULL);
    if (!graph_validate_node_payload(&graph->nodes[i], validation)) return false;
    if (index->node_dup_after[i]) return graph_validation_fail(validation, "GRF003", "duplicate node id", graph->nodes[i].id, NULL, NULL, NULL);
  }
  return true;
}

static bool graph_validate_edges(const GraphValidateIndex *index, ZProgramGraphValidation *validation) {
  const ZProgramGraph *graph = index->graph;
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    const char *edge_target = z_program_graph_edge_target_name(edge->target);
    if (!graph_edge_target_valid(edge->target)) return graph_validation_fail(validation, "GRF008", "edge target domain is invalid", NULL, edge->from, edge->to, edge_target);
    if (!edge->from || !edge->from[0] || z_program_graph_adjacency_node_index(&index->adjacency, edge->from) == SIZE_MAX) {
      return graph_validation_fail(validation, "GRF004", "edge source is missing", NULL, edge->from, edge->to, edge_target);
    }
    if (!graph_edge_target_exists(index, edge)) return graph_validation_fail(validation, "GRF005", "edge target is missing from declared domain", NULL, edge->from, edge->to, edge_target);
    if (!edge->kind || !edge->kind[0]) return graph_validation_fail(validation, "GRF011", "edge kind is missing", NULL, edge->from, edge->to, edge_target);
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) {
      const ZProgramGraphNode *owner = graph_find_node(index, edge->from);
      const ZProgramGraphNode *child = graph_find_node(index, edge->to);
      if (!graph_edge_child_allowed(owner, edge->kind, child)) {
        return graph_validation_fail(validation, "GRF012", "edge child kind is invalid for source node", NULL, edge->from, edge->to, edge_target);
      }
    }
    if (index->edge_dup_after[i]) {
      char message[160];
      snprintf(message, sizeof(message), "duplicate ordered edge: %s[%zu]", edge->kind, edge->order);
      return graph_validation_fail(validation, "GRF006", message, NULL, edge->from, edge->to, edge_target);
    }
  }
  return true;
}

bool z_program_graph_validate(const ZProgramGraph *graph, ZProgramGraphValidation *validation) {
  if (validation) *validation = (ZProgramGraphValidation){.ok = true, .state = Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID};
  if (!graph) return graph_validation_fail(validation, "GRF001", "program graph is missing", NULL, NULL, NULL, NULL);
  if (!graph->module_identity || !graph->module_identity[0]) return graph_validation_fail(validation, "GRF009", "program graph is missing module identity", NULL, NULL, NULL, NULL);
  GraphValidateIndex index;
  graph_validate_index_init(&index, graph);
  bool ok = graph_validate_nodes(&index, validation) && graph_validate_edges(&index, validation) && graph_validate_order_groups(&index, validation);
  for (size_t i = 0; ok && i < graph->node_len; i++) {
    ok = graph_validate_required_edges(&index, &graph->nodes[i], validation);
  }
  graph_validate_index_free(&index);
  return ok;
}
