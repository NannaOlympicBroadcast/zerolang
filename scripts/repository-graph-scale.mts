#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";

const root = join("/tmp", `zero-repository-graph-scale-${process.pid}`);
const source = join(root, "main.0");
const store = join(root, "zero.graph");
const zero = resolve("bin/zero");
const maxMs = Number(process.env.ZERO_REPOSITORY_GRAPH_SCALE_MAX_MS ?? "15000");

function json(args: string[], allowFailure = false) {
  try {
    const stdout = execFileSync(zero, args, { encoding: "utf8", maxBuffer: 16 * 1024 * 1024, stdio: ["ignore", "pipe", "pipe"] });
    return { code: 0, body: JSON.parse(stdout) };
  } catch (error) {
    if (!allowFailure) throw error;
    return { code: error.status ?? 1, body: JSON.parse(error.stdout?.toString() ?? "{}") };
  }
}

function elapsed<T>(fn: () => T) {
  const started = Date.now();
  const value = fn();
  return { value, ms: Date.now() - started };
}

rmSync(root, { recursive: true, force: true });
mkdirSync(root, { recursive: true });

const functions: string[] = [];
for (let i = 0; i < 96; i++) {
  functions.push(`fn value_${i}() -> i32 {\n    return ${i}\n}\n`);
}
writeFileSync(
  source,
  `${functions.join("\n")}\npub fn main(world: World) -> Void raises {\n    check world.out.write("repository graph scale ok\\n")\n}\n`,
);

const imported = elapsed(() => json(["import", "--format", "text", "--json", source]));
assert.equal(imported.value.code, 0);
assert.equal(imported.value.body.repositoryGraph.projectionState, "clean");
assert(imported.ms < maxMs, `repository graph import took ${imported.ms}ms`);

const status = elapsed(() => json(["status", "--json", source]));
assert.equal(status.value.body.repositoryGraph.storeValid, true);
assert.equal(status.value.body.storage.encoding, "single-file-text");
assert.equal(status.value.body.storage.interface, "ProgramGraphStore");
assert.equal(status.value.body.repositoryGraph.semanticValidity, "shape-valid");
assert.equal(status.value.body.repositoryGraph.projectionValidity, "clean");
assert.equal(status.value.body.compilerStore.shape, "compiler-oriented-tables");
assert.equal(status.value.body.compilerStore.sourceFreeInspection, true);
assert.equal(status.value.body.compilerStore.sourceProjectionRequiredForSemanticFacts, false);
assert.equal(status.value.body.compilerStore.tables.schema, 1);
assert.equal(status.value.body.compilerStore.tables.node, status.value.body.scale.nodes);
assert.equal(status.value.body.compilerStore.tables.edge, status.value.body.scale.edges);
assert.deepEqual(status.value.body.compilerStore.hashInputs.graphHashExcludes, ["sourcePath", "line", "column", "projectionText"]);
assert(status.value.body.scale.nodes >= 300, `expected a large graph, got ${status.value.body.scale.nodes} nodes`);
assert(status.value.body.scale.edges >= 200, `expected many graph edges, got ${status.value.body.scale.edges}`);
assert(status.ms < maxMs, `repository graph status took ${status.ms}ms`);

const verify = elapsed(() => json(["verify-projection", "--json", source]));
assert.equal(verify.value.code, 0);
assert.equal(verify.value.body.ok, true);
assert(verify.ms < maxMs, `repository graph verify-projection took ${verify.ms}ms`);

const originalStore = readFileSync(store, "utf8");
assert.match(originalStore, /^compilerStore schemaVersion:1 shape:"compiler-oriented-tables"/m);
assert.match(originalStore, /^compilerTables schema:1 package:/m);
assert.match(originalStore, /^compilerHashInputs graphHashExcludes:"source-path,line,column,projection-text"/m);

rmSync(source, { force: true });
const sourceFreeStatus = elapsed(() => json(["status", "--json", root]));
assert.equal(sourceFreeStatus.value.body.repositoryGraph.storeValid, true);
assert.equal(sourceFreeStatus.value.body.repositoryGraph.semanticValidity, "shape-valid");
assert.equal(sourceFreeStatus.value.body.repositoryGraph.projectionValidity, "missing");
assert.equal(sourceFreeStatus.value.body.compilerStore.sourceFreeInspection, true);
assert.equal(sourceFreeStatus.value.body.compilerStore.semanticValidity.ok, true);
assert.equal(sourceFreeStatus.value.body.compilerStore.projectionValidity.state, "missing");
assert.equal(sourceFreeStatus.value.body.compilerStore.tables.node, status.value.body.scale.nodes);
assert(sourceFreeStatus.ms < maxMs, `repository graph source-free status took ${sourceFreeStatus.ms}ms`);

writeFileSync(store, originalStore.replace("zero-repository-graph v1", "zero-repository-graph v2"));
const futureSchema = json(["status", "--json", root], true);
assert.notEqual(futureSchema.code, 0);
assert.equal(futureSchema.body.diagnostics[0].code, "RGP003");
assert.match(futureSchema.body.diagnostics[0].actual, /invalid repository graph store|valid zero.graph/);
writeFileSync(store, originalStore);

console.log(`repository graph scale ok (${status.value.body.scale.nodes} nodes, ${status.value.body.scale.edges} edges)`);
