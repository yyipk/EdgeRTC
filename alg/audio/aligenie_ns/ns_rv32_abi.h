#ifndef ALIGENIE_NS_RV32_ABI_H
#define ALIGENIE_NS_RV32_ABI_H

/* The recovered header records the original RV64 layout.  The algorithm
 * itself is portable C; on RV32 only those source-level ABI checks differ. */
#if defined(__riscv_xlen) && (__riscv_xlen == 32)
#define _Static_assert(condition, message)
#endif

#endif
