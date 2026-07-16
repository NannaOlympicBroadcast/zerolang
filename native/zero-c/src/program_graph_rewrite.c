#include "program_graph_rewrite.h"

#include "canonical_text.h"
#include "program_graph_handle.h"
#include "program_graph_import.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { REWRITE_METAVAR_MAX = 26 };
enum { REWRITE_MATCH_DEPTH_MAX = 96 };

static const char *const rewrite_metavar_prefix = "__zero_mv_";

static bool rewrite_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static void rewrite_fail(ZDiag *diag, const char *message, const char *expected, const char *actual) {
  if (!diag || diag->code != 0) return;
  diag->code = 2002;
  diag->path = "<rewrite>";
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "graph rewrite failed");
  snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "a canonical projection expression with $A-style metavariables");
  snprintf(diag->actual, sizeof(diag->actual), "%.250s", actual ? actual : "");
  snprintf(diag->help, sizeof(diag->help), "patterns are expressions exactly as zero view prints them; metavariables $A, $B bind arbitrary subtrees");
}

static const ZProgramGraphNode *rewrite_node_by_id(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (rewrite_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphNode *rewrite_child(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  for (size_t i = 0; graph && from && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge->order == order && rewrite_text_eq(edge->from, from) && rewrite_text_eq(edge->kind, kind)) {
      return rewrite_node_by_id(graph, edge->to);
    }
  }
  return NULL;
}

// ---- metavariable substitution ----

static bool rewrite_substitute_metavars(const char *text, ZBuf *out, bool names_seen[REWRITE_METAVAR_MAX], ZDiag *diag, const char *what) {
  bool quoted = false;
  bool escaped = false;
  for (const char *cursor = text ? text : ""; *cursor; cursor++) {
    char ch = *cursor;
    if (quoted) {
      zbuf_append_char(out, ch);
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') quoted = false;
      continue;
    }
    if (ch == '"') {
      quoted = true;
      zbuf_append_char(out, ch);
      continue;
    }
    if (ch != '$') {
      zbuf_append_char(out, ch);
      continue;
    }
    char name = cursor[1];
    if (name < 'A' || name > 'Z' || (isalnum((unsigned char)cursor[2]) || cursor[2] == '_')) {
      char message[160];
      snprintf(message, sizeof(message), "invalid metavariable in rewrite %s", what);
      rewrite_fail(diag, message, "$A through $Z", cursor);
      return false;
    }
    zbuf_append(out, rewrite_metavar_prefix);
    zbuf_append_char(out, name);
    if (names_seen) names_seen[name - 'A'] = true;
    cursor++;
  }
  return true;
}

static int rewrite_metavar_index(const ZProgramGraphNode *node) {
  if (!node || node->kind != Z_PROGRAM_GRAPH_NODE_IDENTIFIER || !node->name) return -1;
  size_t prefix_len = strlen(rewrite_metavar_prefix);
  if (strncmp(node->name, rewrite_metavar_prefix, prefix_len) != 0) return -1;
  char name = node->name[prefix_len];
  if (name < 'A' || name > 'Z' || node->name[prefix_len + 1]) return -1;
  return name - 'A';
}

// ---- expression parsing (pattern, template) ----

static bool rewrite_parse_expression_graph(const char *expr_text, ZProgramGraph *out_graph, const ZProgramGraphNode **out_root, ZDiag *diag, const char *what) {
  ZBuf source;
  zbuf_init(&source);
  zbuf_append(&source, "fn __zero_rewrite_expr() -> Void raises {\n    return ");
  zbuf_append(&source, expr_text ? expr_text : "");
  zbuf_append(&source, "\n}\n");
  Program program = {0};
  ZDiag parse_diag = {0};
  if (!z_parse_canonical_text_program_source(source.data ? source.data : "", &program, &parse_diag)) {
    char message[160];
    snprintf(message, sizeof(message), "rewrite %s did not parse as a Zero expression", what);
    rewrite_fail(diag, message, parse_diag.expected[0] ? parse_diag.expected : "an expression in zero view syntax", parse_diag.message[0] ? parse_diag.message : expr_text);
    z_free_program(&program);
    zbuf_free(&source);
    return false;
  }
  SourceInput input = {0};
  input.source_file = z_strdup("<rewrite>");
  input.source = z_strdup(source.data ? source.data : "");
  input.canonical_text_source = true;
  bool ok = z_program_graph_from_program(&input, &program, out_graph);
  const ZProgramGraphNode *root = NULL;
  if (ok) {
    const ZProgramGraphNode *fn = NULL;
    for (size_t i = 0; i < out_graph->node_len; i++) {
      if (out_graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && rewrite_text_eq(out_graph->nodes[i].name, "__zero_rewrite_expr")) fn = &out_graph->nodes[i];
    }
    const ZProgramGraphNode *body = fn ? rewrite_child(out_graph, fn->id, "body", 0) : NULL;
    const ZProgramGraphNode *ret = body ? rewrite_child(out_graph, body->id, "statement", 0) : NULL;
    root = ret ? rewrite_child(out_graph, ret->id, "expr", 0) : NULL;
    if (!root) ok = false;
  }
  if (!ok) {
    char message[160];
    snprintf(message, sizeof(message), "rewrite %s could not build an expression subtree", what);
    rewrite_fail(diag, message, "a lowerable Zero expression", expr_text);
  }
  z_free_source(&input);
  z_free_program(&program);
  zbuf_free(&source);
  if (ok) *out_root = root;
  return ok;
}

// ---- expression rendering (canonical text, reparseable) ----

static bool rewrite_kind_supported(ZProgramGraphNodeKind kind) {
  switch (kind) {
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
    case Z_PROGRAM_GRAPH_NODE_SLICE:
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
    case Z_PROGRAM_GRAPH_NODE_CAST:
    case Z_PROGRAM_GRAPH_NODE_BORROW:
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
    case Z_PROGRAM_GRAPH_NODE_META:
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL:
    case Z_PROGRAM_GRAPH_NODE_TYPE_REF:
      return true;
    default:
      return false;
  }
}

static bool rewrite_binary_operator(const char *text) {
  static const char *const ops[] = {"+", "-", "*", "/", "%", "&&", "||", "==", "!=", "<", "<=", ">", ">=", "+%", "+|", NULL};
  for (size_t i = 0; ops[i]; i++) {
    if (rewrite_text_eq(text, ops[i])) return true;
  }
  return false;
}

static int rewrite_binary_precedence(const char *op) {
  if (rewrite_text_eq(op, "||")) return 1;
  if (rewrite_text_eq(op, "&&")) return 2;
  if (rewrite_text_eq(op, "==") || rewrite_text_eq(op, "!=")) return 3;
  if (rewrite_text_eq(op, "<") || rewrite_text_eq(op, ">") || rewrite_text_eq(op, "<=") || rewrite_text_eq(op, ">=")) return 4;
  if (rewrite_text_eq(op, "+") || rewrite_text_eq(op, "-") || rewrite_text_eq(op, "+%") || rewrite_text_eq(op, "+|")) return 5;
  return 6;
}

static bool rewrite_node_is_binary(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  return node &&
         (node->kind == Z_PROGRAM_GRAPH_NODE_CALL || node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) &&
         rewrite_binary_operator(node->name) &&
         rewrite_child(graph, node->id, "left", 0) &&
         rewrite_child(graph, node->id, "right", 1);
}

static int rewrite_node_precedence(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  if (!node) return 10;
  if (rewrite_node_is_binary(graph, node)) return rewrite_binary_precedence(node->name);
  if (node->kind == Z_PROGRAM_GRAPH_NODE_CAST) return 7;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_BORROW || node->kind == Z_PROGRAM_GRAPH_NODE_CHECK ||
      node->kind == Z_PROGRAM_GRAPH_NODE_RESCUE || node->kind == Z_PROGRAM_GRAPH_NODE_META) {
    return 8;
  }
  return 10;
}

