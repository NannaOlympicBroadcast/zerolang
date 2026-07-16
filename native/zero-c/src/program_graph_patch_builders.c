#include "program_graph_patch_builders.h"
#include "type_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool builder_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static void builder_replace_text(char **slot, const char *value) {
  if (!slot) return;
  free(*slot);
  *slot = z_strdup(value ? value : "");
}

static void builder_fail(ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, const char *code, const char *message, const char *expected, const char *actual) {
  if (op) {
    op->ok = false;
    snprintf(op->code, sizeof(op->code), "%s", code ? code : "GPH000");
    snprintf(op->message, sizeof(op->message), "%s", message ? message : "program graph patch operation failed");
    builder_replace_text(&op->expected, expected);
    builder_replace_text(&op->actual, actual);
  }
  if (result) {
    result->ok = false;
    snprintf(result->code, sizeof(result->code), "%s", code ? code : "GPH000");
    snprintf(result->message, sizeof(result->message), "%s", message ? message : "program graph patch failed");
    builder_replace_text(&result->expected, expected);
    builder_replace_text(&result->actual, actual);
  }
}

static bool builder_identifier_valid(const char *text) {
  if (!text || !text[0] || !(isalpha((unsigned char)text[0]) || text[0] == '_')) return false;
  for (const unsigned char *p = (const unsigned char *)text + 1; *p; p++) {
    if (!(isalnum(*p) || *p == '_')) return false;
  }
  return true;
}

static bool builder_operator_valid(const char *text) {
  static const char *operators[] = {"+", "-", "*", "/", "%", "&&", "||", "==", "!=", "<", "<=", ">", ">=", "+%", "+|", NULL};
  for (size_t i = 0; operators[i]; i++) {
    if (builder_text_eq(text, operators[i])) return true;
  }
  return false;
}

static bool builder_type_valid(const char *text) {
  ZTypeArena arena;
  z_type_arena_init(&arena);
  ZTypeId type = Z_TYPE_ID_INVALID;
  ZTypeParseError error = {0};
  bool ok = text && text[0] && z_type_parse(&arena, text, &type, &error);
  z_type_arena_free(&arena);
  return ok;
}

static ZProgramGraphNode *builder_find_node(ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (builder_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static ZProgramGraphNode *builder_find_function(ZProgramGraph *graph, const char *name, size_t *out_count) {
  ZProgramGraphNode *found = NULL;
  size_t count = 0;
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && builder_text_eq(graph->nodes[i].name, name)) {
      found = &graph->nodes[i];
      count++;
    }
  }
  if (out_count) *out_count = count;
  return count == 1 ? found : NULL;
}

static ZProgramGraphNode *builder_child_node(ZProgramGraph *graph, const char *from, const char *kind) {
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && builder_text_eq(edge->from, from) && builder_text_eq(edge->kind, kind)) return builder_find_node(graph, edge->to);
  }
  return NULL;
}

static ZProgramGraphNode *builder_require_body(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  size_t count = 0;
  ZProgramGraphNode *function = builder_find_function(graph, op ? op->function : NULL, &count);
  if (!function) {
    builder_fail(result, op, count > 1 ? "GPH003" : "GPH004", count > 1 ? "patch function name is ambiguous" : "patch function was not found", op && op->function ? op->function : "fn", "");
    return NULL;
  }
  ZProgramGraphNode *body = builder_child_node(graph, function->id, "body");
  if (!body || body->kind != Z_PROGRAM_GRAPH_NODE_BLOCK) {
    builder_fail(result, op, "GPH004", "patch function body was not found", "body Block node", function->name);
    return NULL;
  }
  return body;
}

static void builder_reserve_nodes(ZProgramGraph *graph, size_t len) {
  if (graph->node_cap >= len) return;
  size_t next = graph->node_cap ? graph->node_cap * 2 : 8;
  while (next < len) next *= 2;
  graph->nodes = z_checked_reallocarray(graph->nodes, next, sizeof(ZProgramGraphNode));
  for (size_t i = graph->node_cap; i < next; i++) graph->nodes[i] = (ZProgramGraphNode){0};
  graph->node_cap = next;
}

static void builder_reserve_edges(ZProgramGraph *graph, size_t len) {
  if (graph->edge_cap >= len) return;
  size_t next = graph->edge_cap ? graph->edge_cap * 2 : 8;
  while (next < len) next *= 2;
  graph->edges = z_checked_reallocarray(graph->edges, next, sizeof(ZProgramGraphEdge));
  for (size_t i = graph->edge_cap; i < next; i++) graph->edges[i] = (ZProgramGraphEdge){0};
  graph->edge_cap = next;
}

