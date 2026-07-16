#ifndef ZERO_C_ZERO_CONTRACTS_H
#define ZERO_C_ZERO_CONTRACTS_H

/*
 * Zero-cost access-effect annotations for C API pointer contracts.
 *
 * These macros document the caller/callee contract at the declaration site and
 * are intentionally empty for C compilers. The contract checker keeps exported
 * native APIs from accepting unannotated pointer parameters or pointer returns.
 */
#define Z_IN
#define Z_OUT
#define Z_INOUT
#define Z_SINK
#define Z_OPTIONAL
#define Z_RET_OWNED
#define Z_RET_BORROWED
#define Z_RET_OPTIONAL

#endif
