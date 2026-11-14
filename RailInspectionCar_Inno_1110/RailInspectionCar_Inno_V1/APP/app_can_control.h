#ifndef APP_CAN_CONTROL_H
#define APP_CAN_CONTROL_H

#include "app_remote_servo_sync.h"
#include "can_identity.h"
#include "can_protocol.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    CAN_CONTROL_IDLE, CAN_CONTROL_PREPARING, CAN_CONTROL_FIRST_ZERO,
    CAN_CONTROL_READY, CAN_CONTROL_RUNNING, CAN_CONTROL_REVERSING,
    CAN_CONTROL_DRAIN, CAN_CONTROL_DECEL, CAN_CONTROL_ZERO,
    CAN_CONTROL_HOLD, CAN_CONTROL_DONE, CAN_CONTROL_FAULT
} app_can_control_state_t;

/* Local transaction state. stop_external remains historical wire evidence. */
typedef enum
{
    CAN_CROSS_STOP_NONE, CAN_CROSS_STOP_RUNNING,
    CAN_CROSS_STOP_SUCCEEDED_RELEASED, CAN_CROSS_STOP_FAILED_SAFETY
} app_can_cross_stop_result_t;

typedef struct
{
    uint32_t generation, batch, stop_generation, sample_ms;
    uint32_t speed_age_ms, remaining_ms, stop_elapsed_ms, stop_timeout_ms;
    uint32_t first_command_ms, zero_ms, complete_ms;
    uint32_t rejected, duplicates, conflicts, queue_full, grants;
    uint32_t write_sequence[2];
    int16_t commanded_rpm[2], pending_rpm[2], target_mm_s, actual_mm_s;
    uint16_t motion_token, speed_sequence, stop_sequence, limit_mm_s, detail;
    uint8_t axis_mask, pending_mask, zero_mask, failed_mask, stop_phase, stop_result;
    uint8_t stop_reason, work_mode;
    uint8_t stop_owner;
    bool stop_external;
    app_can_cross_stop_result_t cross_stop_result;
    bool authorized, first_zero_done, speed_seen, stop_seen, actual_valid, strategy_ready;
    app_can_control_state_t state;
    float stop_deceleration;
    remote_sync_status_t sync;
} app_can_control_status_t;

typedef struct
{
    uint8_t sequence, command, result, phase;
    uint16_t detail, link_token;
    uint32_t link_generation;
} app_can_control_result_t;

void app_can_control_init(const can_identity_t *identity);
void app_can_control_link(uint16_t token, uint32_t generation, uint32_t boot,
                          uint32_t heartbeat_ms, bool ready, bool strategy);
uint8_t app_can_control_submit(const uint8_t request[8], uint32_t received_ms,
                              uint32_t link_generation);
bool app_can_control_speed(const uint8_t data[8], uint32_t received_ms);
bool app_can_control_stop(const uint8_t data[8], uint32_t received_ms);
void app_can_control_safety(void);
void app_can_control_arm3_revision(bool enabled);
void app_can_control_recovery_reset(void);
void app_can_control_pre_service(void);
void app_can_control_service(void);
bool app_can_control_take_result(app_can_control_result_t *result);
bool app_can_control_get_status(app_can_control_status_t *status);
/* Narrow read-only projection of the already published state, at most 24 words. */
uint8_t app_can_control_diag_words(uint32_t *words, uint8_t phase, uint32_t *sampled_ms);
uint16_t app_can_control_limit(void);
/* ARM3 opcode10: explicit same-session 080 -> actual execution binding. */
bool app_can_control_stop_binding(const can_protocol_record_t *query,
                                  can_protocol_record_t *reply, uint32_t now_ms);
double app_can_control_rpm_mm_s(int16_t rpm, uint8_t axis);
bool app_can_control_config_valid(void);

#endif
