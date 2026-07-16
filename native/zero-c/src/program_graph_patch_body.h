#ifndef ZERO_C_PROGRAM_GRAPH_PATCH_BODY_H
#define ZERO_C_PROGRAM_GRAPH_PATCH_BODY_H

#include "program_graph_patch.h"

bool z_program_graph_patch_apply_replace_function_body(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_replace_block_body(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_upsert_function(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_append_stmt(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_add_return_expr(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_add_test_body(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_replace_expr(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_set_const(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_patch_apply_add_param_to(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);

#endif
