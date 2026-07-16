#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = "bin/zero";
const outDir = ".zero/stdlib-target-matrix";
const fixtures = [
  "conformance/native/pass/stdlib-target-neutral.graph",
  "conformance/native/pass/direct-checksum-helpers.graph",
];
const artifactBudgetBytes = 120_000;
const targets = [
  "darwin-arm64",
  "darwin-x64",
  "linux-arm64",
  "linux-musl-arm64",
  "linux-musl-x64",
  "linux-x64",
  "win32-arm64.exe",
  "win32-x64.exe",
];
const representativeStdlibTargets = [
  "darwin-arm64",
  "linux-musl-x64",
  "win32-x64.exe",
];
const matrixScope = process.env.ZERO_STDLIB_TARGET_MATRIX_SCOPE === "fast" ? "fast" : "deep";

type MatrixRow = {
  fixture: string;
  target: string;
  ok: boolean;
  artifactPath: string;
  artifactBytes: number;
  generatedCBytes: number;
  directObjectEmitter?: string;
  diagnostic?: string;
};

function parsePositiveInt(value: string | undefined, fallback: number) {
  const parsed = Number.parseInt(value ?? "", 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

async function mapLimit<T, R>(items: T[], limit: number, callback: (item: T, index: number) => Promise<R>) {
  const results = new Array<R>(items.length);
  let next = 0;
  const workerCount = Math.min(Math.max(1, limit), Math.max(1, items.length));

  async function worker() {
    for (;;) {
      const index = next++;
      if (index >= items.length) return;
      results[index] = await callback(items[index], index);
    }
  }

  await Promise.all(Array.from({ length: workerCount }, () => worker()));
  return results;
}

await mkdir(outDir, { recursive: true });

async function json(args: string[]) {
  try {
    const result = await execFileAsync(zero, args, { maxBuffer: 1024 * 1024 * 4 });
    return { code: 0, stdout: result.stdout, stderr: result.stderr, body: JSON.parse(result.stdout) };
  } catch (error) {
    const stdout = error.stdout?.toString() ?? "";
    let body = null;
    try {
      body = JSON.parse(stdout);
    } catch {
      body = null;
    }
    return {
      code: error.code ?? error.status ?? 1,
      stdout,
      stderr: error.stderr?.toString() ?? "",
      body,
    };
  }
}

const rows: MatrixRow[] = [];
const deepMatrix = fixtures.flatMap((fixture) => {
  const fixtureTargets = fixture.includes("stdlib-target-neutral")
    ? representativeStdlibTargets
    : targets;
  return fixtureTargets.map((target) => ({ fixture, target }));
});
const fastMatrix = [
  { fixture: "conformance/native/pass/stdlib-target-neutral.graph", target: "linux-musl-x64" },
  { fixture: "conformance/native/pass/stdlib-target-neutral.graph", target: "win32-x64.exe" },
  { fixture: "conformance/native/pass/direct-checksum-helpers.graph", target: "linux-musl-x64" },
  { fixture: "conformance/native/pass/direct-checksum-helpers.graph", target: "darwin-arm64" },
  { fixture: "conformance/native/pass/direct-checksum-helpers.graph", target: "win32-x64.exe" },
];
const matrix = matrixScope === "fast" ? fastMatrix : deepMatrix;
const jobs = parsePositiveInt(process.env.ZERO_STDLIB_TARGET_MATRIX_JOBS, 4);

rows.push(...await mapLimit(matrix, jobs, async ({ fixture, target }) => {
  const startedAt = Date.now();
  const ext = target.includes("win32") ? ".obj" : ".o";
  const stem = fixture.split("/").pop()?.replace(/\.(?:0|graph)$/, "") ?? "fixture";
  const artifactPath = join(outDir, `${stem}-${target}${ext}`);
  const result = await json(["build", "--json", "--emit", "obj", "--target", target, fixture, "--out", artifactPath]);
  const durationMs = Date.now() - startedAt;
  const diagnostic = result.body?.diagnostics?.[0];
  const row: MatrixRow = {
    fixture,
    target,
    ok: result.code === 0 && result.body?.ok !== false,
    artifactPath,
    artifactBytes: result.body?.artifactBytes ?? 0,
    generatedCBytes: result.body?.generatedCBytes ?? -1,
    directObjectEmitter: result.body?.releaseTargetContract?.directObjectEmitter,
    diagnostic: diagnostic ? `${diagnostic.code}: ${diagnostic.message}` : undefined,
  };
  console.error(`stdlib target matrix ${row.ok ? "ok" : "failed"}: ${fixture} ${target} (${durationMs}ms)`);
  assert.equal(row.ok, true, `${fixture} ${target} failed: ${row.diagnostic ?? result.stderr}`);
  assert.equal(row.generatedCBytes, 0, `${fixture} ${target} used generated C fallback`);
  assert.ok(row.artifactBytes > 0, `${fixture} ${target} did not produce an object artifact`);
  assert.ok(row.artifactBytes <= artifactBudgetBytes, `${fixture} ${target} object artifact exceeded ${artifactBudgetBytes} bytes`);
  return row;
}));

for (const fixture of [...new Set(matrix.map((row) => row.fixture))]) {
  const hostRun = await execFileAsync(zero, ["run", fixture]);
  assert.equal(hostRun.stdout, "", `${fixture} should run silently`);
}

const report = {
  schemaVersion: 1,
  ok: rows.every((row) => row.ok),
  scope: matrixScope,
  fixtures,
  artifactBudgetBytes,
  representativeStdlibTargets,
  targets: rows,
};

await writeFile(join(outDir, "report.json"), `${JSON.stringify(report, null, 2)}\n`);
console.log("stdlib target matrix ok");
