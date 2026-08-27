/* Recovered from libmit_rtos.a tdereverb_x.o. */
#include "ns_c_recovered.h"

#include <stdlib.h>
#include <string.h>

void Reset_TDereverb_x_Params(TDereverb_x_Param *pTDPar)
{
    pTDPar->init_flag = 2;
    pTDPar->frame_number = 0;
}

void win_smooth_x(int16_t *input, int16_t *output,
                  int16_t *win, int spec_size, int win_len)
{
    int16_t half_len = (int16_t)(win_len / 2);
    int16_t i;
    int16_t j;
    int32_t tmp1;
    int32_t tmp2;
    int32_t win_sum;

    for (i = half_len; i < spec_size - half_len; ++i) {
        tmp1 = 0;
        for (j = 0; j < win_len; ++j) {
            tmp1 += (((input[i - half_len + j] & 0x7fff) * win[j]) >> 15) +
                    ((input[i - half_len + j] >> 15) * win[j]);
        }
        output[i] = (int16_t)tmp1;
    }

    for (i = 0; i < half_len; ++i) {
        tmp1 = 0;
        tmp2 = 0;
        win_sum = 0;
        for (j = half_len - i; j < win_len; ++j) {
            tmp1 += (((input[i + j - half_len] & 0x7fff) * win[j]) >> 15) +
                    ((input[i + j - half_len] >> 15) * win[j]);
            tmp2 += (((input[spec_size - 1 - i - j] & 0x7fff) * win[j]) >> 15) +
                    ((input[spec_size - 1 - i - j] >> 15) * win[j]);
            win_sum += win[j];
        }
        output[i] = (int16_t)((tmp1 << 15) / win_sum);
        output[spec_size - i - 1] = (int16_t)((tmp2 << 15) / win_sum);
    }
}

int Init_TDereverb_x_Params(TDereverb_x_Param *pTDPar, int spec_size,
                            uint32_t sample_rate)
{
    static const int16_t hanning[9] = {
        626, 2264, 4289, 5928, 6553, 5928, 4289, 2264, 626
    };
    int history_size;

    pTDPar->init_flag = 2;
    pTDPar->spec_size = spec_size;
    pTDPar->frame_number = 0;
    pTDPar->T60 = 0x6000;
    if (sample_rate == 16000)
        pTDPar->R = 160;
    else if (sample_rate == 8000)
        pTDPar->R = 80;
    pTDPar->Ne = 5;
    if (sample_rate == 16000)
        pTDPar->Decay_rate = 9;
    else if (sample_rate == 8000)
        pTDPar->Decay_rate = 19;
    pTDPar->Decay = spx_exp((int16_t)((-2 * pTDPar->Decay_rate *
                                        pTDPar->R) >> 4));
    pTDPar->kapa = 1;
    pTDPar->TDalpha = 16384;
    pTDPar->gain_min = 3277;
    pTDPar->Omega = 4;

    pTDPar->Y2 = calloc((size_t)pTDPar->spec_size, sizeof(*pTDPar->Y2));
    if (!pTDPar->Y2)
        return 0;
    pTDPar->lambda_reverb = calloc((size_t)pTDPar->spec_size,
                                   sizeof(*pTDPar->lambda_reverb));
    if (!pTDPar->lambda_reverb)
        return 0;
    pTDPar->lambda_reverb_old = calloc((size_t)pTDPar->spec_size,
                                       sizeof(*pTDPar->lambda_reverb_old));
    if (!pTDPar->lambda_reverb_old)
        return 0;
    pTDPar->lambda_reverb_save = calloc((size_t)pTDPar->spec_size,
                                        sizeof(*pTDPar->lambda_reverb_save));
    if (!pTDPar->lambda_reverb_save)
        return 0;
    pTDPar->lambda_reverb_late = calloc((size_t)pTDPar->spec_size,
                                        sizeof(*pTDPar->lambda_reverb_late));
    if (!pTDPar->lambda_reverb_late)
        return 0;
    pTDPar->lambda_Z = calloc((size_t)pTDPar->spec_size,
                               sizeof(*pTDPar->lambda_Z));
    if (!pTDPar->lambda_Z)
        return 0;
    pTDPar->lambda_Z_old = calloc((size_t)pTDPar->spec_size,
                                  sizeof(*pTDPar->lambda_Z_old));
    if (!pTDPar->lambda_Z_old)
        return 0;
    history_size = pTDPar->Ne - 1;
    pTDPar->buffer = calloc(history_size * pTDPar->spec_size,
                            sizeof(*pTDPar->buffer));
    if (!pTDPar->buffer)
        return 0;
    pTDPar->Qvalue = calloc((size_t)history_size, sizeof(*pTDPar->Qvalue));
    if (!pTDPar->Qvalue)
        return 0;
    pTDPar->TDereverb_Gain = calloc((size_t)pTDPar->spec_size,
                                    sizeof(*pTDPar->TDereverb_Gain));
    if (!pTDPar->TDereverb_Gain)
        return 0;
    pTDPar->tmp_gain = calloc((size_t)pTDPar->spec_size,
                               sizeof(*pTDPar->tmp_gain));
    if (!pTDPar->tmp_gain)
        return 0;
    pTDPar->TDereverb_gain_old = calloc((size_t)pTDPar->spec_size,
                                        sizeof(*pTDPar->TDereverb_gain_old));
    if (!pTDPar->TDereverb_gain_old)
        return 0;
    pTDPar->hanning = calloc(9, sizeof(*pTDPar->hanning));
    if (!pTDPar->hanning)
        return 0;

    memset(pTDPar->TDereverb_gain_old, 0x7fff,
           (size_t)pTDPar->spec_size * sizeof(*pTDPar->TDereverb_gain_old));
    pTDPar->offset = pTDPar->buffer;
    pTDPar->qmagn_previous = 0;
    pTDPar->hanning[0] = hanning[0];
    pTDPar->hanning[1] = hanning[1];
    pTDPar->hanning[2] = hanning[2];
    pTDPar->hanning[3] = hanning[3];
    pTDPar->hanning[4] = hanning[4];
    pTDPar->hanning[5] = hanning[5];
    pTDPar->hanning[6] = hanning[6];
    pTDPar->hanning[7] = hanning[7];
    pTDPar->hanning[8] = hanning[8];
    return 1;
}

