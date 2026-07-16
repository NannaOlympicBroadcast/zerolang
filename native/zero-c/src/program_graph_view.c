#include "program_graph_view.h"

#include "canonical_text.h"
#include "program_graph_adjacency.h"
#include "program_graph_handle.h"
#include "program_graph_lower.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool view_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool view_starts_with(const char *text, const char *prefix) {
  return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool view_has_suffix(const char *text, const char *suffix) {
  size_t text_len = text ? strlen(text) : 0;
  size_t suffix_len = suffix ? strlen(suffix) : 0;
  return text_len >= suffix_len && view_text_eq(text + (text_len - suffix_len), suffix);
}

static bool view_import_targets_rendered_module(const SourceInput *source, const char *module) {
  if (!source || !module || !module[0] || view_starts_with(module, "std.")) return false;
  for (size_t i = 0; i < source->module_count; i++) {
    if (view_text_eq(source->module_names[i], module)) return true;
  }
  return false;
}

static void view_drop_flattened_module_imports(Program *program, const SourceInput *source) {
  if (!program || !source || source->module_count <= 1) return;
  size_t write = 0;
  for (size_t read = 0; read < program->use_imports.len; read++) {
    UseImport item = program->use_imports.items[read];
    if (view_import_targets_rendered_module(source, item.module)) {
      free(item.module);
      free(item.alias);
      continue;
    }
    if (write != read) program->use_imports.items[write] = item;
    write++;
  }
  program->use_imports.len = write;
}

static void view_copy_node(ZProgramGraph *dst, const ZProgramGraphNode *src) {
  dst->nodes = z_checked_reallocarray(dst->nodes, dst->node_len + 1, sizeof(ZProgramGraphNode));
  ZProgramGraphNode *node = &dst->nodes[dst->node_len++];
  *node = (ZProgramGraphNode){
    .kind = src->kind,
    .line = src->line,
    .column = src->column,
    .is_public = src->is_public,
    .is_mutable = src->is_mutable,
    .is_static = src->is_static,
    .fallible = src->fallible,
    .export_c = src->export_c,
  };
  node->id = z_strdup(src->id ? src->id : "");
  node->name = z_strdup(src->name ? src->name : "");
  node->type = z_strdup(src->type ? src->type : "");
  node->value = z_strdup(src->value ? src->value : "");
  node->path = z_strdup(src->path ? src->path : "");
  node->symbol_id = z_strdup(src->symbol_id ? src->symbol_id : "");
  node->type_id = z_strdup(src->type_id ? src->type_id : "");
  node->effect_id = z_strdup(src->effect_id ? src->effect_id : "");
  node->node_hash = z_strdup(src->node_hash ? src->node_hash : "");
}

static void view_copy_edge(ZProgramGraph *dst, const ZProgramGraphEdge *src) {
  dst->edges = z_checked_reallocarray(dst->edges, dst->edge_len + 1, sizeof(ZProgramGraphEdge));
  ZProgramGraphEdge *edge = &dst->edges[dst->edge_len++];
  *edge = (ZProgramGraphEdge){
    .target = src->target,
    .order = src->order,
  };
  edge->from = z_strdup(src->from ? src->from : "");
  edge->to = z_strdup(src->to ? src->to : "");
  edge->kind = z_strdup(src->kind ? src->kind : "");
}

static bool view_included_node_id(const ZProgramGraphAdjacency *adjacency, const bool *included, const char *id) {
  size_t index = z_program_graph_adjacency_node_index(adjacency, id);
  return index != SIZE_MAX && included[index];
}

static void view_include_reachable_nodes(const ZProgramGraphAdjacency *adjacency, bool *included, size_t index) {
  const ZProgramGraph *graph = adjacency->graph;
  if (!graph || !included || index >= graph->node_len || included[index]) return;
  included[index] = true;
  size_t start = 0;
  size_t len = 0;
  z_program_graph_adjacency_owner_run(adjacency, graph->nodes[index].id, NULL, &start, &len);
  for (size_t i = start; i < start + len; i++) {
    const ZProgramGraphEdge *edge = z_program_graph_adjacency_owner_edge_at(adjacency, i);
    size_t target = z_program_graph_adjacency_node_index(adjacency, edge->to);
    if (target != SIZE_MAX) view_include_reachable_nodes(adjacency, included, target);
  }
}

static bool view_graph_has_symbol(const ZProgramGraph *graph, const bool *included, const char *symbol_id) {
  for (size_t i = 0; graph && symbol_id && i < graph->node_len; i++) {
    if (included[i] && view_text_eq(graph->nodes[i].symbol_id, symbol_id)) return true;
  }
  return false;
}

