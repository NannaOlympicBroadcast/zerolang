#include "program_graph_c_import.h"

#include <stdlib.h>
#include <string.h>

static bool graph_c_import_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static char *graph_c_import_dirname_copy(const char *path) {
  if (!path || !path[0]) return z_strdup(".");
  const char *slash = strrchr(path, '/');
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static char *graph_c_import_join_path(const char *dir, const char *path) {
  if (!path || !path[0]) return z_strdup("");
  if (path[0] == '/') return z_strdup(path);
  if (!dir || !dir[0] || graph_c_import_text_eq(dir, ".")) return z_strdup(path);
  ZBuf joined;
  zbuf_init(&joined);
  zbuf_append(&joined, dir);
  if (joined.len > 0 && joined.data[joined.len - 1] != '/') zbuf_append_char(&joined, '/');
  zbuf_append(&joined, path);
  return joined.data ? joined.data : z_strdup(path);
}

static bool graph_c_import_read_header_candidate(const char *path, char **out_header, char **out_resolved) {
  if (!path || !path[0] || !out_header || !out_resolved) return false;
  ZDiag diag = {0};
  char *header = z_read_file(path, &diag);
  if (!header) return false;
  *out_header = header;
  *out_resolved = z_strdup(path);
  return true;
}

static bool graph_c_import_read_header(const ZProgramGraphNode *c_import, const IrProgram *ir, char **out_header, char **out_resolved) {
  if (!c_import || !c_import->value || !c_import->value[0] || !out_header || !out_resolved) return false;
  *out_header = NULL;
  *out_resolved = NULL;
  if (graph_c_import_read_header_candidate(c_import->value, out_header, out_resolved)) return true;
  if (ir && ir->package_root && ir->package_root[0]) {
    char *package_relative = graph_c_import_join_path(ir->package_root, c_import->value);
    bool ok = graph_c_import_read_header_candidate(package_relative, out_header, out_resolved);
    free(package_relative);
    if (ok) return true;
  }
  char *source_dir = graph_c_import_dirname_copy(c_import->path);
  char *source_relative = graph_c_import_join_path(source_dir, c_import->value);
  if (graph_c_import_read_header_candidate(source_relative, out_header, out_resolved)) {
    free(source_relative);
    free(source_dir);
    return true;
  }
  free(source_relative);
  char *package_dir = graph_c_import_dirname_copy(source_dir);
  char *package_relative = graph_c_import_join_path(package_dir, c_import->value);
  bool ok = graph_c_import_read_header_candidate(package_relative, out_header, out_resolved);
  free(package_relative);
  free(package_dir);
  free(source_dir);
  return ok;
}

bool z_program_graph_find_c_import_function(const ZProgramGraphNode *c_import, const IrProgram *ir, const ZTargetInfo *target, const char *symbol, ZCImportFunction *out) {
  if (!c_import || !symbol || !symbol[0] || !out) return false;
  char *header = NULL;
  char *resolved_header = NULL;
  if (!graph_c_import_read_header(c_import, ir, &header, &resolved_header)) return false;
  ZCImportFunctionVec functions = {0};
  z_c_header_parse_functions_for_target(header, target, &functions);
  free(header);
  for (size_t i = 0; i < functions.len; i++) {
    ZCImportFunction *function = &functions.items[i];
    if (!graph_c_import_text_eq(function->name, symbol)) continue;
    *out = *function;
    *function = (ZCImportFunction){0};
    out->import_header = z_strdup(c_import->value);
    out->import_resolved_header = z_strdup(resolved_header ? resolved_header : c_import->value);
    z_c_import_function_vec_free(&functions);
    free(resolved_header);
    return true;
  }
  z_c_import_function_vec_free(&functions);
  free(resolved_header);
  return false;
}
