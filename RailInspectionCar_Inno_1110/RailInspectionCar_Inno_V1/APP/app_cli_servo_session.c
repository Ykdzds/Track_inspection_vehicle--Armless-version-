#include "app_cli_servo_session.h"

#include <string.h>
#include "task.h"
#include "app_config.h"
#include "app_health.h"
#include "app_motion_ramp.h"
#include "app_remote_servo.h"
#include "motion_control.h"
#include "servo_pulse.h"
#include "app_control_arbiter.h"

#if APP_CLI_SERVO_ACTIVE
static cli_servo_session_status_t s_session;
static cli_servo_request_t s_request;
static TickType_t s_keepalive;
static TickType_t s_prepare_tick;
static TickType_t s_stop_tick;
static uint32_t s_stop_sequence;
static uint8_t s_zero_samples;
static uint8_t s_stop_requested;
static uint8_t s_off_requested;
static uint8_t s_abort_requested;
static uint8_t s_attached;
static uint8_t s_stop_started;
static uint8_t s_restore_started;
static uint8_t s_rearming;
static uint8_t s_speed_stop;
static uint8_t s_disable_started;
static uint32_t s_acquire_ticket;
static uint8_t s_external_stop;
static app_motion_ramp_t s_external_ramp;
static TickType_t s_external_tick, s_external_start;
static uint32_t s_external_timeout, s_external_write;
static int16_t s_external_output;
static uint16_t s_external_deceleration = 500U;

void app_cli_servo_session_external_stop(void)
{
    (void)app_control_arbiter_submit_stop(5U, 500U, 0U, 0x10U);
}

static uint32_t elapsed_ms(TickType_t now, TickType_t then)
{
    return (uint32_t)((uint64_t)(TickType_t)(now - then) * 1000U /
                      configTICK_RATE_HZ);
}

static bool feedback_safe(const servo_snapshot_t *axis)
{
    return axis->online && !axis->fault && axis->speed_valid &&
        axis->fault_valid && axis->speed_age_ms < APP_SERVO_STATUS_MAX_AGE_MS &&
        axis->fault_age_ms < APP_SERVO_STATUS_MAX_AGE_MS;
}

static bool startup_ready(void)
{
    return !app_health_watchdog_test_requested() &&
        aimtor_watchdog_recovery_ready() && aimtor_startup_stop_ready() &&
        aimtor_boot_zero_ready() && app_config_motion_hardware_ready() &&
        app_config_servo_bus_ready() && !app_config_servo_readonly_test();
}

static bool remote_busy(void)
{
    remote_servo_status_t remote;
    if (!app_remote_servo_get_status(&remote) ||
        (app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE &&
         app_control_arbiter_owner() != APP_CONTROL_OWNER_CLI)) return true;
    return remote.authorized || remote.state == REMOTE_SERVO_ARMING ||
        remote.hold_stopping || remote.state == REMOTE_SERVO_STOPPING ||
        (remote.estop_latched && remote.stop_reason == REMOTE_STOP_K1);
}

void app_cli_servo_session_init(void)
{
    memset(&s_session, 0, sizeof(s_session));
    s_stop_requested = s_off_requested = s_abort_requested = 0U;
    s_attached = s_stop_started = s_restore_started = s_rearming = 0U;
    s_speed_stop = s_disable_started = 0U;
    s_acquire_ticket = 0U;
    s_external_stop = 0U; s_external_write = 0U;
    servo_debug_release_axis();
}

void app_cli_servo_session_recovery_reset(void)
{
    uint32_t session = s_session.session_id, generation = s_session.generation;
    aimtor_cli_gate_init();
    app_cli_servo_session_init();
    s_session.session_id = session; s_session.generation = generation + 1U;
    memset(&s_request, 0, sizeof(s_request));
}

bool app_cli_servo_session_active(void)
{
    return s_session.state != CLI_SESSION_IDLE;
}