static bool view_graph_has_type(const ZProgramGraph *graph, const bool *included, const char *type_id) {
  for (size_t i = 0; graph && type_id && i < graph->node_len; i++) {
    if (included[i] && view_text_eq(graph->nodes[i].type_id, type_id)) return true;
  }
  return false;
}

static bool view_graph_has_effect(const ZProgramGraph *graph, const bool *included, const char *effect_id) {
  for (size_t i = 0; graph && effect_id && i < graph->node_len; i++) {
    if (included[i] && view_text_eq(graph->nodes[i].effect_id, effect_id)) return true;
  }
  return false;
}

static bool view_edge_target_included(const ZProgramGraph *graph, const ZProgramGraphAdjacency *adjacency, const bool *included, const ZProgramGraphEdge *edge) {
  switch (edge->target) {
    case Z_PROGRAM_GRAPH_EDGE_TARGET_NODE:
      return view_included_node_id(adjacency, included, edge->to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL:
      return view_graph_has_symbol(graph, included, edge->to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE:
      return view_graph_has_type(graph, included, edge->to);
    case Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT:
      return view_graph_has_effect(graph, included, edge->to);
  }
  return false;
}

static bool view_slice_graph_for_source(const ZProgramGraph *graph, const char *source_path, ZProgramGraph *out, ZDiag *diag) {
  if (!graph || !source_path || !source_path[0]) return false;
  ZProgramGraphAdjacency adjacency;
  z_program_graph_adjacency_init(&adjacency, graph);
  bool *included = calloc(graph->node_len ? graph->node_len : 1, sizeof(bool));
  if (!included) {
    z_program_graph_adjacency_free(&adjacency);
    if (diag) {
      diag->code = 2001;
      diag->path = source_path;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "out of memory while rendering projection-backed graph view");
    }
    return false;
  }

  bool found_module = false;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_MODULE && view_text_eq(graph->nodes[i].path, source_path)) {
      found_module = true;
      view_include_reachable_nodes(&adjacency, included, i);
    }
  }
  for (size_t i = 0; i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_MODULE) included[i] = true;
  }

  if (!found_module) {
    z_program_graph_adjacency_free(&adjacency);
    free(included);
    if (diag) {
      diag->code = 2002;
      diag->path = source_path;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "projection-backed graph has no module for input file");
      snprintf(diag->expected, sizeof(diag->expected), "Module node with matching source path");
      snprintf(diag->actual, sizeof(diag->actual), "missing module");
      snprintf(diag->help, sizeof(diag->help), "dump the graph and patch a node that belongs to the input .0 file");
    }
    return false;
  }

  z_program_graph_init(out);
  free(out->module_identity);
  out->module_identity = z_strdup(graph->module_identity ? graph->module_identity : "module:main");
  out->validation_state = graph->validation_state;
  out->canonical_source = graph->canonical_source;
  out->next_id = graph->next_id;
  if (graph->graph_hash) out->graph_hash = z_strdup(graph->graph_hash);
  for (size_t i = 0; i < graph->node_len; i++) {
    if (included[i]) view_copy_node(out, &graph->nodes[i]);
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (!view_included_node_id(&adjacency, included, edge->from)) continue;
    if (!view_edge_target_included(graph, &adjacency, included, edge)) continue;
    view_copy_edge(out, edge);
  }
  z_program_graph_adjacency_free(&adjacency);
  free(included);
  return true;
}

static bool view_append_program(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, bool drop_flattened_imports, ZDiag *diag) {
  if (!buf || !graph) return false;
  Program program = {0};
  SourceInput input = {0};
  const char *path = source_path && source_path[0] ? source_path : "<program-graph>";
  bool ok = z_program_graph_lower_to_program_for_roundtrip(graph, path, &program, &input, diag);
  if (ok && drop_flattened_imports) view_drop_flattened_module_imports(&program, &input);
  if (ok) ok = z_canonical_text_write_program(&program, buf, diag);
  if (ok) {
    Program parsed = {0};
    ZDiag parse_diag = {0};
    ok = z_parse_canonical_text_program_source(buf->data ? buf->data : "", &parsed, &parse_diag);
    if (!ok && diag) {
      *diag = parse_diag;
      if (!diag->path) diag->path = path;
    }
    z_free_program(&parsed);
  }
  if (!ok && diag && diag->code == 0) {
    diag->code = 2002;
    diag->path = path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "failed to render program graph as canonical source");
    snprintf(diag->expected, sizeof(diag->expected), "lowerable ProgramGraph");
    snprintf(diag->actual, sizeof(diag->actual), "invalid graph view");
    snprintf(diag->help, sizeof(diag->help), "run zero check to inspect graph lowering diagnostics");
  }
  z_free_program(&program);
  z_free_source(&input);
  return ok;
}

