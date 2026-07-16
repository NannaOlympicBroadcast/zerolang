#ifndef ZERO_C_PROGRAM_GRAPH_C_IMPORT_H
#define ZERO_C_PROGRAM_GRAPH_C_IMPORT_H

#include "c_import.h"
#include "program_graph.h"

bool z_program_graph_find_c_import_function(const ZProgramGraphNode *c_import, const IrProgram *ir, const ZTargetInfo *target, const char *symbol, ZCImportFunction *out);

#endif
