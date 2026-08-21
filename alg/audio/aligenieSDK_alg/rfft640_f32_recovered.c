#include "aec_frontend_recovered.h"
#include <math.h>

#define _L0 (-0x1.41b2f769cf0ep-6)
#define _LC1 0x1.8p+1
#define _LC2 0x1p+2
#define _LC3 0x1.4p+2
#define _LC4 0x1.4p+4
#define _LC5 0x1p-1
#define _LC6 0x1.3c6ef4p-2f
#define _LC7 (-0x1.9e377ap-1f)
#define _LC8 0x1.e6f0e2p-1f
#define _LC9 0x1.2cf23p-1f
#define _LC10 (-0x1.e6f0e2p-1f)
#define _LC11 (-0x1.2cf23p-1f)
#define _LC12 0x1p-1f
#define _LC13 0x1.99999ap-10f
#define _LC14 0x1.99999ap-9f
#define _LC15 (-0x1.99999ap-9f)
#if defined(__riscv) && defined(__GNUC__)
#define AEC_NO_REORDER __attribute__((no_reorder))
#else
#define AEC_NO_REORDER
#endif

static float cfft320_r4_tw3[48] AEC_NO_REORDER;
static float cfft320_r4_tw2[96] AEC_NO_REORDER;
static float rfft_split_tab[318] AEC_NO_REORDER;
static float cfft320_r5_tw1[512] AEC_NO_REORDER;

/* fft_stage_DFT4_s4 @ ram:00100f38 */

static void fft_stage_DFT4_s4(float *y, const float *x)
{
  const CpxFloat_T *px0 = (const CpxFloat_T *)x;
  const CpxFloat_T *px1 = px0 + 80;
  const CpxFloat_T *px2 = px1 + 80;
  const CpxFloat_T *px3 = px2 + 80;
  CpxFloat_T *py0 = (CpxFloat_T *)y;
  CpxFloat_T *py1 = py0 + 80;
  CpxFloat_T *py2 = py1 + 80;
  CpxFloat_T *py3 = py2 + 80;
  int i;

  for (i = 0; i < 80; i += 2) {
    CpxFloat_T x0, x1, x2, x3;
    CpxFloat_T x4, x5, x6, x7;

    x0 = *px0++;
    x1 = *px1++;
    x2 = *px2++;
    x3 = *px3++;
    x4 = *px0++;
    x5 = *px1++;
    x6 = *px2++;
    x7 = *px3++;

    {
      CpxFloat_T s0, s1, d0, d1;
      s0.r = x0.r + x2.r;
      s0.i = x0.i + x2.i;
      d0.r = x0.r - x2.r;
      d0.i = x0.i - x2.i;
      s1.r = x1.r + x3.r;
      s1.i = x1.i + x3.i;
      d1.r = x1.r - x3.r;
      d1.i = x1.i - x3.i;
      x0.r = s0.r + s1.r;
      x0.i = s0.i + s1.i;
      x2.r = s0.r - s1.r;
      x2.i = s0.i - s1.i;
      x3.r = d0.r - d1.i;
      x3.i = d0.i + d1.r;
      x1.r = d0.r + d1.i;
      x1.i = d0.i - d1.r;
    }

    {
      CpxFloat_T s0, s1, d0, d1;
      s0.r = x4.r + x6.r;
      s0.i = x4.i + x6.i;
      d0.r = x4.r - x6.r;
      d0.i = x4.i - x6.i;
      s1.r = x5.r + x7.r;
      s1.i = x5.i + x7.i;
      d1.r = x5.r - x7.r;
      d1.i = x5.i - x7.i;
      x4.r = s0.r + s1.r;
      x4.i = s0.i + s1.i;
      x6.r = s0.r - s1.r;
      x6.i = s0.i - s1.i;
      x7.r = d0.r - d1.i;
      x7.i = d0.i + d1.r;
      x5.r = d0.r + d1.i;
      x5.i = d0.i - d1.r;
    }

    *py0++ = x0;
    *py1++ = x1;
    *py2++ = x2;
    *py3++ = x3;
    *py0++ = x4;
    *py1++ = x5;
    *py2++ = x6;
    *py3++ = x7;
  }
}


/* fft_stage_DFT4x2_s3 @ ram:00101046 */

