#ifndef ZERO_CAPABILITY_SUMMARY_H
#define ZERO_CAPABILITY_SUMMARY_H
#include "zero.h"

typedef struct {
  bool args;
  bool env;
  bool fs;
  bool memory;
  bool alloc;
  bool path;
  bool codec;
  bool parse;
  bool time;
  bool rand;
  bool net;
  bool proc;
  bool web;
  bool world;
} CapabilitySummary;

void z_capability_summary_merge(CapabilitySummary *caps, const CapabilitySummary *other);
void z_capability_summary_set(CapabilitySummary *caps, const char *capability);
void z_capability_summary_collect_std_name(const char *name, CapabilitySummary *caps);
CapabilitySummary z_ir_program_capabilities(const IrProgram *ir);
#endif
