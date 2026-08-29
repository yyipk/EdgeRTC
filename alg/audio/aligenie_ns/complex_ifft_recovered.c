/* Recovered from libmit_rtos.a complex_ifft.o. */
#include "ns_c_recovered.h"
#include "dsp_fft.h"

int TSpl_ComplexIFFT(int16_t *frfi, int stages, int mode)
{
    return dsp_ifft_q15(frfi, stages, mode, TSpl_kSinTable1024);
}
