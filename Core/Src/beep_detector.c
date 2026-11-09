/** @file beep_detector.c  Smoke (T3) / CO (T4) alarm pattern detector. See beep_detector.h.
 *
 *  1. Every hop (32 ms): find the strongest narrow peak between 2.4 and 4.2 kHz and how far
 *     it stands above the rest of that band ("tonality").
 *  2. Each pattern has its own beep tracker, because they need different settings:
 *       smoke beeps are long (0.5 s): a beep ends only when the tone drops 10 dB, short dips
 *         are joined (room echo makes the level of a long tone wander);
 *       CO beeps are short with 0.1 s gaps that room echo partly fills: a beep ends at a
 *         3 dB drop.
 *     A beep must START with a rise in level: the echo after a beep is also a pure tone,
 *     but it fades, so it is not counted as new beeps. Blips too short to be a beep are
 *     ignored.
 *  3. The newest beeps are matched against the T3 / T4 timing; the group must be followed
 *     by the long pause. Two groups in a row at the right spacing -> alert.
 *  Timing tolerances are wide (real alarms, room echo and the 64 ms analysis window all blur
 *  the edges), but no ordinary sound repeats this exact rhythm on a steady ~3 kHz tone.
 */
#include <stdio.h>
#include <string.h>
#include "beep_detector.h"

#define HZ2BIN(f)     ((int)((f) * BEEP_NFFT / BEEP_FS + 0.5f))
#define BIN_LO        HZ2BIN(2400.0f)
#define BIN_HI        HZ2BIN(4200.0f)
#define HOP_S         ((float)BEEP_HOP / BEEP_FS)            /* 0.032 s */
#define S2H(s)        ((int32_t)((s) / HOP_S + 0.5f))         /* seconds -> hops */

#define TONAL_ON      12.0f   /* peak (3 bins) vs average 3 bins elsewhere in band: 10.8 dB */
#define TONAL_OFF     5.0f    /* hysteresis: stay on down to 7 dB                            */
#define ABS_MIN       0.002f  /* ignore tones below about -75 dBFS (mic self-noise)          */
#define BIN_TOL       5       /* +/- 78 Hz frequency stability                               */
#define ECHO_REL      0.2f    /* a piece < 7 dB below the beep it follows ...                */
#define ECHO_WIN      S2H(0.40f) /* ... within 0.4 s is that beep's room echo: ignored      */
#define RING          24

typedef struct {
    const char *name;
    float   drop_off;         /* beep ends when the tone falls this far below its maximum */
    float   rise;             /* beep starts with this rise over the lowest of 2 hops ago */
    int     n_min, n_max;     /* beeps per group                                          */
    int32_t join;             /* join pieces split by a dip of <= join hops               */
    int32_t min_len;          /* ignore pieces shorter than this (echo blips)             */
    int32_t on_min, on_max;   /* beep length                                              */
    int32_t gap_min, gap_max; /* gap between beeps in a group                             */
    int32_t pause_min;        /* silence before / after a group                           */
    int32_t space_min, space_max; /* end of one group to start of the next                */
    int     len_ratio;        /* longest beep in a group <= len_ratio x shortest (0 = off) */
    int     cycles;           /* groups in a row needed for an alert                      */
} pattern_t;

/* Measured beeps are ~1 hop longer and gaps ~1 hop shorter than the real ones (64 ms window). */
static const pattern_t T3 = {   /* smoke: 3 x (0.5 s on, 0.5 s off), 1.5 s pause */
    "smoke T3", 0.1f, 4.0f, 3, 3, 4, 3, S2H(0.28f), S2H(1.00f), S2H(0.20f), S2H(0.80f),
    S2H(0.90f), S2H(0.90f), S2H(2.60f), 0, 2 };
/* CO: 4 x (0.1 s on, 0.1 s off), ~5 s pause. Room echo can merge two beeps or split one,
 * so a burst of 3-5 short beeps counts. Short pure chirps (birds) can look like that, so CO
 * needs beeps of similar length, the same tone in every group and 3 groups in a row
 * (alert ~12 s after the first beep; CO builds up over minutes, unlike a fire). */
static const pattern_t T4 = {
    "CO T4", 0.5f, 2.0f, 3, 5, 0, 2, 2, S2H(0.30f), 1, S2H(0.30f),
    S2H(1.00f), S2H(2.50f), S2H(8.00f), 3, 3 };

typedef struct { int32_t s, e; int16_t bin; float pmax; } pulse_t;

