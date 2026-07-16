#include "program_graph_reconcile.h"

#include "program_graph_adjacency.h"
#include "program_graph_source_map.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool reconcile_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static bool reconcile_module_identity_changed(const ZProgramGraph *base, const ZProgramGraph *edited) {
  return !reconcile_text_eq(base ? base->module_identity : NULL, edited ? edited->module_identity : NULL);
}

bool z_program_graph_identity_refresh_compatible(const char *store_identity, const char *source_identity) {
  if (!store_identity || !source_identity || strncmp(source_identity, "package:", 8) != 0) return false;
  if (strncmp(store_identity, "module:", 7) == 0) return true;
  if (strncmp(store_identity, "package:", 8) != 0) return false;
  const char *left = store_identity + 8;
  const char *right = source_identity + 8;
  while (*left && *right && *left != '@' && *right != '@' && *left == *right) { left++; right++; }
  return (*left == 0 || *left == '@') && (*right == 0 || *right == '@');
}

static void reconcile_json_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const unsigned char *cursor = (const unsigned char *)(value ? value : ""); *cursor; cursor++) {
    unsigned char ch = *cursor;
    switch (ch) {
      case '"': zbuf_append(buf, "\\\""); break;
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (ch < 0x20) zbuf_appendf(buf, "\\u%04x", (unsigned)ch);
        else zbuf_append_char(buf, (char)ch);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static void reconcile_patch_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const char *cursor = value ? value : ""; *cursor; cursor++) {
    switch (*cursor) {
      case '"': zbuf_append(buf, "\\\""); break;
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default: zbuf_append_char(buf, *cursor); break;
    }
  }
  zbuf_append_char(buf, '"');
}

