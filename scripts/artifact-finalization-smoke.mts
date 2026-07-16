#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, mkdtemp, rm, stat, symlink, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const root = process.cwd();
const zero = "bin/zero";
const fixture = "examples/hello.graph";
const tempDir = await mkdtemp("/tmp/zero-artifact-finalization-");
const outDir = join(tempDir, "out");
const exe = join(outDir, "hello");
const dirOutput = join(outDir, "as-directory");
const symlinkTarget = join(outDir, "symlink-target");
const symlinkOutput = join(outDir, "as-symlink");

type CommandFailure = {
  status: number | null;
  stdout: string;
  stderr: string;
};

async function runZero(args: string[]) {
  return execFileAsync(zero, args, { cwd: root, timeout: 20000 });
}

async function expectZeroFailure(args: string[]): Promise<CommandFailure> {
  try {
    await runZero(args);
  } catch (error) {
    const failure = error as CommandFailure;
    assert.notEqual(failure.status, 0);
    return {
      status: failure.status ?? null,
      stdout: failure.stdout ?? "",
      stderr: failure.stderr ?? "",
    };
  }
  assert.fail(`expected zero ${args.join(" ")} to fail`);
}

function assertMentionsPathRejection(failure: CommandFailure) {
  const text = `${failure.stdout}\n${failure.stderr}`;
  assert.match(text, /regular|directory|symlink|artifact|output/i);
}

async function assertExecutable(path: string) {
  const info = await stat(path);
  assert.equal(info.isFile(), true);
  assert.ok(info.size > 0, "executable output must be non-empty");
  if (process.platform !== "win32") {
    assert.notEqual(info.mode & 0o111, 0, "executable output must have an execute bit");
  }
}

async function assertRunnable(path: string) {
  const { stdout, stderr } = await execFileAsync(path, [], { timeout: 5000 });
  assert.equal(stderr, "");
  assert.equal(stdout.trim(), "hello from zero");
}

try {
  await mkdir(outDir, { recursive: true });

  await runZero(["build", "--out", exe, fixture]);
  await assertExecutable(exe);
  await assertRunnable(exe);

  await mkdir(dirOutput);
  const directoryFailure = await expectZeroFailure(["build", "--out", dirOutput, fixture]);
  assertMentionsPathRejection(directoryFailure);

  if (process.platform !== "win32") {
    await writeFile(symlinkTarget, "not an executable\n");
    await symlink(symlinkTarget, symlinkOutput);
    const symlinkFailure = await expectZeroFailure(["build", "--out", symlinkOutput, fixture]);
    assertMentionsPathRejection(symlinkFailure);
  }

  console.log("artifact finalization smoke ok");
} finally {
  await rm(tempDir, { recursive: true, force: true });
}
