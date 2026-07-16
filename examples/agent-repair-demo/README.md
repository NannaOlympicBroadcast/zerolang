# Agent Repair Demo

This demo shows the intended agent loop on a real diagnostic. The checked-in
example stays graph-backed; the script writes its intentionally broken input
under `.zero/agent-repair-demo/` at runtime.

Explain the diagnostic:

```sh
bin/zero explain --json TYP009
```

Inspect the repair plan:

```sh
pnpm run agent:demo
```

Apply the suggested edit:

```diff
-    let dst: [4]u8 = [0, 0, 0, 0]
+    var dst: [4]u8 = [0, 0, 0, 0]
```

Review the fixed projection:

```sh
bin/zero check examples/agent-repair-demo/fixed.graph
```
