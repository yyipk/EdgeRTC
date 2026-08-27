/* Recovered from libmit_rtos.a spl_sqrt.o. */
#include "ns_c_recovered.h"

#include <limits.h>

int32_t TSpl_SqrtLocal(int32_t in)
{
    int16_t x_half;
    int16_t t16;
    int32_t a;
    int32_t b;
    int32_t x2;

    b = in >> 1;
    b = b - INT32_C(0x40000000);
    x_half = (int16_t)(b >> 16);
    b = b + INT32_C(0x40000000);
    b = b + INT32_C(0x40000000);
    x2 = (int32_t)x_half * x_half * 2;
    a = -x2;
    b = b + (a >> 1);
    a >>= 16;
    a = a * a * 2;
    t16 = (int16_t)(a >> 16);
    b += -20480 * t16 * 2;
    a = x_half * t16 * 2;
    t16 = (int16_t)(a >> 16);
    b += 28672 * t16 * 2;
    t16 = (int16_t)(x2 >> 16);
    a = x_half * t16 * 2;
    b = b + (a >> 1);
    b = b + 32768;
    return b;
}

int32_t TSpl_Sqrt(int32_t value)
{
    int16_t x_norm;
    int16_t nshift;
    int16_t t16;
    int16_t sh;
    int32_t a;
    int16_t k_sqrt_2 = 23170;

    a = value;
    if (a == 0)
        return 0;
    x_norm = (int16_t)TSpl_NormW32(a);
    a <<= x_norm;
    if (a >= 0x7fff8000)
        a = INT32_MAX;
    else
        a += 32768;
    nshift = (int16_t)(x_norm >> 1);
    sh = (int16_t)-nshift;
    a &= INT32_C(0xffff0000);
    if (a < 0)
        a = -a;
    a = TSpl_SqrtLocal(a);
    if (-2 * sh == x_norm) {
        t16 = (int16_t)(a >> 16);
        a = k_sqrt_2 * t16 * 2;
        a += 32768;
        a &= INT32_C(0x7fff0000);
        a >>= 15;
    } else {
        a >>= 16;
    }
    a &= 0xffff;
    if (sh >= 0)
        return a << sh;
    return a >> -sh;
}
