#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "../include/zero.h"
#include "../src/process_exec.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

static void fail(const char *message) {
  fprintf(stderr, "process_exec_smoke: %s\n", message);
  exit(1);
}

static void expect_true(bool value, const char *message) {
  if (!value) fail(message);
}

static void expect_false(bool value, const char *message) {
  if (value) fail(message);
}

static void expect_text(const char *actual, const char *expected, const char *message) {
  if (strcmp(actual ? actual : "", expected ? expected : "") != 0) {
    fprintf(stderr, "process_exec_smoke: %s\nactual:   %s\nexpected: %s\n", message, actual ? actual : "", expected ? expected : "");
    exit(1);
  }
}

void *z_checked_reallocarray(void *ptr, size_t count, size_t item_size) {
  if (item_size != 0 && count > ((size_t)-1) / item_size) fail("allocation overflow");
  size_t size = count * item_size;
  void *next = realloc(ptr, size == 0 ? 1 : size);
  if (!next) fail("out of memory");
  return next;
}

size_t z_grow_capacity(size_t current, size_t required, size_t initial) {
  size_t next = current == 0 ? (initial == 0 ? 1 : initial) : current;
  while (next < required) {
    if (next > ((size_t)-1) / 2) return required;
    next *= 2;
  }
  return next;
}

char *z_strndup(const char *text, size_t len) {
  char *copy = malloc(len + 1);
  if (!copy) fail("out of memory");
  memcpy(copy, text, len);
  copy[len] = 0;
  return copy;
}

char *z_strdup(const char *text) {
  return z_strndup(text ? text : "", strlen(text ? text : ""));
}