bool z_program_graph_append_view(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, ZDiag *diag) {
  return view_append_program(buf, graph, source_path, true, diag);
}

static size_t view_name_distance(const char *left, const char *right) {
  size_t left_len = left ? strlen(left) : 0;
  size_t right_len = right ? strlen(right) : 0;
  enum { VIEW_NAME_DISTANCE_MAX = 64 };
  if (left_len > VIEW_NAME_DISTANCE_MAX || right_len > VIEW_NAME_DISTANCE_MAX) {
    return left_len > right_len ? left_len : right_len;
  }
  size_t row[VIEW_NAME_DISTANCE_MAX + 1];
  for (size_t j = 0; j <= right_len; j++) row[j] = j;
  for (size_t i = 1; i <= left_len; i++) {
    size_t previous_diagonal = row[0];
    row[0] = i;
    for (size_t j = 1; j <= right_len; j++) {
      size_t previous = row[j];
      size_t substitute = previous_diagonal + ((left[i - 1] == right[j - 1] || (left[i - 1] | 32) == (right[j - 1] | 32)) ? 0 : 1);
      size_t insert = row[j - 1] + 1;
      size_t delete_cost = previous + 1;
      size_t best = substitute < insert ? substitute : insert;
      row[j] = best < delete_cost ? best : delete_cost;
      previous_diagonal = previous;
    }
  }
  return row[right_len];
}

static void view_collect_function_suggestions(const ZProgramGraph *graph, const char *function_name, char *out, size_t out_size) {
  enum { VIEW_SUGGESTION_MAX = 3 };
  const char *names[VIEW_SUGGESTION_MAX] = {0};
  size_t distances[VIEW_SUGGESTION_MAX] = {0};
  size_t count = 0;
  size_t name_len = function_name ? strlen(function_name) : 0;
  size_t threshold = name_len / 3 + 2;
  if (out_size) out[0] = '\0';
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || !node->name || !node->name[0]) continue;
    size_t distance = view_name_distance(function_name, node->name);
    if (distance > threshold) continue;
    bool duplicate = false;
    for (size_t known = 0; known < count && !duplicate; known++) {
      duplicate = view_text_eq(names[known], node->name);
    }
    if (duplicate) continue;
    size_t slot = count < VIEW_SUGGESTION_MAX ? count : VIEW_SUGGESTION_MAX;
    for (size_t pos = 0; pos < count; pos++) {
      if (distance < distances[pos]) {
        slot = pos;
        break;
      }
    }
    if (slot >= VIEW_SUGGESTION_MAX) continue;
    size_t last = count < VIEW_SUGGESTION_MAX ? count : VIEW_SUGGESTION_MAX - 1;
    for (size_t pos = last; pos > slot; pos--) {
      names[pos] = names[pos - 1];
      distances[pos] = distances[pos - 1];
    }
    names[slot] = node->name;
    distances[slot] = distance;
    if (count < VIEW_SUGGESTION_MAX) count++;
  }
  size_t used = 0;
  for (size_t i = 0; i < count && out_size; i++) {
    int written = snprintf(out + used, out_size - used, "%s%s", i ? ", " : "", names[i]);
    if (written < 0 || (size_t)written >= out_size - used) break;
    used += (size_t)written;
  }
}

static bool view_line_is_function_header(const char *line, size_t line_len, const char *function_name) {
  size_t name_len = strlen(function_name);
  if (line_len >= 4 && strncmp(line, "pub ", 4) == 0) {
    line += 4;
    line_len -= 4;
  }
  if (line_len < 3 || strncmp(line, "fn ", 3) != 0) return false;
  line += 3;
  line_len -= 3;
  return line_len > name_len && strncmp(line, function_name, name_len) == 0 && line[name_len] == '(';
}

bool z_program_graph_append_view_function(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, const char *function_name, ZDiag *diag) {
  if (!buf || !function_name || !function_name[0]) return false;
  ZBuf full;
  zbuf_init(&full);
  if (!view_append_program(&full, graph, source_path, true, diag)) {
    zbuf_free(&full);
    return false;
  }
  const char *text = full.data ? full.data : "";
  const char *line = text;
  const char *function_start = NULL;
  while (*line) {
    const char *line_end = strchr(line, '\n');
    size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);
    if (!function_start && view_line_is_function_header(line, line_len, function_name)) {
      function_start = line;
    } else if (function_start && line_len == 1 && line[0] == '}') {
      full.data[(size_t)(line + line_len - text)] = '\0';
      zbuf_append(buf, function_start);
      zbuf_append_char(buf, '\n');
      zbuf_free(&full);
      return true;
    }
    if (!line_end) break;
    line = line_end + 1;
  }
  zbuf_free(&full);
  if (diag) {
    char suggestions[160];
    view_collect_function_suggestions(graph, function_name, suggestions, sizeof(suggestions));
    diag->code = 2002;
    diag->path = source_path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "function '%s' not found in graph view", function_name);
    snprintf(diag->expected, sizeof(diag->expected), "an existing function name");
    snprintf(diag->actual, sizeof(diag->actual), "--fn %s", function_name);
    if (suggestions[0]) {
      snprintf(diag->help, sizeof(diag->help), "close matches: %.120s; use zero view --fn <name> or zero query --find %.64s", suggestions, function_name);
    } else {
      snprintf(diag->help, sizeof(diag->help), "run zero query --find %.128s to search the graph", function_name);
    }
  }
  return false;
}

