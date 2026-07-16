#ifndef ZERO_C_PROGRAM_GRAPH_VIEW_H
#define ZERO_C_PROGRAM_GRAPH_VIEW_H

#include "program_graph.h"

bool z_program_graph_append_view(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, ZDiag *diag); bool z_program_graph_append_source_view(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, ZDiag *diag);
bool z_program_graph_append_view_function(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, const char *function_name, ZDiag *diag);
bool z_program_graph_append_view_function_handles(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, const char *function_name, ZDiag *diag);
bool z_program_graph_append_view_outline(ZBuf *buf, const ZProgramGraph *graph, const char *input, const char *scope, ZDiag *diag);
bool z_program_graph_append_view_function_around(ZBuf *buf, const ZProgramGraph *graph, const char *source_path, const char *function_name, const char *around, ZDiag *diag);

#endif
