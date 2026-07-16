#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const outDir = join("/tmp", `zero-reliability-smoke-${process.pid}`);
const zero = "bin/zero";
const rows = [];

await mkdir(outDir, { recursive: true });

async function run(args, { allowFailure = false } = {}) {
  try {
    const result = await execFileAsync(zero, args);
    return { code: 0, stdout: result.stdout, stderr: result.stderr };
  } catch (error) {
    if (!allowFailure) throw error;
    return {
      code: error.code ?? error.status ?? 1,
      stdout: error.stdout?.toString() ?? "",
      stderr: error.stderr?.toString() ?? "",
    };
  }
}

async function json(args, options = {}) {
  const result = await run(args, options);
  return { ...result, body: JSON.parse(result.stdout) };
}

function record(id, kind, ok, facts = {}) {
  rows.push({ id, kind, ok, ...facts });
  assert.equal(ok, true, `${id} failed`);
}

const packageTests = await json(["test", "--json", "conformance/packages/test-app"]);
record("package-test-golden", "golden", packageTests.body.ok === true && packageTests.body.stdout === "3 test(s) ok\n", {
  selectedTests: packageTests.body.selectedTests,
  expectedFailures: packageTests.body.expectedFailures,
  snapshotKey: packageTests.body.fixtures?.snapshotKey,
});

const filteredTests = await json(["test", "--json", "--filter", "helper", "conformance/packages/test-app"]);
record("package-test-filter", "golden", filteredTests.body.selectedTests === 2 && filteredTests.body.discoveredTests === 3, {
  stdout: filteredTests.body.stdout,
});

const expectedFail = await json(["test", "--json", "conformance/native/pass/test-expected-fail.graph"]);
record("expected-fail-contract", "golden", expectedFail.body.expectedFailures === 1 && expectedFail.body.failedTests === 0, {
  status: expectedFail.body.results?.[0]?.status,
});

const validFuzz = join(outDir, "valid-fuzz.0");
await writeFile(validFuzz, `fn add(a: i32, b: i32) -> i32 {
    return a + b
}

test "fuzz valid arithmetic" {
    expect (add(21, 21) == 42)
}
`);
const validTokens = await json(["tokens", "--json", validFuzz]);
const validParse = await json(["parse", "--json", validFuzz]);
const validGraph = join(outDir, "valid-fuzz.graph");
await json(["import", "--json", "--format", "binary", "--out", validGraph, validFuzz]);
const validCheck = await json(["check", "--json", validGraph]);
record("parser-checker-fuzz-valid", "fuzz", validTokens.body.tokens.length > 0 && validParse.body.root.kind === "module" && validCheck.body.ok === true, {
  tokenCount: validTokens.body.tokens.length,
});

const invalidFuzz = join(outDir, "invalid-fuzz.0");
await writeFile(invalidFuzz, "pub fn main(world: World -> Void {\n");
const invalidGraph = join(outDir, "invalid-fuzz.graph");
const invalidImport = await json(["import", "--json", "--format", "binary", "--out", invalidGraph, invalidFuzz], { allowFailure: true });
record("parser-checker-fuzz-invalid", "fuzz", invalidImport.code !== 0 && invalidImport.body.diagnostics?.length > 0, {
  diagnostic: invalidImport.body.diagnostics?.[0]?.code,
});

const fmtFixture = await run(["fmt", "conformance/native/pass/test-blocks.0"]);
const fmtExpected = await readFile("conformance/native/pass/test-blocks.0", "utf8");
record("formatter-snapshot", "snapshot", fmtFixture.stdout === fmtExpected, {
  bytes: fmtFixture.stdout.length,
});

const stdlibJson = await json(["size", "--json", "conformance/native/pass/std-codec-json-url.graph"]);
record("stdlib-parser-golden", "golden", stdlibJson.body.usedStdlibHelpers?.some((helper) => helper.name === "std.json.field") && stdlibJson.body.compilerCaches?.some((cache) => cache.sourceKind === "program-graph"), {
  helperCount: stdlibJson.body.usedStdlibHelpers?.length ?? 0,
});

const stdlibEdgesGraph = "conformance/native/pass/std-codec-json-url.graph";
const stdlibEdgesRun = await run(["run", stdlibEdgesGraph]);
record("stdlib-codec-json-http-edges", "golden", stdlibEdgesRun.code === 0 && stdlibEdgesRun.stdout === "std codec json url ok\n", {
  fixture: stdlibEdgesGraph,
});

const crasher = join(outDir, "crasher-repro-unclosed-string.0");
await writeFile(crasher, "pub fn main() -> Void {\n    let value: String = \"unterminated\n}\n");
const crasherGraph = join(outDir, "crasher-repro-unclosed-string.graph");
const crasherImport = await json(["import", "--json", "--format", "binary", "--out", crasherGraph, crasher], { allowFailure: true });
record("minimized-crasher-repro", "crasher", crasherImport.code !== 0 && crasherImport.body.diagnostics?.length > 0, {
  diagnostic: crasherImport.body.diagnostics?.[0]?.code,
});

const report = {
  schemaVersion: 1,
  ok: rows.every((row) => row.ok),
  rows,
  conventions: {
    fuzzCorpus: [validGraph, invalidFuzz, "conformance/format/functions-blocks.0", "examples/std-data-formats.graph"],
    goldenOutputs: ["zero test --json conformance/packages/test-app", "zero fmt conformance/native/pass/test-blocks.0"],
    crasherRepros: [crasher],
    sanitizerGate: "pnpm run native:sanitize",
  },
};

await writeFile(join(outDir, "report.json"), `${JSON.stringify(report, null, 2)}\n`);
console.log("reliability smoke ok");
