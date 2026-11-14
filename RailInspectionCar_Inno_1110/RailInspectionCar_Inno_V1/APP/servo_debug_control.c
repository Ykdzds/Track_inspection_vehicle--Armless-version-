#include "app_config.h"
#if APP_CLI_SERVO_ACTIVE
#include "servo_debug_control.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "motion_control.h"
#include "servo_pulse.h"

typedef enum
{
    PREPARE_STOP = 0,
    PREPARE_DISABLE,
    PREPARE_CONFIGURE,
    PREPARE_WAIT_CONFIGURE
} servo_debug_prepare_stage_t;

typedef enum
{
    AUTO_START_NONE = 0,
    AUTO_START_SPEED,
    AUTO_START_POSITION,
    AUTO_START_PULSE
} servo_debug_auto_start_t;

static servo_debug_state_t s_state;
static servo_debug_mode_t s_mode;
static servo_debug_mode_t s_requested_mode;
static servo_debug_prepare_stage_t s_prepare_stage;
static servo_debug_auto_start_t s_auto_start;
static uint8_t s_disable_requested;
static uint8_t s_enable_pending;
static uint8_t s_enable_axis;
static uint8_t s_prepare_axis;
static uint8_t s_prepare_done_mask;
static uint8_t s_position_seen_motion;
static uint8_t s_position_direction_error;
static uint32_t s_position_start_sequence;
#if APP_SERVO_PULSE_ENABLED
static uint8_t s_pulse_wait_still;
static uint8_t s_pulse_stop_confirmed;
static uint8_t s_pulse_completion_timeout;
#endif
static TickType_t s_run_deadline;
#if APP_SERVO_PULSE_ENABLED
static TickType_t s_pulse_completion_deadline;
#endif
static TickType_t s_prepare_deadline;
static TickType_t s_stop_deadline;
static int32_t s_target;
static int32_t s_position_start;
static int16_t s_target_rpm;
static uint16_t s_target_ramp_ms;
#if APP_SERVO_PULSE_ENABLED
static uint32_t s_target_frequency_hz;
static uint32_t s_target_pulses;
static uint8_t s_pulse_forward;
static uint8_t s_pulse_position_initialized_mask;
static uint8_t s_pulse_position_stable[SERVO_AXIS_COUNT];
static uint32_t s_pulse_completion_position_sequence[SERVO_AXIS_COUNT];
static uint32_t s_pulse_completion_speed_sequence[SERVO_AXIS_COUNT];
static uint32_t s_pulse_position_last_sequence[SERVO_AXIS_COUNT];
static int32_t s_pulse_position_start[SERVO_AXIS_COUNT];
static int32_t s_pulse_position_now[SERVO_AXIS_COUNT];
static int32_t s_pulse_position_target[SERVO_AXIS_COUNT];
static int32_t s_pulse_position_error[SERVO_AXIS_COUNT];
static int32_t s_pulse_position_last[SERVO_AXIS_COUNT];
static TickType_t s_pulse_position_request_tick[SERVO_AXIS_COUNT];
#endif
static servo_result_t s_result;
static uint8_t s_runtime_axis_valid;
static servo_axis_t s_runtime_axis;

static uint8_t servo_debug_control_mask(void)
{
    return s_runtime_axis_valid ? (uint8_t)(1U << s_runtime_axis) :
           app_config_servo_control_axis_mask();
}

static uint8_t servo_debug_axis_selected(uint8_t axis)
{
    return (uint8_t)((servo_debug_control_mask() & (uint8_t)(1U << axis)) != 0U);
}

/* 双轴脉冲测试使用控制掩码；RS485调试命令仍保持单轴语义。 */
static servo_axis_t servo_debug_axis(void)
{
    if (s_runtime_axis_valid) return s_runtime_axis;
    return (APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_RIGHT) ?
           SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT;
}

static TickType_t servo_debug_deadline(uint32_t milliseconds)
{
    TickType_t ticks;

    ticks = pdMS_TO_TICKS(milliseconds);
    if (ticks == 0U)
    {
        ticks = 1U;
    }
    return xTaskGetTickCount() + ticks;
}

/* 使用有符号差值判断调试状态机的超时，兼容Tick计数回绕。 */
static uint8_t servo_debug_tick_due(TickType_t now, TickType_t deadline)
{
    return (uint8_t)(deadline != 0U &&
                     ((int32_t)(now - deadline) >= 0));
}

static uint8_t servo_debug_selected(void)
{
    return (uint8_t)((s_runtime_axis_valid ||
                     app_config_servo_control_axis_count() == 1U) &&
                     app_config_servo_axis_controllable(
                         (uint8_t)servo_debug_axis()));
}

static uint8_t servo_debug_pulse_selected(void)
{
    uint8_t axis;

    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (servo_debug_axis_selected(axis) &&
            !app_config_servo_axis_controllable(axis))
        {
            return 0U;
        }
    }
    return app_config_servo_control_axis_count() != 0U;
}

static uint8_t servo_debug_busy(const servo_snapshot_t *snapshot)
{
    return snapshot->busy != 0U;
}

#if APP_SERVO_PULSE_ENABLED
static uint8_t servo_debug_pulse_position_due(uint8_t axis,
                                              TickType_t now)
{
    TickType_t period;

    period = pdMS_TO_TICKS(APP_PULSE_COMPLETION_POSITION_POLL_MS);
    if (period == 0U)
    {
        period = 1U;
    }
    return (uint8_t)((s_pulse_position_request_tick[axis] == 0U) ||
                     ((TickType_t)(now -
                                   s_pulse_position_request_tick[axis]) >=
                      period));
}
#endif

#if APP_SERVO_PULSE_ENABLED
static void servo_debug_request_pulse_position(uint8_t axis)
{
    TickType_t now;

    now = xTaskGetTickCount();
    if (!servo_debug_pulse_position_due(axis, now))
    {
        return;
    }
    if (aimtor_request_position((servo_axis_t)axis) == SERVO_OK)
    {
        s_pulse_position_request_tick[axis] = now;
    }
}
#endif

/* 取消尚未启动的命令，避免停止后旧命令在异步事务结束时重新启动。 */
static void servo_debug_cancel_auto_start(void)
{
    s_auto_start = AUTO_START_NONE;
    s_target = 0;
    s_target_rpm = 0;
#if APP_SERVO_PULSE_ENABLED
    s_target_frequency_hz = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_target_pulses = 0U;
#endif
}

