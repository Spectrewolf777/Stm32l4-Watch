/** @file sound_detector.c
 *  Sound alerts: mic (mic.c, 16 kHz) -> log-mel (audio_features.c) -> int8 CNN (ST Edge AI,
 *  Core/AI/network*.c) -> smoothing -> sd_on_detect() (ui_watch.c: vibrate + popup).
 *
 *  It reads the SAME 16-bit samples the voice recorder writes to the WAV files, so
 *  recordings made with the watch are exactly what the network sees.
 *
 *  Next to the network, beep_detector.c watches the same spectrum for the standard smoke (T3)
 *  and CO (T4) alarm beep patterns. That rule needs no training and almost never fires on
 *  anything else, so smoke / CO alarms do not depend on the AI being right. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "mic.h"
#include "network.h"
#include "network_data.h"
#include "audio_features.h"
#include "sound_detector.h"
#include "beep_detector.h"

/* ------------------------------------------------------------------ tuning */
#define INFER_EVERY_HOPS   15      /* run the network every 15 x 32 ms = 0.48 s          */
#define SMOOTH_N           3       /* average the last 3 results (~1.5 s)                */
#define DETECT_THRESHOLD   0.85f   /* averaged probability needed to alert (train.py prints
                                      false buzzes/hour at 0.7 / 0.8 / 0.9)               */
#define COOLDOWN_MS        8000U   /* don't alert again for the same sound within 8 s    */
#define ENERGY_GATE        200     /* skip the network if the 1 s window peak < this     */
#define MAX_HOPS_PER_CALL  4       /* keep the main loop (UI, USB) responsive            */

_Static_assert(AF_N_CLASSES <= 16, "sd_stats_t.last_prob is too small");
_Static_assert(AF_SAMPLE_RATE == BEEP_FS && AF_N_FFT == BEEP_NFFT && AF_HOP == BEEP_HOP,
               "beep_detector.h must use the same FFT settings as the network features");

#ifdef AF_CLASS_ALARM
#define ALARM_CLASS AF_CLASS_ALARM      /* beep alerts share the AI alarm cooldown */
#else
#define ALARM_CLASS (-1)
#endif

#if AI_NETWORK_IN_1_SIZE != AF_N_FRAMES * AF_N_MELS || AI_NETWORK_OUT_1_SIZE != AF_N_CLASSES
#error "Core/AI/network.h does not match Core/AI/model_config.h - run tools/update_model.sh"
#endif

/* ------------------------------------------------------------------ state */
static int16_t  s_hop[AF_HOP];
static int16_t  s_frame[AF_N_FFT];                 /* [previous hop | new hop]      */
static float    s_spec[AF_N_FRAMES][AF_N_MELS];    /* rolling spectrogram (ring)    */
static int16_t  s_peak[AF_N_FRAMES];               /* loudest sample per frame      */
static uint32_t s_head, s_filled, s_since_infer;
static float    s_hist[SMOOTH_N][AF_N_CLASSES];
static uint32_t s_hist_i, s_last_alert[AF_N_CLASSES], s_last_beep_alert;
static bool     s_ready, s_running, s_log;
static sd_stats_t s_stats;

static ai_handle s_net = AI_HANDLE_NULL;
AI_ALIGNED(32) static ai_u8 s_act[AI_NETWORK_DATA_ACTIVATIONS_SIZE];
AI_ALIGNED(4)  static ai_float s_in[AI_NETWORK_IN_1_SIZE];
AI_ALIGNED(4)  static ai_float s_out[AI_NETWORK_OUT_1_SIZE];
static ai_buffer *s_ai_in, *s_ai_out;

extern void USB_Printf(const char *fmt, ...);      /* main.c */

/* ------------------------------------------------------------------ helpers */
static void cycles_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static bool ai_run(void)
{
    uint32_t t0 = DWT->CYCCNT;
    bool ok = ai_network_run(s_net, s_ai_in, s_ai_out) == 1;
    uint32_t ms = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000U);
    s_stats.last_infer_ms = ms;
    if (ms > s_stats.max_infer_ms) s_stats.max_infer_ms = ms;
    if (!ok) memset(s_out, 0, sizeof(s_out));
    return ok;
}