static void fft_stage_DFT4x2_s3(float *y, const float *x, const float *tw)
{
  const CpxFloat_T *px0;
  const CpxFloat_T *px1;
  CpxFloat_T *py0;
  CpxFloat_T *py1;
  const CpxFloat_T *ptwd;
  int i, j;

  for (i = 0; i < 20; i += 2) {
    CpxFloat_T x0, x1, x2, x3, t;
    CpxFloat_T tw1, tw2, tw3;
    ptwd = (const CpxFloat_T *)tw;
    px0 = (const CpxFloat_T *)x + i;
    py0 = (CpxFloat_T *)y + i;
    px1 = (const CpxFloat_T *)x + i + 1;
    py1 = (CpxFloat_T *)y + i + 1;
    for (j = 0; j < 4; j++) {
      tw1 = *ptwd++;
      tw2 = *ptwd++;
      tw3 = *ptwd++;
      x0 = px0[0];
      x1 = px0[80];
      x2 = px0[160];
      x3 = px0[240];
      px0 += 20;
      {
        CpxFloat_T s0, s1, d0, d1;
        s0.r = x0.r + x2.r;
        s0.i = x0.i + x2.i;
        d0.r = x0.r - x2.r;
        d0.i = x0.i - x2.i;
        s1.r = x1.r + x3.r;
        s1.i = x1.i + x3.i;
        d1.r = x1.r - x3.r;
        d1.i = x1.i - x3.i;
        x0.r = s0.r + s1.r;
        x0.i = s0.i + s1.i;
        x2.r = s0.r - s1.r;
        x2.i = s0.i - s1.i;
        x3.r = d0.r - d1.i;
        x3.i = d0.i + d1.r;
        x1.r = d0.r + d1.i;
        x1.i = d0.i - d1.r;
      }
      t.r = tw1.r * x1.r - tw1.i * x1.i;
      t.i = tw1.i * x1.r + tw1.r * x1.i;
      x1 = t;
      t.r = tw2.r * x2.r - tw2.i * x2.i;
      t.i = tw2.i * x2.r + tw2.r * x2.i;
      x2 = t;
      t.r = tw3.r * x3.r - tw3.i * x3.i;
      t.i = tw3.i * x3.r + tw3.r * x3.i;
      x3 = t;
      py0[0] = x0;
      py0[20] = x1;
      py0[40] = x2;
      py0[60] = x3;
      py0 += 80;
      x0 = px1[0];
      x1 = px1[80];
      x2 = px1[160];
      x3 = px1[240];
      px1 += 20;
      {
        CpxFloat_T s0, s1, d0, d1;
        s0.r = x0.r + x2.r;
        s0.i = x0.i + x2.i;
        d0.r = x0.r - x2.r;
        d0.i = x0.i - x2.i;
        s1.r = x1.r + x3.r;
        s1.i = x1.i + x3.i;
        d1.r = x1.r - x3.r;
        d1.i = x1.i - x3.i;
        x0.r = s0.r + s1.r;
        x0.i = s0.i + s1.i;
        x2.r = s0.r - s1.r;
        x2.i = s0.i - s1.i;
        x3.r = d0.r - d1.i;
        x3.i = d0.i + d1.r;
        x1.r = d0.r + d1.i;
        x1.i = d0.i - d1.r;
      }
      t.r = tw1.r * x1.r - tw1.i * x1.i;
      t.i = tw1.i * x1.r + tw1.r * x1.i;
      x1 = t;
      t.r = tw2.r * x2.r - tw2.i * x2.i;
      t.i = tw2.i * x2.r + tw2.r * x2.i;
      x2 = t;
      t.r = tw3.r * x3.r - tw3.i * x3.i;
      t.i = tw3.i * x3.r + tw3.r * x3.i;
      x3 = t;
      py1[0] = x0;
      py1[20] = x1;
      py1[40] = x2;
      py1[60] = x3;
      py1 += 80;
    }
  }
}


/* fft_stage_DFT4_s2 @ ram:0010122a */

