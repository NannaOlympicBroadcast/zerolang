#ifndef ZERO_C_INIT_TEMPLATE_H
#define ZERO_C_INIT_TEMPLATE_H

#include "zero.h"

#include <stdbool.h>

bool z_init_write_manifest(const char *root, const char *name, const char *main_path, const char *manifest_format, bool template_manifest, const char **manifest_file_name_out, ZDiag *diag);
bool z_init_template_kind_is_known(const char *kind);
const char *z_init_template_projection_path(const char *kind);
bool z_init_template_reject_existing_outputs(const char *root, const char *kind, ZDiag *diag);
bool z_init_template_write_files(const char *root, const char *kind, const char *name, const char *manifest_format, const char **manifest_file_name, ZDiag *diag);

#endif