static void reset_stream(void)
{
    memset(s_frame, 0, sizeof(s_frame));
    memset(s_hist, 0, sizeof(s_hist));
    for (int k = 0; k < SMOOTH_N; k++) s_hist[k][AF_CLASS_BACKGROUND] = 1.0f;
    s_head = s_filled = s_since_infer = s_hist_i = 0;
    beep_init();
}

static void beep_log(const char *line) { USB_Printf("%s", line); }

/* smoke / CO pattern found by the rule-based detector */
static void beep_alert(beep_event_t ev)
{
    uint32_t now = HAL_GetTick();
    if (s_last_beep_alert && now - s_last_beep_alert <= COOLDOWN_MS) return;
    s_last_beep_alert = now ? now : 1;
    if (ALARM_CLASS >= 0) s_last_alert[ALARM_CLASS] = s_last_beep_alert;   /* no 2nd alert from the AI */
    const char *name = ev == BEEP_T3_SMOKE ? "smoke_alarm" : "co_alarm";
    s_stats.alerts++;
    s_stats.last_alert_class = ALARM_CLASS;
    s_stats.last_alert_conf = 1.0f;
    s_stats.last_alert_tick = now;
    if (s_log) USB_Printf("[BEEP] >>> ALERT %s (beep pattern)\r\n", name);
    sd_on_detect(ALARM_CLASS, name, 1.0f);
}

/* ------------------------------------------------------------------ public */
int SD_Init(void)
{
    cycles_init();
    af_init();
    const ai_handle acts[] = { s_act };
    ai_error err = ai_network_create_and_init(&s_net, acts, NULL);
    if (err.type != AI_ERROR_NONE) return -1;
    s_ai_in  = ai_network_inputs_get(s_net, NULL);
    s_ai_out = ai_network_outputs_get(s_net, NULL);
    s_ai_in[0].data  = AI_HANDLE_PTR(s_in);
    s_ai_out[0].data = AI_HANDLE_PTR(s_out);
    s_ready = true;
    return 0;
}

bool SD_Start(void)
{
    if (!s_ready) return false;
    if (s_running) return true;
    reset_stream();
    if (!Mic_Open(MIC_READER_AI)) return false;
    s_running = true;
    return true;
}

void SD_Stop(void)
{
    if (!s_running) return;
    Mic_Close(MIC_READER_AI);
    s_running = false;
}

bool SD_IsRunning(void) { return s_running; }
bool SD_IsReady(void)   { return s_ready; }
void SD_SetLog(bool on) { s_log = on; beep_set_log(on ? beep_log : NULL); }
const sd_stats_t *SD_Stats(void)
{
    s_stats.mic_dropped = Mic_OverrunsFor(MIC_READER_AI);
    return &s_stats;
}
const char *SD_ClassName(int c) { return (c >= 0 && c < AF_N_CLASSES) ? af_class_names[c] : "?"; }
int SD_NumClasses(void) { return AF_N_CLASSES; }

__attribute__((weak)) void sd_on_detect(int class_id, const char *name, float confidence)
{
    (void)class_id; (void)name; (void)confidence;
}

