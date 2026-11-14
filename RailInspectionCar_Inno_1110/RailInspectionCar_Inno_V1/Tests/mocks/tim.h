#ifndef TEST_TIM_H
#define TEST_TIM_H
#include <stddef.h>
#include <stdint.h>
typedef struct
{
    uint32_t CR1;
    uint32_t EGR;
    uint32_t ARR;
    uint32_t CCR1;
    uint32_t CNT;
    uint32_t DIER;
    uint32_t SR;
    uint32_t CCER;
} TIM_TypeDef;
typedef struct { TIM_TypeDef *Instance; } TIM_HandleTypeDef;
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
extern TIM_HandleTypeDef htim9, htim11;
#define TIM_CHANNEL_1 0U
#define TIM_EGR_UG 1U
#define TIM_CR1_UDIS 2U
#define TIM_SR_UIF 1U
#define TIM_DIER_UIE 1U
#define TIM_CCER_CC1E 1U
#define TIM9 (htim9.Instance)
#define TIM11 (htim11.Instance)
#define SET_BIT(r,b) ((r) |= (b))
#define CLEAR_BIT(r,b) ((r) &= ~(b))
#define __HAL_TIM_SET_COMPARE(h,c,v) ((h)->Instance->CCR1 = (v))
#define __HAL_TIM_SET_AUTORELOAD(h,v) ((h)->Instance->ARR = (v))
#define __HAL_TIM_SET_COUNTER(h,v) ((h)->Instance->CNT = (v))
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel);
#endif
