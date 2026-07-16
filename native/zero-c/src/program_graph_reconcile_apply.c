#include "program_graph_reconcile_apply.h"

#include "program_graph_adjacency.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef char IdentityIdBase[80];

typedef struct {
  const ZProgramGraph *base;
  ZProgramGraph *edited;
  size_t *base_to_edited;
  bool *edited_matched;
  ZProgramGraphAdjacencyNodeEntry *base_id_index;
  ZProgramGraphAdjacencyNodeEntry *edited_id_index;
  size_t *base_owner_edge;
  size_t *edited_owner_edge;
  size_t *base_edge_source;
  size_t *edited_edge_source;
  size_t *base_child_start;
  size_t *base_child_edges;
  size_t *base_child_nodes;
  size_t *edited_child_start;
  size_t *edited_child_edges;
  size_t *edited_child_nodes;
  uint64_t *base_fingerprints;
  bool *base_fingerprints_done;
  uint64_t *edited_fingerprints;
  bool *edited_fingerprints_done;
  IdentityIdBase *base_id_bases;
  IdentityIdBase *edited_id_bases;
  const char **edited_id_base_ptrs;
  ZProgramGraphAdjacencyNodeEntry *edited_id_base_index;
  const char *exclude_path;
  ZProgramGraphIdentityReconcile result;
} IdentityContext;

typedef bool (*IdentityCandidateFn)(IdentityContext *context, size_t base_index, size_t edited_index);

static size_t identity_missing(void) { return SIZE_MAX; }

static size_t identity_resolve_best_candidate(IdentityContext *context, size_t base_index, const size_t *candidates, size_t count);
static bool identity_resolve_reverse_unique(IdentityContext *context, IdentityCandidateFn candidate, size_t base_index, size_t edited_index);

static bool identity_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool identity_text_present(const char *text) { return text && text[0]; }

/* File-scope rewrite acceptance: nodes on the excluded path are withheld from
 * every matching pass so the file's base nodes count as deleted and its edited
 * nodes keep fresh identities. */
static bool identity_base_excluded(const IdentityContext *context, size_t base_index) {
  return context->exclude_path && base_index < context->base->node_len &&
         identity_text_eq(context->base->nodes[base_index].path, context->exclude_path);
}

static bool identity_edited_excluded(const IdentityContext *context, size_t edited_index) {
  return context->exclude_path && edited_index < context->edited->node_len &&
         identity_text_eq(context->edited->nodes[edited_index].path, context->exclude_path);
}

static void identity_fail(IdentityContext *context, const char *code, const char *message, const char *node_id, const char *candidate_id) {
  if (!context || !context->result.ok) return;
  context->result.ok = false;
  context->result.ambiguous = identity_text_eq(code, "GRC001");
  context->result.module_identity_changed = identity_text_eq(code, "GRC003");
  snprintf(context->result.code, sizeof(context->result.code), "%s", code ? code : "GRC000");
  snprintf(context->result.message, sizeof(context->result.message), "%s", message ? message : "program graph source identity could not be preserved");
  snprintf(context->result.node_id, sizeof(context->result.node_id), "%s", node_id ? node_id : "");
  snprintf(context->result.candidate_id, sizeof(context->result.candidate_id), "%s", candidate_id ? candidate_id : "");
}

static size_t identity_find_node(const IdentityContext *context, const ZProgramGraph *graph, const char *id) {
  if (!context || !graph || !id) return identity_missing();
  const ZProgramGraphAdjacencyNodeEntry *index = graph == context->base ? context->base_id_index : context->edited_id_index;
  return z_program_graph_id_index_find(index, graph->node_len, id);
}

static bool identity_owner_edge_kind(const char *kind) {
  static const char *const owner_kinds[] = {
    "alias",
    "arg",
    "arm",
    "body",
    "cImport",
    "case",
    "choice",
    "const",
    "constraint",
    "declaredType",
    "default",
    "effect",
    "else",
    "enum",
    "error",
    "expr",
    "field",
    "function",
    "guard",
    "import",
    "interface",
    "left",
    "method",
    "param",
    "rangeEnd",
    "returnType",
    "right",
    "shape",
    "statement",
    "target",
    "then",
    "type",
    "typeArg",
    "typeParam",
    "value",
    "variant",
  };
  for (size_t i = 0; i < sizeof(owner_kinds) / sizeof(owner_kinds[0]); i++) {
    if (identity_text_eq(kind, owner_kinds[i])) return true;
  }
  return false;
}

static size_t identity_owner_edge_index(const IdentityContext *context, const ZProgramGraph *graph, size_t node_index) {
  if (!context || !graph || node_index >= graph->node_len) return identity_missing();
  const size_t *owner_edges = graph == context->base ? context->base_owner_edge : context->edited_owner_edge;
  return owner_edges ? owner_edges[node_index] : identity_missing();
}

static size_t *identity_build_owner_edges(const IdentityContext *context, const ZProgramGraph *graph) {
  size_t *owner_edges = z_checked_calloc(graph && graph->node_len ? graph->node_len : 1, sizeof(size_t));
  for (size_t i = 0; graph && i < graph->node_len; i++) owner_edges[i] = identity_missing();
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !identity_owner_edge_kind(edge->kind)) continue;
    size_t target_index = identity_find_node(context, graph, edge->to);
    if (target_index != identity_missing() && owner_edges[target_index] == identity_missing()) owner_edges[target_index] = i;
  }
  return owner_edges;
}

static size_t identity_edge_source_index(const IdentityContext *context, const ZProgramGraph *graph, size_t edge_index) {
  if (!context || !graph || edge_index >= graph->edge_len) return identity_missing();
  const size_t *edge_sources = graph == context->base ? context->base_edge_source : context->edited_edge_source;
  return edge_sources ? edge_sources[edge_index] : identity_missing();
}

static size_t *identity_build_edge_sources(const IdentityContext *context, const ZProgramGraph *graph) {
  size_t *edge_sources = z_checked_calloc(graph && graph->edge_len ? graph->edge_len : 1, sizeof(size_t));
  for (size_t i = 0; graph && i < graph->edge_len; i++) edge_sources[i] = identity_find_node(context, graph, graph->edges[i].from);
  return edge_sources;
}

static int identity_text_cmp(const char *left, const char *right) {
  const unsigned char *a = (const unsigned char *)(left ? left : "");
  const unsigned char *b = (const unsigned char *)(right ? right : "");
  while (*a && *b && *a == *b) {
    a++;
    b++;
  }
  return (int)*a - (int)*b;
}

static bool identity_child_order_before(const ZProgramGraph *graph, size_t left_edge, size_t right_edge) {
  const ZProgramGraphEdge *left = &graph->edges[left_edge];
  const ZProgramGraphEdge *right = &graph->edges[right_edge];
  int kind_cmp = identity_text_cmp(left->kind, right->kind);
  if (kind_cmp != 0) return kind_cmp < 0;
  if (left->order != right->order) return left->order < right->order;
  return left_edge < right_edge;
}

