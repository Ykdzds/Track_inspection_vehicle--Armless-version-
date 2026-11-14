#ifndef TEST_HMI_TASK_H
#define TEST_HMI_TASK_H
#include "../mocks/task.h"
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
#define portYIELD_FROM_ISR(woken) ((void)(woken))
#endif
