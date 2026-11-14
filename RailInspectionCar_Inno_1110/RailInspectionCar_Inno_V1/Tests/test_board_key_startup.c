#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "main.h"
#include "key.h"
#include "queue.h"
#include "task.h"
#include "app_config.h"
#include "aimtor_servo.h"
#include "app_cli_servo_session.h"
#include "app_remote_servo.h"
#include "servo_debug_control.h"

GPIO_TypeDef test_gpio_b, test_gpio_c, test_gpio_d, test_gpio_e;
static key_event_t events[12];
static unsigned head, count, normal_stops, emergency_stops, diagnostic_calls;
static unsigned cli_active, remote_active, lcd_toggles, mode;
static TickType_t ticks;
static jmp_buf finished;

TickType_t xTaskGetTickCount(void) { return ticks; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == GPIOC && pin == GPIO_PIN_13) {
        if (mode == 1U) return ticks < 2000U ? GPIO_PIN_RESET : GPIO_PIN_SET;
        if (mode == 2U) return (ticks < 80U || (ticks >= 200U && ticks < 280U)) ? GPIO_PIN_RESET : GPIO_PIN_SET;
        if (mode == 3U) return ticks < 80U ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
    return GPIO_PIN_SET;
}
QueueHandle_t xQueueCreate(unsigned capacity, unsigned size)
{ assert(capacity == 12U && size == sizeof(key_event_t)); head=count=0U; return events; }
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait)
{ (void)wait; assert(queue == events && count < 12U); events[(head+count++)%12U]=*(const key_event_t *)item; return pdTRUE; }
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{ (void)wait; assert(queue == events); if(!count)return pdFALSE; *(key_event_t *)item=events[head]; head=(head+1U)%12U; --count; return pdTRUE; }
void vTaskDelayUntil(TickType_t *last, TickType_t period)
{ *last+=period; ticks=*last; if(ticks>=2300U)longjmp(finished,1); }
void aimtor_diag_startup_stop(uint8_t source,uint8_t axis,uint16_t reason,uint32_t detail)
{
    assert(source==AIMTOR_STOP_SOURCE_BOARD_KEY && axis==0xFFU);
    assert((reason==KEY_EVENT_SHORT && mode==3U) || (reason==KEY_EVENT_LONG && mode==1U));
    assert(detail == (((uint32_t)KEY_ID_1 << 16U) | ((reason==KEY_EVENT_LONG) ? 0U : 1U)));
    ++diagnostic_calls;
}
bool app_cli_servo_session_active(void) { return cli_active!=0U; }
void app_cli_servo_session_stop(void) { assert(mode==3U && cli_active); ++normal_stops; }
void app_cli_servo_session_abort(cli_session_reason_t reason)
{ assert(reason==CLI_EXIT_MANUAL && mode==1U && cli_active); ++emergency_stops; }
bool app_remote_servo_is_active(void) { return remote_active!=0U; }
void app_remote_servo_request_estop(void) { assert(remote_active); }
servo_result_t servo_debug_stop(void) { ++normal_stops; return SERVO_OK; }
servo_result_t servo_debug_emergency_stop(void) { ++emergency_stops; return SERVO_OK; }
servo_result_t aimtor_reset_fault(servo_axis_t axis) { (void)axis; assert(0); return SERVO_OK; }
uint8_t app_config_servo_axis_selected(uint8_t axis) { (void)axis; return 1U; }
void app_lcd_toggle_page(void) { ++lcd_toggles; }
#include "rail_board_key_task.inc"
static void run(unsigned selected_mode, unsigned cli)
{
    mode=selected_mode; cli_active=cli; remote_active=0U; ticks=0U;
    head=count=normal_stops=emergency_stops=diagnostic_calls=lcd_toggles=0U;
    key_init(); if(setjmp(finished)==0)key_task(NULL);
}
int main(void)
{
    run(1U,0U); assert(diagnostic_calls==1U && emergency_stops==1U && normal_stops==0U);
    run(1U,1U); assert(diagnostic_calls==1U && emergency_stops==1U && normal_stops==0U);
    run(3U,0U); assert(diagnostic_calls==1U && normal_stops==1U && emergency_stops==0U);
    run(3U,1U); assert(diagnostic_calls==1U && normal_stops==1U && emergency_stops==0U);
    run(2U,0U); assert(diagnostic_calls==0U && normal_stops==0U && emergency_stops==0U &&
        lcd_toggles==(APP_CHASSIS_LCD_ENABLED ? 1U : 0U));
    run(0U,0U); assert(diagnostic_calls==0U && normal_stops==0U && emergency_stops==0U && lcd_toggles==0U);
    puts("PASS board PC13: short=normal stop, long=emergency stop, double=LCD page; PD10 has no stop consumer"); return 0;
}
