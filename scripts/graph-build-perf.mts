#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { execFile } from "node:child_process";
import { mkdir, rm } from "node:fs/promises";
import { join } from "node:path";
import { performance } from "node:perf_hooks";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = process.env.ZERO_BIN || "bin/zero";
const target = process.env.ZERO_GRAPH_BUILD_PERF_TARGET || "linux-musl-x64";
const outRoot = process.env.ZERO_GRAPH_BUILD_PERF_DIR || `/tmp/zero-graph-build-perf-${process.pid}`;
const maxBuffer = 128 * 1024 * 1024;

type Fixture = {
  name: string;
  input: string;
  target?: string;
  checkTarget?: string;
};

type CommandRun = {
  ok: boolean;
  command: string[];
  wallMs: number;
  body: any;
  stderr: string;
};

type PerformanceBudget = {
  name: string;
  fixture: string;
  pass: "cold" | "warm";
  command: "build" | "check";
  metric: string;
  max: number;
  unit: "ms" | "bytes";
};

const fixtures: Fixture[] = [
  { name: "hello-artifact", input: "examples/hello.graph" },
  { name: "stdlib-heavy-artifact", input: "conformance/native/pass/stdlib-target-neutral.graph" },
  { name: "crm-api-package", input: "examples/crm-api" },
];

const budgetMode = process.env.ZERO_GRAPH_BUILD_PERF_BUDGET || "warn";

