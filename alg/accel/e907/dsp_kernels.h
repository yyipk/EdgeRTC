#ifndef ALG_ACCEL_E907_DSP_KERNELS_H
#define ALG_ACCEL_E907_DSP_KERNELS_H

#include <stdint.h>
#include <riscv-dsp.h>

typedef uint32_t dsp_i16x2_t;

static inline int dsp_count_leading_zeros_u32(uint32_t value)
{
    uint32_t zeros;

    __asm__ volatile("ff1 %0, %1" : "=r"(zeros) : "r"(value));
    return value == 0 ? 0 : (int)zeros;
}

static inline int dsp_count_leading_signs_i32(int32_t value)
{
    return value == 0 ? 0 : (int)__rv__clrs32(value);
}

static inline int dsp_count_leading_signs_i16(int16_t value)
{
    return value == 0 ? 0 :
        (int16_t)__rv__clrs16((uint16_t)value);
}

static inline dsp_i16x2_t dsp_i16x2_pack(int16_t low, int16_t high)
{
    return __rv__pkbb16((uint16_t)high, (uint16_t)low);
}

static inline dsp_i16x2_t dsp_i16x2_load(const int16_t *values)
{
    uint32_t result;

    values = __builtin_assume_aligned(values, sizeof(result));
    __builtin_memcpy(&result, values, sizeof(result));
    return result;
}

static inline void dsp_i16x2_store(int16_t *values, dsp_i16x2_t value)
{
    values = __builtin_assume_aligned(values, sizeof(value));
    __builtin_memcpy(values, &value, sizeof(value));
}

static inline dsp_i16x2_t dsp_i16x2_add(dsp_i16x2_t a, dsp_i16x2_t b)
{
    return __rv__add16(a, b);
}

static inline dsp_i16x2_t dsp_i16x2_sub(dsp_i16x2_t a, dsp_i16x2_t b)
{
    return __rv__sub16(a, b);
}

static inline dsp_i16x2_t dsp_i16x2_cras(dsp_i16x2_t a, dsp_i16x2_t b)
{
    return __rv__cras16(a, b);
}

static inline dsp_i16x2_t dsp_i16x2_crsa(dsp_i16x2_t a, dsp_i16x2_t b)
{
    return __rv__crsa16(a, b);
}

static inline void dsp_i16x2_cmul(dsp_i16x2_t a, dsp_i16x2_t b,
                                  int32_t *real, int32_t *imag)
{
    uint64_t cross = __rv__smulx16(a, b);

    *real = __rv__smdrs(a, b);
    *imag = (int32_t)((uint32_t)(int32_t)cross +
                      (uint32_t)(int32_t)(cross >> 32));
}

static inline dsp_i16x2_t dsp_i16x2_cmul_q15(dsp_i16x2_t a,
                                              dsp_i16x2_t b)
{
    int32_t real = __rv__smdrs(a, b);
    int32_t imag = __rv__kmxda(a, b);

    return dsp_i16x2_pack((real + 16384) >> 15,
                          (imag + 16384) >> 15);
}

static inline dsp_i16x2_t dsp_i16x2_cmul_q13(dsp_i16x2_t a,
                                              dsp_i16x2_t b)
{
    int32_t real = __rv__smdrs(a, b);
    int32_t imag = __rv__kmxda(a, b);

    return dsp_i16x2_pack((real + 65536) >> 17,
                          (imag + 65536) >> 17);
}

static inline dsp_i16x2_t dsp_i16x2_scale_q15(dsp_i16x2_t value,
                                               int16_t scale)
{
    uint64_t product = __rv__smul16(value,
        dsp_i16x2_pack(scale, scale));

    return dsp_i16x2_pack(((int32_t)product + 16384) >> 15,
                          ((int32_t)(product >> 32) + 16384) >> 15);
}

static inline dsp_i16x2_t dsp_i16x2_shift_left(dsp_i16x2_t value, int shift)
{
    return __rv__sll16(value, shift);
}

static inline dsp_i16x2_t dsp_i16x2_shift_right_round(dsp_i16x2_t value,
                                                       int shift)
{
    return __rv__sra16_u(value, shift);
}

static inline int dsp_i16x2_max_abs(dsp_i16x2_t value)
{
    uint32_t absolute = __rv__kabs16(value);
    uint32_t low = absolute & UINT16_MAX;
    uint32_t high = absolute >> 16;

    return low > high ? (int)low : (int)high;
}

static inline void dsp_i16x2_mul(int16_t a0, int16_t b0,
                                 int16_t a1, int16_t b1,
                                 int32_t *r0, int32_t *r1)
{
    uint32_t a = dsp_i16x2_pack(a0, a1);
    uint32_t b = dsp_i16x2_pack(b0, b1);
    uint64_t result = __rv__smul16(a, b);

    *r0 = (int32_t)result;
    *r1 = (int32_t)(result >> 32);
}

static inline int32_t dsp_mul_i16_i32_q15(int16_t a, int32_t b)
{
    if (a == INT16_MIN && b == INT32_MIN)
        return INT32_MIN;
    return __rv__kmmwb2(b, (uint16_t)a);
}

static inline int32_t dsp_mul_i16_i32_p15(int16_t a, int32_t b)
{
    if (a == INT16_MIN && b == INT32_MIN)
        return INT32_MIN;
    return __rv__kmmwb2_u(b, (uint16_t)a);
}

#endif
