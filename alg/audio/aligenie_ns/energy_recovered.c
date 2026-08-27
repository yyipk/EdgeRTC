/* Recovered from libmit_rtos.a energy.o. */
#include "ns_c_recovered.h"

int32_t TSpl_Energy(int16_t *vector, int length, int *scale_factor)
{
    int32_t energy = 0;
    int i;
    int scale = TSpl_GetScalingSquare(vector, length, length);

    for (i = 0; i < length; ++i)
        energy += ((int32_t)vector[i] * vector[i]) >> scale;
    *scale_factor = scale;
    return energy;
}
