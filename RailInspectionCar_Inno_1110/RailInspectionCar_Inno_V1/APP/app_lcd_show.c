#include "app_config.h"

#if APP_CHASSIS_LCD_ENABLED
#include "app_lcd_show.h"

#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "app_health.h"
#include "app_nrf_remote.h"
#include "app_remote_servo.h"
#include "app_position_monitor.h"
#include "app_rtos_create.h"
#include "aimtor_servo.h"
#include "bsp_lcd.h"
#include "bsp_rs485.h"
#include "bsp_system.h"
#include "motion_control.h"
#include "servo_debug_control.h"
#include "servo_pulse.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static lcd *s_lcd;
static uint8_t s_lcd_ready;
static uint32_t s_lcd_retry_at, s_lcd_rebuilds;
static uint8_t s_lcd_attempted;
static uint8_t s_retry_failures;
static uint32_t s_full_redraw_at, s_display_generation, s_full_redraws;
static uint32_t s_clear_count, s_last_clear_ms;
static uint8_t s_clear_reason;
static char s_line_cache[10][41];
static uint8_t s_line_valid[10];
static TickType_t s_last_refresh_ticks;
static TickType_t s_max_refresh_ticks;
static volatile lcd_page_t s_page = LCD_PAGE_SERVO;
static lcd_page_t s_rendered_page = LCD_PAGE_SERVO;
static app_lcd_event_state_t s_lcd_event;

/* 固定为40列，截断超长行并补空格，防止换行覆盖及旧字符残留。 */
static void app_lcd_line(lcd *display, uint16_t x, uint16_t y,
                         const char *format, ...)
{
    char text[41];
    size_t length;
    va_list args;
    if (bsp_lcd_get_init_stage() == LCD_INIT_STAGE_SPI_ERROR) return;
    va_start(args, format);
    (void)vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    length = strlen(text);
    memset(text + length, ' ', 40U - length);
    text[40] = '\0';
    if (x == 0U && y / 16U < 10U)
    {
        unsigned row = y / 16U;
        unsigned first = 0U, end = 40U;
        if (s_line_valid[row])
        {
            while (first < 40U && text[first] == s_line_cache[row][first]) ++first;
            while (end > first && text[end - 1U] == s_line_cache[row][end - 1U]) --end;
        }
        if (first == end) return;
        {
            char saved = text[end];
            text[end] = '\0';
            lcd_show_string(display, (uint16_t)(x + first * display->font.width), y, (const uint8_t *)text + first);
            text[end] = saved;
        }
        if (bsp_lcd_get_init_stage() != LCD_INIT_STAGE_SPI_ERROR) {
            memcpy(s_line_cache[row], text, sizeof(text)); s_line_valid[row] = 1U;
        } else memset(s_line_valid, 0, sizeof(s_line_valid));
    }
    else lcd_show_string(display, x, y, (const uint8_t *)text);
}

#define lcd_print app_lcd_line

/* Fixed-cost publication only: never put LCD I/O or waiting in this section.
   Phase 1/2 brackets every rebuild attempt, including failed attempts. */
static void app_lcd_publish_event(uint8_t phase)
{
    uint32_t now = HAL_GetTick();
    uint8_t stage = (uint8_t)bsp_lcd_get_init_stage();
    uint8_t running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (running) taskENTER_CRITICAL();
    if (++s_lcd_event.revision == 0U) ++s_lcd_event.revision;
    s_lcd_event.at_ms = now;
    if (phase == 1U) {
        s_lcd_event.rebuild_started_ms = now;
        s_lcd_event.rebuild_finished_ms = 0U;
        s_lcd_event.rebuilding = 1U;
    } else if (phase == 2U) {
        s_lcd_event.rebuild_finished_ms = now;
        s_lcd_event.rebuilding = 0U;
    }
    s_lcd_event.rebuilds = s_lcd_rebuilds;
    s_lcd_event.generation = s_display_generation;
    s_lcd_event.clear_count = s_clear_count;
    s_lcd_event.clear_at_ms = s_last_clear_ms;
    s_lcd_event.retry_at_ms = s_lcd_retry_at;
    s_lcd_event.ready = s_lcd_ready;
    s_lcd_event.stage = stage;
    s_lcd_event.page = (uint8_t)s_rendered_page;
    s_lcd_event.clear_reason = s_clear_reason;
    if (running) taskEXIT_CRITICAL();
}

