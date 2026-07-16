## From Graph To Code

Zerolang is easiest to understand against parse-first compilers. Most languages
have a parse-first compile path: the normal compiler input is text, so every
compile starts by parsing text into compiler data structures. Zerolang's
package path is graph-first. It is the same kind of pipeline with fewer stages,
and the input is the graph store instead of text:

```json-render
{
  "type": "flow",
  "title": "Parse-first compile path vs Zero graph-first path",
  "nodes": [
    { "id": "p1", "label": "source files", "x": 0, "y": 0, "tone": "text" },
    { "id": "p2", "label": "lexer / parser", "x": 0, "y": 96, "tone": "text" },
    { "id": "p3", "label": "AST", "x": 0, "y": 192, "tone": "text" },
    { "id": "p4", "label": "name resolution", "x": 0, "y": 288, "tone": "text" },
    { "id": "p5", "label": "type checking", "x": 0, "y": 384, "tone": "text" },
    { "id": "p6", "label": "IR lowering", "x": 0, "y": 480, "tone": "text" },
    { "id": "p7", "label": "optimization", "x": 0, "y": 576, "tone": "text" },
    { "id": "p8", "label": "codegen", "x": 0, "y": 672, "tone": "text" },
    { "id": "p9", "label": "artifact", "x": 0, "y": 768, "tone": "text" },

    { "id": "g1", "label": "zero.graph", "x": 380, "y": 0, "tone": "graph" },
    { "id": "g2", "label": "repository graph tables", "x": 380, "y": 96, "tone": "graph" },
    { "id": "g3", "label": "semantic validation", "x": 380, "y": 192, "tone": "compiler" },
    { "id": "g4", "label": "type checking", "x": 380, "y": 288, "tone": "compiler" },
    { "id": "g5", "label": "MIR and backend facts", "x": 380, "y": 384, "tone": "graph" },
    { "id": "g6", "label": "direct codegen", "x": 380, "y": 480, "tone": "compiler" },
    { "id": "g7", "label": "artifact", "x": 380, "y": 576, "tone": "graph" }
  ],
  "edges": [
    { "source": "p1", "target": "p2" },
    { "source": "p2", "target": "p3" },
    { "source": "p3", "target": "p4" },
    { "source": "p4", "target": "p5" },
    { "source": "p5", "target": "p6" },
    { "source": "p6", "target": "p7" },
    { "source": "p7", "target": "p8" },
    { "source": "p8", "target": "p9" },
    { "source": "g1", "target": "g2" },
    { "source": "g2", "target": "g3" },
    { "source": "g3", "target": "g4" },
    { "source": "g4", "target": "g5" },
    { "source": "g5", "target": "g6" },
    { "source": "g6", "target": "g7" }
  ]
}
```

Rust, Go, Zig, C, and many other languages differ in details, but the normal
compiler input is text. The `.0` projection can be exported and imported, but it
is not the normal package compile input. The compiler loads the graph store
directly.

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "why is this less work for the agent?"
    },
    {
      "role": "assistant",
      "text": "The agent can ask the compiler for graph facts, submit one checked semantic patch, and avoid a separate write-format-parse cycle. The compiler still checks and builds, but the edit itself is closer to the final compiler model."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero query --calls write",
          "output": "call write\n  receiver: world.out\n  arg0: #expr_653eeb6e String"
        },
        {
          "command": "zero patch --op 'set node=\"#expr_653eeb6e\" field=\"value\" expect=\"old\\n\" value=\"new\\n\"'",
          "output": "program graph patch ok"
        }
      ]
    }
  ]
}
```

## Why The Path Matters

For agents, the compile path is also the authoring path. If text is primary,
the agent writes text and waits for the compiler to tell it whether the text
meant what it intended.

If the graph is primary, the agent can ask the compiler for node handles,
symbol facts, calls, references, diagnostics, and patch operations before it
edits. The edit is already expressed in compiler terms.

That is why Zero keeps investing in binary graph storage and direct graph
loading. The long-term goal is to memory-map final compiler IR and codegen from
semantic data with as little redundant parsing and reconstruction as possible.

## Performance And Size Angle

The graph model is not only about agent ergonomics. It also supports Zero's
systems goals:

- fewer reparsed text inputs on normal package commands
- deterministic graph identity and stable diff/review output
- pay-as-used standard library helpers
- explicit capability and allocation facts
- small direct artifacts when the selected profile and target support them

Use `zero size --json`, `zero mem --json`, and the benchmark docs to inspect
those facts for a graph input instead of treating performance claims as prose.

## Current Boundary

Zero is still experimental. Some commands and targets expose readiness facts or
structured diagnostics when a backend cannot build a graph shape yet. That is
intentional: the docs should show what works today and what the compiler can
explain, not imply production completeness.
