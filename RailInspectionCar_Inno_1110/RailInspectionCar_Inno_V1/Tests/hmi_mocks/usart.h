#ifndef TEST_HMI_USART_H
#define TEST_HMI_USART_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
#define HAL_UART_STATE_READY (0x20U)
#define HAL_UART_STATE_BUSY_RX (0x22U)
#define HAL_UART_STATE_BUSY_TX (0x21U)
#define HAL_UART_ERROR_PE (1U)
#define HAL_UART_ERROR_NE (2U)
#define HAL_UART_ERROR_FE (4U)
#define HAL_UART_ERROR_ORE (8U)
#define USART_CR1_RXNEIE (1U << 5)
#define USART_CR1_PEIE (1U << 8)
#define USART_CR1_TCIE (1U << 6)
#define USART_CR1_TXEIE (1U << 7)
#define USART_CR3_EIE (1U)
#define USART_SR_PE 1U
#define USART_SR_FE 2U
#define USART_SR_NE 4U
#define USART_SR_ORE 8U
#define USART_SR_RXNE 32U
#define HAL_UART_ERROR_NONE 0U
#define HAL_UART_ERROR_DMA 16U
#define HAL_DMA_ERROR_NONE 0U
#define HAL_DMA_STATE_READY 0U
#define DMA_NORMAL 0U
#define DMA_CIRCULAR (1U << 8)
#define DMA_CHANNEL_4 (4U << 25)
#define DMA_PERIPH_TO_MEMORY 0U
#define DMA_MEMORY_TO_PERIPH (1U << 6)
#define DMA_SxCR_EN 1U
#define DMA_SxCR_CIRC DMA_CIRCULAR
#define DMA_IT_TC 16U
#define DMA_IT_HT 8U
#define DMA_IT_TE 4U
#define DMA_IT_DME 2U
#define DMA_IT_FE 128U
#define USART_CR1_IDLEIE (1U << 4)
#define USART_CR3_DMAR (1U << 6)
#define USART_CR3_DMAT (1U << 7)
#define HAL_UART_RECEPTION_TOIDLE 1U
#define HAL_UART_RXEVENT_TC 0U
#define HAL_UART_RXEVENT_HT 1U
#define HAL_UART_RXEVENT_IDLE 2U
#define __ALIGN_BEGIN
#define __ALIGN_END __attribute__((aligned(4)))
#define __DMB() ((void)0)
typedef struct { uint32_t CR, NDTR, FCR, flags; } DMA_Stream_TypeDef;
typedef struct test_dma_handle DMA_HandleTypeDef;
struct test_dma_handle {
    DMA_Stream_TypeDef *Instance;
    void *Parent;
    struct { uint32_t Mode, Channel, Direction; } Init;
    uint32_t ErrorCode, State;
    void (*XferAbortCallback)(DMA_HandleTypeDef *);
};
extern DMA_Stream_TypeDef test_rx_stream, test_tx_stream;
#define DMA1_Stream0 (&test_rx_stream)
#define DMA1_Stream7 (&test_tx_stream)
#define DMA1_Stream0_IRQn 0
#define DMA1_Stream7_IRQn 7
#define __HAL_DMA_GET_COUNTER(dma) ((dma)->Instance->NDTR)
#define __HAL_DMA_GET_FLAG(dma, flag) ((dma)->Instance->flags & (flag))
#define __HAL_DMA_CLEAR_FLAG(dma, flag) ((dma)->Instance->flags &= ~(flag))
#define __HAL_DMA_GET_TC_FLAG_INDEX(dma) 16U
#define __HAL_DMA_GET_HT_FLAG_INDEX(dma) 8U
#define __HAL_DMA_GET_TE_FLAG_INDEX(dma) 4U
#define __HAL_DMA_GET_DME_FLAG_INDEX(dma) 2U
#define __HAL_DMA_GET_FE_FLAG_INDEX(dma) 1U
#define __HAL_UNLOCK(dma) ((void)(dma))
void HAL_NVIC_ClearPendingIRQ(int irq);
uint32_t HAL_RCC_GetPCLK1Freq(void);
typedef struct { uint32_t CFGR; } test_rcc_t;
extern test_rcc_t test_rcc;
#define RCC (&test_rcc)
#define RCC_CFGR_PPRE1 0x1C00U
typedef struct { uint32_t SR, DR, CR1, CR3; } test_uart_t;
typedef struct
{
    test_uart_t *Instance;
    uint8_t *pRxBuffPtr;
    uint32_t gState, RxState, ErrorCode;
    uint16_t RxXferCount, RxXferSize, TxXferCount;
    DMA_HandleTypeDef *hdmarx, *hdmatx;
    uint32_t ReceptionType;
} UART_HandleTypeDef;
void test_uart_clear(UART_HandleTypeDef *);
#define __HAL_UART_CLEAR_OREFLAG(u) test_uart_clear(u)
extern UART_HandleTypeDef huart5;
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *, const uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortTransmit_IT(UART_HandleTypeDef *);
#endif