void app_lcd_get_event_state(app_lcd_event_state_t *state)
{
    uint8_t running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (state == NULL) return;
    if (running) taskENTER_CRITICAL();
    *state = s_lcd_event;
    if (running) taskEXIT_CRITICAL();
}

static void app_lcd_retry(void)
{
    static const uint32_t delays[] = {2000U,4000U,8000U,10000U};
    unsigned index = s_retry_failures < 4U ? s_retry_failures : 3U;
    s_lcd_retry_at = HAL_GetTick() + delays[index];
    if (s_retry_failures < 4U) ++s_retry_failures;
    memset(s_line_valid, 0, sizeof(s_line_valid));
}

void lcd_init(void)
{
    lcd_port_stats_t before, after;
    if (s_lcd_ready != 0U)
    {
        return;
    }

    if (s_lcd_attempted && (int32_t)(HAL_GetTick() - s_lcd_retry_at) < 0) return;
    s_lcd_attempted = 1U;
    app_lcd_publish_event(1U);
    lcd_port_get_stats(&before);
    s_lcd = bsp_lcd_init();
    lcd_port_get_stats(&after);
    if (after.init_clears != before.init_clears) {
        s_clear_reason = s_lcd_rebuilds ? 2U : 1U;
        s_last_clear_ms = HAL_GetTick(); ++s_clear_count;
    }
    ++s_lcd_rebuilds;
    memset(s_line_valid, 0, sizeof(s_line_valid));
    s_lcd_ready = (uint8_t)((s_lcd != NULL) && bsp_lcd_is_ready());
    if (s_lcd_ready) {
        ++s_display_generation;
        s_rendered_page = app_lcd_get_page();
        s_full_redraw_at = HAL_GetTick() + 2000U;
    } else app_lcd_retry();
    app_lcd_publish_event(2U);
}

bool app_lcd_is_ready(void)
{
    return s_lcd_ready != 0U;
}

void app_lcd_toggle_page(void)
{
    taskENTER_CRITICAL();
    s_page = (lcd_page_t)(((uint8_t)s_page + 1U) % 3U);
    taskEXIT_CRITICAL();
}

lcd_page_t app_lcd_get_page(void)
{
    return s_page;
}

static void app_lcd_frame_begin(lcd_page_t page)
{
    if (s_rendered_page != page) {
        ++s_clear_count; s_clear_reason = 3U; s_last_clear_ms = HAL_GetTick();
        lcd_clear(s_lcd, BLACK);
        memset(s_line_valid, 0, sizeof(s_line_valid));
        s_rendered_page = page;
        app_lcd_publish_event(0U);
    }
    if ((int32_t)(HAL_GetTick() - s_full_redraw_at) >= 0) {
        memset(s_line_valid, 0, sizeof(s_line_valid));
        ++s_full_redraws; s_full_redraw_at = HAL_GetTick() + 2000U;
    }
}

static void app_lcd_frame_end(void)
{
    if (bsp_lcd_get_init_stage() == LCD_INIT_STAGE_SPI_ERROR) {
        app_health_set_diagnostic_fault(APP_DIAG_LCD_SPI_ERROR);
        app_lcd_retry(); s_lcd_ready = 0U; s_lcd = NULL;
        app_lcd_publish_event(0U);
    } else s_retry_failures = 0U;
}