/* 底层已经接受运动命令后，仅清除待启动标志，保留目标值供状态查询。 */
static void servo_debug_finish_auto_start(void)
{
    s_auto_start = AUTO_START_NONE;
}

static void servo_debug_enter_fault(servo_result_t result)
{
    servo_debug_cancel_auto_start();
    s_enable_pending = 0U;
    s_enable_axis = 0U;
    s_prepare_axis = 0U;
    s_prepare_done_mask = 0U;
    s_disable_requested = 0U;
#if APP_SERVO_PULSE_ENABLED
    s_pulse_wait_still = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_pulse_stop_confirmed = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_pulse_completion_timeout = 0U;
#endif
    s_prepare_deadline = 0U;
    s_stop_deadline = 0U;
    s_run_deadline = 0U;
    s_result = result;
#if APP_SERVO_PULSE_ENABLED
    s_pulse_completion_deadline = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_pulse_position_initialized_mask = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    memset(s_pulse_position_stable, 0, sizeof(s_pulse_position_stable));
#endif
#if APP_SERVO_PULSE_ENABLED
    memset(s_pulse_completion_position_sequence, 0,
           sizeof(s_pulse_completion_position_sequence));
#endif
#if APP_SERVO_PULSE_ENABLED
    memset(s_pulse_completion_speed_sequence, 0,
           sizeof(s_pulse_completion_speed_sequence));
#endif
#if APP_SERVO_PULSE_ENABLED
    memset(s_pulse_position_last_sequence, 0,
           sizeof(s_pulse_position_last_sequence));
#endif
#if APP_SERVO_PULSE_ENABLED
    memset(s_pulse_position_request_tick, 0,
           sizeof(s_pulse_position_request_tick));
#endif
#if APP_SERVO_PULSE_ENABLED
    memset(s_pulse_position_last_sequence, 0,
           sizeof(s_pulse_position_last_sequence));
#endif
    s_state = SERVO_DEBUG_FAULT;
}

void servo_debug_init(void)
{
    s_state = SERVO_DEBUG_IDLE;
    s_mode = SERVO_DEBUG_MODE_NONE;
    s_requested_mode = SERVO_DEBUG_MODE_NONE;
    s_prepare_stage = PREPARE_STOP;
    s_auto_start = AUTO_START_NONE;
    s_disable_requested = 0U;
    s_enable_pending = 0U;
    s_position_seen_motion = 0U;
    s_position_direction_error = 0U;
    s_position_start_sequence = 0U;
#if APP_SERVO_PULSE_ENABLED
    s_pulse_wait_still = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_pulse_stop_confirmed = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_pulse_completion_timeout = 0U;
#endif
    s_run_deadline = 0U;
    s_prepare_deadline = 0U;
#if APP_SERVO_PULSE_ENABLED
    s_pulse_completion_deadline = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_pulse_position_initialized_mask = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    memset(s_pulse_position_stable, 0, sizeof(s_pulse_position_stable));
#endif
#if APP_SERVO_PULSE_ENABLED
    memset(s_pulse_position_request_tick, 0,
           sizeof(s_pulse_position_request_tick));
#endif
#if APP_SERVO_PULSE_ENABLED
    memset(s_pulse_position_last_sequence, 0,
           sizeof(s_pulse_position_last_sequence));
#endif
    s_stop_deadline = 0U;
    s_target = 0;
    s_position_start = 0;
    s_target_rpm = 0;
    s_target_ramp_ms = APP_SERVO_DEBUG_POSITION_RAMP_MS;
#if APP_SERVO_PULSE_ENABLED
    s_target_frequency_hz = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_target_pulses = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_pulse_forward = 1U;
#endif
    s_result = SERVO_OK;
}

void servo_debug_select_axis(servo_axis_t axis)
{
    servo_debug_init();
    s_runtime_axis = axis;
    s_runtime_axis_valid = axis < SERVO_AXIS_COUNT;
}

void servo_debug_release_axis(void)
{
    servo_debug_init();
    s_runtime_axis_valid = 0U;
}

void servo_debug_adopt_speed(void)
{
    s_mode = s_requested_mode = SERVO_DEBUG_MODE_RS485_SPEED;
    s_state = SERVO_DEBUG_READY;
    s_result = SERVO_OK;
}

servo_axis_t servo_debug_get_axis(void) { return servo_debug_axis(); }

servo_result_t servo_debug_prepare(servo_debug_mode_t mode)
{
#if !APP_SERVO_PULSE_ENABLED
    if (mode == SERVO_DEBUG_MODE_PULSE) return SERVO_NOT_CONFIGURED;
#endif
    if (!aimtor_motion_allowed()) return SERVO_NOT_READY;
    if ((mode < SERVO_DEBUG_MODE_PULSE) ||
        (mode > SERVO_DEBUG_MODE_RS485_POSITION))
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if ((mode == SERVO_DEBUG_MODE_PULSE) ?
        !servo_debug_pulse_selected() : !servo_debug_selected())
    {
        return SERVO_NOT_CONFIGURED;
    }
    if (s_state == SERVO_DEBUG_PREPARING ||
        s_state == SERVO_DEBUG_STOPPING ||
        s_enable_pending != 0U)
    {
        return SERVO_BUSY;
    }
    if (motion_stop(MOTION_STOP_IMMEDIATE) != MOTION_OK)
    {
        servo_debug_enter_fault(SERVO_NOT_READY);
        return SERVO_NOT_READY;
    }
    s_requested_mode = mode;
    s_prepare_stage = PREPARE_STOP;
    s_prepare_axis = 0U;
    s_prepare_done_mask = 0U;
    while (s_prepare_axis < SERVO_AXIS_COUNT &&
           !servo_debug_axis_selected(s_prepare_axis))
    {
        s_prepare_axis++;
    }
    s_disable_requested = 0U;
    s_position_seen_motion = 0U;
    s_position_direction_error = 0U;
    s_position_start_sequence = 0U;
#if APP_SERVO_PULSE_ENABLED
    s_pulse_wait_still = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_pulse_stop_confirmed = 0U;
#endif
#if APP_SERVO_PULSE_ENABLED
    s_pulse_completion_timeout = 0U;
#endif
    s_result = SERVO_BUSY;
    s_state = SERVO_DEBUG_PREPARING;
    s_prepare_deadline = servo_debug_deadline(10000U);
    return SERVO_OK;
}

