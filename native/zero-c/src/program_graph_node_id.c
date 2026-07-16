#include "program_graph.h"

#include "program_graph_adjacency.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int graph_text_cmp(const char *left, const char *right) {
  const unsigned char *a = (const unsigned char *)(left ? left : "");
  const unsigned char *b = (const unsigned char *)(right ? right : "");
  while (*a && *b && *a == *b) {
    a++;
    b++;
  }
  return (int)*a - (int)*b;
}

static bool graph_text_eq(const char *left, const char *right) {
  return graph_text_cmp(left, right) == 0;
}

static uint64_t graph_hash_text(uint64_t hash, const char *text) {
  const unsigned char *p = (const unsigned char *)(text ? text : "");
  while (*p) {
    hash ^= (uint64_t)*p++;
    hash *= 1099511628211ull;
  }
  hash ^= 0xffu;
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t graph_hash_u64(uint64_t hash, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) {
    hash ^= (value >> (i * 8)) & 0xffu;
    hash *= 1099511628211ull;
  }
  return hash;
}

bool z_program_graph_node_id_valid(const char *id) {
  if (!id || id[0] != '#' || !id[1]) return false;
  for (const char *cursor = id + 1; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (!((ch >= 'a' && ch <= 'z') ||
          (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') ||
          ch == '_' || ch == '-' || ch == '.')) {
      return false;
    }
  }
  return true;
}

static const char *graph_node_id_domain(const ZProgramGraphNode *node) {
  if (!node) return "node";
  switch (node->kind) {
    case Z_PROGRAM_GRAPH_NODE_MODULE: return "mod";
    case Z_PROGRAM_GRAPH_NODE_IMPORT: return "imp";
    case Z_PROGRAM_GRAPH_NODE_C_IMPORT: return "cimp";
    case Z_PROGRAM_GRAPH_NODE_CONST:
    case Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS:
    case Z_PROGRAM_GRAPH_NODE_SHAPE:
    case Z_PROGRAM_GRAPH_NODE_INTERFACE:
    case Z_PROGRAM_GRAPH_NODE_ENUM:
    case Z_PROGRAM_GRAPH_NODE_CHOICE:
    case Z_PROGRAM_GRAPH_NODE_FUNCTION:
      return "decl";
    case Z_PROGRAM_GRAPH_NODE_PARAM: return "param";
    case Z_PROGRAM_GRAPH_NODE_FIELD: return "field";
    case Z_PROGRAM_GRAPH_NODE_ENUM_CASE:
    case Z_PROGRAM_GRAPH_NODE_CHOICE_CASE:
      return "case";
    case Z_PROGRAM_GRAPH_NODE_BLOCK: return "block";
    case Z_PROGRAM_GRAPH_NODE_LET:
    case Z_PROGRAM_GRAPH_NODE_ASSIGNMENT:
    case Z_PROGRAM_GRAPH_NODE_DEFER:
    case Z_PROGRAM_GRAPH_NODE_CHECK:
    case Z_PROGRAM_GRAPH_NODE_RETURN:
    case Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT:
    case Z_PROGRAM_GRAPH_NODE_IF:
    case Z_PROGRAM_GRAPH_NODE_WHILE:
    case Z_PROGRAM_GRAPH_NODE_FOR:
    case Z_PROGRAM_GRAPH_NODE_BREAK:
    case Z_PROGRAM_GRAPH_NODE_CONTINUE:
    case Z_PROGRAM_GRAPH_NODE_MATCH:
    case Z_PROGRAM_GRAPH_NODE_RAISE:
    case Z_PROGRAM_GRAPH_NODE_MATCH_ARM:
    case Z_PROGRAM_GRAPH_NODE_STATEMENT:
      return "stmt";
    case Z_PROGRAM_GRAPH_NODE_TYPE_REF: return "type";
    case Z_PROGRAM_GRAPH_NODE_EFFECT_REF: return "effect";
    case Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT: return "err";
    default:
      return "expr";
  }
}

static bool graph_edge_kind_is_declaration(const char *kind) {
  return graph_text_eq(kind, "import") || graph_text_eq(kind, "cImport") ||
         graph_text_eq(kind, "const") || graph_text_eq(kind, "alias") ||
         graph_text_eq(kind, "shape") || graph_text_eq(kind, "interface") ||
         graph_text_eq(kind, "enum") || graph_text_eq(kind, "choice") || graph_text_eq(kind, "function") ||
         graph_text_eq(kind, "method");
}

static bool graph_edge_order_participates(const ZProgramGraphNode *node, const ZProgramGraphEdge *edge) {
  if (!edge) return false;
  if (node && graph_edge_kind_is_declaration(edge->kind)) return false;
  if (graph_text_eq(edge->kind, "statement")) return false;
  return true;
}

static uint64_t graph_node_id_hash_value(const ZProgramGraph *graph, const ZProgramGraphNode *node, const ZProgramGraphEdge *owner_edge, const char *owner_new_id) {
  uint64_t hash = 1469598103934665603ull;
  hash = graph_hash_text(hash, graph && graph->module_identity ? graph->module_identity : "");
  hash = graph_hash_text(hash, graph_node_id_domain(node));
  hash = graph_hash_text(hash, z_program_graph_node_kind_name(node->kind));
  hash = graph_hash_text(hash, node->type);
  hash = graph_hash_u64(hash, node->is_public ? 1 : 0);
  hash = graph_hash_u64(hash, node->is_mutable ? 1 : 0);
  hash = graph_hash_u64(hash, node->is_static ? 1 : 0);
  hash = graph_hash_u64(hash, node->fallible ? 1 : 0);
  hash = graph_hash_u64(hash, node->export_c ? 1 : 0);
  if (owner_edge) {
    hash = graph_hash_text(hash, owner_new_id);
    hash = graph_hash_text(hash, owner_edge->kind);
    if (graph_edge_order_participates(node, owner_edge)) hash = graph_hash_u64(hash, (uint64_t)owner_edge->order);
  } else if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE) {
    hash = graph_hash_text(hash, node->name);
  }
  return hash;
}

