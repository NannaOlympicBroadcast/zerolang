#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

native_shards="${ZERO_AGENT_NATIVE_TEST_SHARDS:-4}"
if ! [[ "$native_shards" =~ ^[0-9]+$ ]] || (( native_shards < 1 )); then
  echo "ZERO_AGENT_NATIVE_TEST_SHARDS must be a positive integer" >&2
  exit 1
fi

dry_run="${ZERO_AGENT_CHECK_DRY_RUN:-0}"
work_root="${ZERO_AGENT_CHECK_WORK_ROOT:-/tmp/zero-agent-checks-$$}"
log_dir="${ZERO_AGENT_CHECK_LOG_DIR:-$root/.zero/agent-checks}"
mkdir -p "$work_root" "$log_dir" "$root/.zero/toolchains"

cleanup() {
  if [[ "${ZERO_AGENT_CHECK_KEEP_WORKDIR:-}" != "1" ]]; then
    rm -rf "$work_root"
  fi
}
trap cleanup EXIT

zig_platform() {
  case "$(uname -s):$(uname -m)" in
    Linux:x86_64) printf "linux-x64" ;;
    Darwin:arm64 | Darwin:aarch64) printf "macos-aarch64" ;;
    Darwin:x86_64) printf "macos-x86_64" ;;
    *)
      echo "unsupported host for pinned Zig installer: $(uname -s):$(uname -m)" >&2
      exit 1
      ;;
  esac
}

zig_dir="$root/.zero/toolchains/zig-0.14.1-$(zig_platform)"
if [[ "$dry_run" != "1" ]]; then
  bash scripts/install-zig.sh >/dev/null
fi
export PATH="$zig_dir:$PATH"

prepare_job_dir() {
  local name="$1"
  local dir="$work_root/$name"
  mkdir -p "$dir"
  (
    cd "$root"
    tar \
      --exclude='./.git' \
      --exclude='./.zero' \
      --exclude='./node_modules' \
      --exclude='./docs/.next' \
      --exclude='./docs/node_modules' \
      -cf - .
  ) | (
    cd "$dir"
    tar -xf -
  )
  printf "%s" "$dir"
}

pids=()
names=()
logs=()

run_job() {
  local name="$1"
  shift
  if [[ "$dry_run" == "1" ]]; then
    echo "would start: $name: $*"
    return
  fi

  local dir
  dir="$(prepare_job_dir "$name")"
  local log="$log_dir/$name.log"
  names+=("$name")
  logs+=("$log")
  (
    cd "$dir"
    export PATH="$PATH"
    pnpm install --frozen-lockfile --prefer-offline
    "$@"
  ) >"$log" 2>&1 &
  pids+=("$!")
  echo "started: $name"
}

run_job conformance pnpm run conformance:local

for shard in $(seq 1 "$native_shards"); do
  run_job "native-tests-${shard}-of-${native_shards}" env ZERO_NATIVE_TEST_SHARD="${shard}/${native_shards}" pnpm run native:test:local
done

run_job native-sanitizer-smoke pnpm run native:sanitize
run_job command-contract-snapshots pnpm run command-contracts:local
run_job workspace-checks bash scripts/workspace-checks.sh

if [[ "$dry_run" == "1" ]]; then
  echo "agent checks plan ok"
  exit 0
fi

failed=0
for i in "${!pids[@]}"; do
  if wait "${pids[$i]}"; then
    echo "ok: ${names[$i]}"
  else
    echo "failed: ${names[$i]}"
    echo "log: ${logs[$i]}"
    failed=1
  fi
done

if (( failed != 0 )); then
  echo
  echo "failed job logs:"
  for i in "${!pids[@]}"; do
    if [[ -f "${logs[$i]}" ]]; then
      echo "--- ${names[$i]} (${logs[$i]}) ---"
      tail -n 80 "${logs[$i]}"
    fi
  done
  exit 1
fi

echo "agent checks ok"
