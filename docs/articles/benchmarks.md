## Measure The Current Compiler

Zerolang benchmarks are regression signals for the current compiler. They are not broad
marketing claims. Use them to compare graph inputs, artifact sizes, build time,
startup/runtime behavior, and memory use across changes.

Run:

```sh
pnpm run bench
```

Outputs:

```sh
.zero/bench/latest.json
.zero/bench/trends/latest.json
.zero/bench/trends/summary.md
```

## Cases

Current benchmark cases include:

- `hello`, `add`, `structs`, `params`
- `buffers`, `parser`, `codec`, `parse`
- `slices`, `arena`, `fallibility`, `branches`
- `module-package`, `rescue`
- `fs-resource`, `mem-copy-fill`, `zero-hash`

The Zero inputs live under `benchmarks/zero`. Host targets that cannot run a
case report `skipped` with a reason instead of failing the entire benchmark.

## Metrics

Important fields:

- `buildMs`
- `runMs`
- `runMinMs`
- `runRuns`
- `artifactBytes`
- `compressedArtifactBytes`
- `peakRssBytes`
- `expectedStdout`
- `outputMatches`

## Options

```sh
ZERO_BENCH_RUNS=<n> pnpm run bench
ZERO_BENCH_MODE=sandbox pnpm run bench
```

Use one run for smoke checks and more runs when comparing trends.