static void identity_build_child_index(IdentityContext *context, const ZProgramGraph *graph, size_t **out_start, size_t **out_edges, size_t **out_nodes) {
  size_t len = graph ? graph->node_len : 0;
  size_t *start = z_checked_calloc(len + 1, sizeof(size_t));
  size_t *edges = z_checked_calloc(len ? len : 1, sizeof(size_t));
  size_t *nodes = z_checked_calloc(len ? len : 1, sizeof(size_t));
  size_t *cursor = z_checked_calloc(len ? len : 1, sizeof(size_t));
  const size_t *owner_edges = graph == context->base ? context->base_owner_edge : context->edited_owner_edge;
  for (size_t i = 0; i < len; i++) {
    size_t edge_index = owner_edges[i];
    if (edge_index == identity_missing()) continue;
    size_t owner = identity_edge_source_index(context, graph, edge_index);
    if (owner != identity_missing()) start[owner + 1]++;
  }
  for (size_t i = 0; i < len; i++) start[i + 1] += start[i];
  for (size_t i = 0; i < len; i++) {
    size_t edge_index = owner_edges[i];
    if (edge_index == identity_missing()) continue;
    size_t owner = identity_edge_source_index(context, graph, edge_index);
    if (owner == identity_missing()) continue;
    size_t slot = start[owner] + cursor[owner]++;
    edges[slot] = edge_index;
    nodes[slot] = i;
  }
  for (size_t owner = 0; owner < len; owner++) {
    for (size_t i = start[owner] + 1; i < start[owner + 1]; i++) {
      size_t edge_item = edges[i];
      size_t node_item = nodes[i];
      size_t at = i;
      while (at > start[owner] && identity_child_order_before(graph, edge_item, edges[at - 1])) {
        edges[at] = edges[at - 1];
        nodes[at] = nodes[at - 1];
        at--;
      }
      edges[at] = edge_item;
      nodes[at] = node_item;
    }
  }
  free(cursor);
  *out_start = start;
  *out_edges = edges;
  *out_nodes = nodes;
}

static uint64_t identity_fp_text(uint64_t hash, const char *text) {
  const unsigned char *p = (const unsigned char *)(text ? text : "");
  while (*p) {
    hash ^= (uint64_t)*p++;
    hash *= 1099511628211ull;
  }
  hash ^= 0xffu;
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t identity_fp_u64(uint64_t hash, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) {
    hash ^= (value >> (i * 8)) & 0xffu;
    hash *= 1099511628211ull;
  }
  return hash;
}

static uint64_t identity_subtree_fingerprint(IdentityContext *context, const ZProgramGraph *graph, size_t node_index, size_t depth) {
  bool is_base = graph == context->base;
  uint64_t *memo = is_base ? context->base_fingerprints : context->edited_fingerprints;
  bool *done = is_base ? context->base_fingerprints_done : context->edited_fingerprints_done;
  if (node_index >= graph->node_len) return 0;
  if (done[node_index]) return memo[node_index];
  const ZProgramGraphNode *node = &graph->nodes[node_index];
  uint64_t hash = 1469598103934665603ull;
  hash = identity_fp_text(hash, z_program_graph_node_kind_name(node->kind));
  hash = identity_fp_text(hash, node->name);
  hash = identity_fp_text(hash, node->type);
  hash = identity_fp_text(hash, node->value);
  hash = identity_fp_text(hash, node->path);
  hash = identity_fp_u64(hash, (uint64_t)((node->is_public ? 1 : 0) | (node->is_mutable ? 2 : 0) | (node->is_static ? 4 : 0) | (node->fallible ? 8 : 0) | (node->export_c ? 16 : 0)));
  const size_t *start = is_base ? context->base_child_start : context->edited_child_start;
  const size_t *child_edges = is_base ? context->base_child_edges : context->edited_child_edges;
  const size_t *child_nodes = is_base ? context->base_child_nodes : context->edited_child_nodes;
  if (depth <= graph->node_len) {
    for (size_t i = start[node_index]; i < start[node_index + 1]; i++) {
      hash = identity_fp_text(hash, graph->edges[child_edges[i]].kind);
      hash = identity_fp_u64(hash, identity_subtree_fingerprint(context, graph, child_nodes[i], depth + 1));
    }
  }
  done[node_index] = true;
  memo[node_index] = hash;
  return hash;
}

static void identity_subtree_line_span(IdentityContext *context, const ZProgramGraph *graph, size_t node_index, int *min_line, int *max_line, size_t depth) {
  if (node_index >= graph->node_len || depth > graph->node_len) return;
  const ZProgramGraphNode *node = &graph->nodes[node_index];
  if (node->line > 0) {
    if (*min_line == 0 || node->line < *min_line) *min_line = node->line;
    if (node->line > *max_line) *max_line = node->line;
  }
  bool is_base = graph == context->base;
  const size_t *start = is_base ? context->base_child_start : context->edited_child_start;
  const size_t *child_nodes = is_base ? context->base_child_nodes : context->edited_child_nodes;
  for (size_t i = start[node_index]; i < start[node_index + 1]; i++) {
    identity_subtree_line_span(context, graph, child_nodes[i], min_line, max_line, depth + 1);
  }
}

static void identity_format_node_range(IdentityContext *context, const ZProgramGraph *graph, const size_t *nodes, size_t len, char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  out[0] = 0;
  const char *path = NULL;
  int min_line = 0;
  int max_line = 0;
  for (size_t i = 0; i < len; i++) {
    if (!nodes || nodes[i] >= graph->node_len) continue;
    if (!path && identity_text_present(graph->nodes[nodes[i]].path)) path = graph->nodes[nodes[i]].path;
    identity_subtree_line_span(context, graph, nodes[i], &min_line, &max_line, 0);
  }
  if (!path || min_line == 0) return;
  if (max_line > min_line) snprintf(out, out_len, "%s:%d-%d", path, min_line, max_line);
  else snprintf(out, out_len, "%s:%d", path, min_line);
}

static void identity_fail_nodes(IdentityContext *context, const char *code, const char *message, size_t base_index, const size_t *candidates, size_t candidate_len) {
  if (!context || !context->result.ok) return;
  const char *node_id = base_index < context->base->node_len ? context->base->nodes[base_index].id : "";
  const char *candidate_id = candidate_len > 0 && candidates && candidates[0] < context->edited->node_len ? context->edited->nodes[candidates[0]].id : "";
  identity_fail(context, code, message, node_id, candidate_id);
  if (base_index < context->base->node_len) {
    identity_format_node_range(context, context->base, &base_index, 1, context->result.node_range, sizeof(context->result.node_range));
  }
  if (candidate_len > 0) {
    identity_format_node_range(context, context->edited, candidates, candidate_len, context->result.candidate_range, sizeof(context->result.candidate_range));
  }
  context->result.candidate_count = candidate_len;
  if (context->result.node_range[0] && context->result.candidate_range[0]) {
    snprintf(context->result.hint,
             sizeof(context->result.hint),
             "split the text edit: import the change touching %.95s on its own first, or use zero patch",
             context->result.candidate_range);
  }
}