// ---- handle annotations ----
//
// `zero view --fn <name> --handles` prints the same canonical body text with a
// trailing `// #handle` margin comment per statement. The annotations are
// inserted after formatting, by walking the rendered lines in lockstep with
// the lowered statement tree (the canonical writer emits exactly one line per
// simple statement and one header line per compound clause), so the rendered
// source itself never changes.

typedef struct {
  const char *text;
  ZBuf *out;
  size_t cursor;
  bool ok;
  const ZProgramGraphHandleShortener *shortener;
} ViewHandleAnnotator;

static bool va_line_bounds(const ViewHandleAnnotator *va, size_t *start, size_t *len, size_t *advance) {
  if (!va->text[va->cursor]) return false;
  const char *line = va->text + va->cursor;
  const char *end = strchr(line, '\n');
  *start = va->cursor;
  *len = end ? (size_t)(end - line) : strlen(line);
  *advance = *len + (end ? 1 : 0);
  return true;
}

static void va_emit(ViewHandleAnnotator *va, const char *annotation) {
  size_t start = 0;
  size_t len = 0;
  size_t advance = 0;
  if (!va->ok || !va_line_bounds(va, &start, &len, &advance)) {
    va->ok = false;
    return;
  }
  zbuf_appendf(va->out, "%.*s", (int)len, va->text + start);
  if (annotation && annotation[0]) zbuf_appendf(va->out, "  // %s", annotation);
  zbuf_append_char(va->out, '\n');
  va->cursor += advance;
}

static void va_append_handle(const ViewHandleAnnotator *va, ZBuf *note, const char *id) {
  if (!id || !id[0]) {
    zbuf_append(note, "?");
    return;
  }
  z_program_graph_append_short_handle(va->shortener, id, note);
}

/* Annotation grammar: simple statements get `// #stmt`; compound headers get
 * `// #stmt #block` where the second handle is the clause body block (the
 * replaceBlockBody target); `} else {` and match-arm headers get the clause
 * block handle alone. */
static void va_emit_with_handles(ViewHandleAnnotator *va, const char *id, const char *block_id) {
  ZBuf note;
  zbuf_init(&note);
  if (id) va_append_handle(va, &note, id);
  if (block_id) {
    if (note.len > 0) zbuf_append_char(&note, ' ');
    va_append_handle(va, &note, block_id);
  }
  va_emit(va, note.data ? note.data : "");
  zbuf_free(&note);
}

static void va_annotate_block(ViewHandleAnnotator *va, const StmtVec *body);

static void va_annotate_stmt(ViewHandleAnnotator *va, const Stmt *stmt) {
  if (!va->ok) return;
  if (!stmt) {
    va_emit(va, NULL);
    return;
  }
  switch (stmt->kind) {
    case STMT_IF:
      va_emit_with_handles(va, stmt->graph_id ? stmt->graph_id : "?", stmt->then_graph_id);
      va_annotate_block(va, &stmt->then_body);
      if (stmt->else_body.len == 1 && stmt->else_body.items[0] && stmt->else_body.items[0]->kind == STMT_IF) {
        // rendered as `} else if ... {`; the nested if consumes the closing brace
        va_annotate_stmt(va, stmt->else_body.items[0]);
        return;
      }
      if (stmt->else_body.len > 0) {
        va_emit_with_handles(va, NULL, stmt->else_graph_id ? stmt->else_graph_id : "?");
        va_annotate_block(va, &stmt->else_body);
      }
      va_emit(va, NULL);
      return;
    case STMT_WHILE:
    case STMT_FOR:
      va_emit_with_handles(va, stmt->graph_id ? stmt->graph_id : "?", stmt->then_graph_id);
      va_annotate_block(va, &stmt->then_body);
      va_emit(va, NULL);
      return;
    case STMT_MATCH:
      va_emit_with_handles(va, stmt->graph_id ? stmt->graph_id : "?", NULL);
      for (size_t i = 0; i < stmt->match_arms.len; i++) {
        const MatchArm *arm = &stmt->match_arms.items[i];
        va_emit_with_handles(va, NULL, arm->body_graph_id ? arm->body_graph_id : "?");
        va_annotate_block(va, &arm->body);
        va_emit(va, NULL);
      }
      va_emit(va, NULL);
      return;
    default:
      va_emit_with_handles(va, stmt->graph_id ? stmt->graph_id : "?", NULL);
      return;
  }
}

