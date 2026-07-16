## Make Retention Visible

Zerolang optimization docs should answer a concrete question: why did this graph
input retain these bytes, helpers, allocations, or runtime shims?

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "why is this binary bigger now?"
    },
    {
      "role": "assistant",
      "text": "I’ll inspect the size report and trace what is being retained."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero size --json",
          "output": "{\"sizeBreakdown\":{},\"retentionReasons\":[\"std.http.fetch retained by handle\"]}"
        }
      ]
    }
  ]
}
```

## What This Means

Use graph inputs or graph-first packages:

```sh
zero size --json examples/fixed-vec.graph
zero mem --json examples/allocator-collections.graph
zero build --json --profile tiny --target linux-musl-x64 examples/hello.graph --out .zero/out/hello
```

## Profiles

| Profile | Use when | Contract |
| --- | --- | --- |
| `debug` | You need diagnostics and local symbols. | Keeps diagnostic/debug metadata. |
| `fast` | Throughput matters more than minimum size. | Optimizes for speed within current direct codegen limits. |
| `small` | You want the normal release shape. | Pay-as-used helpers and deterministic artifacts. |
| `tiny` | Artifact size is the main constraint. | Minimum runtime metadata and strict helper budget. |

Build JSON includes `profileSemantics`, `profileCatalog`, `profileBudget`, and
`safetyFacts`.

## Size Reports

`zero size --json` adds:

- `sizeBreakdown`
- `retentionReasons`
- `optimizationHints`
- retained function and helper facts
- literal and section cost
- target and profile facts

Use `retentionReasons` when an agent needs to explain why a helper, literal, or
debug metadata block stayed in the artifact.

## Memory Reports

`zero mem --json` includes:

- `memoryBudgets`
- `allocatorFacts`
- `allocationInstrumentation`
- `collectionFacts`
- `safetyFacts`

These fields are the public way to check whether an example remains
fixed-capacity, caller-buffered, or heap-free.