typedef struct {
    const pattern_t *p;
    /* beep tracker */
    bool     in;
    int32_t  start;
    int16_t  bin;
    float    pmax;
    pulse_t  ring[RING];
    uint32_t np;
    /* pattern matcher */
    bool     pending;         /* group seen, waiting for the pause after it */
    int32_t  g_start, g_end;
    int16_t  g_bin, last_bin;
    int32_t  last_end;        /* end of the previous confirmed group        */
    bool     have_last;
    int      run;             /* confirmed groups in a row                  */
    uint32_t *cycles;
} track_t;

static int32_t  s_t;                      /* hop counter                     */
static float    s_prev[2];                /* band peak 1 and 2 hops ago      */
static track_t  s_t3, s_t4;
static beep_stats_t s_stats;
static void (*s_log)(const char *line);

static bool in_range(int32_t v, int32_t lo, int32_t hi) { return v >= lo && v <= hi; }
static int  iabs(int v) { return v < 0 ? -v : v; }
static int  ms(int32_t hops) { return (int)(hops * BEEP_HOP * 1000 / BEEP_FS); }

void beep_init(void)
{
    s_t = 0;
    s_prev[0] = s_prev[1] = 0.0f;
    memset(&s_stats, 0, sizeof s_stats);
    memset(&s_t3, 0, sizeof s_t3);
    memset(&s_t4, 0, sizeof s_t4);
    s_t3.p = &T3; s_t3.cycles = &s_stats.t3_cycles;
    s_t4.p = &T4; s_t4.cycles = &s_stats.t4_cycles;
}

const beep_stats_t *beep_stats(void) { return &s_stats; }
void beep_set_log(void (*log)(const char *line)) { s_log = log; }

/* Newest-first beeps as this pattern sees them: pieces joined, blips and echoes dropped.
 * *newest_ok = the newest piece belongs to a beep that counts. */
static int view(const track_t *k, pulse_t *out, int max, bool *newest_ok)
{
    const pattern_t *p = k->p;
    pulse_t m[RING], f[RING];
    int nm = 0, nf = 0, have = k->np < RING ? (int)k->np : RING;
    for (int i = have - 1; i >= 0; i--) {             /* oldest -> newest, join pieces */
        const pulse_t *q = &k->ring[(k->np - 1 - i) % RING];
        if (nm > 0 && q->s - m[nm - 1].e <= p->join && iabs(m[nm - 1].bin - q->bin) <= BIN_TOL) {
            m[nm - 1].e = q->e;
            if (q->pmax > m[nm - 1].pmax) m[nm - 1].pmax = q->pmax;
        } else {
            m[nm++] = *q;
        }
    }
    /* only pieces on the same tone as the newest one: other sounds at other pitches
     * (clinks, birds, music) must not break up an alarm's rhythm */
    int16_t bin = k->ring[(k->np - 1) % RING].bin;
    for (int i = 0; i < nm; i++) {                    /* drop blips and room echoes */
        if (m[i].e - m[i].s < p->min_len) continue;
        if (iabs(m[i].bin - bin) > BIN_TOL) continue;
        if (nf > 0 && m[i].s - f[nf - 1].e <= ECHO_WIN && m[i].pmax < ECHO_REL * f[nf - 1].pmax)
            continue;
        f[nf++] = m[i];
    }
    /* is the newest raw piece part of a kept beep? */
    const pulse_t *raw = &k->ring[(k->np - 1) % RING];
    *newest_ok = nf > 0 && f[nf - 1].e == raw->e;
    int n = 0;
    for (int i = nf - 1; i >= 0 && n < max; i--) out[n++] = f[i];
    return n;
}

static void match(track_t *k)
{
    const pattern_t *p = k->p;
    pulse_t q[8];
    bool ok;
    int have = view(k, q, p->n_max + 2, &ok);
    if (!ok || have == 0) return;                     /* newest piece was only a blip */

    /* a real beep soon after a pending group: the group was not followed by the pause.
     * (q[0] may be the group's own last beep grown by a joined piece: same start then) */
    if (k->pending && q[0].s > k->g_end && q[0].s - k->g_end < p->pause_min &&
        iabs(q[0].bin - k->g_bin) <= BIN_TOL)
        k->pending = false;

    /* the group = newest beeps that follow each other with short gaps */
    int c = 1;
    while (c < have && q[c - 1].s - q[c].e <= p->gap_max) c++;
    if (c < p->n_min || c > p->n_max) return;
    for (int i = 0; i < c; i++) {
        if (!in_range(q[i].e - q[i].s, p->on_min, p->on_max)) return;
        if (iabs(q[i].bin - q[c - 1].bin) > BIN_TOL) return;
        if (i < c - 1 && q[i].s - q[i + 1].e < p->gap_min) return;
    }
    if (p->len_ratio) {
        int32_t lo = q[0].e - q[0].s, hi = lo;
        for (int i = 1; i < c; i++) {
            int32_t l = q[i].e - q[i].s;
            if (l < lo) lo = l;
            if (l > hi) hi = l;
        }
        if (hi > p->len_ratio * lo) return;
    }
    if (c < have && q[c - 1].s - q[c].e < p->pause_min) return;   /* no pause before it */
    k->pending = true;
    k->g_start = q[c - 1].s;
    k->g_end   = q[0].e;
    k->g_bin   = q[0].bin;
}