static bool identity_id_base(const char *id, char *out, size_t out_len) {
  if (!id || !out || out_len == 0) return false;
  const char *underscore = strchr(id, '_');
  if (!underscore) {
    snprintf(out, out_len, "%s", id);
    return true;
  }
  const char *cursor = underscore + 1;
  for (int i = 0; i < 8; i++) {
    char ch = cursor[i];
    bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    if (!hex) {
      snprintf(out, out_len, "%s", id);
      return true;
    }
  }
  size_t len = (size_t)(cursor + 8 - id);
  if (len >= out_len) len = out_len - 1;
  memcpy(out, id, len);
  out[len] = 0;
  return true;
}

static bool identity_node_payload_eq(const ZProgramGraphNode *left, const ZProgramGraphNode *right) {
  return left &&
         right &&
         left->kind == right->kind &&
         identity_text_eq(left->name, right->name) &&
         identity_text_eq(left->type, right->type) &&
         identity_text_eq(left->value, right->value) &&
         left->is_public == right->is_public &&
         left->is_mutable == right->is_mutable &&
         left->is_static == right->is_static &&
         left->fallible == right->fallible &&
         left->export_c == right->export_c;
}

static bool identity_match_nodes(IdentityContext *context, size_t base_index, size_t edited_index) {
  if (!context || base_index >= context->base->node_len || edited_index >= context->edited->node_len) return false;
  if (context->base_to_edited[base_index] != identity_missing() && context->base_to_edited[base_index] != edited_index) {
    identity_fail_nodes(context, "GRC001", "source edit has ambiguous graph identity", base_index, &edited_index, 1);
    return false;
  }
  if (context->edited_matched[edited_index] && context->base_to_edited[base_index] != edited_index) {
    identity_fail_nodes(context, "GRC001", "source edit has ambiguous graph identity", base_index, &edited_index, 1);
    return false;
  }
  context->base_to_edited[base_index] = edited_index;
  context->edited_matched[edited_index] = true;
  return true;
}

static void identity_match_exact_ids(IdentityContext *context) {
  for (size_t i = 0; context && context->result.ok && i < context->base->node_len; i++) {
    if (context->base_to_edited[i] != identity_missing() || identity_base_excluded(context, i)) continue;
    size_t edited = identity_find_node(context, context->edited, context->base->nodes[i].id);
    if (edited == identity_missing() || context->edited_matched[edited] || identity_edited_excluded(context, edited)) continue;
    identity_match_nodes(context, i, edited);
  }
}

static void identity_align_unbind_base(IdentityContext *context, size_t *edited_to_base, size_t base_index) {
  size_t edited = context->base_to_edited[base_index];
  if (edited == identity_missing()) return;
  context->base_to_edited[base_index] = identity_missing();
  context->edited_matched[edited] = false;
  edited_to_base[edited] = identity_missing();
}

static void identity_align_unbind_edited(IdentityContext *context, size_t *edited_to_base, size_t edited_index) {
  size_t base_index = edited_to_base[edited_index];
  if (base_index != identity_missing()) {
    identity_align_unbind_base(context, edited_to_base, base_index);
    return;
  }
  context->edited_matched[edited_index] = false;
}

static void identity_align_bind_subtree(IdentityContext *context, size_t *edited_to_base, size_t base_index, size_t edited_index, size_t depth) {
  if (depth > context->base->node_len || base_index >= context->base->node_len || edited_index >= context->edited->node_len) return;
  if (context->base_to_edited[base_index] != edited_index) {
    identity_align_unbind_base(context, edited_to_base, base_index);
    identity_align_unbind_edited(context, edited_to_base, edited_index);
    context->base_to_edited[base_index] = edited_index;
    context->edited_matched[edited_index] = true;
    edited_to_base[edited_index] = base_index;
  }
  size_t base_start = context->base_child_start[base_index];
  size_t base_end = context->base_child_start[base_index + 1];
  size_t edited_start = context->edited_child_start[edited_index];
  size_t edited_end = context->edited_child_start[edited_index + 1];
  if (base_end - base_start != edited_end - edited_start) return;
  for (size_t k = 0; k < base_end - base_start; k++) {
    size_t base_child = context->base_child_nodes[base_start + k];
    size_t edited_child = context->edited_child_nodes[edited_start + k];
    if (!identity_text_eq(context->base->edges[context->base_child_edges[base_start + k]].kind,
                          context->edited->edges[context->edited_child_edges[edited_start + k]].kind)) {
      return;
    }
    if (identity_subtree_fingerprint(context, context->base, base_child, 0) !=
        identity_subtree_fingerprint(context, context->edited, edited_child, 0)) {
      continue;
    }
    identity_align_bind_subtree(context, edited_to_base, base_child, edited_child, depth + 1);
  }
}

static size_t identity_statement_children(IdentityContext *context, const ZProgramGraph *graph, size_t node_index, size_t **out) {
  bool is_base = graph == context->base;
  const size_t *start = is_base ? context->base_child_start : context->edited_child_start;
  const size_t *child_edges = is_base ? context->base_child_edges : context->edited_child_edges;
  const size_t *child_nodes = is_base ? context->base_child_nodes : context->edited_child_nodes;
  size_t count = 0;
  for (size_t i = start[node_index]; i < start[node_index + 1]; i++) {
    if (identity_text_eq(graph->edges[child_edges[i]].kind, "statement")) count++;
  }
  *out = count ? z_checked_calloc(count, sizeof(size_t)) : NULL;
  size_t at = 0;
  for (size_t i = start[node_index]; i < start[node_index + 1]; i++) {
    if (identity_text_eq(graph->edges[child_edges[i]].kind, "statement")) (*out)[at++] = child_nodes[i];
  }
  return count;
}

