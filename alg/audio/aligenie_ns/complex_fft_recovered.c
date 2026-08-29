/* Recovered from libmit_rtos.a complex_fft.o. */
#include "ns_c_recovered.h"
#include "dsp_fft.h"

int TSpl_ComplexFFT(int16_t *frfi, int stages, int mode)
{
    return dsp_fft_q15(frfi, stages, mode, TSpl_kSinTable1024);
}
