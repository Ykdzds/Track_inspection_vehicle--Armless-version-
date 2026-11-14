#include "app_config.h"
#if APP_SERVO_PULSE_ENABLED
#include "motion_control.h"

#include <math.h>
#include <float.h>

#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "app_sync_params.h"
#include "aimtor_servo.h"
#include "servo_pulse.h"
#include "app_health.h"

typedef enum
{
    MOTION_MODE_IDLE = 0,
    MOTION_MODE_VELOCITY,
    MOTION_MODE_DISTANCE,
    MOTION_MODE_PULSE_TEST
} motion_mode_t;

static volatile motion_state_t s_state;
static motion_mode_t s_mode;
static float s_target_velocity[2];
static float s_command_velocity[2];
static float s_acceleration;
static float s_deceleration;
static float s_distance_target;
static float s_distance_done;
static uint8_t s_direction_wait_ticks[2];
static uint8_t s_applied_forward[2];
static uint8_t s_direction_initialized[2];
static volatile uint8_t s_pulse_test_active_mask;
static volatile uint8_t s_pulse_completion_pending_mask;
static volatile uint8_t s_system_ready;
static motion_state_t s_stop_target_state;
static volatile motion_stop_class_t s_stop_class;
static volatile motion_fault_reason_t s_fault_reason;
static volatile uint8_t s_controlled_safety_requested;

/* 在已进入临界区的上下文中停止运动，避免高层接口重复嵌套临界区。 */
static motion_result_t motion_stop_locked(motion_stop_mode_t mode)
{
    if ((mode != MOTION_STOP_CONTROLLED) &&
        (mode != MOTION_STOP_IMMEDIATE))
    {
        return MOTION_INVALID_ARGUMENT;
    }
    s_target_velocity[0] = 0.0f;
    s_target_velocity[1] = 0.0f;
    s_pulse_test_active_mask = 0U;
    s_pulse_completion_pending_mask = 0U;
    s_mode = MOTION_MODE_IDLE;
    if (mode == MOTION_STOP_IMMEDIATE)
    {
        s_command_velocity[0] = 0.0f;
        s_command_velocity[1] = 0.0f;
        s_direction_wait_ticks[0] = 0U;
        s_direction_wait_ticks[1] = 0U;
        servo_pulse_stop_all();
        if ((s_state != MOTION_FAULT) && (s_state != MOTION_OFFLINE))
        {
            s_state = (s_system_ready != 0U) ? MOTION_READY : MOTION_SAFE;
        }
    }
    else if (s_state == MOTION_RUNNING)
    {
        s_stop_target_state = MOTION_READY;
        s_state = MOTION_STOPPING;
    }
    return MOTION_OK;
}

/** 返回浮点数绝对值，用于速度和距离计算。 */
static float motion_abs(float value)
{
    return value < 0.0f ? -value : value;
}

/* 同时拒绝NaN和无穷值，避免异常浮点数进入频率转换。 */
static uint8_t motion_finite(float value)
{
    return (uint8_t)((value >= -FLT_MAX) && (value <= FLT_MAX));
}

/** 按限定步长将当前值逼近目标值，避免速度指令突变。 */
static float motion_step_toward(float current, float target, float step)
{
    if (current < target)
    {
        current += step;
        return current > target ? target : current;
    }
    if (current > target)
    {
        current -= step;
        return current < target ? target : current;
    }
    return current;
}

// 所有运动命令必须经过硬件、通信和伺服状态门禁。
static motion_result_t motion_check_ready(void)
{
    if (app_sync_params_maintenance()) return MOTION_NOT_READY;
    if (!aimtor_motion_allowed())
    {
        return MOTION_NOT_READY;
    }
    if (!app_config_motion_hardware_ready())
    {
        return MOTION_NOT_CONFIGURED;
    }
    if (s_system_ready == 0U)
    {
        return MOTION_NOT_READY;
    }
    if ((s_state == MOTION_FAULT) || (s_state == MOTION_OFFLINE))
    {
        return MOTION_FAULTED;
    }
    if ((s_state != MOTION_READY) && (s_state != MOTION_RUNNING))
    {
        return MOTION_NOT_READY;
    }
    return MOTION_OK;
}

// 初始化脉冲输出并进入SAFE，初始化阶段不启动任何PWM通道。
void motion_init(void)
{
    servo_pulse_init(); /* 伺服电机脉冲通道绑定，以及停止PWM输出等功能 */
    s_state = MOTION_BOOT; /* 初始化状态 */
    s_mode = MOTION_MODE_IDLE; /* 伺服空闲状态 */
    s_acceleration = APP_DEFAULT_ACCELERATION_MPS2;
    s_deceleration = APP_DEFAULT_ACCELERATION_MPS2;
    s_system_ready = 0U;
    s_stop_target_state = MOTION_READY;
    s_target_velocity[0] = s_target_velocity[1] = 0.0f;
    s_command_velocity[0] = s_command_velocity[1] = 0.0f;
    s_direction_wait_ticks[0] = s_direction_wait_ticks[1] = 0U;
    s_direction_initialized[0] = s_direction_initialized[1] = 0U;
    s_pulse_test_active_mask = 0U;
    s_pulse_completion_pending_mask = 0U;
    s_stop_class = MOTION_STOP_CLASS_NONE;
    s_fault_reason = MOTION_FAULT_REASON_NONE;
    s_controlled_safety_requested = 0U;
    s_state = MOTION_SAFE;
}