function envNumber(name: string, fallback: number) {
  const raw = process.env[name];
  if (!raw) return fallback;
  const parsed = Number(raw);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

const performanceBudgets: PerformanceBudget[] = [
  {
    name: "simple graph cold build",
    fixture: "hello-artifact",
    pass: "cold",
    command: "build",
    metric: "compilerElapsedMs",
    max: envNumber("ZERO_GRAPH_BUILD_PERF_SIMPLE_COLD_MAX_MS", 5000),
    unit: "ms",
  },
  {
    name: "simple graph warm build",
    fixture: "hello-artifact",
    pass: "warm",
    command: "build",
    metric: "compilerElapsedMs",
    max: envNumber("ZERO_GRAPH_BUILD_PERF_SIMPLE_WARM_MAX_MS", 3000),
    unit: "ms",
  },
  {
    name: "simple graph artifact size",
    fixture: "hello-artifact",
    pass: "warm",
    command: "build",
    metric: "artifactBytes",
    max: envNumber("ZERO_GRAPH_BUILD_PERF_SIMPLE_ARTIFACT_MAX_BYTES", 8192),
    unit: "bytes",
  },
  {
    name: "simple graph lowered IR size",
    fixture: "hello-artifact",
    pass: "warm",
    command: "build",
    metric: "loweredIrBytes",
    max: envNumber("ZERO_GRAPH_BUILD_PERF_SIMPLE_LOWERED_IR_MAX_BYTES", 16384),
    unit: "bytes",
  },
  {
    name: "stdlib-heavy cold build",
    fixture: "stdlib-heavy-artifact",
    pass: "cold",
    command: "build",
    metric: "compilerElapsedMs",
    max: envNumber("ZERO_GRAPH_BUILD_PERF_STDLIB_HEAVY_COLD_MAX_MS", 120000),
    unit: "ms",
  },
  {
    name: "stdlib-heavy warm build",
    fixture: "stdlib-heavy-artifact",
    pass: "warm",
    command: "build",
    metric: "compilerElapsedMs",
    max: envNumber("ZERO_GRAPH_BUILD_PERF_STDLIB_HEAVY_WARM_MAX_MS", 120000),
    unit: "ms",
  },
  {
    name: "stdlib-heavy warm graph load",
    fixture: "stdlib-heavy-artifact",
    pass: "warm",
    command: "build",
    metric: "timings.graphLoadMs",
    max: envNumber("ZERO_GRAPH_BUILD_PERF_STDLIB_HEAVY_GRAPH_LOAD_MAX_MS", 30000),
    unit: "ms",
  },
  {
    name: "stdlib-heavy warm stdlib merge",
    fixture: "stdlib-heavy-artifact",
    pass: "warm",
    command: "build",
    metric: "timings.stdlibMergeMs",
    max: envNumber("ZERO_GRAPH_BUILD_PERF_STDLIB_HEAVY_STDLIB_MERGE_MAX_MS", 70000),
    unit: "ms",
  },
  {
    name: "stdlib-heavy artifact size",
    fixture: "stdlib-heavy-artifact",
    pass: "warm",
    command: "build",
    metric: "artifactBytes",
    max: envNumber("ZERO_GRAPH_BUILD_PERF_STDLIB_HEAVY_ARTIFACT_MAX_BYTES", 262144),
    unit: "bytes",
  },
  {
    name: "stdlib-heavy lowered IR size",
    fixture: "stdlib-heavy-artifact",
    pass: "warm",
    command: "build",
    metric: "loweredIrBytes",
    max: envNumber("ZERO_GRAPH_BUILD_PERF_STDLIB_HEAVY_LOWERED_IR_MAX_BYTES", 4194304),
    unit: "bytes",
  },
];

function selectedFixtures() {
  const raw = process.env.ZERO_GRAPH_BUILD_PERF_CASES;
  if (!raw) return fixtures;
  const wanted = new Set(raw.split(",").map((part) => part.trim()).filter(Boolean));
  return fixtures.filter((fixture) => wanted.has(fixture.name) || wanted.has(fixture.input));
}

function phaseMs(body: any, name: string) {
  const phase = Array.isArray(body?.compilerPhases)
    ? body.compilerPhases.find((item: any) => item?.name === name)
    : null;
  return typeof phase?.elapsedMs === "number" ? phase.elapsedMs : null;
}

function graphBuildTimings(body: any) {
  const timings = body?.graphBuild?.timings || {};
  return {
    graphLoadMs: timings.graphLoadMs ?? null,
    stdlibMergeMs: timings.stdlibMergeMs ?? null,
    stdlibReferenceScanMs: timings.stdlibReferenceScanMs ?? null,
    stdlibCleanupMs: timings.stdlibCleanupMs ?? null,
    stdlibModuleLoadMs: timings.stdlibModuleLoadMs ?? null,
    stdlibNodeMergeMs: timings.stdlibNodeMergeMs ?? null,
    stdlibEdgeMergeMs: timings.stdlibEdgeMergeMs ?? null,
    stdlibFinalizeMs: timings.stdlibFinalizeMs ?? null,
    readinessCheckMs: timings.readinessCheckMs ?? null,
    mirCacheLoadMs: timings.mirCacheLoadMs ?? null,
    mirLowerMs: timings.mirLowerMs ?? null,
    mirCacheWriteMs: timings.mirCacheWriteMs ?? null,
    mirCacheReloadMs: timings.mirCacheReloadMs ?? null,
    mirLoadOrLowerMs: timings.mirLoadOrLowerMs ?? null,
    lowerPhaseMs: timings.lowerPhaseMs ?? phaseMs(body, "lower"),
    codegenMs: timings.codegenMs ?? phaseMs(body, "codegen"),
    objectMs: timings.objectMs ?? phaseMs(body, "object"),
    linkMs: timings.linkMs ?? phaseMs(body, "link"),
  };
}

function graphBuildStdlibMergeFacts(body: any) {
  const merge = body?.graphBuild?.stdlibMerge || {};
  return {
    modulesMerged: merge.modulesMerged ?? null,
    nodesMerged: merge.nodesMerged ?? null,
    edgesMerged: merge.edgesMerged ?? null,
    cacheHit: merge.cacheHit ?? null,
    cacheStored: merge.cacheStored ?? null,
  };
}

function graphCheckTimings(body: any) {
  const timings = body?.graphCompiler?.timings || {};
  const resolveMs = timings.resolveMs ?? phaseMs(body, "resolve");
  const checkMs = timings.checkMs ?? phaseMs(body, "check");
  const lowerMs = timings.lowerMs ?? phaseMs(body, "lower");
  return {
    graphLoadMs: timings.loadMs ?? null,
    resolveMs: resolveMs ?? null,
    checkMs: checkMs ?? null,
    readinessCheckMs: typeof resolveMs === "number" && typeof checkMs === "number" ? resolveMs + checkMs : null,
    readinessLowerMs: lowerMs ?? null,
    cacheMs: timings.cacheMs ?? null,
  };
}

function cacheFacts(body: any) {
  const caches = Array.isArray(body?.compilerCaches) ? body.compilerCaches : [];
  const mapped = caches.find((cache: any) => cache?.name === "mappedFinalMir") || body?.graphBuild?.mappedFinalMir || null;
  return {
    hits: caches.filter((cache: any) => cache?.hit === true).length,
    misses: caches.filter((cache: any) => cache?.hit === false).length,
    mappedFinalMir: mapped ? {
      hit: mapped.hit ?? null,
      written: mapped.written ?? null,
      byteLength: mapped.byteLength ?? null,
      memoryMapped: mapped.memoryMapped ?? null,
      codegenImmediate: mapped.codegenImmediate ?? null,
      programReconstructed: mapped.programReconstructed ?? null,
    } : null,
  };
}

function valueAtPath(value: any, path: string) {
  return path.split(".").reduce((current, part) => current?.[part], value);
}

function evaluateBudgets(results: any[]) {
  const mode = budgetMode === "fail" || budgetMode === "warn" || budgetMode === "off" ? budgetMode : "warn";
  if (mode === "off") {
    return { schemaVersion: 1, mode, status: "off", ok: true, overCount: 0, skippedCount: 0, items: [] };
  }
  const items = performanceBudgets.map((budget) => {
    const fixture = results.find((item) => item.name === budget.fixture);
    const run = fixture?.[budget.pass]?.[budget.command];
    const actual = valueAtPath(run, budget.metric);
    if (!fixture) {
      return { ...budget, status: "skipped", actual: null, reason: "fixture not selected" };
    }
    if (typeof actual !== "number") {
      return { ...budget, status: "skipped", actual: null, reason: "metric missing" };
    }
    const overBy = Number(Math.max(0, actual - budget.max).toFixed(3));
    const unitSuffix = budget.unit === "ms" ? "Ms" : "Bytes";
    return {
      ...budget,
      status: actual <= budget.max ? "ok" : "over",
      actual,
      max: budget.max,
      [`actual${unitSuffix}`]: actual,
      [`max${unitSuffix}`]: budget.max,
      [`overBy${unitSuffix}`]: overBy,
      ratio: Number((actual / budget.max).toFixed(3)),
    };
  });
  const overCount = items.filter((item) => item.status === "over").length;
  const skippedCount = items.filter((item) => item.status === "skipped").length;
  return {
    schemaVersion: 1,
    mode,
    status: overCount === 0 ? "ok" : mode === "fail" ? "fail" : "warn",
    ok: overCount === 0,
    overCount,
    skippedCount,
    items,
  };
}

function writeBudgetMessages(budget: any) {
  if (!budget || budget.mode === "off") return;
  for (const item of budget.items || []) {
    if (item.status === "over") {
      process.stderr.write(
        `graph perf budget ${budget.status}: ${item.name} ${item.actual} ${item.unit} > ${item.max} ${item.unit} (${item.metric})\n`,
      );
    }
  }
}

async function runJson(args: string[], env: Record<string, string | undefined>): Promise<CommandRun> {
  const started = performance.now();
  try {
    const result = await execFileAsync(zero, args, {
      env: { ...process.env, ...env },
      maxBuffer,
    });
    const wallMs = Number((performance.now() - started).toFixed(3));
    return { ok: true, command: [zero, ...args], wallMs, body: JSON.parse(result.stdout), stderr: result.stderr };
  } catch (error: any) {
    const wallMs = Number((performance.now() - started).toFixed(3));
    let body: any = null;
    try {
      body = JSON.parse(error.stdout?.toString() ?? "");
    } catch {
      body = null;
    }
    return {
      ok: false,
      command: [zero, ...args],
      wallMs,
      body,
      stderr: error.stderr?.toString() ?? error.message ?? "",
    };
  }
}

async function buildCompiler() {
  if (process.env.ZERO_GRAPH_BUILD_PERF_SKIP_BUILD === "1") return null;
  const started = performance.now();
  await execFileAsync("make", ["-C", "native/zero-c"], { maxBuffer });
  return Number((performance.now() - started).toFixed(3));
}

async function zeroVersion() {
  const result = await runJson(["--version", "--json"], {});
  if (!result.ok) throw new Error(`zero version failed: ${result.stderr}`);
  return result.body;
}

function requireOk(run: CommandRun) {
  if (run.ok && run.body?.ok !== false) return;
  const diagnostic = run.body?.diagnostics?.[0];
  const message = diagnostic ? `${diagnostic.code}: ${diagnostic.message}` : run.stderr || "command failed";
  throw new Error(`${run.command.join(" ")} failed: ${message}`);
}

function normalizeBuild(run: CommandRun) {
  requireOk(run);
  return {
    ok: true,
    wallMs: run.wallMs,
    compilerElapsedMs: run.body?.elapsedMs ?? null,
    graphHash: run.body?.graph?.graphHash ?? null,
    lowering: run.body?.graph?.lowering ?? null,
    artifactBytes: run.body?.artifactBytes ?? null,
    loweredIrBytes: run.body?.loweredIrBytes ?? null,
    timings: graphBuildTimings(run.body),
    stdlibMerge: graphBuildStdlibMergeFacts(run.body),
    caches: cacheFacts(run.body),
  };
}

function normalizeCheck(run: CommandRun) {
  requireOk(run);
  return {
    ok: true,
    wallMs: run.wallMs,
    graphHash: run.body?.graph?.graphHash ?? run.body?.graphHash ?? null,
    targetReady: run.body?.targetReadiness?.ok ?? null,
    timings: graphCheckTimings(run.body),
    caches: cacheFacts(run.body),
  };
}

async function runFixture(fixture: Fixture) {
  const fixtureTarget = fixture.target || target;
  const checkTarget = fixture.checkTarget || process.env.ZERO_GRAPH_BUILD_PERF_CHECK_TARGET || "";
  const fixtureRoot = join(outRoot, fixture.name);
  const buildCache = join(fixtureRoot, "build-cache");
  const checkCache = join(fixtureRoot, "check-cache");
  await mkdir(fixtureRoot, { recursive: true });

  async function runPass(pass: "cold" | "warm") {
    const artifactPath = join(fixtureRoot, `${fixture.name}-${pass}.o`);
    process.stderr.write(`graph perf ${fixture.name} ${pass} build\n`);
    const build = await runJson(["build", "--json", "--emit", "obj", "--target", fixtureTarget, fixture.input, "--out", artifactPath], {
      ZERO_CACHE_DIR: buildCache,
    });
    process.stderr.write(`graph perf ${fixture.name} ${pass} check\n`);
    const checkArgs = ["check", "--json"];
    if (checkTarget) checkArgs.push("--target", checkTarget);
    checkArgs.push(fixture.input);
    const check = await runJson(checkArgs, {
      ZERO_CACHE_DIR: checkCache,
    });
    return {
      build: normalizeBuild(build),
      check: normalizeCheck(check),
    };
  }

  return {
    name: fixture.name,
    input: fixture.input,
    target: fixtureTarget,
    checkTarget: checkTarget || "host-default",
    cold: await runPass("cold"),
    warm: await runPass("warm"),
  };
}

async function main() {
  await rm(outRoot, { recursive: true, force: true });
  await mkdir(outRoot, { recursive: true });
  const nativeBuildMs = await buildCompiler();
  const version = await zeroVersion();
  const cases = selectedFixtures();
  if (cases.length === 0) throw new Error("ZERO_GRAPH_BUILD_PERF_CASES selected no fixtures");
  const started = performance.now();
  const results = [];
  for (const fixture of cases) results.push(await runFixture(fixture));
  const elapsedMs = Number((performance.now() - started).toFixed(3));
  const budgets = evaluateBudgets(results);
  const report = {
    schemaVersion: 1,
    kind: "zero-graph-build-perf",
    generatedAt: new Date().toISOString(),
    zero: version,
    target,
    outDir: outRoot,
    nativeBuildMs,
    elapsedMs,
    budgets,
    cases: results,
  };
  process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
  writeBudgetMessages(budgets);
  if (budgets.status === "fail") process.exit(1);
}

main().catch((error) => {
  process.stderr.write(`${error instanceof Error ? error.stack || error.message : String(error)}\n`);
  process.exit(1);
});
