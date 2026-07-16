#include "program_graph_repository_repair.h"

#include <stdlib.h>

static char *repair_command_text(const char *command, const char *input) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, command);
  zbuf_append_char(&buf, ' ');
  zbuf_append(&buf, input && input[0] ? input : ".");
  return buf.data;
}

static void repair_append_json_string(ZBuf *buf, const char *text) {
  zbuf_append_char(buf, '"');
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
    switch (*p) {
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '"': zbuf_append(buf, "\\\""); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (*p < 0x20) zbuf_appendf(buf, "\\u%04x", *p);
        else zbuf_append_char(buf, (char)*p);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static void repair_append_json_command(ZBuf *buf, bool *first, const char *command, const char *input) {
  char *text = repair_command_text(command, input);
  if (!*first) zbuf_append(buf, ", ");
  repair_append_json_string(buf, text);
  *first = false;
  free(text);
}

void z_repository_graph_append_repair_commands_json(ZBuf *buf, const char *input, ZRepositoryGraphRepair repair) {
  zbuf_append(buf, "[");
  bool first = true;
  if (repair == REPO_GRAPH_REPAIR_FROM_SOURCE) {
    repair_append_json_command(buf, &first, "zero import", input);
  } else if (repair == REPO_GRAPH_REPAIR_FROM_GRAPH) {
    repair_append_json_command(buf, &first, "zero export", input);
  } else if (repair == REPO_GRAPH_REPAIR_IMPORT_OR_EXPORT) {
    repair_append_json_command(buf, &first, "zero import", input);
    repair_append_json_command(buf, &first, "zero export", input);
  } else if (repair == REPO_GRAPH_REPAIR_STATUS) {
    repair_append_json_command(buf, &first, "zero status", input);
  }
  zbuf_append(buf, "]");
}

static void repair_print_command(FILE *stream, const char *command, const char *input) {
  char *text = repair_command_text(command, input);
  fprintf(stream, "  repair: %s\n", text);
  free(text);
}

void z_repository_graph_print_repair_commands(FILE *stream, const char *input, ZRepositoryGraphRepair repair) {
  if (!stream || repair == REPO_GRAPH_REPAIR_NONE) return;
  if (repair == REPO_GRAPH_REPAIR_FROM_SOURCE) {
    repair_print_command(stream, "zero import", input);
  } else if (repair == REPO_GRAPH_REPAIR_FROM_GRAPH) {
    repair_print_command(stream, "zero export", input);
  } else if (repair == REPO_GRAPH_REPAIR_IMPORT_OR_EXPORT) {
    repair_print_command(stream, "zero import", input);
    repair_print_command(stream, "zero export", input);
  } else if (repair == REPO_GRAPH_REPAIR_STATUS) {
    repair_print_command(stream, "zero status", input);
  }
}
