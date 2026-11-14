#ifndef APP_DUAL_AXIS_MOTION_H
#define APP_DUAL_AXIS_MOTION_H

#include "app_remote_servo_sync.h"

/* All continuous state is wheel rad/s, rad/s^2 and rad/s^3. Public protocols
 * and legacy sync diagnostics keep their existing motor-rpm units. */
enum {
    DUAL_LIMIT_RATE = 1U, DUAL_LIMIT_SPEED = 2U, DUAL_LIMIT_ZERO = 4U,
    DUAL_LIMIT_FEEDBACK = 8U, DUAL_LIMIT_JERK = 16U,
    DUAL_HARD_FALLBACK = 32U, DUAL_WRITE_CHANGED = 64U
};
typedef struct {
    float duration, jerk;
} app_dual_segment_t;
typedef struct {
    float velocity, acceleration, target;
    app_dual_segment_t segment[3];
    uint8_t count, index;
} app_dual_trajectory_t;
typedef struct {
    uint8_t owner, mask, initialized, zeroing, s_mode, pi_mode;
    uint32_t parameter_version, parameter_used_ms;
    uint32_t generation, target_sequence, target_received_ms, deadline_ms;
    uint32_t service_ms, allocation_ms, batch, submitted_ms[2], acknowledged_ms[2];
    uint32_t clipped, discarded_ms, integrated_ms, allocation_budget_ms, constraints;
    float acceleration_limit, deceleration_limit, jerk_limit;
    app_dual_trajectory_t trajectory;
    float command[2], command_acceleration[2];
    float correction, integral, requested, applied, common_error;
    float overshoot_bound, overshoot_retract_seconds, actual[2];
    int16_t planned_rpm[2], submitted_rpm[2], acknowledged_rpm[2];
    uint8_t submitted_mask, acknowledged_mask, failed_mask;
} app_dual_axis_motion_t;

float app_dual_motor_to_wheel(float rpm, uint8_t axis);
float app_dual_wheel_to_motor(float omega, uint8_t axis);
float app_dual_nominal_rpm_to_wheel(float rpm);
float app_dual_vehicle_mm_s_to_wheel(float mm_s);
bool app_dual_config_valid(void);
void app_dual_begin(app_dual_axis_motion_t *s, uint8_t owner, uint32_t generation,
    uint8_t mask, uint32_t now_ms, float acceleration_mps2, float deceleration_mps2);
void app_dual_cancel(app_dual_axis_motion_t *s);
/* Retarget settles the old trajectory first. Repeated keepalive never resets dt. */
void app_dual_target(app_dual_axis_motion_t *s, float omega, uint32_t sequence,
    uint32_t received_ms, uint32_t deadline_ms, uint32_t now_ms);
void app_dual_advance(app_dual_axis_motion_t *s, uint32_t now_ms);
void app_dual_anchor(app_dual_axis_motion_t *s, const int16_t rpm[2], uint32_t now_ms);
/* Explicit reversal-zero phase, owned by the original source state machine. */
void app_dual_zero(app_dual_axis_motion_t *s, const int16_t rpm[2], uint32_t now_ms);
/* Call only when a new batch may be latched. Does not write the bus. */
bool app_dual_output(app_dual_axis_motion_t *s, remote_sync_status_t *sync,
    const servo_snapshot_t *front, const servo_snapshot_t *rear, TickType_t tick,
    uint32_t now_ms, uint8_t compensate, int16_t rpm[2]);
void app_dual_execution(app_dual_axis_motion_t *s, uint32_t generation, uint32_t batch,
    uint8_t axis, int16_t actual_rpm, uint32_t now_ms, uint8_t completed,
    uint8_t success);

/* Analytic velocity OTG. Exposed for deterministic host verification. */
bool app_dual_trajectory_plan(app_dual_trajectory_t *s, float target,
    float positive_acceleration, float negative_acceleration, float jerk);
void app_dual_trajectory_step(app_dual_trajectory_t *s, float seconds);

#endif
