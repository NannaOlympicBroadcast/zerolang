#include "program_graph_patch.h"

#include <stddef.h>

static const char *const graph_patch_authoring_operation_examples[] = {
  "addMain",
  "addCheckWrite fn=\"main\" text=\"hello\\n\"",
  "addFunction name=\"add\" ret=\"i32\"",
  "addParam fn=\"add\" name=\"left\" type=\"i32\"",
  "addParamTo fn=\"add\" name=\"bias\" type=\"i32\" default=\"0\"",
  "setConst name=\"limit\" value=\"64\"",
  "setReturnType fn=\"add\" type=\"i64\"",
  "addReturnBinary fn=\"add\" name=\"+\" left=\"left\" right=\"right\" type=\"i32\"",
  "addLetLiteral fn=\"main\" name=\"count\" type=\"u32\" value=\"0\"",
  "addLetBinary fn=\"add\" name=\"sum\" type=\"i32\" operator=\"+\" left=\"left\" right=\"right\"",
  "addReturnValue fn=\"identity\" value=\"input\" type=\"i32\"",
  "addReturnExpr fn=\"maybe\" expr=\"null\"",
  "appendStmt fn=\"main\" stmt=\"check std.http.listen(world, 3000_u16)\"",
  "addCheckWriteValue fn=\"main\" value=\"message\" type=\"String\"",
  "addTest name=\"addition works\" call=\"add\" arg0=\"40\" arg1=\"2\" expect=\"42\" type=\"i32\"",
  "addTestBody name=\"api add\"\n  expect apiAddOk()\nend",
  "renameTest name=\"api add\" value=\"api add route\"",
  "deleteTest name=\"api add\"",
  "upsertFunction handle\nfn handle(request: Span<u8>, response: MutSpan<u8>) -> Maybe<Span<u8>> {\n    return null\n}\nend",
  "replaceFunctionBody main\n  let name Maybe<String> = std.args.get 1\n  if name.has\n    check world.out.write \"hello \"\n    check world.out.write name.value\n    check world.out.write \"\\n\"\n  else\n    check world.out.write \"hello anonymous\\n\"\nend",
  "replaceBlockBody #block_id\n  check world.out.write \"updated\\n\"\nend",
  NULL,
};

static const char *const graph_patch_node_operation_examples[] = {
  "expect graphHash \"graph:a7f7e6899a73f3b4\"",
  "set node=\"#id\" field=\"value\" expect=\"old\" value=\"new\"",
  "insert node=\"#id\" kind=\"Literal\" parent=\"#parent\" edge=\"arg\" order=\"0\" type=\"String\" value=\"text\"",
  "insertEdge from=\"#from\" to=\"#to\" edge=\"arg\" target=\"node\" order=\"0\"",
  "replace node=\"#id\" expect=\"nodehash:abc123\" kind=\"Literal\" type=\"String\" value=\"text\"",
  "replaceExpr node=\"#id\" with=\"left + 1\"",
  "delete node=\"#id\" expect=\"nodehash:abc123\"",
  "delete node=\"#id\"",
  "rename node=\"#id\" expect=\"old\" value=\"new\"",
  NULL,
};

enum {
  GRAPH_PATCH_AUTHORING_EXAMPLE_LEN = sizeof(graph_patch_authoring_operation_examples) / sizeof(graph_patch_authoring_operation_examples[0]),
  GRAPH_PATCH_NODE_EXAMPLE_LEN = sizeof(graph_patch_node_operation_examples) / sizeof(graph_patch_node_operation_examples[0]),
};

const char *const *z_program_graph_patch_authoring_operation_examples(void) {
  return graph_patch_authoring_operation_examples;
}

const char *const *z_program_graph_patch_node_operation_examples(void) {
  return graph_patch_node_operation_examples;
}

const char *const *z_program_graph_patch_operation_examples(void) {
  static const char *combined[GRAPH_PATCH_AUTHORING_EXAMPLE_LEN + GRAPH_PATCH_NODE_EXAMPLE_LEN] = {0};
  if (!combined[0]) {
    size_t len = 0;
    for (size_t i = 0; graph_patch_authoring_operation_examples[i]; i++) combined[len++] = graph_patch_authoring_operation_examples[i];
    for (size_t i = 0; graph_patch_node_operation_examples[i]; i++) combined[len++] = graph_patch_node_operation_examples[i];
    combined[len] = NULL;
  }
  return (const char *const *)combined;
}
