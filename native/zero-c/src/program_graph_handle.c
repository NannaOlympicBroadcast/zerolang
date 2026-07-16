#include "program_graph_handle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *handle_segment_of(const char *id) {
  if (!id) return "";
  if (id[0] == '#') id++;
  const char *underscore = strchr(id, '_');
  return underscore ? underscore + 1 : id;
}

static bool handle_starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool handle_segment_matches_alias(const char *segment, const char *head, size_t head_len, const char *tail, size_t tail_len) {
  size_t segment_len = strlen(segment);
  if (segment_len < head_len + tail_len) return false;
  if (strncmp(segment, head, head_len) != 0) return false;
  return memcmp(segment + (segment_len - tail_len), tail, tail_len) == 0;
}

const ZProgramGraphNode *z_program_graph_resolve_handle(const ZProgramGraph *graph, const char *handle, bool *ambiguous) {
  if (ambiguous) *ambiguous = false;
  if (!graph || !handle || handle[0] != '#' || !handle[1]) return NULL;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (graph->nodes[i].id && strcmp(graph->nodes[i].id, handle) == 0) return &graph->nodes[i];
  }
  const char *text = handle + 1;
  const ZProgramGraphNode *exact = NULL;
  size_t exact_count = 0;
  const ZProgramGraphNode *prefixed = NULL;
  size_t prefix_count = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    const char *segment = handle_segment_of(graph->nodes[i].id);
    if (!handle_starts_with(segment, text)) continue;
    prefixed = &graph->nodes[i];
    prefix_count++;
    if (segment[strlen(text)] == '\0') {
      exact = &graph->nodes[i];
      exact_count++;
    }
  }
  if (exact_count == 1) return exact;
  if (exact_count > 1) {
    if (ambiguous) *ambiguous = true;
    return NULL;
  }
  if (prefix_count == 1) return prefixed;
  if (prefix_count > 1) {
    if (ambiguous) *ambiguous = true;
    return NULL;
  }
  /* #head..tail matches the unique segment that starts with head and ends
   * with tail; ids with long shared prefixes shorten this way. */
  const char *dots = strstr(text, "..");
  if (!dots || dots == text) return NULL;
  size_t head_len = (size_t)(dots - text);
  const char *tail = dots + 2;
  size_t tail_len = strlen(tail);
  const ZProgramGraphNode *alias = NULL;
  size_t alias_count = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    const char *segment = handle_segment_of(graph->nodes[i].id);
    if (!handle_segment_matches_alias(segment, text, head_len, tail, tail_len)) continue;
    alias = &graph->nodes[i];
    alias_count++;
  }
  if (alias_count == 1) return alias;
  if (alias_count > 1 && ambiguous) *ambiguous = true;
  return NULL;
}

enum { HANDLE_DISTANCE_MAX = 64 };

static size_t handle_distance(const char *left, const char *right) {
  size_t left_len = strlen(left);
  size_t right_len = strlen(right);
  if (left_len > HANDLE_DISTANCE_MAX || right_len > HANDLE_DISTANCE_MAX) {
    return left_len > right_len ? left_len : right_len;
  }
  size_t row[HANDLE_DISTANCE_MAX + 1];
  for (size_t j = 0; j <= right_len; j++) row[j] = j;
  for (size_t i = 1; i <= left_len; i++) {
    size_t previous_diagonal = row[0];
    row[0] = i;
    for (size_t j = 1; j <= right_len; j++) {
      size_t previous = row[j];
      size_t substitute = previous_diagonal + (left[i - 1] == right[j - 1] ? 0 : 1);
      size_t insert = row[j - 1] + 1;
      size_t delete_cost = previous + 1;
      size_t best = substitute < insert ? substitute : insert;
      row[j] = best < delete_cost ? best : delete_cost;
      previous_diagonal = previous;
    }
  }
  return row[right_len];
}

size_t z_program_graph_handle_distance(const char *left, const char *right) {
  return handle_distance(left ? left : "", right ? right : "");
}

const char *z_program_graph_nearest_handle(const ZProgramGraph *graph, const char *handle) {
  if (!graph || !handle || !handle[0]) return NULL;
  const char *text = handle[0] == '#' ? handle + 1 : handle;
  size_t text_len = strlen(text);
  size_t threshold = text_len / 3 + 2;
  const char *best = NULL;
  size_t best_distance = 0;
  for (size_t i = 0; i < graph->node_len; i++) {
    const char *id = graph->nodes[i].id;
    if (!id || !id[0]) continue;
    const char *bare = id[0] == '#' ? id + 1 : id;
    size_t distance = handle_distance(text, bare);
    const char *segment = handle_segment_of(id);
    size_t segment_distance = handle_distance(text, segment);
    if (segment_distance < distance) distance = segment_distance;
    /* Typo'd short handles compare best against the segment prefix of the
     * same length. */
    size_t segment_len = strlen(segment);
    if (text_len < segment_len && text_len <= HANDLE_DISTANCE_MAX) {
      char prefix[HANDLE_DISTANCE_MAX + 1];
      memcpy(prefix, segment, text_len);
      prefix[text_len] = '\0';
      size_t prefix_distance = handle_distance(text, prefix);
      if (prefix_distance < distance) distance = prefix_distance;
    }
    if (distance > threshold) continue;
    if (!best || distance < best_distance) {
      best = id;
      best_distance = distance;
    }
  }
  return best;
}

static const ZProgramGraph *handle_sort_graph;

static int handle_segment_compare(const void *left, const void *right) {
  size_t left_index = *(const size_t *)left;
  size_t right_index = *(const size_t *)right;
  return strcmp(handle_segment_of(handle_sort_graph->nodes[left_index].id), handle_segment_of(handle_sort_graph->nodes[right_index].id));
}

