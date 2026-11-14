#ifndef APP_RTOS_CREATE_H
#define APP_RTOS_CREATE_H

#include "FreeRTOS.h"
#include "task.h"
#include "iwdg.h"

extern TaskHandle_t motion_task_handle;
extern TaskHandle_t servo_comm_task_handle;
extern TaskHandle_t modbus_task_handle;
extern TaskHandle_t health_task_handle;
extern TaskHandle_t key_task_handle;
extern TaskHandle_t status_task_handle;
extern TaskHandle_t cli_task_handle;
extern TaskHandle_t lcd_task_handle;
extern TaskHandle_t iwdg_task_handle;
extern TaskHandle_t nrf_task_handle;
extern TaskHandle_t can_task_handle;
extern TaskHandle_t hmi_task_handle;

void app_init(void);
void freertos_taskstart(void);

#endif
