#ifndef ZERO_C_PROGRAM_GRAPH_STD_DEPS_H
#define ZERO_C_PROGRAM_GRAPH_STD_DEPS_H

#include "program_graph.h"

bool z_program_graph_references_std_module(const ZProgramGraph *graph, const char *module);
void z_program_graph_collect_std_module_references(const ZProgramGraph *graph, bool *referenced);

#endif
