#ifndef TEST_NRF_TASK_H
#define TEST_NRF_TASK_H
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *higher);
#define portYIELD_FROM_ISR(higher) ((void)(higher))
#endif
