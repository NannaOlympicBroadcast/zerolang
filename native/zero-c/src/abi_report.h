#ifndef ZERO_C_ABI_REPORT_H
#define ZERO_C_ABI_REPORT_H

#include "program_graph.h"
#include "zero.h"

typedef void (*ZAbiCImportsJsonFn)(ZBuf *buf, const Program *program, const ZTargetInfo *target);

void z_append_abi_dump_json(
  ZBuf *buf,
  const SourceInput *input,
  const Program *program,
  const ZTargetInfo *target,
  const ZProgramGraph *graph,
  ZAbiCImportsJsonFn append_c_imports_json
);

#endif