servo_result_t servo_debug_enable(void)
{
    servo_snapshot_t snapshot;
    servo_result_t result;
    if (!aimtor_motion_allowed()) return SERVO_NOT_READY;

    if (!servo_debug_selected())
    {
        return SERVO_NOT_CONFIGURED;
    }
    if ((s_state != SERVO_DEBUG_READY) ||
        !aimtor_get_snapshot(servo_debug_axis(), &snapshot))
    {
        return SERVO_NOT_READY;
    }
    if (snapshot.busy || snapshot.enabled ||
        !snapshot.parameter_verified ||
        ((s_mode == SERVO_DEBUG_MODE_PULSE) &&
         APP_SERVO_BOOT_ZERO_ENABLED && !snapshot.zero_verified))
    {
        return snapshot.busy ? SERVO_BUSY : SERVO_NOT_READY;
    }
    result = aimtor_enable(servo_debug_axis());
    if (result == SERVO_OK)
    {
        s_enable_pending = 1U;
        s_result = SERVO_BUSY;
    }
    return result;
}

servo_result_t servo_debug_disable(void)
{
    servo_snapshot_t snapshot;
    servo_result_t result;

    if (!servo_debug_selected())
    {
        return SERVO_NOT_CONFIGURED;
    }
    result = servo_debug_stop();
    if ((result != SERVO_OK) && (result != SERVO_BUSY))
    {
        return result;
    }
    if (!aimtor_get_snapshot(servo_debug_axis(), &snapshot))
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (snapshot.busy)
    {
        s_disable_requested = 1U;
        return SERVO_BUSY;
    }
    result = aimtor_disable(servo_debug_axis());
    if ((result == SERVO_OK) || (result == SERVO_BUSY))
    {
        s_disable_requested = 1U;
        s_result = SERVO_BUSY;
        s_state = SERVO_DEBUG_STOPPING;
        s_stop_deadline = servo_debug_deadline(5000U);
    }
    return result;
}

servo_result_t servo_debug_stop(void)
{
    servo_result_t result;

    if (!servo_debug_selected())
    {
        return SERVO_NOT_CONFIGURED;
    }
    servo_debug_cancel_auto_start();
    if (s_state == SERVO_DEBUG_IDLE ||
        s_state == SERVO_DEBUG_READY)
    {
        (void)motion_stop(MOTION_STOP_IMMEDIATE);
        return SERVO_OK;
    }
    if (s_enable_pending != 0U)
    {
        /* 使能事务尚未结束时，等事务完成后立即走失能路径。 */
        s_disable_requested = 1U;
    }
    result = aimtor_stop(servo_debug_axis());
    if ((result == SERVO_OK) || (result == SERVO_BUSY))
    {
        s_result = SERVO_BUSY;
        s_state = SERVO_DEBUG_STOPPING;
        s_stop_deadline = servo_debug_deadline(5000U);
    }
    return result;
}

servo_result_t servo_debug_emergency_stop(void)
{
    if (!servo_debug_selected())
    {
        return SERVO_NOT_CONFIGURED;
    }
    aimtor_emergency_stop();
    servo_debug_enter_fault(SERVO_NOT_READY);
    return SERVO_OK;
}

void servo_debug_abort_from_fault(void)
{
    servo_debug_enter_fault(SERVO_NOT_READY);
}

servo_result_t servo_debug_set_speed(int16_t rpm)
{
    servo_result_t result;
    if (!aimtor_motion_allowed()) return SERVO_NOT_READY;

    if ((rpm > APP_OPERATION_MAX_MOTOR_RPM) ||
        (rpm < -APP_OPERATION_MAX_MOTOR_RPM))
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (rpm == 0)
    {
        if ((s_mode != SERVO_DEBUG_MODE_RS485_SPEED) ||
            ((s_state != SERVO_DEBUG_READY) &&
             (s_state != SERVO_DEBUG_RUNNING)))
        {
            return s_state == SERVO_DEBUG_STOPPING ? SERVO_BUSY :
                                                     SERVO_NOT_READY;
        }
        result = aimtor_set_velocity_rpm(servo_debug_axis(), 0);
        if (result == SERVO_OK)
        {
            s_target_rpm = 0;
            servo_debug_cancel_auto_start();
            s_result = SERVO_BUSY;
            s_state = SERVO_DEBUG_STOPPING;
            s_stop_deadline = servo_debug_deadline(5000U);
        }
        return result;
    }

    if ((s_state == SERVO_DEBUG_RUNNING) ||
        (s_state == SERVO_DEBUG_PREPARING) ||
        (s_state == SERVO_DEBUG_STOPPING) ||
        (s_enable_pending != 0U))
    {
        return SERVO_BUSY;
    }
    if (!servo_debug_selected())
    {
        return SERVO_NOT_CONFIGURED;
    }
    {
        servo_snapshot_t snapshot;

        if (!aimtor_get_snapshot(servo_debug_axis(), &snapshot) ||
            snapshot.fault || snapshot.stop_latched)
        {
            return SERVO_NOT_READY;
        }
    }
    if ((s_state != SERVO_DEBUG_READY) ||
        (s_mode != SERVO_DEBUG_MODE_RS485_SPEED))
    {
        result = servo_debug_prepare(SERVO_DEBUG_MODE_RS485_SPEED);
        if (result != SERVO_OK)
        {
            return result;
        }
    }
    s_requested_mode = SERVO_DEBUG_MODE_RS485_SPEED;
    s_target_rpm = rpm;
    s_target = rpm;
    s_auto_start = AUTO_START_SPEED;
    s_result = SERVO_BUSY;
    return SERVO_OK;
}

servo_result_t servo_debug_run_position(int32_t pulses,
                                        uint16_t rpm,
                                        uint16_t ramp_ms)
{
    servo_result_t result;
    if (!aimtor_motion_allowed()) return SERVO_NOT_READY;
    if ((pulses == 0) ||
        (rpm == 0U) || (rpm > (uint16_t)APP_OPERATION_MAX_MOTOR_RPM) ||
        (ramp_ms == 0U))
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if ((s_state == SERVO_DEBUG_RUNNING) ||
        (s_state == SERVO_DEBUG_PREPARING) ||
        (s_state == SERVO_DEBUG_STOPPING) ||
        (s_enable_pending != 0U))
    {
        return SERVO_BUSY;
    }
    if (!servo_debug_selected())
    {
        return SERVO_NOT_CONFIGURED;
    }
    {
        servo_snapshot_t snapshot;

        if (!aimtor_get_snapshot(servo_debug_axis(), &snapshot) ||
            snapshot.fault || snapshot.stop_latched)
        {
            return SERVO_NOT_READY;
        }
    }
    if ((s_state != SERVO_DEBUG_READY) ||
        (s_mode != SERVO_DEBUG_MODE_RS485_POSITION))
    {
        result = servo_debug_prepare(SERVO_DEBUG_MODE_RS485_POSITION);
        if (result != SERVO_OK)
        {
            return result;
        }
    }
    s_requested_mode = SERVO_DEBUG_MODE_RS485_POSITION;
    s_target = pulses;
    s_target_rpm = (int16_t)rpm;
    s_target_ramp_ms = ramp_ms;
    s_position_seen_motion = 0U;
    s_position_direction_error = 0U;
    s_position_start_sequence = 0U;
    s_run_deadline = servo_debug_deadline(30000U);
    s_auto_start = AUTO_START_POSITION;
    s_result = SERVO_BUSY;
    return SERVO_OK;
}