static size_t builder_next_order(const ZProgramGraph *graph, const char *from, const char *kind) {
  size_t next = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && builder_text_eq(edge->from, from) && builder_text_eq(edge->kind, kind) && edge->order >= next) next = edge->order + 1;
  }
  return next;
}

static void builder_append_id_segment(ZBuf *buf, const char *text) {
  bool wrote = false;
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
    if (isalnum(*p) || *p == '_') {
      zbuf_append_char(buf, (char)*p);
      wrote = true;
    } else if (wrote && buf->data && buf->data[buf->len - 1] != '_') {
      zbuf_append_char(buf, '_');
    }
  }
  while (buf->len > 0 && buf->data[buf->len - 1] == '_') buf->data[--buf->len] = 0;
  if (!wrote) zbuf_append(buf, "node");
}

static char *builder_unique_id(ZProgramGraph *graph, const char *prefix, const char *name, size_t order) {
  ZBuf base;
  zbuf_init(&base);
  zbuf_append_char(&base, '#');
  zbuf_append(&base, prefix && prefix[0] ? prefix : "node");
  zbuf_append_char(&base, '_');
  builder_append_id_segment(&base, name);
  zbuf_appendf(&base, "_%zu", order);
  if (!builder_find_node(graph, base.data)) return base.data;
  for (size_t suffix = 1; suffix < 100000; suffix++) {
    ZBuf candidate;
    zbuf_init(&candidate);
    zbuf_append(&candidate, base.data);
    zbuf_appendf(&candidate, "_%zu", suffix);
    if (!builder_find_node(graph, candidate.data)) {
      zbuf_free(&base);
      return candidate.data;
    }
    zbuf_free(&candidate);
  }
  zbuf_free(&base);
  return z_strdup("#node_conflict");
}

static ZProgramGraphNode *builder_append_node(ZProgramGraph *graph, const char *id, ZProgramGraphNodeKind kind, const char *path, const char *name, const char *type, const char *value, bool mutable) {
  builder_reserve_nodes(graph, graph->node_len + 1);
  ZProgramGraphNode *node = &graph->nodes[graph->node_len++];
  *node = (ZProgramGraphNode){0};
  node->id = z_strdup(id);
  node->kind = kind;
  node->path = z_strdup(path && path[0] ? path : "src/main.0");
  if (name) node->name = z_strdup(name);
  if (type) node->type = z_strdup(type);
  if (value) node->value = z_strdup(value);
  node->line = 1;
  node->column = 1;
  node->is_mutable = mutable;
  return node;
}

static void builder_place_node(ZProgramGraph *graph, const char *id, int line, int column) {
  ZProgramGraphNode *node = builder_find_node(graph, id);
  if (!node) return;
  node->line = line;
  node->column = column;
}

static void builder_append_edge(ZProgramGraph *graph, const char *from, const char *kind, const char *to, size_t order) {
  builder_reserve_edges(graph, graph->edge_len + 1);
  ZProgramGraphEdge *edge = &graph->edges[graph->edge_len++];
  *edge = (ZProgramGraphEdge){0};
  edge->from = z_strdup(from);
  edge->to = z_strdup(to);
  edge->kind = z_strdup(kind);
  edge->target = Z_PROGRAM_GRAPH_EDGE_TARGET_NODE;
  edge->order = order;
}

static bool builder_add_type_ref(ZProgramGraph *graph, const char *owner, const char *name, const char *edge, const char *type, const char *path) {
  char label[160];
  snprintf(label, sizeof(label), "%s_%s_type", name ? name : "node", edge ? edge : "type");
  char *id = builder_unique_id(graph, "type", label, 0);
  builder_append_node(graph, id, Z_PROGRAM_GRAPH_NODE_TYPE_REF, path, NULL, type, NULL, false);
  builder_append_edge(graph, owner, edge, id, 0);
  free(id);
  return true;
}

static bool builder_validate_common(ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op, bool need_value) {
  if (!op || !op->function || !op->name || !op->type || (need_value && !op->value)) {
    builder_fail(result, op, "GPH001", "graph builder operation is missing required attributes", "fn, name, type, and value", "");
    return false;
  }
  if (!builder_identifier_valid(op->name)) {
    builder_fail(result, op, "GPH003", "let name must be a Zero identifier", "identifier", op->name);
    return false;
  }
  if (!builder_type_valid(op->type)) {
    builder_fail(result, op, "GPH003", "type must be valid Zero type syntax", "Zero type syntax", op->type);
    return false;
  }
  return true;
}

