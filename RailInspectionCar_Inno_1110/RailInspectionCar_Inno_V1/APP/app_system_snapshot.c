#include "app_system_snapshot.h"
#include "app_tune_cli.h"
#include "app_tune_diagnostics.h"
#include "app_runtime_metrics.h"
#include "app_health.h"
#include "modbus_rtu.h"
#include "app_rtos_create.h"
#include "app_boot_identity.h"
#include "task.h"
#include "main.h"
#include "nrf_status_telemetry.h"
#include "app_can_status.h"
#include "app_nrf_remote.h"
#include <limits.h>
#include <string.h>

static app_system_snapshot_t sys_buffer[2];
static uint8_t sys_write, sys_active, sys_initialized, sys_startup_complete;
static uint32_t sys_sequence, sys_last_stack_scan;
static uint16_t sys_minimum_stack;
static uint32_t sys_stack_words[12];
static nrf_remote_snapshot_t sys_radio;

static uint16_t age16(uint32_t age)
{
    return age > 65534U ? 65534U : (uint16_t)age;
}

static void scan_stack(uint32_t now)
{
    TaskHandle_t handles[12];
    uint32_t minimum = 65535U;
    uint8_t i;
    if (sys_initialized && (uint32_t)(now - sys_last_stack_scan) < 1000U) return;
    handles[0] = motion_task_handle; handles[1] = servo_comm_task_handle;
    handles[2] = health_task_handle; handles[3] = key_task_handle;
    handles[4] = status_task_handle; handles[5] = cli_task_handle;
    handles[6] = lcd_task_handle; handles[7] = iwdg_task_handle;
    handles[8] = nrf_task_handle; handles[9] = can_task_handle;
    handles[10] = hmi_task_handle; handles[11] = modbus_task_handle;
    for (i = 0U; i < 12U; i++)
    {
        uint32_t free_words;
        if (handles[i] == NULL) { sys_stack_words[i]=UINT32_MAX; continue; }
        free_words = uxTaskGetStackHighWaterMark(handles[i]);
        sys_stack_words[i]=free_words;
        if (free_words < minimum) minimum = free_words;
    }
    sys_minimum_stack = minimum == 65535U ? 0xFFFFU : age16(minimum);
    sys_last_stack_scan = now;
}

static void startup_progress(void)
{
    uint8_t i, failed = 0U;
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
    {
        if (!(sys_buffer[sys_write].selected & (1U << i))) continue;
        if (sys_buffer[sys_write].watchdog[i].state == SERVO_WDG_RECOVERY_FAILED ||
            sys_buffer[sys_write].startup[i].state == SERVO_STARTUP_STOP_FAILED ||
            sys_buffer[sys_write].boot_zero[i].state == SERVO_BOOT_ZERO_FAILED ||
            sys_buffer[sys_write].hold[i].state == SERVO_STARTUP_HOLD_FAILED) failed = 1U;
    }
    /* 初始化完成单独锁存，运行期撤权或停车不应让进度退回启动状态。 */
    if (!sys_startup_complete && modbus_motion_permitted() && sys_buffer[sys_write].startup_valid &&
        aimtor_watchdog_recovery_ready() && aimtor_startup_stop_ready() &&
        aimtor_boot_zero_ready() && aimtor_startup_hold_ready())
        sys_startup_complete = 1U;
    sys_buffer[sys_write].init_complete = sys_startup_complete;
    if (failed && !sys_startup_complete)
    {
        sys_buffer[sys_write].init_stage = 7U;
        sys_buffer[sys_write].init_progress = 0U;
    }
    else if (sys_startup_complete)
    {
        sys_buffer[sys_write].init_stage = 6U;
        sys_buffer[sys_write].init_progress = 100U;
    }
    else if (!sys_buffer[sys_write].startup_valid || !sys_buffer[sys_write].axes_valid)
    {
        sys_buffer[sys_write].init_stage = 3U;
        sys_buffer[sys_write].init_progress = 30U;
    }
    else
    {
        sys_buffer[sys_write].init_stage = 5U;
        sys_buffer[sys_write].init_progress = 70U;
        for (i = 0U; i < SERVO_AXIS_COUNT; i++)
            if ((sys_buffer[sys_write].selected & (1U << i)) && !sys_buffer[sys_write].axis[i].online)
            {
                sys_buffer[sys_write].init_stage = 4U;
                sys_buffer[sys_write].init_progress = 50U;
            }
    }
}