static void identity_align_segment(IdentityContext *context, size_t *edited_to_base, const size_t *base_items, size_t base_len, const size_t *edited_items, size_t edited_len) {
  if (!context->result.ok) return;
  size_t prefix = 0;
  while (prefix < base_len && prefix < edited_len &&
         identity_subtree_fingerprint(context, context->base, base_items[prefix], 0) ==
           identity_subtree_fingerprint(context, context->edited, edited_items[prefix], 0)) {
    identity_align_bind_subtree(context, edited_to_base, base_items[prefix], edited_items[prefix], 0);
    prefix++;
  }
  size_t suffix = 0;
  while (suffix < base_len - prefix && suffix < edited_len - prefix &&
         identity_subtree_fingerprint(context, context->base, base_items[base_len - 1 - suffix], 0) ==
           identity_subtree_fingerprint(context, context->edited, edited_items[edited_len - 1 - suffix], 0)) {
    identity_align_bind_subtree(context, edited_to_base, base_items[base_len - 1 - suffix], edited_items[edited_len - 1 - suffix], 0);
    suffix++;
  }
  const size_t *base_mid = base_items + prefix;
  size_t base_mid_len = base_len - prefix - suffix;
  const size_t *edited_mid = edited_items + prefix;
  size_t edited_mid_len = edited_len - prefix - suffix;
  if (base_mid_len == 0 && edited_mid_len == 0) return;
  if (base_mid_len == 0) {
    for (size_t i = 0; i < edited_mid_len; i++) identity_align_unbind_edited(context, edited_to_base, edited_mid[i]);
    return;
  }
  if (edited_mid_len == 0) {
    for (size_t i = 0; i < base_mid_len; i++) identity_align_unbind_base(context, edited_to_base, base_mid[i]);
    return;
  }
  size_t anchor_count = 0;
  size_t gap_base = 0;
  size_t gap_edited = 0;
  size_t last_edited = 0;
  for (size_t i = 0; i < base_mid_len; i++) {
    uint64_t fingerprint = identity_subtree_fingerprint(context, context->base, base_mid[i], 0);
    size_t base_dup = 0;
    for (size_t j = 0; j < base_mid_len; j++) {
      if (identity_subtree_fingerprint(context, context->base, base_mid[j], 0) == fingerprint) base_dup++;
    }
    if (base_dup != 1) continue;
    size_t edited_at = identity_missing();
    size_t edited_dup = 0;
    for (size_t j = 0; j < edited_mid_len; j++) {
      if (identity_subtree_fingerprint(context, context->edited, edited_mid[j], 0) == fingerprint) {
        edited_dup++;
        edited_at = j;
      }
    }
    if (edited_dup != 1) continue;
    if (anchor_count > 0 && edited_at <= last_edited) continue;
    identity_align_segment(context, edited_to_base, base_mid + gap_base, i - gap_base, edited_mid + gap_edited, edited_at - gap_edited);
    identity_align_bind_subtree(context, edited_to_base, base_mid[i], edited_mid[edited_at], 0);
    gap_base = i + 1;
    gap_edited = edited_at + 1;
    last_edited = edited_at;
    anchor_count++;
  }
  if (anchor_count == 0) return;
  identity_align_segment(context, edited_to_base, base_mid + gap_base, base_mid_len - gap_base, edited_mid + gap_edited, edited_mid_len - gap_edited);
}

static size_t identity_base_node_depth(IdentityContext *context, size_t node_index, size_t *depths, unsigned char *states) {
  if (states[node_index] == 2) return depths[node_index];
  if (states[node_index] == 1) {
    states[node_index] = 2;
    depths[node_index] = 0;
    return 0;
  }
  states[node_index] = 1;
  size_t edge = context->base_owner_edge[node_index];
  size_t owner = edge == identity_missing() ? identity_missing() : identity_edge_source_index(context, context->base, edge);
  size_t depth = owner == identity_missing() ? 0 : identity_base_node_depth(context, owner, depths, states) + 1;
  states[node_index] = 2;
  depths[node_index] = depth;
  return depth;
}

static void identity_align_statement_sequences(IdentityContext *context) {
  if (!context || !context->result.ok || context->base->node_len == 0) return;
  size_t base_len = context->base->node_len;
  size_t edited_len = context->edited->node_len;
  size_t *edited_to_base = z_checked_calloc(edited_len ? edited_len : 1, sizeof(size_t));
  for (size_t i = 0; i < edited_len; i++) edited_to_base[i] = identity_missing();
  for (size_t i = 0; i < base_len; i++) {
    if (context->base_to_edited[i] != identity_missing()) edited_to_base[context->base_to_edited[i]] = i;
  }
  size_t *depths = z_checked_calloc(base_len, sizeof(size_t));
  unsigned char *states = z_checked_calloc(base_len, sizeof(unsigned char));
  for (size_t i = 0; i < base_len; i++) identity_base_node_depth(context, i, depths, states);
  size_t *order = z_checked_calloc(base_len, sizeof(size_t));
  size_t *bucket_start = z_checked_calloc(base_len + 2, sizeof(size_t));
  for (size_t i = 0; i < base_len; i++) bucket_start[depths[i] + 1]++;
  for (size_t d = 0; d < base_len + 1; d++) bucket_start[d + 1] += bucket_start[d];
  for (size_t i = 0; i < base_len; i++) order[bucket_start[depths[i]]++] = i;
  for (size_t k = 0; context->result.ok && k < base_len; k++) {
    size_t base_index = order[k];
    size_t edited_index = context->base_to_edited[base_index];
    if (edited_index == identity_missing()) continue;
    size_t *base_items = NULL;
    size_t *edited_items = NULL;
    size_t base_count = identity_statement_children(context, context->base, base_index, &base_items);
    size_t edited_count = identity_statement_children(context, context->edited, edited_index, &edited_items);
    if (base_count > 0 || edited_count > 0) {
      identity_align_segment(context, edited_to_base, base_items, base_count, edited_items, edited_count);
    }
    free(base_items);
    free(edited_items);
  }
  free(bucket_start);
  free(order);
  free(states);
  free(depths);
  free(edited_to_base);
}

static bool identity_source_anchor_candidate(IdentityContext *context, size_t base_index, size_t edited_index) {
  const ZProgramGraphNode *base = &context->base->nodes[base_index];
  const ZProgramGraphNode *edited = &context->edited->nodes[edited_index];
  if (base->kind != edited->kind ||
      base->line != edited->line ||
      base->column != edited->column ||
      !identity_text_eq(base->path, edited->path)) {
    return false;
  }
  if ((identity_text_present(base->name) || identity_text_present(edited->name)) &&
      !identity_text_eq(base->name, edited->name)) {
    return false;
  }
  size_t base_edge_index = identity_owner_edge_index(context, context->base, base_index);
  size_t edited_edge_index = identity_owner_edge_index(context, context->edited, edited_index);
  if (base_edge_index == identity_missing() && edited_edge_index == identity_missing()) return true;
  if (base_edge_index == identity_missing() || edited_edge_index == identity_missing()) return false;
  const ZProgramGraphEdge *base_edge = &context->base->edges[base_edge_index];
  const ZProgramGraphEdge *edited_edge = &context->edited->edges[edited_edge_index];
  if (!identity_text_eq(base_edge->kind, edited_edge->kind)) return false;
  size_t base_owner = identity_edge_source_index(context, context->base, base_edge_index);
  size_t edited_owner = identity_edge_source_index(context, context->edited, edited_edge_index);
  if (base_owner == identity_missing() || edited_owner == identity_missing()) return false;
  return context->base_to_edited[base_owner] == edited_owner;
}

