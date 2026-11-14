#ifndef APP_MOTION_RAMP_H
#define APP_MOTION_RAMP_H

#include <stdint.h>
#include "app_config.h"

/* 统一的物理加减速斜坡状态。 */
typedef struct
{
    float current_rpm;
    uint8_t initialized;
} app_motion_ramp_t;

/* 将车辆线速度换算为电机轴转速，输入单位为mm/s。 */
static inline int16_t app_motion_ramp_vehicle_mm_s_to_motor_rpm(int32_t velocity_mm_s)
{
    float rpm = (float)velocity_mm_s * 60.0f * APP_GEAR_REDUCTION_RATIO /
        (APP_WHEEL_CIRCUMFERENCE_M * 1000.0f);
    int32_t rounded = (int32_t)(rpm >= 0.0f ? rpm + 0.5f : rpm - 0.5f);

    if (rounded > (int32_t)APP_OPERATION_MAX_MOTOR_RPM)
        rounded = APP_OPERATION_MAX_MOTOR_RPM;
    if (rounded < -(int32_t)APP_OPERATION_MAX_MOTOR_RPM)
        rounded = -APP_OPERATION_MAX_MOTOR_RPM;
    return (int16_t)rounded;
}

/* 将电机轴转速换算为车辆线速度，输出单位为mm/s。 */
static inline int32_t app_motion_ramp_motor_rpm_to_vehicle_mm_s(int32_t rpm)
{
    float velocity = (float)rpm * APP_WHEEL_CIRCUMFERENCE_M * 1000.0f /
        (60.0f * APP_GEAR_REDUCTION_RATIO);
    return (int32_t)(velocity >= 0.0f ? velocity + 0.5f : velocity - 0.5f);
}

/* 返回未量化的车辆线速度，单位为mm/s，供控制环和诊断计算使用。 */
static inline float app_motion_ramp_motor_rpm_to_vehicle_mm_s_f(int32_t rpm)
{
    return (float)rpm * APP_WHEEL_CIRCUMFERENCE_M * 1000.0f /
        (60.0f * APP_GEAR_REDUCTION_RATIO);
}

/* 返回车辆线加速度对应的电机rpm变化率，单位为rpm/(m/s)。 */
static inline float app_motion_ramp_motor_rpm_per_mps(void)
{
    return 60.0f * APP_GEAR_REDUCTION_RATIO / APP_WHEEL_CIRCUMFERENCE_M;
}

/* 初始化斜坡状态，current_rpm为当前已经下发的转速。 */
static inline void app_motion_ramp_reset(app_motion_ramp_t *ramp,
                                          int16_t current_rpm)
{
    if (ramp == 0) return;
    ramp->current_rpm = (float)current_rpm;
    ramp->initialized = 1U;
}

/* 清除斜坡状态，下一次计算将从调用者提供的当前转速重新开始。 */
static inline void app_motion_ramp_clear(app_motion_ramp_t *ramp)
{
    if (ramp == 0) return;
    ramp->current_rpm = 0.0f;
    ramp->initialized = 0U;
}

/* 将连续转速按目标方向安全量化为整数rpm。 */
static inline int16_t app_motion_ramp_round_rpm(float rpm)
{
    int32_t rounded = (int32_t)(rpm >= 0.0f ? rpm + 0.5f : rpm - 0.5f);
    if (rounded > 32767) rounded = 32767;
    if (rounded < -32767) rounded = -32767;
    return (int16_t)rounded;
}

/*
 * 按物理加速度推进目标转速。
 * elapsed_ms会被限制，避免任务调度延迟造成一次性大幅跳变。
 * 正反转必须先经过零速，调用者仍需根据实际整数命令完成零速确认。
 */
static inline int16_t app_motion_ramp_step_rate(app_motion_ramp_t *ramp,
                                                 int16_t target_rpm,
                                                 uint32_t elapsed_ms,
                                                 float acceleration_mps2,
                                                 float deceleration_mps2,
                                                 float rpm_per_mps)
{
    float requested_target;
    float rate_rpm_per_s;
    float delta_rpm;
    float current;
    float step_target;
    int16_t result;

    if (ramp == 0) return 0;
    if (!ramp->initialized) app_motion_ramp_reset(ramp, 0);
    if (elapsed_ms > APP_MOTION_RAMP_MAX_DT_MS)
        elapsed_ms = APP_MOTION_RAMP_MAX_DT_MS;
    if (elapsed_ms == 0U) return app_motion_ramp_round_rpm(ramp->current_rpm);
    if (!(acceleration_mps2 > 0.0f && acceleration_mps2 < 1000000.0f) ||
        !(deceleration_mps2 > 0.0f && deceleration_mps2 < 1000000.0f) ||
        !(rpm_per_mps > 0.0f && rpm_per_mps < 1000000.0f))
        return app_motion_ramp_round_rpm(ramp->current_rpm);

    current = ramp->current_rpm;
    requested_target = (float)target_rpm;
    step_target = requested_target;
    if ((current > 0.0f && requested_target < 0.0f) ||
        (current < 0.0f && requested_target > 0.0f))
        step_target = 0.0f;

    rate_rpm_per_s = ((step_target < 0.0f ? -step_target : step_target) >
                     (current < 0.0f ? -current : current) ?
                     acceleration_mps2 : deceleration_mps2) *
        rpm_per_mps;
    delta_rpm = rate_rpm_per_s * (float)elapsed_ms / 1000.0f;
    if (step_target > current)
    {
        current += delta_rpm;
        if (current > step_target) current = step_target;
    }
    else if (step_target < current)
    {
        current -= delta_rpm;
        if (current < step_target) current = step_target;
    }
    if (current > -0.0001f && current < 0.0001f) current = 0.0f;
    ramp->current_rpm = current;
    result = app_motion_ramp_round_rpm(current);
    return result;
}

static inline int16_t app_motion_ramp_step(app_motion_ramp_t *ramp,
                                            int16_t target_rpm,
                                            uint32_t elapsed_ms,
                                            float acceleration_mps2,
                                            float deceleration_mps2)
{
    return app_motion_ramp_step_rate(
        ramp, target_rpm, elapsed_ms, acceleration_mps2,
        deceleration_mps2,
        app_motion_ramp_motor_rpm_per_mps());
}

#endif
