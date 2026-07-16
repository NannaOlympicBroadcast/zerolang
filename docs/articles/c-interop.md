## The C Boundary

Zerolang supports a small explicit C ABI surface from graph inputs. C-facing code
should stay narrow, inspectable, and target-aware.

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "can this call my tiny c helper on linux musl?"
    },
    {
      "role": "assistant",
      "text": "I’ll check the target link plan and tell you what blocks the call."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero inspect --json --target linux-musl-x64",
          "output": "{\"cLibraries\":[{\"linkPlan\":{\"sysrootStatus\":\"configured\"}}]}"
        }
      ]
    }
  ]
}
```

## What This Means

Projection example:

```zero
export c fn add(a: i32, b: i32) -> i32 {
    return a + b
}
```

Graph checks:

```sh
zero check conformance/native/pass/c-abi-export.graph
zero abi dump --json conformance/native/pass/c-abi-export.graph
```

`zero abi dump --json` reports exported symbols and generated header facts such
as `generatedHeader.available`.

## Import Metadata

Header imports expose typed metadata:

```sh
zero inspect --json --target linux-musl-x64 conformance/check/pass/c-header-import.graph
```

The JSON includes `cImports[].typedModel` with imported functions, constants,
structs, enums, and typedefs.

Callable imports are limited to direct scalar ABI types today: `Void`, `Bool`,
`u8`, `u16`, `usize`, `i32`, `u32`, `i64`, and `u64`. Pointer, array, struct,
and unsupported-width parameters should be wrapped by a small C shim.

## Link Plans

Executable builds with direct extern C calls require package link metadata in
`zero.toml`. Imported headers must appear in `c.libs.*.headers`, and the
library must provide `lib` or `link` inputs.

`zero inspect --json` reports each `cLibraries[].linkPlan` with include paths,
library paths, sysroot status, target ABI, host discovery status, and header
hash data.

Unsafe foreign-target discovery fails with `CIMP003`. Missing or unsafe link
inputs fail with `CIMP005`.
