/* Recovered from libmit_rtos.a norm_u32.o. */
#include "ns_c_recovered.h"
#include "dsp_kernels.h"

int TSpl_NormU32(uint32_t value)
{
    return dsp_count_leading_zeros_u32(value);
}