static void va_annotate_block(ViewHandleAnnotator *va, const StmtVec *body) {
  for (size_t i = 0; va->ok && body && i < body->len; i++) va_annotate_stmt(va, body->items[i]);
}

static const Function *view_find_program_function(const Program *program, const char *function_name) {
  for (size_t i = 0; program && function_name && i < program->functions.len; i++) {
    if (view_text_eq(program->functions.items[i].name, function_name)) return &program->functions.items[i];
  }
  return NULL;
}

static bool view_annotate_function_handles(ZBuf *buf, const char *function_text, const Function *fun, const ZProgramGraph *graph, const char *source_path, ZDiag *diag) {
  ZProgramGraphHandleShortener shortener;
  z_program_graph_handle_shortener_init(&shortener, graph);
  ViewHandleAnnotator va = {.text = function_text, .out = buf, .cursor = 0, .ok = true, .shortener = &shortener};
  size_t start = 0;
  size_t len = 0;
  size_t advance = 0;
  // signature line stays unannotated
  va_emit(&va, NULL);
  va_annotate_block(&va, &fun->body);
  // exactly the closing `}` line must remain
  bool closing_ok = va.ok && va_line_bounds(&va, &start, &len, &advance) && len == 1 && function_text[start] == '}';
  if (closing_ok) {
    va_emit(&va, NULL);
    closing_ok = va.ok && !va_line_bounds(&va, &start, &len, &advance);
  }
  z_program_graph_handle_shortener_free(&shortener);
  if (!closing_ok) {
    if (diag) {
      diag->code = 2002;
      diag->path = source_path;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "handle annotations did not align with the rendered function body");
      snprintf(diag->expected, sizeof(diag->expected), "one rendered line per statement");
      snprintf(diag->actual, sizeof(diag->actual), "--fn %s --handles", fun && fun->name ? fun->name : "");
      snprintf(diag->help, sizeof(diag->help), "run zero view --fn <name> without --handles, or zero query --fn <name> --handles for the statement handle list");
    }
    return false;
  }
  return true;
}

bool z_program_graph_append_view_function_handles(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, const char *function_name, ZDiag *diag) {
  if (!buf || !function_name || !function_name[0]) return false;
  ZBuf plain;
  zbuf_init(&plain);
  Program program = {0};
  SourceInput input = {0};
  const char *path = source_path && source_path[0] ? source_path : "<program-graph>";
  bool ok = z_program_graph_lower_to_program_for_roundtrip(graph, path, &program, &input, diag);
  if (ok) view_drop_flattened_module_imports(&program, &input);
  if (ok) ok = z_canonical_text_write_program(&program, &plain, diag);
  if (!ok) {
    z_free_program(&program);
    z_free_source(&input);
    zbuf_free(&plain);
    return false;
  }
  // extract the function's text the same way the plain view does
  const char *text = plain.data ? plain.data : "";
  const char *line = text;
  const char *function_start = NULL;
  char *function_text = NULL;
  while (*line) {
    const char *line_end = strchr(line, '\n');
    size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);
    if (!function_start && view_line_is_function_header(line, line_len, function_name)) {
      function_start = line;
    } else if (function_start && line_len == 1 && line[0] == '}') {
      function_text = z_strndup(function_start, (size_t)(line + line_len - function_start) + 1);
      break;
    }
    if (!line_end) break;
    line = line_end + 1;
  }
  if (!function_text) {
    if (diag) {
      char suggestions[160];
      view_collect_function_suggestions(graph, function_name, suggestions, sizeof(suggestions));
      diag->code = 2002;
      diag->path = source_path;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "function '%s' not found in graph view", function_name);
      snprintf(diag->expected, sizeof(diag->expected), "an existing function name");
      snprintf(diag->actual, sizeof(diag->actual), "--fn %s", function_name);
      if (suggestions[0]) {
        snprintf(diag->help, sizeof(diag->help), "close matches: %.120s; use zero view --fn <name> or zero query --find %.64s", suggestions, function_name);
      } else {
        snprintf(diag->help, sizeof(diag->help), "run zero query --find %.128s to search the graph", function_name);
      }
    }
    z_free_program(&program);
    z_free_source(&input);
    zbuf_free(&plain);
    return false;
  }
  const Function *fun = view_find_program_function(&program, function_name);
  ok = fun != NULL && view_annotate_function_handles(buf, function_text, fun, graph, source_path, diag);
  free(function_text);
  z_free_program(&program);
  z_free_source(&input);
  zbuf_free(&plain);
  return ok;
}