#if APP_SERVO_PULSE_ENABLED
servo_result_t servo_debug_run_pulse(bool forward,
                                     uint32_t frequency_hz,
                                     uint32_t pulses)
{
    servo_result_t prepare_result;
    if (!aimtor_motion_allowed()) return SERVO_NOT_READY;
    /* 完成超时必须人工重新准备模式，不能直接续用旧运行上下文。 */
    if (s_pulse_completion_timeout != 0U)
    {
        return SERVO_NOT_READY;
    }
    if ((frequency_hz < APP_PULSE_MIN_HZ) ||
        (frequency_hz > APP_PULSE_MAX_HZ) ||
        (frequency_hz > APP_DEBUG_MAX_PULSE_HZ) ||
        (pulses == 0U))
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if ((s_state == SERVO_DEBUG_RUNNING) ||
        (s_state == SERVO_DEBUG_PREPARING) ||
        (s_state == SERVO_DEBUG_STOPPING) ||
        (s_enable_pending != 0U))
    {
        return SERVO_BUSY;
    }
    if (!servo_debug_pulse_selected())
    {
        return SERVO_NOT_CONFIGURED;
    }
    {
        servo_axis_t axis;
        servo_snapshot_t snapshot;

        for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!servo_debug_axis_selected((uint8_t)axis))
            {
                continue;
            }
            if (!aimtor_get_snapshot(axis, &snapshot) ||
                snapshot.fault || snapshot.stop_latched)
            {
                return SERVO_NOT_READY;
            }
        }
    }
    if ((s_state != SERVO_DEBUG_READY) ||
        (s_mode != SERVO_DEBUG_MODE_PULSE))
    {
        prepare_result = servo_debug_prepare(SERVO_DEBUG_MODE_PULSE);
        if (prepare_result != SERVO_OK)
        {
            return prepare_result;
        }
    }
    s_requested_mode = SERVO_DEBUG_MODE_PULSE;
    s_pulse_forward = forward ? 1U : 0U;
    s_target_frequency_hz = frequency_hz;
    s_target_pulses = pulses;
    s_target = (int32_t)pulses;
    s_pulse_wait_still = 0U;
    s_pulse_stop_confirmed = 0U;
    s_pulse_completion_timeout = 0U;
    s_run_deadline = servo_debug_deadline(30000U);
    s_auto_start = AUTO_START_PULSE;
    s_pulse_completion_deadline = 0U;
    s_pulse_position_initialized_mask = 0U;
    memset(s_pulse_position_stable, 0, sizeof(s_pulse_position_stable));
    memset(s_pulse_position_request_tick, 0,
           sizeof(s_pulse_position_request_tick));
    s_result = SERVO_BUSY;
    return SERVO_OK;
}
#else
servo_result_t servo_debug_run_pulse(bool f,uint32_t hz,uint32_t n)
{ (void)f;(void)hz;(void)n;return SERVO_NOT_CONFIGURED; }
#endif

/* 模式准备和使能结束后，由通信任务启动已受理的单轴运动命令。 */
static void servo_debug_service_auto_start(servo_snapshot_t *snapshot)
{
    servo_result_t servo_result;
#if APP_SERVO_PULSE_ENABLED
    motion_result_t motion_result;
    servo_axis_t axis;
#endif
    servo_snapshot_t axis_snapshot;

    servo_result = SERVO_BUSY;

    if (s_auto_start == AUTO_START_NONE)
    {
        return;
    }
    if (!aimtor_motion_allowed())
    {
        servo_debug_cancel_auto_start();
        return;
    }
    if ((s_state != SERVO_DEBUG_READY) ||
        (s_mode != s_requested_mode && s_requested_mode != SERVO_DEBUG_MODE_NONE))
    {
        return;
    }
    if (snapshot->fault || snapshot->stop_latched || snapshot->busy)
    {
        servo_debug_cancel_auto_start();
        s_result = SERVO_NOT_READY;
        s_state = SERVO_DEBUG_FAULT;
        return;
    }
    /*
     * 内部多段位置模式由 aimtor_run_segment() 在同一事务中完成
     * 失能、参数配置、重新使能和触发，不能在这里提前单独使能。
     */
#if APP_SERVO_PULSE_ENABLED
    if ((s_auto_start != AUTO_START_POSITION) &&
        (s_requested_mode == SERVO_DEBUG_MODE_PULSE))
    {
        for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!servo_debug_axis_selected((uint8_t)axis))
            {
                continue;
            }
            if (!aimtor_get_snapshot(axis, &axis_snapshot))
            {
                return;
            }
            if (axis_snapshot.busy)
            {
                return;
            }
            if (!axis_snapshot.enabled)
            {
                servo_result = aimtor_enable(axis);
                if (servo_result == SERVO_OK)
                {
                    s_enable_axis = (uint8_t)axis;
                    s_enable_pending = 1U;
                }
                else if (servo_result != SERVO_BUSY)
                {
                    servo_debug_cancel_auto_start();
                    s_result = servo_result;
                    s_state = SERVO_DEBUG_FAULT;
                }
                return;
            }
        }
        snapshot = &axis_snapshot;
    }
    else