static bool builder_validate_optional_type(ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!op || !op->type || !op->type[0]) return true;
  if (builder_type_valid(op->type)) return true;
  builder_fail(result, op, "GPH003", "type must be valid Zero type syntax", "Zero type syntax", op->type);
  return false;
}

bool z_program_graph_patch_apply_add_let_literal(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!builder_validate_common(result, op, true)) return false;
  ZProgramGraphNode *body = builder_require_body(graph, result, op);
  if (!body) return false;
  const char *path = op->path && op->path[0] ? op->path : (body->path ? body->path : "src/main.0");
  char *body_id = z_strdup(body->id ? body->id : "");
  size_t order = builder_next_order(graph, body->id, "statement");
  int line = (int)order + 2;
  char *let_id = builder_unique_id(graph, "stmt", op->name, order);
  char *literal_id = builder_unique_id(graph, "expr", op->name, 0);
  builder_append_node(graph, let_id, Z_PROGRAM_GRAPH_NODE_LET, path, op->name, op->type, NULL, op->has_mutable_value && op->mutable_value);
  builder_append_node(graph, literal_id, Z_PROGRAM_GRAPH_NODE_LITERAL, path, NULL, op->type, op->value, false);
  builder_place_node(graph, let_id, line, 5);
  builder_place_node(graph, literal_id, line, 20);
  builder_append_edge(graph, let_id, "expr", literal_id, 0);
  builder_add_type_ref(graph, let_id, op->name, "declaredType", op->type, path);
  builder_append_edge(graph, body_id, "statement", let_id, order);
  free(body_id);
  free(let_id);
  free(literal_id);
  op->ok = true;
  return true;
}

bool z_program_graph_patch_apply_add_let_binary(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!builder_validate_common(result, op, false)) return false;
  if (!op->call || !op->left || !op->right || !builder_operator_valid(op->call) || !builder_identifier_valid(op->left) || !builder_identifier_valid(op->right)) {
    builder_fail(result, op, "GPH003", "binary let requires operator, left, and right identifiers", "operator plus identifier operands", "");
    return false;
  }
  ZProgramGraphNode *body = builder_require_body(graph, result, op);
  if (!body) return false;
  const char *path = op->path && op->path[0] ? op->path : (body->path ? body->path : "src/main.0");
  char *body_id = z_strdup(body->id ? body->id : "");
  size_t order = builder_next_order(graph, body->id, "statement");
  int line = (int)order + 2;
  char *let_id = builder_unique_id(graph, "stmt", op->name, order);
  char *call_id = builder_unique_id(graph, "expr", op->name, 0);
  char *left_id = builder_unique_id(graph, "expr", op->left, 0);
  char *right_id = builder_unique_id(graph, "expr", op->right, 1);
  builder_append_node(graph, let_id, Z_PROGRAM_GRAPH_NODE_LET, path, op->name, op->type, NULL, false);
  builder_append_node(graph, call_id, Z_PROGRAM_GRAPH_NODE_CALL, path, op->call, op->type, NULL, false);
  builder_append_node(graph, left_id, Z_PROGRAM_GRAPH_NODE_IDENTIFIER, path, op->left, op->type, NULL, false);
  builder_append_node(graph, right_id, Z_PROGRAM_GRAPH_NODE_IDENTIFIER, path, op->right, op->type, NULL, false);
  builder_place_node(graph, let_id, line, 5);
  builder_place_node(graph, call_id, line, 20);
  builder_place_node(graph, left_id, line, 22);
  builder_place_node(graph, right_id, line, 26);
  builder_append_edge(graph, call_id, "left", left_id, 0);
  builder_append_edge(graph, call_id, "right", right_id, 1);
  builder_append_edge(graph, let_id, "expr", call_id, 0);
  builder_add_type_ref(graph, let_id, op->name, "declaredType", op->type, path);
  builder_append_edge(graph, body_id, "statement", let_id, order);
  free(body_id);
  free(let_id);
  free(call_id);
  free(left_id);
  free(right_id);
  op->ok = true;
  return true;
}