static uint64_t z_program_graph_id_set_hash(const char *id) {
  uint64_t hash = 1469598103934665603ull;
  for (const unsigned char *p = (const unsigned char *)(id ? id : ""); *p; p++) {
    hash ^= (uint64_t)*p;
    hash *= 1099511628211ull;
  }
  return hash;
}

void z_program_graph_id_set_init(ZProgramGraphIdSet *set, size_t expected) {
  set->cap = 16;
  while (set->cap < expected * 2) set->cap *= 2;
  set->slots = z_checked_calloc(set->cap, sizeof(const char *));
  set->len = 0;
}

void z_program_graph_id_set_free(ZProgramGraphIdSet *set) {
  free(set->slots);
  *set = (ZProgramGraphIdSet){0};
}

bool z_program_graph_id_set_has(const ZProgramGraphIdSet *set, const char *id) {
  if (!set || set->cap == 0 || !id) return false;
  size_t slot = (size_t)(z_program_graph_id_set_hash(id) & (set->cap - 1));
  while (set->slots[slot]) {
    if (graph_text_eq(set->slots[slot], id)) return true;
    slot = (slot + 1) & (set->cap - 1);
  }
  return false;
}

void z_program_graph_id_set_add(ZProgramGraphIdSet *set, const char *id) {
  if (!set || !id) return;
  if ((set->len + 1) * 2 > set->cap) {
    ZProgramGraphIdSet grown;
    z_program_graph_id_set_init(&grown, set->len + 1);
    for (size_t i = 0; i < set->cap; i++) {
      if (set->slots[i]) z_program_graph_id_set_add(&grown, set->slots[i]);
    }
    free(set->slots);
    *set = grown;
  }
  size_t slot = (size_t)(z_program_graph_id_set_hash(id) & (set->cap - 1));
  while (set->slots[slot]) {
    if (graph_text_eq(set->slots[slot], id)) return;
    slot = (slot + 1) & (set->cap - 1);
  }
  set->slots[slot] = id;
  set->len++;
}

static size_t graph_find_old_id(const ZProgramGraphAdjacencyNodeEntry *old_id_index, size_t len, const char *id) {
  return z_program_graph_id_index_find(old_id_index, len, id);
}

char *z_program_graph_source_node_base_id(const ZProgramGraph *graph, const ZProgramGraphNode *node, const ZProgramGraphEdge *owner_edge, const char *owner_new_id) {
  uint64_t hash = graph_node_id_hash_value(graph, node, owner_edge, owner_new_id);
  ZBuf base;
  zbuf_init(&base);
  zbuf_appendf(&base, "#%s_%08llx", graph_node_id_domain(node), (unsigned long long)(hash & 0xffffffffull));
  return base.data ? base.data : z_strdup("#node_00000000");
}

