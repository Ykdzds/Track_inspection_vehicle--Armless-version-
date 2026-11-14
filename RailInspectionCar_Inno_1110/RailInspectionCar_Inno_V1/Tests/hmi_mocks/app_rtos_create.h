#ifndef APP_RTOS_CREATE_H
#define APP_RTOS_CREATE_H
#include "FreeRTOS.h"
extern TaskHandle_t motion_task_handle, servo_comm_task_handle, health_task_handle;
extern TaskHandle_t key_task_handle, status_task_handle, cli_task_handle;
extern TaskHandle_t lcd_task_handle, iwdg_task_handle, nrf_task_handle, can_task_handle, hmi_task_handle;
#endif