void zbuf_init(ZBuf *buf) {
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

void zbuf_append_char(ZBuf *buf, char ch) {
  size_t required = buf->len + 2;
  if (required > buf->cap) {
    size_t next = z_grow_capacity(buf->cap, required, 16);
    buf->data = z_checked_reallocarray(buf->data, next, sizeof(char));
    buf->cap = next;
  }
  buf->data[buf->len++] = ch;
  buf->data[buf->len] = 0;
}

void zbuf_append(ZBuf *buf, const char *text) {
  for (const char *cursor = text ? text : ""; *cursor; cursor++) zbuf_append_char(buf, *cursor);
}

void zbuf_free(ZBuf *buf) {
  free(buf->data);
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

static void test_argv_builder(void) {
  ZProcessArgv argv;
  z_process_argv_init(&argv);
  expect_false(z_process_argv_push(NULL, "cc"), "push rejects null argv");
  expect_false(z_process_argv_push(&argv, ""), "push rejects empty value");
  expect_true(z_process_argv_push(&argv, "cc"), "push accepts command");
  expect_true(argv.len == 1 && argv.items && argv.items[1] == NULL, "push keeps argv null-terminated");
  z_process_argv_free(&argv);
  expect_true(argv.items == NULL && argv.len == 0 && argv.cap == 0, "free resets argv");
}

static void test_flag_parser(void) {
  ZProcessArgv argv;
  z_process_argv_init(&argv);
  bool suppress_stderr = false;
  expect_true(z_process_argv_append_flag_text(&argv, "-O2 -DNAME='two words' escaped\\ space 2>/dev/null", &suppress_stderr), "flag parser accepts quoted flags");
  expect_true(suppress_stderr, "flag parser recognizes stderr suppression");
  expect_true(argv.len == 3, "flag parser appends expected token count");
  expect_text(argv.items[0], "-O2", "flag parser keeps first flag");
  expect_text(argv.items[1], "-DNAME=two words", "flag parser preserves quoted spaces inside token");
  expect_text(argv.items[2], "escaped space", "flag parser handles escaped spaces");
  z_process_argv_free(&argv);

  ZProcessArgv malformed;
  z_process_argv_init(&malformed);
  suppress_stderr = false;
  expect_false(z_process_argv_append_flag_text(&malformed, "-O2 -DNAME='unterminated", &suppress_stderr), "flag parser rejects unterminated quotes");
  z_process_argv_free(&malformed);
}

static void test_ensure_dir(void) {
  char dir_path[256];
  char file_path[256];
  snprintf(dir_path, sizeof(dir_path), "/tmp/zero-process-exec-smoke-dir-%ld", (long)getpid());
  snprintf(file_path, sizeof(file_path), "/tmp/zero-process-exec-smoke-file-%ld", (long)getpid());
  remove(dir_path);
  remove(file_path);
  expect_true(z_process_ensure_dir(dir_path), "ensure_dir creates missing directory");
  expect_true(z_process_ensure_dir(dir_path), "ensure_dir accepts existing directory");
  FILE *file = fopen(file_path, "wb");
  expect_true(file != NULL, "created regular file for ensure_dir check");
  fputs("not a directory", file);
  fclose(file);
  expect_false(z_process_ensure_dir(file_path), "ensure_dir rejects existing regular file");
  remove(file_path);
  rmdir(dir_path);
}

static void write_text_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  expect_true(file != NULL, "created fixture file");
  fputs(text ? text : "", file);
  fclose(file);
}

static void test_output_file_contract(void) {
  expect_false(z_process_prepare_output_file(NULL), "output preparation rejects null path");
  expect_false(z_process_prepare_output_file(""), "output preparation rejects empty path");
  expect_false(z_process_output_file_ready(NULL), "output ready rejects null path");
  expect_false(z_process_output_file_ready(""), "output ready rejects empty path");

  char root[256];
  snprintf(root, sizeof(root), "/tmp/zero-process-output-smoke-%ld", (long)getpid());
  rmdir(root);
  expect_true(mkdir(root, 0700) == 0, "created output contract temp dir");
  char missing_path[256];
  char file_path[256];
  char empty_path[256];
  char dir_path[256];
  char missing_parent_path[256];
  char cleanup_path[256];
  char cleanup_missing_path[256];
  snprintf(missing_path, sizeof(missing_path), "%s/missing.out", root);
  snprintf(file_path, sizeof(file_path), "%s/file.out", root);
  snprintf(empty_path, sizeof(empty_path), "%s/empty.out", root);
  snprintf(dir_path, sizeof(dir_path), "%s/dir.out", root);
  snprintf(missing_parent_path, sizeof(missing_parent_path), "%s/nope/out", root);
  snprintf(cleanup_path, sizeof(cleanup_path), "%s/cleanup.out", root);
  snprintf(cleanup_missing_path, sizeof(cleanup_missing_path), "%s/already-gone.out", root);

  expect_true(z_process_prepare_output_file(missing_path), "missing output is ready for creation");
  expect_false(z_process_output_file_ready(missing_path), "missing output is not ready after tool run");
  expect_false(z_process_executable_file_ready(missing_path), "executable ready rejects missing output");
  expect_false(z_process_mark_executable(missing_path), "mark executable rejects missing output");
  expect_true(z_process_remove_regular_file(cleanup_missing_path), "cleanup accepts missing regular output");
  expect_false(z_process_prepare_output_file(missing_parent_path), "output preparation rejects missing parent directory");

  write_text_file(file_path, "artifact");
  expect_true(z_process_output_file_ready(file_path), "non-empty regular output is ready");
#if !defined(_WIN32)
  expect_false(z_process_executable_file_ready(file_path), "plain output is not executable before finalization");
#endif
  expect_true(z_process_mark_executable(file_path), "mark executable accepts regular non-empty output");
  expect_true(z_process_executable_file_ready(file_path), "executable ready accepts finalized artifact");
#if !defined(_WIN32)
  expect_true(access(file_path, X_OK) == 0, "mark executable sets execute bit");
#endif
  expect_true(z_process_prepare_output_file(file_path), "stale regular output can be removed");
  expect_false(z_process_output_file_ready(file_path), "removed stale output is no longer ready");

  write_text_file(empty_path, "");
  expect_false(z_process_output_file_ready(empty_path), "empty output is not ready");
  expect_false(z_process_executable_file_ready(empty_path), "executable ready rejects empty output");
  expect_false(z_process_mark_executable(empty_path), "mark executable rejects empty output");
  expect_true(z_process_prepare_output_file(empty_path), "empty regular output can be removed before rebuild");

  write_text_file(cleanup_path, "temporary");
  expect_true(z_process_remove_regular_file(cleanup_path), "cleanup removes regular output");
  expect_false(z_process_output_file_ready(cleanup_path), "cleanup removed regular output");

  expect_true(mkdir(dir_path, 0700) == 0, "created directory output fixture");
  expect_false(z_process_prepare_output_file(dir_path), "output preparation rejects directories");
  expect_false(z_process_output_file_ready(dir_path), "output ready rejects directories");
  expect_false(z_process_executable_file_ready(dir_path), "executable ready rejects directories");
  expect_false(z_process_mark_executable(dir_path), "mark executable rejects directories");
  expect_false(z_process_remove_regular_file(dir_path), "cleanup rejects directories");
  rmdir(dir_path);

#if !defined(_WIN32)
  char target_path[256];
  char symlink_path[256];
  snprintf(target_path, sizeof(target_path), "%s/target.out", root);
  snprintf(symlink_path, sizeof(symlink_path), "%s/link.out", root);
  write_text_file(target_path, "target");
  expect_true(symlink(target_path, symlink_path) == 0, "created symlink output fixture");
  expect_false(z_process_prepare_output_file(symlink_path), "output preparation rejects symlinks");
  expect_false(z_process_output_file_ready(symlink_path), "output ready rejects symlinks");
  expect_false(z_process_executable_file_ready(symlink_path), "executable ready rejects symlinks");
  expect_false(z_process_mark_executable(symlink_path), "mark executable rejects symlinks");
  expect_false(z_process_remove_regular_file(symlink_path), "cleanup rejects symlinks");
  unlink(symlink_path);
  unlink(target_path);
#endif

  rmdir(root);
}

static void test_command_lookup(void) {
  expect_false(z_process_command_available("sh;rm"), "command lookup rejects shell syntax");
  expect_false(z_process_command_available("../sh"), "command lookup rejects unresolved relative path");
#if !defined(_WIN32)
  expect_true(z_process_command_available("/bin/sh"), "command lookup accepts executable absolute path");
#endif
}

static void test_run_argv(void) {
#if !defined(_WIN32)
  const char *ok_argv[] = {"/bin/sh", "-c", "exit 0", NULL};
  const char *bad_argv[] = {"/bin/sh", "-c", "exit 7", NULL};
  ZProcessArgv ok;
  z_process_argv_init(&ok);
  expect_true(z_process_argv_push(&ok, ok_argv[0]), "run argv command push");
  expect_true(z_process_argv_push(&ok, ok_argv[1]), "run argv flag push");
  expect_true(z_process_argv_push(&ok, ok_argv[2]), "run argv script push");
  expect_true(z_process_run_argv(&ok, true, true, false), "run argv reports successful child");
  z_process_argv_free(&ok);

  ZProcessArgv bad;
  z_process_argv_init(&bad);
  expect_true(z_process_argv_push(&bad, bad_argv[0]), "bad argv command push");
  expect_true(z_process_argv_push(&bad, bad_argv[1]), "bad argv flag push");
  expect_true(z_process_argv_push(&bad, bad_argv[2]), "bad argv script push");
  expect_false(z_process_run_argv(&bad, true, true, false), "run argv reports failed child");
  z_process_argv_free(&bad);
#endif
}

static void test_first_stdout_line(void) {
#if !defined(_WIN32)
  const char *first_line[] = {"/bin/sh", "-c", "printf 'alpha\\nbeta\\n'", NULL};
  char *line = z_process_first_stdout_line(first_line, true);
  expect_text(line, "alpha", "first stdout line captures only first line");
  free(line);

  const char *failed_after_output[] = {"/bin/sh", "-c", "printf stale; exit 9", NULL};
  line = z_process_first_stdout_line(failed_after_output, true);
  expect_text(line, "", "first stdout line ignores failed child output");
  free(line);

  const char *missing[] = {"/tmp/zero-process-exec-smoke-missing-command", NULL};
  line = z_process_first_stdout_line(missing, true);
  expect_text(line, "", "first stdout line handles missing command");
  free(line);
#endif
}

int main(void) {
  test_argv_builder();
  test_flag_parser();
  test_ensure_dir();
  test_output_file_contract();
  test_command_lookup();
  test_run_argv();
  test_first_stdout_line();
  return 0;
}
