/* Recovered from libmit_rtos.a norm_w16.o. */
#include "ns_c_recovered.h"
#include "dsp_kernels.h"

int TSpl_NormW16(int16_t value)
{
    return dsp_count_leading_signs_i16(value);
}