typedef struct {
  const ZProgramGraphNode *bound[REWRITE_METAVAR_MAX];
} RewriteBindings;

typedef struct {
  const ZProgramGraph *graph;
  const ZProgramGraph *binding_graph;   /* target graph the bindings live in */
  const RewriteBindings *bindings;      /* NULL when rendering plain subtrees */
  ZBuf *out;
  bool ok;
} RewriteRender;

static int rewrite_metavar_index(const ZProgramGraphNode *node);

static void rewrite_render_expr(RewriteRender *render, const ZProgramGraphNode *node, int parent_prec, bool right_assoc);
static void rewrite_render_plain(RewriteRender *render, const ZProgramGraphNode *node, int prec);

static void rewrite_render_escaped_string(ZBuf *out, const char *text) {
  zbuf_append_char(out, '"');
  for (const char *p = text ? text : ""; *p; p++) {
    unsigned char ch = (unsigned char)*p;
    switch (ch) {
      case '\\': zbuf_append(out, "\\\\"); break;
      case '"': zbuf_append(out, "\\\""); break;
      case '\n': zbuf_append(out, "\\n"); break;
      case '\r': zbuf_append(out, "\\r"); break;
      case '\t': zbuf_append(out, "\\t"); break;
      default:
        if (ch < 0x20) zbuf_appendf(out, "\\x%02x", ch);
        else zbuf_append_char(out, (char)ch);
        break;
    }
  }
  zbuf_append_char(out, '"');
}