void z_program_graph_handle_shortener_init(ZProgramGraphHandleShortener *shortener, const ZProgramGraph *graph) {
  if (!shortener) return;
  *shortener = (ZProgramGraphHandleShortener){.graph = graph};
  if (!graph || graph->node_len == 0) return;
  shortener->order = z_checked_reallocarray(NULL, graph->node_len, sizeof(size_t));
  shortener->len = graph->node_len;
  for (size_t i = 0; i < graph->node_len; i++) shortener->order[i] = i;
  handle_sort_graph = graph;
  qsort(shortener->order, shortener->len, sizeof(size_t), handle_segment_compare);
  handle_sort_graph = NULL;
}

void z_program_graph_handle_shortener_free(ZProgramGraphHandleShortener *shortener) {
  if (!shortener) return;
  free(shortener->order);
  *shortener = (ZProgramGraphHandleShortener){0};
}

static const char *shortener_segment_at(const ZProgramGraphHandleShortener *shortener, size_t sorted_index) {
  return handle_segment_of(shortener->graph->nodes[shortener->order[sorted_index]].id);
}

/* First sorted index whose segment is >= prefix. */
static size_t shortener_lower_bound(const ZProgramGraphHandleShortener *shortener, const char *prefix) {
  size_t lo = 0;
  size_t hi = shortener->len;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (strcmp(shortener_segment_at(shortener, mid), prefix) < 0) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

/* Counts segments with the given prefix and how many equal it exactly. */
static void shortener_prefix_counts(const ZProgramGraphHandleShortener *shortener, const char *prefix, size_t *prefix_count, size_t *exact_count) {
  *prefix_count = 0;
  *exact_count = 0;
  size_t prefix_len = strlen(prefix);
  for (size_t i = shortener_lower_bound(shortener, prefix); i < shortener->len; i++) {
    const char *segment = shortener_segment_at(shortener, i);
    if (strncmp(segment, prefix, prefix_len) != 0) break;
    (*prefix_count)++;
    if (segment[prefix_len] == '\0') (*exact_count)++;
    if (*prefix_count > 1 && *exact_count > 1) break;
  }
}

enum { HANDLE_SHORT_MIN = 2 };
enum { HANDLE_ALIAS_HEAD_MAX = 6 };
enum { HANDLE_ALIAS_TAIL_MAX = 4 };

/* True when head..tail matches exactly one segment, the target's own. */
static bool shortener_alias_unique(const ZProgramGraphHandleShortener *shortener, const char *head, size_t head_len, const char *tail, size_t tail_len, const char *segment) {
  char prefix[HANDLE_ALIAS_HEAD_MAX + 1];
  memcpy(prefix, head, head_len);
  prefix[head_len] = '\0';
  size_t matches = 0;
  for (size_t i = shortener_lower_bound(shortener, prefix); i < shortener->len; i++) {
    const char *candidate = shortener_segment_at(shortener, i);
    if (strncmp(candidate, prefix, head_len) != 0) break;
    size_t candidate_len = strlen(candidate);
    if (candidate_len < head_len + tail_len) continue;
    if (memcmp(candidate + (candidate_len - tail_len), tail, tail_len) != 0) continue;
    matches++;
    if (matches > 1 || strcmp(candidate, segment) != 0) return false;
  }
  return matches == 1;
}

void z_program_graph_append_short_handle(const ZProgramGraphHandleShortener *shortener, const char *id, ZBuf *out) {
  if (!out) return;
  const char *full = id && id[0] ? id : "?";
  if (!shortener || !shortener->graph || !id || !id[0]) {
    zbuf_append(out, full);
    return;
  }
  const char *segment = handle_segment_of(id);
  size_t segment_len = strlen(segment);
  char best[HANDLE_DISTANCE_MAX + 4];
  size_t best_len = strlen(full);
  snprintf(best, sizeof(best), "%s", full);
  char prefix[HANDLE_DISTANCE_MAX + 1];
  for (size_t k = HANDLE_SHORT_MIN; k <= segment_len && k <= HANDLE_DISTANCE_MAX; k++) {
    memcpy(prefix, segment, k);
    prefix[k] = '\0';
    size_t prefix_count = 0;
    size_t exact_count = 0;
    shortener_prefix_counts(shortener, prefix, &prefix_count, &exact_count);
    bool resolves_here = (exact_count == 1 && k == segment_len) || (exact_count == 0 && prefix_count == 1);
    if (resolves_here) {
      if (k + 1 < best_len) {
        best_len = k + 1;
        snprintf(best, sizeof(best), "#%s", prefix);
      }
      break;
    }
    if (k == segment_len) break;
  }
  /* Ids in shared-prefix families shorten further as #head..tail. */
  if (best_len > HANDLE_ALIAS_HEAD_MAX + 4) {
    for (size_t h = HANDLE_SHORT_MIN; h <= HANDLE_ALIAS_HEAD_MAX && h < segment_len; h++) {
      for (size_t t = 1; t <= HANDLE_ALIAS_TAIL_MAX && h + t <= segment_len; t++) {
        size_t alias_len = h + 2 + t + 1; /* #head..tail */
        if (alias_len >= best_len) continue;
        if (!shortener_alias_unique(shortener, segment, h, segment + (segment_len - t), t, segment)) continue;
        best_len = alias_len;
        snprintf(best, sizeof(best), "#%.*s..%s", (int)h, segment, segment + (segment_len - t));
      }
    }
  }
  zbuf_append(out, best);
}