servo_result_t app_cli_servo_session_on(servo_axis_t axis, uint32_t *session)
{
    servo_axis_t a;
    servo_snapshot_t feedback;
    uint8_t rearming;
    if (!APP_SERVO_CLI_DEBUG_ENABLED || app_config_servo_readonly_test() ||
        !app_config_servo_axis_controllable((uint8_t)axis))
        return SERVO_NOT_CONFIGURED;
    taskENTER_CRITICAL();
    rearming = s_session.state == CLI_SESSION_LOCKED;
    if ((s_session.state != CLI_SESSION_IDLE && !rearming) || remote_busy())
    {
        taskEXIT_CRITICAL();
        return SERVO_BUSY;
    }
    if (aimtor_recovery_blocks_motion() || !startup_ready() || (rearming && !s_session.recoverable) ||
        (!rearming && !aimtor_startup_hold_ready()))
    {
        taskEXIT_CRITICAL();
        return SERVO_NOT_READY;
    }
    for (a = SERVO_AXIS_LEFT; a < SERVO_AXIS_COUNT; a++)
    {
        if (!app_config_servo_axis_selected((uint8_t)a)) continue;
        if (!aimtor_get_snapshot(a, &feedback) || feedback.feedback_degraded || !feedback_safe(&feedback) ||
            !feedback.enable_feedback_valid || feedback.enable_feedback != feedback.enabled ||
            feedback.enable_feedback_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
            feedback.actual_speed_rpm || feedback.busy ||
            servo_pulse_output_active(a) ||
            (rearming && (feedback.enabled || !feedback.disable_verified)) ||
            (!rearming && feedback.stop_latched))
        {
            taskEXIT_CRITICAL();
            return SERVO_NOT_READY;
        }
    }
    s_session.session_id++;
    if (!s_session.session_id) s_session.session_id++;
    s_session.generation++;
    s_session.axis = axis;
    s_session.state = CLI_SESSION_ENTERING;
    s_session.reason = CLI_EXIT_NONE;
    s_session.authorized = s_session.pending = 0U;
    s_session.last_result = SERVO_BUSY;
    s_keepalive = xTaskGetTickCount();
    s_prepare_tick = s_keepalive;
    s_stop_requested = s_off_requested = s_abort_requested = 0U;
    s_attached = s_stop_started = s_restore_started = 0U;
    s_rearming = rearming;
    if (rearming) (void)app_control_arbiter_clear_safety();
    s_acquire_ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_CLI, (uint8_t)(1U << axis));
    if (!s_acquire_ticket)
    {
        s_session.state = rearming ? CLI_SESSION_LOCKED : CLI_SESSION_IDLE;
        taskEXIT_CRITICAL();
        return SERVO_BUSY;
    }
    if (session) *session = s_session.session_id;
    taskEXIT_CRITICAL();
    return SERVO_OK;
}

bool app_cli_servo_session_keepalive(uint32_t session)
{
    bool accepted;
    taskENTER_CRITICAL();
    accepted = !app_health_watchdog_test_requested() &&
        session != 0U && session == s_session.session_id &&
        (s_session.state == CLI_SESSION_ENTERING ||
         s_session.state == CLI_SESSION_READY) &&
        elapsed_ms(xTaskGetTickCount(), s_keepalive) <
            APP_SERVO_CLI_DEBUG_TIMEOUT_MS && !s_abort_requested &&
        !s_off_requested;
    if (accepted) s_keepalive = xTaskGetTickCount();
    taskEXIT_CRITICAL();
    return accepted;
}

servo_result_t app_cli_servo_session_off(void)
{
    taskENTER_CRITICAL();
    if (s_session.state == CLI_SESSION_IDLE)
    {
        taskEXIT_CRITICAL();
        return SERVO_OK;
    }
    if (s_session.state == CLI_SESSION_LOCKED)
    {
        taskEXIT_CRITICAL();
        return SERVO_NOT_READY;
    }
    if (s_off_requested || s_session.state == CLI_SESSION_RESTORING)
    {
        taskEXIT_CRITICAL();
        return SERVO_OK;
    }
    s_off_requested = s_stop_requested = 1U;
    s_session.authorized = s_session.pending = 0U;
    s_session.generation++;
    aimtor_cli_authorize(false);
    taskEXIT_CRITICAL();
    return SERVO_OK;
}

