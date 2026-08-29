#ifndef ALG_ACCEL_DSP_FFT_H
#define ALG_ACCEL_DSP_FFT_H

#include <stdint.h>

int dsp_fft_q15(int16_t *data, int stages, int mode,
                const int16_t *sine_table);
int dsp_ifft_q15(int16_t *data, int stages, int mode,
                 const int16_t *sine_table);

#endif
