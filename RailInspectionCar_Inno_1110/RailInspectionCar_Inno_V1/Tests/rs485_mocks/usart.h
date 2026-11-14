#ifndef TEST_RS485_USART_H
#define TEST_RS485_USART_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t SR, CR1, CR3; } UART_TypeDef;
typedef struct { uint32_t CR, NDTR; } DMA_Stream_TypeDef;
typedef struct {
    DMA_Stream_TypeDef *Instance;
    void *Parent;
    struct { uint32_t Mode, Channel; } Init;
} DMA_HandleTypeDef;
typedef struct {
    UART_TypeDef *Instance;
    DMA_HandleTypeDef *hdmarx;
    uint32_t ErrorCode, RxState;
} UART_HandleTypeDef;
typedef struct { uint32_t MODER, AFR[2]; } GPIO_TypeDef;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart5;
uint32_t HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *uart);
extern UART_TypeDef test_uart1;
extern UART_TypeDef test_uart5;
#define USART1 (&test_uart1)
#define USART2 (huart2.Instance)
#define UART5 (&test_uart5)
extern DMA_Stream_TypeDef test_stream;
extern GPIO_TypeDef test_gpio;
#define DMA1_Stream5 (&test_stream)
#define GPIOA (&test_gpio)
#define DMA_SxCR_EN 1U
#define DMA_SxCR_CIRC (1U << 8)
#define DMA_CIRCULAR DMA_SxCR_CIRC
#define DMA_CHANNEL_4 (4U << 25)
#define USART_CR3_DMAR (1U << 6)
#define UART_IT_IDLE (1U << 4)
#define UART_IT_PE (1U << 8)
#define UART_IT_ERR (0x30000000U | 1U)
#define UART_IT_TC (1U << 6)
#define UART_FLAG_IDLE (1U << 4)
#define UART_FLAG_TC (1U << 6)
#define RESET 0U
#define UART_FLAG_PE 1U
#define UART_FLAG_FE 2U
#define UART_FLAG_NE 4U
#define UART_FLAG_ORE 8U
#define HAL_UART_ERROR_PE 1U
#define HAL_UART_ERROR_NE 2U
#define HAL_UART_ERROR_FE 4U
#define HAL_UART_ERROR_ORE 8U
#define HAL_UART_ERROR_DMA 16U
/* STM32 HAL UARTEx receive event values used by the production IRQ bridge. */
#ifndef HAL_UART_RXEVENT_TC
#define HAL_UART_RXEVENT_TC 0U
#endif
#ifndef HAL_UART_RXEVENT_HT
#define HAL_UART_RXEVENT_HT 1U
#endif
#ifndef HAL_UART_RXEVENT_IDLE
#define HAL_UART_RXEVENT_IDLE 2U
#endif
#define READ_REG(reg) (reg)
#define __HAL_DMA_GET_COUNTER(h) ((h)->Instance->NDTR)
#define TEST_UART_IT_REG(h, it) (*(((it) == UART_IT_ERR) ? &(h)->Instance->CR3 : &(h)->Instance->CR1))
#define __HAL_UART_ENABLE_IT(h, it) (TEST_UART_IT_REG(h, it) |= ((it) & 0xFFFFU))
#define __HAL_UART_DISABLE_IT(h, it) (TEST_UART_IT_REG(h, it) &= ~((it) & 0xFFFFU))
#define __HAL_UART_GET_IT_SOURCE(h, it) (TEST_UART_IT_REG(h, it) & ((it) & 0xFFFFU))
#define __HAL_UART_GET_FLAG(h, flag) ((h)->Instance->SR & (flag))
#define __HAL_UART_CLEAR_IDLEFLAG(h) ((h)->Instance->SR &= ~(UART_FLAG_IDLE | 15U))
void HAL_UART_IRQHandler(UART_HandleTypeDef *uart);
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *);
#endif
