#ifndef TEST_DEBUG_UART_USART_H
#define TEST_DEBUG_UART_USART_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct {uint32_t SR,DR,CR1,CR3;} UART_TypeDef;
typedef struct {uint32_t NDTR,flags;} DMA_Stream_TypeDef;
typedef struct {DMA_Stream_TypeDef *Instance; uint32_t ErrorCode;} DMA_HandleTypeDef;
typedef struct {UART_TypeDef *Instance; DMA_HandleTypeDef *hdmarx,*hdmatx;uint32_t ErrorCode;} UART_HandleTypeDef;
extern UART_HandleTypeDef huart1;
#define __NVIC_PRIO_BITS 4U
#define UART_FLAG_PE 1U
#define UART_FLAG_FE 2U
#define UART_FLAG_NE 4U
#define UART_FLAG_ORE 8U
#define HAL_UART_ERROR_PE 1U
#define HAL_UART_ERROR_NE 2U
#define HAL_UART_ERROR_FE 4U
#define HAL_UART_ERROR_ORE 8U
#define HAL_UART_ERROR_DMA 16U
#define HAL_DMA_ERROR_NONE 0U
#define HAL_UART_RXEVENT_TC 0U
#define HAL_UART_RXEVENT_HT 1U
#define HAL_UART_RXEVENT_IDLE 2U
#define __HAL_DMA_GET_TC_FLAG_INDEX(dma) (1U)
#define __HAL_DMA_GET_HT_FLAG_INDEX(dma) (2U)
#define __HAL_DMA_GET_FLAG(dma,flag) ((dma)->Instance->flags & (flag))
#define __HAL_DMA_CLEAR_FLAG(dma,flag) ((dma)->Instance->flags &= ~(flag))
#define __HAL_DMA_GET_COUNTER(dma) ((dma)->Instance->NDTR)
#define __HAL_UART_CLEAR_OREFLAG(h) ((h)->Instance->SR=0U)
typedef struct {uint32_t CTRL,CYCCNT;} test_dwt_t;
typedef struct {uint32_t DEMCR;} test_core_t;
extern test_dwt_t test_dwt;
extern test_core_t test_core;
extern uint32_t SystemCoreClock;
#define DWT (&test_dwt)
#define CoreDebug (&test_core)
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
uint32_t __get_BASEPRI(void);
void __set_BASEPRI_MAX(uint32_t value);
void __set_BASEPRI(uint32_t value);
uint32_t __get_IPSR(void);
void test_dmb(void);
#define __DMB() test_dmb()
#define __DSB() ((void)0)
#define __ISB() ((void)0)
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortTransmit_IT(UART_HandleTypeDef *);
#endif
