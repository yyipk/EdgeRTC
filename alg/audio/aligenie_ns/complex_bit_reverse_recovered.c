/* Recovered from libmit_rtos.a complex_bit_reverse.o. */
#include "ns_c_recovered.h"

void TSpl_ComplexBitReverse(int16_t *frfi, int stages)
{
    int n = 1 << stages;
    int nn = n - 1;
    int mr = 0;
    int m;

    for (m = 1; m <= nn; ++m) {
        int l = n;

        do {
            l >>= 1;
        } while (mr + l > nn);
        mr = (mr & (l - 1)) + l;
        if (m < mr) {
            int16_t value = frfi[2 * m];

            frfi[2 * m] = frfi[2 * mr];
            frfi[2 * mr] = value;
            value = frfi[2 * m + 1];
            frfi[2 * m + 1] = frfi[2 * mr + 1];
            frfi[2 * mr + 1] = value;
        }
    }
}
