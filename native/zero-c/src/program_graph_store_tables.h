#ifndef ZERO_C_PROGRAM_GRAPH_STORE_TABLES_H
#define ZERO_C_PROGRAM_GRAPH_STORE_TABLES_H

#include "program_graph.h"
#include "program_graph_store.h"

typedef struct {
  size_t schema;
  size_t package;
  size_t module;
  size_t declaration;
  size_t scope;
  size_t import;
  size_t symbol;
  size_t type;
  size_t effect;
  size_t capability;
  size_t ownership;
  size_t resource;
  size_t node;
  size_t edge;
  size_t projection;
  size_t source_map;
} ZProgramGraphStoreTableCounts;

void z_program_graph_store_table_counts_for_graph(const ZProgramGraph *graph, size_t source_count, size_t projection_count, ZProgramGraphStoreTableCounts *out);
void z_program_graph_store_append_compiler_metadata_for_graph(ZBuf *buf, const ZProgramGraph *graph, size_t source_count, size_t projection_count);
bool z_program_graph_store_compiler_metadata_matches(const ZProgramGraph *graph, size_t source_count, size_t projection_count, const char *compiler_store, const char *compiler_tables, const char *compiler_hash_inputs, const char **actual);
void z_program_graph_store_append_table_counts_json(ZBuf *buf, const ZProgramGraphStoreTableCounts *counts);
void z_program_graph_store_append_compiler_tables_json(ZBuf *buf, const ZProgramGraphStore *store);
void z_program_graph_store_append_compiler_hash_inputs_json(ZBuf *buf);

#endif
