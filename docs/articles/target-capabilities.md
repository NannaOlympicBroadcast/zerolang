## Capabilities Are Target Facts

In Zerolang, start with the user request. The agent should inspect target capability facts
before patching APIs that depend on hosted runtime support.

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "make this cli work on linux musl too"
    },
    {
      "role": "assistant",
      "text": "I’ll check the target facts and call out anything that blocks the port."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero check --json --target linux-musl-x64",
          "output": "{\"ok\":false,\"diagnostics\":[{\"code\":\"TAR002\",\"message\":\"target does not provide required capability\"}]}"
        }
      ]
    }
  ]
}
```

## What This Means

Zero does not assume every target can do filesystem, network, process, time, or
random operations. Those are explicit target facts. Target JSON includes host
identity, aliases, object formats, C target mapping, capabilities, HTTP runtime
metadata, and `targetToolchains`.

## Inspect Targets

```sh
zero targets
zero targets --json
zero check --json --target linux-musl-x64 examples/memory-package
```

## Hosted Capabilities

The current hosted capability set includes:

- `args`
- `env`
- `fs`
- `memory`
- `net`
- `proc`
- `rand`
- `stdio`
- `time`

Non-host targets expose only the capabilities listed for that target. Network
support is intentionally target-gated. HTTP helpers that only parse or write
request/response envelopes are target-neutral; hosted fetch and listen require
network-capable host support.

## Capability Failure Is A Feature

If a graph input uses `std.fs` on a target that cannot provide filesystem
support, `zero check --target ...` should report a diagnostic instead of
silently changing behavior.

```sh
zero check --json --target linux-musl-x64 conformance/common/fail/unsupported-target-feature.graph
```

The diagnostic is `TAR002` and the repair id points at choosing a target with
the required capability.

## What To Remember

Capabilities are part of the graph contract. Standard library pages document
effects and target support so an agent can choose the right helper before it
patches the program.
