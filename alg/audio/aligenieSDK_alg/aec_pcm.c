#include "aec_pcm.h"
#include "aec_frontend_recovered.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int aec_algorithm;
    int nummics;
    int numrefs;
    int numbins;
    int aec_power;
    int aec_power2;
    float aec_level;
    float aec_forget;
    float aec_gain;
} CoreAecParams;

int DRAEC_init(const CoreAecParams *params, void **handler);
void DRAEC_free(void *handler);
int DRAEC_getTmpMemSize(void *handler);
void DRAEC_setTmpMem(void *handler, void *mem);
void DRAEC_process(void *handler, float *data);
void DRAEC_update(void *handler, float *data);

#if defined(__GNUC__) || defined(__clang__)
#define AEC_PCM_WEAK __attribute__((weak))
#else
#define AEC_PCM_WEAK
#endif

AEC_PCM_WEAK float Util_round(float value)
{
    return value < 0.0f ? ceilf(value - 0.5f) : floorf(value + 0.5f);
}

AEC_PCM_WEAK void CLog_error(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
}

struct AecPcm {
    HPF *hpf;
    PPFBankAnalyzer *analyzer;
    PPFBankSynthesizer *synthesizer;
    void *draec;
    void *tmp;
    short pcm[3 * AEC_PCM_FRAME_SAMPLES];
    short output[2 * AEC_PCM_FRAME_SAMPLES];
    float hpf_output[3 * AEC_PCM_FRAME_SAMPLES];
    float spectrum[3 * 2 * AEC_PCM_FRAME_SAMPLES];
    float microphone_spectrum[2 * 2 * AEC_PCM_FRAME_SAMPLES];
    int16_t *reference_ring;
    size_t reference_ring_size;
    long long reference_written;
    double reference_read_position;
    double reference_step;
    int reference_started;
    int reference_delay;
    int aec_enabled;
};

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static int16_t reference_at(const AecPcm *aec, long long index)
{
    long long oldest = aec->reference_written -
                       (long long)aec->reference_ring_size;

    if (index < 0 || index < oldest)
        return 0;
    if (index >= aec->reference_written)
        return aec->reference_written == 0 ? 0 :
            aec->reference_ring[(size_t)(aec->reference_written - 1) %
                                aec->reference_ring_size];
    return aec->reference_ring[(size_t)index % aec->reference_ring_size];
}

static short aligned_reference_sample(AecPcm *aec)
{
    long long index = (long long)floor(aec->reference_read_position);
    double fraction = aec->reference_read_position - (double)index;
    double a = reference_at(aec, index);
    double b = reference_at(aec, index + 1);
    long value = lround(a + (b - a) * fraction);

    aec->reference_read_position += aec->reference_step;
    if (value > INT16_MAX)
        value = INT16_MAX;
    else if (value < INT16_MIN)
        value = INT16_MIN;
    return (short)value;
}

void aec_pcm_default_config(AecPcmConfig *config)
{
    if (!config)
        return;
    config->hpf_power = 3;
    config->microphone_prediction_taps = 0;
    config->reference_delay_samples = 0;
    config->reference_drift_ppm = 0.0f;
    config->aec_enabled = 1;
}

int aec_pcm_set_reference_timing(AecPcm *aec, int delay_samples,
                                 float drift_ppm)
{
    if (!aec || delay_samples < 0 ||
        delay_samples > AEC_PCM_MAX_REFERENCE_DELAY ||
        !isfinite(drift_ppm) || drift_ppm < -2000.0f ||
        drift_ppm > 2000.0f)
        return -1;

    aec->reference_delay = delay_samples;
    aec->reference_step = 1.0 + (double)drift_ppm * 1.0e-6;
    if (aec->reference_written > 0) {
        aec->reference_read_position =
            (double)aec->reference_written - delay_samples;
        aec->reference_started = 1;
    }
    return 0;
}

int aec_pcm_create(const AecPcmConfig *config, AecPcm **out)
{
    enum { AEC_ALGORITHM_DRAEC = 301 };
    CoreAecParams params = { AEC_ALGORITHM_DRAEC, 2, 1, 320,
                             5, 0, 0.0f, 0.0f, 0.0f };
    AecPcmConfig defaults;
    AecPcm *aec;
    int tmp_size;
    int status;

    if (!out || sizeof(short) != sizeof(int16_t))
        return -1;
    *out = NULL;
    if (!config) {
        aec_pcm_default_config(&defaults);
        config = &defaults;
    }
    if (config->hpf_power < 0 || config->hpf_power > 10)
        return -1;
    if (config->microphone_prediction_taps < 0 ||
        config->microphone_prediction_taps > 32)
        return -1;
    params.aec_power2 = config->microphone_prediction_taps;

    aec = calloc(1, sizeof(*aec));
    if (!aec)
        return -2;
    aec->reference_ring_size = AEC_PCM_MAX_REFERENCE_DELAY +
                               2 * AEC_PCM_FRAME_SAMPLES + 2;
    aec->reference_ring = calloc(aec->reference_ring_size,
                                 sizeof(*aec->reference_ring));
    if (!aec->reference_ring)
        goto no_memory;

    status = HPF_init(3, AEC_PCM_FRAME_SAMPLES, config->hpf_power, &aec->hpf);
    if (status != 0)
        goto failed;
    status = PPFBankAnalyzer_init(3, WINDOW_PPFA_PROTO_320_1920_DELAY960,
                                  &aec->analyzer);
    if (status != 0)
        goto failed;
    status = PPFBankSynthesizer_init(2, WINDOW_PPFA_PROTO_320_1920_DELAY960,
                                     &aec->synthesizer);
    if (status != 0)
        goto failed;
    status = DRAEC_init(&params, &aec->draec);
    if (status != 0)
        goto failed;

    tmp_size = PPFBankAnalyzer_getTmpMemSize(aec->analyzer);
    tmp_size = max_int(tmp_size,
                       PPFBankSynthesizer_getTmpMemSize(aec->synthesizer));
    tmp_size = max_int(tmp_size, DRAEC_getTmpMemSize(aec->draec));
    aec->tmp = calloc(1, (size_t)tmp_size);
    if (!aec->tmp)
        goto no_memory;
    PPFBankAnalyzer_setTmpMem(aec->analyzer, aec->tmp);
    PPFBankSynthesizer_setTmpMem(aec->synthesizer, aec->tmp);
    DRAEC_setTmpMem(aec->draec, aec->tmp);
    aec->aec_enabled = config->aec_enabled != 0;
    status = aec_pcm_set_reference_timing(
        aec, config->reference_delay_samples, config->reference_drift_ppm);
    if (status != 0)
        goto failed;
    *out = aec;
    return 0;

no_memory:
    status = -2;
failed:
    aec_pcm_destroy(aec);
    return status;
}

