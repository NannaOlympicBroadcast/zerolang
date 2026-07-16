#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include "http_listen_temp.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if defined(_WIN32)
#define ZERO_LISTEN_PATH_SEP '\\'
#else
#define ZERO_LISTEN_PATH_SEP '/'
#endif

static void listen_temp_diag(ZDiag *diag, const char *message, const char *expected, const char *actual, const char *help) {
  if (!diag) return;
  diag->code = 2002;
  diag->line = 1;
  diag->column = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "std.http.listen temporary file setup failed");
  snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "safe temporary listener artifact");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "temporary file setup failed");
  snprintf(diag->help, sizeof(diag->help), "%s", help ? help : "inspect /tmp permissions and available disk space");
}

bool z_http_listen_temp_path(const char *temp_dir, const char *leaf, char *out, size_t out_cap, ZDiag *diag) {
  if (!temp_dir || !leaf || !out || out_cap == 0) {
    listen_temp_diag(diag, "std.http.listen runner is missing temporary path storage", "temporary directory and file name", "missing path storage", NULL);
    return false;
  }
  int n = snprintf(out, out_cap, "%s%c%s", temp_dir, ZERO_LISTEN_PATH_SEP, leaf);
  if (n < 0 || (size_t)n >= out_cap) {
    listen_temp_diag(diag, "std.http.listen temporary path is too long", "temporary path within host PATH_MAX", "path truncated", "use a shorter TMPDIR or package path");
    return false;
  }
  return true;
}

#if defined(_WIN32)
static const char *listen_temp_root(void) {
  const char *root = getenv("TEMP");
  if (root && root[0]) return root;
  root = getenv("TMP");
  if (root && root[0]) return root;
  return ".";
}

static bool listen_create_unique_temp_dir(char *out, size_t out_cap, ZDiag *diag) {
  const char *root = listen_temp_root();
  unsigned long pid = (unsigned long)_getpid();
  for (unsigned long attempt = 0; attempt < 1000; attempt++) {
    int n = snprintf(out, out_cap, "%s%czero-listen-%lu-%lu", root, ZERO_LISTEN_PATH_SEP, pid, attempt);
    if (n < 0 || (size_t)n >= out_cap) {
      listen_temp_diag(diag, "std.http.listen temporary directory path is too long", "temporary path within host PATH_MAX", "path truncated", "use a shorter TEMP directory");
      return false;
    }
    if (_mkdir(out) == 0) return true;
    if (errno == EEXIST) continue;
    listen_temp_diag(diag, "std.http.listen could not create a private temporary directory", "private zero-listen-* directory", strerror(errno), "check temporary directory permissions and available disk space");
    return false;
  }
  listen_temp_diag(diag, "std.http.listen could not create a unique temporary directory", "unused zero-listen-* directory", "temporary directory attempts exhausted", "clean stale temporary directories and retry");
  return false;
}
#endif

bool z_http_listen_create_temp_dir(char *out, size_t out_cap, ZDiag *diag) {
#if defined(_WIN32)
  if (!out || out_cap == 0) {
    listen_temp_diag(diag, "std.http.listen temporary directory buffer is too small", "temporary path storage", "path buffer too small", NULL);
    return false;
  }
  return listen_create_unique_temp_dir(out, out_cap, diag);
#else
  static const char template_path[] = "/tmp/zero-listen-XXXXXX";
  if (!out || out_cap < sizeof(template_path)) {
    listen_temp_diag(diag, "std.http.listen temporary directory buffer is too small", "temporary path storage", "path buffer too small", NULL);
    return false;
  }
  memcpy(out, template_path, sizeof(template_path));
  if (!mkdtemp(out)) {
    listen_temp_diag(diag, "std.http.listen could not create a private temporary directory", "private /tmp/zero-listen-* directory", strerror(errno), "check /tmp permissions and available disk space");
    return false;
  }
  return true;
#endif
}

static bool listen_temp_dir_is_owned(const char *temp_dir) {
#if defined(_WIN32)
  static const char marker[] = "zero-listen-";
  if (!temp_dir || !temp_dir[0]) return false;
  const char *base = strrchr(temp_dir, '/');
  const char *backslash = strrchr(temp_dir, '\\');
  if (!base || (backslash && backslash > base)) base = backslash;
  const char *name = base ? base + 1 : temp_dir;
  size_t marker_len = sizeof(marker) - 1;
  return strncmp(name, marker, marker_len) == 0 && name[marker_len] != '\0';
#else
  static const char prefix[] = "/tmp/zero-listen-";
  size_t prefix_len = sizeof(prefix) - 1;
  if (!temp_dir || strncmp(temp_dir, prefix, prefix_len) != 0) return false;
  const char *suffix = temp_dir + prefix_len;
  return suffix[0] != '\0' && strchr(suffix, '/') == NULL;
#endif
}

static int listen_stat_path(const char *path, struct stat *st) {
#if defined(_WIN32)
  return stat(path, st);
#else
  return lstat(path, st);
#endif
}

static bool listen_stat_is_dir(const struct stat *st) {
#if defined(_WIN32)
  return st && (st->st_mode & _S_IFDIR) != 0;
#else
  return st && S_ISDIR(st->st_mode);
#endif
}

static int listen_remove_file(const char *path) {
#if defined(_WIN32)
  return remove(path);
#else
  return unlink(path);
#endif
}

static int listen_remove_dir(const char *path) {
#if defined(_WIN32)
  return _rmdir(path);
#else
  return rmdir(path);
#endif
}

static void listen_remove_tree(const char *path, unsigned depth) {
  if (!path || depth > 8) return;
  DIR *dir = opendir(path);
  if (!dir) {
    listen_remove_file(path);
    return;
  }
  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) continue;
    char child[PATH_MAX];
    int n = snprintf(child, sizeof(child), "%s%c%s", path, ZERO_LISTEN_PATH_SEP, entry->d_name);
    if (n < 0 || (size_t)n >= sizeof(child)) continue;
    struct stat st;
    if (listen_stat_path(child, &st) != 0) {
      listen_remove_file(child);
      continue;
    }
    if (listen_stat_is_dir(&st)) listen_remove_tree(child, depth + 1);
    else listen_remove_file(child);
  }
  closedir(dir);
  listen_remove_dir(path);
}

void z_http_listen_cleanup_temp_dir(const char *temp_dir) {
  if (!temp_dir || !temp_dir[0]) return;
  if (!listen_temp_dir_is_owned(temp_dir)) return;
  listen_remove_tree(temp_dir, 0);
}
