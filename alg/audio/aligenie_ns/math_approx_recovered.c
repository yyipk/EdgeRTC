/* Recovered from libmit_rtos.a math_approx.o. */
#include "ns_c_recovered.h"

#define SHR16(a, shift) ((a) >> (shift))
#define SHL16(a, shift) ((a) << (shift))
#define ADD16(a, b) ((int16_t)((int16_t)(a) + (int16_t)(b)))
#define MULT16_16(a, b) ((int32_t)(int16_t)(a) * (int32_t)(int16_t)(b))
#define MULT16_16_Q14(a, b) (MULT16_16((a), (b)) >> 14)
#define EXTEND32(x) ((int32_t)(x))
#define VSHR32(a, shift) ((shift) > 0 ? (a) >> (shift) : (a) << -(shift))

int32_t spx_exp2(int16_t x)
{
    int integer;
    int16_t frac;

    integer = SHR16(x, 11);
    if (integer > 14)
        return INT32_MAX;
    if (integer < -15)
        return 0;
    frac = SHL16(x - SHL16(integer, 11), 3);
    frac = ADD16(16384,
                 MULT16_16_Q14(frac,
                                ADD16(11356,
                                      MULT16_16_Q14(frac,
                                                    ADD16(3726,
                                                          MULT16_16_Q14(1301,
                                                                        frac))))));
    return VSHR32(EXTEND32(frac), -integer - 2);
}

#undef VSHR32
#undef EXTEND32
#undef MULT16_16_Q14
#undef MULT16_16
#undef ADD16
#undef SHL16
#undef SHR16

int32_t spx_exp(int16_t x)
{
    if (x > 21290)
        return INT32_MAX;
    if (x < -21290)
        return 0;
    return spx_exp2((int16_t)(((int32_t)23637 * x + 8192) >> 14));
}
