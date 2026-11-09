/** @file recorder.h  Voice recorder: mic -> 16 kHz 16-bit mono WAV on the
 *  XTSD flash (FatFs), files saved as /REC/RYYMMDD_HHMMSS.WAV.
 *
 *  While recording, USB mass storage reports "medium not present" so the PC
 *  and the watch never write the filesystem at the same time. When recording
 *  stops the PC is told the medium changed and re-reads it.
 *  The flash must already be FAT formatted (format it from the PC). */
#ifndef RECORDER_H
#define RECORDER_H

#include <stdbool.h>
#include <stdint.h>

bool        Recorder_Start(void);
void        Recorder_Stop(void);
bool        Recorder_Toggle(void);
/* ERASES the flash: new MBR + FAT32 volume with a REC folder */
bool        Recorder_Format(void);
void        Recorder_Task(void);        /* main loop: writes audio to flash */
bool        Recorder_IsRunning(void);
/* Label for sound-AI training recordings: "" (voice memo), ALARM, KNOCK, BELL, SIREN, BG.
 * Used as the file name prefix, e.g. REC/KNOCK_261108_113000.WAV */
const char *Recorder_GetLabel(void);
void        Recorder_NextLabel(void);
uint32_t    Recorder_Seconds(void);
const char *Recorder_FileName(void);     /* current / last file  */
const char *Recorder_Status(void);       /* last error or "ok"   */

#endif