/** 设置左右轴目标速度，并切换到速度控制模式。 */
motion_result_t motion_set_velocity(float left_mps, float right_mps)
{
    motion_result_t result;
    servo_snapshot_t snapshot;

    /* 未选中的轴不得接收运动命令；已选中的轴可独立设置目标速度。 */
    if (!app_config_servo_axis_controllable(SERVO_AXIS_LEFT) &&
        (left_mps != 0.0f))
    {
        return MOTION_NOT_CONFIGURED;
    }
    if (!app_config_servo_axis_controllable(SERVO_AXIS_RIGHT) &&
        (right_mps != 0.0f))
    {
        return MOTION_NOT_CONFIGURED;
    }
    if (!motion_finite(left_mps) || !motion_finite(right_mps) ||
        (motion_abs(left_mps) > APP_DEBUG_MAX_VELOCITY_MPS) ||
        (motion_abs(right_mps) > APP_DEBUG_MAX_VELOCITY_MPS) ||
        (left_mps != 0.0f &&
         motion_abs(left_mps) < (float)APP_PULSE_MIN_HZ / APP_PULSES_PER_METER) ||
         (right_mps != 0.0f &&
         motion_abs(right_mps) < (float)APP_PULSE_MIN_HZ / APP_PULSES_PER_METER))
    {
        return MOTION_INVALID_ARGUMENT;
    }
    /* 独立CLI会话不得借旧连续脉冲接口向非会话轴输出运动。 */
    if (aimtor_cli_control_active() &&
        ((!aimtor_cli_axis_owned(SERVO_AXIS_LEFT) && left_mps != 0.0f) ||
         (!aimtor_cli_axis_owned(SERVO_AXIS_RIGHT) && right_mps != 0.0f)))
    {
        return MOTION_NOT_READY;
    }
    /*
     * 旧底盘速度接口只能作为左轴外部脉冲兼容入口。
     * RS485速度和内部位置模式下禁止从该入口启动TIM9，避免两个控制源
     * 同时向同一个驱动器下发运动命令。
     */
    if (app_config_servo_axis_controllable(SERVO_AXIS_LEFT))
    {
        if (aimtor_maintenance_enabled() ||
            !aimtor_get_snapshot(SERVO_AXIS_LEFT, &snapshot) ||
            !snapshot.online || !snapshot.enabled ||
            !snapshot.parameter_verified ||
            snapshot.fault || snapshot.stop_latched ||
            (snapshot.control_mode != SERVO_CONTROL_PULSE_POSITION))
        {
            return MOTION_NOT_READY;
        }
    }
    if (app_config_servo_axis_controllable(SERVO_AXIS_RIGHT))
    {
        if (aimtor_maintenance_enabled() ||
            !aimtor_get_snapshot(SERVO_AXIS_RIGHT, &snapshot) ||
            !snapshot.online || !snapshot.enabled ||
            !snapshot.parameter_verified ||
            snapshot.fault || snapshot.stop_latched ||
            (snapshot.control_mode != SERVO_CONTROL_PULSE_POSITION))
        {
            return MOTION_NOT_READY;
        }
    }
    taskENTER_CRITICAL();
    result = motion_check_ready();
    if (result != MOTION_OK)
    {
        taskEXIT_CRITICAL();
        return result;
    }
    s_acceleration = APP_DEFAULT_ACCELERATION_MPS2;
    s_deceleration = APP_DEFAULT_ACCELERATION_MPS2;
    /* 显式接受新的连续速度命令后，解除上次定脉冲的到数锁存。 */
    servo_pulse_set_target(SERVO_AXIS_LEFT, 0U);
    servo_pulse_set_target(SERVO_AXIS_RIGHT, 0U);
    s_target_velocity[0] = left_mps;
    s_target_velocity[1] = right_mps;
    s_pulse_test_active_mask = 0U;
    s_mode = MOTION_MODE_VELOCITY;
    s_stop_target_state = MOTION_READY;
    s_state = MOTION_RUNNING;
    taskEXIT_CRITICAL();
    return MOTION_OK;
}

motion_result_t motion_start_pulse_mask(uint8_t axis_mask,
                                        uint8_t forward,
                                        uint32_t frequency_hz,
                                        uint32_t pulses)
{
    motion_result_t result;
    servo_snapshot_t snapshot;
    uint32_t maximum_frequency;
    uint8_t axis;

    maximum_frequency = APP_DEBUG_MAX_PULSE_HZ;
    if ((axis_mask == 0U) || (forward > 1U) || (pulses == 0U) ||
        ((axis_mask & (uint8_t)~((1U << SERVO_AXIS_COUNT) - 1U)) != 0U) ||
        (frequency_hz < APP_PULSE_MIN_HZ) ||
        (frequency_hz > APP_PULSE_MAX_HZ) ||
        (frequency_hz > maximum_frequency))
    {
        return MOTION_INVALID_ARGUMENT;
    }
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if ((axis_mask & (uint8_t)(1U << axis)) == 0U)
        {
            continue;
        }
        if (aimtor_cli_control_active() &&
            !aimtor_cli_axis_owned((servo_axis_t)axis))
        {
            return MOTION_NOT_READY;
        }
        if (!app_config_servo_axis_controllable(axis) ||
            !aimtor_get_snapshot((servo_axis_t)axis, &snapshot))
        {
            return MOTION_NOT_CONFIGURED;
        }
        if (!snapshot.online || !snapshot.enabled ||
            !snapshot.parameter_verified || snapshot.feedback_degraded ||
            (snapshot.control_mode != SERVO_CONTROL_PULSE_POSITION) ||
            snapshot.fault || snapshot.busy || snapshot.stop_latched)
        {
            return MOTION_NOT_READY;
        }
    }
    taskENTER_CRITICAL();
    result = motion_check_ready();
    if ((result != MOTION_OK) || (s_state != MOTION_READY) ||
        (s_pulse_test_active_mask != 0U))
    {
        taskEXIT_CRITICAL();
        return result != MOTION_OK ? result : MOTION_NOT_READY;
    }
    s_pulse_test_active_mask = axis_mask;
    s_pulse_completion_pending_mask = 0U;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if ((axis_mask & (uint8_t)(1U << axis)) != 0U)
        {
            /* 定脉冲测试使用对应定时器更新事件计数。 */
            servo_pulse_set_target((servo_axis_t)axis, pulses);
        }
    }
    /*
     * 定脉冲模式使用专用加减速率：
     * 20 rpm/s负责加速，100 rpm/s负责正常减速。
     */
    s_acceleration = APP_PULSE_ACCELERATION_MPS2;
    s_deceleration = APP_PULSE_DECELERATION_MPS2;
    s_target_velocity[0] = (axis_mask & (1U << SERVO_AXIS_LEFT)) != 0U ?
        ((forward != 0U) ? (float)frequency_hz / APP_PULSES_PER_METER :
                           -(float)frequency_hz / APP_PULSES_PER_METER) : 0.0f;
    s_target_velocity[1] = (axis_mask & (1U << SERVO_AXIS_RIGHT)) != 0U ?
        ((forward != 0U) ? (float)frequency_hz / APP_PULSES_PER_METER :
                           -(float)frequency_hz / APP_PULSES_PER_METER) : 0.0f;
    s_command_velocity[0] = 0.0f;
    s_command_velocity[1] = 0.0f;
    s_mode = MOTION_MODE_PULSE_TEST;
    s_stop_target_state = MOTION_READY;
    s_state = MOTION_RUNNING;
    taskEXIT_CRITICAL();
    return MOTION_OK;
}

