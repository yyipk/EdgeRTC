/* Recovered from libmit_rtos.a get_scaling_square.o. */
#include "ns_c_recovered.h"

int TSpl_GetScalingSquare(const int16_t *vector, int length, int times)
{
    int nbits = TSpl_GetSizeInBits((uint32_t)times);
    int i;
    int16_t smax = -1;
    int16_t sabs;
    const int16_t *sptr = vector;
    int t;
    int looptimes = length;

    for (i = looptimes; i > 0; --i) {
        sabs = *sptr > 0 ? *sptr++ : (int16_t)-*sptr++;
        smax = sabs > smax ? sabs : smax;
    }
    t = TSpl_NormW32((int32_t)smax * smax);
    if (smax == 0)
        return 0;
    return t > nbits ? 0 : nbits - t;
}
