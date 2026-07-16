#include "init_template.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void init_append_json_string(ZBuf *buf, const char *text) {
  zbuf_append_char(buf, '"');
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    switch (*cursor) {
      case '"': zbuf_append(buf, "\\\""); break;
      case '\\': zbuf_append(buf, "\\\\"); break;
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

static char *init_join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left ? left : "");
  if (buf.len > 0 && buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right ? right : "");
  return buf.data;
}

static bool init_path_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static bool init_write_project_file(const char *root, const char *relative, const char *text, ZDiag *diag) {
  char *path = init_join_path(root, relative);
  bool ok = z_write_file(path, text, diag);
  free(path);
  return ok;
}

static bool init_write_project_buf(const char *root, const char *relative, ZBuf *buf, ZDiag *diag) {
  bool ok = init_write_project_file(root, relative, buf->data ? buf->data : "", diag);
  zbuf_free(buf);
  return ok;
}

static void append_template_manifest_json(ZBuf *buf, const char *name, const char *main_path) {
  zbuf_append(buf, "{\n");
  zbuf_append(buf, "  \"package\": {\n");
  zbuf_append(buf, "    \"name\": ");
  init_append_json_string(buf, name);
  zbuf_append(buf, ",\n    \"version\": \"0.1.0\",\n    \"license\": \"MIT\"\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"targets\": {\n");
  zbuf_append(buf, "    \"cli\": {\n");
  zbuf_append(buf, "      \"kind\": \"exe\",\n");
  zbuf_append(buf, "      \"main\": ");
  init_append_json_string(buf, main_path);
  zbuf_append(buf, ",\n      \"defaultTarget\": \"linux-musl-x64\",\n      \"devTarget\": \"host\",\n      \"releaseProfile\": \"release-small\"\n    }\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"deps\": {},\n");
  zbuf_append(buf, "  \"profiles\": {\n");
  zbuf_append(buf, "    \"dev\": { \"inherits\": \"dev\" },\n");
  zbuf_append(buf, "    \"release-small\": { \"inherits\": \"release-small\" }\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"docs\": {\n");
  zbuf_append(buf, "    \"readme\": \"README.md\",\n");
  zbuf_append(buf, "    \"examples\": [");
  init_append_json_string(buf, main_path);
  zbuf_append(buf, "]\n");
  zbuf_append(buf, "  }\n");
  zbuf_append(buf, "}\n");
}

static void append_graph_first_manifest_json(ZBuf *buf, const char *name, const char *main_path) {
  zbuf_append(buf, "{\n");
  zbuf_append(buf, "  \"package\": {\n");
  zbuf_append(buf, "    \"name\": ");
  init_append_json_string(buf, name);
  zbuf_append(buf, ",\n    \"version\": \"0.1.0\",\n    \"license\": \"MIT\"\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"targets\": {\n");
  zbuf_append(buf, "    \"cli\": {\n");
  zbuf_append(buf, "      \"kind\": \"exe\",\n");
  zbuf_append(buf, "      \"main\": ");
  init_append_json_string(buf, main_path);
  zbuf_append(buf, ",\n      \"defaultTarget\": \"linux-musl-x64\",\n      \"devTarget\": \"host\",\n      \"releaseProfile\": \"release-small\"\n    }\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"deps\": {},\n");
  zbuf_append(buf, "  \"profiles\": {\n");
  zbuf_append(buf, "    \"dev\": { \"inherits\": \"dev\" },\n");
  zbuf_append(buf, "    \"release-small\": { \"inherits\": \"release-small\" }\n");
  zbuf_append(buf, "  }\n");
  zbuf_append(buf, "}\n");
}

static void append_graph_first_manifest_toml(ZBuf *buf, const char *name, const char *main_path) {
  zbuf_append(buf, "[package]\n");
  zbuf_append(buf, "name = ");
  init_append_json_string(buf, name);
  zbuf_append(buf, "\nversion = \"0.1.0\"\nlicense = \"MIT\"\n\n");
  zbuf_append(buf, "[targets.cli]\n");
  zbuf_append(buf, "kind = \"exe\"\nmain = ");
  init_append_json_string(buf, main_path);
  zbuf_append(buf, "\ndefaultTarget = \"linux-musl-x64\"\ndevTarget = \"host\"\nreleaseProfile = \"release-small\"\n\n");
  zbuf_append(buf, "[deps]\n\n");
  zbuf_append(buf, "[profiles.dev]\n");
  zbuf_append(buf, "inherits = \"dev\"\n\n");
  zbuf_append(buf, "[profiles.release-small]\n");
  zbuf_append(buf, "inherits = \"release-small\"\n");
}

