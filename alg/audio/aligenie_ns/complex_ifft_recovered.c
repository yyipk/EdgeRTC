/* Recovered from libmit_rtos.a complex_ifft.o. */
#include "ns_c_recovered.h"

int TSpl_ComplexIFFT(int16_t *frfi, int stages, int mode)
{
    int i, j, l, k, istep, n, m, scale, shift;
    int16_t wr, wi;
    int32_t tr32, ti32, qr32, qi32;
    int32_t tmp32, round2;

    n = 1 << stages;
    if (n > 1024)
        return -1;
    scale = 0;
    l = 1;
    k = 9;
    while (l < n) {
        shift = 0;
        round2 = 8192;
        tmp32 = TSpl_MaxAbsValueW16(frfi, (int16_t)(2 * n));
        if (tmp32 > 13573) {
            ++shift;
            ++scale;
            round2 <<= 1;
        }
        if (tmp32 > 27146) {
            ++shift;
            ++scale;
            round2 <<= 1;
        }
        istep = l << 1;
        if (mode == 0) {
            for (m = 0; m < l; ++m) {
                j = m << k;
                wr = TSpl_kSinTable1024[j + 256];
                wi = TSpl_kSinTable1024[j];
                for (i = m; i < n; i += istep) {
                    j = i + l;
                    qr32 = frfi[2 * i];
                    qi32 = frfi[2 * i + 1];
                    tr32 = (int32_t)wr * frfi[2 * j] -
                           (int32_t)wi * frfi[2 * j + 1];
                    ti32 = (int32_t)wr * frfi[2 * j + 1] +
                           (int32_t)wi * frfi[2 * j];
                    frfi[2 * j] = (int16_t)((qr32 - (tr32 >> 15)) >> shift);
                    frfi[2 * j + 1] = (int16_t)((qi32 - (ti32 >> 15)) >> shift);
                    frfi[2 * i] = (int16_t)((qr32 + (tr32 >> 15)) >> shift);
                    frfi[2 * i + 1] = (int16_t)((qi32 + (ti32 >> 15)) >> shift);
                }
            }
        } else {
            for (m = 0; m < l; ++m) {
                j = m << k;
                wr = TSpl_kSinTable1024[j + 256];
                wi = TSpl_kSinTable1024[j];
                for (i = m; i < n; i += istep) {
                    j = i + l;
                    tr32 = (int32_t)wr * frfi[2 * j] -
                           (int32_t)wi * frfi[2 * j + 1];
                    ti32 = (int32_t)wr * frfi[2 * j + 1] +
                           (int32_t)wi * frfi[2 * j];
                    tr32 = (tr32 + 1) >> 1;
                    ti32 = (ti32 + 1) >> 1;
                    qr32 = (int32_t)frfi[2 * i] << 14;
                    qi32 = (int32_t)frfi[2 * i + 1] << 14;
                    frfi[2 * j] = (int16_t)((qr32 - tr32 + round2) >>
                                             (shift + 14));
                    frfi[2 * j + 1] = (int16_t)((qi32 - ti32 + round2) >>
                                                 (shift + 14));
                    frfi[2 * i] = (int16_t)((qr32 + tr32 + round2) >>
                                             (shift + 14));
                    frfi[2 * i + 1] = (int16_t)((qi32 + ti32 + round2) >>
                                                 (shift + 14));
                }
            }
        }
        --k;
        l = istep;
    }
    return scale;
}