static void rewrite_render_char_literal(ZBuf *out, const char *text) {
  unsigned value = (unsigned)strtoul(text ? text : "0", NULL, 10);
  zbuf_append_char(out, '\'');
  if (value == '\n') zbuf_append(out, "\\n");
  else if (value == '\r') zbuf_append(out, "\\r");
  else if (value == '\t') zbuf_append(out, "\\t");
  else if (value == '\'') zbuf_append(out, "\\'");
  else if (value == '\\') zbuf_append(out, "\\\\");
  else if (value >= 32 && value < 127) zbuf_append_char(out, (char)value);
  else zbuf_appendf(out, "\\x%02x", value & 0xff);
  zbuf_append_char(out, '\'');
}

static void rewrite_render_type_args(RewriteRender *render, const ZProgramGraphNode *node) {
  bool first = true;
  for (size_t order = 0;; order++) {
    const ZProgramGraphNode *arg = rewrite_child(render->graph, node->id, "typeArg", order);
    if (!arg) break;
    zbuf_append(render->out, first ? "<" : ", ");
    zbuf_append(render->out, arg->type ? arg->type : "");
    first = false;
  }
  if (!first) zbuf_append_char(render->out, '>');
}

static void rewrite_render_args(RewriteRender *render, const ZProgramGraphNode *node) {
  zbuf_append_char(render->out, '(');
  for (size_t order = 0;; order++) {
    const ZProgramGraphNode *arg = rewrite_child(render->graph, node->id, "arg", order);
    if (!arg) break;
    if (order > 0) zbuf_append(render->out, ", ");
    rewrite_render_expr(render, arg, 0, false);
  }
  zbuf_append_char(render->out, ')');
}

static void rewrite_render_literal(RewriteRender *render, const ZProgramGraphNode *node) {
  if (rewrite_text_eq(node->type, "String")) {
    rewrite_render_escaped_string(render->out, node->value);
    return;
  }
  if (rewrite_text_eq(node->type, "char")) {
    rewrite_render_char_literal(render->out, node->value);
    return;
  }
  zbuf_append(render->out, node->value && node->value[0] ? node->value : "0");
}

static void rewrite_render_expr(RewriteRender *render, const ZProgramGraphNode *node, int parent_prec, bool right_assoc) {
  if (!render->ok) return;
  if (!node) {
    render->ok = false;
    return;
  }
  if (render->bindings) {
    int metavar = rewrite_metavar_index(node);
    if (metavar >= 0) {
      const ZProgramGraphNode *bound = render->bindings->bound[metavar];
      if (!bound) {
        render->ok = false;
        return;
      }
      RewriteRender bound_render = {.graph = render->binding_graph, .out = render->out, .ok = true};
      rewrite_render_expr(&bound_render, bound, parent_prec, right_assoc);
      if (!bound_render.ok) render->ok = false;
      return;
    }
  }
  if (!rewrite_kind_supported(node->kind)) {
    render->ok = false;
    return;
  }
  const ZProgramGraph *graph = render->graph;
  bool prefix_deref = (node->kind == Z_PROGRAM_GRAPH_NODE_CALL || node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) && rewrite_text_eq(node->value, "prefix-deref");
  int prec = prefix_deref ? 8 : rewrite_node_precedence(graph, node);
  bool grouped = prec > 0 && prec < 10 && (prec < parent_prec || (right_assoc && prec == parent_prec));
  if (grouped) zbuf_append_char(render->out, '(');
  if (prefix_deref) {
    const ZProgramGraphNode *operand = rewrite_child(graph, node->id, "arg", 0);
    zbuf_append_char(render->out, '*');
    rewrite_render_expr(render, operand, prec, false);
  } else if (rewrite_node_is_binary(graph, node)) {
    rewrite_render_expr(render, rewrite_child(graph, node->id, "left", 0), prec, false);
    zbuf_appendf(render->out, " %s ", node->name);
    rewrite_render_expr(render, rewrite_child(graph, node->id, "right", 1), prec, true);
  } else {
    rewrite_render_plain(render, node, prec);
  }
  if (grouped) zbuf_append_char(render->out, ')');
}