servo_result_t app_cli_servo_session_request(const cli_servo_request_t *request)
{
    if (!request) return SERVO_INVALID_ARGUMENT;
    if (aimtor_recovery_blocks_motion()) return SERVO_NOT_READY;
    taskENTER_CRITICAL();
    if (app_health_watchdog_test_requested() ||
        s_session.state != CLI_SESSION_READY || !s_session.authorized ||
        s_off_requested || s_abort_requested || s_stop_requested ||
        request->axis != s_session.axis)
    {
        taskEXIT_CRITICAL();
        return SERVO_NOT_READY;
    }
    if (s_session.pending)
    {
        taskEXIT_CRITICAL();
        return SERVO_BUSY;
    }
    s_request = *request;
    s_request.generation = s_session.generation;
    s_session.pending = 1U;
    taskEXIT_CRITICAL();
    return SERVO_OK;
}

void app_cli_servo_session_stop(void)
{
    taskENTER_CRITICAL();
    if (s_session.state == CLI_SESSION_IDLE ||
        s_session.state == CLI_SESSION_LOCKED ||
        s_session.state == CLI_SESSION_RESTORING ||
        s_session.state == CLI_SESSION_STOPPING || s_stop_requested)
    {
        taskEXIT_CRITICAL();
        return;
    }
    s_stop_requested = 1U;
    s_session.authorized = s_session.pending = 0U;
    s_session.generation++;
    aimtor_cli_authorize(false);
    taskEXIT_CRITICAL();
    (void)motion_stop(MOTION_STOP_IMMEDIATE);
}

void app_cli_servo_session_abort(cli_session_reason_t reason)
{
    taskENTER_CRITICAL();
    if (s_session.state == CLI_SESSION_IDLE)
    {
        taskEXIT_CRITICAL();
        return;
    }
    if (s_session.state == CLI_SESSION_LOCKED && !s_session.recoverable &&
        reason != CLI_EXIT_SAFETY && reason != CLI_EXIT_RESTORE_FAILED)
    {
        taskEXIT_CRITICAL();
        return;
    }
    if (!s_abort_requested || reason == CLI_EXIT_SAFETY ||
        reason == CLI_EXIT_RESTORE_FAILED)
    {
        s_abort_requested = (uint8_t)reason;
    }
    s_session.authorized = s_session.pending = 0U;
    s_session.generation++;
    aimtor_cli_authorize(false);
    taskEXIT_CRITICAL();
    (void)motion_stop(MOTION_STOP_IMMEDIATE);
}

static void lock_session(cli_session_reason_t reason)
{
    uint8_t recoverable = reason == CLI_EXIT_TIMEOUT ||
        reason == CLI_EXIT_K1 || reason == CLI_EXIT_MANUAL;
    taskENTER_CRITICAL();
    if (s_session.state == CLI_SESSION_LOCKED && !s_session.recoverable)
    {
        reason = s_session.reason;
        recoverable = 0U;
    }
    s_session.authorized = s_session.pending = 0U;
    s_session.reason = reason;
    s_session.recoverable = recoverable;
    s_session.state = CLI_SESSION_LOCKED;
    s_session.last_result = SERVO_NOT_READY;
    /* 人工锁存保留显式重建待机资格；真实故障始终使用不可绕过路径。 */
    if (recoverable) aimtor_emergency_stop_k1();
    else aimtor_emergency_stop();
    (void)aimtor_cli_take_abort();
    servo_debug_abort_from_fault();
    aimtor_cli_release_axis();
    app_control_arbiter_finish_stop(APP_CONTROL_OWNER_CLI, 5U, 14U, 0U, 0U);
    app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
    s_abort_requested = s_stop_requested = s_off_requested = 0U;
    s_stop_started = s_restore_started = 0U;
    taskEXIT_CRITICAL();
}

