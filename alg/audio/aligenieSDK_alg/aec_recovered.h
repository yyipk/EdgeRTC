#ifndef AEC_RECOVERED_H
#define AEC_RECOVERED_H

#include <stddef.h>

typedef enum {
    AEC_ALG_BYPASS = 0, AEC_ALG_BASIC = 100, AEC_ALG_INST = 101,
    AEC_ALG_RLS_V3 = 103, AEC_ALG_LX_GENERAL = 201,
    AEC_ALG_LX_LOW_COMPLEXITY = 202, AEC_ALG_DRAEC = 301,
    AEC_ALG_STEREO_AEC_BSS_INST = 303, AEC_ALG_KALMAN = 400,
    AEC_ALG_KALMAN_DRAEC = 401
} AEC_ALGORITHM;

typedef struct {
    AEC_ALGORITHM aec_algorithm;
    int nummics, numrefs, numbins, aec_power, aec_power2;
    float aec_level, aec_forget, aec_gain;
} AECParams;

typedef struct { float r, i; } CpxFloat_T;
#if defined(__riscv) && defined(__riscv_zfh)
#define AEC_USE_RISCV_FP16 1
typedef __fp16 FP16IfEn;
typedef FP16IfEn FP16IfEn_T;
typedef struct { __fp16 r, i; } CpxFp16_T;
static inline float CpxFp16_value(__fp16 h) { return (float)h; }
static inline __fp16 CpxFp16_from_float(float f) { return (__fp16)f; }
#define AEC_FP16_VALUE(value) ((float)(value))
#define AEC_FP16_FROM_FLOAT(value) ((FP16IfEn_T)(value))
#else
#include <stdint.h>
#include <string.h>

#define AEC_USE_RISCV_FP16 0
typedef uint16_t FP16IfEn;
typedef FP16IfEn FP16IfEn_T;
typedef struct { uint16_t r, i; } CpxFp16_T;

static inline float CpxFp16_value(uint16_t h)
{
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1fu;
    uint32_t fraction = h & 0x03ffu;
    uint32_t bits;
    float value;

    if (exponent == 0) {
        if (fraction == 0) {
            bits = sign;
        } else {
            exponent = 113;
            while ((fraction & 0x0400u) == 0) {
                fraction <<= 1;
                --exponent;
            }
            bits = sign | (exponent << 23) |
                   ((fraction & 0x03ffu) << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (fraction << 13);
    } else {
        bits = sign | ((exponent + 112) << 23) | (fraction << 13);
    }

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline uint16_t CpxFp16_from_float(float value)
{
    uint32_t bits;
    uint32_t sign;
    uint32_t exponent;
    uint32_t fraction;
    uint32_t result;

    memcpy(&bits, &value, sizeof(bits));
    sign = (bits >> 16) & 0x8000u;
    exponent = (bits >> 23) & 0xffu;
    fraction = bits & 0x007fffffu;

    if (exponent == 0xffu) {
        if (fraction == 0)
            return (uint16_t)(sign | 0x7c00u);
        fraction >>= 13;
        return (uint16_t)(sign | 0x7c00u | fraction | (fraction == 0));
    }

    if (exponent > 142)
        return (uint16_t)(sign | 0x7c00u);

    if (exponent < 113) {
        uint32_t shift;
        uint32_t remainder;
        uint32_t halfway;

        if (exponent < 103)
            return (uint16_t)sign;
        fraction |= 0x00800000u;
        shift = 126u - exponent;
        result = fraction >> shift;
        remainder = fraction & ((1u << shift) - 1u);
        halfway = 1u << (shift - 1u);
        if (remainder > halfway ||
            (remainder == halfway && (result & 1u)))
            ++result;
        return (uint16_t)(sign | result);
    }

    result = sign | ((exponent - 112u) << 10) | (fraction >> 13);
    fraction &= 0x1fffu;
    if (fraction > 0x1000u ||
        (fraction == 0x1000u && (result & 1u)))
        ++result;
    return (uint16_t)result;
}
#define AEC_FP16_VALUE(value) CpxFp16_value(value)
#define AEC_FP16_FROM_FLOAT(value) CpxFp16_from_float(value)
#endif

typedef struct DRAEC DRAEC;
struct DRAEC {
    int nummics, numrefs, numbins;
    float gain_amp, sqrt1nforget, isqrtforget;
    int mtaps, rtaps, fioffset, filen, randseed;
    int micPos, refPos, drDelay;
    CpxFp16_T *pInBuf;
    CpxFloat_T *pDrAecPath;
    FP16IfEn_T *pIu;
    float *pTmpRandn;
    CpxFloat_T *pTmpDlUpd, *pTmpVa, *pTmpVu;
};

typedef struct {
    void *handler;
    int (*init)(const AECParams *, DRAEC **);
    void (*free)(void *);
    int (*numMics)(void *);
    int (*numRefs)(void *);
    int (*numBins)(void *);
    int (*delayBlocks)(void *);
    void (*process)(void *, float *);
    void (*update)(void *, float *);
} AEC;

float Util_randn(int *seed);
float Util_db2pow(float db);
void NlsFe_DrVaInit(CpxFloat_T *, const FP16IfEn_T *, const CpxFloat_T *, int, float);
float NlsFe_DrGivRot(FP16IfEn_T *, const CpxFloat_T *, CpxFloat_T *,
                     const int, const float);

void AEC_defaultParams(const void *, AECParams *);
int DRAEC_init(const AECParams *, DRAEC **);
void DRAEC_free(void *);
int DRAEC_numMics(void *);
int DRAEC_numRefs(void *);
int DRAEC_numBins(void *);
int DRAEC_delayBlocks(void *);
int DRAEC_getTmpMemSize(void *);
void DRAEC_setTmpMem(void *, void *);
void DRAEC_process(void *, float *);
void DRAEC_update(void *, float *);
void DRAEC_mount(void *, AEC *);

void NlsFe_DrAecFilter(CpxFp16_T *, const int, const int, CpxFloat_T *,
                       const int, const int, const int, CpxFloat_T *,
                       const int, const int, const int, const float);
void NlsFe_DrAecSetNoiseBuf(CpxFp16_T *, const int, const int, const int,
                            const int, const int, CpxFloat_T *, const int,
                            const int, const float *);
void NlsFe_DrAecUpdatePath(CpxFloat_T *, const CpxFloat_T *,
                           const CpxFloat_T *, int, int, int, float, float);
float NlsFe_DrAecCalcSqrt(CpxFp16_T *, CpxFloat_T *, int, int, float);

#endif
