#include "zero.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void z_diag_set_path_copy(ZDiag *diag, const char *path) {
  if (!diag) return;
  diag->path = path ? z_strdup(path) : NULL;
}

static void read_diag_io(ZDiag *diag, const char *path, const char *action) {
  if (!diag) return;
  diag->code = 1;
  z_diag_set_path_copy(diag, path);
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "failed to %s '%s': %s", action, path ? path : "", strerror(errno));
}

static bool read_path_is_existing_directory(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool reject_invalid_read_path(const char *path, ZDiag *diag) {
  if (!path || !path[0]) {
    errno = EINVAL;
    read_diag_io(diag, path, "read");
    return false;
  }
  if (read_path_is_existing_directory(path)) {
    errno = EACCES;
    read_diag_io(diag, path, "read");
    return false;
  }
  return true;
}

bool z_read_binary_file(const char *path, unsigned char **out, size_t *out_len, ZDiag *diag) {
  if (out) *out = NULL;
  if (out_len) *out_len = 0;
  if (!out || !out_len) {
    errno = EINVAL;
    read_diag_io(diag, path, "read");
    return false;
  }
  if (!reject_invalid_read_path(path, diag)) return false;
  FILE *file = fopen(path, "rb");
  if (!file) { read_diag_io(diag, path, "read"); return false; }
  if (fseek(file, 0, SEEK_END) != 0) {
    if (errno == 0) errno = EIO;
    read_diag_io(diag, path, "read");
    fclose(file);
    return false;
  }
  long size = ftell(file);
  if (size < 0 || (size_t)size > SIZE_MAX - 1) {
    if (errno == 0) errno = EIO;
    read_diag_io(diag, path, "read");
    fclose(file);
    return false;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    if (errno == 0) errno = EIO;
    read_diag_io(diag, path, "read");
    fclose(file);
    return false;
  }
  unsigned char *data = z_checked_malloc((size_t)size + 1);
  if (size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size) {
    if (errno == 0) errno = EIO;
    read_diag_io(diag, path, "read");
    free(data);
    fclose(file);
    return false;
  }
  data[(size_t)size] = 0;
  if (fclose(file) != 0) {
    read_diag_io(diag, path, "read");
    free(data);
    return false;
  }
  *out = data;
  *out_len = (size_t)size;
  return true;
}

bool z_read_file_prefix(const char *path, void *bytes, size_t len, size_t *out_read, ZDiag *diag) {
  if (out_read) *out_read = 0;
  if (!bytes || len == 0) {
    errno = EINVAL;
    read_diag_io(diag, path, "read");
    return false;
  }
  if (!reject_invalid_read_path(path, diag)) return false;
  FILE *file = fopen(path, "rb");
  if (!file) { read_diag_io(diag, path, "read"); return false; }
  size_t read = fread(bytes, 1, len, file);
  if (out_read) *out_read = read;
  if (ferror(file)) {
    if (errno == 0) errno = EIO;
    read_diag_io(diag, path, "read");
    fclose(file);
    return false;
  }
  if (fclose(file) != 0) {
    read_diag_io(diag, path, "read");
    return false;
  }
  return true;
}

char *z_read_file(const char *path, ZDiag *diag) {
  unsigned char *data = NULL;
  size_t len = 0;
  if (!z_read_binary_file(path, &data, &len, diag)) return NULL;
  (void)len;
  return (char *)data;
}
