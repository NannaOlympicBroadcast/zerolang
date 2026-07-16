#include "program_graph_check_gate.h"
#include <string.h>

static bool check_gate_text_eq(const char *left, const char *right) {
  return left && right && strcmp(left, right) == 0;
}

/*
 * Check-time buildability gate facts: `zero check` and `zero import` fail
 * with the same BLD004 "typed graph MIR unsupported" diagnostics `zero
 * build`/`zero run` would report when the typed graph cannot lower to MIR at
 * all. Target-specific backend buildability subsets (for example fs values
 * on the Mach-O direct backend), target/backend availability, and toolchain
 * readiness stay target-readiness facts only, so packages that build for
 * another supported target keep checking clean on every host.
 */
bool z_check_gate_diag_is_buildability_blocker(const ZDiag *diag) {
  if (!diag) return false;
  if (diag->code != 2004 && diag->code != 4004) return false;
  if (!diag->backend_blocker.present) return false;
  return check_gate_text_eq(diag->backend_blocker.stage, "lower");
}

static bool check_gate_type_decl_is_enum_or_choice(const ZProgramGraph *graph, const char *name) {
  if (!graph || !name || !name[0]) return false;
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_ENUM && node->kind != Z_PROGRAM_GRAPH_NODE_CHOICE) continue;
    if (check_gate_text_eq(node->name, name)) return true;
  }
  return false;
}

static bool check_gate_is_non_entry_world_param(const ZProgramGraphNode *node) {
  return check_gate_text_eq(node ? node->type : NULL, "World") &&
         node->symbol_id && !strstr(node->symbol_id, "::value.main/param.");
}

/*
 * The gate starts with the typed graph MIR gaps agents hit most often, so
 * conformance check-pass coverage for the remaining gaps stays valid while
 * each gap either gains lowering support or migrates to a check-failure
 * contract. Every gated construct fails `zero build` identically today; the
 * list only controls how much of that build signal check surfaces early.
 */
bool z_check_gate_diag_is_known_construct(const ZProgramGraph *graph, const ZDiag *diag) {
  if (!diag) return false;
  const char *message = diag->message;
  const char *actual = diag->actual;
  if (check_gate_text_eq(message, "typed graph MIR statement kind is unsupported")) return check_gate_text_eq(actual, "Defer");
  if (check_gate_text_eq(message, "typed graph MIR local type is unsupported")) return check_gate_type_decl_is_enum_or_choice(graph, actual);
  if (check_gate_text_eq(message, "typed graph MIR rescue supports fallible function calls with primitive fallbacks")) return true;
  if (check_gate_text_eq(message, "typed graph MIR literal type is unsupported")) return check_gate_text_eq(actual, "String");
  if (check_gate_text_eq(message, "typed graph MIR fallible return type is unsupported")) return check_gate_text_eq(actual, "String");
  if (check_gate_text_eq(message, "typed graph MIR parameter type is unsupported")) return check_gate_text_eq(actual, "ref<ByteBuf>") || check_gate_text_eq(actual, "World");
  if (check_gate_text_eq(message, "typed graph MIR reference parameter requires a shape whose fields are scalars or fixed scalar arrays")) return check_gate_text_eq(actual, "ref<ByteBuf>");
  if (check_gate_text_eq(message, "typed graph MIR call target is unsupported")) return check_gate_text_eq(actual, "readU32");
  if (check_gate_text_eq(message, "typed graph MIR allocator local requires FixedBufAlloc")) return check_gate_text_eq(actual, "PageAlloc") || check_gate_text_eq(actual, "GeneralAlloc");
  return false;
}

/*
 * Fast filter: the gate only pays for merged lowering when the graph can
 * contain a gated construct at all. One node pass keeps construct-free
 * programs at pre-gate check and import cost.
 */
bool z_check_gate_scan_finds_known_construct(const ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    switch (node->kind) {
      case Z_PROGRAM_GRAPH_NODE_DEFER:
      case Z_PROGRAM_GRAPH_NODE_RESCUE:
      case Z_PROGRAM_GRAPH_NODE_ENUM:
      case Z_PROGRAM_GRAPH_NODE_CHOICE:
        return true;
      case Z_PROGRAM_GRAPH_NODE_LET:
        if (check_gate_text_eq(node->type, "PageAlloc") || check_gate_text_eq(node->type, "GeneralAlloc")) return true;
        break;
      case Z_PROGRAM_GRAPH_NODE_PARAM:
        if (check_gate_text_eq(node->type, "ref<ByteBuf>") || check_gate_is_non_entry_world_param(node)) return true;
        break;
      case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
      case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
        if (check_gate_text_eq(node->name, "readU32")) return true;
        break;
      default:
        break;
    }
  }
  return false;
}
