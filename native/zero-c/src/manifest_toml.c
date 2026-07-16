#include "manifest_toml.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *toml_skip_ws(const char *cursor) {
  while (*cursor && isspace((unsigned char)*cursor)) cursor++;
  return cursor;
}

static const char *toml_skip_string(const char *cursor) {
  if (*cursor != '"') return NULL;
  cursor++;
  while (*cursor) {
    if (*cursor == '\\' && cursor[1]) {
      cursor += 2;
      continue;
    }
    if (*cursor == '"') return cursor + 1;
    cursor++;
  }
  return NULL;
}

static void toml_manifest_push_c_lib(ZManifest *manifest, ZManifestCLib lib) {
  manifest->c_libs = z_checked_reallocarray(manifest->c_libs, manifest->c_lib_count + 1, sizeof(ZManifestCLib));
  manifest->c_libs[manifest->c_lib_count++] = lib;
}

static void toml_manifest_push_dependency(ZManifest *manifest, ZManifestDependency dep) {
  manifest->dependencies = z_checked_reallocarray(manifest->dependencies, manifest->dependency_count + 1, sizeof(ZManifestDependency));
  manifest->dependencies[manifest->dependency_count++] = dep;
}

static ZManifestDependency *toml_manifest_dependency_named(ZManifest *manifest, const char *name) {
  if (!manifest || !name || !name[0]) return NULL;
  for (size_t i = 0; i < manifest->dependency_count; i++) {
    if (manifest->dependencies[i].name && strcmp(manifest->dependencies[i].name, name) == 0) return &manifest->dependencies[i];
  }
  ZManifestDependency dep = {0};
  dep.name = z_strdup(name ? name : "");
  dep.version = z_strdup("");
  dep.path = z_strdup("");
  dep.targets_json = z_strdup("[]");
  toml_manifest_push_dependency(manifest, dep);
  return &manifest->dependencies[manifest->dependency_count - 1];
}

static ZManifestCLib *toml_manifest_c_lib_named(ZManifest *manifest, const char *name) {
  if (!manifest || !name || !name[0]) return NULL;
  for (size_t i = 0; i < manifest->c_lib_count; i++) {
    if (manifest->c_libs[i].name && strcmp(manifest->c_libs[i].name, name) == 0) return &manifest->c_libs[i];
  }
  ZManifestCLib lib = {0};
  lib.name = z_strdup(name ? name : "");
  lib.headers_json = z_strdup("[]");
  lib.include_json = z_strdup("[]");
  lib.lib_json = z_strdup("[]");
  lib.link_json = z_strdup("[]");
  lib.mode = z_strdup("static");
  lib.pkg_config = z_strdup("");
  toml_manifest_push_c_lib(manifest, lib);
  return &manifest->c_libs[manifest->c_lib_count - 1];
}

static void toml_manifest_replace_string(char **slot, char *value) {
  if (!slot) {
    free(value);
    return;
  }
  free(*slot);
  *slot = value ? value : z_strdup("");
}

typedef enum {
  TOML_MANIFEST_STRING,
  TOML_MANIFEST_STRING_ARRAY,
  TOML_MANIFEST_STRING_LIST,
} TomlManifestValueKind;

typedef struct {
  const char *field;
  char **slot;
  TomlManifestValueKind kind;
} TomlManifestFieldBinding;

typedef struct {
  char *trimmed;
  char *key;
  char *value;
  char *full;
} TomlParsedLine;

static void toml_parsed_line_free(TomlParsedLine *parsed) {
  if (!parsed) return;
  free(parsed->full);
  free(parsed->value);
  free(parsed->key);
  free(parsed->trimmed);
  memset(parsed, 0, sizeof(*parsed));
}

static void toml_set_diag(ZDiag *diag, int line, const char *message, const char *expected, const char *actual, const char *help) {
  if (!diag) return;
  diag->code = 2002;
  diag->line = line;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "zero.toml parse error");
  snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "valid zero.toml");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "");
  snprintf(diag->help, sizeof(diag->help), "%s", help ? help : "fix zero.toml before using the package");
}

