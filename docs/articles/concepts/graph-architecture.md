## The Program Database

Zerolang exists because humans increasingly ask agents to write programs.

Most programming languages still make text the primary program database. That
works for humans, but it is a poor interface for agents. An agent has to infer
semantic structure from text, make a text edit, run tools to learn whether the
edit was valid, format the result, and then inspect failures after the fact.

In Zero, the graph is the program database. The graph stores declarations,
types, calls, blocks, imports, capabilities, and source-map facts directly.
Agents edit those facts with checked graph patches. Humans read `.0`
projections when they want a source-like review view.

## The Editing Loop

A traditional agent loop writes text, then runs check, format, and build to find
out what the text meant. Zero's loop queries the graph, submits one checked
patch, and only runs the validation a task actually needs:

```json-render
{
  "type": "flow",
  "title": "Traditional source loop vs Zero graph loop",
  "height": 520,
  "nodes": [
    { "id": "t1", "label": "agent writes text", "x": 0, "y": 0, "tone": "text" },
    { "id": "t2", "label": "check", "x": 0, "y": 90, "tone": "compiler" },
    { "id": "t3", "label": "format", "x": 0, "y": 180, "tone": "text" },
    { "id": "t4", "label": "build", "x": 0, "y": 270, "tone": "compiler" },
    { "id": "t5", "label": "inspect failures", "x": 0, "y": 360, "tone": "human" },
    { "id": "z1", "label": "agent queries graph", "x": 360, "y": 0, "tone": "graph" },
    { "id": "z2", "label": "agent submits checked patch", "x": 360, "y": 105, "tone": "graph" },
    { "id": "z3", "label": "compiler rejects invalid graph edits immediately", "x": 360, "y": 210, "tone": "compiler" },
    { "id": "z4", "label": "agent runs only task validation", "x": 360, "y": 315, "tone": "graph" },
    { "id": "z5", "label": "human reviews projection when useful", "x": 360, "y": 420, "tone": "human" }
  ],
  "edges": [
    { "source": "t1", "target": "t2" },
    { "source": "t2", "target": "t3" },
    { "source": "t3", "target": "t4" },
    { "source": "t4", "target": "t5" },
    { "source": "t5", "target": "t1", "label": "repeat" },
    { "source": "z1", "target": "z2" },
    { "source": "z2", "target": "z3" },
    { "source": "z3", "target": "z4" },
    { "source": "z4", "target": "z5" }
  ]
}
```

The difference is not just syntax. A graph patch can target “the literal
argument to this write call” or “the body of this block” instead of asking an
agent to locate and rewrite a span of text.

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "change the greeting to hello graph"
    },
    {
      "role": "assistant",
      "text": "I’ll patch the greeting and run the program so you can see the output."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero query --fn main",
          "output": "main\n  check world.out.write \"hello from zero\\n\"\n  graphHash graph:a7f7e6899a73f3b4"
        },
        {
          "command": "zero patch --expect-graph-hash graph:a7f7e6899a73f3b4 --op 'set node=\"#expr_653eeb6e\" field=\"value\" expect=\"hello from zero\\n\" value=\"hello graph\\n\"'",
          "output": "program graph patch ok"
        },
        {
          "command": "zero run",
          "output": "hello graph"
        }
      ]
    }
  ]
}
```

## Invalid Edits Fail Earlier

The graph store has shape rules. Required edges, ordered child groups, node
kinds, type facts, and repository metadata are validated when patches are
applied. If an edit would leave a sparse argument list, a missing expression, a
stale graph hash, or an invalid repository store, the patch fails before the
package becomes the new compiler input.

That is the agent-facing contract: write checked semantic edits, not hopeful
text diffs.

## Human Review Stays Textual

Humans should not have to inspect graph dumps to trust a change. `.0`
projections exist so people can read, review, and occasionally manually edit a
program.

The important distinction is ownership:

- agents normally author through `zero query` and `zero patch`
- humans review through projections
- humans may edit projections as an escape hatch
- `zero import` reconstructs the graph from reviewed projection text
- `zero verify-projection` catches drift instead of hiding it

Zero is a graph-native language with human-editable text projections.

## The Payoff

The graph-first model is meant to reduce guessing and reduce tool calls. A
checked patch combines edit intent, stale-state protection, shape validation,
and formatting-normalized projection output into one compiler-mediated step.

That gives agents a smaller, more precise work surface. It gives humans a
reviewable source-like view. It gives the compiler a direct path to semantic
program facts without reparsing text on the normal package compile path.