static void fft_stage_DFT4_s2(float *y, const float *x, const float *tw)
{
  const CpxFloat_T *ptwd;
  CpxFloat_T *px0;
  CpxFloat_T *py0;
  int i, j;

  for (i = 0; i < 5; i++) {
    CpxFloat_T x0, x1, x2, x3, t;
    CpxFloat_T tw1, tw2, tw3;
    ptwd = (const CpxFloat_T *)tw;
    px0 = (CpxFloat_T *)x + i;
    py0 = (CpxFloat_T *)y + i;
    for (j = 0; j < 16; j++) {
      tw1 = *ptwd++;
      tw2 = *ptwd++;
      tw3 = *ptwd++;
      x0 = px0[0];
      x1 = px0[80];
      x2 = px0[160];
      x3 = px0[240];
      px0 += 5;
      {
        CpxFloat_T s0, s1, d0, d1;
        s0.r = x0.r + x2.r;
        s0.i = x0.i + x2.i;
        d0.r = x0.r - x2.r;
        d0.i = x0.i - x2.i;
        s1.r = x1.r + x3.r;
        s1.i = x1.i + x3.i;
        d1.r = x1.r - x3.r;
        d1.i = x1.i - x3.i;
        x0.r = s0.r + s1.r;
        x0.i = s0.i + s1.i;
        x2.r = s0.r - s1.r;
        x2.i = s0.i - s1.i;
        x3.r = d0.r - d1.i;
        x3.i = d0.i + d1.r;
        x1.r = d0.r + d1.i;
        x1.i = d0.i - d1.r;
      }
      t.i = tw1.i * x1.r + tw1.r * x1.i;
      t.r = tw1.r * x1.r - tw1.i * x1.i;
      x1 = t;
      t.r = tw2.r * x2.r - tw2.i * x2.i;
      t.i = tw2.i * x2.r + tw2.r * x2.i;
      x2 = t;
      t.r = tw3.r * x3.r - tw3.i * x3.i;
      t.i = tw3.i * x3.r + tw3.r * x3.i;
      x3 = t;
      py0[0] = x0;
      py0[5] = x1;
      py0[10] = x2;
      py0[15] = x3;
      py0 += 20;
    }
  }
}