static void sys_collect(void)
{
    uint8_t i;
    uint16_t lease;
    memset(&sys_buffer[sys_write], 0, sizeof(sys_buffer[sys_write]));
    sys_buffer[sys_write].now_ms = HAL_GetTick();
    sys_buffer[sys_write].now_tick = xTaskGetTickCount();
    sys_buffer[sys_write].boot_id = app_boot_identity_id();
    sys_buffer[sys_write].identity_valid = app_boot_identity_valid();
    sys_buffer[sys_write].identity_result = (uint16_t)app_boot_identity_get()->result;
    sys_buffer[sys_write].health_valid = 1U;
    sys_buffer[sys_write].owner_valid = 1U;
    sys_buffer[sys_write].sequence = sys_sequence + 1U;
    if (sys_buffer[sys_write].sequence == 0U) sys_buffer[sys_write].sequence = 1U;
    sys_buffer[sys_write].reset_reason = app_health_get_reset_reason();
    sys_buffer[sys_write].diagnostic = app_health_get_diagnostic_faults();
    sys_buffer[sys_write].selected = app_config_servo_control_axis_mask();
    sys_buffer[sys_write].axes_valid = aimtor_get_snapshot_pair(&sys_buffer[sys_write].axis[0], &sys_buffer[sys_write].axis[1]);
    sys_buffer[sys_write].position_valid = app_position_monitor_get_status(&sys_buffer[sys_write].position);
    sys_buffer[sys_write].remote_valid = app_remote_servo_get_status(&sys_buffer[sys_write].remote);
    sys_buffer[sys_write].can_control_valid = app_can_control_get_status(&sys_buffer[sys_write].can_control);
    sys_buffer[sys_write].cli_valid = app_cli_servo_session_get_status(&sys_buffer[sys_write].cli);
    sys_buffer[sys_write].debug_valid = servo_debug_get_status(&sys_buffer[sys_write].debug);
    sys_buffer[sys_write].k1_valid = aimtor_k1_hold_get_status(&sys_buffer[sys_write].k1);
    aimtor_get_recovery_status(&sys_buffer[sys_write].recovery);
    sys_buffer[sys_write].startup_valid = 1U;
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
    {
        uint8_t valid = 1U;
        valid = aimtor_startup_stop_get_status((servo_axis_t)i, &sys_buffer[sys_write].startup[i]) && valid;
        valid = aimtor_startup_hold_get_status((servo_axis_t)i, &sys_buffer[sys_write].hold[i]) && valid;
        valid = aimtor_watchdog_recovery_get_status((servo_axis_t)i, &sys_buffer[sys_write].watchdog[i]) && valid;
        valid = aimtor_boot_zero_get_status((servo_axis_t)i, &sys_buffer[sys_write].boot_zero[i]) && valid;
        if (!valid) sys_buffer[sys_write].startup_valid = 0U;
        sys_buffer[sys_write].motion_velocity[i] = motion_get_command_velocity(i);
    }
    sys_buffer[sys_write].can_valid = app_can_get_snapshot(&sys_buffer[sys_write].can, sys_buffer[sys_write].upper) &&
        sys_buffer[sys_write].can.enabled;
    for (i = 0U; i < CAN_ARM_COUNT; ++i)
        sys_buffer[sys_write].arm_valid[i] = (uint8_t)app_can_get_arm_summary(
            (uint8_t)(i + 1U), &sys_buffer[sys_write].arm[i]);
    bsp_hmi_uart_get_stats(&sys_buffer[sys_write].uart);
    debug_uart_get_stats(&sys_buffer[sys_write].debug_uart);
    modbus_get_stats(&sys_buffer[sys_write].modbus);
    if (app_nrf_remote_get_snapshot(&sys_radio))
        sys_buffer[sys_write].nrf_generation = sys_radio.recovery_generation;
    sys_buffer[sys_write].motion = motion_get_state();
    sys_buffer[sys_write].safety_stop_class = (uint16_t)motion_get_stop_class();
    sys_buffer[sys_write].safety_fault_reason = (uint16_t)motion_get_fault_reason();
    sys_buffer[sys_write].maintenance = aimtor_maintenance_enabled();
    sys_buffer[sys_write].safety_ready = (uint8_t)(modbus_motion_permitted() && !app_config_servo_readonly_test() &&
        app_config_motion_hardware_ready() && app_config_servo_bus_ready() &&
        !sys_buffer[sys_write].maintenance && aimtor_watchdog_recovery_ready() &&
        aimtor_startup_stop_ready() && aimtor_boot_zero_ready() &&
        aimtor_startup_hold_ready());
    sys_buffer[sys_write].owner = (uint16_t)app_control_arbiter_owner();
    sys_buffer[sys_write].lock_reason = (uint16_t)app_control_arbiter_lock_reason();
    lease = app_hmi_control_remaining_ms();
    app_hmi_control_get_status(&sys_buffer[sys_write].hmi_control);
    sys_buffer[sys_write].hmi_session = sys_buffer[sys_write].hmi_control.session;
    sys_buffer[sys_write].hmi_remaining = lease;
    sys_buffer[sys_write].hmi_age = sys_buffer[sys_write].hmi_session ? APP_HMI_SESSION_LEASE_MS - lease : 0xFFFFU;
    sys_buffer[sys_write].watchdog_decision = app_health_get_watchdog_decision();
    sys_buffer[sys_write].failed_task = app_health_get_failed_heartbeat() < APP_HEARTBEAT_COUNT ?
        (uint16_t)app_health_get_failed_heartbeat() : 0xFFFFU;
    for (i = 0U; i < APP_HEARTBEAT_COUNT; i++)
        sys_buffer[sys_write].heartbeat_ms[i] = (uint32_t)((uint64_t)app_health_get_heartbeat_age(
            (app_heartbeat_t)i) * 1000U / configTICK_RATE_HZ);
    scan_stack(sys_buffer[sys_write].now_ms);
    sys_buffer[sys_write].minimum_stack = sys_minimum_stack;
    sys_buffer[sys_write].arbiter_generation = app_control_arbiter_generation();
    sys_buffer[sys_write].ready_mask = (uint8_t)(
        (aimtor_startup_stop_ready() ? 1U : 0U) |
        (aimtor_watchdog_recovery_ready() ? 2U : 0U) |
        (aimtor_boot_zero_ready() ? 4U : 0U) |
        (aimtor_startup_hold_ready() ? 8U : 0U));
    sys_buffer[sys_write].hold_active = aimtor_startup_hold_active();
    sys_buffer[sys_write].motion_allowed = aimtor_motion_allowed();
    sys_buffer[sys_write].arbiter_axes = app_control_arbiter_axis_mask();
    app_control_arbiter_get_stop(&sys_buffer[sys_write].stop);
    startup_progress();
}