static void rewrite_render_plain(RewriteRender *render, const ZProgramGraphNode *node, int prec) {
  const ZProgramGraph *graph = render->graph;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
      zbuf_append(render->out, node->name && node->name[0] ? node->name : "__unnamed");
      rewrite_render_type_args(render, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
      rewrite_render_literal(render, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
      rewrite_render_expr(render, rewrite_child(graph, node->id, "left", 0), 10, false);
      zbuf_append_char(render->out, '.');
      zbuf_append(render->out, node->name && node->name[0] ? node->name : "__unnamed");
      rewrite_render_type_args(render, node);
      break;
    case Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS:
      rewrite_render_expr(render, rewrite_child(graph, node->id, "left", 0), 10, false);
      zbuf_append_char(render->out, '[');
      rewrite_render_expr(render, rewrite_child(graph, node->id, "right", 1), 0, false);
      zbuf_append_char(render->out, ']');
      break;
    case Z_PROGRAM_GRAPH_NODE_SLICE: {
      rewrite_render_expr(render, rewrite_child(graph, node->id, "left", 0), 10, false);
      zbuf_append_char(render->out, '[');
      const ZProgramGraphNode *start = rewrite_child(graph, node->id, "arg", 0);
      const ZProgramGraphNode *end = rewrite_child(graph, node->id, "arg", 1);
      if (start) rewrite_render_expr(render, start, 0, false);
      zbuf_append(render->out, "..");
      if (end) rewrite_render_expr(render, end, 0, false);
      zbuf_append_char(render->out, ']');
      break;
    }
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL: {
      const ZProgramGraphNode *callee = rewrite_child(graph, node->id, "left", 0);
      if (callee) rewrite_render_expr(render, callee, 10, false);
      else zbuf_append(render->out, node->name && node->name[0] ? node->name : "__unnamed");
      rewrite_render_type_args(render, node);
      rewrite_render_args(render, node);
      break;
    }
    case Z_PROGRAM_GRAPH_NODE_CAST:
      rewrite_render_expr(render, rewrite_child(graph, node->id, "left", 0), prec + 1, false);
      zbuf_append(render->out, " as ");
      zbuf_append(render->out, node->name && node->name[0] ? node->name : (node->type ? node->type : ""));
      break;
    case Z_PROGRAM_GRAPH_NODE_BORROW:
      zbuf_append(render->out, node->is_mutable ? "&mut " : "&");
      rewrite_render_expr(render, rewrite_child(graph, node->id, "left", 0), prec, false);
      break;
    case Z_PROGRAM_GRAPH_NODE_RESCUE:
      zbuf_append(render->out, "rescue (");
      rewrite_render_expr(render, rewrite_child(graph, node->id, "left", 0), 0, false);
      zbuf_append(render->out, ") err (");
      rewrite_render_expr(render, rewrite_child(graph, node->id, "right", 1), 0, false);
      zbuf_append_char(render->out, ')');
      break;
    case Z_PROGRAM_GRAPH_NODE_META:
      zbuf_append(render->out, "meta ");
      rewrite_render_expr(render, rewrite_child(graph, node->id, "left", 0), prec, false);
      break;
    case Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL: {
      zbuf_append(render->out, node->name && node->name[0] ? node->name : "__unnamed");
      zbuf_append(render->out, " {");
      bool first = true;
      for (size_t order = 0;; order++) {
        const ZProgramGraphNode *field = rewrite_child(graph, node->id, "field", order);
        if (!field) break;
        zbuf_append(render->out, first ? " " : ", ");
        zbuf_append(render->out, field->name ? field->name : "");
        zbuf_append(render->out, ": ");
        rewrite_render_expr(render, rewrite_child(graph, field->id, "value", 0), 0, false);
        first = false;
      }
      zbuf_append(render->out, first ? "}" : " }");
      break;
    }
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL: {
      zbuf_append_char(render->out, '[');
      if (rewrite_text_eq(node->value, "repeat")) {
        rewrite_render_expr(render, rewrite_child(graph, node->id, "arg", 0), 0, false);
        zbuf_append(render->out, "; ");
        rewrite_render_expr(render, rewrite_child(graph, node->id, "arg", 1), 0, false);
      } else {
        for (size_t order = 0;; order++) {
          const ZProgramGraphNode *item = rewrite_child(graph, node->id, "arg", order);
          if (!item) break;
          if (order > 0) zbuf_append(render->out, ", ");
          rewrite_render_expr(render, item, 0, false);
        }
      }
      zbuf_append_char(render->out, ']');
      break;
    }
    default:
      render->ok = false;
      break;
  }
}

static bool rewrite_render_to(const ZProgramGraph *graph, const ZProgramGraphNode *node, ZBuf *out) {
  RewriteRender render = {.graph = graph, .out = out, .ok = true};
  rewrite_render_expr(&render, node, 0, false);
  return render.ok;
}

// ---- structural matching ----

typedef struct {
  const char *kind;
  size_t order;
  const char *to;
} RewriteEdgeRef;

static int rewrite_edge_ref_compare(const void *left, const void *right) {
  const RewriteEdgeRef *a = (const RewriteEdgeRef *)left;
  const RewriteEdgeRef *b = (const RewriteEdgeRef *)right;
  int by_kind = strcmp(a->kind ? a->kind : "", b->kind ? b->kind : "");
  if (by_kind != 0) return by_kind;
  if (a->order != b->order) return a->order < b->order ? -1 : 1;
  return 0;
}

enum { REWRITE_EDGE_MAX = 64 };

static size_t rewrite_collect_child_edges(const ZProgramGraph *graph, const char *from, RewriteEdgeRef *out, size_t cap) {
  size_t len = 0;
  for (size_t i = 0; graph && from && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !rewrite_text_eq(edge->from, from)) continue;
    if (len >= cap) return (size_t)-1;
    out[len].kind = edge->kind ? edge->kind : "";
    out[len].order = edge->order;
    out[len].to = edge->to;
    len++;
  }
  qsort(out, len, sizeof(RewriteEdgeRef), rewrite_edge_ref_compare);
  return len;
}

