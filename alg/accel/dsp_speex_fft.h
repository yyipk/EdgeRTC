#ifndef ALG_ACCEL_DSP_SPEEX_FFT_H
#define ALG_ACCEL_DSP_SPEEX_FFT_H

#if defined(FIXED_POINT) && defined(__riscv) && \
    (__riscv_xlen == 32) && defined(__riscv_zpsfoperand)
#include "e907/dsp_speex_fft.h"
#else
static inline int dsp_speex_fft_bfly4(kiss_fft_cpx *output,
                                      size_t fstride,
                                      const kiss_fft_cfg state,
                                      int m, int count, int stride)
{
    (void)output;
    (void)fstride;
    (void)state;
    (void)m;
    (void)count;
    (void)stride;
    return 0;
}

static inline int dsp_speex_fft_bfly5(kiss_fft_cpx *output,
                                      size_t fstride,
                                      const kiss_fft_cfg state, int m)
{
    (void)output;
    (void)fstride;
    (void)state;
    (void)m;
    return 0;
}
#endif

#endif