static bool reconcile_id_base(const char *id, char *out, size_t out_len) {
  if (!id || !out || out_len == 0) return false;
  const char *underscore = strchr(id, '_');
  if (!underscore) {
    snprintf(out, out_len, "%s", id);
    return true;
  }
  const char *cursor = underscore + 1;
  for (int i = 0; i < 8; i++) {
    if (!isxdigit((unsigned char)cursor[i])) {
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

typedef char ReconcileIdBase[80];

typedef struct {
  const ZProgramGraph *base;
  const ZProgramGraph *edited;
  ZProgramGraphAdjacencyNodeEntry *base_id_index;
  ZProgramGraphAdjacencyNodeEntry *edited_id_index;
  ReconcileIdBase *edited_id_bases;
  const char **edited_id_base_ptrs;
  ZProgramGraphAdjacencyNodeEntry *edited_id_base_index;
  ReconcileIdBase *missing_id_bases;
  const char **missing_id_base_ptrs;
  size_t missing_len;
  ZProgramGraphAdjacencyNodeEntry *missing_id_base_index;
} ReconcileIndex;

static ZProgramGraphAdjacencyNodeEntry *reconcile_build_graph_id_index(const ZProgramGraph *graph) {
  size_t len = graph ? graph->node_len : 0;
  const char **ids = z_checked_calloc(len ? len : 1, sizeof(const char *));
  for (size_t i = 0; i < len; i++) ids[i] = graph->nodes[i].id;
  ZProgramGraphAdjacencyNodeEntry *index = z_program_graph_id_index_build(ids, len);
  free(ids);
  return index;
}

static void reconcile_index_init(ReconcileIndex *index, const ZProgramGraph *base, const ZProgramGraph *edited) {
  size_t base_len = base ? base->node_len : 0;
  size_t edited_len = edited ? edited->node_len : 0;
  *index = (ReconcileIndex){.base = base, .edited = edited};
  index->base_id_index = reconcile_build_graph_id_index(base);
  index->edited_id_index = reconcile_build_graph_id_index(edited);
  index->edited_id_bases = z_checked_calloc(edited_len ? edited_len : 1, sizeof(ReconcileIdBase));
  index->edited_id_base_ptrs = z_checked_calloc(edited_len ? edited_len : 1, sizeof(const char *));
  for (size_t i = 0; i < edited_len; i++) {
    if (!reconcile_id_base(edited->nodes[i].id, index->edited_id_bases[i], sizeof(ReconcileIdBase))) index->edited_id_bases[i][0] = 0;
    index->edited_id_base_ptrs[i] = index->edited_id_bases[i];
  }
  index->edited_id_base_index = z_program_graph_id_index_build(index->edited_id_base_ptrs, edited_len);
  index->missing_id_bases = z_checked_calloc(base_len ? base_len : 1, sizeof(ReconcileIdBase));
  index->missing_id_base_ptrs = z_checked_calloc(base_len ? base_len : 1, sizeof(const char *));
  for (size_t i = 0; i < base_len; i++) {
    if (z_program_graph_id_index_find(index->edited_id_index, edited_len, base->nodes[i].id) != SIZE_MAX) continue;
    if (!reconcile_id_base(base->nodes[i].id, index->missing_id_bases[index->missing_len], sizeof(ReconcileIdBase))) continue;
    index->missing_id_base_ptrs[index->missing_len] = index->missing_id_bases[index->missing_len];
    index->missing_len++;
  }
  index->missing_id_base_index = z_program_graph_id_index_build(index->missing_id_base_ptrs, index->missing_len);
}

static void reconcile_index_free(ReconcileIndex *index) {
  if (!index) return;
  free(index->base_id_index);
  free(index->edited_id_index);
  free(index->edited_id_bases);
  free(index->edited_id_base_ptrs);
  free(index->edited_id_base_index);
  free(index->missing_id_bases);
  free(index->missing_id_base_ptrs);
  free(index->missing_id_base_index);
  *index = (ReconcileIndex){0};
}

static const ZProgramGraphNode *reconcile_find_node(const ReconcileIndex *index, const ZProgramGraph *graph, const char *id) {
  if (!index || !graph || !id) return NULL;
  const ZProgramGraphAdjacencyNodeEntry *entries = graph == index->base ? index->base_id_index : index->edited_id_index;
  size_t found = z_program_graph_id_index_find(entries, graph->node_len, id);
  return found == SIZE_MAX ? NULL : &graph->nodes[found];
}

static bool reconcile_node_hash_eq(const ZProgramGraphNode *left, const ZProgramGraphNode *right) {
  return reconcile_text_eq(left ? left->node_hash : NULL, right ? right->node_hash : NULL);
}

static size_t reconcile_count_base_candidates(const ReconcileIndex *index, const char *base_id, const ZProgramGraphNode **first) {
  char base[80];
  if (first) *first = NULL;
  if (!index || !index->edited || !base_id) return 0;
  reconcile_id_base(base_id, base, sizeof(base));
  size_t run_start = 0;
  size_t run_len = 0;
  z_program_graph_id_index_run(index->edited_id_base_index, index->edited->node_len, base, &run_start, &run_len);
  if (first && run_len > 0) *first = &index->edited->nodes[index->edited_id_base_index[run_start].node_index];
  return run_len;
}

static bool reconcile_is_candidate_for_missing_base(const ReconcileIndex *index, size_t edited_index) {
  if (!index || !index->base || !index->edited || edited_index >= index->edited->node_len) return false;
  const ZProgramGraphNode *edited_node = &index->edited->nodes[edited_index];
  if (!edited_node->id) return false;
  if (z_program_graph_id_index_find(index->base_id_index, index->base->node_len, edited_node->id) != SIZE_MAX) return false;
  return z_program_graph_id_index_find(index->missing_id_base_index, index->missing_len, index->edited_id_bases[edited_index]) != SIZE_MAX;
}

static bool reconcile_same_field(const char *left, const char *right) {
  return reconcile_text_eq(left, right);
}

static bool reconcile_simple_text_patch_field(const ZProgramGraphNode *base, const ZProgramGraphNode *edited, const char **field) {
  if (!base || !edited || base->kind != edited->kind) return false;
  int changed = 0;
  const char *changed_field = NULL;
  if (!reconcile_same_field(base->name, edited->name)) {
    changed++;
    changed_field = "name";
  }
  if (!reconcile_same_field(base->type, edited->type)) {
    changed++;
    changed_field = "type";
  }
  if (!reconcile_same_field(base->value, edited->value)) {
    changed++;
    changed_field = "value";
  }
  if (base->is_public != edited->is_public || base->is_mutable != edited->is_mutable || base->is_static != edited->is_static ||
      base->fallible != edited->fallible || base->export_c != edited->export_c) {
    changed++;
  }
  if (changed != 1 || !changed_field) return false;
  if (field) *field = changed_field;
  return true;
}

static const char *reconcile_field_value(const ZProgramGraphNode *node, const char *field) {
  if (reconcile_text_eq(field, "name")) return node ? node->name : "";
  if (reconcile_text_eq(field, "type")) return node ? node->type : "";
  if (reconcile_text_eq(field, "value")) return node ? node->value : "";
  return "";
}

static bool reconcile_append_patch_text(ZBuf *buf, const ReconcileIndex *index, const ZProgramGraph *base, const ZProgramGraph *edited) {
  bool wrote_edit = false;
  zbuf_append(buf, "zero-program-graph-patch v1\n");
  zbuf_append(buf, "expect graphHash ");
  reconcile_patch_string(buf, base ? base->graph_hash : "");
  zbuf_append_char(buf, '\n');
  for (size_t i = 0; base && i < base->node_len; i++) {
    const ZProgramGraphNode *before = &base->nodes[i];
    const ZProgramGraphNode *after = reconcile_find_node(index, edited, before->id);
    if (!after || reconcile_node_hash_eq(before, after)) continue;
    const char *field = NULL;
    if (!reconcile_simple_text_patch_field(before, after, &field)) return false;
    zbuf_append(buf, reconcile_text_eq(field, "name") ? "rename node=" : "set node=");
    reconcile_patch_string(buf, before->id);
    if (!reconcile_text_eq(field, "name")) {
      zbuf_append(buf, " field=");
      reconcile_patch_string(buf, field);
    }
    zbuf_append(buf, " expect=");
    reconcile_patch_string(buf, reconcile_field_value(before, field));
    zbuf_append(buf, " value=");
    reconcile_patch_string(buf, reconcile_field_value(after, field));
    zbuf_append_char(buf, '\n');
    wrote_edit = true;
  }
  return wrote_edit;
}

static void reconcile_summary_with_index(const ReconcileIndex *index, const ZProgramGraph *base, const ZProgramGraph *edited, ZProgramGraphReconcileSummary *out) {
  if (!out) return;
  *out = (ZProgramGraphReconcileSummary){.ok = true};
  out->module_identity_changed = reconcile_module_identity_changed(base, edited);
  for (size_t i = 0; base && i < base->node_len; i++) {
    const ZProgramGraphNode *before = &base->nodes[i];
    const ZProgramGraphNode *after = reconcile_find_node(index, edited, before->id);
    if (after) {
      if (reconcile_node_hash_eq(before, after)) out->unchanged++;
      else out->edited++;
      continue;
    }
    const ZProgramGraphNode *first = NULL;
    size_t candidates = reconcile_count_base_candidates(index, before->id, &first);
    (void)first;
    if (candidates == 0) out->deleted++;
    else if (candidates == 1) out->identity_changed++;
    else out->ambiguous++;
  }
  for (size_t i = 0; edited && i < edited->node_len; i++) {
    if (!reconcile_find_node(index, base, edited->nodes[i].id) && !reconcile_is_candidate_for_missing_base(index, i)) out->inserted++;
  }
  out->ok = !out->module_identity_changed && out->ambiguous == 0 && out->identity_changed == 0;
  if (out->ok && out->deleted == 0 && out->inserted == 0 && out->edited > 0) {
    ZBuf patch;
    zbuf_init(&patch);
    out->patch_available = reconcile_append_patch_text(&patch, index, base, edited);
    zbuf_free(&patch);
  }
}

void z_program_graph_reconcile_summary(const ZProgramGraph *base, const ZProgramGraph *edited, ZProgramGraphReconcileSummary *out) {
  ReconcileIndex index;
  reconcile_index_init(&index, base, edited);
  reconcile_summary_with_index(&index, base, edited, out);
  reconcile_index_free(&index);
}

/*
 * Declaration-edit classification for the stale-source refresh note: each
 * function gets a fingerprint over its declared signature (name, ordered
 * parameter names/types/defaults, return type, raises) and each top-level
 * const over its type and value, keyed by name plus projection path tail
 * (store paths are root-relative while freshly built source graphs keep the
 * root prefix) so content-derived node id churn from a retype cannot hide
 * the change. Body-only edits move neither fingerprint.
 */
static const char *reconcile_sig_path_tail(const char *path) {
  const char *text = path ? path : "";
  const char *last = strrchr(text, '/');
  if (!last) return text;
  const char *prev = NULL;
  for (const char *cursor = text; cursor < last; cursor++) {
    if (*cursor == '/') prev = cursor;
  }
  return prev ? prev + 1 : text;
}

static uint64_t reconcile_sig_fold(uint64_t hash, const char *text) {
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    hash ^= *cursor;
    hash *= 1099511628211ull;
  }
  hash ^= 0x1f;
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t reconcile_sig_fold_u64(uint64_t hash, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) {
    hash ^= (value >> (i * 8)) & 0xffu;
    hash *= 1099511628211ull;
  }
  return hash;
}

/*
 * Order-insensitive subtree fingerprint: sibling contributions are summed
 * (each already folds the edge kind and order) so edge array ordering
 * differences between a store round trip and a fresh source build cannot
 * move the hash. Used for const initializer expressions, which live as
 * child subtrees rather than on the Const node itself.
 */
static uint64_t reconcile_sig_subtree_hash(const ZProgramGraph *graph, const ZProgramGraphAdjacencyNodeEntry *id_index, const char *node_id, int depth) {
  uint64_t hash = 1469598103934665603ull;
  if (!graph || !node_id || depth > 16) return hash;
  size_t index = z_program_graph_id_index_find(id_index, graph->node_len, node_id);
  if (index == SIZE_MAX) return hash;
  const ZProgramGraphNode *node = &graph->nodes[index];
  hash = reconcile_sig_fold(hash, z_program_graph_node_kind_name(node->kind));
  hash = reconcile_sig_fold(hash, node->name);
  hash = reconcile_sig_fold(hash, node->type);
  hash = reconcile_sig_fold(hash, node->value);
  uint64_t children = 0;
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !reconcile_text_eq(edge->from, node_id)) continue;
    uint64_t child = reconcile_sig_fold(1469598103934665603ull, edge->kind);
    child = reconcile_sig_fold_u64(child, (uint64_t)edge->order);
    child = reconcile_sig_fold_u64(child, reconcile_sig_subtree_hash(graph, id_index, edge->to, depth + 1));
    children += child;
  }
  return reconcile_sig_fold_u64(hash, children);
}

