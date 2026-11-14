#ifndef APP_POSITION_MONITOR_H
#define APP_POSITION_MONITOR_H

#include <stdbool.h>
#include <stdint.h>
#include "aimtor_servo.h"

typedef enum
{
    POSITION_MONITOR_DISABLED = 0,
    POSITION_MONITOR_WAITING,
    POSITION_MONITOR_TRACKING,
    POSITION_MONITOR_STALE,
    POSITION_MONITOR_INVALID
} position_monitor_state_t;

typedef struct
{
    uint8_t selected;
    uint8_t valid;
    uint8_t incomplete;
    uint8_t trip_anchor_pending;
    int32_t raw_position;
    uint32_t sequence;
    uint32_t generation;
    uint32_t age_ms;
    uint32_t lost_intervals;
    int64_t boot_net_counts;
    int64_t trip_net_counts;
    uint64_t boot_path_counts;
    uint64_t trip_path_counts;
    double boot_net_mm;
    double trip_net_mm;
    double boot_path_mm;
    double trip_path_mm;
    position_monitor_state_t state;
} position_axis_status_t;

typedef struct
{
    uint8_t enabled;
    uint8_t source_mask;
    uint8_t vehicle_valid;
    uint8_t pair_valid;
    uint8_t rate_valid;
    uint8_t skew_estimate_valid;
    uint8_t trip_reset_pending;
    position_monitor_state_t state;
    position_axis_status_t axis[SERVO_AXIS_COUNT];
    uint32_t baseline_generation;
    uint32_t authorization_generation;
    uint32_t pair_sequence[SERVO_AXIS_COUNT];
    uint32_t pair_age_ms;
    uint32_t sample_skew_ms;
    uint32_t pair_count;
    uint32_t lost_intervals;
    uint32_t trip_reset_count;
    uint32_t trip_reset_rejected;
    int64_t relative_counts[SERVO_AXIS_COUNT];
    int64_t raw_difference_counts;
    int64_t difference_counts;
    double difference_mm;
    double difference_rate_mm_s;
    double skew_estimate_mm;
    double vehicle_boot_net_mm;
    double vehicle_trip_net_mm;
    double vehicle_boot_path_mm;
    double vehicle_trip_path_mm;
} position_monitor_status_t;

void app_position_monitor_init(void);
void app_position_monitor_service(void);
bool app_position_monitor_get_status(position_monitor_status_t *status);
/* 仅申请清零，运动任务重新检查门禁后执行，不写驱动器参数。 */
bool app_position_monitor_request_trip_reset(void);
const char *app_position_monitor_state_name(position_monitor_state_t state);

#endif
