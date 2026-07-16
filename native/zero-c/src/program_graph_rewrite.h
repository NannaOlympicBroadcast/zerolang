#ifndef ZERO_C_PROGRAM_GRAPH_REWRITE_H
#define ZERO_C_PROGRAM_GRAPH_REWRITE_H

#include "program_graph.h"

/*
 * Structural rewrite by example: `zero patch --rewrite '<pattern>' --to
 * '<template>' [--fn <name>] [--apply]`. Pattern and template are canonical
 * projection expressions with metavariables $A, $B, ... that bind arbitrary
 * expression subtrees; the same metavariable twice must match equal subtrees.
 * Matching is expression-level only and skips subtree kinds the matcher does
 * not support.
 */

typedef struct {
  char *node_id;       /* full id of the matched expression root */
  char *short_handle;  /* shortest patch handle for the match site */
  char *function_name; /* enclosing function, when one exists */
  char *path;          /* module source path */
  char *before;        /* rendered matched expression */
  char *after;         /* rendered instantiated template */
} ZProgramGraphRewriteMatch;

typedef struct {
  ZProgramGraphRewriteMatch *items;
  size_t len;
  size_t cap;
  size_t skipped_unsupported;
  size_t functions_scanned;
} ZProgramGraphRewriteResult;

/* Collects every match site in scope (one function with function_filter, the
 * whole package without). Returns false with a populated diag on pattern or
 * template errors. */
bool z_program_graph_rewrite_collect(const ZProgramGraph *graph,
                                     const char *pattern,
                                     const char *template_text,
                                     const char *function_filter,
                                     ZProgramGraphRewriteResult *out,
                                     ZDiag *diag);

/* Builds a zero-program-graph-patch v1 text with one replaceExpr per match. */
char *z_program_graph_rewrite_build_patch_text(const ZProgramGraphRewriteResult *result);

void z_program_graph_rewrite_result_free(ZProgramGraphRewriteResult *result);

#endif
