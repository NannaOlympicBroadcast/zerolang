#include "program_graph_store_tables.h"

#include <stdlib.h>
#include <string.h>

static bool tables_text_eq(const char *left, const char *right) {
  const unsigned char *a = (const unsigned char *)(left ? left : "");
  const unsigned char *b = (const unsigned char *)(right ? right : "");
  while (*a && *b && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

static bool tables_starts_with(const char *text, const char *prefix) {
  size_t len = prefix ? strlen(prefix) : 0;
  return text && prefix && strncmp(text, prefix, len) == 0;
}

static bool tables_contains(const char *text, const char *needle) {
  return text && needle && strstr(text, needle) != NULL;
}

static bool tables_node_declares(const ZProgramGraphNode *node) {
  if (!node) return false;
  switch (node->kind) {
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
    case Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT:
      return true;
    default:
      return false;
  }
}

static bool tables_node_scopes(const ZProgramGraphNode *node) {
  if (!node) return false;
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_MODULE:
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
    case Z_PROGRAM_GRAPH_NODE_BLOCK:
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
    case Z_PROGRAM_GRAPH_NODE_ENUM:
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
      return true;
    default:
      return false;
  }
}

static bool tables_node_imports(const ZProgramGraphNode *node) {
  return node && (node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT || node->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT);
}

static bool tables_capability_type(const char *type) {
  return tables_text_eq(type, "World") ||
         tables_text_eq(type, "Fs") ||
         tables_text_eq(type, "File") ||
         tables_text_eq(type, "Net") ||
         tables_text_eq(type, "HttpClient");
}

static bool tables_ownership_type(const char *type) {
  return tables_contains(type, "owned<") ||
         tables_contains(type, "MutSpan<") ||
         tables_contains(type, "mutref<");
}

static bool tables_resource_type(const char *type) {
  return tables_text_eq(type, "World") ||
         tables_text_eq(type, "Fs") ||
         tables_text_eq(type, "File") ||
         tables_contains(type, "owned<File>");
}

void z_program_graph_store_table_counts_for_graph(const ZProgramGraph *graph, size_t source_count, size_t projection_count, ZProgramGraphStoreTableCounts *out) {
  if (!out) return;
  *out = (ZProgramGraphStoreTableCounts){
    .schema = 1,
    .package = graph && tables_starts_with(graph->module_identity, "package:") ? 1 : 0,
    .node = graph ? graph->node_len : 0,
    .edge = graph ? graph->edge_len : 0,
    .projection = projection_count,
    .source_map = 0,
  };
  (void)source_count;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE) out->module++;
    if (tables_node_declares(node)) out->declaration++;
    if (tables_node_scopes(node)) out->scope++;
    if (tables_node_imports(node)) out->import++;
    if (node->symbol_id && node->symbol_id[0]) out->symbol++;
    if (node->type_id && node->type_id[0]) out->type++;
    if ((node->effect_id && node->effect_id[0]) || node->kind == Z_PROGRAM_GRAPH_NODE_EFFECT_REF || node->fallible) out->effect++;
    if (node->fallible || tables_capability_type(node->type)) out->capability++;
    if (node->is_mutable || tables_ownership_type(node->type)) out->ownership++;
    if (tables_resource_type(node->type)) out->resource++;
    if ((node->path && node->path[0]) || node->line > 0 || node->column > 0) out->source_map++;
  }
}

