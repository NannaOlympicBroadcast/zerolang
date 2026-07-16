#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const outDir = ".zero/agent-repair-demo";
mkdirSync(outDir, { recursive: true });

const brokenSource = `pub fn main(world: World) -> Void raises {
    let src: [4]u8 = [1, 2, 3, 4]
    let dst: [4]u8 = [0, 0, 0, 0]
    let copied: usize = std.mem.copy(dst, src)
    if copied == 4 {
        check world.out.write("copied\\n")
    }
}
`;
const workFile = join(outDir, "main.0");
const workGraph = join(outDir, "main.graph");
writeFileSync(workFile, brokenSource);

function zeroJson(args, allowFailure = false) {
  try {
    return JSON.parse(execFileSync("bin/zero", args, { encoding: "utf8" }));
  } catch (error) {
    if (!allowFailure) throw error;
    return JSON.parse(error.stdout.toString());
  }
}

const importPlan = zeroJson(["import", "--json", "--format", "binary", "--out", workGraph, workFile], true);
assert.equal(importPlan.ok, false);
assert.equal(importPlan.diagnostics[0].code, "TYP009");
assert.equal(importPlan.diagnostics[0].repair.id, "make-binding-mutable");

const explain = zeroJson(["explain", "--json", "TYP009"]);
assert.equal(explain.code, "TYP009");
assert.equal(explain.repair.id, "make-binding-mutable");

const repair = importPlan.diagnostics[0].repair;
assert.equal(repair.id, "make-binding-mutable");

const fixedSource = brokenSource
  .replace("    let dst: [4]u8", "    var dst: [4]u8")
  .replace("    let _copied: i32", "    let _copied: usize");
writeFileSync(workFile, fixedSource);
const fixedImport = zeroJson(["import", "--json", "--format", "binary", "--out", workGraph, workFile]);
assert.equal(fixedImport.ok, true);

const fixed = zeroJson(["check", "--json", workGraph]);
assert.equal(fixed.ok, true);
assert.equal(fixed.diagnostics.length, 0);

const projectDir = join(outDir, "project");
rmSync(projectDir, { recursive: true, force: true });
execFileSync("bin/zero", ["new", "package", projectDir], { stdio: ["ignore", "ignore", "pipe"] });

function assertNoC(body) {
  assert.equal(body.generatedCBytes ?? 0, 0);
  assert.equal(body.cBridgeFallback ?? body.selfHostRouting?.cBridge?.required ?? false, false);
  assert.notEqual(body.legacy, true);
}

const projectCheck = zeroJson(["check", "--json", projectDir]);
assert.equal(projectCheck.ok, true);
assertNoC(projectCheck);

const projectTest = zeroJson(["test", "--json", projectDir]);
assert.equal(projectTest.ok, true);
assert.equal(projectTest.testBackend, "direct-program-graph");
assertNoC(projectTest);

const projectMain = join(projectDir, "src", "main.0");
const projectMainSource = readFileSync(projectMain, "utf8");
writeFileSync(projectMain, projectMainSource.split('\n\ntest "package import works"')[0] + "\n");

const projectGraph = zeroJson(["inspect", "--json", projectDir]);
assert.equal(projectGraph.selfHostRouting.cBridge.required, false);

const projectDoc = zeroJson(["doc", "--json", projectDir]);
assertNoC(projectDoc);

const projectSize = zeroJson(["size", "--json", projectDir]);
assertNoC(projectSize);

const projectMem = zeroJson(["mem", "--json", projectDir]);
assertNoC(projectMem);

const releaseOut = join(outDir, "project-linux-release");
const projectRelease = zeroJson(["build", "--json", "--release", "tiny", "--target", "linux-musl-x64", projectDir, "--out", releaseOut]);
assert.equal(projectRelease.emit, "exe");
assert.equal(projectRelease.compiler, "zero-elf64");
assertNoC(projectRelease);

console.log("agent repair demo ok");
