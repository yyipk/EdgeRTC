#include "dsp_speex_fft_320.h"

#if DSP_SPEEX_FFT_320_AVAILABLE

#include "dsp_kernels.h"

#pragma GCC optimize("O3")

#define COMPLEX_SAMPLES 160

static const int16_t fft_twiddles[COMPLEX_SAMPLES * 2] = {
    32767, 0, 32743, -1287, 32667, -2570, 32541, -3851,
    32364, -5125, 32138, -6393, 31863, -7650, 31539, -8895,
    31165, -10126, 30743, -11340, 30274, -12540, 29758, -13718,
    29197, -14875, 28590, -16010, 27940, -17119, 27246, -18205,
    26510, -19260, 25734, -20286, 24918, -21281, 24063, -22242,
    23171, -23171, 22244, -24063, 21282, -24917, 20288, -25733,
    19261, -26509, 18205, -27246, 17122, -27940, 16012, -28590,
    14878, -29197, 13720, -29757, 12540, -30274, 11342, -30743,
    10127, -31164, 8895, -31537, 7650, -31862, 6393, -32138,
    5127, -32364, 3852, -32541, 2572, -32667, 1287, -32742,
    0, -32767, -1287, -32743, -2570, -32667, -3851, -32541,
    -5125, -32364, -6393, -32138, -7650, -31863, -8895, -31539,
    -10126, -31165, -11340, -30743, -12540, -30274, -13718, -29758,
    -14875, -29197, -16010, -28590, -17119, -27940, -18205, -27246,
    -19260, -26510, -20286, -25734, -21281, -24918, -22242, -24063,
    -23171, -23171, -24063, -22244, -24917, -21282, -25733, -20288,
    -26509, -19261, -27246, -18205, -27940, -17122, -28590, -16012,
    -29197, -14878, -29757, -13720, -30274, -12540, -30743, -11342,
    -31164, -10127, -31537, -8895, -31862, -7650, -32138, -6393,
    -32364, -5127, -32541, -3852, -32667, -2572, -32742, -1287,
    -32767, 0, -32743, 1287, -32667, 2570, -32541, 3851,
    -32364, 5125, -32138, 6393, -31863, 7650, -31539, 8895,
    -31165, 10126, -30743, 11340, -30274, 12540, -29758, 13718,
    -29197, 14875, -28590, 16010, -27940, 17119, -27246, 18205,
    -26510, 19260, -25734, 20286, -24918, 21281, -24063, 22242,
    -23171, 23171, -22244, 24063, -21282, 24917, -20288, 25733,
    -19261, 26509, -18205, 27246, -17122, 27940, -16012, 28590,
    -14878, 29197, -13720, 29757, -12540, 30274, -11342, 30743,
    -10127, 31164, -8895, 31537, -7650, 31862, -6393, 32138,
    -5127, 32364, -3852, 32541, -2572, 32667, -1287, 32742,
    0, 32767, 1287, 32743, 2570, 32667, 3851, 32541,
    5125, 32364, 6393, 32138, 7650, 31863, 8895, 31539,
    10126, 31165, 11340, 30743, 12540, 30274, 13718, 29758,
    14875, 29197, 16010, 28590, 17119, 27940, 18205, 27246,
    19260, 26510, 20286, 25734, 21281, 24918, 22242, 24063,
    23171, 23171, 24063, 22244, 24917, 21282, 25733, 20288,
    26509, 19261, 27246, 18205, 27940, 17122, 28590, 16012,
    29197, 14878, 29757, 13720, 30274, 12540, 30743, 11342,
    31164, 10127, 31537, 8895, 31862, 7650, 32138, 6393,
    32364, 5127, 32541, 3852, 32667, 2572, 32742, 1287,
};