void aec_pcm_destroy(AecPcm *aec)
{
    if (!aec)
        return;
    if (aec->draec)
        DRAEC_free(aec->draec);
    if (aec->synthesizer)
        PPFBankSynthesizer_free(aec->synthesizer);
    if (aec->analyzer)
        PPFBankAnalyzer_free(aec->analyzer);
    if (aec->hpf)
        HPF_free(aec->hpf);
    free(aec->tmp);
    free(aec->reference_ring);
    free(aec);
}

int aec_pcm_processing_delay_samples(const AecPcm *aec)
{
    return aec ? PPFBankSynthesizer_delay(aec->synthesizer) : -1;
}

static int process_planar(AecPcm *aec, const int16_t *mic0,
                          const int16_t *mic1, const int16_t *reference)
{
    int reference_active = 0;
    int i;

    if (!aec || !mic0 || !mic1 || !reference)
        return -1;
    for (i = 0; i < AEC_PCM_FRAME_SAMPLES; ++i) {
        aec->pcm[i] = (short)mic0[i];
        aec->pcm[AEC_PCM_FRAME_SAMPLES + i] = (short)mic1[i];
        aec->reference_ring[(size_t)aec->reference_written %
                            aec->reference_ring_size] = reference[i];
        ++aec->reference_written;
    }
    if (!aec->reference_started) {
        aec->reference_read_position =
            (double)(aec->reference_written - AEC_PCM_FRAME_SAMPLES -
                     aec->reference_delay);
        aec->reference_started = 1;
    }
    for (i = 0; i < AEC_PCM_FRAME_SAMPLES; ++i) {
        short sample = aligned_reference_sample(aec);
        aec->pcm[2 * AEC_PCM_FRAME_SAMPLES + i] = sample;
        reference_active |= sample != 0;
    }

    HPF_process(aec->hpf, aec->pcm, aec->hpf_output);
    PPFBankAnalyzer_process(aec->analyzer, aec->hpf_output, aec->spectrum);
    if (aec->aec_enabled) {
        if (!reference_active)
            memcpy(aec->microphone_spectrum, aec->spectrum,
                   sizeof(aec->microphone_spectrum));
        DRAEC_process(aec->draec, aec->spectrum);
        if (reference_active)
            DRAEC_update(aec->draec, aec->spectrum);
        else
            memcpy(aec->spectrum, aec->microphone_spectrum,
                   sizeof(aec->microphone_spectrum));
    }
    PPFBankSynthesizer_process(aec->synthesizer, aec->spectrum,
                               aec->output);
    return 0;
}

int aec_pcm_process_planar(AecPcm *aec, const int16_t *mic0,
                           const int16_t *mic1, const int16_t *reference,
                           int16_t *output0, int16_t *output1)
{
    int status;
    int i;

    if (!output0 || !output1)
        return -1;
    status = process_planar(aec, mic0, mic1, reference);
    if (status != 0)
        return status;
    for (i = 0; i < AEC_PCM_FRAME_SAMPLES; ++i) {
        output0[i] = (int16_t)aec->output[i];
        output1[i] = (int16_t)aec->output[AEC_PCM_FRAME_SAMPLES + i];
    }
    return 0;
}

int aec_pcm_process_mono(AecPcm *aec, const int16_t *mic,
                         const int16_t *reference, int16_t *output)
{
    int status;
    int i;

    if (!output)
        return -1;
    status = process_planar(aec, mic, mic, reference);
    if (status != 0)
        return status;
    for (i = 0; i < AEC_PCM_FRAME_SAMPLES; ++i)
        output[i] = (int16_t)aec->output[i];
    return 0;
}

int aec_pcm_process(AecPcm *aec, const int16_t *mic_interleaved,
                    const int16_t *reference, int16_t *output_interleaved)
{
    int i;
    int status;

    if (!aec || !mic_interleaved || !reference || !output_interleaved)
        return -1;
    for (i = 0; i < AEC_PCM_FRAME_SAMPLES; ++i) {
        aec->pcm[i] = (short)mic_interleaved[2 * i];
        aec->pcm[AEC_PCM_FRAME_SAMPLES + i] =
            (short)mic_interleaved[2 * i + 1];
    }
    status = process_planar(aec, (int16_t *)aec->pcm,
                            (int16_t *)aec->pcm + AEC_PCM_FRAME_SAMPLES,
                            reference);
    if (status != 0)
        return status;
    for (i = 0; i < AEC_PCM_FRAME_SAMPLES; ++i) {
        output_interleaved[2 * i] = (int16_t)aec->output[i];
        output_interleaved[2 * i + 1] =
            (int16_t)aec->output[AEC_PCM_FRAME_SAMPLES + i];
    }
    return 0;
}
