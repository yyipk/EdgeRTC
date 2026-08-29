#ifndef NS_C_RECOVERED_H
#define NS_C_RECOVERED_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    NS_ALG_BYPASS = 0,
    NS_ALG_RES_BASIC = 1,
    NS_ALG_RES_PILOT = 2,
    NS_ALG_RES_NNMASK = 100,
    NS_ALG_RES_NNRES = 101,
    NS_ALG_RNNOISE = 200
} NS_ALGORITHM;

typedef struct {
    NS_ALGORITHM ns_algorithm;
    int numchs;
    int numrefs;
    int numbins;
    int ns_power;
    float ns_level;
    const char *ns_model_base;
    int refdelay;
    int echodelay;
    void *extra;
} NSParams;

typedef struct {
    uint8_t reserved0[20];
    int nummics;
    int numrefs;
    uint8_t reserved1[1140];
    NS_ALGORITHM ns_algorithm;
    int ns_power;
    float ns_level;
    const char *ns_model_base;
} Params;

typedef struct {
    int init_flag;
    int spec_size;
    uint32_t frame_number;
    int T60;
    int16_t R;
    int16_t Ne;
    int16_t Decay_rate;
    int Decay;
    int16_t kapa;
    int16_t TDalpha;
    int16_t gain_min;
    int16_t Omega;
    int16_t *hanning;
    int32_t *Y2;
    int32_t *lambda_reverb;
    int32_t *lambda_reverb_old;
    int32_t *lambda_reverb_save;
    int32_t *lambda_reverb_late;
    int32_t *lambda_Z_old;
    int32_t *lambda_Z;
    int32_t *buffer;
    int32_t *offset;
    int16_t *tmp_gain;
    int16_t *TDereverb_Gain;
    int16_t *TDereverb_gain_old;
    int16_t *Qvalue;
    int16_t qmagn_previous;
} TDereverb_x_Param;

typedef struct {
    uint32_t fs;
    const int16_t *window;
    int16_t analysisBuffer[256];
    int16_t synthesisBuffer[256];
    uint16_t noiseSupFilter[129];
    uint16_t overdrive;
    uint16_t denoiseBound;
    const int16_t *factor2Table;
    int16_t noiseEstLogQuantile[387];
    int16_t noiseEstDensity[387];
    int16_t noiseEstCounter[3];
    int16_t noiseEstQuantile[129];
    int16_t anaLen;
    int anaLen2;
    int magnLen;
    int aggrMode;
    int stages;
    int initFlag;
    int gainMap;
    int32_t maxLrt;
    int32_t minLrt;
    int32_t logLrtTimeAvgW32[129];
    int32_t featureLogLrt;
    int32_t thresholdLogLrt;
    int16_t weightLogLrt;
    uint32_t featureSpecDiff;
    uint32_t thresholdSpecDiff;
    int16_t weightSpecDiff;
    uint32_t featureSpecFlat;
    uint32_t thresholdSpecFlat;
    int16_t weightSpecFlat;
    int32_t avgMagnPause[129];
    uint32_t magnEnergy;
    uint32_t sumMagn;
    uint32_t curAvgMagnEnergy;
    uint32_t timeAvgMagnEnergy;
    uint32_t timeAvgMagnEnergyTmp;
    uint32_t whiteNoiseLevel;
    uint32_t initMagnEst[129];
    int32_t pinkNoiseNumerator;
    int32_t pinkNoiseExp;
    int minNorm;
    int zeroInputSignal;
    uint32_t prevNoiseU32[129];
    uint16_t prevMagnU16[129];
    int16_t priorNonSpeechProb;
    int blockIndex;
    int modelUpdate;
    int cntThresUpdate;
    int16_t histLrt[1000];
    int16_t histSpecFlat[1000];
    int16_t histSpecDiff[1000];
    int16_t dataBufHBFX[256];
    int16_t dataBufHBFX1[256];
    int qNoise;
    int prevQNoise;
    int prevQMagn;
    int blockLen10ms;
    int16_t real[256];
    int16_t imag[256];
    int32_t energyIn;
    int scaleEnergyIn;
    int normData;
    TDereverb_x_Param pTDPar;
    int Dereverb_switch;
} __attribute__((aligned(8))) NsxInst_t;