void app_system_snapshot_init(void)
{
    memset(sys_buffer, 0, sizeof(sys_buffer));
    sys_sequence = 0U;
    sys_active = 0U;
    sys_initialized = 0U;
    sys_startup_complete = 0U;
    sys_last_stack_scan = 0U;
    sys_minimum_stack = 0xFFFFU;
    memset(sys_stack_words,0xFF,sizeof(sys_stack_words));
    app_system_snapshot_service();
}

void app_system_snapshot_service(void)
{
    uint32_t generation = app_control_arbiter_generation();
    if (sys_initialized && (uint32_t)(HAL_GetTick() -
        sys_buffer[sys_active].now_ms) < APP_SYSTEM_SNAPSHOT_PERIOD_MS) return;
    sys_write = (uint8_t)(sys_active ^ 1U);
    sys_collect();
    taskENTER_CRITICAL();
    if (generation != app_control_arbiter_generation())
    {
        taskEXIT_CRITICAL();
        return;
    }
    sys_sequence = sys_buffer[sys_write].sequence;
    sys_active = sys_write;
    sys_initialized = 1U;
    taskEXIT_CRITICAL();
}

uint8_t app_system_snapshot_read_with_meta(app_system_snapshot_t *snapshot,
    uint32_t *source_ms, uint32_t *sequence, uint32_t *source_age_ms,
    uint8_t *initialized)
{
    uint8_t valid;
    uint32_t age, started;
    if (snapshot == NULL) return 0U;
    started = app_runtime_clock();
    taskENTER_CRITICAL();
    age = sys_initialized ? (uint32_t)(HAL_GetTick() -
        sys_buffer[sys_active].now_ms) : UINT32_MAX;
    valid = sys_initialized && age <= APP_SYSTEM_SNAPSHOT_MAX_AGE_MS;
    if (source_ms != NULL) *source_ms = sys_buffer[sys_active].now_ms;
    if (sequence != NULL) *sequence = sys_buffer[sys_active].sequence;
    if (source_age_ms != NULL) *source_age_ms = age;
    if (initialized != NULL) *initialized = sys_initialized;
    if (valid) memcpy(snapshot, &sys_buffer[sys_active], sizeof(*snapshot));
    taskEXIT_CRITICAL();
    app_runtime_max(APP_RUNTIME_SNAPSHOT_CRITICAL,app_runtime_clock()-started);
    return valid;
}

