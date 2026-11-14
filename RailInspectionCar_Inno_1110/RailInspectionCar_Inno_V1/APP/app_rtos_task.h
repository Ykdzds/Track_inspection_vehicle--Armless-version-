#ifndef APP_RTOS_TASK_H
#define APP_RTOS_TASK_H

#include <stdint.h>

/* 绿色指示灯表示当前至少存在一种有效运动授权。 */
static inline uint8_t app_status_motion_authorized(uint8_t remote_authorized,
                                                   uint8_t hmi_authorized,
                                                   uint8_t can_authorized)
{
    return (uint8_t)((remote_authorized != 0U) ||
                     (hmi_authorized != 0U) ||
                     (can_authorized != 0U));
}

void motion_task(void *argument);
void servo_comm_task(void *argument);
void modbus_bus_task(void *argument);
void health_task(void *argument);
void key_task(void *argument);
void status_task(void *argument);
void cli_task(void *argument);
void lcd_task(void *argument);
void iwdg_task(void *argument);
void nrf_task(void *argument);
void hmi_task(void *argument);

#endif