static const int16_t real_twiddles[162] = {
    0, -32767, -643, -32762, -1287, -32743, -1928, -32711,
    -2570, -32667, -3212, -32610, -3851, -32541, -4490, -32459,
    -5125, -32364, -5760, -32257, -6393, -32138, -7022, -32007,
    -7650, -31863, -8273, -31707, -8895, -31539, -9512, -31357,
    -10126, -31165, -10736, -30959, -11340, -30743, -11943, -30515,
    -12540, -30274, -13130, -30022, -13718, -29758, -14299, -29484,
    -14875, -29197, -15447, -28899, -16010, -28590, -16569, -28271,
    -17119, -27940, -17666, -27598, -18205, -27246, -18735, -26884,
    -19260, -26510, -19777, -26128, -20286, -25734, -20788, -25330,
    -21281, -24918, -21766, -24495, -22242, -24063, -22711, -23622,
    -23171, -23171, -23621, -22712, -24063, -22244, -24494, -21767,
    -24917, -21282, -25330, -20788, -25733, -20288, -26127, -19777,
    -26509, -19261, -26883, -18737, -27246, -18205, -27598, -17668,
    -27940, -17122, -28270, -16571, -28590, -16012, -28899, -15447,
    -29197, -14878, -29483, -14300, -29757, -13720, -30022, -13133,
    -30274, -12540, -30514, -11944, -30743, -11342, -30959, -10737,
    -31164, -10127, -31357, -9512, -31537, -8895, -31707, -8274,
    -31862, -7650, -32006, -7023, -32138, -6393, -32257, -5762,
    -32364, -5127, -32459, -4492, -32541, -3852, -32610, -3212,
    -32667, -2572, -32711, -1929, -32742, -1287, -32762, -644,
    -32767, 0,
};

static const uint16_t fft_shuffle[COMPLEX_SAMPLES] = {
    0, 32, 64, 96, 128, 16, 48, 80, 112, 144, 4, 36, 68, 100, 132, 20,
    52, 84, 116, 148, 8, 40, 72, 104, 136, 24, 56, 88, 120, 152, 12, 44,
    76, 108, 140, 28, 60, 92, 124, 156, 1, 33, 65, 97, 129, 17, 49, 81,
    113, 145, 5, 37, 69, 101, 133, 21, 53, 85, 117, 149, 9, 41, 73, 105,
    137, 25, 57, 89, 121, 153, 13, 45, 77, 109, 141, 29, 61, 93, 125, 157,
    2, 34, 66, 98, 130, 18, 50, 82, 114, 146, 6, 38, 70, 102, 134, 22,
    54, 86, 118, 150, 10, 42, 74, 106, 138, 26, 58, 90, 122, 154, 14, 46,
    78, 110, 142, 30, 62, 94, 126, 158, 3, 35, 67, 99, 131, 19, 51, 83,
    115, 147, 7, 39, 71, 103, 135, 23, 55, 87, 119, 151, 11, 43, 75, 107,
    139, 27, 59, 91, 123, 155, 15, 47, 79, 111, 143, 31, 63, 95, 127, 159,
};

static dsp_i16x2_t load_complex(const int16_t *data, int index)
{
    return dsp_i16x2_load(data + 2 * index);
}

static void store_complex(int16_t *data, int index, dsp_i16x2_t value)
{
    dsp_i16x2_store(data + 2 * index, value);
}

static dsp_i16x2_t load_twiddle(const int16_t *table, int index,
                                int inverse)
{
    dsp_i16x2_t value = dsp_i16x2_load(table + 2 * index);

    if (!inverse)
        return value;
    return dsp_i16x2_pack((int16_t)value,
                          (int16_t)-(int16_t)(value >> 16));
}

static void shuffle_input(int16_t *output, const int16_t *input)
{
    int index;

    for (index = 0; index < COMPLEX_SAMPLES; index++)
        store_complex(output, index,
                      load_complex(input, fft_shuffle[index]));
}