bool z_program_graph_patch_apply_add_return_value(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!op || !op->function || !op->value || !builder_identifier_valid(op->value)) {
    builder_fail(result, op, "GPH001", "addReturnValue only returns identifiers; use addReturnExpr for null, literals, calls, or other expressions", "fn and identifier value, or addReturnExpr fn=\"...\" expr=\"...\"", "");
    return false;
  }
  if (!builder_validate_optional_type(result, op)) return false;
  ZProgramGraphNode *body = builder_require_body(graph, result, op);
  if (!body) return false;
  const char *path = op->path && op->path[0] ? op->path : (body->path ? body->path : "src/main.0");
  char *body_id = z_strdup(body->id ? body->id : "");
  const char *type = op->type && op->type[0] ? op->type : "Unknown";
  size_t order = builder_next_order(graph, body->id, "statement");
  int line = (int)order + 2;
  char *return_id = builder_unique_id(graph, "stmt", "return", order);
  char *value_id = builder_unique_id(graph, "expr", op->value, order);
  builder_append_node(graph, return_id, Z_PROGRAM_GRAPH_NODE_RETURN, path, NULL, NULL, NULL, false);
  builder_append_node(graph, value_id, Z_PROGRAM_GRAPH_NODE_IDENTIFIER, path, op->value, type, NULL, false);
  builder_place_node(graph, return_id, line, 5);
  builder_place_node(graph, value_id, line, 12);
  builder_append_edge(graph, return_id, "expr", value_id, 0);
  builder_append_edge(graph, body_id, "statement", return_id, order);
  free(body_id);
  free(return_id);
  free(value_id);
  op->ok = true;
  return true;
}

bool z_program_graph_patch_apply_add_check_write_value(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op) {
  if (!op || !op->function || !op->value || !builder_identifier_valid(op->value)) {
    builder_fail(result, op, "GPH001", "write value operation requires fn and identifier value", "fn and value", "");
    return false;
  }
  if (!builder_validate_optional_type(result, op)) return false;
  ZProgramGraphNode *body = builder_require_body(graph, result, op);
  if (!body) return false;
  const char *path = op->path && op->path[0] ? op->path : (body->path ? body->path : "src/main.0");
  char *body_id = z_strdup(body->id ? body->id : "");
  const char *type = op->type && op->type[0] ? op->type : "String";
  size_t order = builder_next_order(graph, body->id, "statement");
  int line = (int)order + 2;
  char *check_id = builder_unique_id(graph, "stmt", "write_value", order);
  char *call_id = builder_unique_id(graph, "expr", "write_call", order);
  char *write_id = builder_unique_id(graph, "expr", "write", order);
  char *out_id = builder_unique_id(graph, "expr", "out", order);
  char *world_id = builder_unique_id(graph, "expr", "world", order);
  char *value_id = builder_unique_id(graph, "expr", op->value, order);
  builder_append_node(graph, check_id, Z_PROGRAM_GRAPH_NODE_CHECK, path, NULL, NULL, NULL, false);
  builder_append_node(graph, call_id, Z_PROGRAM_GRAPH_NODE_METHOD_CALL, path, "write", "Void", NULL, false);
  builder_append_node(graph, write_id, Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS, path, "write", NULL, NULL, false);
  builder_append_node(graph, out_id, Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS, path, "out", NULL, NULL, false);
  builder_append_node(graph, world_id, Z_PROGRAM_GRAPH_NODE_IDENTIFIER, path, "world", NULL, NULL, false);
  builder_append_node(graph, value_id, Z_PROGRAM_GRAPH_NODE_IDENTIFIER, path, op->value, type, NULL, false);
  builder_place_node(graph, check_id, line, 5);
  builder_place_node(graph, call_id, line, 11);
  builder_place_node(graph, write_id, line, 21);
  builder_place_node(graph, out_id, line, 17);
  builder_place_node(graph, world_id, line, 11);
  builder_place_node(graph, value_id, line, 27);
  builder_append_edge(graph, out_id, "left", world_id, 0);
  builder_append_edge(graph, write_id, "left", out_id, 0);
  builder_append_edge(graph, call_id, "left", write_id, 0);
  builder_append_edge(graph, call_id, "arg", value_id, 0);
  builder_append_edge(graph, check_id, "expr", call_id, 0);
  builder_append_edge(graph, body_id, "statement", check_id, order);
  free(body_id);
  free(check_id);
  free(call_id);
  free(write_id);
  free(out_id);
  free(world_id);
  free(value_id);
  op->ok = true;
  return true;
}
