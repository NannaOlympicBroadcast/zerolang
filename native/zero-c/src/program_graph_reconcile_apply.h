#ifndef ZERO_C_PROGRAM_GRAPH_RECONCILE_APPLY_H
#define ZERO_C_PROGRAM_GRAPH_RECONCILE_APPLY_H

#include "program_graph.h"

typedef struct {
  bool ok;
  bool ambiguous;
  bool module_identity_changed;
  size_t preserved;
  size_t inserted;
  size_t deleted;
  char code[16];
  char message[160];
  char node_id[64];
  char candidate_id[64];
  char node_range[96];
  char candidate_range[96];
  size_t candidate_count;
  size_t auto_resolved;
  char hint[224];
} ZProgramGraphIdentityReconcile;

bool z_program_graph_preserve_source_node_ids(const ZProgramGraph *base, ZProgramGraph *edited, ZProgramGraphIdentityReconcile *out);
bool z_program_graph_preserve_source_node_ids_excluding_path(const ZProgramGraph *base, ZProgramGraph *edited, const char *exclude_path, ZProgramGraphIdentityReconcile *out);

#endif
