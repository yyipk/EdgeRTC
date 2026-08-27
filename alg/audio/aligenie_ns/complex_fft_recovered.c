/* Recovered from libmit_rtos.a complex_fft.o. */
#include "ns_c_recovered.h"

int TSpl_ComplexFFT(int16_t *frfi, int stages, int mode)
{
    int i, j, l, k, istep, n, m;
    int16_t wr, wi;
    int32_t tr32, ti32, qr32, qi32;

    n = 1 << stages;
    if (n > 1024)
        return -1;

    l = 1;
    k = 9;
    if (mode == 0) {
        while (l < n) {
            istep = l << 1;
            for (m = 0; m < l; ++m) {
                j = m << k;
                wr = TSpl_kSinTable1024[j + 256];
                wi = (int16_t)-TSpl_kSinTable1024[j];

                for (i = m; i < n; i += istep) {
                    j = i + l;
                    tr32 = ((int32_t)wr * frfi[2 * j] -
                            (int32_t)wi * frfi[2 * j + 1]) >> 15;
                    ti32 = ((int32_t)wr * frfi[2 * j + 1] +
                            (int32_t)wi * frfi[2 * j]) >> 15;
                    qr32 = frfi[2 * i];
                    qi32 = frfi[2 * i + 1];
                    frfi[2 * j] = (int16_t)((qr32 - tr32) >> 1);
                    frfi[2 * j + 1] = (int16_t)((qi32 - ti32) >> 1);
                    frfi[2 * i] = (int16_t)((qr32 + tr32) >> 1);
                    frfi[2 * i + 1] = (int16_t)((qi32 + ti32) >> 1);
                }
            }
            --k;
            l = istep;
        }
    } else {
        while (l < n) {
            istep = l << 1;
            for (m = 0; m < l; ++m) {
                j = m << k;
                wr = TSpl_kSinTable1024[j + 256];
                wi = (int16_t)-TSpl_kSinTable1024[j];

                for (i = m; i < n; i += istep) {
                    j = i + l;
                    tr32 = (int32_t)wr * frfi[2 * j] -
                           (int32_t)wi * frfi[2 * j + 1] + 1;
                    ti32 = (int32_t)wr * frfi[2 * j + 1] +
                           (int32_t)wi * frfi[2 * j] + 1;
                    tr32 >>= 1;
                    ti32 >>= 1;
                    qr32 = (int32_t)frfi[2 * i] * (1 << 14);
                    qi32 = (int32_t)frfi[2 * i + 1] * (1 << 14);
                    frfi[2 * j] = (int16_t)((qr32 - tr32 + 16384) >> 15);
                    frfi[2 * j + 1] = (int16_t)((qi32 - ti32 + 16384) >> 15);
                    frfi[2 * i] = (int16_t)((qr32 + tr32 + 16384) >> 15);
                    frfi[2 * i + 1] = (int16_t)((qi32 + ti32 + 16384) >> 15);
                }
            }
            --k;
            l = istep;
        }
    }
    return 0;
}
