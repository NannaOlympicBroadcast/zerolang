## Start With Graph Examples

The Zerolang examples directory is the fastest way to understand what works today. Most
examples are checked-in `.graph` inputs or graph-first packages with
`zero.graph` stores. Sibling `.0` files, when present, are human-readable
projections for review.

## Expected Usage

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "make a tiny contacts api like the crm example"
    },
    {
      "role": "assistant",
      "text": "I’ll use the CRM example as the pattern and run the route checks."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero query examples/crm-api --find route",
          "output": "routes: health, contacts list, contact by id, create, update, delete"
        },
        {
          "command": "zero patch /tmp/contacts-api.patch",
          "output": "program graph patch ok"
        },
        {
          "command": "zero run -- $'GET /health\\n\\n'",
          "output": "HTTP/1.1 200 OK"
        }
      ]
    }
  ]
}
```

## Learning Order

Small programs:

- `examples/hello.graph`
- `examples/add.graph`
- `examples/point.graph`
- `examples/result-choice.graph`
- `examples/fallibility.graph`

Memory and ownership:

- `examples/memory-primitives.graph`
- `examples/allocator-collections.graph`
- `examples/ownership-cleanup.graph`
- `examples/fixed-vec.graph`

CLI and files:

- `examples/cli-file.graph`
- `examples/cli-config.graph`
- `examples/file-copy.graph`
- `examples/readall-cli/`
- `examples/resource-cli/`

Data and web:

- `examples/std-data-formats.graph`
- `examples/json-api-client.graph`
- `examples/json-api-router.graph`
- `examples/crm-api/`
- `examples/std-http-json.graph`
- `examples/std-http-request.graph`

Compiler and agent workflows:

- `examples/compile-time-v1.graph`
- `examples/agent-repair-demo/`
- `examples/error-tour/`
- `examples/memory-package/`
- `examples/zero-hash/`

## Inspect And Run Examples

```sh
zero query examples/hello.graph
zero check examples/hello.graph
zero run examples/add.graph
zero build --emit exe --target linux-musl-x64 examples/add.graph --out .zero/out/add
```

## Build And Size Examples

```sh
zero build --release tiny --target linux-musl-x64 examples/hello.graph --out .zero/out/hello-linux-musl
zero build --release tiny --target win32-x64.exe examples/hello.graph --out .zero/out/hello-win32
zero size --json --release tiny --target linux-musl-x64 examples/hello.graph
```

Size output reports retained helpers, section sizes, profile facts, target
report data, and artifact size. Use it when an agent needs to explain why bytes
were retained.

## Native Workflow Coverage

The examples intentionally cover arguments and environment, filesystem
resources, deterministic exit status, unhandled error exit paths, fixed-capacity
storage, HTTP routing, JSON helpers, target status, and repair workflows.

Use them as prompts for agents and as smoke checks for humans reviewing a
language change.
