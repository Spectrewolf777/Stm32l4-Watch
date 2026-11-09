/** @file mic.h  IM68D128 PDM microphone on DFSDM1 (PB8 CKOUT, PB1 DATIN0).
 *
 *  2.0 MHz PDM clock (120 MHz / 60), Sinc3 x125 -> 16 kHz, 16-bit mono.
 *  The PDM clock is switched off when not recording to save power.
 *
 *  If recordings are silent / pure noise, the mic's SELECT pin probably puts
 *  data on the other clock edge: set MIC_DATA_ON_FALLING_EDGE to 1.
 *  Too quiet / clipping: change MIC_GAIN_SHIFT (smaller = louder). */
#ifndef MIC_H
#define MIC_H

#include <stdbool.h>
#include <stdint.h>

#define MIC_SAMPLE_RATE           16000U
#define MIC_DATA_ON_FALLING_EDGE  0
#define MIC_GAIN_SHIFT            5

/* Several parts of the firmware can listen at once, each reads at its own pace */
typedef enum { MIC_READER_REC = 0, MIC_READER_AI, MIC_READERS } mic_reader_t;

void     Mic_Init(void);              /* after MX_DFSDM1_Init() */
bool     Mic_Open(mic_reader_t r);    /* starts the mic if nobody else is using it */
void     Mic_Close(mic_reader_t r);   /* stops the mic when the last reader closes */
bool     Mic_IsOpen(mic_reader_t r);
uint32_t Mic_AvailableFor(mic_reader_t r);
uint32_t Mic_ReadFor(mic_reader_t r, int16_t *dst, uint32_t max_samples);
uint32_t Mic_OverrunsFor(mic_reader_t r);

/* Recorder shortcuts (= MIC_READER_REC) */
bool     Mic_Start(void);
void     Mic_Stop(void);
bool     Mic_IsRunning(void);
uint32_t Mic_Available(void);         /* samples waiting */
uint32_t Mic_Read(int16_t *dst, uint32_t max_samples);
uint32_t Mic_Overruns(void);          /* samples dropped (writer too slow) */
uint16_t Mic_Level(void);             /* recent peak 0..32767, for a meter */

#endif