static servo_result_t execute_request(const cli_servo_request_t *request)
{
    switch (request->kind)
    {
    case CLI_REQUEST_MODE: return servo_debug_prepare(request->mode);
    case CLI_REQUEST_ENABLE: return servo_debug_enable();
    case CLI_REQUEST_SPEED: return servo_debug_set_speed((int16_t)request->value);
    case CLI_REQUEST_POSITION:
        return servo_debug_run_position(request->value, request->speed, request->ramp);
    case CLI_REQUEST_PULSE:
        return servo_debug_run_pulse(request->forward != 0U, request->frequency,
                                    (uint32_t)request->value);
    case CLI_REQUEST_VERIFY: return aimtor_request_parameter_verify(request->axis);
    case CLI_REQUEST_ZERO: return aimtor_zero_once(request->axis);
    case CLI_REQUEST_SAVE: return aimtor_save_parameters(request->axis);
    default: return SERVO_INVALID_ARGUMENT;
    }
}

void app_cli_servo_session_service(void)
{
    TickType_t now = xTaskGetTickCount();
    servo_snapshot_t feedback;
    cli_servo_request_t request;
    uint8_t abort;
    uint8_t have_request;
    servo_result_t result;
    uint8_t bottom_abort = aimtor_cli_take_abort();
    app_control_stop_t stop;
    if (aimtor_safety_stop_active() || aimtor_recovery_blocks_motion())
    {
        if (s_session.state == CLI_SESSION_IDLE) return;
        if (s_session.state == CLI_SESSION_ENTERING && elapsed_ms(now, s_prepare_tick) >= 2000U)
            s_session.reason = CLI_EXIT_TIMEOUT;
        else if (s_session.state != CLI_SESSION_LOCKED)
            s_session.reason = CLI_EXIT_SAFETY;
        s_session.authorized = s_session.pending = 0U;
        s_session.recoverable = 0U;
        s_session.state = CLI_SESSION_LOCKED;
        s_stop_requested = s_abort_requested = s_off_requested = 0U;
        aimtor_cli_authorize(false);
        return;
    }
    if (app_control_arbiter_take_stop(APP_CONTROL_OWNER_CLI, &stop))
    {
        if (!s_external_stop)
        { s_external_start = xTaskGetTickCount(); s_external_deceleration = stop.deceleration_mm_s2 ? stop.deceleration_mm_s2 : 500U; }
        else if (stop.deceleration_mm_s2 > s_external_deceleration)
            s_external_deceleration = stop.deceleration_mm_s2;
        s_external_stop = 1U; s_off_requested = 1U;
        if (s_session.state != CLI_SESSION_STOPPING && s_session.state != CLI_SESSION_RESTORING)
            s_stop_requested = 1U;
        s_session.authorized = s_session.pending = 0U;
        s_session.generation++; aimtor_cli_authorize(false);
    }
    if (bottom_abort && app_cli_servo_session_active())
        app_cli_servo_session_abort(CLI_EXIT_SAFETY);
    taskENTER_CRITICAL();
    abort = s_abort_requested;
    taskEXIT_CRITICAL();
    if (abort)
    {
        lock_session((cli_session_reason_t)abort);
        return;
    }
    if (s_session.state == CLI_SESSION_IDLE) return;
    if (s_session.state == CLI_SESSION_LOCKED)
    {
        servo_axis_t axis;
        for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!app_config_servo_axis_selected((uint8_t)axis)) continue;
            if (!aimtor_get_snapshot(axis, &feedback) ||
                !feedback_safe(&feedback))
            {
                s_session.recoverable = 0U;
                s_session.reason = CLI_EXIT_SAFETY;
            }
        }
        return;
    }
    if ((s_session.state == CLI_SESSION_ENTERING ||
         s_session.state == CLI_SESSION_READY) &&
        elapsed_ms(now, s_session.state == CLI_SESSION_ENTERING ? s_prepare_tick : s_keepalive) >= (s_session.state == CLI_SESSION_ENTERING ?
            2000U : APP_SERVO_CLI_DEBUG_TIMEOUT_MS))
    {
        lock_session(CLI_EXIT_TIMEOUT);
        return;
    }
    {
        servo_axis_t axis;
        /* 非POR也核验另一选中轴，不能因没有待机状态机而漏掉通信失效。 */
        for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
        {
            servo_snapshot_t selected;
            if (!app_config_servo_axis_selected((uint8_t)axis)) continue;
            if (!aimtor_get_snapshot(axis, &selected) || !feedback_safe(&selected))
            {
                lock_session(CLI_EXIT_SAFETY);
                return;
            }
        }
    }
    if (!aimtor_get_snapshot(s_session.axis, &feedback)) return;
    if (s_session.state == CLI_SESSION_ENTERING && !s_off_requested)
    {
        servo_k1_hold_status_t hold;
        if (aimtor_k1_hold_get_status(&hold) && hold.state != SERVO_K1_HOLD_INACTIVE &&
            aimtor_k1_hold_release() != SERVO_OK)
        { lock_session(CLI_EXIT_SAFETY); return; }
        if (s_acquire_ticket)
        {
            int8_t granted = app_control_arbiter_poll(s_acquire_ticket, &s_session.generation);
            if (granted == 0) return;
            if (granted < 0 || !app_control_arbiter_request(APP_CONTROL_OWNER_CLI,
                (uint8_t)(1U << s_session.axis), &s_session.generation))
            { s_session.state = CLI_SESSION_IDLE; s_session.last_result = SERVO_BUSY; return; }
            s_acquire_ticket = 0U;
        }
        if (s_rearming)
        {
            if (aimtor_startup_hold_active())
            {
                /* 恢复整组人工锁存时，暂停目标托管，先完成原待机重建。 */
                aimtor_cli_release_axis();
                if (!aimtor_startup_hold_ready())
                {
                    result = aimtor_startup_hold_request_rearm();
                    if (result != SERVO_OK && result != SERVO_NOT_READY &&
                        result != SERVO_BUSY) lock_session(CLI_EXIT_SAFETY);
                    return;
                }
            }
            else
            {
                result = aimtor_clear_stop_latch();
                if (result != SERVO_OK) return;
            }
            s_rearming = 0U;
        }
        if (!s_attached)
        {
            aimtor_cli_take_axis(s_session.axis);
            servo_debug_select_axis(s_session.axis);
            s_attached = 1U;
        }
        if (feedback.busy || feedback.actual_speed_rpm || feedback.feedback_degraded ||
            !feedback.enable_feedback_valid || feedback.enable_feedback != feedback.enabled ||
            feedback.enable_feedback_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS) return;
        if (feedback.enabled &&
            feedback.control_mode == SERVO_CONTROL_RS485_VELOCITY &&
            feedback.parameter_verified &&
            feedback.ramp_read_mask == 3U && feedback.ramp_match_mask == 3U &&
            feedback.ramp_readback_ms[0] == APP_SERVO_DIRECT_ACCELERATION_RAMP_MS)
            servo_debug_adopt_speed();
        taskENTER_CRITICAL();
        if (!s_abort_requested && !s_off_requested && !s_stop_requested)
        {
            s_session.authorized = 1U;
            aimtor_cli_authorize(true);
            s_session.state = CLI_SESSION_READY;
            s_session.last_result = SERVO_OK;
        }
        taskEXIT_CRITICAL();
    }
    if (s_stop_requested && s_session.state == CLI_SESSION_STOPPING)
        s_stop_requested = 0U;
    if (s_stop_requested)
    {
        if (s_external_stop && feedback.busy)
        {
            if (elapsed_ms(now, s_external_start) >= 2000U) lock_session(CLI_EXIT_SAFETY);
            return;
        }
        s_stop_requested = 0U;
        if (!s_attached)
        {
            aimtor_cli_take_axis(s_session.axis);
            servo_debug_select_axis(s_session.axis);
            s_attached = 1U;
        }
        s_external_output = feedback.actual_speed_rpm;
        if (s_external_stop)
        {
            servo_debug_status_t debug;
            if (servo_debug_get_status(&debug) && debug.mode == SERVO_DEBUG_MODE_RS485_SPEED &&
                (debug.target_rpm < 0 ? -debug.target_rpm : debug.target_rpm) >
                (s_external_output < 0 ? -s_external_output : s_external_output))
                s_external_output = debug.target_rpm;
        }
        servo_debug_abort_from_fault();
        if (!s_external_stop || feedback.control_mode != SERVO_CONTROL_RS485_VELOCITY)
            aimtor_cli_cancel_actions();
        s_session.pending = 0U;
        s_session.state = CLI_SESSION_STOPPING;
        s_session.authorized = 0U;
        aimtor_cli_authorize(false);
        s_stop_tick = s_external_stop ? s_external_start : now;
        s_stop_sequence = feedback.speed_sequence;
        s_zero_samples = s_stop_started = 0U;
        s_speed_stop = feedback.enabled &&
            feedback.control_mode == SERVO_CONTROL_RS485_VELOCITY;
        s_disable_started = 0U;
        s_external_write = 0U;
        s_external_tick = now;
        s_external_timeout = (uint32_t)((float)(s_external_output < 0 ? -s_external_output : s_external_output) *
            APP_WHEEL_CIRCUMFERENCE_M / (60.0f * APP_GEAR_REDUCTION_RATIO) /
            (s_external_deceleration / 1000.0f) * 1000.0f) + 2000U;
        app_motion_ramp_reset(&s_external_ramp, s_external_output);
        app_control_arbiter_stop_timeout(APP_CONTROL_OWNER_CLI, s_external_timeout);
    }
    if (s_session.state == CLI_SESSION_STOPPING)
    {
        if (elapsed_ms(now, s_stop_tick) >= (s_external_stop && s_speed_stop ? s_external_timeout : 5000U))
        {
            lock_session(CLI_EXIT_SAFETY);
            return;
        }
        if (s_external_stop && s_speed_stop && !s_stop_started)
        {
            if (s_external_write)
            {
                if (feedback.speed_write_completed != s_external_write) return;
                if (feedback.speed_write_result != SERVO_OK) { lock_session(CLI_EXIT_SAFETY); return; }
                s_external_write = 0U;
                if (!s_external_output)
                { s_stop_started = 1U; s_stop_sequence = feedback.speed_sequence; s_zero_samples = 0U; }
            }
            if (!s_stop_started && !feedback.busy)
            {
                int16_t target = app_motion_ramp_step(&s_external_ramp, 0,
                    elapsed_ms(now, s_external_tick), APP_DESIGN_ACCELERATION_MPS2, s_external_deceleration / 1000.0f);
                s_external_tick = now;
                if (target != s_external_output || !target)
                {
                    aimtor_cli_internal(true);
                    result = aimtor_set_stop_velocity_rpm(s_session.axis, target);
                    aimtor_cli_internal(false);
                    if (result == SERVO_OK)
                    {
                        servo_snapshot_t submitted;
                        aimtor_get_snapshot(s_session.axis, &submitted);
                        s_external_write = submitted.speed_write_sequence;
                        s_external_output = target;
                    }
                    else if (result != SERVO_BUSY) lock_session(CLI_EXIT_SAFETY);
                }
            }
            if (!s_stop_started) return;
        }
        aimtor_cli_internal(true);
        if (!s_stop_started && !feedback.busy)
        {
            /* 速度模式先给零，位置模式则终止触发；均保留独立停稳确认。 */
            result = s_speed_stop ? aimtor_set_velocity_rpm(s_session.axis, 0) :
                                    aimtor_disable(s_session.axis);
            if (result == SERVO_OK) s_stop_started = 1U;
            else if (result != SERVO_BUSY) lock_session(CLI_EXIT_SAFETY);
        }
        aimtor_cli_internal(false);
        if (feedback.speed_sequence != s_stop_sequence)
        {
            s_stop_sequence = feedback.speed_sequence;
            if (s_stop_started && feedback.actual_speed_rpm == 0 &&
                !feedback.busy &&
                (s_speed_stop || (feedback.disable_verified && !feedback.enabled)))
                s_zero_samples++;
            else s_zero_samples = 0U;
        }
        if (s_zero_samples < 2U) return;
        if (s_external_stop && s_speed_stop)
        {
            uint16_t mask = (uint16_t)(1U << s_session.axis);
            if (!feedback.enable_feedback_valid || !feedback.enable_feedback ||
                feedback.enable_feedback_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
                !feedback.enabled || feedback.busy) return;
            aimtor_cli_release_axis(); servo_debug_release_axis();
            s_session.state = CLI_SESSION_IDLE; s_session.last_result = SERVO_OK;
            s_session.authorized = s_session.pending = 0U;
            s_attached = s_off_requested = s_external_stop = 0U;
            app_control_arbiter_finish_stop(APP_CONTROL_OWNER_CLI, 3U, 0U, mask, mask);
            app_control_arbiter_release(APP_CONTROL_OWNER_CLI);
            return;
        }
        if (s_speed_stop && !s_disable_started)
        {
            /* 停稳后才失能并清理模式，避免速度停车一开始就丢失保持力。 */
            aimtor_cli_internal(true);
            result = aimtor_disable(s_session.axis);
            aimtor_cli_internal(false);
            if (result == SERVO_OK) s_disable_started = 1U;
            else if (result != SERVO_BUSY) lock_session(CLI_EXIT_SAFETY);
            return;
        }
        if (feedback.enabled || feedback.busy || !feedback.disable_verified)
            return;
        if (!s_off_requested)
        {
            taskENTER_CRITICAL();
            if (s_abort_requested || s_off_requested)
            {
                taskEXIT_CRITICAL();
                return;
            }
            servo_debug_select_axis(s_session.axis);
            s_session.state = CLI_SESSION_READY;
            s_session.authorized = 1U;
            aimtor_cli_authorize(true);
            s_session.last_result = SERVO_OK;
            taskEXIT_CRITICAL();
            return;
        }
        s_session.reason = CLI_EXIT_OFF;
        s_session.state = CLI_SESSION_RESTORING;
        s_stop_tick = now;
        s_restore_started = 0U;
    }
    if (s_session.state == CLI_SESSION_RESTORING)
    {
        if (elapsed_ms(now, s_stop_tick) >= APP_SERVO_STARTUP_HOLD_TIMEOUT_MS)
        {
            lock_session(CLI_EXIT_RESTORE_FAILED);
            return;
        }
        if (aimtor_startup_hold_active())
        {
            if (!s_restore_started)
            {
                result = aimtor_cli_restore_hold();
                if (result == SERVO_OK) s_restore_started = 1U;
                else if (result != SERVO_NOT_READY && result != SERVO_BUSY)
                    lock_session(CLI_EXIT_RESTORE_FAILED);
                return;
            }
            if (!aimtor_startup_hold_ready()) return;
        }
        taskENTER_CRITICAL();
        if (s_abort_requested)
        {
            taskEXIT_CRITICAL();
            return;
        }
        aimtor_cli_release_axis();
        servo_debug_release_axis();
        if (s_external_stop)
        {
            app_control_arbiter_finish_stop(APP_CONTROL_OWNER_CLI, 3U,
                s_speed_stop ? 0U : 9U, (uint16_t)(1U << s_session.axis),
                aimtor_startup_hold_ready() ? (uint16_t)(1U << s_session.axis) : 0U);
            s_external_stop = 0U;
        }
        s_session.state = CLI_SESSION_IDLE;
        s_session.last_result = SERVO_OK;
        s_session.authorized = s_session.pending = 0U;
        s_attached = s_off_requested = 0U;
        app_control_arbiter_release(APP_CONTROL_OWNER_CLI);
        taskEXIT_CRITICAL();
        return;
    }
    taskENTER_CRITICAL();
    have_request = s_session.pending;
    request = s_request;
    s_session.pending = 0U;
    taskEXIT_CRITICAL();
    /* 请求启动与撤权使用同一短临界区，代次失效后不能启动维护或运动。 */
    taskENTER_CRITICAL();
    if (have_request && !s_abort_requested && !s_off_requested &&
        !s_stop_requested && s_session.state == CLI_SESSION_READY &&
        s_session.authorized && request.generation == s_session.generation)
        s_session.last_result = execute_request(&request);
    taskEXIT_CRITICAL();
}

