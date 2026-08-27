/*
 * C recovery of nsx_core.o from libmit_rtos.a.
 *
 * This file deliberately contains source-level fixed-point operations only.
 * It does not include assembly, binary data, or compiler directives that
 * inject instructions.
 */
#include "ns_c_recovered.h"

#include <limits.h>
#include <string.h>

extern int16_t TSpl_MaxValueW16(const int16_t *vector, int16_t length);
extern int TSpl_NormU32(uint32_t value);
extern int TSpl_NormW16(int16_t value);
extern int TSpl_NormW32(int32_t value);
extern int16_t TSpl_DivW32W16ResW16(int32_t numerator, int16_t denominator);
extern int32_t TSpl_DivW32W16(int32_t numerator, int16_t denominator);
extern uint32_t TSpl_DivU32U16(uint32_t numerator, uint16_t denominator);
extern int16_t TSpl_MaxAbsValueW16(const int16_t *vector, int16_t length);
extern int32_t TSpl_Energy(int16_t *vector, int length, int *scale_factor);
extern int TSpl_ComplexFFT(int16_t *data, int stages, int mode);
extern int TSpl_ComplexIFFT(int16_t *data, int stages, int mode);
extern void TSpl_ComplexBitReverse(int16_t *data, int stages);
extern int32_t TSpl_Sqrt(int32_t value);
extern int16_t TSpl_AddSatW16(int16_t left, int16_t right);
extern void De_Late_reverb_x(int16_t *magn, TDereverb_x_Param *params,
                             int16_t q_magn);

/* Preserve the original translation unit's constant-pool declaration order. */
static const int16_t kRoundTable[16];
const int16_t TNRx_kLogTableFrac[256];
static const int16_t kIndicatorTable[17];
static const int16_t kBlocks80w128x[128];
static const int16_t kBlocks160w256x[256];
static const int16_t kFactor2Aggressiveness1[64];
static const int16_t kFactor2Aggressiveness2[64];
static const int16_t kFactor2Aggressiveness3[64];
static const int16_t kLogIndex[129];
static const int16_t kFactor1Table[193];

/* Hybrid Hanning and flat analysis windows, Q14. */
static const int16_t kBlocks80w128x[128] = {
    0, 536, 1072, 1606, 2139, 2669, 3196, 3720, 4240, 4756, 5266,
    5771, 6270, 6762, 7246, 7723, 8192, 8652, 9102, 9543, 9974,
    10394, 10803, 11200, 11585, 11958, 12318, 12665, 12998, 13318,
    13623, 13913, 14189, 14449, 14694, 14924, 15137, 15334, 15515,
    15679, 15826, 15956, 16069, 16165, 16244, 16305, 16349, 16375,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16375, 16349, 16305, 16244,
    16165, 16069, 15956, 15826, 15679, 15515, 15334, 15137, 14924,
    14694, 14449, 14189, 13913, 13623, 13318, 12998, 12665, 12318,
    11958, 11585, 11200, 10803, 10394, 9974, 9543, 9102, 8652, 8192,
    7723,
    7246, 6762, 6270, 5771, 5266, 4756, 4240, 3720, 3196, 2669,
    2139, 1606, 1072, 536
};

static const int16_t kRoundTable[16] = {
    0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
    16384
};

static const int16_t kBlocks160w256x[256] = {
    0, 268, 536, 804, 1072, 1339, 1606, 1872, 2139, 2404, 2669, 2933,
    3196, 3459, 3720, 3981, 4240, 4499, 4756, 5012, 5266, 5520, 5771,
    6021, 6270, 6517, 6762, 7005, 7246, 7486, 7723, 7959, 8192, 8423,
    8652, 8878, 9102, 9324, 9543, 9760, 9974, 10185, 10394, 10600,
    10803, 11003, 11200, 11394, 11585, 11773, 11958, 12140, 12318,
    12493, 12665, 12833, 12998, 13160, 13318, 13472, 13623, 13770,
    13913, 14053, 14189, 14321, 14449, 14574, 14694, 14811, 14924,
    15032, 15137, 15237, 15334, 15426, 15515, 15599, 15679, 15754,
    15826, 15893, 15956, 16015, 16069, 16119, 16165, 16207, 16244,
    16277, 16305, 16329, 16349, 16364, 16375, 16382, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16382, 16375, 16364, 16349, 16329,
    16305, 16277, 16244, 16207, 16165, 16119, 16069, 16015, 15956,
    15893, 15826, 15754, 15679, 15599, 15515, 15426, 15334, 15237,
    15137, 15032, 14924, 14811, 14694, 14574, 14449, 14321, 14189,
    14053, 13913, 13770, 13623, 13472, 13318, 13160, 12998, 12833,
    12665, 12493, 12318, 12140, 11958, 11773, 11585, 11394, 11200,
    11003, 10803, 10600, 10394, 10185, 9974, 9760, 9543, 9324, 9102,
    8878, 8652,
    8423, 8192, 7959, 7723, 7486, 7246, 7005, 6762, 6517, 6270, 6021,
    5771, 5520, 5266, 5012, 4756, 4499, 4240, 3981, 3720, 3459, 3196,
    2933, 2669, 2404, 2139, 1872, 1606, 1339, 1072, 804, 536, 268
};

static const int16_t kLogIndex[129] = {
    0, 0, 4096, 6492, 8192, 9511, 10588, 11499, 12288, 12984, 13607,
    14170, 14684, 15157, 15595, 16003, 16384, 16742, 17080, 17400,
    17703, 17991, 18266, 18529, 18780, 19021, 19253, 19476, 19691,
    19898, 20099, 20292, 20480, 20662, 20838, 21010, 21176, 21338,
    21496, 21649, 21799, 21945, 22087, 22226, 22362, 22495, 22625,
    22752, 22876, 22998, 23117, 23234, 23349, 23462, 23572, 23680,
    23787, 23892, 23994, 24095, 24195, 24292, 24388, 24483, 24576,
    24668, 24758, 24847, 24934, 25021, 25106, 25189, 25272, 25354,
    25434, 25513, 25592, 25669, 25745, 25820, 25895, 25968, 26041,
    26112, 26183, 26253, 26322, 26390, 26458, 26525, 26591, 26656,
    26721, 26784, 26848, 26910, 26972, 27033, 27094, 27154, 27213,
    27272, 27330, 27388, 27445, 27502, 27558, 27613, 27668, 27722,
    27776, 27830, 27883, 27935, 27988, 28039, 28090, 28141, 28191,
    28241, 28291, 28340, 28388, 28437, 28484, 28532, 28579, 28626,
    28672
};

/* log2(1 + x), x in [0, 1), Q8. */
const int16_t TNRx_kLogTableFrac[256] = {
    0, 1, 3, 4, 6, 7, 9, 10, 11, 13, 14, 16, 17, 18, 20, 21,
    22, 24, 25, 26, 28, 29, 30, 32, 33, 34, 36, 37, 38, 40, 41, 42,
    44, 45, 46, 47, 49, 50, 51, 52, 54, 55, 56, 57, 59, 60, 61, 62,
    63, 65, 66, 67, 68, 69, 71, 72, 73, 74, 75, 77, 78, 79, 80, 81,
    82, 84, 85, 86, 87, 88, 89, 90, 92, 93, 94, 95, 96, 97, 98, 99,
    100, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113,
    114, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
    128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140,
    141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153,
    154, 155, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165,
    166, 167, 168, 169, 169, 170, 171, 172, 173, 174, 175, 176, 177,
    178, 178, 179, 180, 181, 182, 183, 184, 185, 185, 186, 187, 188,
    189, 190, 191, 192, 192, 193, 194, 195, 196, 197, 198, 198, 199,
    200, 201, 202, 203, 203, 204, 205, 206, 207, 208, 208, 209, 210,
    211, 212, 212, 213, 214, 215, 216, 216, 217, 218, 219, 220, 220,
    221, 222, 223, 224, 224, 225, 226, 227, 228, 228, 229, 230, 231,
    231, 232, 233, 234, 234, 235, 236, 237, 238, 238, 239, 240, 241,
    241, 242, 243, 244, 244, 245, 246, 247, 247, 248, 249, 249, 250,
    251, 252, 252, 253, 254, 255, 255
};

static const int16_t kIndicatorTable[17] = {
    0, 2017, 3809, 5227, 6258, 6963, 7424, 7718, 7901, 8014, 8084,
    8126, 8152, 8168, 8177, 8183, 8187
};

static const int16_t kFactor1Table[193] = {
    8233, 8274, 8315, 8355, 8396, 8436, 8475, 8515,
    8554, 8592, 8631, 8669, 8707, 8745, 8783, 8820,
    8857, 8894, 8931, 8967, 9003, 9039, 9075, 9111,
    9146, 9181, 9216, 9251, 9286, 9320, 9354, 9388,
    9422, 9456, 9489, 9523, 9556, 9589, 9622, 9655,
    9687, 9719, 9752, 9784, 9816, 9848, 9879, 9911,
    9942, 9973, 10004, 10035, 10066, 10097, 10128, 10158,
    10188, 10218, 10249, 10279, 10308, 10338, 10368, 10397,
    10426, 10456, 10485, 10514, 10543, 10572, 10600, 10629,
    10657, 10686, 10714, 10742, 10770, 10798, 10826, 10854,
    10882, 10847, 10810, 10774, 10737, 10701, 10666, 10631,
    10596, 10562, 10527, 10494, 10460, 10427, 10394, 10362,
    10329, 10297, 10266, 10235, 10203, 10173, 10142, 10112,
    10082, 10052, 10023, 9994, 9965, 9936, 9908, 9879,
    9851, 9824, 9796, 9769, 9742, 9715, 9689, 9662,
    9636, 9610, 9584, 9559, 9534, 9508, 9484, 9459,
    9434, 9410, 9386, 9362, 9338, 9314, 9291, 9268,
    9245, 9222, 9199, 9176, 9154, 9132, 9110, 9088,
    9066, 9044, 9023, 9002, 8980, 8959, 8939, 8918,
    8897, 8877, 8857, 8836, 8816, 8796, 8777, 8757,
    8738, 8718, 8699, 8680, 8661, 8642, 8623, 8605,
    8586, 8568, 8550, 8532, 8514, 8496, 8478, 8460,
    8443, 8425, 8408, 8391, 8373, 8356, 8339, 8323,
    8306, 8289, 8273, 8256, 8240, 8224, 8208, 8192,
    0
};