uint8_t app_system_snapshot_read(app_system_snapshot_t *snapshot)
{
    return app_system_snapshot_read_with_meta(snapshot, NULL, NULL, NULL, NULL);
}

uint8_t app_system_snapshot_source_meta(uint32_t *source_ms, uint32_t *sequence)
{
    uint8_t valid;
    if (source_ms == NULL || sequence == NULL) return 0U;
    taskENTER_CRITICAL();
    valid = sys_initialized;
    *source_ms = sys_buffer[sys_active].now_ms;
    *sequence = sys_buffer[sys_active].sequence;
    taskEXIT_CRITICAL();
    return valid;
}

static uint8_t current(void)
{
    return sys_initialized && (uint32_t)(HAL_GetTick() -
        sys_buffer[sys_active].now_ms) <= APP_SYSTEM_SNAPSHOT_MAX_AGE_MS;
}

static int16_t target_rpm(int16_t mm_s)
{
    double rpm = (double)mm_s * 60.0 * (double)APP_GEAR_REDUCTION_RATIO /
        ((double)APP_WHEEL_CIRCUMFERENCE_M * 1000.0);
    if (rpm > 32766.0) return 32766;
    if (rpm < -32768.0) return -32768;
    return (int16_t)(rpm < 0.0 ? rpm - 0.5 : rpm + 0.5);
}

uint8_t app_system_snapshot_read_nrf(nrf_status_input_t *input,
    nrf_snapshot_context_t *context)
{
    const app_system_snapshot_t *snapshot;
    uint8_t valid;
    if (input == NULL || context == NULL) return 0U;
    taskENTER_CRITICAL();
    valid = current();
    if (valid)
    {
        snapshot = &sys_buffer[sys_active];
        input->remote = snapshot->remote;
        input->position = snapshot->position;
        memcpy(input->axis, snapshot->axis, sizeof(input->axis));
        input->snapshot_sequence = snapshot->sequence;
        input->snapshot_ms = snapshot->now_ms;
        input->control_owner = (uint8_t)snapshot->owner;
        input->can_target_mm_s = snapshot->can_control.target_mm_s;
        input->recovery_generation = snapshot->recovery.generation;
        input->recovery_healthy_ms = snapshot->recovery.healthy_ms;
        input->recovery_state = (uint8_t)snapshot->recovery.state;
        input->recovery_eligible = snapshot->recovery.eligible;
        input->recovery_failure = snapshot->recovery.failure;
        context->cli = snapshot->cli;
        memcpy(context->startup, snapshot->startup, sizeof(context->startup));
        memcpy(context->hold, snapshot->hold, sizeof(context->hold));
        memcpy(context->zero, snapshot->boot_zero, sizeof(context->zero));
        memcpy(context->watchdog, snapshot->watchdog, sizeof(context->watchdog));
        context->reset = snapshot->reset_reason;
        context->health = snapshot->diagnostic;
        context->ready_mask = snapshot->ready_mask;
        context->hold_active = snapshot->hold_active;
        context->motion_allowed = snapshot->motion_allowed;
        context->maintenance = snapshot->maintenance;
        context->motion = snapshot->motion;
        context->startup_valid = snapshot->startup_valid;
        context->owner = (uint8_t)snapshot->owner;
        context->authorized = snapshot->lock_reason != APP_CONTROL_LOCK_NONE ? 0U :
            snapshot->owner == APP_CONTROL_OWNER_HMI ?
            (uint8_t)((snapshot->hmi_control.flags & 2U) != 0U) :
            snapshot->owner == APP_CONTROL_OWNER_CAN ?
            (uint8_t)(snapshot->can_control_valid && snapshot->can_control.authorized) : snapshot->owner == APP_CONTROL_OWNER_REMOTE ? snapshot->remote.authorized : 0U;
        context->target_rpm = snapshot->remote.target_rpm;
        context->command_rpm[0] = snapshot->remote.left_commanded_rpm;
        context->command_rpm[1] = snapshot->remote.right_commanded_rpm;
        if (snapshot->owner == APP_CONTROL_OWNER_HMI)
        {
            context->target_rpm = snapshot->hmi_control.target;
            memcpy(context->command_rpm, snapshot->hmi_control.output,
                sizeof(context->command_rpm));
            input->remote.sync = snapshot->hmi_control.sync;
        }
        else if (snapshot->owner == APP_CONTROL_OWNER_CAN)
        {
            context->target_rpm = target_rpm(snapshot->can_control.target_mm_s);
            memcpy(context->command_rpm, snapshot->can_control.commanded_rpm,
                sizeof(context->command_rpm));
            input->remote.sync = snapshot->can_control.sync;
        }
    }
    if (valid) input->control_authorized = context->authorized;
    if (valid && !context->authorized)
    {
        context->target_rpm = 0;
        context->command_rpm[0] = context->command_rpm[1] = 0;
    }
    taskEXIT_CRITICAL();
    return valid;
}

