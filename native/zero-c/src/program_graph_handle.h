#ifndef ZERO_C_PROGRAM_GRAPH_HANDLE_H
#define ZERO_C_PROGRAM_GRAPH_HANDLE_H

#include "program_graph.h"

/*
 * Short patch handles. Every node id has the shape #<kind>_<segment>
 * (for example #stmt_55ae541c). Patch operations and zero query --node accept
 * three spellings for a node reference:
 *   1. the exact node id (#stmt_55ae541c)
 *   2. the bare segment (#55ae541c)
 *   3. any prefix of the segment that is unique in the graph (#55ae)
 * `zero view --fn <name> --handles` prints the shortest form that resolves.
 */

/* Resolves a handle to a node. On ambiguity sets *ambiguous (when provided)
 * and returns NULL; a missing handle returns NULL with *ambiguous false. */
const ZProgramGraphNode *z_program_graph_resolve_handle(const ZProgramGraph *graph, const char *handle, bool *ambiguous);

/* Finds the node id closest to the missing handle (Levenshtein over ids and
 * segments, with a distance threshold). Returns NULL when nothing is close. */
const char *z_program_graph_nearest_handle(const ZProgramGraph *graph, const char *handle);

/* Edit distance between two short texts (used for nearest-name hints). */
size_t z_program_graph_handle_distance(const char *left, const char *right);

typedef struct {
  const ZProgramGraph *graph;
  size_t *order; /* node indexes sorted by segment */
  size_t len;
} ZProgramGraphHandleShortener;

void z_program_graph_handle_shortener_init(ZProgramGraphHandleShortener *shortener, const ZProgramGraph *graph);
void z_program_graph_handle_shortener_free(ZProgramGraphHandleShortener *shortener);

/* Appends the shortest handle (with leading #) that resolves to the node id;
 * falls back to the full id when no shorter alias is unique. */
void z_program_graph_append_short_handle(const ZProgramGraphHandleShortener *shortener, const char *id, ZBuf *out);

#endif
