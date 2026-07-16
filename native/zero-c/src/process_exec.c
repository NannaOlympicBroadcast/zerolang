#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "process_exec.h"
#include "process_path.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

void z_process_argv_init(ZProcessArgv *argv) {
  argv->items = NULL;
  argv->len = 0;
  argv->cap = 0;
}

bool z_process_argv_push(ZProcessArgv *argv, const char *value) {
  if (!argv || !value || !value[0]) return false;
  size_t required = argv->len + 2;
  if (required > argv->cap) {
    size_t next = z_grow_capacity(argv->cap, required, 8);
    argv->items = z_checked_reallocarray(argv->items, next, sizeof(char *));
    argv->cap = next;
  }
  argv->items[argv->len++] = z_strdup(value);
  argv->items[argv->len] = NULL;
  return true;
}

bool z_process_argv_append_flag_text(ZProcessArgv *argv, const char *text, bool *suppress_stderr) {
  const char *cursor = text ? text : "";
  while (*cursor) {
    while (isspace((unsigned char)*cursor)) cursor++;
    if (!*cursor) break;

    ZBuf token;
    zbuf_init(&token);
    while (*cursor && !isspace((unsigned char)*cursor)) {
      if (*cursor == '\'') {
        cursor++;
        bool closed = false;
        while (*cursor && *cursor != '\'') zbuf_append_char(&token, *cursor++);
        if (*cursor == '\'') {
          closed = true;
          cursor++;
        }
        if (!closed) {
          zbuf_free(&token);
          return false;
        }
      } else if (*cursor == '\\' && cursor[1]) {
        cursor++;
        zbuf_append_char(&token, *cursor++);
      } else {
        zbuf_append_char(&token, *cursor++);
      }
    }

    bool ok = true;
    if (token.len > 0) {
      if (strcmp(token.data, "2>/dev/null") == 0) {
        if (suppress_stderr) *suppress_stderr = true;
      } else {
        ok = z_process_argv_push(argv, token.data);
      }
    }
    zbuf_free(&token);
    if (!ok) return false;
  }
  return true;
}

void z_process_argv_free(ZProcessArgv *argv) {
  if (!argv) return;
  for (size_t i = 0; i < argv->len; i++) free(argv->items[i]);
  free(argv->items);
  argv->items = NULL;
  argv->len = 0;
  argv->cap = 0;
}

