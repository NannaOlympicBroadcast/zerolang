#ifndef ZERO_C_PROGRAM_GRAPH_SEMANTICS_H
#define ZERO_C_PROGRAM_GRAPH_SEMANTICS_H

#include "program_graph.h"
#include "program_graph_resolve.h"

typedef struct {
  bool args, env, fs, memory, alloc, path, codec, parse, time, rand, net, proc, web, world;
  const ZProgramGraphNode *args_node, *env_node, *fs_node, *memory_node, *alloc_node, *path_node, *codec_node, *parse_node, *time_node, *rand_node, *net_node, *proc_node, *web_node, *world_node;
} ZProgramGraphCapabilitySummary;

void z_program_graph_append_semantics_json(ZBuf *buf, const ZProgramGraph *graph);
void z_program_graph_collect_capabilities(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, ZProgramGraphCapabilitySummary *out);

#endif
