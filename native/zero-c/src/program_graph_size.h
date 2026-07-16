#ifndef ZERO_C_PROGRAM_GRAPH_SIZE_H
#define ZERO_C_PROGRAM_GRAPH_SIZE_H

#include "program_graph.h"

void z_program_graph_seed_source_metadata(SourceInput *input, const ZProgramGraph *graph);
void z_program_graph_seed_source_metadata_facts(SourceInput *input, const ZProgramGraph *graph);
void z_program_graph_seed_artifact_source_paths(SourceInput *input, const ZProgramGraph *graph, const char *artifact_path);

#endif
