#include "program_graph_store_prune.h"

#include "program_graph_adjacency.h"
#include "std_source.h"
#include "zero.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool prune_text_eq(const char *left, const char *right) { return strcmp(left ? left : "", right ? right : "") == 0; }

static bool prune_starts_with(const char *text, const char *prefix) {
  size_t len = prefix ? strlen(prefix) : 0;
  return text && prefix && strncmp(text, prefix, len) == 0;
}

static const char *prune_basename(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  return slash ? slash + 1 : (path ? path : "");
}

static const ZStdSourceModule *prune_std_source_module_for_path(const char *path) {
  for (size_t i = 0; path && i < z_std_source_module_count(); i++) {
    const ZStdSourceModule *module = z_std_source_module_at(i);
    if (module &&
        (prune_text_eq(module->path, path) ||
         prune_text_eq(prune_basename(module->path), prune_basename(path)))) return module;
  }
  return NULL;
}

typedef struct {
  const char **paths;
  size_t len;
} PruneEmbeddedPaths;

static void prune_embedded_paths_collect(const ZProgramGraph *graph, PruneEmbeddedPaths *embedded) {
  embedded->paths = z_checked_calloc(graph->node_len ? graph->node_len : 1, sizeof(const char *));
  embedded->len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_MODULE) continue;
    const ZStdSourceModule *module = prune_std_source_module_for_path(node->path);
    if (!module || !prune_text_eq(node->name, module->module)) continue;
    bool seen = false;
    for (size_t j = 0; j < embedded->len && !seen; j++) {
      if (prune_text_eq(embedded->paths[j], node->path)) seen = true;
    }
    if (!seen) embedded->paths[embedded->len++] = node->path;
  }
}

static bool prune_path_is_embedded_std(const PruneEmbeddedPaths *embedded, const char *path) {
  for (size_t i = 0; path && i < embedded->len; i++) {
    if (prune_text_eq(embedded->paths[i], path)) return true;
  }
  return false;
}

static bool prune_node_is_appended_std_helper(const ZProgramGraphNode *node) {
  return node &&
         node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION &&
         prune_starts_with(node->name, "__zero_std_");
}

static void prune_free_node_fields(ZProgramGraphNode *node) {
  if (!node) return;
  free(node->id);
  free(node->name);
  free(node->type);
  free(node->value);
  free(node->path);
  free(node->symbol_id);
  free(node->type_id);
  free(node->effect_id);
  free(node->node_hash);
  *node = (ZProgramGraphNode){0};
}

static void prune_free_edge_fields(ZProgramGraphEdge *edge) {
  if (!edge) return;
  free(edge->from);
  free(edge->to);
  free(edge->kind);
  *edge = (ZProgramGraphEdge){0};
}

static void prune_expand_removed_subgraphs(const ZProgramGraph *graph, bool *remove, const size_t *edge_from, const size_t *edge_to) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; graph && i < graph->edge_len; i++) {
      if (graph->edges[i].target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
      size_t from = edge_from[i];
      size_t to = edge_to[i];
      if (from == SIZE_MAX || to == SIZE_MAX) continue;
      if (remove[from] && !remove[to]) {
        remove[to] = true;
        changed = true;
      }
    }
  }
}

static void prune_removed_edges(ZProgramGraph *graph, const bool *remove, const size_t *edge_from, const size_t *edge_to) {
  size_t write = 0;
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    ZProgramGraphEdge *edge = &graph->edges[i];
    bool drop = (edge_from[i] != SIZE_MAX && remove[edge_from[i]]) ||
                (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge_to[i] != SIZE_MAX && remove[edge_to[i]]);
    if (drop) {
      prune_free_edge_fields(edge);
      continue;
    }
    if (write != i) {
      graph->edges[write] = graph->edges[i];
      graph->edges[i] = (ZProgramGraphEdge){0};
    }
    write++;
  }
  if (graph) graph->edge_len = write;
}

void z_program_graph_prune_embedded_std_source_nodes(ZProgramGraph *graph) {
  if (!graph || graph->node_len == 0) return;
  bool *remove = z_checked_calloc(graph->node_len, sizeof(bool));
  PruneEmbeddedPaths embedded;
  prune_embedded_paths_collect(graph, &embedded);
  bool has_local_source = false;
  size_t removed_len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    bool embedded_std = prune_path_is_embedded_std(&embedded, graph->nodes[i].path);
    bool appended_std_helper = prune_node_is_appended_std_helper(&graph->nodes[i]);
    if (embedded_std || appended_std_helper) {
      remove[i] = true;
      removed_len++;
    } else {
      has_local_source = true;
    }
  }
  const char **node_ids = z_checked_calloc(graph->node_len, sizeof(const char *));
  for (size_t i = 0; i < graph->node_len; i++) node_ids[i] = graph->nodes[i].id;
  ZProgramGraphAdjacencyNodeEntry *id_index = z_program_graph_id_index_build(node_ids, graph->node_len);
  size_t *edge_from = z_checked_calloc(graph->edge_len ? graph->edge_len : 1, sizeof(size_t));
  size_t *edge_to = z_checked_calloc(graph->edge_len ? graph->edge_len : 1, sizeof(size_t));
  for (size_t i = 0; i < graph->edge_len; i++) {
    edge_from[i] = z_program_graph_id_index_find(id_index, graph->node_len, graph->edges[i].from);
    edge_to[i] = graph->edges[i].target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE
      ? z_program_graph_id_index_find(id_index, graph->node_len, graph->edges[i].to)
      : SIZE_MAX;
  }
  prune_expand_removed_subgraphs(graph, remove, edge_from, edge_to);
  removed_len = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (remove[i]) removed_len++;
  }
  if (has_local_source && removed_len > 0) {
    prune_removed_edges(graph, remove, edge_from, edge_to);
    size_t write = 0;
    for (size_t i = 0; i < graph->node_len; i++) {
      if (remove[i]) {
        prune_free_node_fields(&graph->nodes[i]);
        continue;
      }
      if (write != i) {
        graph->nodes[write] = graph->nodes[i];
        graph->nodes[i] = (ZProgramGraphNode){0};
      }
      write++;
    }
    graph->node_len = write;
  }
  free(edge_to);
  free(edge_from);
  free(id_index);
  free(node_ids);
  free(embedded.paths);
  free(remove);
}
