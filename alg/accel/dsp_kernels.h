#ifndef ALG_ACCEL_DSP_KERNELS_H
#define ALG_ACCEL_DSP_KERNELS_H

#if defined(__riscv) && \
    (__riscv_xlen == 32) && defined(__riscv_zpsfoperand)
#include "e907/dsp_kernels.h"
#else
#include "generic/dsp_kernels.h"
#endif

#endif
