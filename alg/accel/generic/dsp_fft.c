#include "dsp_fft.h"

#include <limits.h>

static int16_t max_abs_i16(const int16_t *data, int length)
{
    int32_t maximum = 0;
    int i;

    for (i = 0; i < length; i++) {
        int32_t value = data[i];

        if (value < 0)
            value = -value;
        if (value > maximum)
            maximum = value;
    }
    return maximum < INT16_MAX ? (int16_t)maximum : INT16_MAX;
}

static void fft_mode0(int16_t *data, int stages, const int16_t *sine_table)
{
    int i, j, l, k, step, n, m;
    int16_t wr, wi;
    int32_t tr, ti, qr, qi;

    n = 1 << stages;
    l = 1;
    k = 9;
    while (l < n) {
        step = l << 1;
        for (m = 0; m < l; m++) {
            j = m << k;
            wr = sine_table[j + 256];
            wi = (int16_t)-sine_table[j];
            for (i = m; i < n; i += step) {
                j = i + l;
                tr = (int32_t)wr * data[2 * j] -
                     (int32_t)wi * data[2 * j + 1];
                ti = (int32_t)wr * data[2 * j + 1] +
                     (int32_t)wi * data[2 * j];
                tr >>= 15;
                ti >>= 15;
                qr = data[2 * i];
                qi = data[2 * i + 1];
                data[2 * j] = (int16_t)((qr - tr) >> 1);
                data[2 * j + 1] = (int16_t)((qi - ti) >> 1);
                data[2 * i] = (int16_t)((qr + tr) >> 1);
                data[2 * i + 1] = (int16_t)((qi + ti) >> 1);
            }
        }
        k--;
        l = step;
    }
}

static void fft_mode1(int16_t *data, int stages, const int16_t *sine_table)
{
    int i, j, l, k, step, n, m;
    int16_t wr, wi;
    int32_t tr, ti, qr, qi;

    n = 1 << stages;
    l = 1;
    k = 9;
    while (l < n) {
        step = l << 1;
        for (m = 0; m < l; m++) {
            j = m << k;
            wr = sine_table[j + 256];
            wi = sine_table[j];
            for (i = m; i < n; i += step) {
                j = i + l;
                tr = (int32_t)wr * data[2 * j] -
                     (int32_t)wi * data[2 * j + 1];
                ti = (int32_t)wr * data[2 * j + 1] +
                     (int32_t)wi * data[2 * j];
                tr = (tr + 1) >> 1;
                ti = (ti + 1) >> 1;
                qr = (int32_t)data[2 * i] << 14;
                qi = (int32_t)data[2 * i + 1] << 14;
                data[2 * j] = (int16_t)((qr - tr + 16384) >> 15);
                data[2 * j + 1] = (int16_t)((qi - ti + 16384) >> 15);
                data[2 * i] = (int16_t)((qr + tr + 16384) >> 15);
                data[2 * i + 1] = (int16_t)((qi + ti + 16384) >> 15);
            }
        }
        k--;
        l = step;
    }
}

int dsp_fft_q15(int16_t *data, int stages, int mode,
                const int16_t *sine_table)
{
    if ((1 << stages) > 1024)
        return -1;
    if (mode == 0)
        fft_mode0(data, stages, sine_table);
    else
        fft_mode1(data, stages, sine_table);
    return 0;
}

static int ifft_shift(const int16_t *data, int n, int *rounding)
{
    int maximum = max_abs_i16(data, 2 * n);
    int shift = (maximum > 13573) + (maximum > 27146);

    *rounding = 8192 << shift;
    return shift;
}

static int ifft_mode0(int16_t *data, int stages, const int16_t *sine_table)
{
    int i, j, l, k, step, n, m, scale;
    int16_t wr, wi;
    int32_t tr, ti, qr, qi;

    n = 1 << stages;
    scale = 0;
    l = 1;
    k = 9;
    while (l < n) {
        int shift;
        int rounding;

        shift = ifft_shift(data, n, &rounding);
        (void)rounding;
        scale += shift;
        step = l << 1;
        for (m = 0; m < l; m++) {
            j = m << k;
            wr = sine_table[j + 256];
            wi = sine_table[j];
            for (i = m; i < n; i += step) {
                j = i + l;
                tr = (int32_t)wr * data[2 * j] -
                     (int32_t)wi * data[2 * j + 1];
                ti = (int32_t)wr * data[2 * j + 1] +
                     (int32_t)wi * data[2 * j];
                qr = data[2 * i];
                qi = data[2 * i + 1];
                data[2 * j] = (int16_t)((qr - (tr >> 15)) >> shift);
                data[2 * j + 1] = (int16_t)((qi - (ti >> 15)) >> shift);
                data[2 * i] = (int16_t)((qr + (tr >> 15)) >> shift);
                data[2 * i + 1] = (int16_t)((qi + (ti >> 15)) >> shift);
            }
        }
        k--;
        l = step;
    }
    return scale;
}

static int ifft_mode1(int16_t *data, int stages, const int16_t *sine_table)
{
    int i, j, l, k, step, n, m, scale;
    int16_t wr, wi;
    int32_t tr, ti, qr, qi;

    n = 1 << stages;
    scale = 0;
    l = 1;
    k = 9;
    while (l < n) {
        int shift;
        int rounding;

        shift = ifft_shift(data, n, &rounding);
        scale += shift;
        step = l << 1;
        for (m = 0; m < l; m++) {
            j = m << k;
            wr = sine_table[j + 256];
            wi = sine_table[j];
            for (i = m; i < n; i += step) {
                j = i + l;
                tr = (int32_t)wr * data[2 * j] -
                     (int32_t)wi * data[2 * j + 1];
                ti = (int32_t)wr * data[2 * j + 1] +
                     (int32_t)wi * data[2 * j];
                tr = (tr + 1) >> 1;
                ti = (ti + 1) >> 1;
                qr = (int32_t)data[2 * i] << 14;
                qi = (int32_t)data[2 * i + 1] << 14;
                data[2 * j] = (int16_t)((qr - tr + rounding) >>
                                         (shift + 14));
                data[2 * j + 1] = (int16_t)((qi - ti + rounding) >>
                                             (shift + 14));
                data[2 * i] = (int16_t)((qr + tr + rounding) >>
                                         (shift + 14));
                data[2 * i + 1] = (int16_t)((qi + ti + rounding) >>
                                             (shift + 14));
            }
        }
        k--;
        l = step;
    }
    return scale;
}

int dsp_ifft_q15(int16_t *data, int stages, int mode,
                 const int16_t *sine_table)
{
    if ((1 << stages) > 1024)
        return -1;
    return mode == 0 ? ifft_mode0(data, stages, sine_table) :
                       ifft_mode1(data, stages, sine_table);
}