static const int16_t kFactor2Aggressiveness1[64] = {
    7577, 7577, 7577, 7577, 7577, 7577, 7577, 7577, 7577, 7577, 7577,
    7577, 7577, 7577, 7577, 7577, 7577, 7596, 7614, 7632, 7650, 7667,
    7683, 7699, 7715, 7731, 7746, 7761, 7775, 7790, 7804, 7818, 7832,
    7845, 7858, 7871, 7884, 7897, 7910, 7922, 7934, 7946, 7958, 7970,
    7982, 7993, 8004, 8016, 8027, 8038, 8049, 8060, 8070, 8081, 8091,
    8102, 8112, 8122, 8132, 8143, 8152, 8162, 8172, 8182
};

static const int16_t kFactor2Aggressiveness2[64] = {
    7270, 7270, 7270, 7270, 7270, 7306, 7339, 7369, 7397, 7424, 7448,
    7472, 7495, 7517, 7537, 7558, 7577, 7596, 7614, 7632, 7650, 7667,
    7683, 7699, 7715, 7731, 7746, 7761, 7775, 7790, 7804, 7818, 7832,
    7845, 7858, 7871, 7884, 7897, 7910, 7922, 7934, 7946, 7958, 7970,
    7982, 7993, 8004, 8016, 8027, 8038, 8049, 8060, 8070, 8081, 8091,
    8102, 8112, 8122, 8132, 8143, 8152, 8162, 8172, 8182
};

static const int16_t kFactor2Aggressiveness3[64] = {
    7184, 7184, 7184, 7229, 7270, 7306, 7339, 7369, 7397, 7424, 7448,
    7472, 7495, 7517, 7537, 7558, 7577, 7596, 7614, 7632, 7650, 7667,
    7683, 7699, 7715, 7731, 7746, 7761, 7775, 7790, 7804, 7818, 7832,
    7845, 7858, 7871, 7884, 7897, 7910, 7922, 7934, 7946, 7958, 7970,
    7982, 7993, 8004, 8016, 8027, 8038, 8049, 8060, 8070, 8081, 8091,
    8102, 8112, 8122, 8132, 8143, 8152, 8162, 8172, 8182
};

static int16_t sat_w32_to_w16(int32_t value)
{
    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}

static inline __attribute__((always_inline)) int32_t shift_w32(int32_t value,
                                                               int shift)
{
    return shift >= 0 ? value << shift : value >> -shift;
}

static inline __attribute__((always_inline)) uint32_t shift_u32(uint32_t value,
                                                                int shift)
{
    return shift >= 0 ? value << shift : value >> -shift;
}

static inline __attribute__((always_inline)) int16_t log2_q8(uint32_t value)
{
    int16_t zeros;

    if (value == 0)
        return 0;
    zeros = TSpl_NormU32(value);
    return (int16_t)(((31 - zeros) << 8) +
                     TNRx_kLogTableFrac[(value << zeros & 0x7fffffff) >> 23]);
}

void TNRx_UpdateNoiseEstimate(NsxHandle inst, int offset)
{
    int32_t tmp32no1 = 0;
    int32_t tmp32no2 = 0;
    int16_t tmp16no1 = 0;
    int16_t tmp16no2 = 0;
    const int16_t exp2_const = 11819;
    int i;

    tmp16no2 = TSpl_MaxValueW16(inst->noiseEstLogQuantile + offset,
                                (int16_t)inst->magnLen);
    inst->qNoise = 14 -
        (int)(((int32_t)exp2_const * tmp16no2 + (1 << 20)) >> 21);
    for (i = 0; i < inst->magnLen; i++) {
        tmp32no2 = exp2_const * inst->noiseEstLogQuantile[offset + i];
        tmp32no1 = 0x00200000 | (tmp32no2 & 0x001fffff);
        tmp16no1 = (int16_t)-(tmp32no2 >> 21);
        tmp16no1 += 21;
        tmp16no1 -= (int16_t)inst->qNoise;
        if (tmp16no1 > 0 && tmp16no1 < 16) {
            inst->noiseEstQuantile[i] = (int16_t)(
                (tmp32no1 + kRoundTable[tmp16no1]) >> tmp16no1);
        } else if (tmp16no1 > 15) {
            inst->noiseEstQuantile[i] =
                (int16_t)((tmp32no1 + 16384) >> tmp16no1);
        } else {
            inst->noiseEstQuantile[i] =
                (int16_t)(tmp32no1 << -tmp16no1);
        }
    }
}

void TNRx_CalcParametricNoiseEstimate(NsxHandle inst,
                                      int16_t pink_noise_exp_avg,
                                      int32_t pink_noise_num_avg,
                                      int freq_index,
                                      uint32_t *noise_estimate,
                                      uint32_t *noise_estimate_avg)
{
    int32_t tmp32no1 = 0;
    int32_t tmp32no2 = 0;
    int16_t int_part = 0;
    int16_t frac_part = 0;

    tmp32no2 = pink_noise_exp_avg * kLogIndex[freq_index];
    tmp32no2 >>= 15;
    tmp32no1 = pink_noise_num_avg - tmp32no2;
    tmp32no1 += (inst->minNorm - inst->stages) << 11;
    if (tmp32no1 > 0) {
        int_part = (int16_t)(tmp32no1 >> 11);
        frac_part = (int16_t)(tmp32no1 & 0x7ff);
        if (frac_part >> 10) {
            tmp32no2 = (2048 - frac_part) * 1244;
            tmp32no2 = 2048 - (tmp32no2 >> 10);
        } else {
            tmp32no2 = (frac_part * 804) >> 10;
        }
        if (int_part > 10)
            tmp32no2 <<= int_part - 11;
        else
            tmp32no2 >>= 11 - int_part;
        *noise_estimate_avg = (1u << int_part) + (uint32_t)tmp32no2;
        *noise_estimate = *noise_estimate_avg *
                          (uint32_t)(inst->blockIndex + 1);
    }
}

int TNRx_InitCore(NsxHandle inst, uint32_t fs)
{
    int i;

    if (inst == NULL)
        return -1;
    if (fs != 8000 && fs != 16000 && fs != 32000 && fs != 48000)
        return -1;

    inst->fs = fs;
    if (fs == 8000) {
        inst->blockLen10ms = 80;
        inst->anaLen = 128;
        inst->stages = 7;
        inst->window = kBlocks80w128x;
        inst->thresholdLogLrt = 131072;
        inst->maxLrt = 0x40000;
        inst->minLrt = 52429;
    } else if (fs == 16000) {
        inst->blockLen10ms = 160;
        inst->anaLen = 256;
        inst->stages = 8;
        inst->window = kBlocks160w256x;
        inst->thresholdLogLrt = 212644;
        inst->maxLrt = 0x80000;
        inst->minLrt = 104858;
    } else if (fs == 32000) {
        inst->blockLen10ms = 160;
        inst->anaLen = 256;
        inst->stages = 8;
        inst->window = kBlocks160w256x;
        inst->thresholdLogLrt = 212644;
        inst->maxLrt = 0x80000;
        inst->minLrt = 104858;
    } else if (fs == 48000) {
        inst->blockLen10ms = 160;
        inst->anaLen = 256;
        inst->stages = 8;
        inst->window = kBlocks160w256x;
        inst->thresholdLogLrt = 212644;
        inst->maxLrt = 0x80000;
        inst->minLrt = 104858;
    }
    inst->anaLen2 = inst->anaLen >> 1;
    inst->magnLen = inst->anaLen2 + 1;

    memset(inst->analysisBuffer, 0, sizeof(inst->analysisBuffer));
    memset(inst->synthesisBuffer, 0, sizeof(inst->synthesisBuffer));
    memset(inst->dataBufHBFX, 0, sizeof(inst->dataBufHBFX));
    memset(inst->dataBufHBFX1, 0, sizeof(inst->dataBufHBFX1));
    memset(inst->noiseEstQuantile, 0, sizeof(inst->noiseEstQuantile));
    for (i = 0;; i++) {
        inst->noiseEstLogQuantile[i] = 2048;
        inst->noiseEstDensity[i] = 153;
        if (i == 386)
            break;
    }
    inst->noiseEstCounter[0] = 66;
    inst->noiseEstCounter[1] = 133;
    inst->noiseEstCounter[2] = 200;
    memset(inst->noiseSupFilter, 0, sizeof(inst->noiseSupFilter));

    inst->aggrMode = 0;
    inst->priorNonSpeechProb = 8192;
    for (i = 0; i < 129; ++i) {
        inst->prevMagnU16[i] = 0;
        inst->prevNoiseU32[i] = 0;
        inst->logLrtTimeAvgW32[i] = 0;
        inst->avgMagnPause[i] = 0;
        inst->initMagnEst[i] = 0;
    }
    inst->thresholdSpecDiff = 50;
    inst->thresholdSpecFlat = 20480;
    inst->featureLogLrt = inst->thresholdLogLrt;
    inst->featureSpecFlat = inst->thresholdSpecFlat;
    inst->featureSpecDiff = inst->thresholdSpecDiff;
    inst->weightLogLrt = 6;
    inst->weightSpecFlat = 0;
    inst->weightSpecDiff = 0;
    inst->curAvgMagnEnergy = 0;
    inst->timeAvgMagnEnergy = 0;
    inst->timeAvgMagnEnergyTmp = 0;
    memset(inst->histLrt, 0, sizeof(inst->histLrt));
    memset(inst->histSpecDiff, 0, sizeof(inst->histSpecDiff));
    memset(inst->histSpecFlat, 0, sizeof(inst->histSpecFlat));
    inst->blockIndex = -1;
    inst->modelUpdate = 512;
    inst->cntThresUpdate = 0;
    inst->sumMagn = 0;
    inst->magnEnergy = 0;
    inst->prevQMagn = 0;
    inst->qNoise = 0;
    inst->prevQNoise = 0;
    inst->energyIn = 0;
    inst->scaleEnergyIn = 0;
    inst->whiteNoiseLevel = 0;
    inst->pinkNoiseNumerator = 0;
    inst->pinkNoiseExp = 0;
    inst->minNorm = 15;
    inst->zeroInputSignal = 0;
    inst->overdrive = 256;
    inst->denoiseBound = 8192;
    inst->gainMap = 0;

    if (Init_TDereverb_x_Params(&inst->pTDPar, inst->magnLen, fs)) {
        inst->Dereverb_switch = 0;
        inst->initFlag = 1;
    }
    return 0;
}