#endif
    if (!snapshot->enabled &&
             (s_auto_start != AUTO_START_POSITION))
    {
        if (s_enable_pending == 0U)
        {
            servo_result = aimtor_enable(servo_debug_axis());
            if (servo_result == SERVO_OK)
            {
                s_enable_pending = 1U;
            }
            else if (servo_result != SERVO_BUSY)
            {
                servo_debug_cancel_auto_start();
                s_result = servo_result;
                s_state = SERVO_DEBUG_FAULT;
            }
        }
        return;
    }
    if (s_enable_pending != 0U)
    {
        if (!aimtor_get_snapshot((servo_axis_t)s_enable_axis,
                                 &axis_snapshot))
        {
            return;
        }
        if (axis_snapshot.busy)
        {
            return;
        }
        s_enable_pending = 0U;
        if (!axis_snapshot.enabled)
        {
            servo_debug_enter_fault(SERVO_NOT_READY);
            return;
        }
    }

    if (s_auto_start == AUTO_START_SPEED)
    {
        servo_result = aimtor_set_velocity_rpm(servo_debug_axis(), s_target_rpm);
        if (servo_result == SERVO_OK)
        {
            s_state = SERVO_DEBUG_RUNNING;
        }
        else if (servo_result != SERVO_BUSY)
        {
            servo_debug_enter_fault(servo_result);
            return;
        }
    }
    else if (s_auto_start == AUTO_START_POSITION)
    {
        /* 记录启动前的实际位置，后续用反馈位置确认确实发生了定位。 */
        s_position_start = snapshot->actual_position;
        servo_result = aimtor_run_segment(servo_debug_axis(), s_target,
                                          (uint16_t)s_target_rpm,
                                          s_target_ramp_ms);
        if (servo_result == SERVO_OK)
        {
            s_position_start_sequence = snapshot->position_sequence;
            s_position_seen_motion = 0U;
            s_position_direction_error = 0U;
            s_run_deadline = servo_debug_deadline(30000U);
            s_state = SERVO_DEBUG_RUNNING;
        }
    }
#if APP_SERVO_PULSE_ENABLED
    else
    {
        /*
         * 锁存每个控制轴的编码器起点。定脉冲完成不能只看TIM计数，
         * 必须用驱动器反馈确认实际位置确实走完本次脉冲量。
         */
        for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!servo_debug_axis_selected((uint8_t)axis))
            {
                continue;
            }
            if (!aimtor_get_snapshot(axis, &axis_snapshot))
            {
                return;
            }
            if (!axis_snapshot.position_valid ||
                (axis_snapshot.position_age_ms >
                 APP_SERVO_POSITION_MAX_AGE_MS) ||
                !axis_snapshot.online ||
                !axis_snapshot.enabled ||
                axis_snapshot.fault ||
                axis_snapshot.busy)
            {
                servo_debug_request_pulse_position((uint8_t)axis);
                return;
            }
            s_pulse_position_start[axis] = axis_snapshot.actual_position;
            s_pulse_position_now[axis] = axis_snapshot.actual_position;
            s_pulse_position_target[axis] =
                axis_snapshot.actual_position +
                (s_pulse_forward != 0U ?
                 (int32_t)s_target_pulses : -(int32_t)s_target_pulses);
            s_pulse_position_error[axis] = 0;
            s_pulse_position_last[axis] = axis_snapshot.actual_position;
            s_pulse_completion_position_sequence[axis] =
                axis_snapshot.position_sequence;
            s_pulse_completion_speed_sequence[axis] =
                axis_snapshot.speed_sequence;
            s_pulse_position_last_sequence[axis] =
                axis_snapshot.position_sequence;
            s_pulse_position_stable[axis] = 0U;
            s_pulse_position_initialized_mask |= (uint8_t)(1U << axis);
        }
        motion_result = motion_start_pulse_mask(
            servo_debug_control_mask(),
            s_pulse_forward,
            s_target_frequency_hz,
            s_target_pulses);
        if (motion_result == MOTION_OK)
        {
            s_state = SERVO_DEBUG_RUNNING;
        }
        else if ((motion_result == MOTION_NOT_READY) &&
                 (motion_get_state() != MOTION_FAULT) &&
                 (motion_get_state() != MOTION_OFFLINE))
        {
            /* 健康任务可能尚未完成本周期的READY更新，保留命令等待下一轮。 */
            return;
        }
        else
        {
            servo_result = motion_result == MOTION_INVALID_ARGUMENT ?
                           SERVO_INVALID_ARGUMENT : SERVO_NOT_READY;
            servo_debug_enter_fault(servo_result);
            return;
        }
    }
#endif
    if (s_state == SERVO_DEBUG_RUNNING)
    {
        servo_debug_finish_auto_start();
        s_result = SERVO_BUSY;
    }
    else if (servo_result != SERVO_BUSY)
    {
        servo_debug_enter_fault(servo_result);
    }
}

