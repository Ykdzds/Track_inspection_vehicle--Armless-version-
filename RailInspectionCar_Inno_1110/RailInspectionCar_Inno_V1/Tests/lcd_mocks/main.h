#ifndef TEST_LCD_MAIN_H
#define TEST_LCD_MAIN_H
#include <stdint.h>
#include <stddef.h>
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT} HAL_StatusTypeDef;
typedef enum {GPIO_PIN_RESET,GPIO_PIN_SET} GPIO_PinState;
typedef struct {uint32_t CR,FCR,NDTR;} DMA_Stream_TypeDef;
typedef struct {DMA_Stream_TypeDef *Instance;uint32_t State,Lock,ErrorCode,Flags;} DMA_HandleTypeDef;
typedef struct {uint32_t CR1,CR2,SR,DR;} SPI_TypeDef;
typedef struct {SPI_TypeDef *Instance;DMA_HandleTypeDef *hdmatx;uint32_t State,Lock,ErrorCode;uint16_t TxXferSize,TxXferCount;} SPI_HandleTypeDef;
typedef struct {uint32_t CTRL,CYCCNT;} DWT_Type;
typedef struct {uint32_t DEMCR;} CoreDebug_Type;
extern DWT_Type lcd_test_dwt;
extern CoreDebug_Type lcd_test_debug;
extern uint32_t SystemCoreClock;
#define DWT (&lcd_test_dwt)
#define CoreDebug (&lcd_test_debug)
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define __NOP() (++lcd_test_dwt.CYCCNT)
#define RESET 0U
#define DMA_SxCR_EN 1U
#define DMA_IT_TC 0x10U
#define DMA_IT_HT 0x20U
#define DMA_IT_TE 0x40U
#define DMA_IT_DME 0x80U
#define DMA_IT_FE 0x100U
#define SPI_CR2_TXDMAEN 2U
#define SPI_SR_TXE 2U
#define SPI_SR_BSY 0x80U
#define SPI_SR_MODF 0x20U
#define SPI_SR_FRE 0x100U
#define HAL_SPI_STATE_RESET 0U
#define HAL_SPI_STATE_READY 1U
#define HAL_SPI_STATE_BUSY_TX 2U
#define HAL_SPI_ERROR_NONE 0U
#define HAL_DMA_STATE_READY 1U
#define HAL_UNLOCKED 0U
#define HAL_DMA_ERROR_TE 1U
#define HAL_DMA_ERROR_DME 2U
#define HAL_DMA_ERROR_FE 4U
#define CLEAR_BIT(r,b) ((r)&=~(b))
#define SET_BIT(r,b) ((r)|=(b))
#define __HAL_SPI_DISABLE(h) ((h)->Instance->CR1&=~0x40U)
#define __HAL_SPI_ENABLE(h) ((h)->Instance->CR1|=0x40U)
#define SPI_1LINE_TX(h) ((h)->Instance->CR1|=0x8000U)
#define __HAL_DMA_DISABLE(h) lcd_test_dma_disable(h)
#define __HAL_DMA_DISABLE_IT(h,it) ((it)==DMA_IT_FE ? ((h)->Instance->FCR&=~(it)):((h)->Instance->CR&=~(it)))
#define __HAL_DMA_GET_TC_FLAG_INDEX(h) ((void)(h),1U)
#define __HAL_DMA_GET_HT_FLAG_INDEX(h) ((void)(h),2U)
#define __HAL_DMA_GET_TE_FLAG_INDEX(h) ((void)(h),4U)
#define __HAL_DMA_GET_DME_FLAG_INDEX(h) ((void)(h),8U)
#define __HAL_DMA_GET_FE_FLAG_INDEX(h) ((void)(h),16U)
#define __HAL_DMA_GET_FLAG(h,f) ((h)->Flags&(f))
#define __HAL_DMA_CLEAR_FLAG(h,f) ((h)->Flags&=~(f))
#define __HAL_RCC_SPI1_FORCE_RESET() lcd_test_spi_reset()
#define __HAL_RCC_SPI1_RELEASE_RESET() ((void)0)
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(void *,uint32_t,GPIO_PinState);
void lcd_test_dma_disable(DMA_HandleTypeDef *);
void lcd_test_spi_reset(void);
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *,uint8_t *,uint16_t,uint32_t);
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *,uint32_t,uint32_t,uint32_t);
#endif
