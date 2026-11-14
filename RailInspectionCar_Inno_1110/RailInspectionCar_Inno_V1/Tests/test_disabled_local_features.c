/* Compile the production disabled paths; only external safety inputs are mocked. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "app_config.h"
#include "app_lcd_show.h"
#include "bsp_lcd.h"
#include "motion_control.h"
#include "servo_debug_control.h"
#include "servo_pulse.h"

GPIO_TypeDef test_gpio_b, test_gpio_c, test_gpio_e;
static bool motion_allowed = true, recovering;
static uint8_t hw_ready = 1U;
static unsigned emergency_count, fallback_count, immediate_record_count;
static uint32_t fallback_reason;
bool aimtor_motion_allowed(void) { return motion_allowed; }
bool aimtor_recovery_blocks_motion(void) { return recovering; }
uint8_t app_config_motion_hardware_ready(void) { return hw_ready; }
void aimtor_emergency_stop(void) { ++emergency_count; }
void aimtor_record_immediate_safety_stop(uint8_t reason)
{ assert(reason == MOTION_FAULT_REASON_STOP_TIMEOUT); ++immediate_record_count; }
void app_health_request_driver_fallback(uint32_t reason)
{ ++fallback_count; fallback_reason = reason; }

static void assert_outputs_off(void)
{
    assert(test_gpio_c.BSRR == ((uint32_t)GPIO_PIN_8 << 16U | GPIO_PIN_9));
    assert(test_gpio_e.BSRR == (uint32_t)GPIO_PIN_5 << 16U);
    assert(test_gpio_b.BSRR == (uint32_t)GPIO_PIN_9 << 16U);
    assert(((test_gpio_e.MODER >> 10U) & 3U) == 1U);
    assert(((test_gpio_b.MODER >> 18U) & 3U) == 1U);
    assert((test_gpio_e.OTYPER & GPIO_PIN_5) == 0U);
    assert((test_gpio_b.OTYPER & GPIO_PIN_9) == 0U);
    assert(((test_gpio_e.PUPDR >> 10U) & 3U) == 0U);
    assert(((test_gpio_b.PUPDR >> 18U) & 3U) == 0U);
}
static void pulse_disabled(void)
{
    memset(&test_gpio_b, 0xFF, sizeof(test_gpio_b));
    memset(&test_gpio_c, 0xFF, sizeof(test_gpio_c));
    memset(&test_gpio_e, 0xFF, sizeof(test_gpio_e));
    servo_pulse_init(); assert_outputs_off();
    for (unsigned a = 0U; a < SERVO_AXIS_COUNT; ++a) {
        servo_axis_t axis = (servo_axis_t)a;
        assert(servo_pulse_set_enable(axis, true) == SERVO_PULSE_NOT_CONFIGURED);
        assert(servo_pulse_set_frequency(axis, 1000U) == SERVO_PULSE_NOT_CONFIGURED);
        assert(servo_pulse_set_direction(axis, true) == SERVO_PULSE_NOT_CONFIGURED);
        assert(servo_pulse_set_enable(axis, false) == SERVO_PULSE_OK);
        assert(servo_pulse_set_frequency(axis, 0U) == SERVO_PULSE_OK);
        servo_pulse_set_target(axis, 10U);servo_pulse_accumulate(100U);
        servo_pulse_update_tim9_isr();servo_pulse_update_tim11_isr();
        assert(!servo_pulse_output_active(axis));
        assert(!servo_pulse_get_target(axis) && !servo_pulse_get_pulse_count(axis));
        assert(!servo_pulse_get_command_position(axis) && !servo_pulse_get_frequency(axis));
        assert(!servo_pulse_target_reached(axis));
    }
    assert(servo_pulse_set_enable((servo_axis_t)99U, true) == SERVO_PULSE_INVALID_ARGUMENT);
    assert_outputs_off();
}
static void safety_retained(void)
{
    motion_init();assert(motion_get_state() == MOTION_SAFE);
    assert(motion_get_stop_class() == MOTION_STOP_CLASS_NONE);
    motion_set_system_ready(1U);assert(motion_get_state() == MOTION_READY);
    assert(motion_set_velocity(0.2f, 0.2f) == MOTION_NOT_CONFIGURED);
    assert(motion_move_distance(1.0f, 0.2f, 0.5f) == MOTION_NOT_CONFIGURED);
    assert(motion_start_pulse_mask(3U, 1U, 1000U, 10U) == MOTION_NOT_CONFIGURED);
    assert(motion_stop((motion_stop_mode_t)99U) == MOTION_INVALID_ARGUMENT);
    motion_set_system_ready(0U);assert(motion_get_state() == MOTION_SAFE);
    assert(motion_get_stop_class() == MOTION_STOP_CLASS_NONE);
    motion_set_system_ready(1U);
    motion_request_controlled_safety_stop(MOTION_FAULT_REASON_FEEDBACK_STALE, 1U);
    assert(motion_get_state() == MOTION_STOPPING);
    assert(motion_get_stop_class() == MOTION_STOP_CLASS_CONTROLLED_SAFETY);
    motion_request_controlled_safety_stop(MOTION_FAULT_REASON_SERVO_FAULT, 0U);
    motion_set_system_ready(1U);motion_update();
    assert(motion_get_state() == MOTION_STOPPING);
    assert(motion_get_fault_reason() == MOTION_FAULT_REASON_FEEDBACK_STALE);
    motion_complete_controlled_safety_stop(1U);
    assert(motion_get_state() == MOTION_OFFLINE && emergency_count == 1U);
    assert(motion_get_stop_class() == MOTION_STOP_CLASS_CONTROLLED_SAFETY);
    recovering = true;motion_clear_fault();assert(motion_get_state() == MOTION_OFFLINE);
    recovering = false;motion_clear_fault();assert(motion_get_state() == MOTION_SAFE);
    assert(motion_get_fault_reason() == MOTION_FAULT_REASON_NONE);
    motion_set_system_ready(1U);
    motion_request_immediate_safety_stop(MOTION_FAULT_REASON_STOP_TIMEOUT, 0U);
    assert(motion_get_state() == MOTION_FAULT && immediate_record_count == 1U);
    assert(motion_get_stop_class() == MOTION_STOP_CLASS_IMMEDIATE_SAFETY);
    assert(fallback_count == 1U && fallback_reason == 0x100U + MOTION_FAULT_REASON_STOP_TIMEOUT);
    assert(emergency_count == 1U); /* Production fallback does not add a serial stop. */
    motion_clear_fault();motion_allowed=false;motion_set_system_ready(1U);
    assert(motion_get_state() == MOTION_SAFE);
    motion_allowed=true;hw_ready=0U;motion_set_system_ready(1U);
    assert(motion_get_state() == MOTION_SAFE);
    assert_outputs_off();
}
static void cli_disabled(void)
{
    servo_debug_status_t status;
    memset(&status, 0xA5, sizeof(status));
    assert(!servo_debug_get_status(NULL));assert(servo_debug_get_status(&status));
    assert(status.mode == SERVO_DEBUG_MODE_NONE && status.state == SERVO_DEBUG_IDLE);
    assert(status.target == 0 && status.target_rpm == 0 && status.result == SERVO_NOT_CONFIGURED);
    assert(servo_debug_prepare(SERVO_DEBUG_MODE_RS485_SPEED) == SERVO_NOT_CONFIGURED);
    assert(servo_debug_enable() == SERVO_NOT_CONFIGURED && servo_debug_disable() == SERVO_NOT_CONFIGURED);
    assert(servo_debug_set_speed(100) == SERVO_NOT_CONFIGURED);
    assert(servo_debug_run_position(100, 10U, 10U) == SERVO_NOT_CONFIGURED);
    assert(servo_debug_run_pulse(true, 1000U, 10U) == SERVO_NOT_CONFIGURED);
    servo_debug_init();servo_debug_select_axis(SERVO_AXIS_RIGHT);servo_debug_service();
    servo_debug_motion_service();servo_debug_abort_from_fault();servo_debug_release_axis();
    assert(servo_debug_stop() == SERVO_OK);assert_outputs_off();
    assert(emergency_count == 1U); /* Normal debug stop keeps its original IDLE semantics. */
    assert(servo_debug_emergency_stop() == SERVO_OK && emergency_count == 2U);
}
static void lcd_disabled(void)
{
    app_lcd_event_state_t event, zero_event;
    lcd_port_stats_t stats, zero_stats;
    memset(&zero_event,0,sizeof(zero_event));memset(&zero_stats,0,sizeof(zero_stats));
    memset(&event,0xA5,sizeof(event));memset(&stats,0xA5,sizeof(stats));
    lcd_init();app_lcd_refresh();app_lcd_toggle_page();
    assert(!app_lcd_is_ready() && !bsp_lcd_is_ready() && bsp_lcd_init() == NULL);
    assert(!app_lcd_get_spi_errors() && !app_lcd_get_rebuild_count());
    assert(!app_lcd_get_last_refresh_ticks() && !app_lcd_get_max_refresh_ticks());
    assert(app_lcd_get_init_stage() == LCD_INIT_STAGE_IDLE);
    app_lcd_get_event_state(&event);assert(memcmp(&event,&zero_event,sizeof(event)) == 0);
    lcd_port_get_stats(&stats);assert(memcmp(&stats,&zero_stats,sizeof(stats)) == 0);
    assert(!lcd_port_recover() && !lcd_port_get_spi_errors());
    app_lcd_get_event_state(NULL);lcd_port_get_stats(NULL);
}
int main(void)
{
    pulse_disabled();safety_retained();cli_disabled();lcd_disabled();
    puts("PASS disabled local features: no pulse requests, hard GPIO off, safety/fault retention, CLI rejection and LCD neutral status");
    return 0;
}