static bool z_process_existing_dir(const char *path) {
#if defined(_WIN32)
  struct _stat st;
  return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool z_process_ensure_dir(const char *path) {
  if (!path || !path[0]) return false;
#if defined(_WIN32)
  if (_mkdir(path) == 0) return true;
#else
  if (mkdir(path, 0777) == 0) return true;
#endif
  if (errno != EEXIST) return false;
  return z_process_existing_dir(path);
}

#if defined(_WIN32)
typedef struct _stat ZProcessOutputStat;
#else
typedef struct stat ZProcessOutputStat;
#endif

static bool z_process_lstat_output(const char *path, ZProcessOutputStat *st) {
  if (!path || !path[0] || !st) return false;
#if defined(_WIN32)
  return _stat(path, st) == 0;
#else
  return lstat(path, st) == 0;
#endif
}

static bool z_process_output_is_regular(const ZProcessOutputStat *st) {
#if defined(_WIN32)
  return st && (st->st_mode & _S_IFREG) != 0;
#else
  return st && S_ISREG(st->st_mode);
#endif
}

static bool z_process_output_is_symlink(const ZProcessOutputStat *st) {
#if defined(_WIN32)
  (void)st;
  return false;
#else
  return st && S_ISLNK(st->st_mode);
#endif
}

static bool z_process_output_parent_ready(const char *path) {
  if (!path || !path[0]) return false;
  const char *last_sep = NULL;
  for (const char *cursor = path; *cursor; cursor++) {
    if (*cursor == '/' || *cursor == '\\') last_sep = cursor;
  }
  if (!last_sep) return true;
  if (last_sep == path) return z_process_existing_dir("/");
#if defined(_WIN32)
  if (last_sep == path + 2 && isalpha((unsigned char)path[0]) && path[1] == ':') {
    char drive_root[4] = {path[0], ':', '\\', 0};
    return z_process_existing_dir(drive_root);
  }
#endif
  ZBuf parent;
  zbuf_init(&parent);
  for (const char *cursor = path; cursor < last_sep; cursor++) zbuf_append_char(&parent, *cursor);
  bool ok = z_process_existing_dir(parent.data);
  zbuf_free(&parent);
  return ok;
}

bool z_process_prepare_output_file(const char *path) {
  ZProcessOutputStat st;
  if (!path || !path[0]) return false;
  if (!z_process_output_parent_ready(path)) return false;
  if (!z_process_lstat_output(path, &st)) return errno == ENOENT;
  if (z_process_output_is_symlink(&st) || !z_process_output_is_regular(&st)) return false;
  if (remove(path) == 0) return true;
  return errno == ENOENT;
}

bool z_process_output_file_ready(const char *path) {
  ZProcessOutputStat st;
  if (!z_process_lstat_output(path, &st)) return false;
  if (z_process_output_is_symlink(&st) || !z_process_output_is_regular(&st)) return false;
  return st.st_size > 0;
}

bool z_process_executable_file_ready(const char *path) {
  if (!z_process_output_file_ready(path)) return false;
#if defined(_WIN32)
  return true;
#else
  return access(path, X_OK) == 0;
#endif
}

bool z_process_mark_executable(const char *path) {
  if (!z_process_output_file_ready(path)) return false;
#if defined(_WIN32)
  return true;
#else
  if (chmod(path, 0755) != 0) return false;
  return z_process_executable_file_ready(path);
#endif
}

bool z_process_remove_regular_file(const char *path) {
  ZProcessOutputStat st;
  if (!path || !path[0]) return false;
  if (!z_process_lstat_output(path, &st)) return errno == ENOENT;
  if (z_process_output_is_symlink(&st) || !z_process_output_is_regular(&st)) return false;
  if (remove(path) == 0) return true;
  return errno == ENOENT;
}

#if !defined(_WIN32)
static bool z_process_suppress_stream(FILE *stream) {
  return freopen("/dev/null", "w", stream) != NULL;
}

static bool z_process_wait_success(pid_t pid) {
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

bool z_process_run_argv(const ZProcessArgv *argv, bool suppress_stdout, bool suppress_stderr, bool uses_zig_env) {
  if (!argv || argv->len == 0 || !argv->items || !argv->items[0]) return false;
  char *executable = z_process_resolve_executable(argv->items[0]);
  if (!executable) return false;
#if defined(_WIN32)
  (void)suppress_stdout;
  (void)suppress_stderr;
  (void)uses_zig_env;
  int rc = _spawnv(_P_WAIT, executable, (const char *const *)argv->items);
  free(executable);
  return rc == 0;
#else
  pid_t pid = fork();
  if (pid < 0) {
    free(executable);
    return false;
  }
  if (pid == 0) {
    if (uses_zig_env) {
      if (setenv("ZIG_GLOBAL_CACHE_DIR", ".zero/zig-global-cache", 1) != 0) _exit(127);
      if (setenv("ZIG_LOCAL_CACHE_DIR", ".zero/zig-local-cache", 1) != 0) _exit(127);
    }
    if (suppress_stdout && !z_process_suppress_stream(stdout)) _exit(127);
    if (suppress_stderr && !z_process_suppress_stream(stderr)) _exit(127);
    execv(executable, argv->items);
    _exit(127);
  }
  free(executable);
  return z_process_wait_success(pid);
#endif
}

char *z_process_first_stdout_line(const char *const *argv, bool suppress_stderr) {
  if (!argv || !argv[0]) return z_strdup("");
  char *executable = z_process_resolve_executable(argv[0]);
  if (!executable) return z_strdup("");
#if defined(_WIN32)
  (void)suppress_stderr;
  free(executable);
  return z_strdup("");
#else
  int pipe_fd[2];
  if (pipe(pipe_fd) != 0) {
    free(executable);
    return z_strdup("");
  }
  pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    free(executable);
    return z_strdup("");
  }
  if (pid == 0) {
    close(pipe_fd[0]);
    if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) _exit(127);
    close(pipe_fd[1]);
    if (suppress_stderr && !z_process_suppress_stream(stderr)) _exit(127);
    execv(executable, (char *const *)argv);
    _exit(127);
  }
  free(executable);
  close(pipe_fd[1]);
  ZBuf line;
  zbuf_init(&line);
  bool capturing = true;
  bool read_ok = true;
  char bytes[256];
  while (true) {
    ssize_t n = read(pipe_fd[0], bytes, sizeof(bytes));
    if (n < 0) {
      if (errno == EINTR) continue;
      read_ok = false;
      break;
    }
    if (n == 0) break;
    for (ssize_t i = 0; capturing && i < n; i++) {
      if (bytes[i] == '\n' || bytes[i] == '\r') {
        capturing = false;
      } else if (line.len < 255) {
        zbuf_append_char(&line, bytes[i]);
      }
    }
  }
  close(pipe_fd[0]);
  bool child_ok = z_process_wait_success(pid);
  char *out = read_ok && child_ok && line.data ? line.data : z_strdup("");
  if (out == line.data) line.data = NULL;
  zbuf_free(&line);
  return out;
#endif
}