void app_lcd_refresh(void)
{
    servo_recovery_status_t recovery;
    servo_snapshot_t left;
    servo_snapshot_t right;
    rs485_stats_t stats;
    nrf_remote_snapshot_t remote;
    remote_servo_status_t control;
    UBaseType_t motion_stack;
    UBaseType_t comm_stack;
    UBaseType_t health_stack;
    TickType_t start;
    TickType_t elapsed;
    lcd_page_t page;

#if APP_CLI_SERVO_ACTIVE && APP_SERVO_PULSE_ENABLED
    servo_debug_status_t pulse;
    uint8_t pulse_page;
#endif

    if ((s_lcd_ready == 0U) || (s_lcd == NULL))
    {
        app_health_set_diagnostic_fault(APP_DIAG_LCD_NOT_READY);
        return;
    }

    start = xTaskGetTickCount();
    page = app_lcd_get_page();
    (void)aimtor_get_snapshot(SERVO_AXIS_LEFT, &left);
    (void)aimtor_get_snapshot(SERVO_AXIS_RIGHT, &right);
    rs485_get_stats(&stats);
    (void)app_nrf_remote_get_snapshot(&remote);
    (void)app_remote_servo_get_status(&control);
#if APP_CLI_SERVO_ACTIVE && APP_SERVO_PULSE_ENABLED
    pulse_page = (uint8_t)(servo_debug_get_status(&pulse) &&
                          (pulse.mode == SERVO_DEBUG_MODE_PULSE));
#endif
    motion_stack = uxTaskGetStackHighWaterMark(motion_task_handle);
    comm_stack = uxTaskGetStackHighWaterMark(servo_comm_task_handle);
    health_stack = uxTaskGetStackHighWaterMark(health_task_handle);

    app_lcd_frame_begin(page);

    if (page == LCD_PAGE_POSITION)
    {
        position_monitor_status_t position;
        uint8_t axis;
        (void)app_position_monitor_get_status(&position);
        lcd_print(s_lcd, 0, 0, "WHEEL EST:%s SRC:%u",
                  app_position_monitor_state_name(position.state), position.source_mask);
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        {
            position_axis_status_t *a = &position.axis[axis];
            if (a->valid)
                lcd_print(s_lcd, 0, (uint16_t)(16U + axis * 32U),
                          "%c POS:%ld AGE:%lu", axis == 0U ? 'L' : 'R',
                          (long)a->raw_position, (unsigned long)a->age_ms);
            else
                lcd_print(s_lcd, 0, (uint16_t)(16U + axis * 32U),
                          "%c POS:-- %s", axis == 0U ? 'L' : 'R',
                          app_position_monitor_state_name(a->state));
            lcd_print(s_lcd, 0, (uint16_t)(32U + axis * 32U),
                      "%c SEQ:%lu GAP:%lu", axis == 0U ? 'L' : 'R',
                      (unsigned long)a->sequence, (unsigned long)a->lost_intervals);
        }
        if (position.pair_valid)
            lcd_print(s_lcd, 0, 80, "DIFF:%.1fmm SKEW:%lums",
                      position.difference_mm, (unsigned long)position.sample_skew_ms);
        else lcd_print(s_lcd, 0, 80, "DIFF:-- SKEW:--");
        if (position.vehicle_valid)
        {
            lcd_print(s_lcd, 0, 96, "TRIP PATH:%.3fm",
                      position.vehicle_trip_path_mm / 1000.0);
            lcd_print(s_lcd, 0, 112, "BOOT PATH:%.3fm",
                      position.vehicle_boot_path_mm / 1000.0);
            lcd_print(s_lcd, 0, 128, "NET:%.3fm/%.3fm",
                      position.vehicle_trip_net_mm / 1000.0,
                      position.vehicle_boot_net_mm / 1000.0);
        }
        else
        {
            lcd_print(s_lcd, 0, 96, "TRIP PATH:--");
            lcd_print(s_lcd, 0, 112, "BOOT PATH:--");
            lcd_print(s_lcd, 0, 128, "NET:--/--");
        }
        lcd_print(s_lcd, 0, 144, "GEN:%lu GAP:%lu SAMPLED",
                  (unsigned long)position.baseline_generation,
                  (unsigned long)position.lost_intervals);
    }
    else if (page == LCD_PAGE_REMOTE)
    {
        lcd_print(s_lcd, 0, 0, "NRF REMOTE INIT:%u SPI:%lu",
                  remote.initialized, (unsigned long)remote.nrf_spi_errors);
        lcd_print(s_lcd, 0, 16, "LINK:%s AGE:%lu LOSS:%lu",
                  remote.link_up ? "OK" : "LOST",
                  (unsigned long)remote.age_ms, (unsigned long)remote.link_loss_count);
        lcd_print(s_lcd, 0, 32, "LH:%04u LV:%04u RH:%04u RV:%04u",
                  remote.left_horizontal, remote.left_vertical,
                  remote.right_horizontal, remote.right_vertical);
        lcd_print(s_lcd, 0, 48, "S:%02X %01X EV:%lu",
                  (unsigned int)(remote.short_press & 0xFFU),
                  (unsigned int)(remote.short_press >> 8U),
                  (unsigned long)remote.event_batches);
        lcd_print(s_lcd, 0, 64, "L:%02X %01X DUP:%lu",
                  (unsigned int)(remote.long_press & 0xFFU),
                  (unsigned int)(remote.long_press >> 8U),
                  (unsigned long)remote.duplicate_events);
        lcd_print(s_lcd, 0, 80, "D:%02X %01X FIFO:%lu",
                  (unsigned int)(remote.double_click & 0xFFU),
                  (unsigned int)(remote.double_click >> 8U),
                  (unsigned long)remote.nrf_fifo_errors);
        lcd_print(s_lcd, 0, 96, "AX:%s RC:%s A:%u",
                  app_config_servo_control_axis_name(),
                  app_remote_servo_state_name(control.state),
                  control.authorized);
        lcd_print(s_lcd, 0, 112, "T:%d C:%d L:%d R:%d",
                  control.target_rpm, control.commanded_rpm,
                  control.left_actual_rpm, control.right_actual_rpm);
        lcd_print(s_lcd, 0, 128, "RX:%lu DROP:%lu CRC:%lu",
                  (unsigned long)remote.valid_frames,
                  (unsigned long)remote.dropped_frames,
                  (unsigned long)remote.crc_errors);
        lcd_print(s_lcd, 0, 144, "STOP:%lu LOST:%lu WHY:%u",
                  (unsigned long)control.estop_count,
                  (unsigned long)control.link_loss_estop_count,
                  (unsigned int)control.stop_reason);
    }
    else
    {
        lcd_print(s_lcd, 0, 0, "S:%u H:%u B:%u C:%u R:%u ",
                  (unsigned int)motion_get_state(),
                  (unsigned int)app_config_motion_hardware_ready(),
                  (unsigned int)app_config_servo_bus_ready(),
                  (unsigned int)motion_get_stop_class(),
                  (unsigned int)motion_get_fault_reason());
        lcd_print(s_lcd, 0, 20, "L O:%u E:%u V:%u M:%u   ",
                  left.online, left.enabled, left.parameter_verified,
                  (unsigned int)left.control_mode);
        lcd_print(s_lcd, 0, 40, "L:%d F:%04X D:%u/%u/%u",
                  left.actual_speed_rpm, left.fault_code,
                  left.feedback_failures[0], left.feedback_failures[1], left.feedback_failures[2]);
        lcd_print(s_lcd, 0, 60, "R O:%u E:%u V:%u M:%u   ",
                  right.online, right.enabled, right.parameter_verified,
                  (unsigned int)right.control_mode);
        lcd_print(s_lcd, 0, 80, "R:%d F:%04X D:%u/%u/%u",
                  right.actual_speed_rpm, right.fault_code,
                  right.feedback_failures[0], right.feedback_failures[1], right.feedback_failures[2]);
#if APP_CLI_SERVO_ACTIVE && APP_SERVO_PULSE_ENABLED
        if (pulse_page != 0U)
        {
            lcd_print(s_lcd, 0, 100, "L P:%lu/%lu OUT:%lu",
                      (unsigned long)servo_pulse_get_pulse_count(SERVO_AXIS_LEFT),
                      (unsigned long)servo_pulse_get_target(SERVO_AXIS_LEFT),
                      (unsigned long)servo_pulse_get_frequency(SERVO_AXIS_LEFT));
            lcd_print(s_lcd, 0, 120, "R P:%lu/%lu OUT:%lu",
                      (unsigned long)servo_pulse_get_pulse_count(SERVO_AXIS_RIGHT),
                      (unsigned long)servo_pulse_get_target(SERVO_AXIS_RIGHT),
                      (unsigned long)servo_pulse_get_frequency(SERVO_AXIS_RIGHT));
            lcd_print(s_lcd, 0, 140, "CMD:%d/%d C:%u STOP:%u TO:%u",
                      (int)(motion_get_command_velocity(SERVO_AXIS_LEFT) *
                            60.0f * APP_GEAR_REDUCTION_RATIO /
                            APP_WHEEL_CIRCUMFERENCE_M),
                      (int)(motion_get_command_velocity(SERVO_AXIS_RIGHT) *
                            60.0f * APP_GEAR_REDUCTION_RATIO /
                            APP_WHEEL_CIRCUMFERENCE_M),
                      pulse.pulse_target_reached, pulse.pulse_stop_confirmed,
                      pulse.pulse_completion_timeout);
        }
        else
#endif
        {
        lcd_print(s_lcd, 0, 100, "RX:%lu OV:%lu ID:%lu H:%lu ",
                  (unsigned long)stats.rx_bytes,
                  (unsigned long)stats.rx_overflows,
                  (unsigned long)stats.idle_interrupts,
                  (unsigned long)stats.dma_half_events);
        lcd_print(s_lcd, 0, 120, "STK:%u/%u/%u UE:%lu     ",
                  (unsigned int)motion_stack,
                  (unsigned int)comm_stack,
                  (unsigned int)health_stack,
                  (unsigned long)stats.uart_errors);
        aimtor_get_recovery_status(&recovery);
        lcd_print(s_lcd, 0, 140, "REC:%u E:%u FIRST:%u/%u F:%u",
                  (unsigned int)recovery.state, recovery.eligible,
                  recovery.first_axis, recovery.first_item, recovery.failure);
        }
    }

    app_lcd_frame_end();

    elapsed = xTaskGetTickCount() - start;
    s_last_refresh_ticks = elapsed;
    if (elapsed > s_max_refresh_ticks)
    {
        s_max_refresh_ticks = elapsed;
    }
}

