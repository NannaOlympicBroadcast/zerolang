# Native C API Contracts

This file defines the public C API contract vocabulary used by
`include/zero.h`. The macros in `include/zero_contracts.h` are zero-cost C
annotations; the contract is enforced by `pnpm run native:contracts`.

## Pointer Parameters

Every exported pointer parameter in `include/zero.h` must carry exactly one
access effect.

| Effect | Contract |
| --- | --- |
| `Z_IN T *p` | Borrowed read-only input. The callee may read `*p` during the call only. It must not mutate, free, or retain `p`. |
| `Z_OUT T *p` | Caller-owned output storage. The callee initializes or overwrites `*p`. Ownership of the outer storage stays with the caller. |
| `Z_INOUT T *p` | Caller-owned mutable storage. The callee may read and write `*p` during the call. Ownership stays with the caller. |
| `Z_SINK T *p` | Consumed pointer value. The caller must not use the incoming pointer after the call unless the return value gives a replacement. |
| `Z_OPTIONAL` | Nullable parameter modifier. It must be paired with one of the access effects above. |

## Pointer Returns

Every exported function returning a pointer must state ownership.

| Return marker | Contract |
| --- | --- |
| `Z_RET_OWNED T *` | Caller owns the returned pointer and releases it with `free` or the documented type-specific free function. |
| `Z_RET_BORROWED T *` | Caller must not free the pointer. The storage is static, table-backed, or owned by a borrowed input. |
| `Z_RET_OPTIONAL` | Nullable return modifier. It must be paired with `Z_RET_OWNED` or `Z_RET_BORROWED`. |

Owned returns are non-null unless marked `Z_RET_OPTIONAL`. Borrowed returns are
valid until the documented table/input lifetime ends and must not be retained
past that lifetime.

## Function Families

| Family | Contract |
| --- | --- |
| `zbuf_*` | `zbuf_init` initializes caller storage. `zbuf_append*` mutate initialized buffers. `zbuf_free` releases owned buffer storage and leaves the buffer empty. |
| `z_checked_*` | Allocation helpers return owned non-null storage or terminate the process with the allocation fatal path. `z_checked_reallocarray` consumes the old pointer and returns the replacement. |
| `z_strdup`, `z_strndup`, path helpers | Return owned heap strings. Optional path lookup helpers return `NULL` when no path exists. |
| `z_read_file` | Returns an owned heap buffer on success; returns `NULL` and writes `ZDiag` on I/O failure. |
| `z_write_file`, `z_write_binary_file` | Borrow input buffers for the duration of the call; return `false` and write `ZDiag` on I/O failure. |
| `z_free_source`, `z_free_manifest`, `z_free_ir_program`, `z_free_program` | Consume owned fields inside caller-owned structs, then leave them reusable or inert. They do not free the outer stack object. |
| target/backend lookup helpers | Return borrowed table/static strings or borrowed table rows. Optional lookup helpers return `NULL` for unknown targets or unsupported requests. |
| emit/build helpers | Borrow the IR/target/toolchain inputs, append or write through caller-owned output buffers, and report failures through `ZDiag`. |

## Internal Representation Boundary

Textual names are accepted at parsing, graph, manifest, CLI, and import
boundaries. Internal compiler stages should carry typed ids, enums, helper
metadata, and graph facts instead of repeatedly branching on strings. When a
string comparison remains in IR or backend code, it should be a boundary decode
or a temporary step toward a typed table.

## Machine Contract

`scripts/native-contracts.mts` checks `include/zero.h` for:

- all contract macros exist in `include/zero_contracts.h`
- every exported pointer parameter has `Z_IN`, `Z_OUT`, `Z_INOUT`, or `Z_SINK`
- `Z_OPTIONAL` parameters are paired with an access effect
- every exported pointer return has `Z_RET_OWNED` or `Z_RET_BORROWED`
- `Z_RET_OPTIONAL` returns are paired with return ownership

