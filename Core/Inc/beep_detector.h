/** @file beep_detector.h
 *  Rule-based detector for the standard smoke / CO alarm sound patterns. No training needed.
 *
 *  ISO 8201 / ANSI S3.41 / NFPA 72 "temporal" patterns, piezo tone ~2.4-4.2 kHz:
 *    T3 (smoke / fire): beep 0.5 s, pause 0.5 s, x3, then 1.5 s pause, repeat
 *    T4 (carbon monoxide): beep 0.1 s, pause 0.1 s, x4, then ~5 s pause, repeat
 *  Alert after 2 complete smoke cycles in a row (~8 s after the first beep) or 3 CO cycles
 *  on the same tone (~12 s).
 *
 *  Runs on the power spectrum the sound AI already computes every 32 ms
 *  (16 kHz, 1024-point FFT, 512-sample hop), so it costs almost nothing extra.
 */
#ifndef BEEP_DETECTOR_H
#define BEEP_DETECTOR_H
#include <stdbool.h>
#include <stdint.h>

#define BEEP_FS     16000
#define BEEP_NFFT   1024
#define BEEP_HOP    512

typedef enum { BEEP_NONE = 0, BEEP_T3_SMOKE, BEEP_T4_CO } beep_event_t;

typedef struct {
    uint32_t pulses;            /* tone beeps seen                       */
    uint32_t t3_cycles, t4_cycles;
    uint32_t t3_alerts, t4_alerts;
    uint16_t last_freq_hz;      /* tone of the last beep                 */
    uint16_t last_len_ms;       /* length of the last beep               */
} beep_stats_t;

void beep_init(void);
/* pow: power spectrum, BEEP_NFFT/2+1 bins, of the latest hop. Call once per hop. */
beep_event_t beep_update(const float *pow);
const beep_stats_t *beep_stats(void);
/* optional: print every beep and cycle (for testing against your own alarm) */
void beep_set_log(void (*log)(const char *line));

#endif
