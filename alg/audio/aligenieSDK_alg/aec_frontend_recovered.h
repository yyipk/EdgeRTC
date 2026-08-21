#ifndef AEC_FRONTEND_RECOVERED_H
#define AEC_FRONTEND_RECOVERED_H

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

typedef struct HPF_ {
    int numchs;
    int blocksize;
    float *b;
    float *a;
    float *bufx;
    float *bufy;
} HPF;

typedef enum {
    BLOCK_DEFAULT = 0,
    BLOCK_10MS = 100,
    BLOCK_10MS_LD = 101,
    BLOCK_16MS = 160,
    BLOCK_16MS_LD = 161,
    BLOCK_20MS = 200,
    BLOCK_20MS_LD = 201,
    BLOCK_40MS = 400
} BLOCK_TYPE;

typedef enum {
    WINDOW_PPFA_PROTO_160_960 = 0,
    WINDOW_PPFA_PROTO_TIANBIAO_160_960 = 1,
    WINDOW_PPFA_PROTO_320_1280 = 2,
    WINDOW_PPFA_PROTO_320_1920 = 3,
    WINDOW_PPFA_PROTO_320_1920_DELAY960 = 4,
    WINDOW_PPFA_PROTO_480_2880 = 5,
    WINDOW_PPFA_PROTO_640_3840 = 6,
    WINDOW_PPFA_PROTO_256_1536 = 7,
    WINDOW_PPFA_PROTO_256_1536_DELAY768 = 8,
    WINDOW_PPFA_PROTO_512_3072 = 9,
    WINDOW_PPFA_PROTO_512_3072_DELAY1536 = 10
} PPFA_WINDOW_TYPE;

#if defined(__riscv) && defined(__riscv_zfh)
typedef __fp16 FP16IfEn;
#else
typedef float FP16IfEn;
#endif

typedef struct {
    float preEmphBk;
    int anaBufPos;
    FP16IfEn *pAnaBuf;
} PreObj_T;

typedef struct {
    float r;
    float i;
} CpxFloat_T;

typedef struct PPFBankAnalyzer_ {
    int numchs;
    int blocksize;
    int numbins;
    int numprotos;
    PreObj_T *pPreObj;
    float *pTmpFFTBuf;
} PPFBankAnalyzer;

typedef struct {
    int synBufPos;
    FP16IfEn *pSynBuf;
    float deEmphBk;
} PostObj_T;

typedef struct PPFBankSynthesizer_ {
    int numchs;
    int blocksize;
    int numbins;
    int numprotos;
    PostObj_T *pPostObj;
    float *pTmpFFTBuf;
} PPFBankSynthesizer;

RET_CODE HPF_init(int numchs, int blocksize, int hpfpower, HPF **this1);
void HPF_free(HPF *this1);
void HPF_process(HPF *this1, short *datain, float *dataout);

RET_CODE PPFBankAnalyzer_init(int numchs, PPFA_WINDOW_TYPE wtype,
                              PPFBankAnalyzer **this1);
int PPFBankAnalyzer_getTmpMemSize(PPFBankAnalyzer *this1);
void PPFBankAnalyzer_setTmpMem(PPFBankAnalyzer *this1, void *pTmpMem);
void PPFBankAnalyzer_free(PPFBankAnalyzer *this1);
int PPFBankAnalyzer_numChannels(PPFBankAnalyzer *this1);
int PPFBankAnalyzer_blockSize(PPFBankAnalyzer *this1);
int PPFBankAnalyzer_numBins(PPFBankAnalyzer *this1);
int PPFBankAnalyzer_f2i(PPFBankAnalyzer *this1, float fs, float f);
float PPFBankAnalyzer_i2f(PPFBankAnalyzer *this1, float fs, int fi);
void PPFBankAnalyzer_process(PPFBankAnalyzer *this1, float *input,
                             float *output);
RET_CODE PPFBankAnalyzer_block2WindowType16k(BLOCK_TYPE btype,
                                              PPFA_WINDOW_TYPE *wtype);

RET_CODE PPFBankSynthesizer_init(int numchs, PPFA_WINDOW_TYPE wtype,
                                 PPFBankSynthesizer **this1);
int PPFBankSynthesizer_getTmpMemSize(PPFBankSynthesizer *this1);
void PPFBankSynthesizer_setTmpMem(PPFBankSynthesizer *this1, void *pTmpMem);
void PPFBankSynthesizer_free(PPFBankSynthesizer *this1);
int PPFBankSynthesizer_numChannels(PPFBankSynthesizer *this1);
int PPFBankSynthesizer_blockSize(PPFBankSynthesizer *this1);
int PPFBankSynthesizer_numBins(PPFBankSynthesizer *this1);
int PPFBankSynthesizer_delay(PPFBankSynthesizer *this1);
void PPFBankSynthesizer_process(PPFBankSynthesizer *this1, float *input,
                                short *output);

void FeOpt_PreEmp(FP16IfEn *pDst, float *pSrc, float *pBkVal,
                  float coefVal, int len);
void FeOpt_DeEmp(short *pDst, FP16IfEn *pSrc, float *pBkVal,
                 float coefVal, int len);
void FeOpt_AddWinMacRev(FP16IfEn *pDst, float *pSrc,
                        const float *pWin, int len);
void FeOpt_AddWinMulRev(FP16IfEn *pDst, float *pSrc,
                        const float *pWin, int len);
void FeOpt_RFFT640_GenTab_f32(void);
void FeOpt_RFFT640_f32(float *pDst, float *pSrc);
void FeOpt_RIFFT640_f32(float *pDst, float *pSrc);

#endif
