#include "recorder.h"
#include "mic.h"
#include "fatfs.h"
#include "rv_3028_c7.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define CHUNK_SAMPLES   2048U          /* 4 KB per f_write  */
#define SYNC_PERIOD_MS  5000U          /* header + f_sync so a crash keeps audio */
#define REC_DIR         "REC"

extern rv3028_handle_t rtc_handle;
bool MSC_Lock(void);                    /* msc_disk.c */
void MSC_Unlock(void);

static bool     s_running;
static bool     s_mounted;
static uint32_t s_data_bytes;
static uint32_t s_last_sync;
static char     s_name[32] = "";
static const char *s_status = "ok";
static char     s_status_buf[48];
static int16_t  s_buf[CHUNK_SAMPLES] __attribute__((aligned(4)));   /* DMA-friendly */

/* Training labels for the sound AI ("" = normal voice memo). Must match the prefixes in
 * training/import_watch_recordings.py */
static const char *const s_labels[] = { "", "ALARM", "KNOCK", "BELL", "SIREN", "BG" };
static uint8_t s_label;

uint32_t SD_Glue_LastError(void);   /* sd_glue.c */

static const char *fr_name(FRESULT fr)
{
    static const char *names[] = {
        "OK", "DISK_ERR", "INT_ERR", "NOT_READY", "NO_FILE", "NO_PATH",
        "INVALID_NAME", "DENIED", "EXIST", "INVALID_OBJECT", "WRITE_PROTECTED",
        "INVALID_DRIVE", "NOT_ENABLED", "NO_FILESYSTEM", "MKFS_ABORTED", "TIMEOUT",
        "LOCKED", "NOT_ENOUGH_CORE", "TOO_MANY_OPEN_FILES", "INVALID_PARAMETER" };
    return (unsigned)fr < sizeof(names) / sizeof(names[0]) ? names[fr] : "?";
}

/* Builds e.g. "mount: DISK_ERR sd=0x8" so failures say exactly why */
static const char *fr_status(const char *what, FRESULT fr)
{
    if (fr == FR_NO_FILESYSTEM)
        snprintf(s_status_buf, sizeof s_status_buf, "%s: no FAT (format FAT32 on PC)", what);
    else if (fr == FR_DISK_ERR || fr == FR_NOT_READY)
        snprintf(s_status_buf, sizeof s_status_buf, "%s: %s sd=0x%lX", what, fr_name(fr),
                 (unsigned long)SD_Glue_LastError());
    else
        snprintf(s_status_buf, sizeof s_status_buf, "%s: %s", what, fr_name(fr));
    return s_status_buf;
}

/* ---- WAV header ---------------------------------------------------------- */
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

static bool write_header(uint32_t data_bytes)
{
    uint8_t h[44];
    UINT bw;
    memcpy(h, "RIFF", 4);         put32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8); put32(h + 16, 16);
    put16(h + 20, 1);             /* PCM  */
    put16(h + 22, 1);             /* mono */
    put32(h + 24, MIC_SAMPLE_RATE);
    put32(h + 28, MIC_SAMPLE_RATE * 2);
    put16(h + 32, 2);             /* block align */
    put16(h + 34, 16);            /* bits        */
    memcpy(h + 36, "data", 4);    put32(h + 40, data_bytes);

    FSIZE_t pos = f_tell(&SDFile);
    if (f_lseek(&SDFile, 0) != FR_OK) return false;
    if (f_write(&SDFile, h, sizeof(h), &bw) != FR_OK || bw != sizeof(h)) return false;
    if (pos > sizeof(h) && f_lseek(&SDFile, pos) != FR_OK) return false;
    return true;
}

/* ---- FatFs timestamp from the RV-3028 (called by get_fattime) ----------- */
DWORD Recorder_FatTime(void)
{
    rv3028_time_t t;
    if (rv3028_get_time(&rtc_handle, &t) != HAL_OK)
        return ((DWORD)(2026 - 1980) << 25) | (1U << 21) | (1U << 16);
    return ((DWORD)(t.year + 2000 - 1980) << 25) | ((DWORD)t.month << 21) |
           ((DWORD)t.date << 16) | ((DWORD)t.hours << 11) |
           ((DWORD)t.minutes << 5) | ((DWORD)t.seconds >> 1);
}

static void release_storage(void)
{
    if (s_mounted) { f_mount(NULL, SDPath, 0); s_mounted = false; }
    MSC_Unlock();
}

static bool fail(const char *why)
{
    s_status = why;
    Mic_Stop();
    f_close(&SDFile);
    release_storage();
    s_running = false;
    return false;
}

