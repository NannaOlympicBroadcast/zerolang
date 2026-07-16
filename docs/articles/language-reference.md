## How Programs Are Shaped

Zerolang programs are semantic graph declarations with a human-readable `.0`
projection. This page names the language pieces that appear in both views.

Read **Primitives And Types** first when you want scalar types, `Maybe<T>`,
spans, arrays, ownership, and layout. Use this page for declarations, function
bodies, capabilities, packages, and projection rules.

## Declarations

The graph stores declarations for functions, types, enums, constants, imports,
tests, and package modules. Projection syntax makes those declarations readable:

```zero
pub fn main(world: World) -> Void raises {
    check world.out.write("hello\n")
}
```

Public declarations should have explicit type information. That makes graph
facts, diagnostics, docs, and repair plans stable.

## Functions

```zero
fn add(x: i32, y: i32) -> i32 {
    return x + y
}
```

Function graph facts include the name, parameters, return type, fallibility,
body block, references, and call edges. Agents should use `zero query --fn add`
before editing a function body or signature.

Fallible functions use `raises`:

```zero
fn requirePositive(value: i32) -> i32 raises [Invalid] {
    if value > 0 {
        return value
    }
    raise Invalid
}
```

`check` propagates failure through explicit control flow. There are no hidden
exceptions.

## Blocks And Control Flow

Blocks are graph nodes. Agents can patch a whole function body or a specific
block body:

```text
replaceFunctionBody main
  check world.out.write "hello\n"
end
```

```text
replaceBlockBody #block_then_1234
  check world.out.write "ready\n"
end
```

Projection syntax:

```zero
if ready {
    check world.out.write("ready\n")
} else {
    check world.out.write("not ready\n")
}
```

Conditions must be `Bool`. `while` loops and `match` expressions also lower to
explicit graph control-flow nodes.

## Capabilities

Zero avoids ambient global runtime access. Programs receive capabilities
explicitly:

```zero
pub fn main(world: World) -> Void raises {
    check world.out.write("ok\n")
}
```

Standard library helpers document their effects and target support. Use
`zero inspect --json` and `zero size --json` to see which helpers and
capabilities a graph input actually retains.

## Packages

Graph-first packages normally have:

```text
zero.toml
zero.graph
src/main.0
```

The target `main` path points at the readable projection for source maps and
review. It does not make `src/main.0` the normal package compile input.
Package commands compile from `zero.graph`.

## Imports And Modules

Package-local modules resolve from `src/` projection paths so humans have
stable review files:

- `src/foo.0` defines module `foo`
- `src/foo/mod.0` defines directory module `foo`

The graph store records the module declarations and relationships. Import
cycles and duplicate public exports are diagnosed before build output.

## Compile-Time Facts

Zero exposes a small compile-time metadata surface for target and type facts.
Current compile-time values include integer, `Bool`, and enum static values.
Representative metadata includes `compileTime`, `target.pointerWidth`,
`fieldType`, and `hasEnumCase`.

Invalid compile-time queries report diagnostics such as `MET001`. The design
goal is explicit graph metadata, not runtime registries or raw token-string
builders.

## Projection Rules

Projection syntax is the human-readable view of the graph:

- export projections for review with `zero export`
- import projections after human edits with `zero import`
- verify drift with `zero verify-projection`
- use graph commands for normal agent authoring

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "change just the ready branch"
    },
    {
      "role": "assistant",
      "text": "I’ll change that branch only and run the behavior it affects."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero query --fn main",
          "output": "if block\n  then #block_then_1234\n  else #block_else_5678"
        },
        {
          "command": "zero patch /tmp/replace-then.patch",
          "output": "program graph patch ok"
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

## What Is Not Hidden

Zero intentionally avoids hidden method registries, vtables, reflection,
ambient heap allocation, and process-global cleanup lists in the current
language model. When a program uses owned resources, allocator state, hosted
I/O, network capability, or C interop, those facts should be visible through
graph inspection and diagnostics.

## Targets

The public native target names are:

- `darwin-arm64`
- `darwin-x64`
- `linux-arm64`
- `linux-musl-arm64`
- `linux-musl-x64`
- `linux-x64`
- `win32-arm64.exe`
- `win32-x64.exe`

Use `zero targets --json` and `zero check --json --target <target>` before
asking an agent to rely on target-specific capabilities.
