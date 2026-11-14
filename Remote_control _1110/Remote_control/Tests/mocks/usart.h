#ifndef TEST_REMOTE_USART_H
#define TEST_REMOTE_USART_H
#include "main.h"
typedef struct { uint32_t gState; } UART_HandleTypeDef;
#define HAL_UART_STATE_READY 0U
#define HAL_UART_STATE_BUSY_TX 1U
extern UART_HandleTypeDef huart1;
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart);
#endif
