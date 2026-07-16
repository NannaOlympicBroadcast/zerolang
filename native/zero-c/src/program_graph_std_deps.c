#include "program_graph_std_deps.h"

#include "program_graph_adjacency.h"
#include "std_source.h"

#include <stdlib.h>
#include <string.h>

static bool std_deps_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static const ZProgramGraphNode *std_deps_ordered_node(const ZProgramGraphAdjacency *adjacency, const char *from, const char *kind, size_t order) {
  size_t start = 0;
  size_t len = 0;
  if (!kind) return NULL;
  z_program_graph_adjacency_owner_run(adjacency, from, kind, &start, &len);
  for (size_t i = start; i < start + len; i++) {
    const ZProgramGraphEdge *edge = z_program_graph_adjacency_owner_edge_at(adjacency, i);
    if (edge->order == order) return z_program_graph_adjacency_node(adjacency, edge->to);
  }
  return NULL;
}

static bool std_deps_expr_name_into(const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *node, ZBuf *out) {
  if (!node || !out) return false;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER || node->kind == Z_PROGRAM_GRAPH_NODE_CALL) {
    zbuf_append(out, node->name ? node->name : "");
    return true;
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS || node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) {
    const ZProgramGraphNode *left = std_deps_ordered_node(adjacency, node->id, "left", 0);
    if (!std_deps_expr_name_into(adjacency, left, out)) return false;
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS ||
        !left || left->kind != Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS || !std_deps_text_eq(left->name, node->name)) {
      zbuf_append_char(out, '.');
      zbuf_append(out, node->name ? node->name : "");
    }
    return true;
  }
  return false;
}

static char *std_deps_expr_name(const ZProgramGraphAdjacency *adjacency, const ZProgramGraphNode *node) {
  ZBuf name;
  zbuf_init(&name);
  if (!std_deps_expr_name_into(adjacency, node, &name)) {
    zbuf_free(&name);
    return NULL;
  }
  return name.data;
}

static void std_deps_mark_module(const ZStdSourceModule *call_module, bool *referenced) {
  for (size_t i = 0; call_module && i < z_std_source_module_count(); i++) {
    if (z_std_source_module_at(i) == call_module) {
      referenced[i] = true;
      return;
    }
  }
}

void z_program_graph_collect_std_module_references(const ZProgramGraph *graph, bool *referenced) {
  if (!referenced) return;
  for (size_t i = 0; i < z_std_source_module_count(); i++) referenced[i] = false;
  if (!graph) return;
  ZProgramGraphAdjacency adjacency;
  z_program_graph_adjacency_init(&adjacency, graph);
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_CALL && node->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL) continue;
    char *qualified = std_deps_expr_name(&adjacency, node);
    std_deps_mark_module(z_std_source_module_for_public_call(qualified && qualified[0] ? qualified : node->name), referenced);
    free(qualified);
  }
  z_program_graph_adjacency_free(&adjacency);
}

bool z_program_graph_references_std_module(const ZProgramGraph *graph, const char *module) {
  if (!graph || !module) return false;
  ZProgramGraphAdjacency adjacency;
  z_program_graph_adjacency_init(&adjacency, graph);
  bool found = false;
  for (size_t i = 0; !found && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_CALL && node->kind != Z_PROGRAM_GRAPH_NODE_METHOD_CALL) continue;
    char *qualified = std_deps_expr_name(&adjacency, node);
    const ZStdSourceModule *call_module = z_std_source_module_for_public_call(qualified && qualified[0] ? qualified : node->name);
    found = call_module && std_deps_text_eq(call_module->module, module);
    free(qualified);
  }
  z_program_graph_adjacency_free(&adjacency);
  return found;
}