int TNRx_set_policy_core(NsxHandle inst, int mode)
{
    if ((unsigned int)mode > 3)
        return -1;

    inst->aggrMode = mode;
    if (mode == 0) {
        inst->overdrive = 256;
        inst->denoiseBound = 8192;
        inst->gainMap = 0;
    } else if (mode == 1) {
        inst->overdrive = 256;
        inst->denoiseBound = 4096;
        inst->factor2Table = kFactor2Aggressiveness1;
        inst->gainMap = 1;
    } else if (mode == 2) {
        inst->overdrive = 282;
        inst->denoiseBound = 2048;
        inst->factor2Table = kFactor2Aggressiveness2;
        inst->gainMap = 1;
    } else {
        inst->overdrive = 320;
        inst->denoiseBound = 1475;
        inst->factor2Table = kFactor2Aggressiveness3;
        inst->gainMap = 1;
    }
    return 0;
}

int TNRx_set_dereverb_core(NsxHandle inst, int mode)
{
    if ((unsigned int)mode > 1)
        return -1;
    if (mode == 0)
        inst->Dereverb_switch = 0;
    else
        inst->Dereverb_switch = 1;
    return 0;
}

void TNRx_NoiseEstimation(NsxHandle inst, uint16_t *magn, uint32_t *noise,
                          int16_t *q_noise)
{
    int32_t numerator;
    int16_t lmagn[129], counter, count_div, count_prod, delta, zeros, frac;
    int16_t log2, tabind, logval, tmp16, tmp16no1, tmp16no2;
    int16_t log2Const = 22713;
    int16_t widthFactor = 21845;
    int i, s, offset;

    numerator = 0x280000;
    tabind = inst->stages - inst->normData;
    logval = (int16_t)(tabind * 177);
    if (tabind < 0)
        logval += tabind / 2;
    else
        logval += tabind >> 1;

    for (i = 0; i < inst->magnLen; i++) {
        if (magn[i]) {
            zeros = (int16_t)TSpl_NormU32((uint32_t)magn[i]);
            frac = (int16_t)((((uint32_t)magn[i] << zeros) & 0x7fffffff) >>
                             23);
            log2 = (int16_t)(((31 - zeros) << 8) +
                             TNRx_kLogTableFrac[frac]);
            lmagn[i] = (int16_t)((log2 * log2Const) >> 15);
            lmagn[i] += logval;
        } else {
            lmagn[i] = logval;
        }
    }

    for (s = 0; s < 3; s++) {
        offset = s * inst->magnLen;
        counter = inst->noiseEstCounter[s];
        count_div = 32768 / (counter + 1);
        if ((counter + 1) < 2 * (32768 - (counter + 1) * count_div))
            count_div++;
        if (count_div < 0)
            count_div = 32767;
        count_prod = (int16_t)(counter * count_div);

        for (i = 0; i < inst->magnLen; i++) {
            if (inst->noiseEstDensity[offset + i] > 512) {
                delta = TSpl_DivW32W16ResW16(
                    numerator, inst->noiseEstDensity[offset + i]);
            } else {
                delta = 5120;
            }

            tmp16 = (int16_t)((delta * count_div) >> 14);
            if (lmagn[i] > inst->noiseEstLogQuantile[offset + i]) {
                tmp16 += 2;
                tmp16no1 = tmp16 >> 2;
                inst->noiseEstLogQuantile[offset + i] += tmp16no1;
            } else {
                tmp16 += 1;
                tmp16no1 = tmp16 >> 1;
                tmp16no2 = (int16_t)((tmp16no1 * 3) >> 1);
                inst->noiseEstLogQuantile[offset + i] -= tmp16no2;
            }

            if ((unsigned int)(lmagn[i] -
                               inst->noiseEstLogQuantile[offset + i] + 2) <=
                4) {
                tmp16no1 = (int16_t)(
                    ((int32_t)inst->noiseEstDensity[offset + i] * count_prod +
                     16384) >> 15);
                tmp16no2 = (int16_t)((widthFactor * count_div + 16384) >>
                                      15);
                inst->noiseEstDensity[offset + i] = tmp16no1 + tmp16no2;
            }
        }

        if (counter > 199) {
            inst->noiseEstCounter[s] = 0;
            if (inst->blockIndex > 199)
                TNRx_UpdateNoiseEstimate(inst, offset);
        }
        inst->noiseEstCounter[s]++;
    }

    if (inst->blockIndex < 200)
        TNRx_UpdateNoiseEstimate(inst, offset);
    for (i = 0; i < inst->magnLen; i++)
        noise[i] = inst->noiseEstQuantile[i];
    *q_noise = (int16_t)inst->qNoise;
}

void TNRx_ComputeSpectralFlatness(NsxHandle inst, uint16_t *magn)
{
    uint32_t tmp_u32;
    uint32_t numerator = 0;
    uint32_t denominator = inst->sumMagn - (uint32_t)magn[0];
    int32_t tmp32;
    int32_t current_flatness;
    int32_t log_flatness;
    int16_t zeros;
    int16_t frac;
    int16_t int_part;
    int i;

    for (i = 1; i < inst->magnLen; i++) {
        if (magn[i]) {
            zeros = TSpl_NormU32((uint32_t)magn[i]);
            frac = (int16_t)((((uint32_t)magn[i] << zeros) & 0x7fffffff) >>
                             23);
            tmp_u32 = (uint32_t)(((31 - zeros) << 8) +
                                 TNRx_kLogTableFrac[frac]);
            numerator += tmp_u32;
        } else {
            tmp_u32 = inst->featureSpecFlat * 0x1333;
            inst->featureSpecFlat -= tmp_u32 >> 14;
            return;
        }
    }

    zeros = TSpl_NormU32(denominator);
    frac = (int16_t)(((denominator << zeros) & 0x7fffffff) >> 23);
    tmp32 = ((31 - zeros) << 8) + TNRx_kLogTableFrac[frac];
    log_flatness = (int32_t)numerator;
    log_flatness += (inst->stages - 1) << (inst->stages + 7);
    log_flatness -= tmp32 << (inst->stages - 1);
    log_flatness <<= 10 - inst->stages;
    tmp32 = 0x20000 |
            ((log_flatness < 0 ? -log_flatness : log_flatness) & 0x1ffff);
    int_part = (int16_t)-(log_flatness >> 17);
    int_part += 7;
    if (int_part > 0)
        current_flatness = tmp32 >> int_part;
    else
        current_flatness = tmp32 << -int_part;

    tmp32 = current_flatness - (int32_t)inst->featureSpecFlat;
    tmp32 = 0x1333 * tmp32;
    inst->featureSpecFlat =
        (uint32_t)((int32_t)inst->featureSpecFlat + (tmp32 >> 14));
}

void TNRx_ComputeSpectralDifference(NsxHandle inst, uint16_t *magn)
{
    uint32_t tmp_u32no1;
    uint32_t tmp_u32no2;
    uint32_t var_magn;
    uint32_t var_pause;
    uint32_t difference;
    int32_t tmp32no1;
    int32_t tmp32no2;
    int32_t avg_pause;
    int32_t avg_magn;
    int32_t cov;
    int32_t max_pause;
    int32_t min_pause;
    int16_t tmp16no1;
    int i;
    int norm32;
    int n_shifts;

    avg_pause = 0;
    max_pause = 0;
    min_pause = inst->avgMagnPause[0];
    for (i = 0; i < inst->magnLen; i++) {
        avg_pause += inst->avgMagnPause[i];
        max_pause = max_pause > inst->avgMagnPause[i] ?
                    max_pause : inst->avgMagnPause[i];
        min_pause = min_pause < inst->avgMagnPause[i] ?
                    min_pause : inst->avgMagnPause[i];
    }
    avg_pause >>= inst->stages - 1;
    avg_magn = (int32_t)(inst->sumMagn >> (inst->stages - 1));
    tmp32no1 = max_pause - avg_pause > avg_pause - min_pause ?
               max_pause - avg_pause : avg_pause - min_pause;
    n_shifts = 10 + inst->stages - TSpl_NormW32(tmp32no1);
    n_shifts = 0 > n_shifts ? 0 :
               10 + inst->stages - TSpl_NormW32(tmp32no1);

    var_magn = 0;
    var_pause = 0;
    cov = 0;
    for (i = 0; i < inst->magnLen; i++) {
        tmp16no1 = (int16_t)((int32_t)magn[i] - avg_magn);
        tmp32no2 = inst->avgMagnPause[i] - avg_pause;
        var_magn += (uint32_t)((int32_t)tmp16no1 * tmp16no1);
        tmp32no1 = tmp32no2 * tmp16no1;
        cov += tmp32no1;
        tmp32no1 = tmp32no2 >> n_shifts;
        var_pause += (uint32_t)(tmp32no1 * tmp32no1);
    }
    inst->curAvgMagnEnergy +=
        inst->magnEnergy >> (2 * inst->normData + inst->stages - 1);

    difference = var_magn;
    if (var_pause && cov) {
        tmp_u32no1 = (uint32_t)(cov >= 0 ? cov : -cov);
        norm32 = TSpl_NormU32(tmp_u32no1) - 16;
        if (norm32 > 0)
            tmp_u32no1 <<= norm32;
        else
            tmp_u32no1 >>= -norm32;
        tmp_u32no2 = tmp_u32no1 * tmp_u32no1;

        n_shifts += norm32;
        n_shifts <<= 1;
        if (n_shifts < 0) {
            var_pause >>= -n_shifts;
            n_shifts = 0;
        }
        tmp_u32no1 = tmp_u32no2 / (var_pause + 1);
        tmp_u32no1 >>= n_shifts;
        difference -= difference < tmp_u32no1 ? difference : tmp_u32no1;
    }
    tmp_u32no1 = difference >> (2 * inst->normData);
    if (inst->featureSpecDiff > tmp_u32no1) {
        tmp_u32no2 = (inst->featureSpecDiff - tmp_u32no1) * 0x4d;
        inst->featureSpecDiff -= tmp_u32no2 >> 8;
    } else {
        tmp_u32no2 = (tmp_u32no1 - inst->featureSpecDiff) * 0x4d;
        inst->featureSpecDiff += tmp_u32no2 >> 8;
    }
}

