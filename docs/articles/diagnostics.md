## Read The Error As A Repair Contract

Zerolang diagnostics are written for humans and agents together. A human should see what
went wrong. An agent should see the safest next edit shape.

Example:

```text
error[NAM003]: Unknown identifier
  unknown identifier 'message'
  examples/hello.0:2:27

  2 |     check world.out.write(message)
    |                           ^^^^^^^
  rule: Names must be declared before use in the current lexical scope.
  expected: local binding, parameter, function, builtin value
  actual: no visible symbol named 'message'
  fix: Introduce a local binding before this use (local-edit)
  explain: zero explain NAM003
```

The path may name a `.0` projection because source maps point at readable text.
That does not mean the agent should edit that file. In a graph-first package,
the repair should normally be a graph patch.

## Agent Repair Flow

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "this fails with NAM003. can you fix it?"
    },
    {
      "role": "assistant",
      "text": "I’ll inspect the diagnostic, patch the missing binding, and rerun the focused check."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero explain NAM003",
          "output": "NAM003: unresolved name\nfix: introduce a binding or use an in-scope symbol"
        },
        {
          "command": "zero query --fn main",
          "output": "main\n  check world.out.write message"
        },
        {
          "command": "zero patch /tmp/introduce-message.patch",
          "output": "program graph patch ok"
        }
      ]
    }
  ]
}
```

## Text And JSON Modes

Use text first:

```sh
zero check
zero explain NAM003
```

Use JSON when a tool needs exact spans, codes, fix ids, or nested readiness
facts:

```sh
zero check --json
zero fix --plan --json
zero doctor --json
```

Agents should not default to JSON for every command. JSON is for automation,
edit planning, CI, editors, and exact diagnostic fields.

## Common Graph-First Diagnostics

`BLD002` means a command expected graph-backed input but received a projection
or another unsupported input. Import the projection or pass the package/graph
store.

`RGP003` means projection state is invalid, stale, conflicting, or impossible
to verify. Use `zero status`, then choose `zero export`, `zero import`, or
manual review.

`GRF013` and related graph diagnostics mean the proposed graph edit would leave
an invalid shape, such as a sparse ordered group.

`CIMP003` and `CIMP005` report unsafe or incomplete C import/link metadata.
Use package-relative headers/libraries or configure target C dependency facts
instead of relying on host paths.

## Fix Plans

`zero fix --plan --json` returns a repair plan, not an automatic edit:

```sh
zero fix --plan --json
```

Use it when an agent needs a safer next step. The agent should still explain
the patch it will apply and validate the user-visible behavior afterward.
