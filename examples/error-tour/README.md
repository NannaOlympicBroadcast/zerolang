# Error Tour

These examples are intentionally copyable diagnostics and repairs for the current compiler slice. They point at real fixtures so docs, tests, and agent guidance stay aligned.

## Hosted Fs On A Non-Host Target

Bad:

```sh
bin/zero check --json --target linux-musl-x64 conformance/common/fail/unsupported-target-feature.graph
```

Good:

```sh
bin/zero build --target linux-musl-x64 examples/memory-package --out .zero/out/memory-package
```

## Immutable Storage Passed To A Mutable Api

Bad:

```sh
bin/zero explain TYP009
```

Good:

```sh
bin/zero check conformance/native/pass/std-mem-copy-fill.graph
```

## Missing Std Fs Error Name

Bad:

```sh
bin/zero explain ERR002
```

Good:

```sh
bin/zero check conformance/native/pass/std-fs-fallible-resources.graph
```

## Unchecked Named-Error Std Fs Call

Bad:

```sh
bin/zero explain ERR003
```

Good:

```sh
bin/zero check conformance/native/pass/std-fs-fallible-resources.graph
```

## Inspect Repairs

```sh
bin/zero explain TAR002
bin/zero fix --plan --json --target linux-musl-x64 conformance/common/fail/unsupported-target-feature.graph
```
