#ifndef SERVO_DEBUG_CONTROL_H
#define SERVO_DEBUG_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "aimtor_servo.h"
#include "app_config.h"

typedef enum
{
    SERVO_DEBUG_IDLE = 0,
    SERVO_DEBUG_PREPARING,
    SERVO_DEBUG_READY,
    SERVO_DEBUG_RUNNING,
    SERVO_DEBUG_STOPPING,
    SERVO_DEBUG_FAULT
} servo_debug_state_t;

typedef enum
{
    SERVO_DEBUG_MODE_NONE = 0,
    SERVO_DEBUG_MODE_PULSE,
    SERVO_DEBUG_MODE_RS485_SPEED,
    SERVO_DEBUG_MODE_RS485_POSITION
} servo_debug_mode_t;

typedef struct
{
    servo_debug_mode_t mode;
    servo_debug_state_t state;
#if APP_CLI_SERVO_ACTIVE
    servo_snapshot_t servo;
#endif
    int32_t target;
    int16_t target_rpm;
#if APP_CLI_SERVO_ACTIVE && APP_SERVO_PULSE_ENABLED
    uint32_t target_frequency_hz;
    uint32_t target_pulses;
    uint32_t pulse_count;
    uint8_t pulse_output_active;
    uint8_t pulse_target_reached;
    uint8_t pulse_stop_confirmed;
    uint8_t pulse_completion_timeout;
    uint8_t pulse_forward;
    uint8_t pulse_position_valid;
    uint8_t pulse_position_stable;
    int32_t pulse_position_start;
    int32_t pulse_position_now;
    int32_t pulse_position_delta;
    int32_t pulse_position_target;
    int32_t pulse_position_error;
#endif
    servo_result_t result;
} servo_debug_status_t;

void servo_debug_init(void);
/* 仅会话任务修改运行时目标，切换会丢弃上一会话的启动上下文。 */
void servo_debug_select_axis(servo_axis_t axis);
void servo_debug_release_axis(void);
void servo_debug_adopt_speed(void);
servo_axis_t servo_debug_get_axis(void);
void servo_debug_service(void);
void servo_debug_motion_service(void);

servo_result_t servo_debug_prepare(servo_debug_mode_t mode);
servo_result_t servo_debug_enable(void);
servo_result_t servo_debug_disable(void);
servo_result_t servo_debug_stop(void);
servo_result_t servo_debug_emergency_stop(void);
/* 底层故障路径已经执行急停后，仅清除调试状态机的旧启动请求。 */
void servo_debug_abort_from_fault(void);

servo_result_t servo_debug_set_speed(int16_t rpm);
servo_result_t servo_debug_run_position(int32_t pulses,
                                        uint16_t rpm,
                                        uint16_t ramp_ms);
servo_result_t servo_debug_run_pulse(bool forward,
                                     uint32_t frequency_hz,
                                     uint32_t pulses);

bool servo_debug_get_status(servo_debug_status_t *status);
const char *servo_debug_mode_name(servo_debug_mode_t mode);
const char *servo_debug_state_name(servo_debug_state_t state);

#endif