uint8_t app_system_snapshot_read_can(app_can_status_input_t *input)
{
    const app_system_snapshot_t *snapshot;
    uint8_t valid;
    if (input == NULL) return 0U;
    taskENTER_CRITICAL();
    valid = current();
    if (valid)
    {
        snapshot = &sys_buffer[sys_active];
        memcpy(input->axis, snapshot->axis, sizeof(input->axis));
        input->remote = snapshot->remote;
        input->cli = snapshot->cli;
        input->position = snapshot->position;
        input->control = snapshot->can_control;
        input->hmi = snapshot->hmi_control;
        input->hmi_remaining_ms = snapshot->hmi_remaining;
        input->arbiter_owner = (app_control_owner_t)snapshot->owner;
        input->arbiter_generation = snapshot->arbiter_generation;
        input->arbiter_axes = snapshot->arbiter_axes;
        input->lock_reason = (app_control_lock_reason_t)snapshot->lock_reason;
        input->stop = snapshot->stop;
        memcpy(input->watchdog, snapshot->watchdog, sizeof(input->watchdog));
        input->recovery = snapshot->recovery;
        input->source_valid = snapshot->axes_valid && snapshot->remote_valid &&
            snapshot->position_valid && snapshot->cli_valid && snapshot->startup_valid;
        input->snapshot_ms = snapshot->now_ms;
        input->snapshot_sequence = snapshot->sequence;
        input->sample_tick = snapshot->now_tick;
        input->selected = snapshot->selected;
        input->diagnostic = snapshot->diagnostic;
        input->reset_reason = snapshot->reset_reason;
        input->minimum_stack = snapshot->minimum_stack;
        input->bus = snapshot->can.bus;
        input->uart = snapshot->debug_uart;
        input->modbus = snapshot->modbus;
        input->nrf_generation = snapshot->nrf_generation;
        input->heartbeat_ages[0] = age16(snapshot->heartbeat_ms[APP_HEARTBEAT_MOTION]);
        input->heartbeat_ages[1] = age16(snapshot->heartbeat_ms[APP_HEARTBEAT_SERVO_COMM]);
        input->heartbeat_ages[2] = age16(snapshot->heartbeat_ms[APP_HEARTBEAT_NRF]);
        input->heartbeat_ages[3] = 0U;
        input->failed_heartbeat_mask = (snapshot->diagnostic & APP_DIAG_HEARTBEAT_TIMEOUT) &&
            snapshot->failed_task < APP_HEARTBEAT_COUNT ? 1UL << snapshot->failed_task : 0U;
        input->gates = snapshot->ready_mask;
        if (input->gates == 15U) input->gates |= 16U;
    }
    else input->source_valid = false;
    taskEXIT_CRITICAL();
    return valid;
}

