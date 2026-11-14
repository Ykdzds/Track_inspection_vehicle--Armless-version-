#ifndef REMOTE_TEST_MAIN_H
#define REMOTE_TEST_MAIN_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { void *Instance; } TIM_HandleTypeDef;
typedef struct { int unused; } SPI_HandleTypeDef;
typedef struct { int unused; } ADC_HandleTypeDef;
typedef struct { int unused; } IWDG_HandleTypeDef;
#define CE_GPIO_Port ((void *)1)
#define CSN_GPIO_Port ((void *)1)
#define CE_Pin 1U
#define CSN_Pin 2U
#define TIM1 ((void *)1)
#define TIM2 ((void *)2)
#define OLED_6X8 6U
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState value);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, uint8_t *tx,
                                         uint8_t *rx, uint16_t length, uint32_t timeout);
void HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *timer);
void HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc, uint32_t *data, uint32_t length);
void HAL_IWDG_Refresh(IWDG_HandleTypeDef *watchdog);
void key_scan(void);
void key_get_and_clear_events(uint16_t *s, uint16_t *l, uint16_t *d);
void OLED_DrawPoint(int16_t x, int16_t y);
void OLED_Init(void);
void OLED_Service(void);
uint8_t OLED_IsReady(void);
void OLED_Clear(void);
void OLED_ShowString(int16_t x, int16_t y, char *text, uint8_t font);
void OLED_ReverseArea(int16_t x, int16_t y, uint8_t width, uint8_t height);
void OLED_UpdateArea(int16_t x, int16_t y, uint8_t width, uint8_t height);
#endif