motion_result_t motion_start_pulse(uint8_t axis,
                                   uint8_t forward,
                                   uint32_t frequency_hz,
                                   uint32_t pulses)
{
    if (axis >= SERVO_AXIS_COUNT)
    {
        return MOTION_INVALID_ARGUMENT;
    }
    return motion_start_pulse_mask((uint8_t)(1U << axis), forward,
                                   frequency_hz, pulses);
}

motion_result_t motion_start_left_pulse(uint8_t forward,
                                        uint32_t frequency_hz,
                                        uint32_t pulses)
{
    return motion_start_pulse(SERVO_AXIS_LEFT, forward, frequency_hz, pulses);
}

/** 设置距离运动目标，并根据剩余距离动态计算减速点。 */
motion_result_t motion_move_distance(float distance_m,
                                     float max_velocity_mps,
                                     float acceleration_mps2)
{
    (void)distance_m;
    (void)max_velocity_mps;
    (void)acceleration_mps2;
    /* 当前阶段只允许通过左轴专用调试接口发起定脉冲运动。 */
    return MOTION_NOT_CONFIGURED;
}

/** 停止当前运动，并根据停止模式选择受控减速或立即停脉冲。 */
motion_result_t motion_stop(motion_stop_mode_t mode)
{
    motion_result_t result;

    taskENTER_CRITICAL();
    result = motion_stop_locked(mode);
    if (result == MOTION_OK && s_controlled_safety_requested == 0U)
    {
        s_stop_class = mode == MOTION_STOP_CONTROLLED ?
            MOTION_STOP_CLASS_NORMAL : MOTION_STOP_CLASS_IMMEDIATE_SAFETY;
    }
    taskEXIT_CRITICAL();
    return result;
}

/** 返回当前运动状态，供任务、CLI和诊断界面查询。 */
motion_state_t motion_get_state(void)
{
    return s_state;
}

/** 更新外部系统就绪条件，并同步维护SAFE和READY状态。 */
void motion_set_system_ready(uint8_t ready)
{
    taskENTER_CRITICAL();
    s_system_ready = (uint8_t)((s_controlled_safety_requested == 0U) &&
                               (ready != 0U) &&
                               aimtor_motion_allowed() &&
                               app_config_motion_hardware_ready());
    if (s_system_ready != 0U)
    {
        if (s_state == MOTION_SAFE)
        {
            s_state = MOTION_READY;
        }
    }
    else if (s_controlled_safety_requested == 0U)
    {
        (void)motion_stop_locked(MOTION_STOP_IMMEDIATE);
    }
    taskEXIT_CRITICAL();
}

/** 通信或控制故障立即切断脉冲并锁存，通信恢复不能自动续跑。 */
void motion_report_fault(uint8_t offline)
{
    taskENTER_CRITICAL();
    s_system_ready = 0U;
    s_controlled_safety_requested = 0U;
    s_stop_class = MOTION_STOP_CLASS_IMMEDIATE_SAFETY;
    if (s_fault_reason == MOTION_FAULT_REASON_NONE)
    {
        s_fault_reason = offline != 0U ? MOTION_FAULT_REASON_SERVO_OFFLINE :
            MOTION_FAULT_REASON_SERVO_FAULT;
    }
    (void)motion_stop_locked(MOTION_STOP_IMMEDIATE);
    (void)servo_pulse_set_enable(SERVO_AXIS_LEFT, false);
    (void)servo_pulse_set_enable(SERVO_AXIS_RIGHT, false);
    s_state = offline != 0U ? MOTION_OFFLINE : MOTION_FAULT;
    taskEXIT_CRITICAL();
}

void motion_request_controlled_safety_stop(motion_fault_reason_t reason,
                                           uint8_t offline)
{
    uint8_t axis;
    taskENTER_CRITICAL();
    if (s_controlled_safety_requested || s_state == MOTION_FAULT ||
        s_state == MOTION_OFFLINE)
    {
        taskEXIT_CRITICAL();
        return;
    }
    s_fault_reason = reason;
    s_stop_class = MOTION_STOP_CLASS_CONTROLLED_SAFETY;
    s_controlled_safety_requested = 1U;
    s_system_ready = 0U;
    for (axis = 0U; axis < 2U; axis++)
    {
        if (s_mode == MOTION_MODE_PULSE_TEST && servo_pulse_get_frequency((servo_axis_t)axis))
        {
            float velocity = (float)servo_pulse_get_frequency((servo_axis_t)axis) / APP_PULSES_PER_METER;
            s_command_velocity[axis] = s_applied_forward[axis] ? velocity : -velocity;
            servo_pulse_set_target((servo_axis_t)axis, 0U);
        }
    }
    (void)offline;
    (void)motion_stop_locked(MOTION_STOP_CONTROLLED);
    s_stop_target_state = MOTION_STOPPING;
    s_state = MOTION_STOPPING;
    taskEXIT_CRITICAL();
}

void motion_request_immediate_safety_stop(motion_fault_reason_t reason,
                                          uint8_t offline)
{
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_DRIVER, 0xFFU,
        (uint16_t)reason, offline);
    taskENTER_CRITICAL();
    s_fault_reason = reason;
    s_stop_class = MOTION_STOP_CLASS_IMMEDIATE_SAFETY;
    taskEXIT_CRITICAL();
    aimtor_record_immediate_safety_stop((uint8_t)reason);
