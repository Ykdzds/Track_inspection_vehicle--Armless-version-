#ifndef TEST_RS485_TASK_H
#define TEST_RS485_TASK_H
#include "../mocks/task.h"
TickType_t xTaskGetTickCountFromISR(void);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken);
#define portYIELD_FROM_ISR(woken) ((void)(woken))
#endif