void Delete_TDereverb_x_Params(TDereverb_x_Param *pTDPar)
{
    free(pTDPar->Y2);
    free(pTDPar->lambda_reverb);
    free(pTDPar->lambda_reverb_old);
    free(pTDPar->lambda_reverb_save);
    free(pTDPar->lambda_reverb_late);
    free(pTDPar->lambda_Z);
    free(pTDPar->lambda_Z_old);
    free(pTDPar->buffer);
    free(pTDPar->Qvalue);
    free(pTDPar->TDereverb_Gain);
    free(pTDPar->tmp_gain);
    free(pTDPar->TDereverb_gain_old);
    free(pTDPar->hanning);
}

void De_Late_reverb_x(int16_t *input, TDereverb_x_Param *pTDPar,
                      int16_t qMagn)
{
    int i;
    int32_t tmp1;
    int16_t q_value;

    for (i = 0; i < pTDPar->spec_size; ++i)
        pTDPar->Y2[i] = (int32_t)input[i] * input[i];

    if (pTDPar->init_flag == 2) {
        pTDPar->init_flag = 1;
        pTDPar->frame_number = 1;
        memcpy(pTDPar->lambda_reverb, pTDPar->Y2,
               (size_t)pTDPar->spec_size * sizeof(*pTDPar->Y2));
        memcpy(pTDPar->lambda_reverb_late, pTDPar->Y2,
               (size_t)pTDPar->spec_size * sizeof(*pTDPar->Y2));
        memcpy(pTDPar->lambda_Z, pTDPar->Y2,
               (size_t)pTDPar->spec_size * sizeof(*pTDPar->Y2));
        memcpy(pTDPar->lambda_Z_old, pTDPar->Y2,
               (size_t)pTDPar->spec_size * sizeof(*pTDPar->Y2));
        memcpy(pTDPar->lambda_reverb_old, pTDPar->Y2,
               (size_t)pTDPar->spec_size * sizeof(*pTDPar->Y2));
        memcpy(pTDPar->lambda_reverb_save, pTDPar->Y2,
               (size_t)pTDPar->spec_size * sizeof(*pTDPar->Y2));
    } else {
        ++pTDPar->frame_number;
        for (i = 0; i < pTDPar->spec_size; ++i) {
            pTDPar->lambda_Z[i] = pTDPar->Y2[i];
            tmp1 = ((int32_t)pTDPar->kapa * pTDPar->Decay) >> 10;
            tmp1 = (tmp1 * pTDPar->lambda_Z_old[i]) >> 6;
            q_value = (int16_t)(qMagn - pTDPar->qmagn_previous);
            if (q_value < 0)
                tmp1 >>= -2 * q_value;
            else
                tmp1 <<= 2 * q_value;
            pTDPar->lambda_reverb[i] = tmp1;
            pTDPar->lambda_Z_old[i] = pTDPar->lambda_Z[i];
        }

        if (pTDPar->frame_number > (uint32_t)pTDPar->Ne) {
            memcpy(pTDPar->lambda_reverb_save, pTDPar->buffer,
                   (size_t)pTDPar->spec_size *
                   sizeof(*pTDPar->lambda_reverb_save));
            pTDPar->offset = pTDPar->buffer + pTDPar->spec_size;
            q_value = (int16_t)(qMagn - pTDPar->Qvalue[0]);
            for (i = 0; i < pTDPar->spec_size; ++i) {
                tmp1 = (int32_t)pTDPar->Decay_rate * pTDPar->R;
                tmp1 *= pTDPar->Ne - 1;
                tmp1 = spx_exp((int16_t)((-2 * tmp1) >> 4)) >> 10;
                pTDPar->lambda_reverb_late[i] =
                    (tmp1 * pTDPar->lambda_reverb_save[i]) >> 6;
            }
            memmove(pTDPar->buffer, pTDPar->offset,
                    (size_t)(pTDPar->Ne - 2) * pTDPar->spec_size *
                    sizeof(*pTDPar->buffer));
            pTDPar->offset = pTDPar->buffer +
                              (pTDPar->Ne - 2) * pTDPar->spec_size;
            memcpy(pTDPar->offset, pTDPar->lambda_reverb,
                   (size_t)pTDPar->spec_size * sizeof(*pTDPar->Y2));
            for (i = 0; i < pTDPar->Ne - 2; ++i)
                pTDPar->Qvalue[i] = pTDPar->Qvalue[i + 1];
            pTDPar->Qvalue[pTDPar->Ne - 2] = qMagn;
            for (i = 0; i < pTDPar->spec_size; ++i) {
                if (q_value < 0)
                    pTDPar->lambda_reverb_late[i] >>= -2 * q_value;
                else
                    pTDPar->lambda_reverb_late[i] <<= 2 * q_value;
            }
        } else {
            for (i = 0; i < pTDPar->spec_size; ++i) {
                tmp1 = (int32_t)pTDPar->Decay_rate * pTDPar->R;
                tmp1 *= pTDPar->Ne - 1;
                tmp1 = spx_exp((int16_t)((-2 * tmp1) >> 4)) >> 10;
                pTDPar->lambda_reverb_late[i] =
                    (tmp1 * pTDPar->lambda_reverb_old[i]) >> 6;
            }
            memcpy(pTDPar->offset, pTDPar->lambda_reverb,
                   (size_t)pTDPar->spec_size *
                   sizeof(*pTDPar->lambda_reverb));
            pTDPar->offset = pTDPar->buffer +
                              (pTDPar->frame_number - 2) *
                              pTDPar->spec_size;
            pTDPar->Qvalue[pTDPar->frame_number - 2] = qMagn;
        }
    }

    for (i = 0; i < pTDPar->spec_size; ++i) {
        if (pTDPar->lambda_reverb_late[i] < pTDPar->lambda_Z[i]) {
            if (pTDPar->lambda_reverb_late[i] < 0) {
                tmp1 = pTDPar->gain_min;
            } else {
                tmp1 = ((pTDPar->lambda_reverb_late[i] << 8) /
                        (pTDPar->lambda_Z[i] + 1)) << 6;
                tmp1 = 32767 - (TSpl_Sqrt(tmp1) << 8);
            }
        } else {
            tmp1 = pTDPar->gain_min;
        }
        pTDPar->tmp_gain[i] = (int16_t)tmp1;
        if (pTDPar->tmp_gain[i] < pTDPar->gain_min)
            pTDPar->tmp_gain[i] = pTDPar->gain_min;
        pTDPar->TDereverb_Gain[i] = pTDPar->tmp_gain[i];
        pTDPar->TDereverb_Gain[i] =
            (int16_t)(((32767 - pTDPar->TDalpha) *
                       pTDPar->TDereverb_Gain[i] +
                       pTDPar->TDalpha * pTDPar->TDereverb_gain_old[i]) >>
                      15);
        pTDPar->tmp_gain[i] = pTDPar->TDereverb_Gain[i];
    }

    pTDPar->qmagn_previous = qMagn;
    win_smooth_x(pTDPar->tmp_gain, pTDPar->TDereverb_Gain,
                 pTDPar->hanning, pTDPar->spec_size,
                 2 * pTDPar->Omega + 1);
    memcpy(pTDPar->TDereverb_gain_old, pTDPar->TDereverb_Gain,
           (size_t)pTDPar->spec_size *
           sizeof(*pTDPar->TDereverb_gain_old));
}