TickType_t app_lcd_get_last_refresh_ticks(void)
{
    return s_last_refresh_ticks;
}

TickType_t app_lcd_get_max_refresh_ticks(void)
{
    return s_max_refresh_ticks;
}

uint8_t app_lcd_get_init_stage(void)
{
    return (uint8_t)bsp_lcd_get_init_stage();
}

uint32_t app_lcd_get_spi_errors(void)
{
    return bsp_lcd_get_spi_errors();
}

uint32_t app_lcd_get_rebuild_count(void) { return s_lcd_rebuilds; }

static void lcd_print_failure(const char *name, const lcd_failure_t *f)
{
    printf("lcd %s t=%lu reason=%u hal=%u stage=%u len=%u remain=%u elapsed=%lu\r\n",
        name,(unsigned long)f->timestamp_ms,f->reason,f->hal_status,f->stage,
        f->length,f->remaining,(unsigned long)f->elapsed_ms);
    printf("lcd %s SR=%08lX CR1=%08lX CR2=%08lX DMA_CR=%08lX flags=%08lX error=%08lX\r\n",
        name,(unsigned long)f->spi_sr,(unsigned long)f->spi_cr1,(unsigned long)f->spi_cr2,
        (unsigned long)f->dma_cr,(unsigned long)f->dma_flags,(unsigned long)f->dma_error);
}

