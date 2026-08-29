/* Recovered from libmit_rtos.a norm_w32.o. */
#include "ns_c_recovered.h"
#include "dsp_kernels.h"

int TSpl_NormW32(int32_t value)
{
    return dsp_count_leading_signs_i32(value);
}
