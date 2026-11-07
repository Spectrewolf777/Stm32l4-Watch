#include "serial_cmd.h"
#include "tusb.h"
#include "main.h"
#include "rv_3028_c7.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "UI/ui.h"
#include "cst816t.h"
#include "i2c_dev.h"
#include "sensors.h"
#include "drv2605l.h"
#include "lis3mdl.h"
#include "bmp388.h"
#include "m24512.h"
#include "ui_watch.h"
#include "recorder.h"
#include "mic.h"
#include "power_mon.h"
#include "weather.h"
#include "bsp_driver_sd.h"

extern rv3028_handle_t rtc_handle;

#define MAX_CMD_LEN 64
static char cmd_buffer[MAX_CMD_LEN];
static uint8_t cmd_idx = 0;


static void cmd_help(const char *args);
static void cmd_set_time(const char *args);
static void cmd_set_date(const char *args);
static void cmd_touch(const char *args);
static void cmd_scan(const char *args);
static void cmd_devices(const char *args);
static void cmd_sensors(const char *args);
static void cmd_buzz(const char *args);
static void cmd_cal(const char *args);
static void cmd_settings(const char *args);
static void cmd_notify(const char *args);
static void cmd_weather(const char *args);
static void cmd_ble(const char *args);
static void cmd_rec(const char *args);
static void cmd_pwr(const char *args);
static void cmd_sd(const char *args);
static void cmd_log(const char *args);
static void cmd_format(const char *args);
static void cmd_wx(const char *args) { (void)args; Weather_PrintReport(); }
static void cmd_mag(const char *args) { (void)args; Compass_PrintDebug(); }


typedef struct {
    const char *cmd;                   // Command string (e.g., "-t")
    const char *help_text;             // Description for the help menu
    void (*handler)(const char *args); // Function pointer to the handler
} SerialCommand_t;

/* --- ADD NEW COMMANDS TO THIS TABLE --- */
static const SerialCommand_t commands[] = {
    {"-h", "Show this help menu", cmd_help},
    {"-t", "Set time. Format: -t HH:MM:SS", cmd_set_time},
    {"-d", "Set date. Format: -d DD/MM/YY W (W = Weekday (1=Monday, 7=Sunday))", cmd_set_date},
    {"-tp", "Touch info. '-tp on' / '-tp off' = print touches & gestures", cmd_touch},
    {"-scan", "Scan I2C1 and I2C2 for devices", cmd_scan},
    {"-dev", "Show which watch chips were detected", cmd_devices},
    {"-s", "Print all sensor readings", cmd_sensors},
    {"-buzz", "Haptic test. Format: -buzz [effect 1-123]", cmd_buzz},
    {"-cal", "Compass calibration: -cal start, rotate watch, -cal stop", cmd_cal},
    {"-set", "Settings: -set timeout <s> | sea <Pa> | haptics <0/1> | battery <0/1/2> | show", cmd_settings},
    {"-n", "Show notification. Format: -n <text>", cmd_notify},
    {"-w", "Set weather text. Format: -w <text>", cmd_weather},
    {"-ble", "Set BLE icon state. Format: -ble <0/1>", cmd_ble},
    {"-rec", "Voice recorder: -rec start | -rec stop | -rec (status)", cmd_rec},
    {"-pwr", "Show USB power status pin and battery", cmd_pwr},
    {"-sd", "Check the flash: card info + what filesystem is on it", cmd_sd},
    {"-log", "Print the time every second: -log on | -log off", cmd_log},
    {"-format", "ERASE flash and make it FAT32: -format yes", cmd_format},
    {"-mag", "Compass debug: raw magnetometer, accelerometer, heading", cmd_mag},
    {"-wx", "Barometer weather forecast + pressure history", cmd_wx}
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))


// Send a complete string over USB CDC, blocking until all bytes are queued.
static void cdc_print(const char *str) {
    size_t len = strlen(str);
    size_t sent = 0;
    while (sent < len) {
        uint32_t written = tud_cdc_write(str + sent, len - sent);
        if (written == 0) {
           
            tud_task();
           
        } else {
            sent += written;
        }
    }
    
    tud_cdc_write_flush();
}






static void cmd_help(const char *args) {
    cdc_print("\r\n--- Supported Commands ---\r\n");
    for (int i = 0; i < NUM_COMMANDS; i++) {
        cdc_print(commands[i].cmd);
        cdc_print(" : ");
        cdc_print(commands[i].help_text);
        cdc_print("\r\n");
    }
    cdc_print("--------------------------\r\n");
}

