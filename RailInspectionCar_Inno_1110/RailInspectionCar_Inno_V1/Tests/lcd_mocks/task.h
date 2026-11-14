#include "FreeRTOS.h"
#define taskSCHEDULER_RUNNING 2
void lcd_test_enter_critical(void);
void lcd_test_exit_critical(void);
#undef taskENTER_CRITICAL
#undef taskEXIT_CRITICAL
#define taskENTER_CRITICAL() lcd_test_enter_critical()
#define taskEXIT_CRITICAL() lcd_test_exit_critical()
int xTaskGetSchedulerState(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);
void vTaskDelay(TickType_t);
uint32_t ulTaskNotifyTake(BaseType_t,TickType_t);
void vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t *);
#define portYIELD_FROM_ISR(x) ((void)(x))