typedef struct {
  char **keys;
  const char **key_ptrs;
  uint64_t *sigs;
  bool *is_const;
  size_t len;
  ZProgramGraphAdjacencyNodeEntry *key_index;
} ReconcileDeclSigs;

static void reconcile_decl_sigs_build(ReconcileDeclSigs *sigs, const ZProgramGraph *graph) {
  size_t node_len = graph ? graph->node_len : 0;
  *sigs = (ReconcileDeclSigs){0};
  uint64_t *param_acc = z_checked_calloc(node_len ? node_len : 1, sizeof(uint64_t));
  ZProgramGraphAdjacencyNodeEntry *id_index = reconcile_build_graph_id_index(graph);
  for (size_t i = 0; graph && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !reconcile_text_eq(edge->kind, "param")) continue;
    size_t fn_index = z_program_graph_id_index_find(id_index, node_len, edge->from);
    size_t param_index = z_program_graph_id_index_find(id_index, node_len, edge->to);
    if (fn_index == SIZE_MAX || param_index == SIZE_MAX) continue;
    if (graph->nodes[fn_index].kind != Z_PROGRAM_GRAPH_NODE_FUNCTION) continue;
    const ZProgramGraphNode *param = &graph->nodes[param_index];
    if (param->kind != Z_PROGRAM_GRAPH_NODE_PARAM) continue;
    uint64_t param_hash = reconcile_sig_fold_u64(1469598103934665603ull, (uint64_t)edge->order);
    param_hash = reconcile_sig_fold(param_hash, param->name);
    param_hash = reconcile_sig_fold(param_hash, param->type);
    param_hash = reconcile_sig_fold(param_hash, param->value);
    param_acc[fn_index] += param_hash;
  }
  sigs->keys = z_checked_calloc(node_len ? node_len : 1, sizeof(char *));
  sigs->key_ptrs = z_checked_calloc(node_len ? node_len : 1, sizeof(const char *));
  sigs->sigs = z_checked_calloc(node_len ? node_len : 1, sizeof(uint64_t));
  sigs->is_const = z_checked_calloc(node_len ? node_len : 1, sizeof(bool));
  for (size_t i = 0; graph && i < node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    bool is_const = node->kind == Z_PROGRAM_GRAPH_NODE_CONST;
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION && !is_const) continue;
    ZBuf key;
    zbuf_init(&key);
    zbuf_append(&key, is_const ? "const\x1f" : "fn\x1f");
    zbuf_append(&key, reconcile_sig_path_tail(node->path));
    zbuf_append_char(&key, '\x1f');
    zbuf_append(&key, node->name ? node->name : "");
    uint64_t decl_hash = reconcile_sig_fold(1469598103934665603ull, node->type);
    if (is_const) {
      decl_hash = reconcile_sig_fold_u64(decl_hash, reconcile_sig_subtree_hash(graph, id_index, node->id, 0));
    } else {
      decl_hash = reconcile_sig_fold_u64(decl_hash, node->fallible ? 1 : 0);
      decl_hash += param_acc[i];
    }
    sigs->keys[sigs->len] = key.data;
    sigs->key_ptrs[sigs->len] = key.data;
    sigs->sigs[sigs->len] = decl_hash;
    sigs->is_const[sigs->len] = is_const;
    sigs->len++;
  }
  free(param_acc);
  free(id_index);
  sigs->key_index = z_program_graph_id_index_build(sigs->key_ptrs, sigs->len);
}