static void cmd_set_time(const char *args) 
{
    int h = 0, m = 0, s = 0;
    
    // Parse the arguments
    if (sscanf(args, "%d:%d:%d", &h, &m, &s) == 3) 
    {
        rv3028_time_t new_time;
        rv3028_get_time(&rtc_handle, &new_time); // Get current to keep date intact
        
        new_time.hours = (uint8_t)h;
        new_time.minutes = (uint8_t)m;
        new_time.seconds = (uint8_t)s;
        
        if (rv3028_set_time(&rtc_handle, &new_time) == HAL_OK) {
            printf("\r\n[SUCCESS] Time updated to %02d:%02d:%02d\r\n", 
                   new_time.hours, new_time.minutes, new_time.seconds);
                   
            
            // Convert to 12-hour format for the display
            uint8_t display_hour = new_time.hours % 12;
            if (display_hour == 0) display_hour = 12; // Handle midnight/noon

            // Format the string and push to the SquareLine label
            char time_buf[16];
            snprintf(time_buf, sizeof(time_buf), "%d:%02d", display_hour, new_time.minutes);
            lv_label_set_text(ui_Time, time_buf);
            // -------------------------------------------
            
        } else {
            printf("\r\n[ERROR] Failed to communicate with RTC.\r\n");
        }
    } 
    else 
    {
        printf("\r\n[ERROR] Invalid format. Use: -t HH:MM:SS\r\n");
    }
    fflush(stdout);
}



static void cmd_set_date(const char *args) 
{
    int d = 0, m = 0, y = 0, w = 0;
    
    // Parse the arguments (Format: DD/MM/YY W)
    if (sscanf(args, "%d/%d/%d %d", &d, &m, &y, &w) == 4) 
    {
        rv3028_time_t new_time;
        rv3028_get_time(&rtc_handle, &new_time); // Get current to keep time intact
        
        new_time.date = (uint8_t)d;
        new_time.month = (uint8_t)m;
        new_time.year = (uint8_t)y;
        new_time.weekday = (uint8_t)w;
        
        if (rv3028_set_time(&rtc_handle, &new_time) == HAL_OK) {
            printf("\r\n[SUCCESS] Date updated to %02d/%02d/%02d (Weekday: %d)\r\n", 
                   new_time.date, new_time.month, new_time.year, new_time.weekday);
        } else {
            printf("\r\n[ERROR] Failed to communicate with RTC.\r\n");
        }
    } 
    else 
    {
        printf("\r\n[ERROR] Invalid format. Use: -d DD/MM/YY W\r\n");
    }
    fflush(stdout);
}


static void cmd_touch(const char *args)
{
    const cst816t_info_t *info = CST816T_GetInfo();
    uint16_t x, y;
    bool pressed = CST816T_GetPoint(&x, &y);

    if (strcmp(args, "on") == 0)  CST816T_SetDebug(true);
    if (strcmp(args, "off") == 0) CST816T_SetDebug(false);

    printf("\r\n[TOUCH] %s chip=0x%02X proj=0x%02X fw=0x%02X last=%u,%u %s\r\n",
           info->present ? "found" : "NOT FOUND",
           info->chip_id, info->proj_id, info->fw_version,
           x, y, pressed ? "(pressed)" : "");
    if (args[0]) printf("[TOUCH] debug print %s\r\n", args);
    fflush(stdout);
}


static void cmd_scan(const char *args)    { (void)args; I2CDev_ScanPrint(); }
static void cmd_devices(const char *args) { (void)args; Sensors_PrintStatus(); }
static void cmd_sensors(const char *args) { (void)args; Sensors_PrintReport(); }

static void cmd_buzz(const char *args)
{
    int fx = HAPTIC_CLICK;
    sscanf(args, "%d", &fx);
    if (fx < 1 || fx > 123) fx = HAPTIC_CLICK;
    printf("\r\n[HAPTIC] effect %d %s\r\n", fx,
           DRV2605L_Play((uint8_t)fx) ? "playing" : "FAILED (chip not found)");
    fflush(stdout);
}