void TNRx_FeatureParameterExtraction(NsxHandle inst, int flag)
{
    uint32_t tmpU32;
    uint32_t histIndex;
    uint32_t posPeak1SpecFlatFX, posPeak2SpecFlatFX;
    uint32_t posPeak1SpecDiffFX, posPeak2SpecDiffFX;
    int32_t tmp32;
    int32_t fluctLrtFX, thresFluctLrtFX;
    int32_t avgHistLrtFX, avgSquareHistLrtFX, avgHistLrtComplFX;
    int16_t j;
    int16_t numHistLrt;
    int i;
    int useFeatureSpecFlat, useFeatureSpecDiff, featureSum;
    int maxPeak1, maxPeak2;
    int weightPeak1SpecFlat, weightPeak2SpecFlat;
    int weightPeak1SpecDiff, weightPeak2SpecDiff;

    if (!flag) {
        histIndex = (uint32_t)inst->featureLogLrt;
        if (histIndex < 1000)
            ++inst->histLrt[histIndex];
        histIndex = inst->featureSpecFlat * 5 >> 8;
        if (histIndex < 1000)
            ++inst->histSpecFlat[histIndex];
        histIndex = 1000;
        if (inst->timeAvgMagnEnergy) {
            histIndex = (inst->featureSpecDiff * 5 >> inst->stages) /
                        (inst->timeAvgMagnEnergy + 1);
        }
        if (histIndex < 1000)
            ++inst->histSpecDiff[histIndex];
    }

    if (flag) {
        useFeatureSpecDiff = 1;
        avgHistLrtFX = 0;
        avgSquareHistLrtFX = 0;
        numHistLrt = 0;
        for (i = 0; i < 10; ++i) {
            j = (int16_t)(2 * i + 1);
            tmp32 = inst->histLrt[i] * j;
            avgHistLrtFX += tmp32;
            numHistLrt += inst->histLrt[i];
            avgSquareHistLrtFX += tmp32 * j;
        }
        avgHistLrtComplFX = avgHistLrtFX;
        for (; i < 1000; ++i) {
            j = (int16_t)(2 * i + 1);
            tmp32 = inst->histLrt[i] * j;
            avgHistLrtComplFX += tmp32;
            avgSquareHistLrtFX += tmp32 * j;
        }
        fluctLrtFX = avgSquareHistLrtFX * numHistLrt;
        fluctLrtFX -= avgHistLrtFX * avgHistLrtComplFX;
        thresFluctLrtFX = 10240 * numHistLrt;
        tmpU32 = 6 * (uint32_t)avgHistLrtFX;
        if (fluctLrtFX < thresFluctLrtFX || numHistLrt == 0 ||
            tmpU32 > (uint32_t)(100 * numHistLrt)) {
            inst->thresholdLogLrt = inst->maxLrt;
        } else {
            tmp32 = (int32_t)((tmpU32 << (9 + inst->stages)) /
                              numHistLrt / 25);
            if (tmp32 > inst->maxLrt)
                tmp32 = inst->maxLrt;
            else if (tmp32 < inst->minLrt)
                tmp32 = inst->minLrt;
            inst->thresholdLogLrt = tmp32;
        }
        if (fluctLrtFX < thresFluctLrtFX)
            useFeatureSpecDiff = 0;

        maxPeak1 = 0;
        maxPeak2 = 0;
        posPeak1SpecFlatFX = 0;
        posPeak2SpecFlatFX = 0;
        weightPeak1SpecFlat = 0;
        weightPeak2SpecFlat = 0;
        for (i = 0; i < 1000; ++i) {
            if (inst->histSpecFlat[i] > maxPeak1) {
                maxPeak2 = maxPeak1;
                weightPeak2SpecFlat = weightPeak1SpecFlat;
                posPeak2SpecFlatFX = posPeak1SpecFlatFX;
                maxPeak1 = inst->histSpecFlat[i];
                weightPeak1SpecFlat = inst->histSpecFlat[i];
                posPeak1SpecFlatFX = (uint32_t)(2 * i + 1);
            } else if (inst->histSpecFlat[i] > maxPeak2) {
                maxPeak2 = inst->histSpecFlat[i];
                weightPeak2SpecFlat = inst->histSpecFlat[i];
                posPeak2SpecFlatFX = (uint32_t)(2 * i + 1);
            }
        }

        useFeatureSpecFlat = 1;
        if (posPeak1SpecFlatFX - posPeak2SpecFlatFX < 4 &&
            weightPeak2SpecFlat * 2 > weightPeak1SpecFlat) {
            weightPeak1SpecFlat += weightPeak2SpecFlat;
            posPeak1SpecFlatFX =
                (posPeak1SpecFlatFX + posPeak2SpecFlatFX) >> 1;
        }
        if (weightPeak1SpecFlat < 154 || posPeak1SpecFlatFX < 24) {
            useFeatureSpecFlat = 0;
        } else {
            tmpU32 = 922 * posPeak1SpecFlatFX;
            if (tmpU32 > 38912)
                tmpU32 = 38912;
            else if (tmpU32 < 4096)
                tmpU32 = 4096;
            inst->thresholdSpecFlat = tmpU32;
        }

        if (useFeatureSpecDiff) {
            maxPeak1 = 0;
            maxPeak2 = 0;
            posPeak1SpecDiffFX = 0;
            posPeak2SpecDiffFX = 0;
            weightPeak1SpecDiff = 0;
            weightPeak2SpecDiff = 0;
            for (i = 0; i < 1000; ++i) {
                if (inst->histSpecDiff[i] > maxPeak1) {
                    maxPeak2 = maxPeak1;
                    weightPeak2SpecDiff = weightPeak1SpecDiff;
                    posPeak2SpecDiffFX = posPeak1SpecDiffFX;
                    maxPeak1 = inst->histSpecDiff[i];
                    weightPeak1SpecDiff = inst->histSpecDiff[i];
                    posPeak1SpecDiffFX = (uint32_t)(2 * i + 1);
                } else if (inst->histSpecDiff[i] > maxPeak2) {
                    maxPeak2 = inst->histSpecDiff[i];
                    weightPeak2SpecDiff = inst->histSpecDiff[i];
                    posPeak2SpecDiffFX = (uint32_t)(2 * i + 1);
                }
            }
            if (posPeak1SpecDiffFX - posPeak2SpecDiffFX < 4 &&
                weightPeak2SpecDiff * 2 > weightPeak1SpecDiff) {
                weightPeak1SpecDiff += weightPeak2SpecDiff;
                posPeak1SpecDiffFX =
                    (posPeak1SpecDiffFX + posPeak2SpecDiffFX) >> 1;
            }
            tmpU32 = 6 * posPeak1SpecDiffFX;
            if (tmpU32 > 100)
                tmpU32 = 100;
            else if (tmpU32 < 16)
                tmpU32 = 16;
            inst->thresholdSpecDiff = tmpU32;
            if (weightPeak1SpecDiff < 154)
                useFeatureSpecDiff = 0;
        }

        featureSum = 6 / (1 + useFeatureSpecFlat + useFeatureSpecDiff);
        inst->weightLogLrt = (int16_t)featureSum;
        inst->weightSpecFlat = (int16_t)(useFeatureSpecFlat * featureSum);
        inst->weightSpecDiff = (int16_t)(useFeatureSpecDiff * featureSum);
        memset(inst->histLrt, 0, sizeof(inst->histLrt));
        memset(inst->histSpecDiff, 0, sizeof(inst->histSpecDiff));
        memset(inst->histSpecFlat, 0, sizeof(inst->histSpecFlat));
    }
}