#if APP_UNPROTECTED_MOTION_TEST_ENABLED
    aimtor_emergency_stop();
#else
    /* 本轮实车仅内部速度模式；失败后停止所有访问，让H0C_30接管，不能再写骤停/失能。 */
    app_health_request_driver_fallback(0x100U + (uint32_t)reason);
#endif
    motion_report_fault(offline);
}

void motion_complete_controlled_safety_stop(uint8_t offline)
{
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_DRIVER, 0xFFU,
        (uint16_t)motion_get_fault_reason(), offline);
    taskENTER_CRITICAL();
    aimtor_emergency_stop();
    motion_report_fault(offline);
    s_stop_class = MOTION_STOP_CLASS_CONTROLLED_SAFETY;
    taskEXIT_CRITICAL();
}

motion_stop_class_t motion_get_stop_class(void)
{
    return s_stop_class;
}

motion_fault_reason_t motion_get_fault_reason(void)
{
    return s_fault_reason;
}

void motion_clear_fault(void)
{
    if (aimtor_recovery_blocks_motion()) return;
    taskENTER_CRITICAL();
    s_system_ready = 0U;
    (void)motion_stop_locked(MOTION_STOP_IMMEDIATE);
    s_state = MOTION_SAFE;
    s_stop_class = MOTION_STOP_CLASS_NONE;
    s_fault_reason = MOTION_FAULT_REASON_NONE;
    s_controlled_safety_requested = 0U;
    taskEXIT_CRITICAL();
}

// 将工程单位速度转换为脉冲频率，并处理停脉冲换向和方向稳定时间。
static void motion_apply_axis(uint8_t axis, float velocity)
{
    uint32_t frequency;
    uint8_t forward;

    if (s_direction_wait_ticks[axis] > 0U)
    {
        s_direction_wait_ticks[axis]--;
        if (s_direction_wait_ticks[axis] > 0U)
        {
            return;
        }
    }

    forward = velocity >= 0.0f ? 1U : 0U;
    frequency = (uint32_t)(motion_abs(velocity) * APP_PULSES_PER_METER + 0.5f);
    if ((frequency > 0U) && (frequency < APP_PULSE_MIN_HZ))
    {
        frequency = 0U;
    }
    if (frequency > APP_PULSE_MAX_HZ)
    {
        frequency = APP_PULSE_MAX_HZ;
    }
    if ((frequency > 0U) &&
        ((s_direction_initialized[axis] == 0U) ||
         (forward != s_applied_forward[axis])))
    {
        (void)servo_pulse_set_frequency((servo_axis_t)axis, 0U);
        if (servo_pulse_set_direction((servo_axis_t)axis,
                                      forward != 0U) != SERVO_PULSE_OK)
        {
            motion_request_immediate_safety_stop(MOTION_FAULT_REASON_OUTPUT_FAILURE, 0U);
            return;
        }
        s_applied_forward[axis] = forward;
        s_direction_initialized[axis] = 1U;
        s_direction_wait_ticks[axis] =
            (APP_DIRECTION_SETTLE_MS + APP_MOTION_PERIOD_MS - 1U) /
            APP_MOTION_PERIOD_MS;
        return;
    }
    if (frequency == 0U)
    {
        (void)servo_pulse_set_frequency((servo_axis_t)axis, 0U);
        return;
    }
    if (servo_pulse_set_frequency((servo_axis_t)axis,
                                  frequency) != SERVO_PULSE_OK)
    {
        motion_request_immediate_safety_stop(MOTION_FAULT_REASON_OUTPUT_FAILURE, 0U);
    }
}

/*
 * 以固定5 ms周期执行运动控制更新，包括：
 * 1. 维护模式安全处理；
 * 2. 状态和硬件门禁检查；
 * 3. 速度规划和加减速；
 * 4. 距离运动的剩余距离计算；
 * 5. 左右轴脉冲输出；
 * 6. 命令位置累计；
 * 7. 运动完成后的停止状态切换。
 */