static void reconcile_decl_sigs_free(ReconcileDeclSigs *sigs) {
  if (!sigs) return;
  for (size_t i = 0; i < sigs->len; i++) free(sigs->keys[i]);
  free(sigs->keys);
  free(sigs->key_ptrs);
  free(sigs->sigs);
  free(sigs->is_const);
  free(sigs->key_index);
  *sigs = (ReconcileDeclSigs){0};
}

static bool reconcile_decl_sigs_match(const ReconcileDeclSigs *sigs, const char *key, uint64_t sig, bool *found) {
  size_t run_start = 0;
  size_t run_len = 0;
  z_program_graph_id_index_run(sigs->key_index, sigs->len, key, &run_start, &run_len);
  if (found) *found = run_len > 0;
  for (size_t i = 0; i < run_len; i++) {
    if (sigs->sigs[sigs->key_index[run_start + i].node_index] == sig) return true;
  }
  return false;
}

ZProgramGraphReconcileEditKind z_program_graph_reconcile_edit_kind(const ZProgramGraph *base, const ZProgramGraph *edited) {
  ReconcileDeclSigs before;
  ReconcileDeclSigs after;
  reconcile_decl_sigs_build(&before, base);
  reconcile_decl_sigs_build(&after, edited);
  bool signature = false;
  bool const_edit = false;
  for (size_t i = 0; i < before.len && !signature; i++) {
    bool found = false;
    bool matched = reconcile_decl_sigs_match(&after, before.keys[i], before.sigs[i], &found);
    if (matched || !found) continue;
    if (before.is_const[i]) const_edit = true;
    else signature = true;
  }
  reconcile_decl_sigs_free(&before);
  reconcile_decl_sigs_free(&after);
  if (signature) return Z_PROGRAM_GRAPH_RECONCILE_EDIT_SIGNATURE;
  if (const_edit) return Z_PROGRAM_GRAPH_RECONCILE_EDIT_CONST;
  return Z_PROGRAM_GRAPH_RECONCILE_EDIT_OTHER;
}

