#ifndef ZERO_C_PROGRAM_GRAPH_PATCH_BUILDERS_H
#define ZERO_C_PROGRAM_GRAPH_PATCH_BUILDERS_H

#include "program_graph_patch.h"

bool z_program_graph_patch_apply_add_let_literal(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_add_let_binary(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_add_return_value(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_add_check_write_value(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);

#endif
