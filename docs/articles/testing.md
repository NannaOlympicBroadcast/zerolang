## Run Tests From The Graph

In Zerolang, `zero test` runs test blocks from graph inputs and graph-first packages. When
this page shows `test` projection syntax, read it as the human review view of a
graph-backed test declaration.

```json-render
{
  "messages": [
    {
      "role": "user",
      "text": "add subtract and prove it works"
    },
    {
      "role": "assistant",
      "text": "I’ll add the function and a focused test for it."
    },
    {
      "role": "tools",
      "calls": [
        {
          "command": "zero patch /tmp/subtract.patch",
          "output": "program graph patch ok"
        },
        {
          "command": "zero test --json --filter subtract",
          "output": "{\"ok\":true,\"passedTests\":1,\"expectedFailures\":0}"
        }
      ]
    }
  ]
}
```

## What This Means

```zero
test "add works" {
    expect (add(40, 2) == 42)
}
```

Agents should add or update tests through graph patches.

## Daily Test Commands

```sh
zero test
zero test --json
zero test --json --filter add
```

Package tests discover test blocks across the package entry file and local
modules. Filters use substring matching on the test name.

## Expected Failures

Expected-fail tests are named with `xfail:`, `expected fail:`, or `[xfail]`.

| Result | JSON effect |
| --- | --- |
| The test fails as expected. | `expectedFailures` increments. |
| The test passes unexpectedly. | `unexpectedPasses` increments and the command fails. |

`zero test --json` includes `fixtures`, `snapshotKey`, selected/discovered
counts, stdout/stderr, and per-test results.

## Repository Reliability

Repository-level reliability checks still live outside the docs site:

```sh
pnpm run reliability:smoke
pnpm run native:sanitize
pnpm run conformance
```

The reliability smoke covers golden output rows, structured snapshot rows,
fuzz cases, and crasher regressions. Use those suites for compiler reliability,
not per-page documentation assertions.