static char *toml_trim_copy(const char *start, size_t len) {
  while (len > 0 && isspace((unsigned char)*start)) {
    start++;
    len--;
  }
  while (len > 0 && isspace((unsigned char)start[len - 1])) len--;
  return z_strndup(start, len);
}

static void toml_strip_comment(char *line) {
  bool in_string = false;
  bool escaped = false;
  for (char *cursor = line; cursor && *cursor; cursor++) {
    if (escaped) {
      escaped = false;
      continue;
    }
    if (in_string && *cursor == '\\') {
      escaped = true;
      continue;
    }
    if (*cursor == '"') {
      in_string = !in_string;
      continue;
    }
    if (!in_string && *cursor == '#') {
      *cursor = 0;
      return;
    }
  }
}

static char *toml_parse_string_copy(const char *value) {
  const char *cursor = toml_skip_ws(value ? value : "");
  if (*cursor != '"') return NULL;
  cursor++;
  ZBuf out;
  zbuf_init(&out);
  while (*cursor) {
    if (*cursor == '"') {
      if (!out.data) zbuf_append(&out, "");
      return out.data;
    }
    if (*cursor == '\\' && cursor[1]) {
      cursor++;
      switch (*cursor) {
        case '"': zbuf_append_char(&out, '"'); break;
        case '\\': zbuf_append_char(&out, '\\'); break;
        case 'n': zbuf_append_char(&out, '\n'); break;
        case 'r': zbuf_append_char(&out, '\r'); break;
        case 't': zbuf_append_char(&out, '\t'); break;
        default: zbuf_append_char(&out, *cursor); break;
      }
      cursor++;
      continue;
    }
    zbuf_append_char(&out, *cursor++);
  }
  zbuf_free(&out);
  return NULL;
}

static bool toml_parse_bool(const char *value, bool *out) {
  const char *start = toml_skip_ws(value ? value : "");
  const char *end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) end--;
  if ((size_t)(end - start) == 4 && strncmp(start, "true", 4) == 0) {
    if (out) *out = true;
    return true;
  }
  if ((size_t)(end - start) == 5 && strncmp(start, "false", 5) == 0) {
    if (out) *out = false;
    return true;
  }
  return false;
}