static void servo_debug_service_prepare(servo_snapshot_t *snapshot)
{
    servo_result_t result;
    servo_axis_t axis = (servo_axis_t)s_prepare_axis;

    if (servo_debug_tick_due(xTaskGetTickCount(), s_prepare_deadline))
    {
        (void)servo_debug_emergency_stop();
        return;
    }
    /* 旧双轴测试逐轴失能；独立会话仅准备运行时选中的目标轴。 */
    if (s_requested_mode == SERVO_DEBUG_MODE_PULSE)
    {
        servo_axis_t other;
        servo_snapshot_t other_snapshot;

        for (other = SERVO_AXIS_LEFT; other < SERVO_AXIS_COUNT; other++)
        {
            if (!servo_debug_axis_selected((uint8_t)other))
            {
                continue;
            }
            if (!aimtor_get_snapshot(other, &other_snapshot) ||
                !other_snapshot.online || other_snapshot.fault)
            {
                servo_debug_enter_fault(SERVO_NOT_READY);
                return;
            }
            if (other_snapshot.busy)
            {
                return;
            }
            if (other_snapshot.actual_speed_rpm != 0)
            {
                (void)aimtor_stop(other);
                return;
            }
            if (other_snapshot.enabled)
            {
                result = aimtor_disable(other);
                if ((result != SERVO_OK) && (result != SERVO_BUSY))
                {
                    servo_debug_enter_fault(result);
                }
                return;
            }
        }
    }
    if (servo_debug_busy(snapshot))
    {
        return;
    }
    if (snapshot->fault)
    {
        servo_debug_enter_fault(SERVO_NOT_READY);
        return;
    }
    if (!snapshot->online)
    {
        (void)aimtor_request_status(axis);
        return;
    }
    if (snapshot->actual_speed_rpm != 0)
    {
        (void)aimtor_stop(axis);
        return;
    }
    if ((s_prepare_stage == PREPARE_STOP) && snapshot->enabled)
    {
        result = aimtor_disable(axis);
        if (result == SERVO_OK)
        {
            s_prepare_stage = PREPARE_DISABLE;
        }
        return;
    }
    if ((s_prepare_stage == PREPARE_STOP) ||
        (s_prepare_stage == PREPARE_DISABLE))
    {
        if (snapshot->enabled)
        {
            return;
        }
        s_prepare_stage = PREPARE_CONFIGURE;
    }
    if (s_prepare_stage == PREPARE_CONFIGURE)
    {
        result = aimtor_set_control_mode(
            axis,
            s_requested_mode == SERVO_DEBUG_MODE_PULSE ?
                SERVO_CONTROL_PULSE_POSITION :
                s_requested_mode == SERVO_DEBUG_MODE_RS485_SPEED ?
                    SERVO_CONTROL_RS485_VELOCITY :
                    SERVO_CONTROL_RS485_SEGMENT_POSITION);
        if (result == SERVO_OK)
        {
            s_prepare_stage = PREPARE_WAIT_CONFIGURE;
        }
        else if (result != SERVO_BUSY)
        {
            servo_debug_enter_fault(result);
        }
        return;
    }
    /*
     * 模式配置是异步事务。失败时只结束本次模式切换，不再等待到
     * 10 秒后误触发急停；下一条模式命令可以直接重新发起配置。
     */
    if ((s_prepare_stage == PREPARE_WAIT_CONFIGURE) &&
        (snapshot->last_result != SERVO_BUSY) &&
        (snapshot->last_result != SERVO_OK))
    {
        s_result = snapshot->last_result;
        s_state = SERVO_DEBUG_FAULT;
        return;
    }
    if ((s_prepare_stage == PREPARE_WAIT_CONFIGURE) &&
        snapshot->parameter_verified &&
        (snapshot->control_mode != SERVO_CONTROL_NONE))
    {
        if ((s_requested_mode == SERVO_DEBUG_MODE_PULSE) &&
            APP_SERVO_BOOT_ZERO_ENABLED && !snapshot->zero_verified)
        {
            if (!snapshot->zero_attempted)
            {
                /* 双轴准备时必须对当前正在处理的轴执行零点动作。 */
                (void)aimtor_zero_once(axis);
            }
            else
            {
                servo_debug_enter_fault(SERVO_NOT_READY);
            }
            return;
        }
        s_prepare_done_mask |= (uint8_t)(1U << s_prepare_axis);
        s_prepare_axis++;
        while (s_prepare_axis < SERVO_AXIS_COUNT &&
               !servo_debug_axis_selected(s_prepare_axis))
        {
            s_prepare_axis++;
        }
        if (s_prepare_axis < SERVO_AXIS_COUNT)
        {
            s_prepare_stage = PREPARE_STOP;
            return;
        }
        s_mode = s_requested_mode;
        s_result = SERVO_OK;
        s_state = SERVO_DEBUG_READY;
    }
}

void servo_debug_service(void)
{
    servo_snapshot_t snapshot;
    servo_axis_t service_axis;
    if (aimtor_safety_stop_active()) return;

    if ((s_state == SERVO_DEBUG_PREPARING) &&
        (s_requested_mode == SERVO_DEBUG_MODE_PULSE))
    {
        service_axis = (servo_axis_t)s_prepare_axis;
    }
    else
    {
        service_axis = servo_debug_axis();
    }
    if (((s_requested_mode == SERVO_DEBUG_MODE_PULSE) ?
         !servo_debug_pulse_selected() : !servo_debug_selected()) ||
        !aimtor_get_snapshot(service_axis, &snapshot))
    {
        return;
    }
#if APP_SERVO_PULSE_ENABLED
    if ((s_state == SERVO_DEBUG_RUNNING) &&
        (s_mode == SERVO_DEBUG_MODE_PULSE))
    {
        uint8_t axis;
        servo_snapshot_t axis_snapshot;

        memset(&axis_snapshot, 0, sizeof(axis_snapshot));

        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!servo_debug_axis_selected(axis))
            {
                continue;
            }
            if (!aimtor_get_snapshot((servo_axis_t)axis, &axis_snapshot) ||
                axis_snapshot.fault || !axis_snapshot.online)
            {
                if (axis_snapshot.fault) (void)servo_debug_emergency_stop();
                else aimtor_request_controlled_safety_stop(1U);
                return;
            }
        }
    }
#endif
    /*
     * 遥控速度接管期间允许关键状态短时过期，由遥控服务统一按
     * 连续失败次数和500 ms年龄判断是否停车；真实故障仍立即处理。
     */
    if ((s_state == SERVO_DEBUG_RUNNING) &&
        (snapshot.fault ||
         (!snapshot.online &&
          !(aimtor_remote_control_active() &&
            s_mode == SERVO_DEBUG_MODE_RS485_SPEED))))
    {
        if (snapshot.fault) (void)servo_debug_emergency_stop();
        else aimtor_request_controlled_safety_stop(1U);
        return;
    }
    if (s_enable_pending != 0U)
    {
        if (snapshot.busy)
        {
            return;
        }
        s_enable_pending = 0U;
        if (snapshot.enabled)
        {
            s_result = SERVO_OK;
        }
        else
        {
            servo_debug_enter_fault(SERVO_NOT_READY);
        }
    }
    if (s_state == SERVO_DEBUG_PREPARING)
    {
        servo_debug_service_prepare(&snapshot);
        return;
    }
    if (s_state == SERVO_DEBUG_STOPPING)
    {
        if (servo_debug_tick_due(xTaskGetTickCount(), s_stop_deadline))
        {
            (void)servo_debug_emergency_stop();
            return;
        }
        if (snapshot.busy)
        {
            return;
        }
        if (snapshot.actual_speed_rpm != 0)
        {
            return;
        }
        if ((s_disable_requested != 0U) &&
            (snapshot.actual_speed_rpm == 0))
        {
            if (snapshot.enabled)
            {
                servo_result_t disable_result;

                disable_result = aimtor_disable(servo_debug_axis());
                if ((disable_result == SERVO_OK) ||
                    (disable_result == SERVO_BUSY))
                {
                    return;
                }
                servo_debug_enter_fault(disable_result);
                return;
            }
            else
            {
                s_disable_requested = 0U;
            }
        }
        if ((s_mode == SERVO_DEBUG_MODE_PULSE) &&
            (motion_get_pulse_active_mask(servo_debug_control_mask()) != 0U))
        {
            return;
        }
        s_result = SERVO_OK;
        s_state = SERVO_DEBUG_READY;
        s_stop_deadline = 0U;
        servo_debug_service_auto_start(&snapshot);
        return;
    }
    servo_debug_service_auto_start(&snapshot);
