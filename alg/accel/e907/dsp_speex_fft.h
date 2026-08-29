#ifndef ALG_ACCEL_E907_DSP_SPEEX_FFT_H
#define ALG_ACCEL_E907_DSP_SPEEX_FFT_H

#include "dsp_kernels.h"

static inline dsp_i16x2_t dsp_speex_fft_load(const kiss_fft_cpx *value)
{
    return dsp_i16x2_load(&value->r);
}

static inline void dsp_speex_fft_store(kiss_fft_cpx *value,
                                       dsp_i16x2_t packed)
{
    dsp_i16x2_store(&value->r, packed);
}

static inline int dsp_speex_fft_bfly4(kiss_fft_cpx *output,
                                      size_t fstride,
                                      const kiss_fft_cfg state,
                                      int m, int count, int stride)
{
    kiss_fft_cpx *output_start = output;
    int m2 = 2 * m;
    int m3 = 3 * m;
    int i;
    int j;

    if (state->inverse) {
        for (i = 0; i < count; i++) {
            const kiss_fft_cpx *tw1 = state->twiddles;
            const kiss_fft_cpx *tw2 = state->twiddles;
            const kiss_fft_cpx *tw3 = state->twiddles;

            output = output_start + i * stride;
            for (j = 0; j < m; j++) {
                dsp_i16x2_t f0 = dsp_speex_fft_load(output);
                dsp_i16x2_t f1 = dsp_i16x2_cmul_q15(
                    dsp_speex_fft_load(output + m),
                    dsp_speex_fft_load(tw1));
                dsp_i16x2_t f2 = dsp_i16x2_cmul_q15(
                    dsp_speex_fft_load(output + m2),
                    dsp_speex_fft_load(tw2));
                dsp_i16x2_t f3 = dsp_i16x2_cmul_q15(
                    dsp_speex_fft_load(output + m3),
                    dsp_speex_fft_load(tw3));
                dsp_i16x2_t sum02 = dsp_i16x2_add(f0, f2);
                dsp_i16x2_t diff02 = dsp_i16x2_sub(f0, f2);
                dsp_i16x2_t sum13 = dsp_i16x2_add(f1, f3);
                dsp_i16x2_t diff13 = dsp_i16x2_sub(f1, f3);

                dsp_speex_fft_store(output,
                                    dsp_i16x2_add(sum02, sum13));
                dsp_speex_fft_store(output + m2,
                                    dsp_i16x2_sub(sum02, sum13));
                dsp_speex_fft_store(output + m,
                                    dsp_i16x2_cras(diff02, diff13));
                dsp_speex_fft_store(output + m3,
                                    dsp_i16x2_crsa(diff02, diff13));
                tw1 += fstride;
                tw2 += 2 * fstride;
                tw3 += 3 * fstride;
                output++;
            }
        }
    } else {
        for (i = 0; i < count; i++) {
            const kiss_fft_cpx *tw1 = state->twiddles;
            const kiss_fft_cpx *tw2 = state->twiddles;
            const kiss_fft_cpx *tw3 = state->twiddles;

            output = output_start + i * stride;
            for (j = 0; j < m; j++) {
                dsp_i16x2_t f0 = dsp_i16x2_shift_right_round(
                    dsp_speex_fft_load(output), 2);
                dsp_i16x2_t f1 = dsp_i16x2_cmul_q13(
                    dsp_speex_fft_load(output + m),
                    dsp_speex_fft_load(tw1));
                dsp_i16x2_t f2 = dsp_i16x2_cmul_q13(
                    dsp_speex_fft_load(output + m2),
                    dsp_speex_fft_load(tw2));
                dsp_i16x2_t f3 = dsp_i16x2_cmul_q13(
                    dsp_speex_fft_load(output + m3),
                    dsp_speex_fft_load(tw3));
                dsp_i16x2_t sum02 = dsp_i16x2_add(f0, f2);
                dsp_i16x2_t diff02 = dsp_i16x2_sub(f0, f2);
                dsp_i16x2_t sum13 = dsp_i16x2_add(f1, f3);
                dsp_i16x2_t diff13 = dsp_i16x2_sub(f1, f3);

                dsp_speex_fft_store(output,
                                    dsp_i16x2_add(sum02, sum13));
                dsp_speex_fft_store(output + m2,
                                    dsp_i16x2_sub(sum02, sum13));
                dsp_speex_fft_store(output + m,
                                    dsp_i16x2_crsa(diff02, diff13));
                dsp_speex_fft_store(output + m3,
                                    dsp_i16x2_cras(diff02, diff13));
                tw1 += fstride;
                tw2 += 2 * fstride;
                tw3 += 3 * fstride;
                output++;
            }
        }
    }
    return 1;
}

