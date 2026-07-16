# Zero Rosetta Tasks

This directory contains Zero implementations for a verified subset of Rosetta
Code tasks. `manifest.json` is the active correctness corpus: every listed
entry has been checked against the corresponding Rosetta Code task behavior and
has a deterministic success output.

Some extra `.0`/`.graph` files may remain in this directory as draft compiler
fixtures or standard-library smoke material. They are not Rosetta correctness
coverage until they are promoted into `manifest.json`.

The repository checks these tasks with `pnpm run rosetta:local`. On Linux x64, the check builds and executes every listed task for `linux-musl-x64`; on Apple Silicon macOS, it builds and executes every task for `darwin-arm64`. Other hosts still verify buildability for the default target. Pass `--target <target>` or set `ZERO_ROSETTA_TARGET` to check a specific supported target.

Current verified manifest: 53 tasks.