static void motion_update_locked(void)
{
    /* 当前周期内允许的最大速度变化量，单位：m/s。 */
    float step;
    /* 当前周期使用的加速度或减速度，单位：m/s²。 */
    float rate;
    /* 距离运动时，为保证能够在目标位置前停止而计算出的允许速度。 */
    float stop_velocity;
    /* 距离运动剩余距离，单位：m。 */
    float remaining_distance;
    /* 当前轴本周期准备执行的目标速度，单位：m/s。 */
    float target;
    /* 当前处理的轴编号：0表示左轴，1表示右轴。 */
    uint8_t axis;
    /*
     * 如果系统进入伺服维护模式，则停止正常底盘运动控制。
     *
     * 维护模式下，伺服可能由RS485内部速度模式或内部位置模式控制，
     * 因此底盘脉冲运动任务必须停止输出，避免两种控制源同时控制伺服。
     */
    if (aimtor_maintenance_enabled())
    {
        /*
         * 清除底盘系统就绪标志。
         * 清除后，普通底盘运动命令不应继续执行。
         */
        s_system_ready = 0U;
        /* 清零左右轴的目标速度。 */
        s_target_velocity[0] = 0.0f;
        s_target_velocity[1] = 0.0f;
        /* 清零左右轴当前规划输出速度。 */
        s_command_velocity[0] = 0.0f;
        s_command_velocity[1] = 0.0f;
        s_pulse_test_active_mask = 0U;
        s_pulse_completion_pending_mask = 0U;
        /*
         * 将运动模式切换为空闲模式。
         * 后续不再执行速度运动或距离运动规划。
         */
        s_mode = MOTION_MODE_IDLE;
        /*
         * 停止左右轴本地脉冲输出。
         *
         * 该操作会停止STM32向AM26C31发送脉冲。
         * 如果伺服当前运行在内部速度模式或内部位置模式，
         * 还需要由AIMtor通信控制路径执行对应的停止或失能。
         */
        servo_pulse_stop_all();
        /*
         * 维护模式不能覆盖更严重的系统状态。
         *
         * 如果当前已经处于FAULT或OFFLINE，则保留原状态；
         * 否则将系统置为SAFE安全状态。
         */
        if ((s_state != MOTION_FAULT) &&
            (s_state != MOTION_OFFLINE))
        {
            s_state = MOTION_SAFE;
        }
        /*
         * 维护模式处理完成后立即返回。
         * 本周期不再执行普通运动规划和脉冲输出。
         */
        return;
    }
    /*
     * 只有RUNNING或STOPPING状态允许继续执行运动更新。
     *
     * 其他状态包括BOOT、SAFE、READY、FAULT和OFFLINE等，
     * 在这些状态下必须停止脉冲输出，防止残留运动。
     */
    if ((s_state != MOTION_RUNNING) &&
        (s_state != MOTION_STOPPING))
    {
        /* 当前不允许运动时，停止左右轴脉冲输出。 */
        if (s_state == MOTION_READY)
        {
            /* 正常完成保留计数供查询；故障仍走清理路径。 */
            (void)servo_pulse_set_frequency(SERVO_AXIS_LEFT, 0U);
            (void)servo_pulse_set_frequency(SERVO_AXIS_RIGHT, 0U);
        }
        else
        {
            servo_pulse_stop_all();
        }
        /* 结束本次运动更新。 */
        return;
    }
    /*
     * 检查运动硬件配置是否已经确认。
     *
     * 该检查通常包括：
     * 1. AM26C31输出使能极性已确认；
     * 2. 左右轴方向控制电平已确认。
     *
     * 如果硬件参数未确认，则禁止继续运动。
     */
    if (!app_config_motion_hardware_ready())
    {
        /*
         * 报告运动硬件配置故障。
         * 参数0表示这里不是由伺服通信故障直接触发。
         */
        motion_report_fault(0U);

        /* 将运动状态切换到安全状态。 */
        s_state = MOTION_SAFE;

        /* 注意：原代码此处没有显式调用servo_pulse_stop_all()，保持原有逻辑不变。 */
        return;
    }
    /*
     * 定脉冲计数由TIM9更新中断完成。
     * 到数后先在中断中关断PWM，再由任务清理规划状态，防止任务重新启动尾脉冲。
     */
    if ((s_mode == MOTION_MODE_PULSE_TEST) &&
        (s_pulse_test_active_mask != 0U))
    {
        uint8_t active = 0U;
        uint8_t pulse_axis;

        for (pulse_axis = 0U; pulse_axis < SERVO_AXIS_COUNT; pulse_axis++)
        {
            if (((s_pulse_test_active_mask & (uint8_t)(1U << pulse_axis)) != 0U) &&
                !servo_pulse_target_reached((servo_axis_t)pulse_axis))
            {
                active = 1U;
                break;
            }
        }
        if (active == 0U)
        {
            s_target_velocity[0] = 0.0f;
            s_target_velocity[1] = 0.0f;
            s_command_velocity[0] = 0.0f;
            s_command_velocity[1] = 0.0f;
            for (pulse_axis = 0U; pulse_axis < SERVO_AXIS_COUNT; pulse_axis++)
            {
                if ((s_pulse_test_active_mask &
                     (uint8_t)(1U << pulse_axis)) != 0U)
                {
                    servo_pulse_complete_axis((servo_axis_t)pulse_axis);
                    s_pulse_completion_pending_mask |=
                        (uint8_t)(1U << pulse_axis);
                }
            }
            return;
        }
    }
    /*
     * 距离运动模式处理。
     *
     * 距离模式的目标是让车辆运动到指定距离，
     * 并在接近目标位置时自动减速停止。
     */
    if (s_mode == MOTION_MODE_DISTANCE)
    {
        /*
         * 计算剩余运动距离：
         *
         * 目标距离 - 已完成距离。
         */
        remaining_distance = s_distance_target - s_distance_done;

        /*
         * 防止由于浮点误差导致剩余距离出现负值。
         */
        if (remaining_distance < 0.0f)
        {
            remaining_distance = 0.0f;
        }

        /*
         * 根据制动距离公式计算允许的最大停止速度：
         *
         * v = sqrt(2 × a × s)
         *
         * 其中：
         * a为当前加速度；
         * s为剩余距离。
         *
         * 剩余距离越小，允许速度越低，
         * 从而保证接近目标位置时能够减速停止。
         */
        stop_velocity = sqrtf(2.0f *
                              s_acceleration *
                              remaining_distance);

        /*
         * 如果当前目标速度超过允许的停止速度，
         * 则把目标速度限制为停止速度。
         *
         * 这里根据原目标速度的正负号保留运动方向。
         */
        if (motion_abs(s_target_velocity[0]) > stop_velocity)
        {
            s_target_velocity[0] =
                s_target_velocity[0] >= 0.0f ?
                stop_velocity : -stop_velocity;

            /*
             * 距离模式下，左右轴使用相同目标速度。
             * 这适用于两侧同步直线运动。
             */
            s_target_velocity[1] = s_target_velocity[0];
        }

        /*
         * 根据当前实际规划速度估算本周期完成的距离。
         *
         * 距离增量 = 速度绝对值 × 时间。
         *
         * 这里累计的是软件命令位置或命令距离，
         * 不是编码器测得的真实机械位置。
         */
        s_distance_done +=
            motion_abs(s_command_velocity[0]) *
            ((float)APP_MOTION_PERIOD_MS / 1000.0f);

        /*
         * 如果累计运动距离达到目标距离，
         * 则请求进入受控停止流程。
         */
        if (s_distance_done >= s_distance_target)
        {
            (void)motion_stop_locked(MOTION_STOP_CONTROLLED);
        }
    }


    /*
     * 依次更新左右两个轴。
     *
     * axis=0：左轴；
     * axis=1：右轴。
     */
    for (axis = 0U; axis < 2U; axis++)
    {
        /*
         * 未选中的轴必须清零并停止输出；选中的轴按自身目标速度更新。
         */
        if (!app_config_servo_axis_selected(axis))
        {
            s_target_velocity[axis] = 0.0f;
            s_command_velocity[axis] = 0.0f;
            (void)servo_pulse_set_frequency((servo_axis_t)axis, 0U);
            continue;
        }
        /*
         * 该轴已经完成定脉冲计数并进入反馈归零等待。
         * 保持输出为0，直到调试层明确释放本次完成状态。
         */
        if ((s_mode == MOTION_MODE_PULSE_TEST) &&
            ((s_pulse_test_active_mask & (uint8_t)(1U << axis)) != 0U) &&
            servo_pulse_target_reached((servo_axis_t)axis))
        {
            s_pulse_completion_pending_mask |= (uint8_t)(1U << axis);
        }
        if ((s_pulse_completion_pending_mask &
             (uint8_t)(1U << axis)) != 0U)
        {
            s_target_velocity[axis] = 0.0f;
            s_command_velocity[axis] = 0.0f;
            servo_pulse_complete_axis((servo_axis_t)axis);
            continue;
        }
        /*
         * 读取当前轴目标速度。
         * 目标速度可能来自速度命令或距离规划器。
         */
        target = s_target_velocity[axis];

        /*
         * 换向保护：
         *
         * 如果当前速度和目标速度方向相反，
         * 不允许本周期直接反向输出。
         *
         * 先将目标速度设置为0，
         * 让当前速度减速到零后，
         * 下一周期再建立相反方向的速度。
         */
        if (s_command_velocity[axis] * target < 0.0f)
        {
            target = 0.0f;
        }

        /*
         * 定脉冲模式提前进入制动区。
         *
         * 计数中断仍然负责准确数到 pulse_target，不能在这里提前关断PWM。
         * 本逻辑只把目标频率限制到能够在剩余脉冲内减到零的范围，
         * 这样到达目标脉冲时输出频率已经较低，最后由中断安全关断。
         */
        if ((s_mode == MOTION_MODE_PULSE_TEST) &&
            ((s_pulse_test_active_mask & (uint8_t)(1U << axis)) != 0U) &&
            !servo_pulse_target_reached((servo_axis_t)axis))
        {
            uint32_t pulse_count;
            uint32_t pulse_target;
            uint32_t remaining_pulses;
            float current_frequency;
            float braking_frequency;
            float braking_distance;

            pulse_count = servo_pulse_get_pulse_count((servo_axis_t)axis);
            pulse_target = servo_pulse_get_target((servo_axis_t)axis);
            if (pulse_target > pulse_count)
            {
                remaining_pulses = pulse_target - pulse_count;
                current_frequency = motion_abs(s_command_velocity[axis]) *
                                    APP_PULSES_PER_METER;
                braking_distance =
                    (current_frequency * current_frequency) /
                    (2.0f * APP_PULSE_DECELERATION_HZ_PER_S);

                if ((float)remaining_pulses <= braking_distance)
                {
                    braking_frequency = sqrtf(
                        (float)remaining_pulses *
                        2.0f * APP_PULSE_DECELERATION_HZ_PER_S);
                    /*
                     * 低于最小有效脉冲频率时仍保持100 Hz输出，
                     * 直到计数中断达到目标，避免提前停止导致少脉冲。
                     */
                    if (braking_frequency <
                        (float)APP_PULSE_MIN_HZ)
                    {
                        braking_frequency = (float)APP_PULSE_MIN_HZ;
                    }
                    if (motion_abs(target) >
                        braking_frequency / APP_PULSES_PER_METER)
                    {
                        target = target >= 0.0f ?
                            braking_frequency / APP_PULSES_PER_METER :
                            -braking_frequency / APP_PULSES_PER_METER;
                    }
                    else if ((motion_abs(target) == 0.0f) &&
                             (motion_abs(s_command_velocity[axis]) >
                              (float)APP_PULSE_MIN_HZ /
                              APP_PULSES_PER_METER))
                    {
                        target = s_command_velocity[axis] >= 0.0f ?
                            braking_frequency / APP_PULSES_PER_METER :
                            -braking_frequency / APP_PULSES_PER_METER;
                    }
                }
            }
        }

        /*
         * 目标绝对速度增大时使用加速度，目标绝对速度减小时使用减速度。
         * 反向命令已在上方被限制为先减速到零。
         */
        {
            servo_snapshot_t feedback;
            uint8_t health_axis;
            for (health_axis = 0U; health_axis < SERVO_AXIS_COUNT; ++health_axis)
                if (app_config_servo_axis_selected(health_axis) &&
                    aimtor_get_snapshot((servo_axis_t)health_axis, &feedback) &&
                    feedback.feedback_degraded)
                {
                    float applied = (float)servo_pulse_get_frequency((servo_axis_t)axis) /
                        APP_PULSES_PER_METER;
                    if (s_command_velocity[axis] < 0.0f) applied = -applied;
                    s_command_velocity[axis] = applied;
                    if (target * applied < 0.0f) target = 0.0f;
                    if (motion_abs(target) > motion_abs(applied)) target = applied;
                    break;
                }
        }
        if (motion_abs(target) > motion_abs(s_command_velocity[axis]))
        {
            rate = s_acceleration;
        }
        else
        {
            rate = s_deceleration;
        }
        step = rate * ((float)APP_MOTION_PERIOD_MS / 1000.0f);

        /*
         * 让当前命令速度逐步接近目标速度。
         *
         * 函数会根据step限制每个周期的速度变化量，
         * 实现平滑加速和减速。
         */
        s_command_velocity[axis] =
            motion_step_toward(s_command_velocity[axis],
                               target,
                               step);

        /*
         * 将当前规划速度应用到对应轴。
         *
         * 该函数通常负责：
         * 1. 根据速度计算脉冲频率；
         * 2. 设置方向；
         * 3. 启动或停止对应PWM；
         * 4. 更新脉冲输出状态。
         */
        motion_apply_axis(axis,
                          s_command_velocity[axis]);

        /*
         * 如果在输出过程中检测到故障或通信离线，
         * 立即执行双轴紧急停止。
         */
        if ((s_state == MOTION_FAULT) ||
            (s_state == MOTION_OFFLINE))
        {
            /*
             * 停止本地脉冲，并尽力通过伺服通信路径执行停止处理。
             */
            aimtor_emergency_stop();

            /*
             * 故障处理完成后退出本次运动更新。
             */
            return;
        }
    }


    /*
     * 根据本周期输出时间累计软件命令脉冲位置。
     *
     * 该位置是根据命令脉冲估算得到的命令位置，
     * 不能等同于伺服内部实际位置或编码器反馈位置。
     */
    servo_pulse_accumulate(APP_MOTION_PERIOD_MS);

    if ((s_mode == MOTION_MODE_PULSE_TEST) &&
        (s_pulse_test_active_mask != 0U))
    {
        uint8_t active = 0U;
        uint8_t pulse_axis;

        for (pulse_axis = 0U; pulse_axis < SERVO_AXIS_COUNT; pulse_axis++)
        {
            if (((s_pulse_test_active_mask & (uint8_t)(1U << pulse_axis)) != 0U) &&
                !servo_pulse_target_reached((servo_axis_t)pulse_axis))
            {
                active = 1U;
                break;
            }
        }
        if (active == 0U)
        {
            s_target_velocity[0] = 0.0f;
            s_command_velocity[0] = 0.0f;
            s_target_velocity[1] = 0.0f;
            s_command_velocity[1] = 0.0f;
            for (pulse_axis = 0U; pulse_axis < SERVO_AXIS_COUNT; pulse_axis++)
            {
                if ((s_pulse_test_active_mask &
                     (uint8_t)(1U << pulse_axis)) != 0U)
                {
                    servo_pulse_complete_axis((servo_axis_t)pulse_axis);
                    s_pulse_completion_pending_mask |=
                        (uint8_t)(1U << pulse_axis);
                }
            }
        }
    }


    /*
     * 如果系统正在执行受控停止，
     * 并且左右轴当前命令速度都已经降为零，
     * 则停止全部脉冲并进入停止目标状态。
     */
    if ((s_state == MOTION_STOPPING) &&
        (s_command_velocity[0] == 0.0f) &&
        (s_command_velocity[1] == 0.0f))
    {
        /*
         * 确保左右轴PWM通道完全停止。
         */
        servo_pulse_stop_all();

        /*
         * 根据停止前保存的目标状态进行状态切换。
         *
         * 例如：
         * 受控停止完成后进入READY或SAFE状态。
         */
        s_state = s_controlled_safety_requested ? MOTION_STOPPING : s_stop_target_state;
    }
}