static bool rewrite_nodes_equal(const ZProgramGraph *graph, const ZProgramGraphNode *left, const ZProgramGraphNode *right, unsigned depth);

static bool rewrite_node_payload_equal(const ZProgramGraphNode *left, const ZProgramGraphNode *right) {
  if (left->kind != right->kind) return false;
  switch (left->kind) {
    case Z_PROGRAM_GRAPH_NODE_LITERAL:
      return rewrite_text_eq(left->value, right->value) &&
             rewrite_text_eq(left->type, "String") == rewrite_text_eq(right->type, "String") &&
             rewrite_text_eq(left->type, "char") == rewrite_text_eq(right->type, "char");
    case Z_PROGRAM_GRAPH_NODE_TYPE_REF:
      return rewrite_text_eq(left->type, right->type);
    case Z_PROGRAM_GRAPH_NODE_CAST:
      return rewrite_text_eq(left->name && left->name[0] ? left->name : left->type, right->name && right->name[0] ? right->name : right->type);
    case Z_PROGRAM_GRAPH_NODE_BORROW:
      return left->is_mutable == right->is_mutable;
    case Z_PROGRAM_GRAPH_NODE_CALL:
    case Z_PROGRAM_GRAPH_NODE_METHOD_CALL:
      return rewrite_text_eq(left->name, right->name) && rewrite_text_eq(left->value, right->value);
    case Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL:
      return rewrite_text_eq(left->value, right->value);
    default:
      return rewrite_text_eq(left->name, right->name);
  }
}

