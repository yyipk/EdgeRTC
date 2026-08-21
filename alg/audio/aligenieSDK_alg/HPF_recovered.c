#include "aec_frontend_recovered.h"
#include <stdlib.h>

void CLog_error(const char *, const char *, ...);
static const char kFileTag[] = "soundconnect, HPF";

static float HPF50[8] = {
    0x1.f60b78p-1f, -0x1.78889ap+1f, 0x1.78889ap+1f, -0x1.f60b78p-1f,
    0x1p+0f, -0x1.7af93ap+1f, 0x1.760b98p+1f, -0x1.ec487ep-1f
};
static float HPF75[8] = {
    0x1.f123c6p-1f, -0x1.74dad4p+1f, 0x1.74dad4p+1f, -0x1.f123c6p-1f,
    0x1p+0f, -0x1.7875ep+1f, 0x1.71242ep+1f, -0x1.e2b5f4p-1f
};
static float HPF100[8] = {
    0x1.ec484p-1f, -0x1.71363p+1f, 0x1.71363p+1f, -0x1.ec484p-1f,
    0x1p+0f, -0x1.75f292p+1f, 0x1.6c4934p+1f, -0x1.d952e6p-1f
};
static float HPF125[8] = {
    0x1.e778c4p-1f, -0x1.6d9a92p+1f, 0x1.6d9a92p+1f, -0x1.e778c4p-1f,
    0x1p+0f, -0x1.736f54p+1f, 0x1.677a9cp+1f, -0x1.d01e58p-1f
};
static float HPF150[8] = {
    0x1.e2b52ap-1f, -0x1.6a07ep+1f, 0x1.6a07ep+1f, -0x1.e2b52ap-1f,
    0x1p+0f, -0x1.70ec2ap+1f, 0x1.62b854p+1f, -0x1.c7175ap-1f
};
static float HPF175[8] = {
    0x1.ddfd5p-1f, -0x1.667dfcp+1f, 0x1.667dfcp+1f, -0x1.ddfd5p-1f,
    0x1p+0f, -0x1.6e6918p+1f, 0x1.5e0248p+1f, -0x1.be3cfap-1f
};
static float HPF200[8] = {
    0x1.d9511p-1f, -0x1.62fcccp+1f, 0x1.62fcccp+1f, -0x1.d9511p-1f,
    0x1p+0f, -0x1.6be622p+1f, 0x1.595868p+1f, -0x1.b58e52p-1f
};
static float HPF225[8] = {
    0x1.d4b046p-1f, -0x1.5f8436p+1f, 0x1.5f8436p+1f, -0x1.d4b046p-1f,
    0x1p+0f, -0x1.69634ap+1f, 0x1.54baa4p+1f, -0x1.ad0a7ep-1f
};
static float HPF250[8] = {
    0x1.d01ad2p-1f, -0x1.5c141ep+1f, 0x1.5c141ep+1f, -0x1.d01ad2p-1f,
    0x1p+0f, -0x1.66e094p+1f, 0x1.5028e8p+1f, -0x1.a4b09ep-1f
};
static float HPF275[8] = {
    0x1.cb909p-1f, -0x1.58ac6cp+1f, 0x1.58ac6cp+1f, -0x1.cb909p-1f,
    0x1p+0f, -0x1.645e06p+1f, 0x1.4ba324p+1f, -0x1.9c7fdcp-1f
};

RET_CODE HPF_init(int numchs, int blocksize, int hpfpower, HPF **this1)
{
    HPF *pp = malloc(sizeof(*pp));
    if (!pp)
        return RET_INSUFFICIENT_MEMORY;

    pp->b = NULL;
    pp->a = NULL;
    pp->bufx = NULL;
    pp->bufy = NULL;
    pp->numchs = numchs;
    pp->blocksize = blocksize;

    switch (hpfpower) {
    case 0: break;
    case 1: pp->b = HPF50;  pp->a = HPF50 + 5;  break;
    case 2: pp->b = HPF75;  pp->a = HPF75 + 5;  break;
    case 3: pp->b = HPF100; pp->a = HPF100 + 5; break;
    case 4: pp->b = HPF125; pp->a = HPF125 + 5; break;
    case 5: pp->b = HPF150; pp->a = HPF150 + 5; break;
    case 6: pp->b = HPF175; pp->a = HPF175 + 5; break;
    case 7: pp->b = HPF200; pp->a = HPF200 + 5; break;
    case 8: pp->b = HPF225; pp->a = HPF225 + 5; break;
    case 9: pp->b = HPF250; pp->a = HPF250 + 5; break;
    case 10: pp->b = HPF275; pp->a = HPF275 + 5; break;
    default:
        CLog_error(kFileTag, "Unsupported hpf_power: %d\n", hpfpower);
        return RET_ILLEGAL_PARAMETER;
    }

    pp->bufx = calloc(1, 4 * numchs * sizeof(*pp->bufx));
    if (!pp->bufx)
        return RET_INSUFFICIENT_MEMORY;
    pp->bufy = calloc(1, 3 * numchs * sizeof(*pp->bufy));
    if (!pp->bufy)
        return RET_INSUFFICIENT_MEMORY;
    *this1 = pp;
    return RET_OK;
}

void HPF_free(HPF *this1)
{
    free(this1->bufx);
    free(this1->bufy);
    free(this1);
}

void HPF_process(HPF *this1, short *datain, float *dataout)
{
    float *b = this1->b;
    float *a = this1->a;
    float *x = this1->bufx;
    float *y = this1->bufy;
    int numChs = this1->numchs;
    int blkSize = this1->blocksize;
    int m, t;

    if (!b) {
        for (m = 0; m < numChs; ++m)
            for (t = 0; t < blkSize; ++t)
                dataout[m * blkSize + t] =
                    (float)datain[m * blkSize + t] * 0x1p-15f;
    } else {
        for (m = 0; m < numChs; ++m) {
            float *pTmpX = x + 4 * m;
            float *pTmpY = y + 3 * m;
            for (t = 0; t < blkSize; ++t) {
                float outVal;
                pTmpX[0] = (float)*datain++ * 0x1p-15f;
                outVal = b[0] * pTmpX[0] + b[1] * pTmpX[1] +
                         b[2] * pTmpX[2] + b[3] * pTmpX[3] -
                         a[0] * pTmpY[0] - a[1] * pTmpY[1] -
                         a[2] * pTmpY[2];
                pTmpX[3] = pTmpX[2];
                pTmpX[2] = pTmpX[1];
                pTmpX[1] = pTmpX[0];
                pTmpY[2] = pTmpY[1];
                pTmpY[1] = pTmpY[0];
                pTmpY[0] = outVal;
                *dataout++ = outVal;
            }
        }
    }
}
