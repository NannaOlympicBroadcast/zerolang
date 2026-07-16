#include "program_graph.h"

static char *program_graph_clone_text(const char *text) {
  return text ? z_strdup(text) : NULL;
}

static void program_graph_clone_node(const ZProgramGraphNode *from, ZProgramGraphNode *to) {
  *to = (ZProgramGraphNode){
    .id = program_graph_clone_text(from->id),
    .kind = from->kind,
    .name = program_graph_clone_text(from->name),
    .type = program_graph_clone_text(from->type),
    .value = program_graph_clone_text(from->value),
    .path = program_graph_clone_text(from->path),
    .symbol_id = program_graph_clone_text(from->symbol_id),
    .type_id = program_graph_clone_text(from->type_id),
    .effect_id = program_graph_clone_text(from->effect_id),
    .node_hash = program_graph_clone_text(from->node_hash),
    .line = from->line,
    .column = from->column,
    .is_public = from->is_public,
    .is_mutable = from->is_mutable,
    .is_static = from->is_static,
    .fallible = from->fallible,
    .export_c = from->export_c,
  };
}

static void program_graph_clone_edge(const ZProgramGraphEdge *from, ZProgramGraphEdge *to) {
  *to = (ZProgramGraphEdge){
    .from = program_graph_clone_text(from->from),
    .to = program_graph_clone_text(from->to),
    .kind = program_graph_clone_text(from->kind),
    .target = from->target,
    .order = from->order,
  };
}

bool z_program_graph_clone(const ZProgramGraph *from, ZProgramGraph *out) {
  if (!from || !out) return false;
  *out = (ZProgramGraph){
    .schema_version = from->schema_version,
    .validation_state = from->validation_state,
    .module_identity = program_graph_clone_text(from->module_identity),
    .graph_hash = program_graph_clone_text(from->graph_hash),
    .node_len = from->node_len,
    .node_cap = from->node_len,
    .edge_len = from->edge_len,
    .edge_cap = from->edge_len,
    .next_id = from->next_id,
    .canonical_source = from->canonical_source,
  };
  if (from->node_len > 0) {
    out->nodes = z_checked_calloc(from->node_len, sizeof(ZProgramGraphNode));
    for (size_t i = 0; i < from->node_len; i++) program_graph_clone_node(&from->nodes[i], &out->nodes[i]);
  }
  if (from->edge_len > 0) {
    out->edges = z_checked_calloc(from->edge_len, sizeof(ZProgramGraphEdge));
    for (size_t i = 0; i < from->edge_len; i++) program_graph_clone_edge(&from->edges[i], &out->edges[i]);
  }
  return true;
}
