#ifndef ALG_ACCEL_GENERIC_DSP_KERNELS_H
#define ALG_ACCEL_GENERIC_DSP_KERNELS_H

#include <stdint.h>

typedef uint32_t dsp_i16x2_t;

static inline int dsp_count_leading_zeros_u32(uint32_t value)
{
    return value == 0 ? 0 : __builtin_clz(value);
}

static inline int dsp_count_leading_signs_i32(int32_t value)
{
    if (value == 0)
        return 0;
    return __builtin_clz(value < 0 ? ~value : value) - 1;
}

static inline int dsp_count_leading_signs_i16(int16_t value)
{
    uint16_t normalized;

    if (value == 0)
        return 0;
    normalized = value < 0 ? (uint16_t)~value : (uint16_t)value;
    return __builtin_clz((uint32_t)normalized) - 17;
}

static inline dsp_i16x2_t dsp_i16x2_pack(int16_t low, int16_t high)
{
    return (uint16_t)low | ((uint32_t)(uint16_t)high << 16);
}

static inline dsp_i16x2_t dsp_i16x2_load(const int16_t *values)
{
    return dsp_i16x2_pack(values[0], values[1]);
}

static inline void dsp_i16x2_store(int16_t *values, dsp_i16x2_t value)
{
    values[0] = (int16_t)value;
    values[1] = (int16_t)(value >> 16);
}

static inline dsp_i16x2_t dsp_i16x2_add(dsp_i16x2_t a, dsp_i16x2_t b)
{
    return dsp_i16x2_pack((int16_t)a + (int16_t)b,
                          (int16_t)(a >> 16) + (int16_t)(b >> 16));
}

static inline dsp_i16x2_t dsp_i16x2_sub(dsp_i16x2_t a, dsp_i16x2_t b)
{
    return dsp_i16x2_pack((int16_t)a - (int16_t)b,
                          (int16_t)(a >> 16) - (int16_t)(b >> 16));
}

static inline dsp_i16x2_t dsp_i16x2_cras(dsp_i16x2_t a, dsp_i16x2_t b)
{
    return dsp_i16x2_pack((int16_t)a - (int16_t)(b >> 16),
                          (int16_t)(a >> 16) + (int16_t)b);
}

static inline dsp_i16x2_t dsp_i16x2_crsa(dsp_i16x2_t a, dsp_i16x2_t b)
{
    return dsp_i16x2_pack((int16_t)a + (int16_t)(b >> 16),
                          (int16_t)(a >> 16) - (int16_t)b);
}

static inline void dsp_i16x2_cmul(dsp_i16x2_t a, dsp_i16x2_t b,
                                  int32_t *real, int32_t *imag)
{
    int16_t ar = (int16_t)a;
    int16_t ai = (int16_t)(a >> 16);
    int16_t br = (int16_t)b;
    int16_t bi = (int16_t)(b >> 16);

    *real = (int32_t)ar * br - (int32_t)ai * bi;
    *imag = (int32_t)((uint32_t)((int32_t)ar * bi) +
                      (uint32_t)((int32_t)ai * br));
}

static inline dsp_i16x2_t dsp_i16x2_cmul_q15(dsp_i16x2_t a,
                                              dsp_i16x2_t b)
{
    int32_t real;
    int32_t imag;

    dsp_i16x2_cmul(a, b, &real, &imag);
    return dsp_i16x2_pack((real + 16384) >> 15,
                          (imag + 16384) >> 15);
}

static inline dsp_i16x2_t dsp_i16x2_cmul_q13(dsp_i16x2_t a,
                                              dsp_i16x2_t b)
{
    int32_t real;
    int32_t imag;

    dsp_i16x2_cmul(a, b, &real, &imag);
    return dsp_i16x2_pack((real + 65536) >> 17,
                          (imag + 65536) >> 17);
}

static inline dsp_i16x2_t dsp_i16x2_scale_q15(dsp_i16x2_t value,
                                               int16_t scale)
{
    int32_t low = (int16_t)value * (int32_t)scale;
    int32_t high = (int16_t)(value >> 16) * (int32_t)scale;

    return dsp_i16x2_pack((low + 16384) >> 15,
                          (high + 16384) >> 15);
}

static inline dsp_i16x2_t dsp_i16x2_shift_left(dsp_i16x2_t value, int shift)
{
    return dsp_i16x2_pack((int16_t)value << shift,
                          (int16_t)(value >> 16) << shift);
}

static inline dsp_i16x2_t dsp_i16x2_shift_right_round(dsp_i16x2_t value,
                                                       int shift)
{
    int rounding;

    if (shift == 0)
        return value;
    rounding = 1 << (shift - 1);

    return dsp_i16x2_pack(((int16_t)value + rounding) >> shift,
                          ((int16_t)(value >> 16) + rounding) >> shift);
}

static inline int dsp_i16x2_max_abs(dsp_i16x2_t value)
{
    int low = (int16_t)value;
    int high = (int16_t)(value >> 16);

    low = low < 0 ? -low : low;
    high = high < 0 ? -high : high;
    return low > high ? low : high;
}

static inline void dsp_i16x2_mul(int16_t a0, int16_t b0,
                                 int16_t a1, int16_t b1,
                                 int32_t *r0, int32_t *r1)
{
    *r0 = (int32_t)a0 * b0;
    *r1 = (int32_t)a1 * b1;
}

static inline int32_t dsp_mul_i16_i32_q15(int16_t a, int32_t b)
{
    return a * (b >> 15) + (a * (b & 0x7fff) >> 15);
}

static inline int32_t dsp_mul_i16_i32_p15(int16_t a, int32_t b)
{
    return a * (b >> 15) + ((a * (b & 0x7fff) + 16384) >> 15);
}

#endif