/* Structural equality of two subtrees in the same graph (metavar reuse). */
static bool rewrite_nodes_equal(const ZProgramGraph *graph, const ZProgramGraphNode *left, const ZProgramGraphNode *right, unsigned depth) {
  if (!left || !right || depth > REWRITE_MATCH_DEPTH_MAX) return false;
  if (left == right) return true;
  if (!rewrite_node_payload_equal(left, right)) return false;
  RewriteEdgeRef left_edges[REWRITE_EDGE_MAX];
  RewriteEdgeRef right_edges[REWRITE_EDGE_MAX];
  size_t left_len = rewrite_collect_child_edges(graph, left->id, left_edges, REWRITE_EDGE_MAX);
  size_t right_len = rewrite_collect_child_edges(graph, right->id, right_edges, REWRITE_EDGE_MAX);
  if (left_len == (size_t)-1 || right_len == (size_t)-1 || left_len != right_len) return false;
  for (size_t i = 0; i < left_len; i++) {
    if (!rewrite_text_eq(left_edges[i].kind, right_edges[i].kind) || left_edges[i].order != right_edges[i].order) return false;
    if (!rewrite_nodes_equal(graph, rewrite_node_by_id(graph, left_edges[i].to), rewrite_node_by_id(graph, right_edges[i].to), depth + 1)) return false;
  }
  return true;
}

static bool rewrite_match_node(const ZProgramGraph *pattern_graph, const ZProgramGraphNode *pattern, const ZProgramGraph *graph, const ZProgramGraphNode *node, RewriteBindings *bindings, unsigned depth) {
  if (!pattern || !node || depth > REWRITE_MATCH_DEPTH_MAX) return false;
  int metavar = rewrite_metavar_index(pattern);
  if (metavar >= 0) {
    if (!rewrite_kind_supported(node->kind)) return false;
    if (bindings->bound[metavar]) return rewrite_nodes_equal(graph, bindings->bound[metavar], node, 0);
    bindings->bound[metavar] = node;
    return true;
  }
  if (!rewrite_kind_supported(pattern->kind) || !rewrite_kind_supported(node->kind)) return false;
  if (!rewrite_node_payload_equal(pattern, node)) return false;
  RewriteEdgeRef pattern_edges[REWRITE_EDGE_MAX];
  RewriteEdgeRef node_edges[REWRITE_EDGE_MAX];
  size_t pattern_len = rewrite_collect_child_edges(pattern_graph, pattern->id, pattern_edges, REWRITE_EDGE_MAX);
  size_t node_len = rewrite_collect_child_edges(graph, node->id, node_edges, REWRITE_EDGE_MAX);
  if (pattern_len == (size_t)-1 || node_len == (size_t)-1 || pattern_len != node_len) return false;
  for (size_t i = 0; i < pattern_len; i++) {
    if (!rewrite_text_eq(pattern_edges[i].kind, node_edges[i].kind) || pattern_edges[i].order != node_edges[i].order) return false;
    if (!rewrite_match_node(pattern_graph, rewrite_node_by_id(pattern_graph, pattern_edges[i].to), graph, rewrite_node_by_id(graph, node_edges[i].to), bindings, depth + 1)) return false;
  }
  return true;
}

// ---- template instantiation ----

/* Renders the template with bound target subtrees substituted for
 * metavariables; output reparses to the instantiated expression. */
static bool rewrite_render_template(const ZProgramGraph *template_graph, const ZProgramGraphNode *node, const ZProgramGraph *graph, const RewriteBindings *bindings, ZBuf *out) {
  RewriteRender render = {.graph = template_graph, .binding_graph = graph, .bindings = bindings, .out = out, .ok = true};
  rewrite_render_expr(&render, node, 0, false);
  return render.ok;
}

// ---- candidate enumeration ----

typedef struct {
  const ZProgramGraph *graph;
  const ZProgramGraph *pattern_graph;
  const ZProgramGraphNode *pattern_root;
  const ZProgramGraph *template_graph;
  const ZProgramGraphNode *template_root;
  const ZProgramGraphHandleShortener *shortener;
  const ZProgramGraphNode *current_function;
  ZProgramGraphRewriteResult *result;
  bool failed;
} RewriteScan;

