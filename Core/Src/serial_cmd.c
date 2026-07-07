#include "serial_cmd.h"
#include "tusb.h"
#include "rv_3028_c7.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "UI/ui.h"
// Bring in the RTC handle initialized in main.c
extern rv3028_handle_t rtc_handle;

#define MAX_CMD_LEN 64
static char cmd_buffer[MAX_CMD_LEN];
static uint8_t cmd_idx = 0;

/* ============================================================================ */
/* Command Handlers (Add new handler prototypes here)                           */
/* ============================================================================ */
static void cmd_help(const char *args);
static void cmd_set_time(const char *args);
static void cmd_set_date(const char *args);

/* ============================================================================ */
/* Command Table Structure                                                      */
/* ============================================================================ */
typedef struct {
    const char *cmd;                   // Command string (e.g., "-t")
    const char *help_text;             // Description for the help menu
    void (*handler)(const char *args); // Function pointer to the handler
} SerialCommand_t;

/* --- ADD NEW COMMANDS TO THIS TABLE --- */
static const SerialCommand_t commands[] = {
    {"-h", "Show this help menu", cmd_help},
    {"-t", "Set time. Format: -t HH:MM:SS", cmd_set_time},
    {"-d", "Set date. Format: -d DD/MM/YY W (W = Weekday (1=Monday, 7=Sunday))", cmd_set_date}
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

/* ============================================================================ */
/* Handler Implementations                                                      */
/* ============================================================================ */

// Send a complete string over USB CDC, blocking until all bytes are queued.
static void cdc_print(const char *str) {
    size_t len = strlen(str);
    size_t sent = 0;
    while (sent < len) {
        uint32_t written = tud_cdc_write(str + sent, len - sent);
        if (written == 0) {
            // Buffer full – let TinyUSB process the outgoing data
            tud_task();
            // A tiny delay can help if tud_task alone doesn't free space quickly
        } else {
            sent += written;
        }
    }
    // Force the final partial packet out immediately
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
                   
            // --- NEW: Update the LVGL UI immediately ---
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

/* ============================================================================ */
/* Main Processing Loop                                                         */
/* ============================================================================ */

void Process_Serial_Commands(void) 
{
    while (tud_cdc_available()) 
    {
        char c = tud_cdc_read_char();
        
        // Echo back for terminal usability
        tud_cdc_write_char(c);
        tud_cdc_write_flush();

        // Process upon Enter key
        if (c == '\r' || c == '\n') 
        {
            if (cmd_idx > 0) 
            {
                cmd_buffer[cmd_idx] = '\0'; 
                
                // 1. Extract the base command (e.g., "-t")
                char *cmd = strtok(cmd_buffer, " ");
                // 2. Extract the remaining string as arguments (e.g., "14:30:00")
                char *args = strtok(NULL, ""); 
                
                if (args == NULL) { args = ""; } // Prevent null pointer if no args given

                // 3. Search the command table
                bool found = false;
                for (int i = 0; i < NUM_COMMANDS; i++) 
                {
                    if (strcmp(cmd, commands[i].cmd) == 0) 
                    {
                        // Match found, execute the associated handler
                        commands[i].handler(args);
                        found = true;
                        break;
                    }
                }

                if (!found) 
                {
                    printf("\r\n[ERROR] Unknown command: '%s'. Type -h for help.\r\n", cmd);
                    fflush(stdout);
                }
                
                // Reset buffer for the next command
                cmd_idx = 0; 
            }
        } 
        else if (cmd_idx < MAX_CMD_LEN - 1) 
        {
            cmd_buffer[cmd_idx++] = c;
        }
    }
}