static uint64_t graph_collision_hash_value(const ZProgramGraphNode *node) {
  uint64_t hash = 1469598103934665603ull;
  hash = graph_hash_text(hash, graph_node_id_domain(node));
  hash = graph_hash_text(hash, z_program_graph_node_kind_name(node->kind));
  hash = graph_hash_text(hash, node->name);
  hash = graph_hash_text(hash, node->type);
  hash = graph_hash_text(hash, node->value);
  hash = graph_hash_text(hash, node->path);
  hash = graph_hash_u64(hash, node->is_public ? 1 : 0);
  hash = graph_hash_u64(hash, node->is_mutable ? 1 : 0);
  hash = graph_hash_u64(hash, node->is_static ? 1 : 0);
  hash = graph_hash_u64(hash, node->fallible ? 1 : 0);
  hash = graph_hash_u64(hash, node->export_c ? 1 : 0);
  return hash;
}

char *z_program_graph_source_node_collision_id(const ZProgramGraphNode *node, const char *base_id, const ZProgramGraphIdSet *used, bool force_suffix) {
  ZBuf base;
  zbuf_init(&base);
  zbuf_append(&base, base_id);
  if (!force_suffix && !z_program_graph_id_set_has(used, base.data)) return base.data ? base.data : z_strdup("#node_00000000");
  uint64_t collision = graph_collision_hash_value(node);
  for (size_t attempt = 0;; attempt++) {
    ZBuf unique;
    zbuf_init(&unique);
    zbuf_append(&unique, base.data);
    if (attempt == 0) zbuf_appendf(&unique, "-%04llx", (unsigned long long)(collision & 0xffffull));
    else zbuf_appendf(&unique, "-%04llx-%zu", (unsigned long long)(collision & 0xffffull), attempt);
    if (z_program_graph_id_set_has(used, unique.data)) {
      zbuf_free(&unique);
      continue;
    }
    zbuf_free(&base);
    return unique.data ? unique.data : z_strdup("#node_00000000");
  }
}

static int graph_collision_node_cmp(const ZProgramGraphNode *left, const ZProgramGraphNode *right) {
  int cmp = graph_text_cmp(left ? left->path : NULL, right ? right->path : NULL);
  if (cmp != 0) return cmp;
  int left_line = left && left->line > 0 ? left->line : 0;
  int right_line = right && right->line > 0 ? right->line : 0;
  if (left_line != right_line) return left_line < right_line ? -1 : 1;
  int left_column = left && left->column > 0 ? left->column : 0;
  int right_column = right && right->column > 0 ? right->column : 0;
  if (left_column != right_column) return left_column < right_column ? -1 : 1;
  cmp = graph_text_cmp(left ? left->name : NULL, right ? right->name : NULL);
  if (cmp != 0) return cmp;
  cmp = graph_text_cmp(left ? left->value : NULL, right ? right->value : NULL);
  if (cmp != 0) return cmp;
  cmp = graph_text_cmp(left ? left->type : NULL, right ? right->type : NULL);
  if (cmp != 0) return cmp;
  return 0;
}

static void graph_sort_collision_group(const ZProgramGraph *graph, size_t *items, size_t len) {
  for (size_t i = 1; i < len; i++) {
    size_t item = items[i];
    size_t cursor = i;
    while (cursor > 0 && graph_collision_node_cmp(&graph->nodes[item], &graph->nodes[items[cursor - 1]]) < 0) {
      items[cursor] = items[cursor - 1];
      cursor--;
    }
    items[cursor] = item;
  }
}

