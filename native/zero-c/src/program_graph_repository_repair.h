#ifndef ZERO_PROGRAM_GRAPH_REPOSITORY_REPAIR_H
#define ZERO_PROGRAM_GRAPH_REPOSITORY_REPAIR_H

#include "zero.h"

#include <stdio.h>

typedef enum {
  REPO_GRAPH_REPAIR_NONE,
  REPO_GRAPH_REPAIR_FROM_SOURCE,
  REPO_GRAPH_REPAIR_FROM_GRAPH,
  REPO_GRAPH_REPAIR_IMPORT_OR_EXPORT,
  REPO_GRAPH_REPAIR_STATUS,
} ZRepositoryGraphRepair;

void z_repository_graph_append_repair_commands_json(ZBuf *buf, const char *input, ZRepositoryGraphRepair repair);
void z_repository_graph_print_repair_commands(FILE *stream, const char *input, ZRepositoryGraphRepair repair);

#endif