static void rewrite_result_push(RewriteScan *scan, const ZProgramGraphNode *node, const RewriteBindings *bindings) {
  ZBuf before;
  ZBuf after;
  zbuf_init(&before);
  zbuf_init(&after);
  bool ok = rewrite_render_to(scan->graph, node, &before);
  if (ok) ok = rewrite_render_template(scan->template_graph, scan->template_root, scan->graph, bindings, &after);
  if (!ok) {
    zbuf_free(&before);
    zbuf_free(&after);
    scan->result->skipped_unsupported++;
    return;
  }
  ZProgramGraphRewriteResult *result = scan->result;
  if (result->len == result->cap) {
    size_t next = result->cap ? result->cap * 2 : 8;
    result->items = z_checked_reallocarray(result->items, next, sizeof(ZProgramGraphRewriteMatch));
    for (size_t i = result->cap; i < next; i++) result->items[i] = (ZProgramGraphRewriteMatch){0};
    result->cap = next;
  }
  ZProgramGraphRewriteMatch *match = &result->items[result->len++];
  match->node_id = z_strdup(node->id ? node->id : "");
  ZBuf short_handle;
  zbuf_init(&short_handle);
  z_program_graph_append_short_handle(scan->shortener, node->id, &short_handle);
  match->short_handle = short_handle.data ? short_handle.data : z_strdup(node->id ? node->id : "");
  match->function_name = z_strdup(scan->current_function && scan->current_function->name ? scan->current_function->name : "");
  match->path = z_strdup(node->path && node->path[0] ? node->path : "");
  match->before = before.data ? before.data : z_strdup("");
  match->after = after.data ? after.data : z_strdup("");
}

/* Pre-order walk over owned child nodes; matched subtrees are not descended. */
static void rewrite_scan_node(RewriteScan *scan, const ZProgramGraphNode *node, unsigned depth) {
  if (!node || scan->failed || depth > REWRITE_MATCH_DEPTH_MAX) return;
  bool is_expression_position = rewrite_kind_supported(node->kind) && node->kind != Z_PROGRAM_GRAPH_NODE_TYPE_REF;
  if (is_expression_position) {
    RewriteBindings bindings = {0};
    if (rewrite_match_node(scan->pattern_graph, scan->pattern_root, scan->graph, node, &bindings, 0)) {
      rewrite_result_push(scan, node, &bindings);
      return;
    }
  } else if (node->kind == Z_PROGRAM_GRAPH_NODE_EXPRESSION || node->kind == Z_PROGRAM_GRAPH_NODE_STATEMENT) {
    /* macro-like or opaque constructs: note and skip silently */
    scan->result->skipped_unsupported++;
    return;
  }
  RewriteEdgeRef edges[REWRITE_EDGE_MAX];
  size_t len = rewrite_collect_child_edges(scan->graph, node->id, edges, REWRITE_EDGE_MAX);
  if (len == (size_t)-1) {
    scan->result->skipped_unsupported++;
    return;
  }
  for (size_t i = 0; i < len; i++) {
    rewrite_scan_node(scan, rewrite_node_by_id(scan->graph, edges[i].to), depth + 1);
  }
}

static bool rewrite_function_selected(const ZProgramGraphNode *function, const char *filter) {
  if (!filter || !filter[0]) return true;
  return function && (rewrite_text_eq(function->name, filter) || rewrite_text_eq(function->id, filter));
}