bool z_program_graph_append_source_view(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, ZDiag *diag) {
  ZProgramGraph sliced = {0};
  if (!view_slice_graph_for_source(graph, source_path, &sliced, diag)) return false;
  bool ok = view_append_program(buf, &sliced, source_path, false, diag);
  z_program_graph_free(&sliced);
  return ok;
}

// ---- outline ----

static const ZProgramGraphNode *view_owner_module(const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *node) {
  const ZProgramGraph *graph = adjacency ? adjacency->graph : NULL;
  const char *current = node ? node->id : NULL;
  for (size_t depth = 0; graph && current && depth < graph->node_len; depth++) {
    const ZProgramGraphEdge *owner = z_program_graph_adjacency_first_child_edge(adjacency, current);
    if (!owner) return NULL;
    const ZProgramGraphNode *parent = z_program_graph_adjacency_node(adjacency, owner->from);
    if (!parent) return NULL;
    if (parent->kind == Z_PROGRAM_GRAPH_NODE_MODULE) return parent;
    current = parent->id;
  }
  return NULL;
}

static bool view_outline_scope_matches_module(const ZProgramGraphNode *module, const char *scope) {
  if (!scope || !scope[0] || view_text_eq(scope, ".") || view_text_eq(scope, "./")) return true;
  if (view_starts_with(scope, "./")) scope += 2;
  if (module->name && view_text_eq(module->name, scope)) return true;
  if (!module->path || !module->path[0]) return false;
  if (view_text_eq(module->path, scope)) return true;
  size_t path_len = strlen(module->path);
  size_t scope_len = strlen(scope);
  return scope_len < path_len &&
         view_text_eq(module->path + (path_len - scope_len), scope) &&
         module->path[path_len - scope_len - 1] == '/';
}

static char *view_outline_read_module_source(const char *input, const char *module_path) {
  if (!module_path || !module_path[0]) return NULL;
  ZDiag read_diag = {0};
  if (input && input[0] && !view_text_eq(input, ".") && !view_text_eq(input, "./")) {
    char joined[1024];
    size_t input_len = strlen(input);
    bool input_is_file = view_has_suffix(input, ".0") || view_has_suffix(input, ".toml") || view_has_suffix(input, ".json") || view_has_suffix(input, ".graph") || view_has_suffix(input, ".program-graph");
    if (input_is_file) {
      const char *slash = strrchr(input, '/');
      input_len = slash ? (size_t)(slash - input) : 0;
    }
    while (input_len > 0 && input[input_len - 1] == '/') input_len--;
    if (input_len > 0 && input_len + 1 + strlen(module_path) < sizeof(joined)) {
      snprintf(joined, sizeof(joined), "%.*s/%s", (int)input_len, input, module_path);
      char *text = z_read_file(joined, &read_diag);
      if (text) return text;
    }
  }
  read_diag = (ZDiag){0};
  return z_read_file(module_path, &read_diag);
}

static const char *view_outline_line_at(const char *text, int line_number, size_t *out_len) {
  if (!text || line_number < 1) return NULL;
  const char *line = text;
  for (int current = 1; current < line_number; current++) {
    const char *next = strchr(line, '\n');
    if (!next) return NULL;
    line = next + 1;
  }
  const char *end = strchr(line, '\n');
  *out_len = end ? (size_t)(end - line) : strlen(line);
  return line;
}

static void view_outline_doc_for_line(const char *source, int decl_line, char *out, size_t out_size) {
  if (out_size) out[0] = '\0';
  if (!source || decl_line <= 1) return;
  int first_comment_line = 0;
  for (int line_number = decl_line - 1; line_number >= 1; line_number--) {
    size_t len = 0;
    const char *line = view_outline_line_at(source, line_number, &len);
    if (!line) return;
    size_t lead = 0;
    while (lead < len && (line[lead] == ' ' || line[lead] == '\t')) lead++;
    if (len - lead >= 2 && line[lead] == '/' && line[lead + 1] == '/') {
      first_comment_line = line_number;
      continue;
    }
    break;
  }
  if (!first_comment_line) return;
  size_t len = 0;
  const char *line = view_outline_line_at(source, first_comment_line, &len);
  if (!line) return;
  size_t lead = 0;
  while (lead < len && (line[lead] == ' ' || line[lead] == '\t')) lead++;
  lead += 2;
  while (lead < len && line[lead] == ' ') lead++;
  size_t copy = len - lead;
  if (copy >= out_size) copy = out_size - 1;
  memcpy(out, line + lead, copy);
  out[copy] = '\0';
}