static const char *init_manifest_file_name(const char *manifest_format, const char *root, ZDiag *diag) {
  const char *format = manifest_format && manifest_format[0] ? manifest_format : "toml";
  if (strcmp(format, "json") == 0) return "zero.json";
  if (strcmp(format, "toml") == 0) return "zero.toml";
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 2002;
    diag->path = root ? root : ".";
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "manifest format is not supported");
    snprintf(diag->expected, sizeof(diag->expected), "--manifest toml|json");
    snprintf(diag->actual, sizeof(diag->actual), "%s", format ? format : "");
    snprintf(diag->help, sizeof(diag->help), "omit --manifest for zero.toml, or pass --manifest json for compatibility metadata");
  }
  return NULL;
}

bool z_init_write_manifest(const char *root, const char *name, const char *main_path, const char *manifest_format, bool template_manifest, const char **manifest_file_name_out, ZDiag *diag) {
  const char *manifest_file_name = init_manifest_file_name(manifest_format, root, diag);
  if (!manifest_file_name) return false;
  ZBuf manifest;
  zbuf_init(&manifest);
  if (strcmp(manifest_file_name, "zero.toml") == 0) append_graph_first_manifest_toml(&manifest, name, main_path);
  else if (template_manifest) append_template_manifest_json(&manifest, name, main_path);
  else append_graph_first_manifest_json(&manifest, name, main_path);
  if (!init_write_project_buf(root, manifest_file_name, &manifest, diag)) return false;
  if (manifest_file_name_out) *manifest_file_name_out = manifest_file_name;
  return true;
}

bool z_init_template_kind_is_known(const char *kind) {
  return kind && (strcmp(kind, "cli") == 0 || strcmp(kind, "lib") == 0 || strcmp(kind, "package") == 0);
}

const char *z_init_template_projection_path(const char *kind) {
  return kind && strcmp(kind, "lib") == 0 ? "src/lib.0" : "src/main.0";
}

static bool init_template_reject_existing_path(const char *root, const char *relative, ZDiag *diag) {
  char *path = init_join_path(root, relative);
  if (init_path_exists(path)) {
    diag->code = 2002;
    diag->path = path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "template output already exists");
    snprintf(diag->expected, sizeof(diag->expected), "project path without generated template files");
    snprintf(diag->actual, sizeof(diag->actual), "%s", path);
    snprintf(diag->help, sizeof(diag->help), "choose a new project path or remove the conflicting file");
    return false;
  }
  free(path);
  return true;
}

bool z_init_template_reject_existing_outputs(const char *root, const char *kind, ZDiag *diag) {
  if (!init_template_reject_existing_path(root, "README.md", diag)) return false;
  if (!init_template_reject_existing_path(root, ".gitignore", diag)) return false;
  if (strcmp(kind, "cli") == 0) {
    if (!init_template_reject_existing_path(root, "src/main.0", diag)) return false;
    if (!init_template_reject_existing_path(root, "src/lib.0", diag)) return false;
  } else if (strcmp(kind, "lib") == 0) {
    if (!init_template_reject_existing_path(root, "src/lib.0", diag)) return false;
  } else if (strcmp(kind, "package") == 0) {
    if (!init_template_reject_existing_path(root, "src/main.0", diag)) return false;
    if (!init_template_reject_existing_path(root, "src/model.0", diag)) return false;
    if (!init_template_reject_existing_path(root, "src/math.0", diag)) return false;
  }
  return true;
}

