#ifndef APP_REMOTE_SERVO_SYNC_H
#define APP_REMOTE_SERVO_SYNC_H

#include "aimtor_servo.h"

typedef enum
{
    REMOTE_SYNC_DISABLED = 0,
    REMOTE_SYNC_TRACKING,
    REMOTE_SYNC_LIMITED,
    REMOTE_SYNC_STALE,
    REMOTE_SYNC_REVERSING,
    REMOTE_SYNC_FAULT
} remote_sync_state_t;

typedef enum
{
    REMOTE_PAIR_OK = 0,
    REMOTE_PAIR_REPEAT,
    REMOTE_PAIR_AGE,
    REMOTE_PAIR_SKEW,
    REMOTE_PAIR_GATE,
    REMOTE_PAIR_WAITING
} remote_pair_reason_t;

typedef struct
{
    uint8_t enabled;
    uint8_t active;
    remote_sync_state_t state;
    uint8_t pair_valid;
    remote_pair_reason_t reject_reason;
    int32_t raw_error;
    int32_t effective_error;
    float requested_correction;
    float applied_correction;
    float effective_limit;
    int16_t integer_correction;
    uint32_t sequence[2];
    uint32_t age_ms[2];
    uint32_t skew_ms;
    /* Integral time and bounded P slew time have different recovery rules. */
    uint32_t dt_ms;
    uint32_t calc_interval_ms, pair_gap_ms, slew_dt_ms, raw_age_ms[2];
    uint32_t new_pair_count, slew_update_count, zero_slew_fresh_count;
    uint32_t invalidation_count, long_gap_count, calc_interval_max_ms, slew_dt_max_ms;
    uint32_t over_limit_duration_ms;
    uint8_t over_limit;
    uint8_t trip;
    uint8_t new_pair;
    uint8_t zero_pairs;
    uint8_t have_pair;
    /* new_pair/pair_valid describe this calculation; validity below survives
     * intervening service calls, never beyond original sample age. */
    uint8_t feedback_valid, correction_valid, aligned;
    float paired_rpm[2], error_f;
    TickType_t sample_tick[2], aligned_tick;
    uint32_t alignment_sequence[2][2];
    TickType_t alignment_tick[2][2];
    TickType_t pair_tick;
    TickType_t calculation_tick;
    TickType_t over_tick;
    servo_speed_sample_t pending[2][SERVO_SPEED_SAMPLE_CAPACITY];
    uint32_t received_sequence[2];
    TickType_t received_tick[2];
    uint8_t received_tick_valid[2];
    uint8_t pending_count[2];
    uint32_t pair_attempts;
    uint32_t pair_valid_count;
    uint32_t pair_reject_count;
    uint32_t pair_reject_repeat_count;
    uint32_t pair_reject_skew_count;
    uint32_t pair_reject_age_count;
    uint32_t pair_max_skew_ms;
    uint32_t pair_last_skew_ms;
} remote_sync_status_t;

typedef struct
{
    float maximum_rpm, kp, correction_limit, correction_rate;
    int32_t deadband, minimum_rpm, error_rpm;
    uint32_t error_ms;
    uint8_t trip_enabled;
    float feedback_scale[2];
} servo_sync_policy_t;
void app_servo_sync_policy_default(servo_sync_policy_t *policy);
/* 配对诊断与运动补偿分离，允许停车时只计算可信实际车速。 */
void app_servo_sync_update(remote_sync_status_t *sync,
    const servo_snapshot_t *left, const servo_snapshot_t *right, int16_t base,
    uint8_t diagnostic_gate, uint8_t compensation_gate, TickType_t now,
    const servo_sync_policy_t *policy);

/* 每次服务采集新反馈，活动速度批次期间也保留最近样本。 */
void app_remote_sync_collect(remote_sync_status_t *sync,
                             const servo_snapshot_t *left,
                             const servo_snapshot_t *right, TickType_t now);
void app_remote_sync_reset(remote_sync_status_t *sync,
                           const servo_snapshot_t *left,
                           const servo_snapshot_t *right, TickType_t now);
void app_remote_sync_update(remote_sync_status_t *sync,
                            const servo_snapshot_t *left,
                            const servo_snapshot_t *right,
                            int16_t base, uint8_t gate, TickType_t now);
const char *app_remote_sync_state_name(remote_sync_state_t state);
const char *app_remote_sync_reject_name(remote_pair_reason_t reason);
#endif
