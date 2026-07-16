#include "program_graph_report.h"

#include "program_graph_format.h"
#include "program_graph_resolve.h"
#include "program_graph_semantics.h"
#include "std_sig.h"

#include <string.h>

static bool graph_report_is_call_ref(const ZProgramGraphResolutionReference *ref) {
  return ref && strcmp(ref->kind ? ref->kind : "", "call") == 0;
}

CapabilitySummary z_program_graph_report_capabilities(const ZProgramGraph *graph) {
  CapabilitySummary caps = {0};
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  if (!graph || !z_program_graph_collect_resolution_facts(graph, &resolution)) return caps;

  ZProgramGraphCapabilitySummary graph_caps = {0};
  z_program_graph_collect_capabilities(graph, &resolution, &graph_caps);
  caps.args = graph_caps.args;
  caps.env = graph_caps.env;
  caps.fs = graph_caps.fs;
  caps.memory = graph_caps.memory;
  caps.alloc = graph_caps.alloc;
  caps.path = graph_caps.path;
  caps.codec = graph_caps.codec;
  caps.parse = graph_caps.parse;
  caps.time = graph_caps.time;
  caps.rand = graph_caps.rand;
  caps.net = graph_caps.net;
  caps.proc = graph_caps.proc;
  caps.web = graph_caps.web;
  caps.world = graph_caps.world;

  for (size_t i = 0; i < resolution.reference_len; i++) {
    if (graph_report_is_call_ref(&resolution.references[i])) z_capability_summary_collect_std_name(resolution.references[i].qualified_name, &caps);
  }
  z_program_graph_resolution_facts_free(&resolution);
  return caps;
}

void z_program_graph_report_mark_used_std_helpers(const ZProgramGraph *graph, bool *used, size_t used_len) {
  if (!graph || !used || used_len == 0) return;
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  if (!z_program_graph_collect_resolution_facts(graph, &resolution)) return;
  for (size_t i = 0; i < resolution.reference_len; i++) {
    if (!graph_report_is_call_ref(&resolution.references[i])) continue;
    int index = z_std_helper_index(resolution.references[i].qualified_name, used_len);
    if (index >= 0) used[index] = true;
  }
  z_program_graph_resolution_facts_free(&resolution);
}

const ZProgramGraph *z_program_graph_report_load_source(const char *artifact, bool repository_store, ZProgramGraphReportLoad *loaded) {
  if (loaded) *loaded = (ZProgramGraphReportLoad){0};
  if (!artifact || !artifact[0] || !loaded) return NULL;
  ZDiag diag = {0};
  if (repository_store) {
    if (!z_program_graph_store_load_path(artifact, &loaded->store, &diag)) return NULL;
    loaded->store_loaded = true;
    return &loaded->store.graph;
  }
  if (!z_program_graph_load(artifact, &loaded->graph, &diag)) return NULL;
  loaded->graph_loaded = true;
  return &loaded->graph;
}

void z_program_graph_report_load_free(ZProgramGraphReportLoad *loaded) {
  if (!loaded) return;
  if (loaded->store_loaded) z_program_graph_store_free(&loaded->store);
  if (loaded->graph_loaded) z_program_graph_free(&loaded->graph);
  *loaded = (ZProgramGraphReportLoad){0};
}
