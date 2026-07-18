#ifndef UART_CMD_H
#define UART_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h" // Ensures HAL and UART_HandleTypeDef are defined

// Initialize the UART command processor (starts listening)
void UART_Cmd_Init(UART_HandleTypeDef *huart);

// Process any pending commands (call this in your main while(1) loop)
void Process_UART_Commands(void);

// Must be called from the HAL_UART_RxCpltCallback in main.c or stm32xxx_it.c
void UART_Cmd_RxCallback(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif // UART_CMD_H