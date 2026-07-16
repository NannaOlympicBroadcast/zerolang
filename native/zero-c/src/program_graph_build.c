#include "program_graph_build.h"

bool z_program_graph_artifact_source_present(const ZProgramGraphArtifactSource *source) {
  return source && source->graph_hash && source->graph_hash[0];
}