static bool identity_symbol_candidate(IdentityContext *context, size_t base_index, size_t edited_index) {
  const ZProgramGraphNode *base = &context->base->nodes[base_index];
  const ZProgramGraphNode *edited = &context->edited->nodes[edited_index];
  return base->kind == edited->kind &&
         identity_text_present(base->symbol_id) &&
         identity_text_eq(base->symbol_id, edited->symbol_id);
}

static bool identity_relaxed_node_key_eq(const ZProgramGraphNode *base, const ZProgramGraphNode *edited) {
  if (!base || !edited || base->kind != edited->kind) return false;
  if (identity_text_present(base->name) || identity_text_present(edited->name)) return identity_text_eq(base->name, edited->name);
  if (identity_text_present(base->value) || identity_text_present(edited->value)) return identity_text_eq(base->value, edited->value);
  if (identity_text_present(base->type) || identity_text_present(edited->type)) return identity_text_eq(base->type, edited->type);
  return true;
}

static bool identity_owner_child_candidate(IdentityContext *context, size_t base_index, size_t edited_index) {
  size_t base_edge_index = identity_owner_edge_index(context, context->base, base_index);
  size_t edited_edge_index = identity_owner_edge_index(context, context->edited, edited_index);
  if (base_edge_index == identity_missing() || edited_edge_index == identity_missing()) return false;
  const ZProgramGraphEdge *base_edge = &context->base->edges[base_edge_index];
  const ZProgramGraphEdge *edited_edge = &context->edited->edges[edited_edge_index];
  if (!identity_text_eq(base_edge->kind, edited_edge->kind) || base_edge->order != edited_edge->order) return false;
  size_t base_owner = identity_edge_source_index(context, context->base, base_edge_index);
  size_t edited_owner = identity_edge_source_index(context, context->edited, edited_edge_index);
  if (base_owner == identity_missing() || edited_owner == identity_missing()) return false;
  if (context->base_to_edited[base_owner] != edited_owner) return false;
  return identity_relaxed_node_key_eq(&context->base->nodes[base_index], &context->edited->nodes[edited_index]);
}

static bool identity_base_id_candidate(IdentityContext *context, size_t base_index, size_t edited_index) {
  const ZProgramGraphNode *base = &context->base->nodes[base_index];
  const ZProgramGraphNode *edited = &context->edited->nodes[edited_index];
  if (base->kind != edited->kind) return false;
  if ((base->kind == Z_PROGRAM_GRAPH_NODE_IMPORT || base->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT) && (identity_text_present(base->name) || identity_text_present(edited->name)) && !identity_text_eq(base->name, edited->name)) return false;
  return base->id && edited->id && identity_text_eq(context->base_id_bases[base_index], context->edited_id_bases[edited_index]);
}

static size_t identity_count_edited_base_id_candidates(IdentityContext *context, const char *base_id, size_t *first) {
  char base[80];
  if (first) *first = identity_missing();
  if (!context || !identity_id_base(base_id, base, sizeof(base))) return 0;
  size_t run_start = 0;
  size_t run_len = 0;
  z_program_graph_id_index_run(context->edited_id_base_index, context->edited->node_len, base, &run_start, &run_len);
  size_t count = 0;
  for (size_t i = 0; i < run_len; i++) {
    size_t edited_index = context->edited_id_base_index[run_start + i].node_index;
    if (context->edited_matched[edited_index] || identity_edited_excluded(context, edited_index) || !context->edited->nodes[edited_index].id) continue;
    if (first && count == 0) *first = edited_index;
    count++;
  }
  return count;
}

static bool identity_allows_import_name_disambiguated_base_collision(const ZProgramGraphNode *node) {
  return node &&
         identity_text_present(node->name) &&
         (node->kind == Z_PROGRAM_GRAPH_NODE_IMPORT || node->kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT);
}

static void identity_reject_ambiguous_missing_base_ids(IdentityContext *context) {
  for (size_t i = 0; context && context->result.ok && i < context->base->node_len; i++) {
    if (context->base_to_edited[i] != identity_missing() || identity_base_excluded(context, i)) continue;
    if (identity_allows_import_name_disambiguated_base_collision(&context->base->nodes[i])) continue;
    size_t first = identity_missing();
    size_t count = identity_count_edited_base_id_candidates(context, context->base->nodes[i].id, &first);
    if (count <= 1) continue;
    char base_id[80];
    size_t *candidates = z_checked_calloc(count, sizeof(size_t));
    size_t at = 0;
    if (identity_id_base(context->base->nodes[i].id, base_id, sizeof(base_id))) {
      size_t run_start = 0;
      size_t run_len = 0;
      z_program_graph_id_index_run(context->edited_id_base_index, context->edited->node_len, base_id, &run_start, &run_len);
      for (size_t j = 0; j < run_len && at < count; j++) {
        size_t edited_index = context->edited_id_base_index[run_start + j].node_index;
        if (context->edited_matched[edited_index] || identity_edited_excluded(context, edited_index) || !context->edited->nodes[edited_index].id) continue;
        candidates[at++] = edited_index;
      }
    }
    if (at == 0 && first != identity_missing()) candidates[at++] = first;
    size_t chosen = identity_resolve_best_candidate(context, i, candidates, at);
    if (chosen != identity_missing() && identity_resolve_reverse_unique(context, identity_base_id_candidate, i, chosen)) {
      free(candidates);
      if (!identity_match_nodes(context, i, chosen)) return;
      context->result.auto_resolved++;
      continue;
    }
    identity_fail_nodes(context, "GRC001", "source edit has ambiguous graph identity", i, candidates, at);
    free(candidates);
    return;
  }
}

