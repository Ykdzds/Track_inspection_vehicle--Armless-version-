#ifndef TEST_HMI_TIM_H
#define TEST_HMI_TIM_H
#include <stdint.h>
typedef struct { uint32_t CR1, DIER, CNT, EGR, PSC, ARR, SR; } test_timer_t;
typedef struct { test_timer_t *Instance; } TIM_HandleTypeDef;
extern TIM_HandleTypeDef htim2;
#define TIM_CR1_CEN 1U
#define TIM_CR1_OPM 8U
#define TIM_DIER_UIE 1U
#define TIM_EGR_UG 1U
#define TIM_FLAG_UPDATE 1U
#define __HAL_TIM_CLEAR_FLAG(timer, flags) ((timer)->Instance->SR &= ~(flags))
#endif