void motion_update(void)
{
    /* 规划与输出构成一次短原子更新，停止请求不能被旧规划覆盖。 */
    taskENTER_CRITICAL();
    motion_update_locked();
    taskEXIT_CRITICAL();
}

/** 返回指定轴当前规划速度，非法轴号返回零。 */
float motion_get_command_velocity(uint8_t axis)
{
    return axis < 2U ? s_command_velocity[axis] : 0.0f;
}

uint8_t motion_get_pulse_active(uint8_t axis)
{
    return axis < SERVO_AXIS_COUNT &&
           (s_pulse_test_active_mask & (uint8_t)(1U << axis)) != 0U;
}

uint8_t motion_get_pulse_active_mask(uint8_t axis_mask)
{
    return (uint8_t)((s_pulse_test_active_mask & axis_mask) != 0U);
}

uint8_t motion_get_pulse_completion_pending_mask(uint8_t axis_mask)
{
    return (uint8_t)(s_pulse_completion_pending_mask & axis_mask);
}

motion_result_t motion_complete_pulse(uint8_t axis_mask)
{
    taskENTER_CRITICAL();
    if ((axis_mask == 0U) ||
        ((s_pulse_completion_pending_mask & axis_mask) != axis_mask))
    {
        taskEXIT_CRITICAL();
        return MOTION_NOT_READY;
    }
    s_pulse_completion_pending_mask &= (uint8_t)~axis_mask;
    s_pulse_test_active_mask &= (uint8_t)~axis_mask;
    if ((s_pulse_completion_pending_mask == 0U) &&
        (s_pulse_test_active_mask == 0U))
    {
        s_target_velocity[0] = 0.0f;
        s_target_velocity[1] = 0.0f;
        s_command_velocity[0] = 0.0f;
        s_command_velocity[1] = 0.0f;
        s_mode = MOTION_MODE_IDLE;
        s_state = (s_system_ready != 0U) ? MOTION_READY : MOTION_SAFE;
    }
    taskEXIT_CRITICAL();
    return MOTION_OK;
}

