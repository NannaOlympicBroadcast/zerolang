#ifndef ZERO_PROGRAM_GRAPH_ADJACENCY_H
#define ZERO_PROGRAM_GRAPH_ADJACENCY_H

#include "program_graph.h"

typedef struct {
  const char *id;
  size_t node_index;
} ZProgramGraphAdjacencyNodeEntry;

typedef struct {
  const ZProgramGraphEdge *edge;
  size_t edge_index;
} ZProgramGraphAdjacencyEdgeEntry;

typedef struct {
  const ZProgramGraph *graph;
  ZProgramGraphAdjacencyNodeEntry *nodes_by_id;
  ZProgramGraphAdjacencyEdgeEntry *edges_by_owner;
  ZProgramGraphAdjacencyEdgeEntry *edges_by_child;
  size_t node_edge_len;
} ZProgramGraphAdjacency;

ZProgramGraphAdjacencyNodeEntry *z_program_graph_id_index_build(const char *const *ids, size_t len);
size_t z_program_graph_id_index_find(const ZProgramGraphAdjacencyNodeEntry *entries, size_t len, const char *id);
void z_program_graph_id_index_run(const ZProgramGraphAdjacencyNodeEntry *entries, size_t len, const char *id, size_t *start, size_t *run_len);
void z_program_graph_adjacency_init(ZProgramGraphAdjacency *adjacency, const ZProgramGraph *graph);
void z_program_graph_adjacency_free(ZProgramGraphAdjacency *adjacency);
size_t z_program_graph_adjacency_node_index(const ZProgramGraphAdjacency *adjacency, const char *id);
const ZProgramGraphNode *z_program_graph_adjacency_node(const ZProgramGraphAdjacency *adjacency, const char *id);
void z_program_graph_adjacency_owner_run(const ZProgramGraphAdjacency *adjacency, const char *from, const char *kind, size_t *start, size_t *len);
const ZProgramGraphEdge *z_program_graph_adjacency_owner_edge_at(const ZProgramGraphAdjacency *adjacency, size_t position);
void z_program_graph_adjacency_child_run(const ZProgramGraphAdjacency *adjacency, const char *to, const char *kind, size_t *start, size_t *len);
const ZProgramGraphEdge *z_program_graph_adjacency_child_edge_at(const ZProgramGraphAdjacency *adjacency, size_t position);
const ZProgramGraphEdge *z_program_graph_adjacency_first_child_edge(const ZProgramGraphAdjacency *adjacency, const char *to);
bool z_program_graph_adjacency_has_child_edge(const ZProgramGraphAdjacency *adjacency, const char *to, const char *kind);

#endif
