#ifndef MULTIMEDIA_USER_H
#define MULTIMEDIA_USER_H

#define CONFIG_DECODER_AAC                    0
#define CONFIG_DECODER_ADPCM_MS               0
#define CONFIG_DECODER_ALAW                   1
#define CONFIG_DECODER_AMRNB                  0
#define CONFIG_DECODER_AMRWB                  0
#define CONFIG_DECODER_FLAC                   0
#define CONFIG_DECODER_MULAW                  0
#define CONFIG_DECODER_OPUS                   0
#define CONFIG_DECODER_PCM                    0
#define CONFIG_DECODER_PVMP3                  0
#define CONFIG_DECODER_SPEEX                  0
#define CONFIG_DECODER_VORBIS                 0

#define CONFIG_DEMUXER_RAWAUDIO               1
#define CONFIG_DEMUXER_WAV                    0

#define CONFIG_STREAMER_ASHX                  0
#define CONFIG_STREAMER_CRYPTO                0
#define CONFIG_STREAMER_FIFO                  1
#define CONFIG_STREAMER_FILE                  0
#define CONFIG_STREAMER_HLS                   0
#define CONFIG_STREAMER_HTTP                  0
#define CONFIG_STREAMER_MEM                   0

#define CONFIG_ATEMPOER_SONIC                 0
#define CONFIG_RESAMPLER_SPEEX                0
#define CONFIG_AO_MIXER_SUPPORT               0
#define CONFIG_AEF_EQ_ENABLE                  0
#define CONFIG_AV_AO_CHANNEL_NUM              2

#define CONFIG_AV_PROBE_SIZE_MAX              64
#define CONFIG_AV_STREAM_INNER_BUF_SIZE        160
#define CONFIG_AV_SAMPLE_NUM_PER_FRAME_MAX    160
#define CONFIG_PLAYER_TASK_STACK_SIZE          (12 * 1024)

#define CONFIG_MSP_DEBUG                       0

#endif
