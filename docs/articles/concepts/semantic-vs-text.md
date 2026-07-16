## Semantic Edits

In Zerolang, text is an excellent human review format. It is not an ideal agent edit format.

When an agent edits text, it must keep many details in its context at once:
syntax, indentation, imports, symbol names, surrounding control flow, stale
file contents, and the compiler's current capabilities. A small mistake can
produce invalid code that is only discovered after another tool call.

Graph edits carry more intent:

```text
replaceFunctionBody main
  check world.out.write "hello\n"
end
```

That patch says which function body changes and what rows should become the new
body. The compiler parses those rows, builds graph nodes, checks graph shape,
updates repository metadata, and can export a stable projection for humans.

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "add a help command and then show me the source"
    },
    {
      "role": "assistant",
      "text": "I’ll add the help branch, run it, and then show the projection."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero patch --check-only /tmp/help.patch",
          "output": "program graph patch ok (check-only)"
        },
        {
          "command": "zero patch /tmp/help.patch",
          "output": "program graph patch ok"
        },
        {
          "command": "zero run -- help",
          "output": "usage: zero run -- <command>"
        },
        {
          "command": "zero export",
          "output": "repository graph export ok\nwrote: ./src/main.0"
        }
      ]
    }
  ]
}
```

## Text Edit Failure Modes

Common text-edit problems for agents:

- editing the wrong overload or similarly named function
- losing an import or closing brace
- creating syntax that looks plausible but is not accepted by this compiler
- formatting code that later changes the span the agent intended to patch
- using stale file contents after another edit
- changing source while the graph store remains the actual compiler input

Zero does not remove all errors. It moves the primary edit operation closer to
the compiler's semantic model so more errors are caught at patch time.

## Graph Patch Guardrails

Graph patches can include:

- graph hash expectations
- node hash expectations
- field expectations
- typed operation names
- function body or block body replacement
- dry-run and check-only modes

Those guardrails make stale edits explicit. They also make the failure useful:
the agent can query the graph again and patch the current node rather than
guessing from text.

## Where Projections Fit

Projection text is still part of the system. It gives humans a compact review
format and a manual edit escape hatch. It also gives diagnostics stable spans
and makes examples readable.

The rule is simple: agents write the graph by default; humans review
projections by choice.