static size_t identity_similarity_score(IdentityContext *context, size_t base_index, size_t edited_index) {
  if (!context || base_index >= context->base->node_len || edited_index >= context->edited->node_len) return 0;
  const ZProgramGraphNode *base = &context->base->nodes[base_index];
  const ZProgramGraphNode *edited = &context->edited->nodes[edited_index];
  if (base->kind != edited->kind) return 0;
  if (identity_subtree_fingerprint(context, context->base, base_index, 0) ==
      identity_subtree_fingerprint(context, context->edited, edited_index, 0)) {
    return (size_t)1 << 20;
  }
  size_t score = 0;
  size_t base_edge_index = identity_owner_edge_index(context, context->base, base_index);
  size_t edited_edge_index = identity_owner_edge_index(context, context->edited, edited_index);
  if (base_edge_index != identity_missing() && edited_edge_index != identity_missing()) {
    const ZProgramGraphEdge *base_edge = &context->base->edges[base_edge_index];
    const ZProgramGraphEdge *edited_edge = &context->edited->edges[edited_edge_index];
    if (identity_text_eq(base_edge->kind, edited_edge->kind)) {
      size_t base_owner = identity_edge_source_index(context, context->base, base_edge_index);
      size_t edited_owner = identity_edge_source_index(context, context->edited, edited_edge_index);
      if (base_owner != identity_missing() && context->base_to_edited[base_owner] == edited_owner) {
        score += 64;
        if (base_edge->order == edited_edge->order) score += 4;
      }
    }
  }
  if (identity_text_present(base->name) && identity_text_eq(base->name, edited->name)) score += 32;
  if (identity_text_present(base->value) && identity_text_eq(base->value, edited->value)) score += 16;
  if (identity_text_present(base->type) && identity_text_eq(base->type, edited->type)) score += 8;
  size_t base_start = context->base_child_start[base_index];
  size_t base_end = context->base_child_start[base_index + 1];
  size_t edited_start = context->edited_child_start[edited_index];
  size_t edited_end = context->edited_child_start[edited_index + 1];
  size_t edited_count = edited_end - edited_start;
  bool *used = z_checked_calloc(edited_count ? edited_count : 1, sizeof(bool));
  for (size_t i = base_start; i < base_end; i++) {
    uint64_t base_fp = identity_subtree_fingerprint(context, context->base, context->base_child_nodes[i], 0);
    const char *base_kind = context->base->edges[context->base_child_edges[i]].kind;
    for (size_t j = 0; j < edited_count; j++) {
      if (used[j]) continue;
      if (!identity_text_eq(base_kind, context->edited->edges[context->edited_child_edges[edited_start + j]].kind)) continue;
      if (identity_subtree_fingerprint(context, context->edited, context->edited_child_nodes[edited_start + j], 0) != base_fp) continue;
      used[j] = true;
      score += 24;
      break;
    }
  }
  free(used);
  return score;
}

static size_t identity_resolve_best_candidate(IdentityContext *context, size_t base_index, const size_t *candidates, size_t count) {
  size_t best = identity_missing();
  size_t best_score = 0;
  bool tie = false;
  for (size_t i = 0; i < count; i++) {
    size_t score = identity_similarity_score(context, base_index, candidates[i]);
    if (score > best_score) {
      best = candidates[i];
      best_score = score;
      tie = false;
    } else if (score == best_score && best != identity_missing()) {
      tie = true;
    }
  }
  return tie || best_score == 0 ? identity_missing() : best;
}

static bool identity_resolve_reverse_unique(IdentityContext *context, IdentityCandidateFn candidate, size_t base_index, size_t edited_index) {
  size_t best = identity_missing();
  size_t best_score = 0;
  bool tie = false;
  for (size_t i = 0; context && i < context->base->node_len; i++) {
    if (context->base_to_edited[i] != identity_missing() || identity_base_excluded(context, i) || !candidate(context, i, edited_index)) continue;
    size_t score = identity_similarity_score(context, i, edited_index);
    if (score > best_score) {
      best = i;
      best_score = score;
      tie = false;
    } else if (score == best_score && best != identity_missing()) {
      tie = true;
    }
  }
  return !tie && best_score > 0 && best == base_index;
}

static size_t identity_count_base_candidates(IdentityContext *context, IdentityCandidateFn candidate, size_t edited_index, size_t *first) {
  size_t count = 0;
  if (first) *first = identity_missing();
  for (size_t i = 0; context && i < context->base->node_len; i++) {
    if (context->base_to_edited[i] != identity_missing() || identity_base_excluded(context, i) || !candidate(context, i, edited_index)) continue;
    if (first && count == 0) *first = i;
    count++;
  }
  return count;
}

static void identity_apply_unique_pass_opt(IdentityContext *context, IdentityCandidateFn candidate, bool fail_on_ambiguous) {
  for (size_t i = 0; context && context->result.ok && i < context->base->node_len; i++) {
    if (context->base_to_edited[i] != identity_missing() || identity_base_excluded(context, i)) continue;
    size_t first = identity_missing();
    size_t count = 0;
    for (size_t j = 0; j < context->edited->node_len; j++) {
      if (context->edited_matched[j] || identity_edited_excluded(context, j) || !candidate(context, i, j)) continue;
      if (count == 0) first = j;
      count++;
    }
    if (count == 0) continue;
    if (count > 1) {
      if (!fail_on_ambiguous) continue;
      size_t *candidates = z_checked_calloc(count, sizeof(size_t));
      size_t at = 0;
      for (size_t j = 0; j < context->edited->node_len && at < count; j++) {
        if (context->edited_matched[j] || identity_edited_excluded(context, j) || !candidate(context, i, j)) continue;
        candidates[at++] = j;
      }
      size_t chosen = identity_resolve_best_candidate(context, i, candidates, at);
      free(candidates);
      if (chosen != identity_missing() && identity_resolve_reverse_unique(context, candidate, i, chosen)) {
        if (!identity_match_nodes(context, i, chosen)) return;
        context->result.auto_resolved++;
        continue;
      }
      candidates = z_checked_calloc(count, sizeof(size_t));
      at = 0;
      for (size_t j = 0; j < context->edited->node_len && at < count; j++) {
        if (context->edited_matched[j] || identity_edited_excluded(context, j) || !candidate(context, i, j)) continue;
        candidates[at++] = j;
      }
      identity_fail_nodes(context, "GRC001", "source edit has ambiguous graph identity", i, candidates, at);
      free(candidates);
      return;
    }
    size_t reverse_first = identity_missing();
    size_t reverse_count = identity_count_base_candidates(context, candidate, first, &reverse_first);
    if (reverse_count != 1 || reverse_first != i) {
      if (!fail_on_ambiguous) continue;
      if (identity_resolve_reverse_unique(context, candidate, i, first)) {
        if (!identity_match_nodes(context, i, first)) return;
        context->result.auto_resolved++;
        continue;
      }
      identity_fail_nodes(context, "GRC001", "source edit has ambiguous graph identity", i, &first, 1);
      return;
    }
    identity_match_nodes(context, i, first);
  }
}

static void identity_apply_unique_pass(IdentityContext *context, IdentityCandidateFn candidate) {
  identity_apply_unique_pass_opt(context, candidate, true);
}

static bool identity_matched_payload_changed(IdentityContext *context, size_t base_index) {
  if (!context || base_index >= context->base->node_len) return false;
  size_t edited_index = context->base_to_edited[base_index];
  if (edited_index == identity_missing()) return false;
  return !identity_node_payload_eq(&context->base->nodes[base_index], &context->edited->nodes[edited_index]);
}