#if APP_SERVO_PULSE_ENABLED
    if ((s_state == SERVO_DEBUG_RUNNING) &&
        (s_mode == SERVO_DEBUG_MODE_PULSE))
    {
        if (s_pulse_wait_still != 0U)
        {
            uint8_t axis;
            uint8_t pending_mask;
            uint8_t confirmed_mask = 0U;
            uint8_t all_confirmed = 1U;
            servo_snapshot_t axis_snapshot;
            pending_mask = motion_get_pulse_completion_pending_mask(
                servo_debug_control_mask());
            for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
            {
                if ((pending_mask & (uint8_t)(1U << axis)) == 0U)
                {
                    continue;
                }
                if (!aimtor_get_snapshot((servo_axis_t)axis, &axis_snapshot))
                {
                    all_confirmed = 0U;
                    continue;
                }
                /* 完成等待期间主动请求位置，避免只依赖1秒后台轮询。 */
                servo_debug_request_pulse_position(axis);
                s_pulse_position_now[axis] = axis_snapshot.actual_position;
                s_pulse_position_error[axis] =
                    axis_snapshot.actual_position -
                    s_pulse_position_target[axis];
                if (s_pulse_position_error[axis] < 0)
                {
                    s_pulse_position_error[axis] =
                        -s_pulse_position_error[axis];
                }
                if ((s_pulse_position_initialized_mask &
                     (uint8_t)(1U << axis)) == 0U ||
                    !axis_snapshot.position_valid ||
                    axis_snapshot.position_age_ms >
                    APP_SERVO_POSITION_MAX_AGE_MS ||
                    axis_snapshot.position_sequence <=
                    s_pulse_completion_position_sequence[axis] ||
                    axis_snapshot.speed_sequence <=
                    s_pulse_completion_speed_sequence[axis] ||
                    s_pulse_position_error[axis] >
                    APP_SERVO_POSITION_TOLERANCE_PULSES)
                {
                    s_pulse_position_stable[axis] = 0U;
                    all_confirmed = 0U;
                    continue;
                }
                if (axis_snapshot.position_sequence >
                    s_pulse_position_last_sequence[axis])
                {
                    if (axis_snapshot.actual_position ==
                        s_pulse_position_last[axis])
                    {
                        if (s_pulse_position_stable[axis] < 255U)
                        {
                            s_pulse_position_stable[axis]++;
                        }
                    }
                    else
                    {
                        s_pulse_position_stable[axis] = 0U;
                    }
                    s_pulse_position_last[axis] =
                        axis_snapshot.actual_position;
                    s_pulse_position_last_sequence[axis] =
                        axis_snapshot.position_sequence;
                }
                if (axis_snapshot.busy ||
                    !axis_snapshot.online ||
                    axis_snapshot.fault ||
                    !axis_snapshot.enabled ||
                    !axis_snapshot.speed_valid ||
                    (axis_snapshot.speed_age_ms >
                     APP_SERVO_STATUS_MAX_AGE_MS) ||
                    !servo_pulse_target_reached((servo_axis_t)axis) ||
                    servo_pulse_output_active((servo_axis_t)axis) ||
                    (axis_snapshot.actual_speed_rpm != 0) ||
                    (s_pulse_position_stable[axis] <
                     APP_PULSE_COMPLETION_STABLE_SAMPLES))
                {
                    all_confirmed = 0U;
                    continue;
                }
                confirmed_mask |= (uint8_t)(1U << axis);
            }

            if (all_confirmed &&
                (confirmed_mask == pending_mask) &&
                (pending_mask == servo_debug_control_mask()) &&
                (motion_complete_pulse(pending_mask) == MOTION_OK))
            {
                s_pulse_wait_still = 0U;
                s_pulse_stop_confirmed = 1U;
                s_pulse_completion_deadline = 0U;
                servo_debug_cancel_auto_start();
                s_result = SERVO_OK;
                s_state = SERVO_DEBUG_READY;
                return;
            }
            if (servo_debug_tick_due(xTaskGetTickCount(),
                                     s_pulse_completion_deadline))
            {
                /*
                 * 正常完成等待超时不能伪装成完成，也不能把伺服失能。
                 * 保持PWM关闭和当前使能状态，等待人工重新处理。
                 */
                s_pulse_completion_timeout = 1U;
                s_pulse_wait_still = 0U;
                servo_debug_cancel_auto_start();
                s_result = SERVO_NOT_READY;
                s_state = SERVO_DEBUG_FAULT;
            }
        }
        return;
    }
#endif
    if ((s_state == SERVO_DEBUG_RUNNING) &&
        (s_mode == SERVO_DEBUG_MODE_RS485_POSITION))
    {
        if ((snapshot.position_sequence > s_position_start_sequence) &&
            (snapshot.actual_position != s_position_start))
        {
            s_position_seen_motion = 1U;
            if (((s_target > 0) && (snapshot.actual_position < s_position_start)) ||
                ((s_target < 0) && (snapshot.actual_position > s_position_start)))
            {
                s_position_direction_error = 1U;
            }
        }
        if (s_position_direction_error != 0U)
        {
            (void)servo_debug_emergency_stop();
            return;
        }
        if (snapshot.actual_speed_rpm != 0)
        {
            s_position_seen_motion = 1U;
        }
        if ((snapshot.position_sequence > s_position_start_sequence) &&
            s_position_seen_motion &&
            snapshot.position_valid &&
            (snapshot.position_age_ms <= APP_SERVO_POSITION_MAX_AGE_MS) &&
            !snapshot.busy &&
            (snapshot.actual_speed_rpm == 0))
        {
            int64_t position_error;
            int64_t expected_position;
            servo_result_t complete_result;

            expected_position = (int64_t)s_position_start + (int64_t)s_target;
            position_error = (int64_t)snapshot.actual_position -
                             expected_position;
            if (position_error < 0)
            {
                position_error = -position_error;
            }
            if (position_error <= APP_SERVO_POSITION_TOLERANCE_PULSES)
            {
                complete_result = aimtor_complete_segment(servo_debug_axis());
                if (complete_result == SERVO_OK)
                {
                    s_result = SERVO_OK;
                    s_state = SERVO_DEBUG_READY;
                }
                else if (complete_result != SERVO_BUSY)
                {
                    servo_debug_enter_fault(complete_result);
                }
            }
        }
        /* 已停下但位置未达标时也必须检查超时，不能无限保持运行状态。 */
        if ((s_state == SERVO_DEBUG_RUNNING) &&
            servo_debug_tick_due(xTaskGetTickCount(), s_run_deadline))
        {
            (void)servo_debug_emergency_stop();
        }
    }
}

