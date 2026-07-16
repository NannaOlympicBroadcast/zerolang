#ifndef ZERO_C_PROGRAM_GRAPH_TEST_CAPS_H
#define ZERO_C_PROGRAM_GRAPH_TEST_CAPS_H

#include "program_graph_resolve.h"
#include "program_graph_semantics.h"

bool z_program_graph_test_target_capabilities_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZTargetInfo *target, const char *filter, ZDiag *diag);

#endif
