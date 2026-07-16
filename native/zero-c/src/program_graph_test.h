#ifndef ZERO_C_PROGRAM_GRAPH_TEST_H
#define ZERO_C_PROGRAM_GRAPH_TEST_H

#include "program_graph.h"

typedef struct {
  const char *input;
  const char *repository_source_input;
  const char *profile;
  const char *filter;
  bool json;
  bool repository_graph_input;
} ZProgramGraphTestCommand;

int z_program_graph_run_tests_direct(const ZProgramGraphTestCommand *command, const ZTargetInfo *target, ZDiag *diag);

#endif
