/** @file audio_features.c
 *  Log-mel features, the same maths as features.py in training/:
 *  periodic Hann window -> 1024-point real FFT -> power -> 40 mel filters -> log -> normalise.
 *
 *  Self-contained (no CMSIS-DSP): the 1024-point real FFT is done as a 512-point complex
 *  FFT of the packed even/odd samples plus the usual split step. Tested on the PC against
 *  the Python features (tools/test_features_host.c). */
#include <math.h>
#include "audio_features.h"

#define N   AF_N_FFT          /* 1024 */
#define NH  (AF_N_FFT / 2)    /* 512  */

#if AF_N_FFT != 1024
#error "audio_features.c is written for a 1024-point FFT"
#endif

static float s_cos[NH], s_sin[NH];        /* cos/sin(2*pi*k/N), k < N/2 (4 KB)   */
static float s_re[NH], s_im[NH];          /* complex FFT work buffers (4 KB)      */
static float s_pow[NH + 1];               /* power spectrum bins 0..N/2 (2 KB)    */
static uint16_t s_bitrev[NH];             /* bit-reversal permutation (1 KB)      */

void af_init(void)
{
    for (int k = 0; k < NH; k++) {
        double a = 2.0 * 3.14159265358979323846 * (double)k / (double)N;
        s_cos[k] = (float)cos(a);
        s_sin[k] = (float)sin(a);
    }
    int bits = 0;
    while ((1 << bits) < NH) bits++;
    for (int i = 0; i < NH; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        s_bitrev[i] = (uint16_t)r;
    }
}

/* periodic Hann: w[n] = 0.5 - 0.5 cos(2 pi n / N), and w[n] = w[N - n] */
static inline float hann(int n)
{
    return n < NH ? 0.5f - 0.5f * s_cos[n] : (n == NH ? 1.0f : 0.5f - 0.5f * s_cos[N - n]);
}

/* In-place radix-2 complex FFT of size NH (input already bit-reversed).
 * Twiddle for size-NH FFT: exp(-j 2 pi k / NH) = exp(-j 2 pi (2k) / N). */
static void fft_nh(void)
{
    for (int len = 2; len <= NH; len <<= 1) {
        int half = len >> 1, step = N / len;      /* index into the N-point tables */
        for (int i = 0; i < NH; i += len) {
            for (int j = 0; j < half; j++) {
                float wr = s_cos[j * step], wi = -s_sin[j * step];
                float *ar = &s_re[i + j], *ai = &s_im[i + j];
                float *br = &s_re[i + j + half], *bi = &s_im[i + j + half];
                float tr = *br * wr - *bi * wi;
                float ti = *br * wi + *bi * wr;
                *br = *ar - tr; *bi = *ai - ti;
                *ar += tr;      *ai += ti;
            }
        }
    }
}

const float *af_power(void) { return s_pow; }

void af_compute_frame(const int16_t *frame, float *out)
{
    /* pack z[k] = x[2k] + j x[2k+1] (windowed, scaled to [-1, 1]) in bit-reversed order */
    for (int k = 0; k < NH; k++) {
        int r = s_bitrev[k];
        s_re[r] = (float)frame[2 * k]     * (1.0f / 32768.0f) * hann(2 * k);
        s_im[r] = (float)frame[2 * k + 1] * (1.0f / 32768.0f) * hann(2 * k + 1);
    }
    fft_nh();

    /* split: X[k] = E[k] + W^k O[k],  E = (Z[k] + conj Z[NH-k]) / 2,  O = (Z[k] - conj Z[NH-k]) / 2j */
    s_pow[0]  = (s_re[0] + s_im[0]) * (s_re[0] + s_im[0]);
    s_pow[NH] = (s_re[0] - s_im[0]) * (s_re[0] - s_im[0]);
    for (int k = 1; k < NH; k++) {
        float ar = s_re[k], ai = s_im[k];
        float br = s_re[NH - k], bi = -s_im[NH - k];          /* conj Z[NH-k] */
        float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
        float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
        float or_ = di, oi = -dr;                             /* divide by j */
        float wr = s_cos[k], wi = -s_sin[k];                  /* exp(-j 2 pi k / N) */
        float xr = er + (or_ * wr - oi * wi);
        float xi = ei + (or_ * wi + oi * wr);
        s_pow[k] = xr * xr + xi * xi;
    }

    for (int m = 0; m < AF_N_MELS; m++) {
        const float *w = &af_mel_weights[af_mel_offset[m]];
        const float *p = &s_pow[af_mel_start[m]];
        float acc = 0.0f;
        for (int i = 0; i < af_mel_len[m]; i++) acc += w[i] * p[i];
        out[m] = (logf(acc + AF_LOG_EPS) - af_feat_mean[m]) * af_feat_inv_std[m];
    }
}