static void reconcile_append_decision_json(ZBuf *buf,
                                           const char *status,
                                           const ZProgramGraphNode *before,
                                           const ZProgramGraphNode *after,
                                           const ZProgramGraphSourceRangeContext *range_context,
                                           const ZProgramGraph *range_graph,
                                           const char *path) {
  zbuf_append(buf, "{\"status\":");
  reconcile_json_string(buf, status);
  zbuf_append(buf, ",\"nodeId\":");
  reconcile_json_string(buf, before ? before->id : (after ? after->id : ""));
  zbuf_append(buf, ",\"candidateId\":");
  if (after && before && !reconcile_text_eq(before->id, after->id)) reconcile_json_string(buf, after->id);
  else zbuf_append(buf, "null");
  zbuf_append(buf, ",\"kind\":");
  reconcile_json_string(buf, before ? z_program_graph_node_kind_name(before->kind) : (after ? z_program_graph_node_kind_name(after->kind) : ""));
  zbuf_append(buf, ",\"name\":");
  reconcile_json_string(buf, after ? after->name : (before ? before->name : ""));
  zbuf_append(buf, ",\"beforeHash\":");
  reconcile_json_string(buf, before ? before->node_hash : "");
  zbuf_append(buf, ",\"afterHash\":");
  reconcile_json_string(buf, after ? after->node_hash : "");
  zbuf_append(buf, ",\"sourceRange\":");
  z_program_graph_append_source_range_from_context_json(buf, range_context, range_graph, after ? after : before, path);
  zbuf_append(buf, "}");
}

