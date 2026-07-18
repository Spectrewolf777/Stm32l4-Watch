#include "uart_cmd.h"
#include "rv_3028_c7.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "UI/ui.h"
#include "main.h"

// Bring in the RTC handle initialized in main.c
extern rv3028_handle_t rtc_handle;

// Pointer to the UART handle used for nRF52 communication
static UART_HandleTypeDef *nrf_uart_handle = NULL;

extern void USB_Print(const char *format, ...);




/* ============================================================================ */
/* Ring Buffer for UART RX                                                      */
/* ============================================================================ */
#define UART_RX_BUF_SIZE 256
static volatile uint8_t rx_ring_buf[UART_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;
static uint8_t rx_temp_byte; // Used by HAL_UART_Receive_IT

/* ============================================================================ */
/* Command Buffer                                                               */
/* ============================================================================ */
#define MAX_CMD_LEN 64
static char cmd_buffer[MAX_CMD_LEN];
static uint8_t cmd_idx = 0;

/* ============================================================================ */
/* Command Handlers (Add new handler prototypes here)                           */
/* ============================================================================ */
static void cmd_help(const char *args);
static void cmd_set_time(const char *args);
static void cmd_set_date(const char *args);


void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    // If the error occurred on our nRF52 UART connection
    if (huart == nrf_uart_handle) 
    {
        // The HAL driver automatically clears the error flags for us,
        // but it aborts the reception. We just need to restart it!
        HAL_UART_Receive_IT(nrf_uart_handle, (uint8_t*)&rx_temp_byte, 1);
    }
}





typedef struct {
    const char *cmd;
    const char *help_text;
    void (*handler)(const char *args);
} UartCommand_t;

