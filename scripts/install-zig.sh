#!/usr/bin/env bash
set -euo pipefail

version="${ZERO_ZIG_VERSION:-0.14.1}"
release_tag="${ZERO_ZIG_RELEASE_TAG:-toolchain-zig-${version}}"
mirror_base="${ZERO_ZIG_MIRROR_BASE:-https://github.com/vercel-labs/zerolang/releases/download/${release_tag}}"
cache_root="${ZERO_ZIG_CACHE_DIR:-${PWD}/.zero/toolchains}"

host_os="$(uname -s)"
host_arch="$(uname -m)"

case "${host_os}:${host_arch}" in
  Linux:x86_64)
    platform="x86_64-linux"
    archive_name="zig-x86_64-linux-${version}.tar.xz"
    archive_sha="24aeeec8af16c381934a6cd7d95c807a8cb2cf7df9fa40d359aa884195c4716c"
    ;;
  Darwin:arm64 | Darwin:aarch64)
    platform="aarch64-macos"
    archive_name="zig-aarch64-macos-${version}.tar.xz"
    archive_sha="39f3dc5e79c22088ce878edc821dedb4ca5a1cd9f5ef915e9b3cc3053e8faefa"
    ;;
  Darwin:x86_64)
    platform="x86_64-macos"
    archive_name="zig-x86_64-macos-${version}.tar.xz"
    archive_sha="b0f8bdfb9035783db58dd6c19d7dea89892acc3814421853e5752fe4573e5f43"
    ;;
  *)
    echo "unsupported Zig host for pinned installer: ${host_os}:${host_arch}" >&2
    exit 1
    ;;
esac

install_dir="${ZERO_ZIG_INSTALL_DIR:-${cache_root}/zig-${version}-${platform}}"
archive_path="${cache_root}/${archive_name}"
archive_url="${mirror_base}/${archive_name}"

check_sha256() {
  expected="$1"
  path="$2"
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s  %s\n' "$expected" "$path" | sha256sum -c -
  else
    printf '%s  %s\n' "$expected" "$path" | shasum -a 256 -c -
  fi
}

if [ -x "${install_dir}/zig" ] && [ "$("${install_dir}/zig" version)" = "$version" ]; then
  echo "Zig ${version} already installed at ${install_dir}"
else
  mkdir -p "$cache_root"
  if [ ! -f "$archive_path" ] || ! check_sha256 "$archive_sha" "$archive_path" >/dev/null 2>&1; then
    echo "Downloading ${archive_name} from repo mirror"
    curl -fsSL --retry 3 --retry-delay 2 --output "$archive_path" "$archive_url"
  fi
  check_sha256 "$archive_sha" "$archive_path"

  tmp_dir="${install_dir}.tmp.$$"
  rm -rf "$tmp_dir"
  mkdir -p "$tmp_dir"
  tar -xf "$archive_path" -C "$tmp_dir" --strip-components=1
  rm -rf "$install_dir"
  mv "$tmp_dir" "$install_dir"
fi

if [ "$("${install_dir}/zig" version)" != "$version" ]; then
  echo "installed Zig version does not match ${version}: $("${install_dir}/zig" version)" >&2
  exit 1
fi

if [ -n "${GITHUB_PATH:-}" ]; then
  printf '%s\n' "$install_dir" >> "$GITHUB_PATH"
fi

echo "Zig ${version} installed at ${install_dir}"
