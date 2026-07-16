#!/usr/bin/env bash
set -euo pipefail

version="${ZERO_ZIG_VERSION:-0.14.1}"
release_tag="${ZERO_ZIG_RELEASE_TAG:-toolchain-zig-${version}}"
upstream_base="${ZERO_ZIG_UPSTREAM_BASE:-https://ziglang.org/download/${version}}"
work_dir="${ZERO_ZIG_MIRROR_WORK_DIR:-/tmp/zero-zig-mirror-${version}}"

assets=(
  "zig-x86_64-linux-${version}.tar.xz:24aeeec8af16c381934a6cd7d95c807a8cb2cf7df9fa40d359aa884195c4716c"
  "zig-aarch64-macos-${version}.tar.xz:39f3dc5e79c22088ce878edc821dedb4ca5a1cd9f5ef915e9b3cc3053e8faefa"
  "zig-x86_64-macos-${version}.tar.xz:b0f8bdfb9035783db58dd6c19d7dea89892acc3814421853e5752fe4573e5f43"
)

check_sha256() {
  expected="$1"
  path="$2"
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s  %s\n' "$expected" "$path" | sha256sum -c -
  else
    printf '%s  %s\n' "$expected" "$path" | shasum -a 256 -c -
  fi
}

if ! command -v gh >/dev/null 2>&1; then
  echo "gh is required to upload mirrored Zig assets" >&2
  exit 1
fi

rm -rf "$work_dir"
mkdir -p "$work_dir"

: > "${work_dir}/CHECKSUMS.txt"
for asset in "${assets[@]}"; do
  name="${asset%%:*}"
  sha="${asset##*:}"
  path="${work_dir}/${name}"
  echo "Downloading ${name}"
  curl -fsSL --retry 3 --retry-delay 2 --output "$path" "${upstream_base}/${name}"
  check_sha256 "$sha" "$path"
  printf '%s  %s\n' "$sha" "$name" >> "${work_dir}/CHECKSUMS.txt"
done

if ! gh release view "$release_tag" >/dev/null 2>&1; then
  gh release create "$release_tag" \
    --title "Zig ${version} mirror" \
    --notes "Repo-owned mirror of pinned Zig ${version} archives used by CI and release workflows." \
    --latest=false
fi

for asset in "${assets[@]}"; do
  name="${asset%%:*}"
  gh release upload "$release_tag" "${work_dir}/${name}" --clobber
done
gh release upload "$release_tag" "${work_dir}/CHECKSUMS.txt" --clobber

echo "Mirrored Zig ${version} assets to GitHub release ${release_tag}"
