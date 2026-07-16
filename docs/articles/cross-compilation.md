## Targets Are Explicit

In Zerolang, start with the result the human wants. The agent decides whether it needs target
facts, a no-write readiness check, or an artifact build.

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "build this for linux musl and tell me if anything blocks it"
    },
    {
      "role": "assistant",
      "text": "I’ll check the target facts first, then build if it is ready."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero check --json --target linux-musl-x64",
          "output": "{\"ok\":true,\"targetReadiness\":{\"ok\":true}}"
        },
        {
          "command": "zero build --target linux-musl-x64 --out .zero/out/app",
          "output": ""
        }
      ]
    }
  ]
}
```

## What This Means

Zero cross-compilation starts from graph facts, target facts, and direct
emitters. Unsupported target or feature combinations should fail with
diagnostics rather than fallback silently.

## Check Target Readiness

Use `check --json` before writing artifacts when an agent needs readiness data:

```sh
zero targets
zero check --json --emit obj --target linux-musl-x64 examples/direct-call-add.graph
zero check --target linux-musl-x64 examples/memory-package
```

Target readiness can include `target`, `objectFormat`, `backend`, `stage`, and
unsupported feature facts.

## Build Direct Artifacts

```sh
zero build --target linux-musl-x64 examples/memory-package --out .zero/out/memory-package
zero build --emit exe --target linux-musl-x64 examples/direct-exe-return.graph --out .zero/out/direct-exe-return
zero build --emit obj --target darwin-arm64 examples/direct-call-add.graph --out .zero/out/direct-call-add.o
```

Use `zero size --json --target <target>` when the question is about retained
helpers, section sizes, or artifact facts rather than producing a new file.

## Sysroots And C Boundaries

Cross-target C interop must use explicit package metadata, vendored inputs, or
target sysroots. The compiler should not silently reuse host headers and
libraries for a foreign target.

Use:

```sh
zero inspect --json --target linux-musl-x64 <package>
zero abi dump --json --target linux-musl-x64 <graph-input>
```
