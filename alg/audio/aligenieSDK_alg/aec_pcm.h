#ifndef AEC_PCM_H
#define AEC_PCM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AEC_PCM_SAMPLE_RATE = 16000,
    AEC_PCM_FRAME_SAMPLES = 320,
    AEC_PCM_MIC_CHANNELS = 2,
    AEC_PCM_REF_CHANNELS = 1,
    AEC_PCM_MAX_REFERENCE_DELAY = 16000
};

typedef struct AecPcm AecPcm;

typedef struct {
    int hpf_power;
    int microphone_prediction_taps;
    int reference_delay_samples;
    float reference_drift_ppm;
    int aec_enabled;
} AecPcmConfig;

void aec_pcm_default_config(AecPcmConfig *config);
int aec_pcm_create(const AecPcmConfig *config, AecPcm **aec);
void aec_pcm_destroy(AecPcm *aec);

int aec_pcm_set_reference_timing(AecPcm *aec, int delay_samples,
                                 float drift_ppm);
int aec_pcm_processing_delay_samples(const AecPcm *aec);

/* Positive drift_ppm consumes the playback reference faster. */

int aec_pcm_process(AecPcm *aec,
                    const int16_t mic_interleaved[AEC_PCM_FRAME_SAMPLES *
                                                   AEC_PCM_MIC_CHANNELS],
                    const int16_t reference[AEC_PCM_FRAME_SAMPLES],
                    int16_t output_interleaved[AEC_PCM_FRAME_SAMPLES *
                                                AEC_PCM_MIC_CHANNELS]);

int aec_pcm_process_planar(AecPcm *aec,
                           const int16_t mic0[AEC_PCM_FRAME_SAMPLES],
                           const int16_t mic1[AEC_PCM_FRAME_SAMPLES],
                           const int16_t reference[AEC_PCM_FRAME_SAMPLES],
                           int16_t output0[AEC_PCM_FRAME_SAMPLES],
                           int16_t output1[AEC_PCM_FRAME_SAMPLES]);

int aec_pcm_process_mono(AecPcm *aec,
                         const int16_t mic[AEC_PCM_FRAME_SAMPLES],
                         const int16_t reference[AEC_PCM_FRAME_SAMPLES],
                         int16_t output[AEC_PCM_FRAME_SAMPLES]);

#ifdef __cplusplus
}
#endif

#endif