#if APP_SERVO_PULSE_ENABLED
void servo_debug_motion_service(void)
{
    if (aimtor_safety_stop_active()) return;
    if ((s_state == SERVO_DEBUG_RUNNING) &&
        (s_mode == SERVO_DEBUG_MODE_PULSE) &&
        (s_target_pulses != 0U) &&
        (motion_get_pulse_completion_pending_mask(
             servo_debug_control_mask()) == servo_debug_control_mask()))
    {
        if (s_pulse_wait_still == 0U)
        {
            uint8_t axis;
            servo_snapshot_t snapshot;

            /* 停止确认从脉冲输出关闭时刻开始单独计时。 */
            s_pulse_completion_deadline =
                servo_debug_deadline(APP_PULSE_COMPLETION_STOP_TIMEOUT_MS);
            for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
            {
                if (!servo_debug_axis_selected(axis) ||
                    !aimtor_get_snapshot((servo_axis_t)axis, &snapshot))
                {
                    continue;
                }
                /* 只接受脉冲输出关闭之后的新位置和速度反馈。 */
                s_pulse_completion_position_sequence[axis] =
                    snapshot.position_sequence;
                s_pulse_completion_speed_sequence[axis] =
                    snapshot.speed_sequence;
                s_pulse_position_last_sequence[axis] =
                    snapshot.position_sequence;
                s_pulse_position_last[axis] =
                    snapshot.actual_position;
                s_pulse_position_stable[axis] = 0U;
            }
        }
        s_pulse_wait_still = 1U;
    }
}
#else
void servo_debug_motion_service(void) {}
#endif

bool servo_debug_get_status(servo_debug_status_t *status)
{
    if ((status == NULL) ||
        !aimtor_get_snapshot(servo_debug_axis(), &status->servo))
    {
        return false;
    }
    taskENTER_CRITICAL();
    status->mode = s_mode;
    status->state = s_state;
    status->target = s_target;
    status->target_rpm = s_target_rpm;
#if APP_SERVO_PULSE_ENABLED
    status->target_frequency_hz = s_target_frequency_hz;
    status->target_pulses = s_mode == SERVO_DEBUG_MODE_PULSE ?
        servo_pulse_get_target(servo_debug_axis()) : s_target_pulses;
    status->pulse_count = servo_pulse_get_pulse_count(servo_debug_axis());
    status->pulse_output_active =
        servo_pulse_output_active(servo_debug_axis());
    status->pulse_target_reached =
        servo_pulse_target_reached(servo_debug_axis()) ? 1U : 0U;
    status->pulse_stop_confirmed = s_pulse_stop_confirmed;
    status->pulse_completion_timeout = s_pulse_completion_timeout;
    status->pulse_forward = s_pulse_forward;
    status->pulse_position_valid =
        (uint8_t)((s_pulse_position_initialized_mask &
                   (uint8_t)(1U << servo_debug_axis())) != 0U);
    status->pulse_position_stable =
        s_pulse_position_stable[servo_debug_axis()];
    status->pulse_position_start =
        s_pulse_position_start[servo_debug_axis()];
    status->pulse_position_now =
        s_pulse_position_now[servo_debug_axis()];
    status->pulse_position_delta =
        s_pulse_position_now[servo_debug_axis()] -
        s_pulse_position_start[servo_debug_axis()];
    status->pulse_position_target =
        s_pulse_position_target[servo_debug_axis()];
    status->pulse_position_error =
        s_pulse_position_error[servo_debug_axis()];
#endif
    status->result = s_result;
    taskEXIT_CRITICAL();
    return true;
}


#else
#include "servo_debug_control.h"
#include "motion_control.h"
#include "servo_pulse.h"
#include <string.h>
/* Public safety actions remain callable from board keys and recovery paths. */
void servo_debug_init(void) {}
void servo_debug_select_axis(servo_axis_t axis) { (void)axis; }
void servo_debug_release_axis(void) {}
void servo_debug_adopt_speed(void) {}
servo_axis_t servo_debug_get_axis(void) { return SERVO_AXIS_LEFT; }
void servo_debug_service(void) {}
void servo_debug_motion_service(void) {}
servo_result_t servo_debug_prepare(servo_debug_mode_t mode)
{ (void)mode;return SERVO_NOT_CONFIGURED; }
servo_result_t servo_debug_enable(void) { return SERVO_NOT_CONFIGURED; }
servo_result_t servo_debug_disable(void) { return SERVO_NOT_CONFIGURED; }
servo_result_t servo_debug_stop(void)
{ servo_pulse_stop_all();(void)motion_stop(MOTION_STOP_IMMEDIATE);return SERVO_OK; }
servo_result_t servo_debug_emergency_stop(void)
{ aimtor_emergency_stop();return SERVO_OK; }
void servo_debug_abort_from_fault(void) {}
servo_result_t servo_debug_set_speed(int16_t rpm)
{ (void)rpm;return SERVO_NOT_CONFIGURED; }
servo_result_t servo_debug_run_position(int32_t n,uint16_t rpm,uint16_t ramp)
{ (void)n;(void)rpm;(void)ramp;return SERVO_NOT_CONFIGURED; }
servo_result_t servo_debug_run_pulse(bool f,uint32_t hz,uint32_t n)
{ (void)f;(void)hz;(void)n;return SERVO_NOT_CONFIGURED; }
bool servo_debug_get_status(servo_debug_status_t *status)
{
    if(!status)return false;
    memset(status,0,sizeof(*status));
    status->mode=SERVO_DEBUG_MODE_NONE;status->state=SERVO_DEBUG_IDLE;
    status->result=SERVO_NOT_CONFIGURED;return true;
}

#endif
const char *servo_debug_mode_name(servo_debug_mode_t mode)
{
    if (mode == SERVO_DEBUG_MODE_PULSE)
    {
        return "pulse";
    }
    if (mode == SERVO_DEBUG_MODE_RS485_SPEED)
    {
        return "speed";
    }
    if (mode == SERVO_DEBUG_MODE_RS485_POSITION)
    {
        return "position";
    }
    return "none";
}

const char *servo_debug_state_name(servo_debug_state_t state)
{
    if (state == SERVO_DEBUG_PREPARING)
    {
        return "preparing";
    }
    if (state == SERVO_DEBUG_READY)
    {
        return "ready";
    }
    if (state == SERVO_DEBUG_RUNNING)
    {
        return "running";
    }
    if (state == SERVO_DEBUG_STOPPING)
    {
        return "stopping";
    }
    if (state == SERVO_DEBUG_FAULT)
    {
        return "fault";
    }
    return "idle";
}
