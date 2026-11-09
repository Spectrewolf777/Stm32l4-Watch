/* PC check that Core/Src/audio_features.c computes the same log-mel features as Python.
 *   gcc -O2 -I Core/Inc -I Core/AI tools/test_features_host.c Core/Src/audio_features.c \
 *       Core/AI/model_tables.c -lm -o /tmp/tf && /tmp/tf
 * Uses Core/AI/test_vector.h (written by train.py). Expect max error < 0.01. */
#include <math.h>
#include <stdio.h>
#include "audio_features.h"
#include "test_vector.h"

int main(void)
{
    float mel[AF_N_MELS], max_err = 0.0f;
    af_init();
    for (int f = 0; f < AF_N_FRAMES; f++) {
        af_compute_frame(&tv_audio[f * AF_HOP], mel);
        for (int m = 0; m < AF_N_MELS; m++) {
            float e = fabsf(mel[m] - tv_features[f * AF_N_MELS + m]);
            if (e > max_err) max_err = e;
        }
    }
    printf("max feature error vs Python: %.3g  -> %s\n", max_err, max_err < 0.01f ? "PASS" : "FAIL");
    return max_err < 0.01f ? 0 : 1;
}