static void reconcile_append_diagnostic_json(ZBuf *buf, const char *code, const ZProgramGraphNode *node, size_t candidates) {
  zbuf_append(buf, "{\"code\":");
  reconcile_json_string(buf, code);
  zbuf_append(buf, ",\"message\":");
  reconcile_json_string(buf, reconcile_text_eq(code, "GRC001") ? "source edit has ambiguous graph identity" : "source edit changed a stable graph identity");
  zbuf_append(buf, ",\"node\":");
  reconcile_json_string(buf, node ? node->id : "");
  zbuf_append(buf, ",\"expected\":");
  reconcile_json_string(buf, reconcile_text_eq(code, "GRC001") ? "one edited node matching the previous graph identity" : "edited source preserves the previous graph node id");
  zbuf_append(buf, ",\"actual\":");
  zbuf_appendf(buf, "\"%zu candidate%s\"", candidates, candidates == 1 ? "" : "s");
  zbuf_append(buf, ",\"help\":");
  reconcile_json_string(buf, "make the edit through zero patch or split the change so identity is unambiguous");
  zbuf_append(buf, "}");
}

static void reconcile_append_module_diagnostic_json(ZBuf *buf, const ZProgramGraph *base, const ZProgramGraph *edited) {
  zbuf_append(buf, "{\"code\":\"GRC003\",\"message\":\"edited source has a different module identity\",\"node\":\"\",\"expected\":");
  reconcile_json_string(buf, base ? base->module_identity : "");
  zbuf_append(buf, ",\"actual\":");
  reconcile_json_string(buf, edited ? edited->module_identity : "");
  zbuf_append(buf, ",\"help\":\"reconcile the original source or package path, or capture a new base graph for the edited module\"}");
}

static void reconcile_append_diagnostics_json(ZBuf *buf, const ReconcileIndex *index, const ZProgramGraph *base, const ZProgramGraph *edited) {
  bool wrote = false;
  zbuf_append(buf, "[");
  if (reconcile_module_identity_changed(base, edited)) {
    reconcile_append_module_diagnostic_json(buf, base, edited);
    wrote = true;
  }
  for (size_t i = 0; base && i < base->node_len; i++) {
    const ZProgramGraphNode *before = &base->nodes[i];
    if (reconcile_find_node(index, edited, before->id)) continue;
    size_t candidates = reconcile_count_base_candidates(index, before->id, NULL);
    if (candidates <= 0) continue;
    if (wrote) zbuf_append(buf, ", ");
    reconcile_append_diagnostic_json(buf, candidates > 1 ? "GRC001" : "GRC002", before, candidates);
    wrote = true;
  }
  zbuf_append(buf, "]");
}