static bool confirm(track_t *k)
{
    const pattern_t *p = k->p;
    if (!k->pending || k->in || s_t - k->g_end < p->pause_min) return false;
    k->pending = false;
    (*k->cycles)++;
    int32_t space = k->g_start - k->last_end;
    bool same_tone = iabs(k->g_bin - k->last_bin) <= BIN_TOL;
    k->run = (k->have_last && same_tone && in_range(space, p->space_min, p->space_max)) ? k->run + 1 : 1;
    k->last_end = k->g_end;
    k->last_bin = k->g_bin;
    k->have_last = true;
    if (s_log) {
        char line[64];
        snprintf(line, sizeof line, "[BEEP] %s cycle %d/%d\r\n", p->name, k->run, p->cycles);
        s_log(line);
    }
    if (k->run >= p->cycles) { k->run = 0; return true; }
    return false;
}

static void piece_end(track_t *k)
{
    pulse_t *p = &k->ring[k->np % RING];
    p->s = k->start; p->e = s_t; p->bin = k->bin; p->pmax = k->pmax;
    k->np++;
    if (k == &s_t3) {                                 /* stats from the smoke tracker */
        s_stats.pulses++;
        s_stats.last_freq_hz = (uint16_t)(p->bin * BEEP_FS / BEEP_NFFT);
        s_stats.last_len_ms  = (uint16_t)ms(p->e - p->s);
    }
    if (s_log && p->e - p->s >= k->p->min_len) {
        char line[80];
        snprintf(line, sizeof line, "[BEEP] %-5.5s tone %u Hz %u ms (gap before %d ms)\r\n",
                 k->p->name, (unsigned)(p->bin * BEEP_FS / BEEP_NFFT), (unsigned)ms(p->e - p->s),
                 k->np > 1 ? ms(p->s - k->ring[(k->np - 2) % RING].e) : -1);
        s_log(line);
    }
    match(k);
}

static void track(track_t *k, float peak, float tonal, int kmax, float low)
{
    if (!k->in) {
        if (tonal >= TONAL_ON && peak >= ABS_MIN && peak >= k->p->rise * low) {
            k->in = true; k->start = s_t; k->bin = (int16_t)kmax; k->pmax = peak;
        }
    } else if (tonal >= TONAL_OFF && peak >= k->pmax * k->p->drop_off && iabs(kmax - k->bin) <= BIN_TOL) {
        if (peak > k->pmax) k->pmax = peak;
    } else {
        k->in = false;
        piece_end(k);
    }
}

beep_event_t beep_update(const float *pow)
{
    beep_event_t ev = BEEP_NONE;
    s_t++;

    /* strongest narrow peak in the alarm band and how tonal it is */
    int kmax = BIN_LO + 1;
    float sum = 0.0f, vmax = 0.0f;
    for (int k = BIN_LO; k <= BIN_HI; k++) {
        sum += pow[k];
        if (pow[k] > vmax && k > BIN_LO && k < BIN_HI) { vmax = pow[k]; kmax = k; }
    }
    float peak  = pow[kmax - 1] + pow[kmax] + pow[kmax + 1];
    float rest  = (sum - peak) / (float)(BIN_HI - BIN_LO + 1 - 3);
    float tonal = peak / (3.0f * rest + 1e-12f);
    float low   = s_prev[0] < s_prev[1] ? s_prev[0] : s_prev[1];

    track(&s_t3, peak, tonal, kmax, low);
    track(&s_t4, peak, tonal, kmax, low);
    s_prev[1] = s_prev[0];
    s_prev[0] = peak;

    if (confirm(&s_t3)) { s_stats.t3_alerts++; ev = BEEP_T3_SMOKE; }
    if (confirm(&s_t4)) { s_stats.t4_alerts++; ev = BEEP_T4_CO; }
    return ev;
}