/* FeOpt_RFFT640_GenTab_f32 @ ram:00101342 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

void FeOpt_RFFT640_GenTab_f32(void)
{
  const double phase = _L0;
  int i;

  for (i = 0; i < 64; i++) {
    cfft320_r5_tw1[8 * i] = (float)cos(i * phase);
    cfft320_r5_tw1[8 * i + 1] = (float)sin(i * phase);
    cfft320_r5_tw1[8 * i + 2] = (float)cos((i * phase) * 2.0);
    cfft320_r5_tw1[8 * i + 3] = (float)sin((i * phase) * 2.0);
    cfft320_r5_tw1[8 * i + 4] = (float)cos((i * phase) * 3.0);
    cfft320_r5_tw1[8 * i + 5] = (float)sin((i * phase) * 3.0);
    cfft320_r5_tw1[8 * i + 6] = (float)cos((i * phase) * 4.0);
    cfft320_r5_tw1[8 * i + 7] = (float)sin((i * phase) * 4.0);
  }

  for (i = 0; i < 16; i++) {
    cfft320_r4_tw2[6 * i] = (float)cos(i * phase * 5.0);
    cfft320_r4_tw2[6 * i + 1] = (float)sin(i * phase * 5.0);
    cfft320_r4_tw2[6 * i + 2] = (float)cos((i * phase * 5.0) * 2.0);
    cfft320_r4_tw2[6 * i + 3] = (float)sin((i * phase * 5.0) * 2.0);
    cfft320_r4_tw2[6 * i + 4] = (float)cos((i * phase * 5.0) * 3.0);
    cfft320_r4_tw2[6 * i + 5] = (float)sin((i * phase * 5.0) * 3.0);
  }

  for (i = 0; i < 4; i++) {
    cfft320_r4_tw3[6 * i] = (float)cos(i * phase * 20.0);
    cfft320_r4_tw3[6 * i + 1] = (float)sin(i * phase * 20.0);
    cfft320_r4_tw3[6 * i + 2] = (float)cos((i * phase * 20.0) * 2.0);
    cfft320_r4_tw3[6 * i + 3] = (float)sin((i * phase * 20.0) * 2.0);
    cfft320_r4_tw3[6 * i + 4] = (float)cos((i * phase * 20.0) * 3.0);
    cfft320_r4_tw3[6 * i + 5] = (float)sin((i * phase * 20.0) * 3.0);
  }

  for (i = 1; i < 160; i++) {
    rfft_split_tab[2 * (i - 1)] = (float)cos(i * phase * 0.5);
    rfft_split_tab[2 * (i - 1) + 1] = (float)sin(i * phase * 0.5);
  }
}


static void rfft_split(CpxFloat_T *pData, CpxFloat_T *pCpxSplit)
{
  CpxFloat_T cpxTmp;
  int i;

  cpxTmp = pData[0];
  pData[0].r = cpxTmp.r + cpxTmp.i;
  pData[0].i = cpxTmp.r - cpxTmp.i;
  for (i = 1; i < 160; i++) {
    CpxFloat_T cpxFpnk, cpxFpk, cpxF1k, cpxF2k;
    cpxFpk = pData[i];
    cpxFpnk = pData[320 - i];
    cpxF1k.r = cpxFpk.r + cpxFpnk.r;
    cpxF1k.i = cpxFpk.i + cpxFpnk.i;
    cpxF2k.r = cpxFpk.r - cpxFpnk.r;
    cpxF2k.i = cpxFpk.i - cpxFpnk.i;
    cpxTmp.r = cpxF2k.r * pCpxSplit[i - 1].i +
               cpxF1k.i * pCpxSplit[i - 1].r;
    cpxTmp.i = cpxF1k.i * pCpxSplit[i - 1].i -
               cpxF2k.r * pCpxSplit[i - 1].r;
    pData[i].r = (cpxF1k.r + cpxTmp.r) * 0.5f;
    pData[i].i = (cpxF2k.i + cpxTmp.i) * 0.5f;
    pData[320 - i].r = (cpxF1k.r - cpxTmp.r) * 0.5f;
    pData[320 - i].i = (cpxTmp.i - cpxF2k.i) * 0.5f;
  }
  pData[160].i = -pData[160].i;
}

static void rifft_merge(CpxFloat_T *pData, CpxFloat_T *pCpxSplit)
{
  CpxFloat_T cpxTmp;
  int i;

  cpxTmp.r = pData[0].r;
  cpxTmp.i = pData[0].i;
  pData[0].r = (cpxTmp.r + cpxTmp.i) * 0x1.99999ap-10f;
  pData[0].i = (cpxTmp.r - cpxTmp.i) * 0x1.99999ap-10f;
  for (i = 1; i < 160; i++) {
    CpxFloat_T cpxFpnk, cpxFpk, cpxF1k, cpxF2k;
    cpxFpk = pData[i];
    cpxFpnk = pData[320 - i];
    cpxF1k.r = cpxFpk.r + cpxFpnk.r;
    cpxF1k.i = cpxFpk.i + cpxFpnk.i;
    cpxF2k.r = cpxFpk.r - cpxFpnk.r;
    cpxF2k.i = cpxFpk.i - cpxFpnk.i;
    cpxTmp.r = cpxF1k.i * pCpxSplit[i - 1].r -
               cpxF2k.r * pCpxSplit[i - 1].i;
    cpxTmp.i = cpxF2k.r * pCpxSplit[i - 1].r +
               cpxF1k.i * pCpxSplit[i - 1].i;
    pData[i].r = (cpxF1k.r - cpxTmp.r) * 0x1.99999ap-10f;
    pData[i].i = (cpxF2k.i + cpxTmp.i) * 0x1.99999ap-10f;
    pData[320 - i].r = (cpxF1k.r + cpxTmp.r) * 0x1.99999ap-10f;
    pData[320 - i].i = (cpxTmp.i - cpxF2k.i) * 0x1.99999ap-10f;
  }
  pData[160].r *= _LC14;
  pData[160].i *= _LC15;
}

static void fft_stage_DFT5_s1(float *y, const float *x, const float *tw)
{
  const CpxFloat_T *px0 = (const CpxFloat_T *)x;
  const CpxFloat_T *px1 = px0 + 64;
  const CpxFloat_T *px2 = px1 + 64;
  const CpxFloat_T *px3 = px2 + 64;
  const CpxFloat_T *px4 = px3 + 64;
  CpxFloat_T *py0 = (CpxFloat_T *)y;
  const CpxFloat_T *ptwd = (const CpxFloat_T *)tw;
  const float real_w1 = _LC6;
  const float jimag_w1 = _LC10;
  const float real_w2 = _LC7;
  const float jimag_w2 = _LC11;
  int i;

  for (i = 0; i < 64; i++) {
    CpxFloat_T x0, x1, x2, x3, x4, t;
    CpxFloat_T tw1, tw2, tw3, tw4;
    CpxFloat_T s1, s2, d1, d2;
    CpxFloat_T y0, y1, y2;
    CpxFloat_T t0, t1, t2, t3;

    tw1 = *ptwd++;
    tw2 = *ptwd++;
    tw3 = *ptwd++;
    tw4 = *ptwd++;
    x0 = *px0++;
    x1 = *px1++;
    x2 = *px2++;
    x3 = *px3++;
    x4 = *px4++;

    s1.r = x1.r + x4.r;
    s1.i = x1.i + x4.i;
    d1.r = x1.r - x4.r;
    d1.i = x1.i - x4.i;
    s2.r = x2.r + x3.r;
    s2.i = x2.i + x3.i;
    d2.r = x2.r - x3.r;
    d2.i = x2.i - x3.i;

    y0.r = x0.r + s1.r + s2.r;
    y0.i = x0.i + s1.i + s2.i;
    y1.r = s1.r * real_w1 + x0.r + s2.r * real_w2;
    y1.i = s1.i * real_w1 + x0.i + s2.i * real_w2;
    y2.r = s1.r * real_w2 + x0.r + s2.r * real_w1;
    y2.i = s1.i * real_w2 + x0.i + s2.i * real_w1;

    t0.r = -jimag_w1 * d1.i;
    t0.i = jimag_w1 * d1.r;
    t1.r = -jimag_w2 * d2.i;
    t1.i = jimag_w2 * d2.r;
    t2.r = -jimag_w1 * d2.i;
    t2.i = jimag_w1 * d2.r;
    t3.r = -jimag_w2 * d1.i;
    t3.i = jimag_w2 * d1.r;
    t0.r += t1.r;
    t0.i += t1.i;
    t3.r -= t2.r;
    t3.i -= t2.i;

    x1.r = y1.r + t0.r;
    x1.i = y1.i + t0.i;
    x4.r = y1.r - t0.r;
    x4.i = y1.i - t0.i;
    x2.r = y2.r + t3.r;
    x2.i = y2.i + t3.i;
    x3.r = y2.r - t3.r;
    x3.i = y2.i - t3.i;

    t.r = tw1.r * x1.r - tw1.i * x1.i;
    t.i = tw1.i * x1.r + tw1.r * x1.i;
    x1 = t;
    t.r = tw2.r * x2.r - tw2.i * x2.i;
    t.i = tw2.i * x2.r + tw2.r * x2.i;
    x2 = t;
    t.r = tw3.r * x3.r - tw3.i * x3.i;
    t.i = tw3.i * x3.r + tw3.r * x3.i;
    x3 = t;
    t.r = tw4.r * x4.r - tw4.i * x4.i;
    t.i = tw4.i * x4.r + tw4.r * x4.i;
    x4 = t;

    *py0++ = y0;
    *py0++ = x1;
    *py0++ = x2;
    *py0++ = x3;
    *py0++ = x4;
  }
}

static void ifft_stage_DFT5_s1(float *y, const float *x, const float *tw)
{
  const CpxFloat_T *ptwd = (const CpxFloat_T *)tw;
  const CpxFloat_T *px0 = (const CpxFloat_T *)x;
  CpxFloat_T *py0 = (CpxFloat_T *)y;
  const float real_w1 = _LC6;
  const float jimag_w1 = _LC10;
  const float real_w2 = _LC7;
  const float jimag_w2 = _LC11;
  int i;

#define IFFT5_STEP(v0, v1, v2, v3, v4) do {                              \
    CpxFloat_T x0, x1, x2, x3, x4, t;                                  \
    CpxFloat_T tw1, tw2, tw3, tw4;                                     \
    CpxFloat_T s1, s2, d1, d2;                                         \
    CpxFloat_T y0, y1, y2;                                              \
    CpxFloat_T t0, t1, t2, t3;                                         \
    tw1 = *ptwd++;                                                       \
    tw2 = *ptwd++;                                                       \
    tw3 = *ptwd++;                                                       \
    tw4 = *ptwd++;                                                       \
    x0 = (v0);                                                           \
    x1 = (v1);                                                           \
    x2 = (v2);                                                           \
    x3 = (v3);                                                           \
    x4 = (v4);                                                           \
    s1.r = x1.r + x4.r;                                                 \
    s1.i = x1.i + x4.i;                                                 \
    d1.r = x4.r - x1.r;                                                 \
    d1.i = x4.i - x1.i;                                                 \
    s2.r = x2.r + x3.r;                                                 \
    s2.i = x2.i + x3.i;                                                 \
    d2.r = x3.r - x2.r;                                                 \
    d2.i = x3.i - x2.i;                                                 \
    y0.r = x0.r + s1.r + s2.r;                                         \
    y0.i = x0.i + s1.i + s2.i;                                         \
    y1.r = s1.r * real_w1 + x0.r + s2.r * real_w2;                     \
    y1.i = s1.i * real_w1 + x0.i + s2.i * real_w2;                     \
    y2.r = s1.r * real_w2 + x0.r + s2.r * real_w1;                     \
    y2.i = s1.i * real_w2 + x0.i + s2.i * real_w1;                     \
    t0.r = -jimag_w1 * d1.i;                                           \
    t0.i = jimag_w1 * d1.r;                                            \
    t1.r = -jimag_w2 * d2.i;                                           \
    t1.i = jimag_w2 * d2.r;                                            \
    t2.r = -jimag_w1 * d2.i;                                           \
    t2.i = jimag_w1 * d2.r;                                            \
    t3.r = -jimag_w2 * d1.i;                                           \
    t3.i = jimag_w2 * d1.r;                                            \
    t0.r += t1.r;                                                       \
    t0.i += t1.i;                                                       \
    t3.r -= t2.r;                                                       \
    t3.i -= t2.i;                                                       \
    x1.r = y1.r + t0.r;                                                 \
    x1.i = y1.i + t0.i;                                                 \
    x4.r = y1.r - t0.r;                                                 \
    x4.i = y1.i - t0.i;                                                 \
    x2.r = y2.r + t3.r;                                                 \
    x2.i = y2.i + t3.i;                                                 \
    x3.r = y2.r - t3.r;                                                 \
    x3.i = y2.i - t3.i;                                                 \
    t.r = tw1.r * x1.r - tw1.i * x1.i;                                 \
    t.i = tw1.i * x1.r + tw1.r * x1.i;                                 \
    x1 = t;                                                              \
    t.r = tw2.r * x2.r - tw2.i * x2.i;                                 \
    t.i = tw2.i * x2.r + tw2.r * x2.i;                                 \
    x2 = t;                                                              \
    t.r = tw3.r * x3.r - tw3.i * x3.i;                                 \
    t.i = tw3.i * x3.r + tw3.r * x3.i;                                 \
    x3 = t;                                                              \
    t.r = tw4.r * x4.r - tw4.i * x4.i;                                 \
    t.i = tw4.i * x4.r + tw4.r * x4.i;                                 \
    x4 = t;                                                              \
    *py0++ = y0;                                                         \
    *py0++ = x1;                                                         \
    *py0++ = x2;                                                         \
    *py0++ = x3;                                                         \
    *py0++ = x4;                                                         \
  } while (0)

  IFFT5_STEP(px0[0], px0[64], px0[128], px0[192], px0[256]);
  px0 += 319;
  for (i = 1; i < 64; i++) {
    IFFT5_STEP(px0[0], px0[-256], px0[-192], px0[-128], px0[-64]);
    --px0;
  }
#undef IFFT5_STEP
}

void FeOpt_RFFT640_f32(float *pDst, float *pSrc)
{
  fft_stage_DFT5_s1(pDst, pSrc, cfft320_r5_tw1);
  fft_stage_DFT4_s2(pSrc, pDst, cfft320_r4_tw2);
  fft_stage_DFT4x2_s3(pDst, pSrc, cfft320_r4_tw3);
  fft_stage_DFT4_s4(pDst, pDst);
  rfft_split((CpxFloat_T *)pDst, (CpxFloat_T *)rfft_split_tab);
}

void FeOpt_RIFFT640_f32(float *pDst, float *pSrc)
{
  rifft_merge((CpxFloat_T *)pSrc, (CpxFloat_T *)rfft_split_tab);
  ifft_stage_DFT5_s1(pDst, pSrc, cfft320_r5_tw1);
  fft_stage_DFT4_s2(pSrc, pDst, cfft320_r4_tw2);
  fft_stage_DFT4x2_s3(pDst, pSrc, cfft320_r4_tw3);
  fft_stage_DFT4_s4(pDst, pDst);
}