void z_program_graph_store_append_compiler_metadata_for_graph(ZBuf *buf, const ZProgramGraph *graph, size_t source_count, size_t projection_count) {
  // Every caller serializes a graph that already passed z_program_graph_validate
  // (store writes validate up front and store loads validate the parsed graph
  // before verifying metadata), so stamp the shape-valid state directly instead
  // of re-running full validation per serialization.
  bool ok = graph != NULL;
  ZProgramGraphStoreTableCounts counts;
  z_program_graph_store_table_counts_for_graph(graph, source_count, projection_count, &counts);
  zbuf_append(buf, "compilerStore schemaVersion:1 shape:\"compiler-oriented-tables\" semanticOk:");
  zbuf_append(buf, ok ? "true" : "false");
  zbuf_append(buf, " semanticValidity:\"");
  zbuf_append(buf, z_program_graph_validation_state_name(ok ? Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID : Z_PROGRAM_GRAPH_VALIDATION_DECODED));
  zbuf_append(buf, "\" sourceProjectionRequired:false sourceMapRequired:false storageInterface:\"ProgramGraphStore\"\n");
  zbuf_appendf(buf,
               "compilerTables schema:%zu package:%zu module:%zu declaration:%zu scope:%zu import:%zu symbol:%zu type:%zu effect:%zu capability:%zu ownership:%zu resource:%zu node:%zu edge:%zu projection:%zu sourceMap:%zu\n",
               counts.schema,
               counts.package,
               counts.module,
               counts.declaration,
               counts.scope,
               counts.import,
               counts.symbol,
               counts.type,
               counts.effect,
               counts.capability,
               counts.ownership,
               counts.resource,
               counts.node,
               counts.edge,
               counts.projection,
               counts.source_map);
  zbuf_append(buf, "compilerHashInputs graphHashExcludes:\"source-path,line,column,projection-text\" nodeHashExcludes:\"source-path,line,column,projection-text\" idCollisionFallbackMayUse:\"source-path,line,column\"\n");
}

bool z_program_graph_store_compiler_metadata_matches(const ZProgramGraph *graph, size_t source_count, size_t projection_count, const char *compiler_store, const char *compiler_tables, const char *compiler_hash_inputs, const char **actual) {
  ZBuf expected;
  zbuf_init(&expected);
  z_program_graph_store_append_compiler_metadata_for_graph(&expected, graph, source_count, projection_count);
  const char *cursor = expected.data ? expected.data : "";
  const char *lines[3] = {compiler_store, compiler_tables, compiler_hash_inputs};
  for (size_t i = 0; i < 3; i++) {
    const char *start = cursor;
    while (*cursor && *cursor != '\n') cursor++;
    char *line = z_strndup(start, (size_t)(cursor - start));
    if (*cursor == '\n') cursor++;
    bool match = tables_text_eq(line, lines[i]);
    free(line);
    if (!match) {
      if (actual) *actual = lines[i] ? lines[i] : "missing compiler store metadata";
      zbuf_free(&expected);
      return false;
    }
  }
  zbuf_free(&expected);
  return true;
}

void z_program_graph_store_append_table_counts_json(ZBuf *buf, const ZProgramGraphStoreTableCounts *counts) {
  ZProgramGraphStoreTableCounts zero = {0};
  if (!counts) counts = &zero;
  zbuf_appendf(buf,
               "{\"schema\":%zu,\"package\":%zu,\"module\":%zu,\"declaration\":%zu,\"scope\":%zu,\"import\":%zu,\"symbol\":%zu,\"type\":%zu,\"effect\":%zu,\"capability\":%zu,\"ownership\":%zu,\"resource\":%zu,\"node\":%zu,\"edge\":%zu,\"projection\":%zu,\"sourceMap\":%zu}",
               counts->schema,
               counts->package,
               counts->module,
               counts->declaration,
               counts->scope,
               counts->import,
               counts->symbol,
               counts->type,
               counts->effect,
               counts->capability,
               counts->ownership,
               counts->resource,
               counts->node,
               counts->edge,
               counts->projection,
               counts->source_map);
}

void z_program_graph_store_append_compiler_tables_json(ZBuf *buf, const ZProgramGraphStore *store) {
  ZProgramGraphStoreTableCounts counts;
  z_program_graph_store_table_counts_for_graph(store ? &store->graph : NULL,
                                               store ? store->source_path_len : 0,
                                               store ? store->projection_len : 0,
                                               &counts);
  z_program_graph_store_append_table_counts_json(buf, &counts);
}

void z_program_graph_store_append_compiler_hash_inputs_json(ZBuf *buf) {
  zbuf_append(buf, "{\"graphHashExcludes\":[\"sourcePath\",\"line\",\"column\",\"projectionText\"],\"nodeHashExcludes\":[\"sourcePath\",\"line\",\"column\",\"projectionText\"],\"idCollisionFallbackMayUse\":[\"sourcePath\",\"line\",\"column\"]}");
}