uint8_t motion_get_left_pulse_active(void)
{
    return motion_get_pulse_active(SERVO_AXIS_LEFT);
}

#else
#include "motion_control.h"
#include "FreeRTOS.h"
#include "task.h"
#include "aimtor_servo.h"
#include "app_health.h"
#include "servo_pulse.h"
/* Public safety state is shared by RS485, watchdog and telemetry. Only the
 * unused local pulse planner is absent. */
static volatile motion_state_t s_state;
static volatile uint8_t s_system_ready;
static volatile motion_stop_class_t s_stop_class;
static volatile motion_fault_reason_t s_fault_reason;
static volatile uint8_t s_controlled_safety_requested;
void motion_init(void)
{
    servo_pulse_init();s_state=MOTION_SAFE;s_system_ready=0U;
    s_stop_class=MOTION_STOP_CLASS_NONE;s_fault_reason=MOTION_FAULT_REASON_NONE;
    s_controlled_safety_requested=0U;
}
motion_result_t motion_set_velocity(float l,float r)
{ (void)l;(void)r;return MOTION_NOT_CONFIGURED; }
motion_result_t motion_move_distance(float d,float v,float a)
{ (void)d;(void)v;(void)a;return MOTION_NOT_CONFIGURED; }
static motion_result_t motion_stop_locked(motion_stop_mode_t mode)
{
    if(mode!=MOTION_STOP_CONTROLLED && mode!=MOTION_STOP_IMMEDIATE) return MOTION_INVALID_ARGUMENT;
    servo_pulse_stop_all();
    if(mode==MOTION_STOP_IMMEDIATE && s_state!=MOTION_FAULT && s_state!=MOTION_OFFLINE)
        s_state=s_system_ready ? MOTION_READY : MOTION_SAFE;
    else if(mode==MOTION_STOP_CONTROLLED && s_state==MOTION_RUNNING) s_state=MOTION_STOPPING;
    return MOTION_OK;
}
motion_result_t motion_stop(motion_stop_mode_t mode)
{
    motion_result_t result;
    taskENTER_CRITICAL();result=motion_stop_locked(mode);
    if(result==MOTION_OK && !s_controlled_safety_requested) s_stop_class=mode==MOTION_STOP_CONTROLLED ?
        MOTION_STOP_CLASS_NORMAL : MOTION_STOP_CLASS_IMMEDIATE_SAFETY;
    taskEXIT_CRITICAL();return result;
}
motion_state_t motion_get_state(void) { return s_state; }
void motion_update(void) {}
void motion_set_system_ready(uint8_t ready)
{
    taskENTER_CRITICAL();
    s_system_ready=(uint8_t)(!s_controlled_safety_requested && ready &&
        aimtor_motion_allowed() && app_config_motion_hardware_ready());
    if(s_system_ready) { if(s_state==MOTION_SAFE)s_state=MOTION_READY; }
    else if(!s_controlled_safety_requested) (void)motion_stop_locked(MOTION_STOP_IMMEDIATE);
    taskEXIT_CRITICAL();
}
void motion_report_fault(uint8_t offline)
{
    taskENTER_CRITICAL();s_system_ready=0U;s_controlled_safety_requested=0U;
    s_stop_class=MOTION_STOP_CLASS_IMMEDIATE_SAFETY;
    if(s_fault_reason==MOTION_FAULT_REASON_NONE) s_fault_reason=offline ?
        MOTION_FAULT_REASON_SERVO_OFFLINE : MOTION_FAULT_REASON_SERVO_FAULT;
    (void)motion_stop_locked(MOTION_STOP_IMMEDIATE);
    (void)servo_pulse_set_enable(SERVO_AXIS_LEFT,false);
    (void)servo_pulse_set_enable(SERVO_AXIS_RIGHT,false);
    s_state=offline ? MOTION_OFFLINE : MOTION_FAULT;taskEXIT_CRITICAL();
}
void motion_request_controlled_safety_stop(motion_fault_reason_t reason,uint8_t offline)
{
    (void)offline;taskENTER_CRITICAL();
    if(!s_controlled_safety_requested && s_state!=MOTION_FAULT && s_state!=MOTION_OFFLINE) {
        s_fault_reason=reason;s_stop_class=MOTION_STOP_CLASS_CONTROLLED_SAFETY;
        s_controlled_safety_requested=1U;s_system_ready=0U;s_state=MOTION_STOPPING;
        servo_pulse_stop_all();
    }
    taskEXIT_CRITICAL();
}
void motion_request_immediate_safety_stop(motion_fault_reason_t reason,uint8_t offline)
{
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_DRIVER, 0xFFU,
        (uint16_t)reason, offline);
    taskENTER_CRITICAL();s_fault_reason=reason;s_stop_class=MOTION_STOP_CLASS_IMMEDIATE_SAFETY;taskEXIT_CRITICAL();
    aimtor_record_immediate_safety_stop((uint8_t)reason);