void TNRx_SpeechNoiseProb(NsxHandle inst, uint16_t *non_speech_prob,
                          uint32_t *prior_snr, uint32_t *post_snr)
{
    uint32_t zeros, num, den, tmpU32no1, tmpU32no2, tmpU32no3;
    int32_t invLrtFX, indPriorFX, tmp32, tmp32no1, tmp32no2, besselTmpFX32;
    int32_t frac32, logTmp;
    int32_t logLrtTimeAvgKsumFX;
    int16_t indPriorFX16;
    int16_t tmp16, tmp16no1, tmp16no2, tmpIndFX, tableIndex, frac, intPart;
    int i, normTmp, normTmp2, nShifts;

    logLrtTimeAvgKsumFX = 0;
    for (i = 0; i < inst->magnLen; i++) {
        besselTmpFX32 = ((int32_t *)post_snr)[i];
        normTmp = TSpl_NormU32((uint32_t)besselTmpFX32);
        num = post_snr[i] << normTmp;
        if (normTmp > 10)
            den = prior_snr[i] << (normTmp - 11);
        else
            den = prior_snr[i] >> (11 - normTmp);
        zeros = (uint32_t)TSpl_NormU32(prior_snr[i]);
        frac32 = (int32_t)(((prior_snr[i] << zeros) & 0x7fffffff) >> 19);
        besselTmpFX32 -= (int32_t)(num / (den + 1));
        tmp32 = frac32 * frac32;
        tmp32 = (tmp32 * -43) >> 19;
        tmp32 += ((int16_t)frac32 * 5412) >> 12;
        frac32 = tmp32 + 37;
        tmp32 = (((31 - (int)zeros) << 12) + frac32) - (11 << 12);
        logTmp = (tmp32 * 178) >> 8;
        tmp32no1 = (logTmp + inst->logLrtTimeAvgW32[i]) >> 1;
        inst->logLrtTimeAvgW32[i] += besselTmpFX32 - tmp32no1;
        logLrtTimeAvgKsumFX += inst->logLrtTimeAvgW32[i];
    }
    inst->featureLogLrt =
        (logLrtTimeAvgKsumFX * 5) >> (inst->stages + 10);

    tmpIndFX = 16384;
    tmp32no1 = logLrtTimeAvgKsumFX - inst->thresholdLogLrt;
    nShifts = 7 - inst->stages;
    if (tmp32no1 < 0) {
        tmpIndFX = 0;
        tmp32no1 = -tmp32no1;
        nShifts++;
    }
    tmp32no1 = shift_w32(tmp32no1, nShifts);
    tableIndex = (int16_t)(tmp32no1 >> 14);
    if (tableIndex < 16 && tableIndex >= 0) {
        tmp16no2 = kIndicatorTable[tableIndex];
        tmp16no1 = (int16_t)(kIndicatorTable[tableIndex + 1] -
                             kIndicatorTable[tableIndex]);
        frac = (int16_t)(tmp32no1 & 0x3fff);
        tmp16no2 += (int16_t)((tmp16no1 * frac) >> 14);
        if (tmpIndFX == 0)
            tmpIndFX = (int16_t)(8192 - tmp16no2);
        else
            tmpIndFX = (int16_t)(8192 + tmp16no2);
    }
    indPriorFX = inst->weightLogLrt * tmpIndFX;

    if (inst->weightSpecFlat) {
        tmpU32no1 = inst->featureSpecFlat * 400;
        tmpIndFX = 16384;
        tmpU32no2 = inst->thresholdSpecFlat - tmpU32no1;
        nShifts = 4;
        if (inst->thresholdSpecFlat < tmpU32no1) {
            tmpIndFX = 0;
            tmpU32no2 = tmpU32no1 - inst->thresholdSpecFlat;
            nShifts++;
        }
        tmp32no1 = (int32_t)TSpl_DivU32U16(tmpU32no2 << nShifts, 25);
        tmpU32no1 = TSpl_DivU32U16(tmpU32no2 << nShifts, 25);
        tableIndex = (int16_t)(tmpU32no1 >> 14);
        if (tableIndex < 16) {
            tmp16no2 = kIndicatorTable[tableIndex];
            tmp16no1 = (int16_t)(kIndicatorTable[tableIndex + 1] -
                                 kIndicatorTable[tableIndex]);
            frac = (int16_t)(tmpU32no1 & 0x3fff);
            tmp16no2 += (int16_t)((tmp16no1 * frac) >> 14);
            if (tmpIndFX)
                tmpIndFX = (int16_t)(8192 + tmp16no2);
            else
                tmpIndFX = (int16_t)(8192 - tmp16no2);
        }
        indPriorFX += inst->weightSpecFlat * tmpIndFX;
    }

    if (inst->weightSpecDiff) {
        tmpU32no1 = 0;
        if (inst->featureSpecDiff) {
            if (20 - inst->stages <
                TSpl_NormU32(inst->featureSpecDiff)) {
                normTmp = 20 - inst->stages;
            } else {
                normTmp = TSpl_NormU32(inst->featureSpecDiff);
            }
            tmpU32no1 = inst->featureSpecDiff << normTmp;
            tmpU32no2 = inst->timeAvgMagnEnergy >>
                         (20 - inst->stages - normTmp);
            if (tmpU32no2)
                tmpU32no1 /= tmpU32no2 + 1;
            else
                tmpU32no1 = 0x7fffffff;
        }
        tmpU32no3 = (inst->thresholdSpecDiff << 17) / 26;
        tmpU32no2 = tmpU32no1 - tmpU32no3;
        nShifts = 1;
        tmpIndFX = 16384;
        if (tmpU32no2 & 0x80000000) {
            tmpIndFX = 0;
            tmpU32no2 = tmpU32no3 - tmpU32no1;
            nShifts--;
        }
        tmpU32no1 = tmpU32no2 >> nShifts;
        tableIndex = (int16_t)(tmpU32no1 >> 14);
        if (tableIndex < 16) {
            tmp16no2 = kIndicatorTable[tableIndex];
            tmp16no1 = (int16_t)(kIndicatorTable[tableIndex + 1] -
                                 kIndicatorTable[tableIndex]);
            frac = (int16_t)(tmpU32no1 & 0x3fff);
            tmp16no2 += (int16_t)((tmp16no1 * frac + 8192) >> 14);
            if (tmpIndFX)
                tmpIndFX = (int16_t)(8192 + tmp16no2);
            else
                tmpIndFX = (int16_t)(8192 - tmp16no2);
        }
        indPriorFX += inst->weightSpecDiff * tmpIndFX;
    }

    indPriorFX16 = TSpl_DivW32W16ResW16(98307 - indPriorFX, 6);
    tmp16 = (int16_t)(indPriorFX16 - inst->priorNonSpeechProb);
    inst->priorNonSpeechProb += (int16_t)((1638 * tmp16) >> 14);

    for (i = 0; i < inst->magnLen; i++) {
        non_speech_prob[i] = 0;
        if (inst->logLrtTimeAvgW32[i] < 65300 &&
            inst->priorNonSpeechProb > 0) {
            tmp32no1 = (inst->logLrtTimeAvgW32[i] * 23637) >> 14;
            intPart = (int16_t)(tmp32no1 >> 12);
            if (intPart < -8)
                intPart = -8;
            frac = (int16_t)(tmp32no1 & 0xfff);
            tmp32no2 = (frac * frac * 44) >> 19;
            tmp32no2 += (frac * 84) >> 7;
            invLrtFX = (1 << (8 + intPart)) +
                       (intPart > 3 ? tmp32no2 << (intPart - 4) :
                        tmp32no2 >> (4 - intPart));

            normTmp = TSpl_NormW32(invLrtFX);
            normTmp2 = TSpl_NormW16(
                (int16_t)(16384 - inst->priorNonSpeechProb));
            if (normTmp + normTmp2 < 15) {
                invLrtFX >>= 15 - normTmp2 - normTmp;
                tmp32no1 = invLrtFX *
                           (16384 - inst->priorNonSpeechProb);
                invLrtFX = shift_w32(tmp32no1,
                                     7 - normTmp - normTmp2);
            } else {
                tmp32no1 = invLrtFX *
                           (16384 - inst->priorNonSpeechProb);
                invLrtFX = tmp32no1 >> 8;
            }
            tmp32no1 = (int32_t)inst->priorNonSpeechProb << 8;
            non_speech_prob[i] = (uint16_t)(tmp32no1 /
                (inst->priorNonSpeechProb + invLrtFX));
            if (7 - normTmp - normTmp2 > 0)
                non_speech_prob[i] = 0;
        }
    }
}

void TNRx_DataAnalysis(NsxHandle inst, int16_t *speech_frame, uint16_t *magn)
{
    uint32_t tmpU32no1, tmpU32no2;
    int32_t tmp_1_w32 = 0;
    int32_t tmp_2_w32 = 0;
    int32_t sum_log_magn = 0;
    int32_t sum_log_i_log_magn = 0;
    uint16_t sum_log_magn_u16 = 0;
    uint16_t tmp_u16 = 0;
    int16_t sum_log_i = 0;
    int16_t sum_log_i_square = 0;
    int16_t frac = 0;
    int16_t log2 = 0;
    int16_t matrix_determinant = 0;
    int16_t winData[256], maxWinData;
    int16_t realImag[512];
    int i, j;
    int outCFFT;
    int zeros;
    int net_norm = 0;
    int right_shifts_in_magnU16 = 0;
    int right_shifts_in_initMagnEst = 0;

    memcpy(inst->analysisBuffer,
           inst->analysisBuffer + inst->blockLen10ms,
           (size_t)(inst->anaLen - inst->blockLen10ms) * sizeof(int16_t));
    memcpy(inst->analysisBuffer + inst->anaLen - inst->blockLen10ms,
           speech_frame, (size_t)inst->blockLen10ms * sizeof(int16_t));
    for (i = 0; i < inst->anaLen; i++) {
        winData[i] = (int16_t)(((int32_t)inst->window[i] *
                                inst->analysisBuffer[i] + 8192) >> 14);
    }
    inst->energyIn = TSpl_Energy(winData, (size_t)inst->anaLen,
                                 &inst->scaleEnergyIn);
    inst->zeroInputSignal = 0;
    maxWinData = TSpl_MaxAbsValueW16(winData, inst->anaLen);
    inst->normData = TSpl_NormW16(maxWinData);
    if (maxWinData == 0) {
        inst->zeroInputSignal = 1;
        return;
    }

    net_norm = inst->stages - inst->normData;
    right_shifts_in_magnU16 = inst->normData - inst->minNorm;
    right_shifts_in_initMagnEst =
        -right_shifts_in_magnU16 > 0 ? -right_shifts_in_magnU16 : 0;
    inst->minNorm -= right_shifts_in_initMagnEst;
    right_shifts_in_magnU16 =
        right_shifts_in_magnU16 > 0 ? right_shifts_in_magnU16 : 0;

    for (i = 0; i < inst->anaLen; i++) {
        j = i << 1;
        realImag[j] = (int16_t)(winData[i] << inst->normData);
        realImag[j + 1] = 0;
    }
    TSpl_ComplexBitReverse(realImag, inst->stages);
    outCFFT = TSpl_ComplexFFT(realImag, inst->stages, 1);
    (void)outCFFT;

    inst->imag[0] = 0;
    inst->imag[inst->anaLen2] = 0;
    inst->real[0] = realImag[0];
    inst->real[inst->anaLen2] = realImag[inst->anaLen];
    inst->magnEnergy = (uint32_t)((int32_t)inst->real[0] * inst->real[0]);
    inst->magnEnergy += (uint32_t)((int32_t)inst->real[inst->anaLen2] *
                                   inst->real[inst->anaLen2]);
    magn[0] = (uint16_t)(inst->real[0] >= 0 ? inst->real[0] : -inst->real[0]);
    magn[inst->anaLen2] = (uint16_t)(inst->real[inst->anaLen2] >= 0 ?
        inst->real[inst->anaLen2] : -inst->real[inst->anaLen2]);
    inst->sumMagn = magn[0];
    inst->sumMagn += magn[inst->anaLen2];

    if (inst->blockIndex < 50) {
        inst->initMagnEst[0] >>= right_shifts_in_initMagnEst;
        inst->initMagnEst[inst->anaLen2] >>= right_shifts_in_initMagnEst;
        tmpU32no1 = (uint32_t)magn[0] >> right_shifts_in_magnU16;
        tmpU32no2 = (uint32_t)magn[inst->anaLen2] >>
                     right_shifts_in_magnU16;
        inst->initMagnEst[0] += tmpU32no1;
        inst->initMagnEst[inst->anaLen2] += tmpU32no2;
        log2 = 0;
        if (magn[inst->anaLen2]) {
            zeros = TSpl_NormU32((uint32_t)magn[inst->anaLen2]);
            frac = (int16_t)((((uint32_t)magn[inst->anaLen2] << zeros) &
                              0x7fffffff) >> 23);
            log2 = (int16_t)(((31 - zeros) << 8) +
                             TNRx_kLogTableFrac[frac]);
        }
        sum_log_magn = log2;
        sum_log_i_log_magn = ((int32_t)kLogIndex[inst->anaLen2] * log2) >> 3;
    }

    for (i = 1; i < inst->anaLen2; i++) {
        j = i << 1;
        inst->real[i] = realImag[j];
        inst->imag[i] = (int16_t)-realImag[j + 1];
        tmpU32no1 = (uint32_t)((int32_t)realImag[j] * realImag[j]);
        tmpU32no1 += (uint32_t)((int32_t)realImag[j + 1] * realImag[j + 1]);
        inst->magnEnergy += tmpU32no1;
        magn[i] = (uint16_t)TSpl_Sqrt((int32_t)tmpU32no1);
        inst->sumMagn += magn[i];
        if (inst->blockIndex < 50) {
            inst->initMagnEst[i] >>= right_shifts_in_initMagnEst;
            tmpU32no1 = (uint32_t)magn[i] >> right_shifts_in_magnU16;
            inst->initMagnEst[i] += tmpU32no1;
            if (i >= 5) {
                log2 = 0;
                if (magn[i]) {
                    zeros = TSpl_NormU32((uint32_t)magn[i]);
                    frac = (int16_t)((((uint32_t)magn[i] << zeros) &
                                      0x7fffffff) >> 23);
                    log2 = (int16_t)(((31 - zeros) << 8) +
                                     TNRx_kLogTableFrac[frac]);
                }
                sum_log_magn += log2;
                sum_log_i_log_magn += ((int32_t)kLogIndex[i] * log2) >> 3;
            }
        }
    }

    if (inst->blockIndex < 50) {
        inst->whiteNoiseLevel >>= right_shifts_in_initMagnEst;
        tmpU32no1 = inst->sumMagn * inst->overdrive;
        tmpU32no1 >>= inst->stages + 8;
        tmpU32no1 >>= right_shifts_in_magnU16;
        inst->whiteNoiseLevel += tmpU32no1;

        matrix_determinant = 18469;
        sum_log_i = 22770;
        sum_log_i_square = 16929;
        if (inst->fs == 8000) {
            tmp_1_w32 = matrix_determinant;
            tmp_1_w32 += ((int32_t)13445 * sum_log_i) >> 9;
            tmp_1_w32 -= ((int32_t)13445 * 13445) >> 10;
            tmp_1_w32 -= (int32_t)sum_log_i_square << 4;
            tmp_1_w32 -= ((int16_t)(inst->magnLen - 5) * 11054) >> 2;
            matrix_determinant = (int16_t)tmp_1_w32;
            sum_log_i -= 13445;
            sum_log_i_square -= 11054;
        }

        zeros = 16 - TSpl_NormW32(sum_log_magn);
        if (zeros < 0)
            zeros = 0;
        tmp_1_w32 = sum_log_magn << 1;
        sum_log_magn_u16 = (uint16_t)(tmp_1_w32 >> zeros);
        tmp_2_w32 = (int32_t)sum_log_i_square * sum_log_magn_u16;
        tmpU32no1 = (uint32_t)sum_log_i_log_magn >> 12;
        tmp_u16 = (uint16_t)sum_log_i << 1;
        if ((uint32_t)sum_log_i > tmpU32no1)
            tmp_u16 >>= zeros;
        else
            tmpU32no1 >>= zeros;
        tmp_2_w32 -= (int32_t)(tmpU32no1 * tmp_u16);
        matrix_determinant = (int16_t)(matrix_determinant >> zeros);
        tmp_2_w32 = TSpl_DivW32W16(tmp_2_w32, matrix_determinant);
        tmp_2_w32 += net_norm << 11;
        if (tmp_2_w32 < 0)
            tmp_2_w32 = 0;
        inst->pinkNoiseNumerator += tmp_2_w32;

        tmp_2_w32 = (int32_t)sum_log_i * sum_log_magn_u16;
        tmp_1_w32 = sum_log_i_log_magn >> (3 + zeros);
        tmp_1_w32 = (inst->magnLen - 5) * tmp_1_w32;
        tmp_2_w32 -= tmp_1_w32;
        if (tmp_2_w32 > 0) {
            tmp_1_w32 = TSpl_DivW32W16(tmp_2_w32, matrix_determinant);
            inst->pinkNoiseExp += tmp_1_w32 > 16384 ? 16384 :
                                  tmp_1_w32 < 0 ? 0 : tmp_1_w32;
        }
    }
}