static void view_outline_append_function(ZBuf *buf, const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *node, const char *module_source) {
  if (node->value && node->value[0] && (!node->name || !node->name[0])) {
    zbuf_appendf(buf, "  test \"%s\"", node->value);
  } else {
    zbuf_appendf(buf, "  %sfn %s(", node->is_public ? "pub " : "", node->name ? node->name : "");
    size_t start = 0;
    size_t len = 0;
    z_program_graph_adjacency_owner_run(adjacency, node->id, "param", &start, &len);
    bool first_param = true;
    for (size_t i = start; i < start + len; i++) {
      const ZProgramGraphEdge *edge = z_program_graph_adjacency_owner_edge_at(adjacency, i);
      const ZProgramGraphNode *param = edge && edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE ? z_program_graph_adjacency_node(adjacency, edge->to) : NULL;
      if (!param) continue;
      if (!first_param) zbuf_append(buf, ", ");
      first_param = false;
      zbuf_appendf(buf, "%s: %s", param->name ? param->name : "", param->type ? param->type : "Unknown");
    }
    zbuf_appendf(buf, ") -> %s%s", node->type && node->type[0] ? node->type : "Void", node->fallible ? " raises" : "");
  }
  char doc[160];
  view_outline_doc_for_line(module_source, node->line, doc, sizeof(doc));
  if (doc[0]) zbuf_appendf(buf, "  // %s", doc);
  zbuf_append_char(buf, '\n');
}

bool z_program_graph_append_view_outline(ZBuf *buf, const ZProgramGraph *graph, const char *input, const char *scope, ZDiag *diag) {
  if (!buf || !graph) return false;
  ZProgramGraphAdjacency adjacency;
  z_program_graph_adjacency_init(&adjacency, graph);
  size_t module_count = 0;
  size_t function_count = 0;
  for (size_t m = 0; m < graph->node_len; m++) {
    const ZProgramGraphNode *module = &graph->nodes[m];
    if (module->kind != Z_PROGRAM_GRAPH_NODE_MODULE || !view_outline_scope_matches_module(module, scope)) continue;
    if (module_count > 0) zbuf_append_char(buf, '\n');
    module_count++;
    zbuf_appendf(buf, "module %s", module->name ? module->name : "");
    if (module->path && module->path[0]) zbuf_appendf(buf, " path:%s", module->path);
    zbuf_append_char(buf, '\n');
    char *module_source = view_outline_read_module_source(input, module->path);
    for (size_t i = 0; i < graph->node_len; i++) {
      const ZProgramGraphNode *node = &graph->nodes[i];
      if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) continue;
      const ZProgramGraphNode *owner = view_owner_module(&adjacency, node);
      if (owner != module) continue;
      function_count++;
      view_outline_append_function(buf, &adjacency, node, module_source);
    }
    free(module_source);
  }
  z_program_graph_adjacency_free(&adjacency);
  if (module_count == 0) {
    if (diag) {
      ZBuf names;
      zbuf_init(&names);
      for (size_t m = 0; m < graph->node_len; m++) {
        const ZProgramGraphNode *module = &graph->nodes[m];
        if (module->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
        if (names.len > 0) zbuf_append(&names, ", ");
        zbuf_append(&names, module->name ? module->name : "");
      }
      diag->code = 2002;
      diag->path = input;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "no module matches --outline %s", scope ? scope : "");
      snprintf(diag->expected, sizeof(diag->expected), "a module name, module source path, or . for every module");
      snprintf(diag->actual, sizeof(diag->actual), "--outline %s", scope ? scope : "");
      snprintf(diag->help, sizeof(diag->help), "modules in this graph: %s", names.data && names.data[0] ? names.data : "(none)");
      zbuf_free(&names);
    }
    return false;
  }
  zbuf_appendf(buf, "\n%zu function%s in %zu module%s; zero view --fn <name> prints one function's source\n", function_count, function_count == 1 ? "" : "s", module_count, module_count == 1 ? "" : "s");
  return true;
}

// ---- around ----

typedef struct {
  size_t open_line;
  size_t close_line;
  size_t depth;
} ViewTextBlock;