bool app_cli_servo_session_get_status(cli_servo_session_status_t *status)
{
    servo_debug_status_t debug;
    uint32_t age;
    if (!status) return false;
    taskENTER_CRITICAL();
    *status = s_session;
    age = elapsed_ms(xTaskGetTickCount(), s_keepalive);
    status->remaining_ms = (s_session.state == CLI_SESSION_ENTERING ||
        s_session.state == CLI_SESSION_READY) &&
        age < APP_SERVO_CLI_DEBUG_TIMEOUT_MS ?
        APP_SERVO_CLI_DEBUG_TIMEOUT_MS - age : 0U;
    taskEXIT_CRITICAL();
    if (servo_debug_get_status(&debug))
    {
        status->mode = debug.mode;
        status->debug_state = debug.state;
        if (debug.result != SERVO_OK && debug.result != SERVO_BUSY)
            status->last_result = debug.result;
    }
    return true;
}

#else
void app_cli_servo_session_init(void) { }
void app_cli_servo_session_recovery_reset(void) { aimtor_cli_gate_init(); }
bool app_cli_servo_session_active(void) { return false; }
servo_result_t app_cli_servo_session_on(servo_axis_t axis, uint32_t *session)
{
    (void)axis;
    if (session) *session = 0U;
    return SERVO_NOT_CONFIGURED;
}
bool app_cli_servo_session_keepalive(uint32_t session) { (void)session; return false; }
servo_result_t app_cli_servo_session_off(void) { return SERVO_OK; }
servo_result_t app_cli_servo_session_request(const cli_servo_request_t *request)
{
    return request ? SERVO_NOT_CONFIGURED : SERVO_INVALID_ARGUMENT;
}
void app_cli_servo_session_stop(void) { }
void app_cli_servo_session_abort(cli_session_reason_t reason) { (void)reason; }
void app_cli_servo_session_service(void) { }
void app_cli_servo_session_external_stop(void)
{
    /* External safety requests remain usable independently of CLI ownership. */
    (void)app_control_arbiter_submit_stop(5U, 500U, 0U, 0x10U);
}
bool app_cli_servo_session_get_status(cli_servo_session_status_t *status)
{
    if (!status) return false;
    memset(status, 0, sizeof(*status));
    status->state = CLI_SESSION_IDLE;
    status->mode = SERVO_DEBUG_MODE_NONE;
    status->debug_state = SERVO_DEBUG_IDLE;
    status->last_result = SERVO_OK;
    return true;
}
#endif /* APP_CLI_SERVO_ACTIVE */

