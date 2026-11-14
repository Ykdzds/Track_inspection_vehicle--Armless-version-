#ifndef APP_ARM3_H
#define APP_ARM3_H

#include "can_arm3.h"

/* All entry points run in one serialized business context. Never call in an ISR.
 * The input adapters retain their own wire sessions and submit normalized intents.
 * The work_lock callback MUST atomically recheck the chassis arbiter on acquire. */
typedef enum {
    APP_ARM3_APPLY=1, APP_ARM3_ALIVE=2, APP_ARM3_RELEASE=3, APP_ARM3_STOP=4,
    APP_ARM3_JOG=5, APP_ARM3_PRESET=6, APP_ARM3_SIX_AXIS=7,
    APP_ARM3_LIGHT_ON=8, APP_ARM3_LIGHT_OFF=9, APP_ARM3_SAVE_ACTUAL=10,
    APP_ARM3_SAVE_TARGET=11, APP_ARM3_READ_POSE=12
} app_arm3_command_t;
typedef struct {
    uint8_t source, command, mask, joint, grade, preset, light_level;
    int8_t direction;
    uint32_t session, sequence, expected_version;
    uint16_t budget_ms;
    bool held, page_active;
    int32_t angle[6];
} app_arm3_intent_t;
typedef struct app_arm3_platform {
    bool profile_enabled, safety_config_approved, chassis_idle, chassis_moving;
    bool no_pending_writes, parameter_locked, recovery_locked, clock_valid;
    uint32_t safety_version, hardware_bitmap, sample_age_ms, sample_tick_ms;
    uint16_t safety_flags, chassis_zero_stable_ms, chassis_feedback_age_ms;
    uint16_t chassis_zero_threshold_mm_s;
    uint32_t raw_active_high_mask, mapped_input_mask, config_flags;
    uint32_t wiring_crc, policy_crc;
    uint16_t debounce_ms;
} app_arm3_platform_t;
typedef struct {
    void *context;
    bool (*work_lock)(void *context, bool acquire);
    void (*vehicle_stop)(void *context);
    /* Force unknown transport lock immediately; never grants arm ownership. */
    void (*transport_lock)(void *context);
} app_arm3_callbacks_t;
typedef struct {
    uint32_t request, generation, received_ms, sequence;
    uint8_t source, result;
    bool present, pending;
} app_arm3_input_result_t;
typedef struct {
    uint32_t operation, generation, version, sequence, result_sequence;
    uint32_t feedback_age_ms, raw_fault, detail, usb_epoch, identity_crc;
    uint32_t config_crc, preset3_revision, proof_id;
    int32_t angle[6], speed[6];
    uint16_t angle_mask, speed_mask;
    uint8_t state, phase, result, result_flags, preset_mask, proof_state;
    bool online, active, stopping, feedback_real, diagnostic_valid, identity_valid, config_valid, transport_approved;
} app_arm3_side_snapshot_t;
typedef struct {
    uint32_t lower_boot, upper_boot, generation, arm_version, safety_version;
    uint32_t safety_sequence, owner_sequence, reason, now_ms, snapshot_sequence;
    uint32_t source_age_ms, last_operation, rejected, queue_full;
    uint16_t owner_remaining_ms, input_remaining_ms;
    uint8_t owner, state, permit_mask, active_mask, transport_mask, feedback_mask;
    uint32_t feature_mask;
    bool negotiated, config_match, link_ready, work_locked, safety_valid, parameter_locked, chassis_busy;
    app_arm3_side_snapshot_t side[2];
} app_arm3_snapshot_t;

/* Return values: 0 success, 2 unsupported,3 unauthorized,4 interlock,
 * 5 busy,6 parameter/configuration,7 stale,13 internal,14 conflict. */
void app_arm3_init(const app_arm3_callbacks_t *callbacks);
void app_arm3_link(uint32_t lower_boot,uint32_t upper_boot,uint16_t token,
    bool ready,uint32_t peer_capabilities,uint32_t now_ms);
void app_arm3_set_platform(const app_arm3_platform_t *platform);
/* Serialized adapter scope: preserve real receive age while evaluating current safety. */
void app_arm3_input_delay(uint32_t elapsed_ms);
uint8_t app_arm3_submit(const app_arm3_intent_t *intent,uint32_t now_ms,
                       uint32_t *operation);
bool app_arm3_get_input_result(uint8_t source,uint32_t session,uint32_t sequence,
                               app_arm3_input_result_t *result);
bool app_arm3_get_result(uint32_t operation,uint8_t arm_id,uint32_t now_ms,
                         can_arm3_result_t *result);
bool app_arm3_request_result(uint32_t operation,uint8_t mask,uint32_t now_ms);
void app_arm3_global_stop(uint32_t now_ms);
void app_arm3_transport_fault(uint32_t now_ms);
void app_arm3_service(uint32_t now_ms);
bool app_arm3_receive(const can_protocol_record_t *record,uint32_t start_ms,
                      uint32_t commit_ms,uint32_t now_ms);
/* Independent urgent drain: stop and priority-zero permit/revoke only. */
bool app_arm3_take_urgent(can_protocol_record_t *record,uint8_t *priority,
                          uint32_t *constructed_ms);
bool app_arm3_take_record(can_protocol_record_t *record,uint8_t *priority,
                          uint32_t *constructed_ms);
/* Notify after final COMMIT TX_OK, never after merely enqueuing. */
void app_arm3_record_transmitted(const can_protocol_record_t *record,
                                uint32_t completed_ms);
/* Discard stale queued source snapshots; preserve immutable action uptime. */
bool app_arm3_prepare_record(can_protocol_record_t *record,
    uint32_t constructed_ms,uint32_t now_ms);
void app_arm3_build_safety(can_protocol_record_t *record,uint32_t now_ms);
void app_arm3_snapshot(app_arm3_snapshot_t *snapshot,uint32_t now_ms);
/* CAN-task-only scalar; does not build a full display/safety snapshot. */
bool app_arm3_negotiated(void);
bool app_arm3_work_locked(void);
bool app_arm3_profile_enabled(void);
uint32_t app_arm3_capabilities(void);

#endif