static inline int dsp_speex_fft_bfly5(kiss_fft_cpx *output,
                                      size_t fstride,
                                      const kiss_fft_cfg state, int m)
{
    const kiss_fft_cpx *twiddles = state->twiddles;
    dsp_i16x2_t ya = dsp_speex_fft_load(twiddles + fstride * m);
    dsp_i16x2_t yb = dsp_speex_fft_load(twiddles + 2 * fstride * m);
    kiss_fft_cpx *output0 = output;
    kiss_fft_cpx *output1 = output0 + m;
    kiss_fft_cpx *output2 = output0 + 2 * m;
    kiss_fft_cpx *output3 = output0 + 3 * m;
    kiss_fft_cpx *output4 = output0 + 4 * m;
    int u;

    for (u = 0; u < m; u++) {
        dsp_i16x2_t f0 = dsp_speex_fft_load(output0);
        dsp_i16x2_t f1 = dsp_speex_fft_load(output1);
        dsp_i16x2_t f2 = dsp_speex_fft_load(output2);
        dsp_i16x2_t f3 = dsp_speex_fft_load(output3);
        dsp_i16x2_t f4 = dsp_speex_fft_load(output4);
        dsp_i16x2_t s5;
        dsp_i16x2_t s6;
        dsp_i16x2_t s7;
        dsp_i16x2_t s8;
        dsp_i16x2_t s9;
        dsp_i16x2_t s10;
        dsp_i16x2_t s11;
        dsp_i16x2_t s12;

        if (!state->inverse) {
            f0 = dsp_i16x2_scale_q15(f0, 32767 / 5);
            f1 = dsp_i16x2_scale_q15(f1, 32767 / 5);
            f2 = dsp_i16x2_scale_q15(f2, 32767 / 5);
            f3 = dsp_i16x2_scale_q15(f3, 32767 / 5);
            f4 = dsp_i16x2_scale_q15(f4, 32767 / 5);
        }
        if (u == 0) {
            int16_t scale = twiddles->r;

            f1 = dsp_i16x2_scale_q15(f1, scale);
            f2 = dsp_i16x2_scale_q15(f2, scale);
            f3 = dsp_i16x2_scale_q15(f3, scale);
            f4 = dsp_i16x2_scale_q15(f4, scale);
        } else {
            f1 = dsp_i16x2_cmul_q15(
                f1, dsp_speex_fft_load(twiddles + u * fstride));
            f2 = dsp_i16x2_cmul_q15(
                f2, dsp_speex_fft_load(twiddles + 2 * u * fstride));
            f3 = dsp_i16x2_cmul_q15(
                f3, dsp_speex_fft_load(twiddles + 3 * u * fstride));
            f4 = dsp_i16x2_cmul_q15(
                f4, dsp_speex_fft_load(twiddles + 4 * u * fstride));
        }

        s7 = dsp_i16x2_add(f1, f4);
        s10 = dsp_i16x2_sub(f1, f4);
        s8 = dsp_i16x2_add(f2, f3);
        s9 = dsp_i16x2_sub(f2, f3);
        dsp_speex_fft_store(output0,
                            dsp_i16x2_add(f0, dsp_i16x2_add(s7, s8)));

        s5 = dsp_i16x2_add(f0,
            dsp_i16x2_add(dsp_i16x2_scale_q15(s7, (int16_t)ya),
                          dsp_i16x2_scale_q15(s8, (int16_t)yb)));
        s6 = dsp_i16x2_crsa(0,
            dsp_i16x2_add(dsp_i16x2_scale_q15(s10,
                                              (int16_t)(ya >> 16)),
                          dsp_i16x2_scale_q15(s9,
                                              (int16_t)(yb >> 16))));
        dsp_speex_fft_store(output1, dsp_i16x2_sub(s5, s6));
        dsp_speex_fft_store(output4, dsp_i16x2_add(s5, s6));

        s11 = dsp_i16x2_add(f0,
            dsp_i16x2_add(dsp_i16x2_scale_q15(s7, (int16_t)yb),
                          dsp_i16x2_scale_q15(s8, (int16_t)ya)));
        s12 = dsp_i16x2_cras(0,
            dsp_i16x2_sub(dsp_i16x2_scale_q15(s10,
                                              (int16_t)(yb >> 16)),
                          dsp_i16x2_scale_q15(s9,
                                              (int16_t)(ya >> 16))));
        dsp_speex_fft_store(output2, dsp_i16x2_add(s11, s12));
        dsp_speex_fft_store(output3, dsp_i16x2_sub(s11, s12));

        output0++;
        output1++;
        output2++;
        output3++;
        output4++;
    }
    return 1;
}

#endif