typedef NsxInst_t *NsxHandle;

typedef enum {
    RET_OK = 0,
    RET_NOT_SUPPORTED = -100,
    RET_NOT_INITIALIZED = -99,
    RET_LICNENCE_CHECK_FAILED = -98,
    RET_ILLEGAL_PARAMETER = -97,
    RET_ILLEGAL_DATA = -96,
    RET_INSUFFICIENT_MEMORY = -95,
    RET_DIVIDE_BY_ZERO = -94
} RET_CODE;

typedef struct {
    NsxHandle ns_st;
    int16_t frame_size;
} NS3Para;

typedef struct AGCPara_ AGCPara;

typedef struct {
    unsigned int agc_algorithm;
    int numchs;
    int blocksize;
    int agc_power;
    int agc_level;
} AGCParams;

typedef struct {
    void *handler;
    RET_CODE (*init)(const AGCParams *params, void **handler);
    void (*free)(void *handler);
    int (*numChannels)(void *handler);
    int (*blockSize)(void *handler);
    int (*delayFrames)(void *handler);
    void (*process)(void *handler, int16_t *pData);
} AGC;

typedef struct {
    int numchs;
    int blocksize;
    NS3Para **ns;
    AGCPara **agc;
    int16_t **buffer;
    int bufinidx;
    int bufoutidx;
} NSAGCWrapper;

_Static_assert(sizeof(NSParams) == 48, "NSParams ABI");
_Static_assert(offsetof(Params, ns_algorithm) == 1168, "Params ABI");
_Static_assert(sizeof(TDereverb_x_Param) == 160, "TDereverb_x_Param ABI");
_Static_assert(offsetof(TDereverb_x_Param, TDereverb_Gain) == 128,
               "TDereverb_x_Param ABI");
_Static_assert(sizeof(NsxInst_t) == 13816, "NsxInst_t ABI");
_Static_assert(offsetof(NsxInst_t, initFlag) == 3144, "NsxInst_t ABI");
_Static_assert(offsetof(NsxInst_t, pTDPar) == 13648, "NsxInst_t ABI");
_Static_assert(offsetof(NsxInst_t, Dereverb_switch) == 13808,
               "NsxInst_t ABI");
_Static_assert(sizeof(NS3Para) == 16, "NS3Para ABI");

void NS_defaultParams(const Params *params, NSParams *specparams);

int TNRx_InitCore(NsxHandle inst, uint32_t fs);
int TNRx_set_policy_core(NsxHandle inst, int mode);
int TNRx_set_dereverb_core(NsxHandle inst, int mode);
void TNRx_UpdateNoiseEstimate(NsxHandle inst, int offset);
void TNRx_CalcParametricNoiseEstimate(NsxHandle inst,
                                      int16_t pink_noise_exp_avg,
                                      int32_t pink_noise_num_avg,
                                      int freq_index,
                                      uint32_t *noise_estimate,
                                      uint32_t *noise_estimate_avg);
void TNRx_NoiseEstimation(NsxHandle inst, uint16_t *magn, uint32_t *noise,
                          int16_t *q_noise);
void TNRx_FeatureParameterExtraction(NsxHandle inst, int flag);
void TNRx_ComputeSpectralFlatness(NsxHandle inst, uint16_t *magn);
void TNRx_ComputeSpectralDifference(NsxHandle inst, uint16_t *magn);
void TNRx_SpeechNoiseProb(NsxHandle inst, uint16_t *non_speech_prob,
                          uint32_t *prior_snr, uint32_t *post_snr);
void TNRx_DataAnalysis(NsxHandle inst, int16_t *speech_frame, uint16_t *magn);
void TNRx_DataSynthesis(NsxHandle inst, int16_t *out_frame, int tns_mode);
int TNRx_ProcessCore(NsxHandle inst, int16_t *speechFrame,
                     int16_t *speechFrameHB, int16_t *outFrame,
                     int16_t *outFrameHB, int tns_mode);
