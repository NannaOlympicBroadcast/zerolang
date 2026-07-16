#ifndef ZERO_PROCESS_EXEC_H
#define ZERO_PROCESS_EXEC_H

#include "zero.h"

typedef struct {
  char **items;
  size_t len;
  size_t cap;
} ZProcessArgv;

void z_process_argv_init(Z_OUT ZProcessArgv *argv);
bool z_process_argv_push(Z_INOUT ZProcessArgv *argv, Z_IN const char *value);
bool z_process_argv_append_flag_text(Z_INOUT ZProcessArgv *argv, Z_IN const char *text, Z_INOUT bool *suppress_stderr);
void z_process_argv_free(Z_INOUT ZProcessArgv *argv);
bool z_process_ensure_dir(Z_IN const char *path);
bool z_process_prepare_output_file(Z_IN const char *path);
bool z_process_output_file_ready(Z_IN const char *path);
bool z_process_executable_file_ready(Z_IN const char *path);
bool z_process_mark_executable(Z_IN const char *path);
bool z_process_remove_regular_file(Z_IN const char *path);
bool z_process_run_argv(Z_IN const ZProcessArgv *argv, bool suppress_stdout, bool suppress_stderr, bool uses_zig_env);
bool z_process_command_available(Z_IN const char *name);
Z_RET_OWNED char *z_process_first_stdout_line(Z_IN const char *const *argv, bool suppress_stderr);

#endif
