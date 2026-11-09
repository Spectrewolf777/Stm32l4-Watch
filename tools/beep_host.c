/* PC harness for the smoke/CO beep detector: raw 16 kHz mono int16 on stdin,
 * prints "T3 <seconds>" / "T4 <seconds>" for every alert. -v prints every beep.
 *   gcc -O2 -I Core/Inc -I Core/AI tools/beep_host.c Core/Src/beep_detector.c \
 *       Core/Src/audio_features.c Core/AI/model_tables.c -lm -o beep_host           */
#include <stdio.h>
#include <string.h>
#include "audio_features.h"
#include "beep_detector.h"

static void log_line(const char *l) { fputs(l, stdout); }

int main(int argc, char **argv)
{
    int16_t frame[AF_N_FFT] = {0}, hop[AF_HOP];
    float mel[AF_N_MELS];
    long hops = 0;
    af_init();
    beep_init();
    if (argc > 1 && strcmp(argv[1], "-v") == 0) beep_set_log(log_line);
    while (fread(hop, sizeof(int16_t), AF_HOP, stdin) == AF_HOP) {
        memmove(frame, frame + AF_HOP, AF_HOP * sizeof(int16_t));
        memcpy(frame + AF_HOP, hop, AF_HOP * sizeof(int16_t));
        af_compute_frame(frame, mel);
        beep_event_t ev = beep_update(af_power());
        hops++;
        if (ev) printf("%s %.2f\n", ev == BEEP_T3_SMOKE ? "T3" : "T4", hops * (double)AF_HOP / AF_SAMPLE_RATE);
    }
    printf("END %.2f pulses %lu\n", hops * (double)AF_HOP / AF_SAMPLE_RATE, (unsigned long)beep_stats()->pulses);
    return 0;
}