static bool identity_same_ordered_payload_group(IdentityContext *context, size_t left_base_index, size_t right_base_index) {
  if (!context ||
      left_base_index >= context->base->node_len ||
      right_base_index >= context->base->node_len) {
    return false;
  }
  size_t left_edited_index = context->base_to_edited[left_base_index];
  size_t right_edited_index = context->base_to_edited[right_base_index];
  if (left_edited_index == identity_missing() || right_edited_index == identity_missing()) return false;
  const ZProgramGraphNode *left_base = &context->base->nodes[left_base_index];
  const ZProgramGraphNode *right_base = &context->base->nodes[right_base_index];
  if (left_base->kind != right_base->kind) return false;
  size_t left_base_edge_index = identity_owner_edge_index(context, context->base, left_base_index);
  size_t right_base_edge_index = identity_owner_edge_index(context, context->base, right_base_index);
  size_t left_edited_edge_index = identity_owner_edge_index(context, context->edited, left_edited_index);
  size_t right_edited_edge_index = identity_owner_edge_index(context, context->edited, right_edited_index);
  if (left_base_edge_index == identity_missing() ||
      right_base_edge_index == identity_missing() ||
      left_edited_edge_index == identity_missing() ||
      right_edited_edge_index == identity_missing()) {
    return false;
  }
  const ZProgramGraphEdge *left_base_edge = &context->base->edges[left_base_edge_index];
  const ZProgramGraphEdge *right_base_edge = &context->base->edges[right_base_edge_index];
  const ZProgramGraphEdge *left_edited_edge = &context->edited->edges[left_edited_edge_index];
  const ZProgramGraphEdge *right_edited_edge = &context->edited->edges[right_edited_edge_index];
  return identity_text_eq(left_base_edge->from, right_base_edge->from) &&
         identity_text_eq(left_base_edge->kind, right_base_edge->kind) &&
         identity_text_eq(left_edited_edge->from, right_edited_edge->from) &&
         identity_text_eq(left_edited_edge->kind, right_edited_edge->kind);
}

static bool identity_edited_payload_matches_base(IdentityContext *context, size_t edited_owner_base_index, size_t payload_base_index) {
  if (!context ||
      edited_owner_base_index >= context->base->node_len ||
      payload_base_index >= context->base->node_len) {
    return false;
  }
  size_t edited_index = context->base_to_edited[edited_owner_base_index];
  if (edited_index == identity_missing()) return false;
  return identity_node_payload_eq(&context->edited->nodes[edited_index], &context->base->nodes[payload_base_index]);
}

static bool identity_payload_cycle_from(IdentityContext *context, size_t start_base_index, size_t current_base_index, bool *visited, size_t depth) {
  if (!context || !visited || depth > context->base->node_len) return false;
  visited[current_base_index] = true;
  for (size_t next = 0; next < context->base->node_len; next++) {
    if (!identity_matched_payload_changed(context, next)) continue;
    if (!identity_same_ordered_payload_group(context, start_base_index, next)) continue;
    if (!identity_edited_payload_matches_base(context, current_base_index, next)) continue;
    if (next == start_base_index) return true;
    if (!visited[next] && identity_payload_cycle_from(context, start_base_index, next, visited, depth + 1)) return true;
  }
  return false;
}

static void identity_detect_ordered_payload_permutation(IdentityContext *context) {
  bool *visited = z_checked_calloc(context && context->base->node_len ? context->base->node_len : 1, sizeof(bool));
  for (size_t i = 0; context && context->result.ok && i < context->base->node_len; i++) {
    if (!identity_matched_payload_changed(context, i)) continue;
    memset(visited, 0, context->base->node_len * sizeof(bool));
    if (identity_payload_cycle_from(context, i, i, visited, 0)) {
      size_t edited_i = context->base_to_edited[i];
      identity_fail(context,
                    "GRC001",
                    "source edit has ambiguous graph identity",
                    context->base->nodes[i].id,
                    edited_i == identity_missing() ? "" : context->edited->nodes[edited_i].id);
      break;
    }
  }
  free(visited);
}

static bool identity_assignments_unique(IdentityContext *context) {
  size_t len = context ? context->edited->node_len : 0;
  if (len == 0) return true;
  const char **ids = z_checked_calloc(len, sizeof(const char *));
  for (size_t i = 0; i < len; i++) ids[i] = context->edited->nodes[i].id;
  ZProgramGraphAdjacencyNodeEntry *sorted = z_program_graph_id_index_build(ids, len);
  bool unique = true;
  for (size_t i = 1; unique && i < len; i++) {
    if (!identity_text_eq(sorted[i - 1].id, sorted[i].id)) continue;
    identity_fail(context, "GRC001", "source edit has ambiguous graph identity", sorted[i - 1].id, sorted[i].id);
    unique = false;
  }
  free(sorted);
  free(ids);
  return unique;
}

static void identity_rename_colliding_inserted_ids(IdentityContext *context) {
  ZProgramGraphIdSet used;
  z_program_graph_id_set_init(&used, context->edited->node_len);
  for (size_t i = 0; i < context->edited->node_len; i++) {
    if (context->edited_matched[i]) z_program_graph_id_set_add(&used, context->edited->nodes[i].id);
  }
  bool *needs_rename = z_checked_calloc(context->edited->node_len ? context->edited->node_len : 1, sizeof(bool));
  for (size_t i = 0; i < context->edited->node_len; i++) {
    if (context->edited_matched[i]) continue;
    if (z_program_graph_id_set_has(&used, context->edited->nodes[i].id)) needs_rename[i] = true;
    else z_program_graph_id_set_add(&used, context->edited->nodes[i].id);
  }
  for (size_t i = 0; i < context->edited->node_len; i++) {
    if (!needs_rename[i]) continue;
    char base_id[80];
    if (!identity_id_base(context->edited->nodes[i].id, base_id, sizeof(base_id))) continue;
    char *renamed = z_program_graph_source_node_collision_id(&context->edited->nodes[i], base_id, &used, false);
    z_program_graph_id_set_add(&used, renamed);
    free(context->edited->nodes[i].id);
    context->edited->nodes[i].id = renamed;
  }
  free(needs_rename);
  z_program_graph_id_set_free(&used);
}

static void identity_apply_ids(IdentityContext *context) {
  char **old_ids = z_checked_calloc(context->edited->node_len ? context->edited->node_len : 1, sizeof(char *));
  for (size_t i = 0; i < context->edited->node_len; i++) old_ids[i] = z_strdup(context->edited->nodes[i].id ? context->edited->nodes[i].id : "");
  ZProgramGraphAdjacencyNodeEntry *old_id_index = z_program_graph_id_index_build((const char *const *)old_ids, context->edited->node_len);

  for (size_t i = 0; i < context->base->node_len; i++) {
    size_t edited_index = context->base_to_edited[i];
    if (edited_index == identity_missing()) continue;
    free(context->edited->nodes[edited_index].id);
    context->edited->nodes[edited_index].id = z_strdup(context->base->nodes[i].id ? context->base->nodes[i].id : "");
  }
  identity_rename_colliding_inserted_ids(context);
  if (!identity_assignments_unique(context)) {
    for (size_t i = 0; i < context->edited->node_len; i++) free(old_ids[i]);
    free(old_id_index);
    free(old_ids);
    return;
  }
  for (size_t i = 0; i < context->edited->edge_len; i++) {
    ZProgramGraphEdge *edge = &context->edited->edges[i];
    size_t from = z_program_graph_id_index_find(old_id_index, context->edited->node_len, edge->from);
    if (from != identity_missing()) {
      free(edge->from);
      edge->from = z_strdup(context->edited->nodes[from].id);
    }
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE) continue;
    size_t to = z_program_graph_id_index_find(old_id_index, context->edited->node_len, edge->to);
    if (to != identity_missing()) {
      free(edge->to);
      edge->to = z_strdup(context->edited->nodes[to].id);
    }
  }
  for (size_t i = 0; i < context->edited->node_len; i++) free(old_ids[i]);
  free(old_id_index);
  free(old_ids);
  z_program_graph_finalize_identities(context->edited);
}

