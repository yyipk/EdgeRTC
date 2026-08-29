#ifndef ALG_ACCEL_DSP_SPEEX_FFT_320_H
#define ALG_ACCEL_DSP_SPEEX_FFT_320_H

#include <stdint.h>

#define DSP_SPEEX_FFT_320_SAMPLES 320

#if defined(__riscv) && \
    (__riscv_xlen == 32) && defined(__riscv_zpsfoperand)
#include "e907/dsp_speex_fft_320_platform.h"
#else
#include "generic/dsp_speex_fft_320_platform.h"
#endif

#if DSP_SPEEX_FFT_320_AVAILABLE
void dsp_speex_fft_320_forward(dsp_speex_fft_320_t *context,
                               const int16_t *input,
                               int16_t *output);
void dsp_speex_fft_320_inverse(dsp_speex_fft_320_t *context,
                               const int16_t *input,
                               int16_t *output);
#endif

#endif
