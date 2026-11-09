/** @file sound_detector.h
 *  Sound alerts for deaf / hard-of-hearing users: smoke alarm, door knock, doorbell, siren.
 *  The network is trained in training/ and imported with tools/update_model.sh.
 */
#ifndef SOUND_DETECTOR_H
#define SOUND_DETECTOR_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t inferences;        /* network runs                                  */
    uint32_t gated;             /* windows skipped because the room was quiet    */
    uint32_t alerts;
    uint32_t last_infer_ms, max_infer_ms;
    uint32_t mic_dropped;       /* samples lost because SD_Task ran too rarely   */
    float    last_prob[16];     /* smoothed probability per class                */
    int      last_alert_class;
    float    last_alert_conf;
    uint32_t last_alert_tick;
} sd_stats_t;

int   SD_Init(void);            /* after Mic_Init(). 0 = ok                       */
bool  SD_Start(void);           /* opens the mic and starts listening            */
void  SD_Stop(void);
bool  SD_IsRunning(void);
bool  SD_IsReady(void);
void  SD_Task(void);            /* call from the main loop                       */
void  SD_SetLog(bool on);       /* print every result over USB serial            */
const sd_stats_t *SD_Stats(void);
const char *SD_ClassName(int c);
int   SD_NumClasses(void);

/* Runs the built-in test clip (Core/AI/test_vector.h) through features + network and
 * compares with what Python computed. true = the watch matches the PC. */
bool  SD_SelfTest(float *feat_err, float *prob_err);

/* Called when a sound is detected (implemented in ui_watch.c) */
void  sd_on_detect(int class_id, const char *name, float confidence);

#endif
