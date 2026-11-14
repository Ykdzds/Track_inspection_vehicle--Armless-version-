#ifndef TEST_NRF_MAIN_H
#define TEST_NRF_MAIN_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t CR, FCR, NDTR; } DMA_Stream_TypeDef;
typedef struct DMA_HandleTypeDef DMA_HandleTypeDef;
struct DMA_HandleTypeDef {
    DMA_Stream_TypeDef *Instance;
    uint32_t State, Lock, ErrorCode;
    uint32_t Flags;
    void (*XferCpltCallback)(DMA_HandleTypeDef *);
    void (*XferHalfCpltCallback)(DMA_HandleTypeDef *);
    void (*XferErrorCallback)(DMA_HandleTypeDef *);
};
typedef struct { uint32_t CR1, CR2, SR; } SPI_TypeDef;
typedef struct {
    SPI_TypeDef *Instance;
    DMA_HandleTypeDef *hdmarx, *hdmatx;
    uint32_t State, Lock, ErrorCode, TxXferCount, RxXferCount;
} SPI_HandleTypeDef;
#define CE_GPIO_Port NULL
#define CSN_GPIO_Port NULL
#define CE_Pin 1U
#define CSN_Pin 2U
#define SPI_CR2_RXDMAEN 1U
#define SPI_CR2_TXDMAEN 2U
#define SPI_CR2_ERRIE 4U
#define DMA_SxCR_EN 1U
#define SPI_FLAG_OVR 0x40U
#define SPI_FLAG_BSY 0x80U
#define RESET 0U
#define HAL_DMA_STATE_READY 1U
#define HAL_SPI_STATE_READY 1U
#define HAL_UNLOCKED 0U
#define HAL_DMA_ERROR_NONE 0U
#define HAL_DMA_ERROR_TE 1U
#define HAL_DMA_ERROR_DME 2U
#define HAL_DMA_ERROR_FE 4U
#define DMA_IT_TC 0x10U
#define DMA_IT_HT 0x20U
#define DMA_IT_TE 0x40U
#define DMA_IT_DME 0x80U
#define DMA_IT_FE 0x100U
#define HAL_SPI_ERROR_NONE 0U
#define CLEAR_BIT(reg, bits) ((reg) &= ~(bits))
#define __HAL_SPI_DISABLE(h) test_spi_disable(h)
#define __HAL_DMA_DISABLE(h) ((h)->Instance->CR &= ~DMA_SxCR_EN)
#define __HAL_DMA_CLEAR_FLAG(h, flags) ((h)->Flags &= ~(flags))
#define __HAL_DMA_GET_FLAG(h, flags) ((h)->Flags & (flags))
#define __HAL_DMA_GET_IT_SOURCE(h, it) ((it) == DMA_IT_FE ? \
    (h)->Instance->FCR & (it) : (h)->Instance->CR & (it))
#define __HAL_DMA_DISABLE_IT(h, it) ((it) == DMA_IT_FE ? \
    ((h)->Instance->FCR &= ~(it)) : ((h)->Instance->CR &= ~(it)))
#define __HAL_DMA_GET_TC_FLAG_INDEX(h) 1U
#define __HAL_DMA_GET_HT_FLAG_INDEX(h) 2U
#define __HAL_DMA_GET_TE_FLAG_INDEX(h) 4U
#define __HAL_DMA_GET_DME_FLAG_INDEX(h) 8U
#define __HAL_DMA_GET_FE_FLAG_INDEX(h) 16U
#define __HAL_SPI_CLEAR_OVRFLAG(h) ((h)->Instance->SR &= ~SPI_FLAG_OVR)
#define __HAL_SPI_GET_FLAG(h, flag) ((h)->Instance->SR & (flag))
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(void *port, uint32_t pin, GPIO_PinState value);
void test_spi_disable(SPI_HandleTypeDef *spi);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(
    SPI_HandleTypeDef *spi, uint8_t *tx, uint8_t *rx, uint16_t size);
#endif
