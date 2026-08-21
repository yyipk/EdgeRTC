#ifndef RNNOISE_STATIC_TABLES_H
#define RNNOISE_STATIC_TABLES_H

#include "kiss_fft.h"

#define RNNOISE_STATIC_FRAME_SIZE 480
#define RNNOISE_STATIC_NB_BANDS 22

extern const kiss_fft_state rnnoise_fft_state_960;
extern const float rnnoise_half_window_480[RNNOISE_STATIC_FRAME_SIZE];
extern const float rnnoise_dct_table_22[RNNOISE_STATIC_NB_BANDS *
                                        RNNOISE_STATIC_NB_BANDS];

#endif