static const UartCommand_t commands[] = {
    {"-h", "Show this help menu", cmd_help},
    {"-t", "Set time. Format: -t HH:MM:SS", cmd_set_time},
    {"-d", "Set date. Format: -d DD/MM/YY W (1=Mon, 7=Sun)", cmd_set_date}
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

/* ============================================================================ */
/* Helper Functions                                                             */
/* ============================================================================ */

// Send a string back to the nRF52
static void uart_print(const char *str) {
    if (nrf_uart_handle == NULL) return;
    //HAL_UART_Transmit(nrf_uart_handle, (uint8_t*)str, strlen(str), HAL_MAX_DELAY); //add later when you fix nrf52 to rx info to display on phone
    USB_Print(str);
}

// Formatted print back to nRF52
static void uart_printf(const char *format, ...) {
    char buf[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    uart_print(buf);
}

// Check if data is available in the ring buffer
static bool uart_available(void) {
    return (rx_head != rx_tail);
}

// Read one character from the ring buffer
static char uart_read_char(void) {
    if (rx_head == rx_tail) return 0; // Empty
    char c = rx_ring_buf[rx_tail];
    rx_tail = (rx_tail + 1) % UART_RX_BUF_SIZE;
    return c;
}

/* ============================================================================ */
/* Handler Implementations                                                      */
/* ============================================================================ */

static void cmd_help(const char *args) {
    uart_print("\r\n--- nRF52 Supported Commands ---\r\n");
    for (int i = 0; i < NUM_COMMANDS; i++) {
        uart_print(commands[i].cmd);
        uart_print(" : ");
        uart_print(commands[i].help_text);
        uart_print("\r\n");
    }
    uart_print("--------------------------------\r\n");
}

static void cmd_set_time(const char *args) {
    int h = 0, m = 0, s = 0;
    if (sscanf(args, "%d:%d:%d", &h, &m, &s) == 3) {
        rv3028_time_t new_time;
        rv3028_get_time(&rtc_handle, &new_time); 
        
        new_time.hours = (uint8_t)h;
        new_time.minutes = (uint8_t)m;
        new_time.seconds = (uint8_t)s;
        
        if (rv3028_set_time(&rtc_handle, &new_time) == HAL_OK) {
            uart_printf("\r\n[SUCCESS] Time updated to %02d:%02d:%02d\r\n", 
                        new_time.hours, new_time.minutes, new_time.seconds);
                   
            // Update the LVGL UI immediately
            uint8_t display_hour = new_time.hours % 12;
            if (display_hour == 0) display_hour = 12;

            char time_buf[16];
            snprintf(time_buf, sizeof(time_buf), "%d:%02d", display_hour, new_time.minutes);
            lv_label_set_text(ui_Time, time_buf);
        } else {
            uart_print("\r\n[ERROR] Failed to communicate with RTC.\r\n");
        }
    } else {
        uart_print("\r\n[ERROR] Invalid format. Use: -t HH:MM:SS\r\n");
    }
}

static void cmd_set_date(const char *args) {
    int d = 0, m = 0, y = 0, w = 0;
    if (sscanf(args, "%d/%d/%d %d", &d, &m, &y, &w) == 4) {
        rv3028_time_t new_time;
        rv3028_get_time(&rtc_handle, &new_time); 
        
        new_time.date = (uint8_t)d;
        new_time.month = (uint8_t)m;
        new_time.year = (uint8_t)y;
        new_time.weekday = (uint8_t)w;
        
        if (rv3028_set_time(&rtc_handle, &new_time) == HAL_OK) {
            uart_printf("\r\n[SUCCESS] Date updated to %02d/%02d/%02d (Weekday: %d)\r\n", 
                        new_time.date, new_time.month, new_time.year, new_time.weekday);
        } else {
            uart_print("\r\n[ERROR] Failed to communicate with RTC.\r\n");
        }
    } else {
        uart_print("\r\n[ERROR] Invalid format. Use: -d DD/MM/YY W\r\n");
    }
}

/* ============================================================================ */
/* Initialization & Interrupt Handling                                          */
/* ============================================================================ */

void UART_Cmd_Init(UART_HandleTypeDef *huart) {
    nrf_uart_handle = huart;
    // Start listening for the first byte via interrupt
    HAL_UART_Receive_IT(nrf_uart_handle, &rx_temp_byte, 1);
}

void Process_UART_Commands(void) {
    static uint32_t last_rx_time = 0;
    bool ready_to_execute = false;

    // 1. Read all available bytes from the ring buffer
    while (uart_available()) {
        last_rx_time = HAL_GetTick(); // Record the exact time we got a character
        char c = uart_read_char();
        
        // Ignore invisible/garbage characters at the start of a command
        if (cmd_idx == 0 && (c < 32 || c > 126)) {
            continue; 
        }
        
        // If we get a real newline character
        if (c == '\r' || c == '\n') {
            ready_to_execute = true;
        } 
        else if (cmd_idx < MAX_CMD_LEN - 1) {
            cmd_buffer[cmd_idx++] = c;
        } 
        else {
            cmd_idx = 0; // Buffer overflow safeguard
        }
    }

    // 2. Idle Timeout: If we have text waiting, but no real "Enter" key was sent,
    // wait 50ms. If no new bytes arrive, assume the BLE packet is finished.
    if (cmd_idx > 0 && !ready_to_execute) {
        if ((HAL_GetTick() - last_rx_time) > 50) {
            ready_to_execute = true;
        }
    }

    // 3. Execute the command
    if (ready_to_execute && cmd_idx > 0) {
        
        // Clean up if the user manually typed a literal "/n" or "\n" at the end
        if (cmd_idx >= 2 && (cmd_buffer[cmd_idx-2] == '\\' || cmd_buffer[cmd_idx-2] == '/') && cmd_buffer[cmd_idx-1] == 'n') {
            cmd_idx -= 2;
        }

        cmd_buffer[cmd_idx] = '\0'; // Null-terminate the string
        
        // Debug: Print the full command once
        USB_Print("\r\nUART CMD RX: '%s'\r\n", cmd_buffer);

        char *cmd = strtok(cmd_buffer, " ");
        char *args = strtok(NULL, ""); 
        if (args == NULL) { args = ""; }
        
        bool found = false;
        for (int i = 0; i < NUM_COMMANDS; i++) {
            if (strcmp(cmd, commands[i].cmd) == 0) {
                commands[i].handler(args);
                found = true;
                break;
            }
        }

        if (!found) {
            uart_printf("\r\n[ERROR] Unknown command: '%s'. Type -h for help.\r\n", cmd);
            USB_Print("[ERROR] Unknown nRF52 command: '%s'\r\n", cmd);
        }
        
        cmd_idx = 0; // Reset buffer for the next command
    }
}

// Feed a byte directly into the command ring buffer
void UART_Cmd_FeedByte(uint8_t c) {
    uint16_t next_head = (rx_head + 1) % UART_RX_BUF_SIZE;
    if (next_head != rx_tail) {
        rx_ring_buf[rx_head] = c;
        rx_head = next_head;
    }
}