static void cmd_cal(const char *args)
{
    if (strcmp(args, "start") == 0) {
        Compass_CalStart();
        printf("\r\n[COMPASS] Calibrating: spin flat 360, then tilt/flip every way, then -cal stop\r\n");
    } else if (strcmp(args, "stop") == 0) {
        uint8_t prog = Compass_CalProgress();
        compass_cal_result_t r = Compass_CalStop(true);
        if (r == CAL_FAILED) {
            printf("\r\n[COMPASS] Not enough flat rotation (%u%%), old calibration kept. Try again.\r\n", prog);
            fflush(stdout);
            return;
        }
        watch_settings_t *c = Settings_Get();
        printf("\r\n[COMPASS] %s (axis map %u). Now do Set North on the watch.\r\n",
               r == CAL_OK_TILT ? "Tilt compensation ON" : "Flat only, tilt compensation OFF",
               c->compass_tilt_map);
        printf("[COMPASS] Saved offsets %d %d %d  scale %d %d %d\r\n",
               c->mag_offset[0], c->mag_offset[1], c->mag_offset[2],
               c->mag_scale[0], c->mag_scale[1], c->mag_scale[2]);
    } else {
        printf("\r\n[ERROR] Use: -cal start | -cal stop\r\n");
    }
    fflush(stdout);
}

static void cmd_settings(const char *args)
{
    watch_settings_t *c = Settings_Get();
    char key[16] = {0};
    long val = 0;
    int n = sscanf(args, "%15s %ld", key, &val);

    if (n == 2 && strcmp(key, "timeout") == 0 && val >= 3 && val <= 600) {
        c->screen_timeout_s = (uint16_t)val;
    } else if (n == 2 && strcmp(key, "sea") == 0 && val > 80000 && val < 120000) {
        c->sea_level_pa = (uint32_t)val;
        BMP388_SetSeaLevel(c->sea_level_pa);
    } else if (n == 2 && strcmp(key, "haptics") == 0) {
        c->haptics_on = val ? 1 : 0;
    } else if (n == 2 && strcmp(key, "battery") == 0 && val >= 0 && val <= 2) {
        c->battery_mode = (uint8_t)val;
    } else if (n < 1 || strcmp(key, "show") != 0) {
        printf("\r\n[ERROR] Use: -set timeout <3-600> | sea <Pa> | haptics <0/1> | battery <0 auto/1 none/2 fitted> | show\r\n");
        fflush(stdout);
        return;
    }
    if (n == 2) {
        printf("\r\n[SETTINGS] %s\r\n", Settings_Save() ? "saved to EEPROM" : "applied (EEPROM not found, not saved)");
    }
    printf("[SETTINGS] timeout=%us sea=%luPa haptics=%u battery=%s\r\n", c->screen_timeout_s,
           (unsigned long)c->sea_level_pa, c->haptics_on,
           c->battery_mode == 1 ? "none" : c->battery_mode == 2 ? "fitted" : "auto");
    fflush(stdout);
}

static void cmd_rec(const char *args)
{
    if (strcmp(args, "start") == 0) Recorder_Start();
    else if (strcmp(args, "stop") == 0) Recorder_Stop();
    UI_RecorderChanged();

    printf("\r\n[REC] %s  %lus  file=%s  status=%s  dropped=%lu\r\n",
           Recorder_IsRunning() ? "RECORDING" : "idle",
           (unsigned long)Recorder_Seconds(), Recorder_FileName(), Recorder_Status(),
           (unsigned long)Mic_Overruns());
    fflush(stdout);
}

static void cmd_pwr(const char *args)
{
    (void)args;
    const sensors_data_t *d = Sensors_Get();
    printf("\r\n[PWR] LM66200 ST pin=%u -> %s\r\n",
           Power_StRaw(), Power_OnUsb() ? "on USB" : "on battery");
    printf("[PWR] battery %s: %u%% %u mV %s\r\n", d->bat_present ? "present" : "NOT detected",
           d->bat_percent, d->bat_mv, d->bat_charging ? "charging" : "");
    fflush(stdout);
}

extern SD_HandleTypeDef hsd2;
uint8_t  SD_Glue_ReadSector0(uint8_t out[512]);
uint32_t SD_Glue_LastError(void);

static void print_fs_type(const uint8_t *b)
{
    if (memcmp(b + 3, "EXFAT   ", 8) == 0)       printf("exFAT  <-- not supported, reformat as FAT32\r\n");
    else if (memcmp(b + 82, "FAT32", 5) == 0)    printf("FAT32 (OK)\r\n");
    else if (memcmp(b + 54, "FAT", 3) == 0)      printf("FAT12/16 (OK)\r\n");
    else if (memcmp(b + 3, "NTFS", 4) == 0)      printf("NTFS  <-- not supported, reformat as FAT32\r\n");
    else                                         printf("unknown / not formatted\r\n");
}

