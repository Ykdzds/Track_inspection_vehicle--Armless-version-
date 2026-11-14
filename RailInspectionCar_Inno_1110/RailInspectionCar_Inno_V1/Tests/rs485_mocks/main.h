#ifndef TEST_RS485_MAIN_H
#define TEST_RS485_MAIN_H
#include "usart.h"
typedef struct { uint32_t unused; } TIM_HandleTypeDef;
typedef struct { uint32_t unused; } CAN_HandleTypeDef;
void HAL_CAN_IRQHandler(CAN_HandleTypeDef *can);
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *timer);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *dma);
#endif