bool z_program_graph_rewrite_collect(const ZProgramGraph *graph,
                                     const char *pattern,
                                     const char *template_text,
                                     const char *function_filter,
                                     ZProgramGraphRewriteResult *out,
                                     ZDiag *diag) {
  if (!out) return false;
  *out = (ZProgramGraphRewriteResult){0};
  if (!pattern || !pattern[0] || !template_text || !template_text[0]) {
    rewrite_fail(diag, "rewrite needs both --rewrite <pattern> and --to <template>", "zero patch --rewrite '<pattern>' --to '<template>' [--fn <name>] [--apply]", pattern && pattern[0] ? "--to" : "--rewrite");
    return false;
  }
  bool pattern_vars[REWRITE_METAVAR_MAX] = {0};
  bool template_vars[REWRITE_METAVAR_MAX] = {0};
  ZBuf pattern_text;
  ZBuf template_substituted;
  zbuf_init(&pattern_text);
  zbuf_init(&template_substituted);
  if (!rewrite_substitute_metavars(pattern, &pattern_text, pattern_vars, diag, "pattern") ||
      !rewrite_substitute_metavars(template_text, &template_substituted, template_vars, diag, "template")) {
    zbuf_free(&pattern_text);
    zbuf_free(&template_substituted);
    return false;
  }
  for (int i = 0; i < REWRITE_METAVAR_MAX; i++) {
    if (template_vars[i] && !pattern_vars[i]) {
      char message[160];
      snprintf(message, sizeof(message), "rewrite template uses $%c which the pattern does not bind", 'A' + i);
      rewrite_fail(diag, message, "template metavariables bound by the pattern", template_text);
      zbuf_free(&pattern_text);
      zbuf_free(&template_substituted);
      return false;
    }
  }
  ZProgramGraph pattern_graph = {0};
  ZProgramGraph template_graph = {0};
  const ZProgramGraphNode *pattern_root = NULL;
  const ZProgramGraphNode *template_root = NULL;
  bool ok = rewrite_parse_expression_graph(pattern_text.data ? pattern_text.data : "", &pattern_graph, &pattern_root, diag, "pattern");
  if (ok) ok = rewrite_parse_expression_graph(template_substituted.data ? template_substituted.data : "", &template_graph, &template_root, diag, "template");
  zbuf_free(&pattern_text);
  zbuf_free(&template_substituted);
  if (!ok) {
    z_program_graph_free(&pattern_graph);
    z_program_graph_free(&template_graph);
    return false;
  }
  if (rewrite_metavar_index(pattern_root) >= 0) {
    rewrite_fail(diag, "rewrite pattern cannot be a bare metavariable", "a pattern with structure around its metavariables", pattern);
    z_program_graph_free(&pattern_graph);
    z_program_graph_free(&template_graph);
    return false;
  }
  ZProgramGraphHandleShortener shortener;
  z_program_graph_handle_shortener_init(&shortener, graph);
  RewriteScan scan = {
    .graph = graph,
    .pattern_graph = &pattern_graph,
    .pattern_root = pattern_root,
    .template_graph = &template_graph,
    .template_root = template_root,
    .shortener = &shortener,
    .result = out,
  };
  bool matched_function = false;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) continue;
    if (!rewrite_function_selected(node, function_filter)) continue;
    matched_function = true;
    out->functions_scanned++;
    scan.current_function = node;
    rewrite_scan_node(&scan, node, 0);
  }
  z_program_graph_handle_shortener_free(&shortener);
  z_program_graph_free(&pattern_graph);
  z_program_graph_free(&template_graph);
  if (function_filter && function_filter[0] && !matched_function) {
    char message[160];
    snprintf(message, sizeof(message), "rewrite --fn function '%s' was not found", function_filter);
    rewrite_fail(diag, message, "an existing function name", function_filter);
    z_program_graph_rewrite_result_free(out);
    return false;
  }
  return true;
}

static void rewrite_append_quoted_attr(ZBuf *out, const char *text) {
  zbuf_append_char(out, '"');
  for (const char *p = text ? text : ""; *p; p++) {
    unsigned char ch = (unsigned char)*p;
    switch (ch) {
      case '\\': zbuf_append(out, "\\\\"); break;
      case '"': zbuf_append(out, "\\\""); break;
      case '\n': zbuf_append(out, "\\n"); break;
      case '\r': zbuf_append(out, "\\r"); break;
      case '\t': zbuf_append(out, "\\t"); break;
      default: zbuf_append_char(out, (char)ch); break;
    }
  }
  zbuf_append_char(out, '"');
}

char *z_program_graph_rewrite_build_patch_text(const ZProgramGraphRewriteResult *result) {
  ZBuf out;
  zbuf_init(&out);
  zbuf_append(&out, "zero-program-graph-patch v1\n");
  for (size_t i = 0; result && i < result->len; i++) {
    zbuf_append(&out, "replaceExpr node=");
    rewrite_append_quoted_attr(&out, result->items[i].node_id);
    zbuf_append(&out, " with=");
    rewrite_append_quoted_attr(&out, result->items[i].after);
    zbuf_append_char(&out, '\n');
  }
  return out.data ? out.data : z_strdup("zero-program-graph-patch v1\n");
}

void z_program_graph_rewrite_result_free(ZProgramGraphRewriteResult *result) {
  if (!result) return;
  for (size_t i = 0; i < result->len; i++) {
    free(result->items[i].node_id);
    free(result->items[i].short_handle);
    free(result->items[i].function_name);
    free(result->items[i].path);
    free(result->items[i].before);
    free(result->items[i].after);
  }
  free(result->items);
  *result = (ZProgramGraphRewriteResult){0};
}
