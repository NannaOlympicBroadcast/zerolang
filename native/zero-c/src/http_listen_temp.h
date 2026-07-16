#ifndef ZERO_C_HTTP_LISTEN_TEMP_H
#define ZERO_C_HTTP_LISTEN_TEMP_H

#include "zero.h"

#include <stdbool.h>
#include <stddef.h>

bool z_http_listen_temp_path(const char *temp_dir, const char *leaf, char *out, size_t out_cap, ZDiag *diag);
bool z_http_listen_create_temp_dir(char *out, size_t out_cap, ZDiag *diag);
void z_http_listen_cleanup_temp_dir(const char *temp_dir);

#endif
