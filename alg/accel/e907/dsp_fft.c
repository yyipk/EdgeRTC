#include "dsp_fft.h"

#include <limits.h>
#include <riscv-dsp.h>

static inline uint32_t load_i16x2(const int16_t *data)
{
    uint32_t value;

    data = __builtin_assume_aligned(data, sizeof(value));
    __builtin_memcpy(&value, data, sizeof(value));
    return value;
}

static inline void store_i16x2(int16_t *data, uint32_t value)
{
    data = __builtin_assume_aligned(data, sizeof(value));
    __builtin_memcpy(data, &value, sizeof(value));
}

static inline uint32_t pack_i16x2(int32_t low, int32_t high)
{
    uint32_t value;

    __asm__ volatile("pkbb16 %0, %1, %2"
                     : "=r"(value)
                     : "r"(high), "r"(low));
    return value;
}

static inline int32_t complex_real(uint32_t left, uint32_t right)
{
    return __rv__smdrs(left, right);
}

static inline int32_t complex_imag(uint32_t left, uint32_t right)
{
    return __rv__kmxda(left, right);
}

static int16_t max_abs_i16(const int16_t *data, int length)
{
    int32_t maximum = 0;
    int i;

    for (i = 0; i + 1 < length; i += 2) {
        uint32_t absolute = __rv__kabs16(load_i16x2(data + i));
        uint32_t pair_maximum = __rv__smax16(absolute, absolute >> 16);
        int32_t value = (int16_t)pair_maximum;

        if (value > maximum)
            maximum = value;
    }
    if (i < length) {
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
    int l, k, step, n, m;

    n = 1 << stages;
    l = 1;
    k = 9;
    while (l < n) {
        step = l << 1;
        for (m = 0; m < l; m++) {
            int table_index = m << k;
            uint32_t twiddle = pack_i16x2(sine_table[table_index + 256],
                                           -sine_table[table_index]);
            int i;

            for (i = m; i < n; i += step) {
                int j = i + l;
                uint32_t input = load_i16x2(&data[2 * j]);
                uint32_t current = load_i16x2(&data[2 * i]);
                int32_t real = complex_real(twiddle, input);
                int32_t imag = complex_imag(twiddle, input);
                uint32_t product = pack_i16x2(real >> 15, imag >> 15);
                uint32_t lower = __rv__rsub16(current, product);
                uint32_t upper = __rv__radd16(current, product);

                store_i16x2(&data[2 * j], lower);
                store_i16x2(&data[2 * i], upper);
            }
        }
        k--;
        l = step;
    }
}

static void fft_mode1(int16_t *data, int stages, const int16_t *sine_table)
{
    int l, k, step, n, m;

    n = 1 << stages;
    l = 1;
    k = 9;
    while (l < n) {
        step = l << 1;
        for (m = 0; m < l; m++) {
            int table_index = m << k;
            uint32_t twiddle = pack_i16x2(sine_table[table_index + 256],
                                           sine_table[table_index]);
            int i;

            for (i = m; i < n; i += step) {
                int j = i + l;
                uint32_t input = load_i16x2(&data[2 * j]);
                uint32_t current = load_i16x2(&data[2 * i]);
                int32_t real = complex_real(twiddle, input);
                int32_t imag = complex_imag(twiddle, input);
                uint64_t current_scaled = __rv__smul16(current, 0x40004000);
                int32_t current_real = (int32_t)current_scaled;
                int32_t current_imag = (int32_t)(current_scaled >> 32);
                real = (real + 1) >> 1;
                imag = (imag + 1) >> 1;
                store_i16x2(&data[2 * j], pack_i16x2(
                    (current_real - real + 16384) >> 15,
                    (current_imag - imag + 16384) >> 15));
                store_i16x2(&data[2 * i], pack_i16x2(
                    (current_real + real + 16384) >> 15,
                    (current_imag + imag + 16384) >> 15));
            }
        }
        k--;
        l = step;
    }
}

static int ifft_shift(const int16_t *data, int n)
{
    int maximum = max_abs_i16(data, 2 * n);

    return (maximum > 13573) + (maximum > 27146);
}

static int ifft_mode0(int16_t *data, int stages, const int16_t *sine_table)
{
    int l, k, step, n, m, scale;

    n = 1 << stages;
    scale = 0;
    l = 1;
    k = 9;
    while (l < n) {
        int shift = ifft_shift(data, n);

        scale += shift;
        step = l << 1;
        for (m = 0; m < l; m++) {
            int table_index = m << k;
            uint32_t twiddle = pack_i16x2(sine_table[table_index + 256],
                                           sine_table[table_index]);
            int i;

            for (i = m; i < n; i += step) {
                int j = i + l;
                uint32_t input = load_i16x2(&data[2 * j]);
                uint32_t current = load_i16x2(&data[2 * i]);
                int32_t real = complex_real(twiddle, input);
                int32_t imag = complex_imag(twiddle, input);
                uint32_t product = pack_i16x2(real >> 15, imag >> 15);

                store_i16x2(&data[2 * j],
                            __rv__sra16(__rv__sub16(current, product), shift));
                store_i16x2(&data[2 * i],
                            __rv__sra16(__rv__add16(current, product), shift));
            }
        }
        k--;
        l = step;
    }
    return scale;
}

static int ifft_mode1(int16_t *data, int stages, const int16_t *sine_table)
{
    int l, k, step, n, m, scale;

    n = 1 << stages;
    scale = 0;
    l = 1;
    k = 9;
    while (l < n) {
        int shift = ifft_shift(data, n);
        int rounding = 8192 << shift;

        scale += shift;
        step = l << 1;
        for (m = 0; m < l; m++) {
            int table_index = m << k;
            uint32_t twiddle = pack_i16x2(sine_table[table_index + 256],
                                           sine_table[table_index]);
            int i;

            for (i = m; i < n; i += step) {
                int j = i + l;
                uint32_t input = load_i16x2(&data[2 * j]);
                uint32_t current = load_i16x2(&data[2 * i]);
                int32_t real = complex_real(twiddle, input);
                int32_t imag = complex_imag(twiddle, input);
                uint64_t current_scaled = __rv__smul16(current, 0x40004000);
                int32_t current_real = (int32_t)current_scaled;
                int32_t current_imag = (int32_t)(current_scaled >> 32);

                real = (real + 1) >> 1;
                imag = (imag + 1) >> 1;
                store_i16x2(&data[2 * j], pack_i16x2(
                    (current_real - real + rounding) >>
                        (shift + 14),
                    (current_imag - imag + rounding) >>
                        (shift + 14)));
                store_i16x2(&data[2 * i], pack_i16x2(
                    (current_real + real + rounding) >>
                        (shift + 14),
                    (current_imag + imag + rounding) >>
                        (shift + 14)));
            }
        }
        k--;
        l = step;
    }
    return scale;
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

int dsp_ifft_q15(int16_t *data, int stages, int mode,
                 const int16_t *sine_table)
{
    if ((1 << stages) > 1024)
        return -1;
    return mode == 0 ? ifft_mode0(data, stages, sine_table) :
                       ifft_mode1(data, stages, sine_table);
}