static inline __attribute__((always_inline))
void butterfly5(int16_t *data, int inverse)
{
    dsp_i16x2_t ya = load_twiddle(fft_twiddles, 32, inverse);
    dsp_i16x2_t yb = load_twiddle(fft_twiddles, 64, inverse);
    int block;

    for (block = 0; block < 32; block++) {
        int base = block * 5;
        dsp_i16x2_t f0 = load_complex(data, base);
        dsp_i16x2_t f1 = load_complex(data, base + 1);
        dsp_i16x2_t f2 = load_complex(data, base + 2);
        dsp_i16x2_t f3 = load_complex(data, base + 3);
        dsp_i16x2_t f4 = load_complex(data, base + 4);
        dsp_i16x2_t s5, s6, s7, s8, s9, s10, s11, s12;

        if (!inverse) {
            f0 = dsp_i16x2_scale_q15(f0, 32767 / 5);
            f1 = dsp_i16x2_scale_q15(f1, 32767 / 5);
            f2 = dsp_i16x2_scale_q15(f2, 32767 / 5);
            f3 = dsp_i16x2_scale_q15(f3, 32767 / 5);
            f4 = dsp_i16x2_scale_q15(f4, 32767 / 5);
            f1 = dsp_i16x2_scale_q15(f1, 32767);
            f2 = dsp_i16x2_scale_q15(f2, 32767);
            f3 = dsp_i16x2_scale_q15(f3, 32767);
            f4 = dsp_i16x2_scale_q15(f4, 32767);
        }
        s7 = dsp_i16x2_add(f1, f4);
        s10 = dsp_i16x2_sub(f1, f4);
        s8 = dsp_i16x2_add(f2, f3);
        s9 = dsp_i16x2_sub(f2, f3);
        store_complex(data, base,
                      dsp_i16x2_add(f0, dsp_i16x2_add(s7, s8)));

        s5 = dsp_i16x2_add(f0,
            dsp_i16x2_add(dsp_i16x2_scale_q15(s7, (int16_t)ya),
                          dsp_i16x2_scale_q15(s8, (int16_t)yb)));
        s6 = dsp_i16x2_crsa(0,
            dsp_i16x2_add(dsp_i16x2_scale_q15(s10,
                                              (int16_t)(ya >> 16)),
                          dsp_i16x2_scale_q15(s9,
                                              (int16_t)(yb >> 16))));
        store_complex(data, base + 1, dsp_i16x2_sub(s5, s6));
        store_complex(data, base + 4, dsp_i16x2_add(s5, s6));

        s11 = dsp_i16x2_add(f0,
            dsp_i16x2_add(dsp_i16x2_scale_q15(s7, (int16_t)yb),
                          dsp_i16x2_scale_q15(s8, (int16_t)ya)));
        s12 = dsp_i16x2_cras(0,
            dsp_i16x2_sub(dsp_i16x2_scale_q15(s10,
                                              (int16_t)(yb >> 16)),
                          dsp_i16x2_scale_q15(s9,
                                              (int16_t)(ya >> 16))));
        store_complex(data, base + 2, dsp_i16x2_add(s11, s12));
        store_complex(data, base + 3, dsp_i16x2_sub(s11, s12));
    }
}

static inline __attribute__((always_inline))
void butterfly2(int16_t *data, int inverse)
{
    int block, index;

    for (block = 0; block < 16; block++) {
        int base = block * 10;

        for (index = 0; index < 5; index++) {
            dsp_i16x2_t current = load_complex(data, base + index);
            dsp_i16x2_t other = load_complex(data, base + index + 5);
            dsp_i16x2_t twiddle = load_twiddle(fft_twiddles,
                                               index * 16, inverse);

            if (inverse) {
                dsp_i16x2_t product = dsp_i16x2_cmul_q15(other,
                                                         twiddle);

                store_complex(data, base + index + 5,
                              dsp_i16x2_sub(current, product));
                store_complex(data, base + index,
                              dsp_i16x2_add(current, product));
            } else {
                int32_t real, imag;
                int32_t current_real = (int16_t)current;
                int32_t current_imag = (int16_t)(current >> 16);

                dsp_i16x2_cmul(other, twiddle, &real, &imag);
                real >>= 1;
                imag >>= 1;
                store_complex(data, base + index + 5,
                    dsp_i16x2_pack(
                        ((current_real << 14) - real + 16384) >> 15,
                        ((current_imag << 14) - imag + 16384) >> 15));
                store_complex(data, base + index,
                    dsp_i16x2_pack(
                        ((current_real << 14) + real + 16384) >> 15,
                        ((current_imag << 14) + imag + 16384) >> 15));
            }
        }
    }
}

static inline __attribute__((always_inline))
void butterfly4_stage(int16_t *data, int fstride, int m,
                      int blocks, int block_stride, int inverse)
{
    int block, index;

    for (block = 0; block < blocks; block++) {
        int base = block * block_stride;

        for (index = 0; index < m; index++) {
            dsp_i16x2_t f0 = load_complex(data, base + index);
            dsp_i16x2_t f1 = load_complex(data, base + index + m);
            dsp_i16x2_t f2 = load_complex(data, base + index + 2 * m);
            dsp_i16x2_t f3 = load_complex(data, base + index + 3 * m);
            dsp_i16x2_t tw1 = load_twiddle(fft_twiddles,
                                           index * fstride, inverse);
            dsp_i16x2_t tw2 = load_twiddle(fft_twiddles,
                                           index * fstride * 2, inverse);
            dsp_i16x2_t tw3 = load_twiddle(fft_twiddles,
                                           index * fstride * 3, inverse);
            dsp_i16x2_t sum02, diff02, sum13, diff13;

            if (inverse) {
                f1 = dsp_i16x2_cmul_q15(f1, tw1);
                f2 = dsp_i16x2_cmul_q15(f2, tw2);
                f3 = dsp_i16x2_cmul_q15(f3, tw3);
            } else {
                f0 = dsp_i16x2_shift_right_round(f0, 2);
                f1 = dsp_i16x2_cmul_q13(f1, tw1);
                f2 = dsp_i16x2_cmul_q13(f2, tw2);
                f3 = dsp_i16x2_cmul_q13(f3, tw3);
            }
            sum02 = dsp_i16x2_add(f0, f2);
            diff02 = dsp_i16x2_sub(f0, f2);
            sum13 = dsp_i16x2_add(f1, f3);
            diff13 = dsp_i16x2_sub(f1, f3);
            store_complex(data, base + index,
                          dsp_i16x2_add(sum02, sum13));
            store_complex(data, base + index + 2 * m,
                          dsp_i16x2_sub(sum02, sum13));
            store_complex(data, base + index + m,
                          inverse ? dsp_i16x2_cras(diff02, diff13) :
                                    dsp_i16x2_crsa(diff02, diff13));
            store_complex(data, base + index + 3 * m,
                          inverse ? dsp_i16x2_crsa(diff02, diff13) :
                                    dsp_i16x2_cras(diff02, diff13));
        }
    }
}

