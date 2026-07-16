#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "process_exec.h"
#include "process_path.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

static bool process_command_name_safe(const char *name) {
  if (!name || !name[0]) return false;
  for (size_t i = 0; name[i]; i++) {
    unsigned char ch = (unsigned char)name[i];
    if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '+')) return false;
  }
  return true;
}

static bool process_has_path_separator(const char *path) {
  if (!path) return false;
  for (size_t i = 0; path[i]; i++) {
    if (path[i] == '/' || path[i] == '\\') return true;
  }
  return false;
}

static bool process_path_executable(const char *path) {
  struct stat st;
#if defined(_WIN32)
  return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode);
#else
  return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
#endif
}

static bool process_path_segment_absolute(const char *segment, size_t len) {
  if (!segment || len == 0) return false;
#if defined(_WIN32)
  if (len >= 3 && isalpha((unsigned char)segment[0]) && segment[1] == ':' && (segment[2] == '\\' || segment[2] == '/')) return true;
  return len >= 2 && segment[0] == '\\' && segment[1] == '\\';
#else
  return segment[0] == '/';
#endif
}

char *z_process_resolve_executable(const char *name) {
  if (!name || !name[0]) return NULL;
  if (process_has_path_separator(name)) return process_path_executable(name) ? z_strdup(name) : NULL;
  if (!process_command_name_safe(name)) return NULL;
  const char *path = getenv("PATH");
  if (!path || !path[0]) return NULL;
#if defined(_WIN32)
  const char delimiter = ';';
#else
  const char delimiter = ':';
#endif
  const char *cursor = path;
  while (true) {
    const char *end = strchr(cursor, delimiter);
    size_t dir_len = end ? (size_t)(end - cursor) : strlen(cursor);
    if (process_path_segment_absolute(cursor, dir_len)) {
      ZBuf candidate;
      zbuf_init(&candidate);
      for (size_t i = 0; i < dir_len; i++) zbuf_append_char(&candidate, cursor[i]);
      if (candidate.len > 0 && candidate.data[candidate.len - 1] != '/' && candidate.data[candidate.len - 1] != '\\') zbuf_append_char(&candidate, '/');
      zbuf_append(&candidate, name);
      bool ok = process_path_executable(candidate.data);
#if defined(_WIN32)
      if (!ok) {
        zbuf_append(&candidate, ".exe");
        ok = process_path_executable(candidate.data);
      }
#endif
      if (ok) return candidate.data;
      zbuf_free(&candidate);
    }
    if (!end) break;
    cursor = end + 1;
  }
  return NULL;
}

bool z_process_command_available(const char *name) {
  char *executable = z_process_resolve_executable(name);
  bool ok = executable != NULL;
  free(executable);
  return ok;
}