const char *app_cli_servo_session_state_name(cli_session_state_t state)
{
    static const char *const names[] = {
        "idle", "entering", "ready", "stopping", "restoring", "locked"
    };
    return state <= CLI_SESSION_LOCKED ? names[state] : "invalid";
}

const char *app_cli_servo_session_reason_name(cli_session_reason_t reason)
{
    static const char *const names[] = {
        "none", "off", "timeout", "k5", "manual", "safety", "restore_failed"
    };
    return reason <= CLI_EXIT_RESTORE_FAILED ? names[reason] : "invalid";
}

const char *app_cli_servo_control_owner(void)
{
#if APP_CLI_SERVO_ACTIVE
    if (s_session.state == CLI_SESSION_STOPPING ||
        s_session.state == CLI_SESSION_RESTORING ||
        s_session.state == CLI_SESSION_LOCKED) return "safety_stop";
    if (app_cli_servo_session_active()) return "cli";
#endif
    switch (app_control_arbiter_owner()) {
    case APP_CONTROL_OWNER_HMI: return "hmi";
    case APP_CONTROL_OWNER_REMOTE: return "remote";
    case APP_CONTROL_OWNER_CAN: return "can";
    case APP_CONTROL_OWNER_AUTO: return "auto";
    case APP_CONTROL_OWNER_CLI: return "cli";
    default: return "none";
    }
}
