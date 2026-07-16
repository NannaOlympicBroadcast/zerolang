#ifndef ZERO_C_STD_SOURCE_H
#define ZERO_C_STD_SOURCE_H

#include "program_graph.h"
#include "zero.h"

#include <stdint.h>

typedef struct {
  const char *module;
  const char *path;
  const unsigned char *graph_bytes;
  size_t graph_len;
} ZStdSourceModule;

size_t z_std_source_module_count(void);
const ZStdSourceModule *z_std_source_module_at(size_t index);
const ZStdSourceModule *z_std_source_module_for_name(const char *module);
const ZStdSourceModule *z_std_source_module_for_path(const char *path);
bool z_std_source_path_is_module_artifact(const char *path);
const ZStdSourceModule *z_std_source_module_for_public_call(const char *qualified_name);
const char *z_std_source_target_for_public_call(const char *qualified_name);
bool z_std_source_module_load_graph(const ZStdSourceModule *module, ZProgramGraph *out, ZDiag *diag);
uint64_t z_std_source_graph_fingerprint(void);

#endif