void Delete_TDereverb_x_Params(TDereverb_x_Param *params);
int Init_TDereverb_x_Params(TDereverb_x_Param *params, int ana_len,
                            uint32_t fs);
void Reset_TDereverb_x_Params(TDereverb_x_Param *params);
void win_smooth_x(int16_t *input, int16_t *output,
                  int16_t *win, int spec_size, int win_len);
void De_Late_reverb_x(int16_t *input, TDereverb_x_Param *pTDPar,
                      int16_t qMagn);

extern const int16_t TSpl_kSinTable1024[1024];

int16_t TSpl_AddSatW16(int16_t left, int16_t right);
void TSpl_ComplexBitReverse(int16_t *frfi, int stages);
int TSpl_ComplexFFT(int16_t *frfi, int stages, int mode);
int TSpl_ComplexIFFT(int16_t *frfi, int stages, int mode);
uint32_t TSpl_DivU32U16(uint32_t numerator, uint16_t denominator);
int32_t TSpl_DivW32W16(int32_t numerator, int16_t denominator);
int16_t TSpl_DivW32W16ResW16(int32_t numerator, int16_t denominator);
int32_t TSpl_Energy(int16_t *vector, int length, int *scale_factor);
int TSpl_GetScalingSquare(const int16_t *vector, int length, int times);
int16_t TSpl_GetSizeInBits(uint32_t value);
int16_t TSpl_MaxAbsValueW16(const int16_t *vector, int16_t length);
int32_t TSpl_MaxAbsValueW32(const int32_t *vector, int16_t length);
int16_t TSpl_MaxValueW16(const int16_t *vector, int16_t length);
int TSpl_NormU32(uint32_t value);
int TSpl_NormW16(int16_t value);
int TSpl_NormW32(int32_t value);
int32_t TSpl_Sqrt(int32_t value);
int32_t TSpl_SqrtLocal(int32_t value);

int32_t spx_exp2(int16_t x);
int32_t spx_exp(int16_t x);

int TNRx_Create(NsxHandle *nsxInst);
int TNRx_Free(NsxHandle nsxInst);
int TNRx_Init(NsxHandle nsxInst, uint32_t fs);
int TNRx_set_policy(NsxHandle nsxInst, int mode);
int TNRx_set_dereverb(NsxHandle nsxInst, int mode);
int TNRx_Process(NsxHandle nsxInst, int16_t *speechFrame,
                 int16_t *speechFrameHB, int16_t *outFrame,
                 int16_t *outFrameHB, int tns_mode);

NS3Para *NS3_Init(int SampleRateHz, int FramePointNum,
                  RET_CODE *ReturnCode);
int NS3_Process(NS3Para *st, int16_t *NS3In, int16_t *NS3InH,
                int16_t *NS3Out, int16_t *NS3OutH, int NS3Mode);
int NS3_Exit(NS3Para *st);
RET_CODE NS3_SetPara(NS3Para *st, const char *ParaName,
                     const char *ParaVal);

AGCPara *AGC_Init(int SampleRateHz, int FramePointNum, int NumChs,
                  RET_CODE *ReturnCode);
int AGC_Process(AGCPara *st, int16_t *AGCIn, int16_t *AGCInH,
                int16_t *AGCOut, int16_t *AGCOutH, int AGCMode);
int AGC_Exit(AGCPara *st);
RET_CODE AGC_SetPara(AGCPara *st, const char *ParaName,
                     const char *ParaVal);

RET_CODE NSAGCWrapper_init(const AGCParams *params, void **handler);
void NSAGCWrapper_free(void *handler);
int NSAGCWrapper_numChannels(void *handler);
int NSAGCWrapper_blockSize(void *handler);
int NSAGCWrapper_delayFrames(void *handler);
void NSAGCWrapper_process(void *handler, int16_t *pData);
void NSAGCWrapper_mount(void *handler, AGC *alg);

#endif
