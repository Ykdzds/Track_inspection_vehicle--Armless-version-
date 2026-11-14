#ifndef BUS_TEST_RTOS_H
#define BUS_TEST_RTOS_H
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef struct { unsigned count, size, length, head; uint8_t *storage; } StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
#define pdPASS 1
#define pdFALSE 0
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define portYIELD_FROM_ISR(value) ((void)(value))
QueueHandle_t xQueueCreateStatic(unsigned, unsigned, uint8_t *, StaticQueue_t *);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueSendToFront(QueueHandle_t, const void *, unsigned);
int xQueueSendFromISR(QueueHandle_t, const void *, BaseType_t *);
int xQueueReceive(QueueHandle_t, void *, unsigned);
int xQueuePeek(QueueHandle_t, void *, unsigned);
void xQueueReset(QueueHandle_t);
void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *);
#endif