/* ---- Public --------------------------------------------------------------- */
bool Recorder_Start(void)
{
    rv3028_time_t t;
    FRESULT fr;

    if (s_running) return true;

    if (!MSC_Lock()) { s_status = "USB busy, try again"; return false; }

    fr = f_mount(&SDFatFS, SDPath, 1);
    if (fr != FR_OK) {
        s_status = fr_status("mount", fr);
        f_mount(NULL, SDPath, 0);
        MSC_Unlock();
        return false;
    }
    s_mounted = true;

    fr = f_mkdir(REC_DIR);
    if (fr != FR_OK && fr != FR_EXIST) return fail(fr_status("mkdir", fr));

    if (rv3028_get_time(&rtc_handle, &t) == HAL_OK) {
        snprintf(s_name, sizeof s_name, REC_DIR "/%s%s%02u%02u%02u_%02u%02u%02u.WAV",
                 s_label ? s_labels[s_label] : "R", s_label ? "_" : "",
                 t.year, t.month, t.date, t.hours, t.minutes, t.seconds);
    } else {
        snprintf(s_name, sizeof s_name, REC_DIR "/%s%s%08lu.WAV",
                 s_label ? s_labels[s_label] : "R", s_label ? "_" : "", (unsigned long)HAL_GetTick());
    }

    fr = f_open(&SDFile, s_name, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return fail(fr_status("open", fr));
    s_data_bytes = 0;
    if (!write_header(0)) return fail("write failed");

    if (!Mic_Start()) return fail("mic start failed");

    s_running   = true;
    s_last_sync = HAL_GetTick();
    s_status    = "ok";
    return true;
}

static bool flush_audio(uint32_t min_samples)
{
    while (Mic_Available() >= min_samples && Mic_Available() > 0) {
        uint32_t n = Mic_Read(s_buf, CHUNK_SAMPLES);
        UINT bw;
        if (f_write(&SDFile, s_buf, n * 2, &bw) != FR_OK || bw != n * 2) return false;
        s_data_bytes += bw;
    }
    return true;
}

void Recorder_Task(void)
{
    if (!s_running) return;

    if (!flush_audio(CHUNK_SAMPLES)) { fail("write failed (flash full?)"); return; }

    if (HAL_GetTick() - s_last_sync >= SYNC_PERIOD_MS) {
        s_last_sync = HAL_GetTick();
        if (!write_header(s_data_bytes) || f_sync(&SDFile) != FR_OK) fail("sync failed");
    }
}

void Recorder_Stop(void)
{
    if (!s_running) return;
    Mic_Stop();
    bool ok = flush_audio(1) && write_header(s_data_bytes);
    if (f_close(&SDFile) != FR_OK) ok = false;
    s_status = ok ? (Mic_Overruns() ? "ok (some audio dropped)" : "ok") : "close failed";
    s_running = false;
    release_storage();
}

bool Recorder_Format(void)
{
    FRESULT fr;

    if (s_running) { s_status = "stop recording first"; return false; }
    if (!MSC_Lock()) { s_status = "USB busy, try again"; return false; }

    /* MBR + one FAT32 partition, 4 KB clusters (fast to format, plenty of
     * clusters for FAT32 on the 484 MB flash). s_buf is reused as the
     * work area since nothing is recording. */
    fr = f_mkfs(SDPath, FM_FAT32, 4096, s_buf, sizeof(s_buf));
    if (fr == FR_OK) {
        fr = f_mount(&SDFatFS, SDPath, 1);
        if (fr == FR_OK) {
            f_mkdir(REC_DIR);
            f_mount(NULL, SDPath, 0);
        }
    }
    s_status = (fr == FR_OK) ? "ok" : fr_status("format", fr);
    MSC_Unlock();               /* PC is told the medium changed */
    return fr == FR_OK;
}

bool Recorder_Toggle(void)
{
    if (s_running) { Recorder_Stop(); return false; }
    return Recorder_Start();
}

const char *Recorder_GetLabel(void) { return s_labels[s_label]; }
void        Recorder_NextLabel(void) { s_label = (uint8_t)((s_label + 1) % (sizeof s_labels / sizeof s_labels[0])); }

bool        Recorder_IsRunning(void) { return s_running; }
uint32_t    Recorder_Seconds(void)   { return s_data_bytes / (MIC_SAMPLE_RATE * 2U); }
const char *Recorder_FileName(void)  { return s_name; }
const char *Recorder_Status(void)    { return s_status; }