static void cmd_sd(const char *args)
{
    (void)args;
    static uint8_t sec[512] __attribute__((aligned(4)));
    HAL_SD_CardInfoTypeDef ci;

    if (Recorder_IsRunning()) { printf("\r\n[SD] busy recording\r\n"); fflush(stdout); return; }

    HAL_SD_GetCardInfo(&hsd2, &ci);
    printf("\r\n[SD] blocks=%lu x %lu B (%lu MB)  state=%lu  hal_err=0x%lX\r\n",
           (unsigned long)ci.BlockNbr, (unsigned long)ci.BlockSize,
           (unsigned long)((uint64_t)ci.BlockNbr * ci.BlockSize / 1048576ULL),
           (unsigned long)HAL_SD_GetCardState(&hsd2), (unsigned long)hsd2.ErrorCode);

    if (SD_Glue_ReadSector0(sec) != 0) {
        printf("[SD] reading sector 0 FAILED, sd err=0x%lX\r\n", (unsigned long)SD_Glue_LastError());
        fflush(stdout);
        return;
    }
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        printf("[SD] sector 0 has no boot signature -> not formatted\r\n");
        fflush(stdout);
        return;
    }

    /* Sector 0 is either a volume boot record (jump instruction) or an MBR */
    if (sec[0] == 0xEB || sec[0] == 0xE9) {
        printf("[SD] no partition table, filesystem: ");
        print_fs_type(sec);
    } else {
        uint8_t  type  = sec[446 + 4];
        uint32_t start = sec[446 + 8] | (sec[446 + 9] << 8) | (sec[446 + 10] << 16) | ((uint32_t)sec[446 + 11] << 24);
        printf("[SD] MBR, partition 1 type=0x%02X start=%lu, filesystem: ", type, (unsigned long)start);
        if (type == 0xEE) { printf("GPT  <-- not supported, use MBR + FAT32\r\n"); fflush(stdout); return; }
        if (BSP_SD_ReadBlocks((uint32_t *)sec, start, 1, 0) != 0) {
            printf("read FAILED\r\n");
        } else {
            print_fs_type(sec);
        }
    }
    fflush(stdout);
}

static void cmd_format(const char *args)
{
    if (strcmp(args, "yes") != 0) {
        printf("\r\n[FORMAT] This ERASES everything on the watch flash.\r\n"
               "[FORMAT] Type: -format yes\r\n");
        fflush(stdout);
        return;
    }
    printf("\r\n[FORMAT] Formatting as FAT32, takes a few seconds...\r\n");
    fflush(stdout);
    tud_task();
    bool ok = Recorder_Format();
    printf("[FORMAT] %s\r\n", ok ? "done - FAT32 with REC folder created" : Recorder_Status());
    fflush(stdout);
}

extern bool g_time_log;   /* main.c */
static void cmd_log(const char *args)
{
    g_time_log = (strcmp(args, "on") == 0);
    printf("\r\n[LOG] time print %s\r\n", g_time_log ? "on" : "off");
    fflush(stdout);
}

static void cmd_notify(const char *args)  { UI_ShowNotification(args); }
static void cmd_weather(const char *args) { UI_SetWeather(args); }
static void cmd_ble(const char *args)     { UI_SetBleConnected(atoi(args) != 0); }


static void execute_command(void)
{
    cmd_buffer[cmd_idx] = '\0';
    cmd_idx = 0;

    // Extract the base command (e.g., "-t") and the rest as arguments
    char *cmd = strtok(cmd_buffer, " ");
    char *args = strtok(NULL, "");
    if (cmd == NULL) return;
    if (args == NULL) { args = ""; }

    for (int i = 0; i < NUM_COMMANDS; i++)
    {
        if (strcmp(cmd, commands[i].cmd) == 0)
        {
            commands[i].handler(args);
            return;
        }
    }
    printf("\r\n[ERROR] Unknown command: '%s'. Type -h for help.\r\n", cmd);
    fflush(stdout);
}

/* A command runs on Enter (\r or \n), or after CMD_IDLE_MS with no new
 * characters, so serial monitors that send without a line ending work too. */
#define CMD_IDLE_MS 300U

void Process_Serial_Commands(void)
{
    static uint32_t last_rx = 0;

    while (tud_cdc_available())
    {
        char c = tud_cdc_read_char();
        last_rx = HAL_GetTick();

        // Echo back for terminal usability
        tud_cdc_write_char(c);
        tud_cdc_write_flush();

        if (c == '\r' || c == '\n')
        {
            if (cmd_idx > 0) execute_command();
        }
        else if (cmd_idx < MAX_CMD_LEN - 1)
        {
            cmd_buffer[cmd_idx++] = c;
        }
    }

    if (cmd_idx > 0 && HAL_GetTick() - last_rx >= CMD_IDLE_MS)
    {
        execute_command();
    }
}
