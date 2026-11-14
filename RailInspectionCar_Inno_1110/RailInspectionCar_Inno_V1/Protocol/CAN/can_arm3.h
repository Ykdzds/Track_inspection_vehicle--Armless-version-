#ifndef CAN_ARM3_H
#define CAN_ARM3_H

#include "can_protocol.h"
#include <limits.h>

#define CAN_ARM3_SCHEMA 0x334D5241UL
#define CAN_ARM3_FEATURE_SCHEMA 0x55493132UL
#define CAN_ARM3_REVISION 2U
#define CAN_ARM3_FEATURES 0x000003FFUL
#define CAN_ARM3_LOCAL_FEATURES 0x0000003DUL /* presets,jog,lamp,six-axis,teach */
#define CAN_ARM3_UNKNOWN UINT32_MAX

typedef struct {
    uint32_t version, identity_crc, payload_crc, enabled_mask;
    int32_t lower[6], upper[6];
    uint8_t flags;
    bool valid;
} can_arm3_config_t;
typedef struct {
    uint32_t revision;
    int32_t angle[6];
    uint8_t flags;
    bool present;
} can_arm3_preset_t;
typedef struct {
    uint32_t sequence, age_ms, fault, received_ms;
    uint16_t angle_mask, speed_mask, span_ms;
    int32_t angle[6], speed[6];
    bool valid;
} can_arm3_pose_t;
typedef struct {
    uint32_t operation, generation, sequence, detail, age_ms, config_version;
    uint32_t received_ms, feedback_sequence, raw_fault;
    uint16_t progress;
    uint8_t mask, arm_id, phase, result, flags;
    bool present;
} can_arm3_result_t;
typedef struct {
    uint32_t operation, operation_generation, generation, safety_version;
    uint32_t usb_epoch, identity_crc, config_crc, preset_revision;
    uint32_t first_sequence, last_sequence, age_ms, received_ms, fault, id, flags;
    uint16_t window_ms, samples, max_interval_ms, span_ms, range[6], angle_mask;
    int32_t angle[6];
    uint8_t state;
    bool valid;
} can_arm3_proof_t;

bool can_arm3_newer32(uint32_t value, uint32_t previous);
uint32_t can_arm3_age(uint32_t reported, uint32_t start_ms,
                      uint32_t commit_ms, uint32_t now_ms);
void can_arm3_manifest(can_protocol_record_t *record, uint8_t role);
bool can_arm3_manifest_matches(const can_protocol_record_t *record, uint8_t role);
void can_arm3_header(can_protocol_record_t *record, uint8_t kind, uint8_t status,
    uint32_t query, uint8_t owner, uint8_t arm, uint32_t generation,
    uint32_t version, uint32_t sample_sequence, uint32_t age, uint32_t flags);
void can_arm3_query(can_protocol_record_t *record, uint32_t query, uint8_t opcode,
    uint8_t owner, uint8_t mask, uint8_t selector, uint32_t generation,
    uint32_t operation, uint32_t version, uint32_t parameter0,
    uint32_t parameter1, uint32_t now, uint32_t safety_sequence);
bool can_arm3_response_valid(const can_protocol_record_t *record);
bool can_arm3_decode_config(const can_protocol_record_t *record,
                           can_arm3_config_t *config);
bool can_arm3_decode_preset(const can_protocol_record_t *record,
                           can_arm3_preset_t *preset);
bool can_arm3_decode_pose(const can_protocol_record_t *record,
    uint32_t start, uint32_t commit, uint32_t now, can_arm3_pose_t *pose);
bool can_arm3_decode_result(const can_protocol_record_t *record,
    uint32_t start, uint32_t commit, uint32_t now, can_arm3_result_t *result);
bool can_arm3_decode_proof(const can_protocol_record_t *record,
    uint32_t start, uint32_t commit, uint32_t now, can_arm3_proof_t *proof);
bool can_arm3_proof_matches(const can_arm3_proof_t *proof,
    const can_arm3_config_t *config, const can_arm3_preset_t *preset,
    uint32_t generation, uint32_t safety_version, uint32_t operation,
    uint32_t operation_generation, uint32_t now);
bool can_arm3_stopped_proof_matches(const can_arm3_proof_t *proof,
    const can_arm3_config_t *config,uint32_t generation,uint32_t safety_version,
    uint32_t operation,uint32_t operation_generation,uint32_t now);

#endif
