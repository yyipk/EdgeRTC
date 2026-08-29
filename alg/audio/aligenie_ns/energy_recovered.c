/* Recovered from libmit_rtos.a energy.o. */
#include "ns_c_recovered.h"
#include "dsp_kernels.h"

int32_t TSpl_Energy(int16_t *vector, int length, int *scale_factor)
{
    int32_t energy = 0;
    int i;
    int scale = TSpl_GetScalingSquare(vector, length, length);

    for (i = 0; i + 1 < length; i += 2) {
        int32_t square0;
        int32_t square1;

        dsp_i16x2_mul(vector[i], vector[i],
                      vector[i + 1], vector[i + 1],
                      &square0, &square1);
        energy += square0 >> scale;
        energy += square1 >> scale;
    }
    if (i < length)
        energy += ((int32_t)vector[i] * vector[i]) >> scale;
    *scale_factor = scale;
    return energy;
}
