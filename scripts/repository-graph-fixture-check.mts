#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { performance } from "node:perf_hooks";
import { resolve } from "node:path";

const root = "conformance/program-graph";
const storePath = `${root}/zero.graph`;
const target = "linux-musl-x64";
const zeroBin = resolve("bin/zero");
const execMaxBuffer = 16 * 1024 * 1024;

const budgets = {
  statusMs: 2000,
  verifyProjectionMs: 3000,
  importMs: 5000,
  exportMs: 5000,
};

function run(args: string[]) {
  const start = performance.now();
  const stdout = execFileSync(zeroBin, args, {
    encoding: "utf8",
    maxBuffer: execMaxBuffer,
    stdio: ["ignore", "pipe", "pipe"],
  });
  return { stdout, elapsedMs: performance.now() - start };
}

function runJson(args: string[]) {
  const result = run(args);
  return { ...result, body: JSON.parse(result.stdout) };
}

function assertBudget(name: string, elapsedMs: number, maxMs: number) {
  assert(
    elapsedMs <= maxMs,
    `${name} exceeded repository graph fixture budget: ${elapsedMs.toFixed(1)}ms > ${maxMs}ms`,
  );
}

const sourceBefore = readFileSync(`${root}/hello.0`, "utf8");
const storeBefore = readFileSync(storePath);

const status = runJson(["status", "--json", "--target", target, root]);
assert.equal(status.body.ok, true);
assert.equal(status.body.repositoryGraph.storePresent, true);
assert.equal(status.body.repositoryGraph.storeValid, true);
assert.equal(status.body.repositoryGraph.projectionState, "clean");
assert.equal(status.body.repositoryGraph.compilerInput, "repository-graph");
assertBudget("status", status.elapsedMs, budgets.statusMs);

const check = runJson(["check", "--json", "--target", target, root]);
assert.equal(check.body.ok, true);
assert.equal(check.body.sourceFile, storePath);
assert.equal(check.body.graph.artifact, storePath);
assert.equal(check.body.graph.canonicalSource, false);
assert.equal(check.body.graph.moduleIdentity, "package:program-graph-fixture@0.1.0");

const verify = runJson(["verify-projection", "--json", "--target", target, root]);
assert.equal(verify.body.ok, true);
assert.equal(verify.body.writes, false);
assert.equal(verify.body.repositoryGraph.projectionState, "clean");
assertBudget("verify-projection", verify.elapsedMs, budgets.verifyProjectionMs);

const fromSource = runJson(["import", "--json", "--target", target, root]);
assert.equal(fromSource.body.ok, true);
assert.equal(fromSource.body.repositoryGraph.projectionState, "clean");
assert.deepEqual(fromSource.body.changedPaths, [storePath]);
assert.equal(readFileSync(`${root}/hello.0`, "utf8"), sourceBefore);
assert.equal(Buffer.compare(readFileSync(storePath), storeBefore), 0);
assertBudget("import", fromSource.elapsedMs, budgets.importMs);

const fromGraph = runJson(["export", "--json", "--target", target, root]);
assert.equal(fromGraph.body.ok, true);
assert.equal(fromGraph.body.repositoryGraph.projectionState, "clean");
assert.deepEqual(fromGraph.body.changedPaths, []);
assert.equal(readFileSync(`${root}/hello.0`, "utf8"), sourceBefore);
assert.equal(Buffer.compare(readFileSync(storePath), storeBefore), 0);
assertBudget("export", fromGraph.elapsedMs, budgets.exportMs);

console.log(
  `repository graph fixture ok (status ${status.elapsedMs.toFixed(1)}ms, verify ${verify.elapsedMs.toFixed(1)}ms, import ${fromSource.elapsedMs.toFixed(1)}ms, export ${fromGraph.elapsedMs.toFixed(1)}ms)`,
);