static void reconcile_append_decisions_json(ZBuf *buf, const ReconcileIndex *index, const ZProgramGraph *base, const ZProgramGraph *edited, const char *edited_path) {
  bool wrote = false;
  ZProgramGraphSourceRangeContext base_range_context;
  ZProgramGraphSourceRangeContext edited_range_context;
  z_program_graph_source_range_context_init(&base_range_context, base);
  z_program_graph_source_range_context_init(&edited_range_context, edited);
  zbuf_append(buf, "[");
  for (size_t i = 0; base && i < base->node_len; i++) {
    const ZProgramGraphNode *before = &base->nodes[i];
    const ZProgramGraphNode *after = reconcile_find_node(index, edited, before->id);
    const char *status = "deleted";
    const ZProgramGraphNode *candidate = NULL;
    if (after) status = reconcile_node_hash_eq(before, after) ? "unchanged" : "edited";
    else {
      size_t candidates = reconcile_count_base_candidates(index, before->id, &candidate);
      if (candidates == 1) {
        status = "identity-changed";
        after = candidate;
      } else if (candidates > 1) {
        status = "ambiguous";
      }
    }
    if (wrote) zbuf_append(buf, ", ");
    reconcile_append_decision_json(buf, status, before, after, after ? &edited_range_context : &base_range_context, after ? edited : base, edited_path);
    wrote = true;
  }
  for (size_t i = 0; edited && i < edited->node_len; i++) {
    if (reconcile_find_node(index, base, edited->nodes[i].id) || reconcile_is_candidate_for_missing_base(index, i)) continue;
    if (wrote) zbuf_append(buf, ", ");
    reconcile_append_decision_json(buf, "inserted", NULL, &edited->nodes[i], &edited_range_context, edited, edited_path);
    wrote = true;
  }
  zbuf_append(buf, "]");
  z_program_graph_source_range_context_free(&base_range_context);
  z_program_graph_source_range_context_free(&edited_range_context);
}

void z_program_graph_append_reconcile_json(ZBuf *buf,
                                           const ZProgramGraph *base,
                                           const ZProgramGraph *edited,
                                           const char *base_path,
                                           const char *edited_path,
                                           const ZProgramGraphReconcileSummary *summary) {
  ReconcileIndex index;
  reconcile_index_init(&index, base, edited);
  ZProgramGraphReconcileSummary local = {0};
  if (!summary) {
    reconcile_summary_with_index(&index, base, edited, &local);
    summary = &local;
  }
  ZBuf patch;
  zbuf_init(&patch);
  bool patch_available = summary->patch_available && reconcile_append_patch_text(&patch, &index, base, edited);

  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(buf, summary->ok ? "true" : "false");
  zbuf_append(buf, ",\n  \"base\": {\"path\": ");
  reconcile_json_string(buf, base_path ? base_path : "");
  zbuf_append(buf, ", \"graphHash\": ");
  reconcile_json_string(buf, base ? base->graph_hash : "");
  zbuf_append(buf, ", \"moduleIdentity\": ");
  reconcile_json_string(buf, base ? base->module_identity : "");
  zbuf_append(buf, "},\n  \"edited\": {\"path\": ");
  reconcile_json_string(buf, edited_path ? edited_path : "");
  zbuf_append(buf, ", \"graphHash\": ");
  reconcile_json_string(buf, edited ? edited->graph_hash : "");
  zbuf_append(buf, ", \"moduleIdentity\": ");
  reconcile_json_string(buf, edited ? edited->module_identity : "");
  zbuf_append(buf, "},\n  \"identity\": {");
  zbuf_appendf(buf,
               "\"unchanged\": %zu, \"edited\": %zu, \"inserted\": %zu, \"deleted\": %zu, \"ambiguous\": %zu, \"identityChanged\": %zu, \"moduleIdentityChanged\": %s",
               summary->unchanged,
               summary->edited,
               summary->inserted,
               summary->deleted,
               summary->ambiguous,
               summary->identity_changed,
               summary->module_identity_changed ? "true" : "false");
  zbuf_append(buf, "},\n  \"graphPatch\": {\"available\": ");
  zbuf_append(buf, patch_available ? "true" : "false");
  zbuf_append(buf, ", \"text\": ");
  if (patch_available) reconcile_json_string(buf, patch.data ? patch.data : "");
  else zbuf_append(buf, "null");
  zbuf_append(buf, "},\n  \"decisions\": ");
  reconcile_append_decisions_json(buf, &index, base, edited, edited_path);
  zbuf_append(buf, ",\n  \"diagnostics\": ");
  reconcile_append_diagnostics_json(buf, &index, base, edited);
  zbuf_append(buf, "\n}\n");

  reconcile_index_free(&index);
  zbuf_free(&patch);
}
