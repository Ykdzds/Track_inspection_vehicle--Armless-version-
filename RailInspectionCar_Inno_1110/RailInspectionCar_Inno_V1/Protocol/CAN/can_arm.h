#ifndef CAN_ARM_H
#define CAN_ARM_H

#include "can_protocol.h"

#define CAN_ARM_COUNT              (2U)
#define CAN_ARM_JOINT_CACHE_COUNT  (32U)

typedef struct
{
    uint8_t arm_id;
    uint8_t state;
    uint16_t flags;
    uint32_t operation_id;
    uint32_t sample_sequence;
    uint32_t sample_age_ms;
    uint32_t fault_code;
    uint32_t config_version;
    uint32_t safety_generation;
    uint16_t last_result;
    uint16_t joint_count;
    uint32_t received_ms;
    bool present;
    bool valid;
} can_arm_summary_t;

typedef struct
{
    uint8_t arm_id;
    uint8_t joint_id;
    uint8_t joint_count;
    uint8_t flags;
    int32_t actual_angle;
    int32_t target_angle;
    int32_t actual_angular_speed;
    uint32_t action_group_id;
    uint32_t fault_code;
    uint32_t sample_age_ms;
    uint32_t config_version;
    uint32_t received_ms;
    bool present;
    bool valid;
} can_arm_joint_t;

typedef struct
{
    uint32_t operation_id;
    uint32_t safety_generation;
    uint8_t arm_mask;
    uint8_t phase;
    uint8_t result;
    uint8_t flags;
    uint32_t detail;
    uint16_t progress;
    uint32_t sample_age_ms;
    uint32_t config_version;
    uint32_t result_sequence;
    uint32_t received_ms;
    bool present;
    bool valid;
} can_arm_result_t;

typedef struct
{
    uint32_t invalid_records;
    uint32_t joint_replaced;
    uint32_t result_updates;
    uint32_t config_changes;
} can_arm_stats_t;

typedef struct
{
    can_arm_summary_t summary[CAN_ARM_COUNT];
    can_arm_joint_t joints[CAN_ARM_JOINT_CACHE_COUNT];
    can_arm_result_t result;
    can_arm_stats_t stats;
} can_arm_cache_t;

void can_arm_init(can_arm_cache_t *cache);
void can_arm_invalidate(can_arm_cache_t *cache);
bool can_arm_publish(can_arm_cache_t *cache,
                     const can_protocol_record_t *record,
                     uint32_t now_ms);
bool can_arm_get_summary(const can_arm_cache_t *cache, uint8_t arm_id,
                         uint32_t now_ms, can_arm_summary_t *summary);
bool can_arm_get_joint(const can_arm_cache_t *cache, uint8_t arm_id,
                       uint8_t joint_id, uint32_t now_ms,
                       can_arm_joint_t *joint);
bool can_arm_get_result(const can_arm_cache_t *cache, uint32_t now_ms,
                        can_arm_result_t *result);
void can_arm_get_stats(const can_arm_cache_t *cache, can_arm_stats_t *stats);

#endif
