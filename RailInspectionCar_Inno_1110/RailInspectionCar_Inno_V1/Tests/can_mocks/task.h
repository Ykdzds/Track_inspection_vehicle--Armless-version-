#ifndef TEST_CAN_TASK_H
#define TEST_CAN_TASK_H
#include "FreeRTOS.h"
#define pdPASS 1
#define portYIELD_FROM_ISR(x) ((void)(x))
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
BaseType_t xTaskCreate(void (*function)(void *), const char *name,
    unsigned stack, void *argument, unsigned priority, TaskHandle_t *task);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken);
#endif