void z_program_graph_assign_source_node_ids(ZProgramGraph *graph) {
  if (!graph || graph->node_len == 0) return;
  char **old_ids = z_checked_calloc(graph->node_len, sizeof(char *));
  char **new_ids = z_checked_calloc(graph->node_len, sizeof(char *));
  char **base_ids = z_checked_calloc(graph->node_len, sizeof(char *));
  bool *assigned = z_checked_calloc(graph->node_len, sizeof(bool));
  size_t *owner_edge_by_node = z_checked_calloc(graph->node_len, sizeof(size_t));
  size_t *owner_index_by_node = z_checked_calloc(graph->node_len, sizeof(size_t));
  size_t *ready = z_checked_calloc(graph->node_len, sizeof(size_t));
  bool *ready_used = z_checked_calloc(graph->node_len, sizeof(bool));
  size_t *group = z_checked_calloc(graph->node_len, sizeof(size_t));
  for (size_t i = 0; i < graph->node_len; i++) old_ids[i] = z_strdup(graph->nodes[i].id ? graph->nodes[i].id : "");
  ZProgramGraphAdjacencyNodeEntry *old_id_index = z_program_graph_id_index_build((const char *const *)old_ids, graph->node_len);
  for (size_t i = 0; i < graph->node_len; i++) owner_edge_by_node[i] = SIZE_MAX;
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
    size_t target_index = graph_find_old_id(old_id_index, graph->node_len, edge->to);
    if (target_index != SIZE_MAX && owner_edge_by_node[target_index] == SIZE_MAX) owner_edge_by_node[target_index] = i;
  }
  for (size_t i = 0; i < graph->node_len; i++) {
    owner_index_by_node[i] = owner_edge_by_node[i] == SIZE_MAX
      ? SIZE_MAX
      : graph_find_old_id(old_id_index, graph->node_len, graph->edges[owner_edge_by_node[i]].from);
  }
  ZProgramGraphIdSet used_ids;
  z_program_graph_id_set_init(&used_ids, graph->node_len);

  size_t assigned_count = 0;
  while (assigned_count < graph->node_len) {
    size_t ready_len = 0;
    for (size_t i = 0; i < graph->node_len; i++) {
      if (assigned[i]) continue;
      const ZProgramGraphEdge *owner_edge = owner_edge_by_node[i] == SIZE_MAX ? NULL : &graph->edges[owner_edge_by_node[i]];
      const char *owner_new_id = NULL;
      if (owner_edge) {
        size_t owner_index = owner_index_by_node[i];
        if (owner_index != SIZE_MAX) {
          if (!assigned[owner_index]) continue;
          owner_new_id = new_ids[owner_index];
        }
      }
      free(base_ids[i]);
      base_ids[i] = z_program_graph_source_node_base_id(graph, &graph->nodes[i], owner_edge, owner_new_id);
      ready[ready_len++] = i;
    }
    if (ready_len == 0) {
      for (size_t i = 0; i < graph->node_len; i++) {
        if (assigned[i]) continue;
        free(base_ids[i]);
        base_ids[i] = z_program_graph_source_node_base_id(graph, &graph->nodes[i], NULL, NULL);
        ready[ready_len++] = i;
      }
    }

    const char **ready_base_ids = z_checked_calloc(ready_len ? ready_len : 1, sizeof(const char *));
    for (size_t i = 0; i < ready_len; i++) ready_base_ids[i] = base_ids[ready[i]];
    ZProgramGraphAdjacencyNodeEntry *ready_base_index = z_program_graph_id_index_build(ready_base_ids, ready_len);
    for (size_t i = 0; i < ready_len; i++) ready_used[i] = false;
    for (size_t i = 0; i < ready_len; i++) {
      if (ready_used[i]) continue;
      size_t run_start = 0;
      size_t run_len = 0;
      z_program_graph_id_index_run(ready_base_index, ready_len, base_ids[ready[i]], &run_start, &run_len);
      size_t group_len = 0;
      for (size_t j = 0; j < run_len; j++) {
        size_t ready_position = ready_base_index[run_start + j].node_index;
        ready_used[ready_position] = true;
        group[group_len++] = ready[ready_position];
      }
      graph_sort_collision_group(graph, group, group_len);
      for (size_t j = 0; j < group_len; j++) {
        size_t index = group[j];
        new_ids[index] = z_program_graph_source_node_collision_id(&graph->nodes[index], base_ids[index], &used_ids, group_len > 1);
        z_program_graph_id_set_add(&used_ids, new_ids[index]);
        assigned[index] = true;
        assigned_count++;
      }
    }
    free(ready_base_index);
    free(ready_base_ids);
  }

  for (size_t i = 0; i < graph->node_len; i++) {
    free(graph->nodes[i].id);
    graph->nodes[i].id = z_strdup(new_ids[i]);
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    size_t from_index = graph_find_old_id(old_id_index, graph->node_len, graph->edges[i].from);
    const char *from = from_index == SIZE_MAX ? graph->edges[i].from : new_ids[from_index];
    free(graph->edges[i].from);
    graph->edges[i].from = z_strdup(from);
    if (graph->edges[i].target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) {
      size_t to_index = graph_find_old_id(old_id_index, graph->node_len, graph->edges[i].to);
      const char *to = to_index == SIZE_MAX ? graph->edges[i].to : new_ids[to_index];
      free(graph->edges[i].to);
      graph->edges[i].to = z_strdup(to);
    }
  }
  for (size_t i = 0; i < graph->node_len; i++) {
    free(old_ids[i]);
    free(new_ids[i]);
    free(base_ids[i]);
  }
  z_program_graph_id_set_free(&used_ids);
  free(old_id_index);
  free(group);
  free(ready_used);
  free(ready);
  free(owner_index_by_node);
  free(owner_edge_by_node);
  free(assigned);
  free(base_ids);
  free(old_ids);
  free(new_ids);
}