static void toml_append_json_string(ZBuf *buf, const char *text) {
  zbuf_append_char(buf, '"');
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    switch (*cursor) {
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '"': zbuf_append(buf, "\\\""); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (*cursor < 0x20) zbuf_appendf(buf, "\\u%04x", *cursor);
        else zbuf_append_char(buf, (char)*cursor);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static char *toml_array_to_json(const char *value) {
  const char *cursor = toml_skip_ws(value ? value : "");
  if (*cursor != '[') return z_strdup("[]");
  cursor++;
  ZBuf out;
  zbuf_init(&out);
  zbuf_append_char(&out, '[');
  bool first = true;
  while (*cursor) {
    cursor = toml_skip_ws(cursor);
    if (*cursor == ']') {
      zbuf_append_char(&out, ']');
      return out.data ? out.data : z_strdup("[]");
    }
    char *item = toml_parse_string_copy(cursor);
    if (!item) break;
    if (!first) zbuf_append_char(&out, ',');
    toml_append_json_string(&out, item);
    first = false;
    free(item);
    const char *after_item = toml_skip_string(cursor);
    if (!after_item) break;
    cursor = toml_skip_ws(after_item);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == ']') {
      zbuf_append_char(&out, ']');
      return out.data ? out.data : z_strdup("[]");
    }
    break;
  }
  zbuf_free(&out);
  return z_strdup("[]");
}

static char *toml_string_to_json_array(const char *value) {
  char *item = toml_parse_string_copy(value);
  if (!item) return toml_array_to_json(value);
  ZBuf out;
  zbuf_init(&out);
  zbuf_append_char(&out, '[');
  toml_append_json_string(&out, item);
  zbuf_append_char(&out, ']');
  free(item);
  return out.data ? out.data : z_strdup("[]");
}

static char *toml_full_key(const char *table, const char *key) {
  if (!table || !table[0]) return z_strdup(key ? key : "");
  ZBuf out;
  zbuf_init(&out);
  zbuf_append(&out, table);
  zbuf_append_char(&out, '.');
  zbuf_append(&out, key ? key : "");
  return out.data ? out.data : z_strdup("");
}

static bool toml_split_name_field(const char *suffix, char *name, size_t name_len, const char **field) {
  const char *dot = suffix ? strchr(suffix, '.') : NULL;
  if (!dot || dot == suffix) return false;
  size_t len = (size_t)(dot - suffix);
  if (len >= name_len) len = name_len - 1;
  memcpy(name, suffix, len);
  name[len] = 0;
  if (field) *field = dot + 1;
  return true;
}

static char *toml_value_for_kind(TomlManifestValueKind kind, const char *value) {
  switch (kind) {
    case TOML_MANIFEST_STRING: return toml_parse_string_copy(value);
    case TOML_MANIFEST_STRING_ARRAY: return toml_string_to_json_array(value);
    case TOML_MANIFEST_STRING_LIST: return toml_array_to_json(value);
  }
  return z_strdup("");
}

static bool toml_assign_bound_field(const char *field, const char *value, const TomlManifestFieldBinding *bindings, size_t binding_count) {
  if (!field || !bindings) return false;
  for (size_t i = 0; i < binding_count; i++) {
    if (bindings[i].field && strcmp(field, bindings[i].field) == 0) {
      toml_manifest_replace_string(bindings[i].slot, toml_value_for_kind(bindings[i].kind, value));
      return true;
    }
  }
  return false;
}

static bool toml_set_dependency_field(ZManifest *out, const char *suffix, const char *value) {
  char name[128];
  const char *field = NULL;
  if (!toml_split_name_field(suffix, name, sizeof(name), &field)) {
    ZManifestDependency *dep = toml_manifest_dependency_named(out, suffix);
    char *version = dep ? toml_parse_string_copy(value) : NULL;
    if (version) toml_manifest_replace_string(&dep->version, version);
    return dep != NULL;
  }
  ZManifestDependency *dep = toml_manifest_dependency_named(out, name);
  if (!dep) return false;
  TomlManifestFieldBinding bindings[] = {
    {"path", &dep->path, TOML_MANIFEST_STRING},
    {"version", &dep->version, TOML_MANIFEST_STRING},
    {"targets", &dep->targets_json, TOML_MANIFEST_STRING_LIST},
    {"target", &dep->targets_json, TOML_MANIFEST_STRING_ARRAY},
  };
  toml_assign_bound_field(field, value, bindings, sizeof(bindings) / sizeof(bindings[0]));
  return true;
}

static bool toml_set_c_lib_field(ZManifest *out, const char *suffix, const char *value) {
  char name[128];
  const char *field = NULL;
  if (!toml_split_name_field(suffix, name, sizeof(name), &field)) return true;
  ZManifestCLib *lib = toml_manifest_c_lib_named(out, name);
  if (!lib) return false;
  TomlManifestFieldBinding bindings[] = {
    {"headers", &lib->headers_json, TOML_MANIFEST_STRING_LIST},
    {"include", &lib->include_json, TOML_MANIFEST_STRING_LIST},
    {"lib", &lib->lib_json, TOML_MANIFEST_STRING_LIST},
    {"link", &lib->link_json, TOML_MANIFEST_STRING_LIST},
    {"mode", &lib->mode, TOML_MANIFEST_STRING},
    {"pkg_config", &lib->pkg_config, TOML_MANIFEST_STRING},
    {"pkgConfig", &lib->pkg_config, TOML_MANIFEST_STRING},
  };
  toml_assign_bound_field(field, value, bindings, sizeof(bindings) / sizeof(bindings[0]));
  return true;
}

static bool toml_key_has_prefix(const char *key, const char *prefix, const char **suffix) {
  if (!key || !prefix) return false;
  size_t len = strlen(prefix);
  if (strncmp(key, prefix, len) != 0) return false;
  if (suffix) *suffix = key + len;
  return true;
}

bool z_parse_manifest_toml(const char *manifest, ZManifest *out, ZDiag *diag) {
  if (!out) return false;
  memset(out, 0, sizeof(*out));
  char *copy = z_strdup(manifest ? manifest : "");
  char table[256] = {0};
  int line_number = 0;
  char *line = copy;
  while (line) {
    line_number++;
    char *next = strchr(line, '\n');
    if (next) *next++ = 0;
    toml_strip_comment(line);
    char *trimmed = toml_trim_copy(line, strlen(line));
    if (!trimmed[0]) {
      free(trimmed);
      line = next;
      continue;
    }
    if (trimmed[0] == '[') {
      char *end = strchr(trimmed, ']');
      if (!end) {
        toml_set_diag(diag, line_number, "zero.toml table header is not closed", "[table]", trimmed, "close the TOML table header before adding fields");
        free(trimmed);
        free(copy);
        return false;
      }
      char *name = toml_trim_copy(trimmed + 1, (size_t)(end - trimmed - 1));
      snprintf(table, sizeof(table), "%s", name);
      free(name);
      free(trimmed);
      line = next;
      continue;
    }
    char *equals = strchr(trimmed, '=');
    if (!equals) {
      free(trimmed);
      line = next;
      continue;
    }
    TomlParsedLine parsed = {0};
    parsed.trimmed = trimmed;
    parsed.key = toml_trim_copy(trimmed, (size_t)(equals - trimmed));
    parsed.value = toml_trim_copy(equals + 1, strlen(equals + 1));
    parsed.full = toml_full_key(table, parsed.key);

    TomlManifestFieldBinding fields[] = {
      {"package.name", &out->package_name, TOML_MANIFEST_STRING},
      {"package.version", &out->package_version, TOML_MANIFEST_STRING},
      {"targets.cli.main", &out->main_path, TOML_MANIFEST_STRING},
      {"targets.cli.graph", &out->graph_path, TOML_MANIFEST_STRING},
      {"targets.cli.kind", &out->kind, TOML_MANIFEST_STRING},
    };
    const char *suffix = NULL;
    bool handled = toml_assign_bound_field(parsed.full, parsed.value, fields, sizeof(fields) / sizeof(fields[0]));
    if (!handled && strcmp(parsed.full, "repositoryGraph.compilerInput") == 0) {
      bool bool_value = false;
      if (!toml_parse_bool(parsed.value, &bool_value)) {
        toml_set_diag(diag, line_number, "repositoryGraph.compilerInput must be a boolean", "true or false", parsed.value, "remove repositoryGraph.compilerInput or set it to a boolean compatibility value");
        toml_parsed_line_free(&parsed);
        free(copy);
        return false;
      }
      out->repository_graph_compiler_input_present = true;
      out->repository_graph_compiler_input = bool_value;
      handled = true;
    }
    if (!handled && toml_key_has_prefix(parsed.full, "deps.", &suffix)) {
      toml_set_dependency_field(out, suffix, parsed.value);
    } else if (!handled && toml_key_has_prefix(parsed.full, "dependencies.", &suffix)) {
      toml_set_dependency_field(out, suffix, parsed.value);
    } else if (!handled && toml_key_has_prefix(parsed.full, "c.libs.", &suffix)) {
      toml_set_c_lib_field(out, suffix, parsed.value);
    }

    toml_parsed_line_free(&parsed);
    line = next;
  }
  free(copy);
  return true;
}
