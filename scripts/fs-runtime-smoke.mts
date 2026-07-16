#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const root = process.cwd();
const tempDir = await mkdtemp("/tmp/zero-fs-runtime-");
const src = join(tempDir, "fs-runtime-smoke.c");
const exe = join(tempDir, "fs-runtime-smoke");
const input = join(tempDir, "input.txt");
const large = join(tempDir, "large.bin");
const nestedDir = join(tempDir, "nested");
const missing = join(tempDir, "missing.txt");

const harness = String.raw`
#include "zero_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ZeroByteView bytes(const char *text) {
  return (ZeroByteView){(const unsigned char *)text, strlen(text)};
}

static ZeroMutByteView mut_bytes(unsigned char *ptr, size_t len) {
  return (ZeroMutByteView){ptr, len};
}

static int fail(const char *name) {
  fprintf(stderr, "fs runtime smoke failed: %s\n", name);
  return 1;
}

static int expect_true(int ok, const char *name) {
  return ok ? 0 : fail(name);
}

static int expect_none(ZeroMaybeUsize result, const char *name) {
  return expect_true(result.has == 0 && result.value == 0, name);
}

static int expect_small_read(const char *path) {
  unsigned char buffer[64];
  memset(buffer, 0, sizeof(buffer));
  ZeroMaybeUsize result = zero_fs_read_bytes(bytes(path), mut_bytes(buffer, sizeof(buffer)));
  return expect_true(
    result.has == 1 &&
      result.value == 14 &&
      memcmp(buffer, "hello runtime\n", 14) == 0 &&
      buffer[14] == 0,
    "read regular file"
  );
}

static int expect_partial_read(const char *path) {
  unsigned char buffer[5];
  memset(buffer, 0, sizeof(buffer));
  ZeroMaybeUsize result = zero_fs_read_bytes(bytes(path), mut_bytes(buffer, sizeof(buffer)));
  return expect_true(
    result.has == 1 &&
      result.value == 14 &&
      memcmp(buffer, "hello", sizeof(buffer)) == 0,
    "read partial buffer reports total size"
  );
}

static int expect_zero_len_read(const char *path) {
  unsigned char buffer[1] = {0xaa};
  ZeroMaybeUsize result = zero_fs_read_bytes(bytes(path), mut_bytes(buffer, 0));
  return expect_true(result.has == 1 && result.value == 14 && buffer[0] == 0xaa, "read zero-length buffer reports total size");
}

static int expect_large_read(const char *path) {
  const size_t len = 1048576u + 17u;
  unsigned char *buffer = (unsigned char *)malloc(len);
  if (!buffer) return fail("allocate large read buffer");
  memset(buffer, 0, len);
  ZeroMaybeUsize result = zero_fs_read_bytes(bytes(path), mut_bytes(buffer, len));
  int ok = result.has == 1 && result.value == len;
  for (size_t i = 0; ok && i < len; i++) {
    ok = buffer[i] == 'z';
  }
  free(buffer);
  return expect_true(ok, "read large file");
}

static int expect_rejected_inputs(const char *directory, const char *missing) {
  unsigned char buffer[16];
  memset(buffer, 0, sizeof(buffer));

  int status = 0;
  status |= expect_none(zero_fs_read_bytes(bytes(directory), mut_bytes(buffer, sizeof(buffer))), "reject directory");
  status |= expect_none(zero_fs_read_bytes(bytes(missing), mut_bytes(buffer, sizeof(buffer))), "reject missing file");
  status |= expect_none(zero_fs_read_bytes((ZeroByteView){NULL, 1}, mut_bytes(buffer, sizeof(buffer))), "reject null path");
  status |= expect_none(zero_fs_read_bytes((ZeroByteView){(const unsigned char *)"", 0}, mut_bytes(buffer, sizeof(buffer))), "reject empty path");
  status |= expect_none(zero_fs_read_bytes(bytes(missing), (ZeroMutByteView){NULL, sizeof(buffer)}), "reject null buffer");
  status |= expect_none(zero_fs_read_bytes(bytes(missing), (ZeroMutByteView){NULL, 0}), "reject null zero-length buffer");

  char long_path[4097];
  memset(long_path, 'a', 4096);
  long_path[4096] = '\0';
  status |= expect_none(
    zero_fs_read_bytes((ZeroByteView){(const unsigned char *)long_path, 4096}, mut_bytes(buffer, sizeof(buffer))),
    "reject path at runtime limit"
  );
  return status;
}

int main(int argc, char **argv) {
  if (argc != 5) return fail("expected input, large, directory, and missing paths");
  int status = 0;
  status |= expect_small_read(argv[1]);
  status |= expect_partial_read(argv[1]);
  status |= expect_zero_len_read(argv[1]);
  status |= expect_large_read(argv[2]);
  status |= expect_rejected_inputs(argv[3], argv[4]);
  return status;
}
`;

try {
  await mkdir(nestedDir);
  await writeFile(input, "hello runtime\n");
  await writeFile(large, Buffer.alloc(1048576 + 17, "z"));
  await writeFile(src, harness);
  await execFileAsync("cc", [
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Inative/zero-c/include",
    "native/zero-c/runtime/zero_runtime.c",
    src,
    "-o",
    exe,
  ], { cwd: root });
  const { stdout, stderr } = await execFileAsync(exe, [input, large, nestedDir, missing], { timeout: 5000 });
  assert.equal(stderr, "");
  assert.equal(stdout, "");
  console.log("fs runtime smoke ok");
} finally {
  await rm(tempDir, { recursive: true, force: true });
}
