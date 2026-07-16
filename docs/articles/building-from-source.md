## Use A Checkout Compiler

Use this page when you are working on Zerolang itself or testing a local compiler
change. Public docs use `zero`; contributor notes cover checkout wrapper
details.

## Build

```sh
pnpm install
make -C native/zero-c
zero --version
```

## Focused Loop

```sh
zero check examples/hello.graph
zero run examples/add.graph
zero build --emit exe --target linux-musl-x64 examples/add.graph --out .zero/out/add
```

Inspect graph and size facts:

```sh
zero inspect --json examples/systems-package
zero size --json examples/point.graph
zero targets
```

Explain diagnostics:

```sh
zero explain TAR002
zero explain --json TYP009
```

## Validation

```sh
pnpm run conformance
pnpm run command-contracts:local
pnpm run native:test
pnpm run docs:build
```

Before ending an agent turn that changed the repository, run
`pnpm run conformance`.

## Native Targets

The documented native target names are:

- `darwin-arm64`
- `darwin-x64`
- `linux-arm64`
- `linux-musl-arm64`
- `linux-musl-x64`
- `linux-x64`
- `win32-arm64.exe`
- `win32-x64.exe`

Unsupported target or feature requests report diagnostics instead of silently
choosing another backend.