static void classify_window(void)
{
    int16_t peak = 0;
    for (int f = 0; f < AF_N_FRAMES; f++) if (s_peak[f] > peak) peak = s_peak[f];

    float *res = s_hist[s_hist_i++ % SMOOTH_N];
    if (peak < ENERGY_GATE) {                         /* quiet: don't run the network */
        memset(res, 0, sizeof(float) * AF_N_CLASSES);
        res[AF_CLASS_BACKGROUND] = 1.0f;
        s_stats.gated++;
    } else {
        for (int f = 0; f < AF_N_FRAMES; f++)        /* oldest frame first */
            memcpy(&s_in[f * AF_N_MELS], s_spec[(s_head + f) % AF_N_FRAMES], sizeof(float) * AF_N_MELS);
        ai_run();
        memcpy(res, s_out, sizeof(float) * AF_N_CLASSES);
        s_stats.inferences++;
    }

    int best = AF_CLASS_BACKGROUND;
    float best_p = 0.0f;
    for (int c = 0; c < AF_N_CLASSES; c++) {
        float avg = 0.0f;
        for (int k = 0; k < SMOOTH_N; k++) avg += s_hist[k][c];
        avg /= SMOOTH_N;
        s_stats.last_prob[c] = avg;
        if (c != AF_CLASS_BACKGROUND && avg > best_p) { best_p = avg; best = c; }
    }

    if (s_log && peak >= ENERGY_GATE) {
        char line[120];
        int n = snprintf(line, sizeof line, "[AI] pk%5d", peak);
        for (int c = 1; c < AF_N_CLASSES && n < (int)sizeof line; c++)   /* skip background */
            n += snprintf(line + n, sizeof line - n, " %.5s %3d", af_class_names[c],
                          (int)(res[c] * 100.0f + 0.5f));
        if (n < (int)sizeof line)
            snprintf(line + n, sizeof line - n, " %lums\r\n", (unsigned long)s_stats.last_infer_ms);
        USB_Printf("%s", line);
    }

    uint32_t now = HAL_GetTick();
    if (best != AF_CLASS_BACKGROUND && best_p >= DETECT_THRESHOLD &&
        (s_last_alert[best] == 0 || now - s_last_alert[best] > COOLDOWN_MS)) {
        s_last_alert[best] = now ? now : 1;
        s_stats.alerts++;
        s_stats.last_alert_class = best;
        s_stats.last_alert_conf = best_p;
        s_stats.last_alert_tick = now;
        if (s_log) USB_Printf("[AI] >>> ALERT %s %d%%\r\n", af_class_names[best], (int)(best_p * 100));
        sd_on_detect(best, af_class_names[best], best_p);
    }
}

void SD_Task(void)
{
    if (!s_running) return;
    for (int n = 0; n < MAX_HOPS_PER_CALL && Mic_AvailableFor(MIC_READER_AI) >= AF_HOP; n++) {
        Mic_ReadFor(MIC_READER_AI, s_hop, AF_HOP);
        memmove(s_frame, &s_frame[AF_HOP], AF_HOP * sizeof(int16_t));
        memcpy(&s_frame[AF_HOP], s_hop, AF_HOP * sizeof(int16_t));

        int16_t pk = 0;
        for (int i = 0; i < AF_N_FFT; i++) {
            int16_t a = s_frame[i] < 0 ? (int16_t)(-(s_frame[i] + 1)) : s_frame[i];
            if (a > pk) pk = a;
        }
        s_peak[s_head] = pk;
        af_compute_frame(s_frame, s_spec[s_head]);
        s_head = (s_head + 1) % AF_N_FRAMES;

        beep_event_t ev = beep_update(af_power());
        if (ev != BEEP_NONE) beep_alert(ev);

        if (s_filled < AF_N_FRAMES + 1) s_filled++;   /* +1: first frame has a zero half */
        if (++s_since_infer >= INFER_EVERY_HOPS && s_filled > AF_N_FRAMES) {
            s_since_infer = 0;
            classify_window();
        }
    }
}

/* ------------------------------------------------------------------ self test */
#include "test_vector.h"

bool SD_SelfTest(float *feat_err, float *prob_err)
{
    float fe = 0.0f, pe = 0.0f;
    if (!s_ready) { *feat_err = *prob_err = 99.0f; return false; }
    for (int f = 0; f < AF_N_FRAMES; f++) {
        af_compute_frame(&tv_audio[f * AF_HOP], &s_in[f * AF_N_MELS]);
        for (int m = 0; m < AF_N_MELS; m++) {
            float e = fabsf(s_in[f * AF_N_MELS + m] - tv_features[f * AF_N_MELS + m]);
            if (e > fe) fe = e;
        }
    }
    ai_run();
    for (int c = 0; c < AF_N_CLASSES; c++) {
        float e = fabsf(s_out[c] - tv_probs[c]);
        if (e > pe) pe = e;
    }
    *feat_err = fe;
    *prob_err = pe;
    return fe < 0.01f && pe < 0.05f;
}