#if APP_UNPROTECTED_MOTION_TEST_ENABLED
    aimtor_emergency_stop();
#else
    app_health_request_driver_fallback(0x100U+(uint32_t)reason);
#endif
    motion_report_fault(offline);
}
void motion_complete_controlled_safety_stop(uint8_t offline)
{
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_DRIVER, 0xFFU,
        (uint16_t)motion_get_fault_reason(), offline);
    taskENTER_CRITICAL();aimtor_emergency_stop();motion_report_fault(offline);
    s_stop_class=MOTION_STOP_CLASS_CONTROLLED_SAFETY;taskEXIT_CRITICAL();
}
motion_stop_class_t motion_get_stop_class(void) { return s_stop_class; }
motion_fault_reason_t motion_get_fault_reason(void) { return s_fault_reason; }
void motion_clear_fault(void)
{
    if(aimtor_recovery_blocks_motion())return;
    taskENTER_CRITICAL();s_system_ready=0U;(void)motion_stop_locked(MOTION_STOP_IMMEDIATE);
    s_state=MOTION_SAFE;s_stop_class=MOTION_STOP_CLASS_NONE;
    s_fault_reason=MOTION_FAULT_REASON_NONE;s_controlled_safety_requested=0U;taskEXIT_CRITICAL();
}
float motion_get_command_velocity(uint8_t axis) { (void)axis;return 0.0f; }
motion_result_t motion_start_pulse_mask(uint8_t mask,uint8_t f,uint32_t hz,uint32_t n)
{ (void)mask;(void)f;(void)hz;(void)n;return MOTION_NOT_CONFIGURED; }
motion_result_t motion_start_pulse(uint8_t axis,uint8_t f,uint32_t hz,uint32_t n)
{ (void)axis;(void)f;(void)hz;(void)n;return MOTION_NOT_CONFIGURED; }
motion_result_t motion_start_left_pulse(uint8_t f,uint32_t hz,uint32_t n)
{ (void)f;(void)hz;(void)n;return MOTION_NOT_CONFIGURED; }
uint8_t motion_get_pulse_active(uint8_t axis) { (void)axis;return 0U; }
uint8_t motion_get_pulse_active_mask(uint8_t mask) { (void)mask;return 0U; }
uint8_t motion_get_pulse_completion_pending_mask(uint8_t mask) { (void)mask;return 0U; }
motion_result_t motion_complete_pulse(uint8_t mask) { (void)mask;return MOTION_NOT_CONFIGURED; }
uint8_t motion_get_left_pulse_active(void) { return 0U; }

#endif
