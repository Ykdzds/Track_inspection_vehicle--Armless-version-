#ifndef REMOTE_STATUS_MODEL_H
#define REMOTE_STATUS_MODEL_H

#include "remote_status_protocol.h"

#define REMOTE_RETURN_TIMEOUT_MS 300U
#define REMOTE_SAMPLE_TIMEOUT_MS 500U
#define REMOTE_RECOVERY_CANDIDATE_TIMEOUT_MS 300U
#define REMOTE_STATUS_EVENT_CAPACITY 16U

typedef enum {
    REMOTE_EVENT_LOST = 1, REMOTE_EVENT_RECOVERED, REMOTE_EVENT_SESSION,
    REMOTE_EVENT_DUPLICATE, REMOTE_EVENT_OLD, REMOTE_EVENT_CANDIDATE,
    REMOTE_EVENT_LENGTH, REMOTE_EVENT_CRC, REMOTE_EVENT_FORMAT
} remote_status_event_kind_t;

typedef struct {
    uint32_t timestamp_ms;
    uint8_t kind, type, session, sequence;
} remote_status_event_t;

typedef enum {
    RH_SYSTEM, RH_MODE, RH_AUTH, RH_COMM, RH_TARGET, RH_BASE,
    RH_CMD1, RH_CMD2, RH_FAST1, RH_FAST2,
    RH_STATE1, RH_STATE2, RH_ENABLE1, RH_ENABLE2, RH_ZERO1, RH_ZERO2,
    RH_FAULT1, RH_FAULT2, RH_RPM1, RH_RPM2,
    RH_AGE1, RH_AGE2, RH_FAIL1, RH_FAIL2,
    RH_RAW1, RH_RAW2, RH_DIFF, RH_NET, RH_BOOT, RH_TRIP, RH_INCOMPLETE,
    RH_SYNC_FLAGS, RH_SYNC_STATE, RH_PAIR, RH_REJECT, RH_DELTA,
    RH_ERROR, RH_CORRECTION, RH_SKEW, RH_SYNC_AGE1, RH_SYNC_AGE2,
    RH_BATTERY, RH_CHARGE, RH_LOCATION, RH_ACCURACY, RH_ARM, RH_LIGHT,
    RH_ACQUISITION, RH_EXT_FAULTS,
    RH_COUNTS1, RH_COUNTS2, RH_WHEEL1, RH_WHEEL2, RH_RATIO1, RH_RATIO2,
    RH_DIRECTION1, RH_DIRECTION2, RH_SPEED_UNIT, RH_POSITION_UNIT,
    RH_ALARM_CODE, RH_ALARM_SOURCE, RH_ALARM_SEQUENCE,
    RH_VTARGET, RH_VCOMMAND, RH_FRONT, RH_REAR, RH_AVERAGE,
    RH_FRONT_AGE, RH_REAR_AGE,
    RH_TORQUE1, RH_TORQUE2, RH_LOAD1, RH_LOAD2,
    RH_CURRENT1, RH_CURRENT2, RH_VOLTAGE1, RH_VOLTAGE2,
    RH_TARGET_KMH1, RH_TARGET_KMH2, RH_COUNT
} remote_history_field_t;

typedef struct {
    int32_t value;
    uint32_t updated_ms;
    uint16_t sample_age;
    uint8_t received, stale;
} remote_history_value_t;

typedef struct {
    remote_status_frame_t frame;
    uint32_t received_ms;
    uint8_t received;
} remote_status_slot_t;

typedef struct {
    uint32_t accepted, length_errors, format_errors, crc_errors;
    uint32_t duplicates, old_frames, sequence_gaps, recoveries;
    uint32_t session_changes, link_losses, alarm_events;
    uint32_t recovery_candidates;
} remote_status_stats_t;

typedef struct {
    remote_status_slot_t slot[REMOTE_STATUS_TYPES];
    remote_status_slot_t diagnostic_cache;
    remote_status_header_t latest;
    remote_status_stats_t stats;
    remote_history_value_t history[RH_COUNT];
    uint32_t last_received_ms;
    uint32_t last_payload_ms;
    uint32_t max_payload_gap_ms;
    uint32_t max_accept_gap_ms;
    remote_status_header_t recovery_candidate;
    uint32_t recovery_candidate_ms;
    uint32_t payloads_received;
    remote_status_event_t events[REMOTE_STATUS_EVENT_CAPACITY];
    uint32_t event_overwrites;
    uint8_t event_head, event_count;
    uint8_t recovery_candidate_valid;
    uint8_t received, loss_reported, alarm_matches;
} remote_status_model_t;

typedef enum {
    REMOTE_RETURN_WAIT = 0,
    REMOTE_RETURN_ONLINE,
    REMOTE_RETURN_LOST
} remote_return_state_t;

void remote_status_init(remote_status_model_t *model);
void remote_status_tick(remote_status_model_t *model, uint32_t now_ms);
void remote_status_note_payload(remote_status_model_t *model, uint32_t now_ms);
uint8_t remote_status_pop_event(remote_status_model_t *model, remote_status_event_t *event);
uint8_t remote_status_accept(remote_status_model_t *model, const uint8_t *data,
                             size_t length, uint32_t now_ms);
remote_return_state_t remote_status_link(const remote_status_model_t *model,
                                         uint32_t now_ms);
uint8_t remote_status_fresh(const remote_status_model_t *model, uint8_t type,
                            uint32_t now_ms);
uint8_t remote_status_valid(const remote_status_model_t *model, uint8_t type,
                            uint8_t mask, uint32_t now_ms);
uint8_t remote_status_axis(const remote_status_model_t *model, uint8_t axis);
uint8_t remote_status_sample_fresh(const remote_status_model_t *model,
                                   uint8_t type, uint16_t sample_age,
                                   uint32_t now_ms);
uint8_t remote_status_alarm_detail(const remote_status_model_t *model,
                                   uint32_t now_ms);
uint8_t remote_history_axis(remote_history_field_t field);
uint8_t remote_history_fresh(const remote_status_model_t *model,
                             remote_history_field_t field, uint32_t now_ms);

#endif