static bool identity_preserve_source_node_ids(const ZProgramGraph *base, ZProgramGraph *edited, const char *exclude_path, ZProgramGraphIdentityReconcile *out) {
  if (out) *out = (ZProgramGraphIdentityReconcile){.ok = true};
  if (!base || !edited) return true;
  IdentityContext context = {
    .base = base,
    .edited = edited,
    .base_to_edited = z_checked_calloc(base->node_len ? base->node_len : 1, sizeof(size_t)),
    .edited_matched = z_checked_calloc(edited->node_len ? edited->node_len : 1, sizeof(bool)),
    .exclude_path = exclude_path && exclude_path[0] ? exclude_path : NULL,
    .result = {.ok = true},
  };
  for (size_t i = 0; i < base->node_len; i++) context.base_to_edited[i] = identity_missing();
  const char **base_node_ids = z_checked_calloc(base->node_len ? base->node_len : 1, sizeof(const char *));
  const char **edited_node_ids = z_checked_calloc(edited->node_len ? edited->node_len : 1, sizeof(const char *));
  for (size_t i = 0; i < base->node_len; i++) base_node_ids[i] = base->nodes[i].id;
  for (size_t i = 0; i < edited->node_len; i++) edited_node_ids[i] = edited->nodes[i].id;
  context.base_id_index = z_program_graph_id_index_build(base_node_ids, base->node_len);
  context.edited_id_index = z_program_graph_id_index_build(edited_node_ids, edited->node_len);
  free(base_node_ids);
  free(edited_node_ids);
  context.base_owner_edge = identity_build_owner_edges(&context, base);
  context.edited_owner_edge = identity_build_owner_edges(&context, edited);
  context.base_edge_source = identity_build_edge_sources(&context, base);
  context.edited_edge_source = identity_build_edge_sources(&context, edited);
  identity_build_child_index(&context, base, &context.base_child_start, &context.base_child_edges, &context.base_child_nodes);
  identity_build_child_index(&context, edited, &context.edited_child_start, &context.edited_child_edges, &context.edited_child_nodes);
  context.base_fingerprints = z_checked_calloc(base->node_len ? base->node_len : 1, sizeof(uint64_t));
  context.base_fingerprints_done = z_checked_calloc(base->node_len ? base->node_len : 1, sizeof(bool));
  context.edited_fingerprints = z_checked_calloc(edited->node_len ? edited->node_len : 1, sizeof(uint64_t));
  context.edited_fingerprints_done = z_checked_calloc(edited->node_len ? edited->node_len : 1, sizeof(bool));
  context.base_id_bases = z_checked_calloc(base->node_len ? base->node_len : 1, sizeof(IdentityIdBase));
  context.edited_id_bases = z_checked_calloc(edited->node_len ? edited->node_len : 1, sizeof(IdentityIdBase));
  context.edited_id_base_ptrs = z_checked_calloc(edited->node_len ? edited->node_len : 1, sizeof(const char *));
  for (size_t i = 0; i < base->node_len; i++) {
    if (!identity_id_base(base->nodes[i].id, context.base_id_bases[i], sizeof(IdentityIdBase))) context.base_id_bases[i][0] = 0;
  }
  for (size_t i = 0; i < edited->node_len; i++) {
    if (!identity_id_base(edited->nodes[i].id, context.edited_id_bases[i], sizeof(IdentityIdBase))) context.edited_id_bases[i][0] = 0;
    context.edited_id_base_ptrs[i] = context.edited_id_bases[i];
  }
  context.edited_id_base_index = z_program_graph_id_index_build(context.edited_id_base_ptrs, edited->node_len);
  if (!identity_text_eq(base->module_identity, edited->module_identity)) {
    identity_fail(&context, "GRC003", "edited source has a different module identity", base->module_identity, edited->module_identity);
  }
  identity_match_exact_ids(&context);
  identity_apply_unique_pass(&context, identity_source_anchor_candidate);
  identity_apply_unique_pass(&context, identity_symbol_candidate);
  identity_apply_unique_pass(&context, identity_owner_child_candidate);
  identity_apply_unique_pass_opt(&context, identity_base_id_candidate, false);
  identity_align_statement_sequences(&context);
  identity_apply_unique_pass(&context, identity_base_id_candidate);
  identity_reject_ambiguous_missing_base_ids(&context);
  identity_detect_ordered_payload_permutation(&context);
  if (context.result.ok) {
    for (size_t i = 0; i < base->node_len; i++) {
      if (context.base_to_edited[i] == identity_missing()) context.result.deleted++;
      else context.result.preserved++;
    }
    for (size_t i = 0; i < edited->node_len; i++) {
      if (!context.edited_matched[i]) context.result.inserted++;
    }
    identity_apply_ids(&context);
  }
  if (out) *out = context.result;
  bool ok = context.result.ok;
  free(context.edited_id_base_index);
  free(context.edited_id_base_ptrs);
  free(context.edited_id_bases);
  free(context.base_id_bases);
  free(context.edited_fingerprints_done);
  free(context.edited_fingerprints);
  free(context.base_fingerprints_done);
  free(context.base_fingerprints);
  free(context.edited_child_nodes);
  free(context.edited_child_edges);
  free(context.edited_child_start);
  free(context.base_child_nodes);
  free(context.base_child_edges);
  free(context.base_child_start);
  free(context.edited_edge_source);
  free(context.base_edge_source);
  free(context.edited_owner_edge);
  free(context.base_owner_edge);
  free(context.edited_id_index);
  free(context.base_id_index);
  free(context.edited_matched);
  free(context.base_to_edited);
  return ok;
}

bool z_program_graph_preserve_source_node_ids(const ZProgramGraph *base, ZProgramGraph *edited, ZProgramGraphIdentityReconcile *out) {
  return identity_preserve_source_node_ids(base, edited, NULL, out);
}

/* Accepts a whole-file rewrite: nodes on exclude_path are reconciled as
 * delete-plus-insert (fresh identities) while every other file's identities
 * are preserved exactly as in the unrestricted pass. */
bool z_program_graph_preserve_source_node_ids_excluding_path(const ZProgramGraph *base, ZProgramGraph *edited, const char *exclude_path, ZProgramGraphIdentityReconcile *out) {
  return identity_preserve_source_node_ids(base, edited, exclude_path, out);
}