static bool create_cli_template(const char *root, const char *name, const char *manifest_format, const char **manifest_file_name, ZDiag *diag) {
  if (!z_init_write_manifest(root, name, "src/main.0", manifest_format, true, manifest_file_name, diag)) return false;
  if (!init_write_project_file(root, "src/lib.0",
    "pub fn greeting_code() -> i32 {\n"
    "    return 42\n"
    "}\n",
    diag)) return false;
  if (!init_write_project_file(root, "src/main.0",
    "use lib\n\n"
    "pub fn main(world: World) -> Void raises {\n"
    "    if greeting_code() == 42 {\n"
    "        check world.out.write(\"hello from zero\\n\")\n"
    "    }\n"
    "}\n\n"
    "test \"greeting is stable\" {\n"
    "    expect greeting_code() == 42\n"
    "}\n",
    diag)) return false;
  if (!init_write_project_file(root, "README.md",
    "# Zero CLI\n\n"
    "This project was created with `zero init --template cli`.\n\n"
    "Try:\n\n"
    "```sh\n"
    "zero check\n"
    "zero test\n"
    "zero run\n"
    "zero dev --json\n"
    "zero build --target linux-musl-x64 --out .zero/out/app\n"
    "```\n\n"
    "The entry point receives `World` explicitly, so I/O is visible in the function signature. The generated output is deterministic and the manifest records the default release target.\n",
    diag)) return false;
  return init_write_project_file(root, ".gitignore", ".zero/\n", diag);
}

static bool create_lib_template(const char *root, const char *name, const char *manifest_format, const char **manifest_file_name, ZDiag *diag) {
  if (!z_init_write_manifest(root, name, "src/lib.0", manifest_format, true, manifest_file_name, diag)) return false;
  if (!init_write_project_file(root, "src/lib.0",
    "pub fn add_one(value: i32) -> i32 {\n"
    "    return value + 1\n"
    "}\n\n"
    "test \"public api works\" {\n"
    "    expect add_one(41) == 42\n"
    "}\n",
    diag)) return false;
  if (!init_write_project_file(root, "README.md",
    "# Zero Library\n\n"
    "This small package exposes one public function, package metadata, and an inline test.\n\n"
    "Try:\n\n"
    "```sh\n"
    "zero check\n"
    "zero test\n"
    "zero dev --json\n"
    "zero inspect --json\n"
    "zero doc --json\n"
    "```\n",
    diag)) return false;
  return init_write_project_file(root, ".gitignore", ".zero/\n", diag);
}

static bool create_package_template(const char *root, const char *name, const char *manifest_format, const char **manifest_file_name, ZDiag *diag) {
  if (!z_init_write_manifest(root, name, "src/main.0", manifest_format, true, manifest_file_name, diag)) return false;
  if (!init_write_project_file(root, "src/model.0",
    "pub type Point {\n"
    "    value: i32,\n"
    "}\n",
    diag)) return false;
  if (!init_write_project_file(root, "src/math.0",
    "fn base(value: i32) -> i32 {\n"
    "    return value\n"
    "}\n\n"
    "pub fn add_one(value: i32) -> i32 {\n"
    "    return base(value) + 1\n"
    "}\n",
    diag)) return false;
  if (!init_write_project_file(root, "src/main.0",
    "use math\n"
    "\n"
    "use model\n\n"
    "pub fn main(world: World) -> Void raises {\n"
    "    let point: Point = Point { value: add_one(41) }\n"
    "    if point.value == 42 {\n"
    "        check world.out.write(\"package ok\\n\")\n"
    "    }\n"
    "}\n\n"
    "test \"package import works\" {\n"
    "    expect add_one(41) == 42\n"
    "}\n",
    diag)) return false;
  if (!init_write_project_file(root, "README.md",
    "# Zero Package\n\n"
    "This template shows package-local imports, one public symbol, and one private helper.\n\n"
    "Try:\n\n"
    "```sh\n"
    "zero check\n"
    "zero test\n"
    "zero run\n"
    "zero dev --json\n"
    "zero build --target linux-musl-x64 --out .zero/out/app\n"
    "zero inspect --json\n"
    "```\n",
    diag)) return false;
  return init_write_project_file(root, ".gitignore", ".zero/\n", diag);
}

bool z_init_template_write_files(const char *root, const char *kind, const char *name, const char *manifest_format, const char **manifest_file_name, ZDiag *diag) {
  if (strcmp(kind, "cli") == 0) return create_cli_template(root, name, manifest_format, manifest_file_name, diag);
  if (strcmp(kind, "lib") == 0) return create_lib_template(root, name, manifest_format, manifest_file_name, diag);
  if (strcmp(kind, "package") == 0) return create_package_template(root, name, manifest_format, manifest_file_name, diag);
  return false;
}
