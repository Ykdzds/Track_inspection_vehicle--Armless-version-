#ifndef REMOTE_STATUS_PROTOCOL_H
#define REMOTE_STATUS_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define REMOTE_STATUS_SIZE 32U
#define REMOTE_STATUS_TYPES 10U

typedef struct {
    uint8_t type, sequence, session, axis_flags, valid_flags, alarm_level;
} remote_status_header_t;

typedef struct {
    uint32_t sequence, sample_ms;
    uint8_t owner, version, capabilities, recovery_state, recovery_eligible, recovery_failure;
    uint16_t recovery_healthy_ms;
} remote_snapshot_metadata_t;

typedef struct {
    uint8_t system, mode, authorization, communication;
    int16_t target, base, command[2], speed[2];
} remote_fast_t;

typedef struct {
    uint8_t state[2], failures[2];
    uint16_t fault[2], age[2];
    int16_t speed[2];
} remote_servo_t;

typedef struct {
    int32_t raw[2];
    int16_t difference_mm, net_dm;
    uint16_t boot_dm, trip_dm;
} remote_position_t;

typedef struct {
    uint8_t flags, state, pair, reject;
    int16_t difference, error, correction;
    uint16_t skew, age[2];
} remote_sync_t;

typedef struct {
    uint8_t battery, charge, location, accuracy, arm, light, acquisition, faults;
    int32_t coordinate[2];
} remote_extension_t;

typedef struct {
    uint16_t counts[2], wheel_mm[2], ratio_milli[2];
    uint8_t direction[2], speed_unit, position_unit;
} remote_parameter_t;

typedef struct {
    uint8_t sequence, level, code, source;
    uint16_t fault[2];
    uint32_t tick, detail;
} remote_alarm_t;

typedef struct {
    uint8_t flags, reserved;
    uint8_t control_extended, control_source, exit_pending, exit_ready;
    int16_t common_centi_kmh;
    int16_t target_mm_s, command_mm_s, front_mm_s, rear_mm_s, average_mm_s;
    uint16_t front_age_ms, rear_age_ms;
} remote_vehicle_speed_t;

typedef struct {
    int16_t torque[2], current[2];
    uint16_t load[2], voltage[2];
    uint8_t valid_mask[2], stale_mask[2];
} remote_servo_diagnostic_t;

typedef struct {
    remote_status_header_t header;
    union {
        remote_snapshot_metadata_t metadata;
        remote_fast_t fast;
        remote_servo_t servo;
        remote_position_t position;
        remote_sync_t sync;
        remote_extension_t extension;
        remote_parameter_t parameter;
        remote_alarm_t alarm;
        remote_vehicle_speed_t vehicle_speed;
        remote_servo_diagnostic_t diagnostic;
    } data;
} remote_status_frame_t;

typedef enum {
    REMOTE_PARSE_OK = 0,
    REMOTE_PARSE_LENGTH,
    REMOTE_PARSE_FORMAT,
    REMOTE_PARSE_CRC
} remote_parse_result_t;

uint16_t remote_status_crc16(const uint8_t *data, size_t length);
remote_parse_result_t remote_status_decode(const uint8_t *data, size_t length,
                                           remote_status_frame_t *out);

#endif