void TNRx_DataSynthesis(NsxHandle inst, int16_t *out_frame, int tns_mode)
{
    int32_t derever_tmp;
    int32_t tmp32no1;
    int32_t energy_out;
    int16_t real_imag[512];
    int16_t tmp16no1;
    int16_t tmp16no2;
    int16_t energy_ratio;
    int16_t gain_factor;
    int16_t gain_factor1;
    int16_t gain_factor2;
    int i;
    int j;
    int out_cifft;
    int scale_energy_out = 0;

    (void)tns_mode;
    if (inst->zeroInputSignal) {
        for (i = 0; i < inst->blockLen10ms; i++)
            out_frame[i] = inst->synthesisBuffer[i];
        memcpy(inst->synthesisBuffer,
               inst->synthesisBuffer + inst->blockLen10ms,
               (size_t)(inst->anaLen - inst->blockLen10ms) *
               sizeof(*inst->synthesisBuffer));
        memset(inst->synthesisBuffer + inst->anaLen - inst->blockLen10ms, 0,
               (size_t)inst->blockLen10ms * sizeof(*inst->synthesisBuffer));
        return;
    }

    for (i = 0; i < inst->magnLen; i++) {
        inst->real[i] = (int16_t)((inst->real[i] *
                                   (int16_t)inst->noiseSupFilter[i]) >> 14);
        inst->imag[i] = (int16_t)((inst->imag[i] *
                                   (int16_t)inst->noiseSupFilter[i]) >> 14);
    }
    if (inst->Dereverb_switch == 1) {
        for (i = 0; i < inst->magnLen; i++) {
            derever_tmp = inst->real[i] * inst->pTDPar.TDereverb_Gain[i];
            inst->real[i] = (int16_t)(derever_tmp >> 15);
            derever_tmp = inst->imag[i] * inst->pTDPar.TDereverb_Gain[i];
            inst->imag[i] = (int16_t)(derever_tmp >> 15);
        }
    }

    real_imag[0] = inst->real[0];
    real_imag[1] = (int16_t)-inst->imag[0];
    for (i = 1; i < inst->anaLen2; i++) {
        j = i << 1;
        tmp16no1 = (int16_t)((inst->anaLen << 1) - j);
        real_imag[j] = inst->real[i];
        real_imag[j + 1] = (int16_t)-inst->imag[i];
        real_imag[tmp16no1] = inst->real[i];
        real_imag[tmp16no1 + 1] = inst->imag[i];
    }
    real_imag[inst->anaLen] = inst->real[inst->anaLen2];
    real_imag[inst->anaLen + 1] = (int16_t)-inst->imag[inst->anaLen2];

    TSpl_ComplexBitReverse(real_imag, inst->stages);
    out_cifft = TSpl_ComplexIFFT(real_imag, inst->stages, 1);

    for (i = 0; i < inst->anaLen; i++) {
        j = i << 1;
        tmp32no1 = shift_w32(real_imag[j], out_cifft - inst->normData);
        inst->real[i] = sat_w32_to_w16(tmp32no1);
    }

    gain_factor = 8192;
    if (inst->gainMap == 1 && inst->blockIndex > 200 && inst->energyIn > 0) {
        energy_out = TSpl_Energy(inst->real, inst->anaLen,
                                 &scale_energy_out);
        if (scale_energy_out == 0 && !(energy_out & 0x7f800000)) {
            energy_out = shift_w32(energy_out,
                                   8 + scale_energy_out -
                                   inst->scaleEnergyIn);
        } else {
            inst->energyIn >>= 8 + scale_energy_out - inst->scaleEnergyIn;
        }

        energy_ratio = (int16_t)((energy_out + (inst->energyIn >> 1)) /
                                 inst->energyIn);
        gain_factor1 = 8192;
        if (energy_ratio > 64) {
            gain_factor1 = 0;
            if (energy_ratio <= 255)
                gain_factor1 = kFactor1Table[energy_ratio - 65];
        }
        if ((uint16_t)energy_ratio <= 63)
            gain_factor2 = inst->factor2Table[energy_ratio];
        else
            gain_factor2 = 8192;

        tmp16no1 = (int16_t)(16384 - inst->priorNonSpeechProb);
        tmp16no1 = (int16_t)((tmp16no1 * gain_factor1) >> 14);
        tmp16no2 = (int16_t)((inst->priorNonSpeechProb *
                              gain_factor2) >> 14);
        gain_factor = (int16_t)(tmp16no1 + tmp16no2);
    }

    for (i = 0; i < inst->anaLen; i++) {
        tmp16no1 = (int16_t)((inst->window[i] * inst->real[i] + 8192) >>
                             14);
        tmp32no1 = (tmp16no1 * gain_factor + 4096) >> 13;
        tmp16no2 = sat_w32_to_w16(tmp32no1);
        inst->synthesisBuffer[i] =
            TSpl_AddSatW16(inst->synthesisBuffer[i], tmp16no2);
    }

    for (i = 0; i < inst->blockLen10ms; i++)
        out_frame[i] = inst->synthesisBuffer[i];
    memcpy(inst->synthesisBuffer,
            inst->synthesisBuffer + inst->blockLen10ms,
            (size_t)(inst->anaLen - inst->blockLen10ms) *
            sizeof(*inst->synthesisBuffer));
    memset(inst->synthesisBuffer + inst->anaLen - inst->blockLen10ms, 0,
           (size_t)inst->blockLen10ms * sizeof(*inst->synthesisBuffer));
}