static void view_around_collect_blocks(const char *text, const size_t *line_starts, size_t line_count, ViewTextBlock **out_blocks, size_t *out_len) {
  ViewTextBlock *blocks = NULL;
  size_t block_len = 0;
  size_t block_cap = 0;
  size_t stack[256];
  size_t stack_len = 0;
  size_t line = 0;
  bool in_string = false;
  for (size_t i = 0; text[i]; i++) {
    while (line + 1 < line_count && i >= line_starts[line + 1]) line++;
    char ch = text[i];
    if (in_string) {
      if (ch == '\\' && text[i + 1]) i++;
      else if (ch == '"') in_string = false;
      else if (ch == '\n') in_string = false;
      continue;
    }
    if (ch == '"') {
      in_string = true;
    } else if (ch == '{') {
      if (block_len == block_cap) {
        block_cap = block_cap ? block_cap * 2 : 16;
        blocks = z_checked_reallocarray(blocks, block_cap, sizeof(ViewTextBlock));
      }
      blocks[block_len] = (ViewTextBlock){.open_line = line, .close_line = line_count ? line_count - 1 : 0, .depth = stack_len};
      if (stack_len < sizeof(stack) / sizeof(stack[0])) stack[stack_len] = block_len;
      stack_len++;
      block_len++;
    } else if (ch == '}') {
      if (stack_len > 0) {
        stack_len--;
        if (stack_len < sizeof(stack) / sizeof(stack[0])) blocks[stack[stack_len]].close_line = line;
      }
    }
  }
  *out_blocks = blocks;
  *out_len = block_len;
}

bool z_program_graph_append_view_function_around(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, const char *function_name, const char *around, ZDiag *diag) {
  if (!buf || !around || !around[0]) return false;
  ZBuf full;
  zbuf_init(&full);
  if (!z_program_graph_append_view_function(&full, graph, source_path, function_name, diag)) {
    zbuf_free(&full);
    return false;
  }
  const char *text = full.data ? full.data : "";
  size_t *line_starts = NULL;
  size_t line_count = 0;
  size_t line_cap = 0;
  for (size_t i = 0; text[i];) {
    if (line_count == line_cap) {
      line_cap = line_cap ? line_cap * 2 : 64;
      line_starts = z_checked_reallocarray(line_starts, line_cap, sizeof(size_t));
    }
    line_starts[line_count++] = i;
    const char *next = strchr(text + i, '\n');
    if (!next) break;
    i = (size_t)(next - text) + 1;
  }
  if (line_count == 0) {
    zbuf_free(&full);
    free(line_starts);
    return false;
  }
  ViewTextBlock *blocks = NULL;
  size_t block_len = 0;
  view_around_collect_blocks(text, line_starts, line_count, &blocks, &block_len);
  bool *selected = calloc(line_count, sizeof(bool));
  size_t match_count = 0;
  size_t text_len = strlen(text);
  for (size_t line = 0; line < line_count && selected; line++) {
    size_t line_end = line + 1 < line_count ? line_starts[line + 1] : text_len;
    size_t line_len = line_end - line_starts[line];
    char saved = full.data[line_starts[line] + line_len];
    full.data[line_starts[line] + line_len] = '\0';
    bool hit = strstr(full.data + line_starts[line], around) != NULL;
    full.data[line_starts[line] + line_len] = saved;
    if (!hit) continue;
    match_count++;
    const ViewTextBlock *enclosing = NULL;
    for (size_t b = 0; b < block_len; b++) {
      const ViewTextBlock *block = &blocks[b];
      if (block->open_line > line || block->close_line < line) continue;
      if (!enclosing || block->depth > enclosing->depth) enclosing = block;
    }
    if (!enclosing) {
      for (size_t mark = 0; mark < line_count; mark++) selected[mark] = true;
      continue;
    }
    for (size_t mark = enclosing->open_line; mark <= enclosing->close_line && mark < line_count; mark++) selected[mark] = true;
  }
  if (match_count == 0 || !selected) {
    zbuf_free(&full);
    free(line_starts);
    free(blocks);
    free(selected);
    if (diag) {
      diag->code = 2002;
      diag->path = source_path;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "text '%s' not found in function '%s'", around, function_name ? function_name : "");
      snprintf(diag->expected, sizeof(diag->expected), "text that occurs inside the function body");
      snprintf(diag->actual, sizeof(diag->actual), "--around %s", around);
      snprintf(diag->help, sizeof(diag->help), "run zero view --fn %s for the whole function, or zero query --find %s to search the graph", function_name ? function_name : "<name>", around);
    }
    return false;
  }
  selected[0] = true;
  selected[line_count - 1] = true;
  bool elided = false;
  for (size_t line = 0; line < line_count; line++) {
    size_t line_end = line + 1 < line_count ? line_starts[line + 1] : text_len;
    if (!selected[line]) {
      if (!elided) zbuf_append(buf, "    ...\n");
      elided = true;
      continue;
    }
    elided = false;
    char saved = full.data[line_end];
    full.data[line_end] = '\0';
    zbuf_append(buf, full.data + line_starts[line]);
    full.data[line_end] = saved;
  }
  if (buf->len > 0 && buf->data[buf->len - 1] != '\n') zbuf_append_char(buf, '\n');
  zbuf_free(&full);
  free(line_starts);
  free(blocks);
  free(selected);
  return true;
}
