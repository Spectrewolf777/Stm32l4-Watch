/** @file audio_features.h  Log-mel features for the sound-alert network. */
#ifndef AUDIO_FEATURES_H
#define AUDIO_FEATURES_H
#include <stdint.h>
#include "model_config.h"

void af_init(void);
/* frame: AF_N_FFT int16 samples (16 kHz). out: AF_N_MELS normalised log-mel values,
 * the same (within float rounding) as features.log_mel() + normalisation in train.py */
void af_compute_frame(const int16_t *frame, float *out);
/* power spectrum (AF_N_FFT/2+1 bins) of the last af_compute_frame() call */
const float *af_power(void);

#endif