int TNRx_ProcessCore(NsxHandle inst, int16_t *speech_frame,
                     int16_t *speech_frame_hb, int16_t *out_frame,
                     int16_t *out_frame_hb, int tns_mode)
{
    uint32_t tmpU32no1, tmpU32no2, tmpU32no3;
    uint32_t satMax, maxNoiseU32;
    uint32_t tmpMagnU32, tmpNoiseU32;
    uint32_t nearMagnEst;
    uint32_t noiseUpdateU32;
    uint32_t noiseU32[129];
    uint32_t postLocSnr[129];
    uint32_t priorLocSnr[129];
    uint32_t prevNearSnr[129];
    uint32_t curNearSnr;
    uint32_t priorSnr;
    uint32_t noise_estimate = 0;
    uint32_t noise_estimate_avg = 0;
    uint32_t numerator = 0;
    int32_t tmp32no1, tmp32no2;
    int32_t pink_noise_num_avg = 0;
    uint16_t tmpU16no1;
    uint16_t magnU16[129];
    uint16_t prevNoiseU16[129];
    uint16_t nonSpeechProbFinal[129];
    uint16_t gammaNoise, prevGammaNoise;
    uint16_t noiseSupFilterTmp[129];
    int16_t qMagn, qNoise;
    int16_t avgProbSpeechHB, gainModHB, avgFilterGainHB, gainTimeDomainHB;
    int16_t pink_noise_exp_avg = 0;
    int i;
    int nShifts, postShifts;
    int norm32no1, norm32no2;
    int flag, sign;
    int q_domain_to_use = 0;

    if (inst->initFlag != 1)
        return -1;
    if (inst->fs == 32000 && speech_frame_hb == NULL)
        return -1;
    if (inst->fs == 48000 && speech_frame_hb == NULL)
        return -1;

    TNRx_DataAnalysis(inst, speech_frame, magnU16);
    if (inst->zeroInputSignal) {
        TNRx_DataSynthesis(inst, out_frame, tns_mode);
        if (inst->fs == 32000) {
            memcpy(inst->dataBufHBFX,
                   inst->dataBufHBFX + inst->blockLen10ms,
                   (size_t)(inst->anaLen - inst->blockLen10ms) *
                   sizeof(*inst->dataBufHBFX));
            memcpy(inst->dataBufHBFX + inst->anaLen - inst->blockLen10ms,
                   speech_frame_hb,
                   (size_t)inst->blockLen10ms * sizeof(*speech_frame_hb));
            for (i = 0; i < inst->blockLen10ms; i++)
                out_frame_hb[i] = inst->dataBufHBFX[i];
        } else if (inst->fs == 48000) {
            memcpy(inst->dataBufHBFX,
                   inst->dataBufHBFX + inst->blockLen10ms,
                   (size_t)(inst->anaLen - inst->blockLen10ms) *
                   sizeof(*inst->dataBufHBFX));
            memcpy(inst->dataBufHBFX1,
                   inst->dataBufHBFX1 + inst->blockLen10ms,
                   (size_t)(inst->anaLen - inst->blockLen10ms) *
                   sizeof(*inst->dataBufHBFX1));
            for (i = 0; i < inst->blockLen10ms; i++) {
                inst->dataBufHBFX[inst->anaLen - inst->blockLen10ms + i] =
                    speech_frame_hb[2 * i];
                inst->dataBufHBFX1[inst->anaLen - inst->blockLen10ms + i] =
                    speech_frame_hb[2 * i + 1];
            }
            for (i = 0; i < inst->blockLen10ms; i++) {
                out_frame_hb[2 * i] = inst->dataBufHBFX[i];
                out_frame_hb[2 * i + 1] = inst->dataBufHBFX1[i];
            }
        }
        return 0;
    }

    inst->blockIndex++;
    qMagn = inst->normData - inst->stages;
    TNRx_ComputeSpectralFlatness(inst, magnU16);
    TNRx_NoiseEstimation(inst, magnU16, noiseU32, &qNoise);
    for (i = 0; i < inst->magnLen; i++)
        prevNoiseU16[i] = (uint16_t)(inst->prevNoiseU32[i] >> 11);

    if (inst->blockIndex < 50) {
        q_domain_to_use = qNoise < inst->minNorm - inst->stages ?
                          qNoise : inst->minNorm - inst->stages;
        if (inst->pinkNoiseExp) {
            pink_noise_exp_avg = (int16_t)TSpl_DivW32W16(
                inst->pinkNoiseExp, (int16_t)(inst->blockIndex + 1));
            pink_noise_num_avg = TSpl_DivW32W16(
                inst->pinkNoiseNumerator,
                (int16_t)(inst->blockIndex + 1));
            TNRx_CalcParametricNoiseEstimate(inst, pink_noise_exp_avg,
                                              pink_noise_num_avg, 5,
                                              &noise_estimate,
                                              &noise_estimate_avg);
        } else {
            noise_estimate = inst->whiteNoiseLevel;
            noise_estimate_avg = noise_estimate /
                                 (uint32_t)(inst->blockIndex + 1);
        }
        for (i = 0; i < inst->magnLen; i++) {
            if (inst->pinkNoiseExp && i >= 5) {
                noise_estimate = 0;
                noise_estimate_avg = 0;
                TNRx_CalcParametricNoiseEstimate(
                    inst, pink_noise_exp_avg, pink_noise_num_avg, i,
                    &noise_estimate, &noise_estimate_avg);
            }
            noiseSupFilterTmp[i] = inst->denoiseBound;
            if (inst->initMagnEst[i]) {
                tmpU32no1 = noise_estimate * inst->overdrive;
                numerator = inst->initMagnEst[i] << 8;
                if (numerator > tmpU32no1) {
                    numerator -= tmpU32no1;
                    nShifts = TSpl_NormU32(numerator);
                    nShifts = nShifts > 6 ? 6 :
                              nShifts < 0 ? 0 : nShifts;
                    numerator <<= nShifts;
                    tmpU32no1 = inst->initMagnEst[i] >> (6 - nShifts);
                    tmpU32no2 = numerator / (tmpU32no1 + 1);
                    noiseSupFilterTmp[i] = (uint16_t)(
                        tmpU32no2 > 16384 ? 16384 :
                        tmpU32no2 < (uint32_t)inst->denoiseBound ?
                        (uint32_t)inst->denoiseBound : tmpU32no2);
                }
            }
            tmpU32no1 = noiseU32[i] >> (qNoise - q_domain_to_use);
            tmpU32no2 = noise_estimate_avg >>
                         (inst->minNorm - inst->stages - q_domain_to_use);
            nShifts = 0;
            if (tmpU32no1 & 0xfc000000) {
                tmpU32no1 >>= 6;
                tmpU32no2 >>= 6;
                nShifts = 6;
            }
            noiseU32[i] = TSpl_DivU32U16(tmpU32no1 + tmpU32no2, 50);
            noiseU32[i] <<= nShifts;
        }
        qNoise = q_domain_to_use;
    }

    if (inst->blockIndex < 200) {
        inst->timeAvgMagnEnergyTmp += inst->magnEnergy >>
            (2 * inst->normData + inst->stages - 1);
        inst->timeAvgMagnEnergy = TSpl_DivU32U16(
            inst->timeAvgMagnEnergyTmp,
            (uint16_t)(inst->blockIndex + 1));
    }

    satMax = 0xfffff;
    postShifts = 6 + qMagn - qNoise;
    nShifts = 5 - inst->prevQMagn + inst->prevQNoise;
    for (i = 0; i < inst->magnLen; i++) {
        postLocSnr[i] = 2048;
        tmpU32no1 = (uint32_t)magnU16[i] << 6;
        if (postShifts < 0)
            tmpU32no2 = noiseU32[i] >> -postShifts;
        else
            tmpU32no2 = noiseU32[i] << postShifts;
        if (tmpU32no1 > tmpU32no2) {
            if (tmpU32no2 > 0) {
                tmpU32no1 <<= 11;
                tmpU32no1 /= tmpU32no2 + 1;
                postLocSnr[i] = tmpU32no1 < satMax ?
                                tmpU32no1 : satMax;
            } else {
                postLocSnr[i] = satMax;
            }
        }
        nearMagnEst = (uint32_t)inst->prevMagnU16[i] *
                      inst->noiseSupFilter[i];
        tmpU32no1 = nearMagnEst << 3;
        tmpU32no2 = inst->prevNoiseU32[i] >> nShifts;
        if (tmpU32no2 > 0) {
            tmpU32no1 = (int32_t)tmpU32no1 / (int32_t)tmpU32no2;
            if (tmpU32no1 > satMax)
                tmpU32no1 = satMax;
        } else {
            tmpU32no1 = satMax;
        }
        prevNearSnr[i] = tmpU32no1;
        tmpU32no1 = prevNearSnr[i] * 2007;
        tmpU32no2 = (postLocSnr[i] - 2048) * 41;
        priorSnr = tmpU32no1 + tmpU32no2 + 512;
        priorLocSnr[i] = 2048 + (priorSnr >> 10);
    }

    TNRx_ComputeSpectralDifference(inst, magnU16);
    inst->cntThresUpdate++;
    flag = (int)(inst->cntThresUpdate == inst->modelUpdate);
    TNRx_FeatureParameterExtraction(inst, flag);
    if (flag) {
        inst->cntThresUpdate = 0;
        inst->curAvgMagnEnergy = inst->curAvgMagnEnergy >> 9;
        tmpU32no1 = (inst->curAvgMagnEnergy +
                     inst->timeAvgMagnEnergy + 1) >> 1;
        if (tmpU32no1 != inst->timeAvgMagnEnergy &&
            inst->featureSpecDiff) {
            norm32no1 = 0;
            tmpU32no3 = tmpU32no1;
            while (tmpU32no3 & 0xffff0000) {
                tmpU32no3 >>= 1;
                norm32no1++;
            }
            tmpU32no2 = inst->featureSpecDiff;
            while (tmpU32no2 & 0xffff0000) {
                tmpU32no2 >>= 1;
                norm32no1++;
            }
            tmpU32no3 *= tmpU32no2;
            tmpU32no3 /= inst->timeAvgMagnEnergy + 1;
            if (TSpl_NormU32(tmpU32no3) < norm32no1) {
                inst->featureSpecDiff = 0x7fffff;
            } else {
                tmpU32no3 <<= norm32no1;
                inst->featureSpecDiff = tmpU32no3 < 0x7fffff ?
                                        tmpU32no3 : 0x7fffff;
            }
        }
        inst->timeAvgMagnEnergy = tmpU32no1;
        inst->curAvgMagnEnergy = 0;
    }

    TNRx_SpeechNoiseProb(inst, nonSpeechProbFinal, priorLocSnr, postLocSnr);
    gammaNoise = 26;
    maxNoiseU32 = 0;
    postShifts = inst->prevQNoise - qMagn;
    nShifts = inst->prevQMagn - qMagn;
    for (i = 0; i < inst->magnLen; ++i) {
        if (postShifts < 0)
            tmpU32no2 = (uint32_t)magnU16[i] >> -postShifts;
        else
            tmpU32no2 = (uint32_t)magnU16[i] << postShifts;
        if (prevNoiseU16[i] > tmpU32no2) {
            sign = -1;
            tmpU32no1 = prevNoiseU16[i] - tmpU32no2;
        } else {
            sign = 1;
            tmpU32no1 = tmpU32no2 - prevNoiseU16[i];
        }
        noiseUpdateU32 = inst->prevNoiseU32[i];
        tmpU32no3 = 0;
        if (tmpU32no1 && nonSpeechProbFinal[i]) {
            tmpU32no3 = tmpU32no1 * nonSpeechProbFinal[i];
            if (tmpU32no3 & 0x7c000000)
                tmpU32no2 = (tmpU32no3 >> 5) * gammaNoise;
            else
                tmpU32no2 = (tmpU32no3 * gammaNoise) >> 5;
            if (sign > 0)
                noiseUpdateU32 += tmpU32no2;
            else
                noiseUpdateU32 -= tmpU32no2;
        }
        prevGammaNoise = gammaNoise;
        gammaNoise = 26;
        if (nonSpeechProbFinal[i] < 205)
            gammaNoise = 3;
        if (prevGammaNoise != gammaNoise) {
            if (tmpU32no3 & 0x7c000000)
                tmpU32no2 = (tmpU32no3 >> 5) * gammaNoise;
            else
                tmpU32no2 = (tmpU32no3 * gammaNoise) >> 5;
            if (sign > 0)
                tmpU32no1 = inst->prevNoiseU32[i] + tmpU32no2;
            else
                tmpU32no1 = inst->prevNoiseU32[i] - tmpU32no2;
            if (noiseUpdateU32 > tmpU32no1)
                noiseUpdateU32 = tmpU32no1;
        }
        noiseU32[i] = noiseUpdateU32;
        if (noiseUpdateU32 > maxNoiseU32)
            maxNoiseU32 = noiseUpdateU32;

        tmp32no2 = -nShifts >= 0 ?
                   inst->avgMagnPause[i] << -nShifts :
                   inst->avgMagnPause[i] >> nShifts;
        if (nonSpeechProbFinal[i] > 205) {
            if (nShifts < 0) {
                tmp32no1 = (int32_t)magnU16[i] - tmp32no2;
                tmp32no1 = tmp32no1 * 13;
                tmp32no1 = (tmp32no1 + 128) >> 8;
            } else {
                tmp32no1 = ((int32_t)magnU16[i] << nShifts) -
                             inst->avgMagnPause[i];
                tmp32no1 = tmp32no1 * 13;
                tmp32no1 = (tmp32no1 + (128 << nShifts)) >>
                           (8 + nShifts);
            }
            tmp32no2 += tmp32no1;
        }
        inst->avgMagnPause[i] = tmp32no2;
    }

    norm32no1 = TSpl_NormU32(maxNoiseU32);
    qNoise = inst->prevQNoise + norm32no1 - 5;
    nShifts = inst->prevQNoise + 11 - qMagn;
    for (i = 0; i < inst->magnLen; i++) {
        curNearSnr = 0;
        if (nShifts < 0) {
            tmpMagnU32 = magnU16[i];
            tmpNoiseU32 = noiseU32[i] << -nShifts;
        } else if (nShifts > 17) {
            tmpMagnU32 = (uint32_t)magnU16[i] << 17;
            tmpNoiseU32 = noiseU32[i] >> (nShifts - 17);
        } else {
            tmpMagnU32 = (uint32_t)magnU16[i] << nShifts;
            tmpNoiseU32 = noiseU32[i];
        }
        if (tmpMagnU32 > tmpNoiseU32) {
            tmpU32no1 = tmpMagnU32 - tmpNoiseU32;
            norm32no2 = 11 < TSpl_NormU32(tmpU32no1) ?
                        11 : TSpl_NormU32(tmpU32no1);
            tmpU32no1 <<= norm32no2;
            tmpU32no2 = tmpNoiseU32 >> (11 - norm32no2);
            tmpU32no1 /= tmpU32no2 + 1;
            curNearSnr = tmpU32no1 < satMax ? tmpU32no1 : satMax;
        }
        tmpU32no1 = prevNearSnr[i] * 2007;
        tmpU32no2 = curNearSnr * 41;
        priorSnr = tmpU32no1 + tmpU32no2;
        tmpU32no1 = inst->overdrive + ((priorSnr + 8192) >> 14);
        tmpU16no1 = (uint16_t)((priorSnr + (tmpU32no1 >> 1)) /
                                (tmpU32no1 + 1));
        inst->noiseSupFilter[i] = (uint16_t)(
            tmpU16no1 > 16384 ? 16384 :
            tmpU16no1 < inst->denoiseBound ?
            inst->denoiseBound : tmpU16no1);
        if (inst->blockIndex < 50) {
            tmpU32no1 = inst->noiseSupFilter[i] *
                         (uint16_t)inst->blockIndex;
            tmpU32no2 = noiseSupFilterTmp[i] *
                         (uint16_t)(50 - inst->blockIndex);
            tmpU32no1 += tmpU32no2;
            inst->noiseSupFilter[i] =
                (uint16_t)TSpl_DivU32U16(tmpU32no1, 50);
        }
    }

    if (inst->Dereverb_switch)
        De_Late_reverb_x((int16_t *)magnU16, &inst->pTDPar, qMagn);
    inst->prevQNoise = qNoise;
    inst->prevQMagn = qMagn;
    if (norm32no1 > 5) {
        for (i = 0; i < inst->magnLen; i++) {
            inst->prevNoiseU32[i] = noiseU32[i] << (norm32no1 - 5);
            inst->prevMagnU16[i] = magnU16[i];
        }
    } else {
        for (i = 0; i < inst->magnLen; i++) {
            inst->prevNoiseU32[i] = noiseU32[i] >> (5 - norm32no1);
            inst->prevMagnU16[i] = magnU16[i];
        }
    }

    TNRx_DataSynthesis(inst, out_frame, tns_mode);
    if (inst->fs == 32000) {
        memcpy(inst->dataBufHBFX,
               inst->dataBufHBFX + inst->blockLen10ms,
               (size_t)(inst->anaLen - inst->blockLen10ms) *
               sizeof(*inst->dataBufHBFX));
        memcpy(inst->dataBufHBFX + inst->anaLen - inst->blockLen10ms,
               speech_frame_hb,
               (size_t)inst->blockLen10ms * sizeof(*speech_frame_hb));
        tmpU32no1 = 0;
        tmpU16no1 = 0;
        for (i = inst->anaLen2 - (inst->anaLen2 >> 2);
             i < inst->anaLen2; i++) {
            tmpU16no1 += nonSpeechProbFinal[i];
            tmpU32no1 += inst->noiseSupFilter[i];
        }
        avgProbSpeechHB = (int16_t)(4096 -
            (tmpU16no1 >> (inst->stages - 7)));
        avgFilterGainHB = (int16_t)(tmpU32no1 >> (inst->stages - 3));
        gainModHB = avgProbSpeechHB < 3607 ? avgProbSpeechHB : 3607;
        if (avgProbSpeechHB < 2048)
            gainTimeDomainHB = (int16_t)((gainModHB << 1) +
                                         (avgFilterGainHB >> 1));
        else
            gainTimeDomainHB = (int16_t)(((3 * avgFilterGainHB) >> 2) +
                                         gainModHB);
        gainTimeDomainHB = gainTimeDomainHB > 16384 ? 16384 :
                           gainTimeDomainHB < (int16_t)inst->denoiseBound ?
                           (int16_t)inst->denoiseBound : gainTimeDomainHB;
        for (i = 0; i < inst->blockLen10ms; i++)
            out_frame_hb[i] = (int16_t)((gainTimeDomainHB *
                                         inst->dataBufHBFX[i]) >> 14);
    } else if (inst->fs == 48000) {
        memcpy(inst->dataBufHBFX,
               inst->dataBufHBFX + inst->blockLen10ms,
               (size_t)(inst->anaLen - inst->blockLen10ms) *
               sizeof(*inst->dataBufHBFX));
        memcpy(inst->dataBufHBFX1,
               inst->dataBufHBFX1 + inst->blockLen10ms,
               (size_t)(inst->anaLen - inst->blockLen10ms) *
               sizeof(*inst->dataBufHBFX1));
        for (i = 0; i < inst->blockLen10ms; i++) {
            inst->dataBufHBFX[inst->anaLen - inst->blockLen10ms + i] =
                speech_frame_hb[2 * i];
            inst->dataBufHBFX1[inst->anaLen - inst->blockLen10ms + i] =
                speech_frame_hb[2 * i + 1];
        }
        tmpU32no1 = 0;
        tmpU16no1 = 0;
        for (i = inst->anaLen2 - (inst->anaLen2 >> 2);
             i < inst->anaLen2; i++) {
            tmpU16no1 += nonSpeechProbFinal[i];
            tmpU32no1 += inst->noiseSupFilter[i];
        }
        avgProbSpeechHB = (int16_t)(4096 -
            (tmpU16no1 >> (inst->stages - 7)));
        avgFilterGainHB = (int16_t)(tmpU32no1 >> (inst->stages - 3));
        gainModHB = avgProbSpeechHB < 3607 ? avgProbSpeechHB : 3607;
        if (avgProbSpeechHB < 2048)
            gainTimeDomainHB = (int16_t)((gainModHB << 1) +
                                         (avgFilterGainHB >> 1));
        else
            gainTimeDomainHB = (int16_t)(((3 * avgFilterGainHB) >> 2) +
                                         gainModHB);
        gainTimeDomainHB = gainTimeDomainHB > 16384 ? 16384 :
                           gainTimeDomainHB < (int16_t)inst->denoiseBound ?
                           (int16_t)inst->denoiseBound : gainTimeDomainHB;
        for (i = 0; i < inst->blockLen10ms; i++) {
            out_frame_hb[2 * i] = (int16_t)((gainTimeDomainHB *
                                             inst->dataBufHBFX[i]) >> 14);
            out_frame_hb[2 * i + 1] = (int16_t)((gainTimeDomainHB *
                                                 inst->dataBufHBFX1[i]) >> 14);
        }
    }
    return 0;
}