void app_lcd_print_status(void)
{
    lcd_port_stats_t stats;
    lcd_port_get_stats(&stats);
    printf("lcd ready=%u stage=%u generation=%lu rebuild=%lu clear=%lu clear_reason=%u clear_at=%lu redraw=%lu retry_at=%lu\r\n",
        s_lcd_ready,(unsigned)app_lcd_get_init_stage(),(unsigned long)s_display_generation,
        (unsigned long)s_lcd_rebuilds,(unsigned long)s_clear_count,s_clear_reason,
        (unsigned long)s_last_clear_ms,(unsigned long)s_full_redraws,(unsigned long)s_lcd_retry_at);
    printf("lcd errors=%lu dma=%lu/%lu poll=%lu max_transfer=%lu refresh=%lu/%lu uptime=%lu reset=%08lX\r\n",
        (unsigned long)stats.errors,(unsigned long)stats.dma_completed,(unsigned long)stats.dma_started,
        (unsigned long)stats.polling,(unsigned long)stats.max_transfer_ms,
        (unsigned long)s_last_refresh_ticks,(unsigned long)s_max_refresh_ticks,
        (unsigned long)HAL_GetTick(),(unsigned long)app_health_get_reset_reason());
    lcd_print_failure("first", &stats.first); lcd_print_failure("last", &stats.last);
}

#else
#include "app_lcd_show.h"
#include <string.h>
#include <stdio.h>
void lcd_init(void) {}
bool app_lcd_is_ready(void) { return false; }
void app_lcd_refresh(void) {}
void app_lcd_toggle_page(void) {}
lcd_page_t app_lcd_get_page(void) { return LCD_PAGE_SERVO; }
TickType_t app_lcd_get_last_refresh_ticks(void) { return 0U; }
TickType_t app_lcd_get_max_refresh_ticks(void) { return 0U; }
uint8_t app_lcd_get_init_stage(void) { return 0U; }
uint32_t app_lcd_get_spi_errors(void) { return 0U; }
uint32_t app_lcd_get_rebuild_count(void) { return 0U; }
void app_lcd_get_event_state(app_lcd_event_state_t *state)
{ if (state) memset(state,0,sizeof(*state)); }
void app_lcd_print_status(void) { printf("lcd disabled\r\n"); }

#endif