static inline __attribute__((always_inline))
void complex_fft_160(int16_t *output, const int16_t *input, int inverse)
{
    shuffle_input(output, input);
    butterfly5(output, inverse);
    butterfly2(output, inverse);
    butterfly4_stage(output, 4, 10, 4, 40, inverse);
    butterfly4_stage(output, 1, 40, 1, 1, inverse);
}

void dsp_speex_fft_320_forward(dsp_speex_fft_320_t *context,
                               const int16_t *input,
                               int16_t *output)
{
    int16_t *workspace = context->workspace;
    int k;
    int32_t dc_real, dc_imag;

    complex_fft_160(workspace, input, 0);
    dc_real = ((int32_t)workspace[0] * 16383 + 16384) >> 15;
    dc_imag = ((int32_t)workspace[1] * 16383 + 16384) >> 15;
    output[0] = (int16_t)(dc_real + dc_imag);
    output[319] = (int16_t)(dc_real - dc_imag);

    for (k = 1; k <= 80; k++) {
        int16_t kr = workspace[2 * k];
        int16_t ki = workspace[2 * k + 1];
        int16_t nr = workspace[2 * (160 - k)];
        int16_t ni = workspace[2 * (160 - k) + 1];
        int32_t f2r = ((int32_t)kr - nr) >> 1;
        int32_t f2i = ((int32_t)ki + ni + 1) >> 1;
        int32_t f1r = ((int32_t)kr + nr) << 13;
        int32_t f1i = ((int32_t)ki - ni) << 13;
        int32_t twr, twi;

        dsp_i16x2_cmul(dsp_i16x2_pack(f2r, f2i),
                       load_twiddle(real_twiddles, k, 0), &twr, &twi);
        twr >>= 1;
        twi >>= 1;
        output[2 * k - 1] = (int16_t)((f1r + twr + 16384) >> 15);
        output[2 * k] = (int16_t)((f1i + twi + 16384) >> 15);
        output[2 * (160 - k) - 1] =
            (int16_t)((f1r - twr + 16384) >> 15);
        output[2 * (160 - k)] =
            (int16_t)((twi - f1i + 16384) >> 15);
    }
}

void dsp_speex_fft_320_inverse(dsp_speex_fft_320_t *context,
                               const int16_t *input,
                               int16_t *output)
{
    int16_t *workspace = context->workspace;
    int k;

    workspace[0] = (int16_t)(input[0] + input[319]);
    workspace[1] = (int16_t)(input[0] - input[319]);
    for (k = 1; k <= 80; k++) {
        dsp_i16x2_t fk = dsp_i16x2_pack(input[2 * k - 1],
                                        input[2 * k]);
        dsp_i16x2_t fnkc = dsp_i16x2_pack(
            input[2 * (160 - k) - 1],
            (int16_t)-input[2 * (160 - k)]);
        dsp_i16x2_t even = dsp_i16x2_add(fk, fnkc);
        dsp_i16x2_t odd = dsp_i16x2_cmul_q15(
            dsp_i16x2_sub(fk, fnkc),
            load_twiddle(real_twiddles, k, 1));
        dsp_i16x2_t upper = dsp_i16x2_sub(even, odd);

        store_complex(workspace, k, dsp_i16x2_add(even, odd));
        upper = dsp_i16x2_pack((int16_t)upper,
                               (int16_t)-(int16_t)(upper >> 16));
        store_complex(workspace, 160 - k, upper);
    }
    complex_fft_160(output, workspace, 1);
}

#endif