/* Copy only primitive inputs while protected. Conversion never masks UART IRQs. */
void app_tune_snapshot(app_tune_input_t *out)
{
    const app_system_snapshot_t *s;
    TickType_t sample[2], tick;
    uint32_t age[2], now, started;
    uint8_t fresh, feedback[2]; unsigned i;
    memset(out,0,sizeof(*out));
    started=app_runtime_clock();
    taskENTER_CRITICAL();
    now=HAL_GetTick(); tick=xTaskGetTickCount(); s=&sys_buffer[sys_active];
    fresh=sys_initialized && (uint32_t)(now-s->now_ms)<APP_SYSTEM_SNAPSHOT_MAX_AGE_MS;
    out->boot=s->boot_id;out->ms=s->now_ms;out->generation=s->arbiter_generation;
    out->owner=(uint8_t)s->owner;
    out->source_valid[0]=fresh && s->lock_reason==APP_CONTROL_LOCK_NONE && s->remote_valid && s->remote.authorized && s->remote.link_valid && s->owner==APP_CONTROL_OWNER_REMOTE;
    out->source_valid[1]=fresh && s->lock_reason==APP_CONTROL_LOCK_NONE && (s->hmi_control.flags&2U) && s->hmi_remaining && s->owner==APP_CONTROL_OWNER_HMI;
    out->source_valid[2]=fresh && s->lock_reason==APP_CONTROL_LOCK_NONE && s->can_control_valid && s->can_control.authorized && s->can_control.remaining_ms && s->owner==APP_CONTROL_OWNER_CAN;
    out->authorized=out->source_valid[0] || out->source_valid[1] || out->source_valid[2];
    for(i=0U;i<2U;++i) {
        out->actual[i]=s->axis[i].actual_speed_rpm;
        out->sequence[i]=s->axis[i].speed_sequence;
        sample[i]=s->axis[i].speed_sample_tick;age[i]=s->axis[i].speed_age_ms;
        feedback[i]=s->axes_valid && s->axis[i].speed_valid && !s->axis[i].feedback_degraded;
        if(s->owner==APP_CONTROL_OWNER_REMOTE) out->commanded[i]=i?s->remote.right_commanded_rpm:s->remote.left_commanded_rpm;
        if(s->owner==APP_CONTROL_OWNER_HMI) out->commanded[i]=s->hmi_control.output[i];
        if(s->owner==APP_CONTROL_OWNER_CAN) out->commanded[i]=s->can_control.commanded_rpm[i];
    }
    taskEXIT_CRITICAL();
    app_runtime_max(APP_RUNTIME_TUNE_CRITICAL,app_runtime_clock()-started);
    for(i=0U;i<2U;++i) {
        uint32_t delta=now-out->ms;
        if(age[i]==UINT32_MAX || age[i]>UINT32_MAX-delta) out->age[i]=UINT32_MAX;
        else out->age[i]=age[i]+delta;
        out->sample_ms[i]=age[i]==UINT32_MAX?UINT32_MAX:
            now-(uint32_t)((uint64_t)(TickType_t)(tick-sample[i])*1000U/configTICK_RATE_HZ);
        out->valid[i]=fresh && feedback[i] && out->age[i]<APP_REMOTE_SERVO_SYNC_MAX_AGE_MS;
    }
}

uint8_t app_system_snapshot_read_diagnostic(app_tune_diag_system_t *out)
{
    const app_system_snapshot_t *s;unsigned i;uint8_t valid;
    taskENTER_CRITICAL();s=&sys_buffer[sys_active];
    valid=sys_initialized;
    out->now_ms=s->now_ms;out->reset_flags=s->reset_reason;out->diagnostic=s->diagnostic;
    out->watchdog_decision=s->watchdog_decision;out->failed_task=s->failed_task;
    out->owner=s->owner;out->lock_reason=s->lock_reason;
    out->motion_allowed=s->motion_allowed;out->ready_mask=s->ready_mask;
    out->arbiter_generation=s->arbiter_generation;
    out->snapshot_fresh=valid && (uint32_t)(HAL_GetTick()-s->now_ms)<APP_SYSTEM_SNAPSHOT_MAX_AGE_MS;
    out->control_authorized=s->lock_reason==APP_CONTROL_LOCK_NONE &&
        ((s->owner==APP_CONTROL_OWNER_HMI && (s->hmi_control.flags&2U) && s->hmi_remaining) ||
         (s->owner==APP_CONTROL_OWNER_REMOTE && s->remote_valid && s->remote.authorized && s->remote.link_valid) ||
         (s->owner==APP_CONTROL_OWNER_CAN && s->can_control_valid && s->can_control.authorized && s->can_control.remaining_ms));
    for(i=0U;i<5U;i++) out->heartbeat_ms[i]=s->heartbeat_ms[i];
    for(i=0U;i<12U;i++) out->stack_words[i]=sys_stack_words[i];
    taskEXIT_CRITICAL();return valid;
}
