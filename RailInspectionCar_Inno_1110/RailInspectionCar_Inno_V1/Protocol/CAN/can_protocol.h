#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define CAN_PROTOCOL_RECORD_MAX 256U
#define CAN_PROTOCOL_RECORD_TIMEOUT_MS 200U

typedef struct
{
    uint8_t type;
    uint16_t sequence;
    uint16_t token;
    uint16_t length;
    uint8_t payload[CAN_PROTOCOL_RECORD_MAX];
    /* Local metadata, never encoded or included in CRC. */
    uint32_t received_start_ms, received_commit_ms;
} can_protocol_record_t;

typedef struct
{
    can_protocol_record_t record;
    uint32_t started_ms;
    uint64_t received;
    bool active;
} can_protocol_reassembly_t;

typedef enum
{
    CAN_PROTOCOL_IGNORED,
    CAN_PROTOCOL_PROGRESS,
    CAN_PROTOCOL_COMPLETE,
    CAN_PROTOCOL_INVALID,
    CAN_PROTOCOL_EXPIRED
} can_protocol_result_t;

uint16_t can_protocol_read_u16(const uint8_t *data);
uint32_t can_protocol_read_u32(const uint8_t *data);
void can_protocol_write_u16(uint8_t *data, uint16_t value);
void can_protocol_write_u32(uint8_t *data, uint32_t value);
void can_protocol_write_u64(uint8_t *data, uint64_t value);
uint32_t can_protocol_crc32(const uint8_t *data, uint16_t length);
uint32_t can_protocol_record_crc32(const can_protocol_record_t *record);
bool can_protocol_sequence_newer(uint16_t value, uint16_t previous);
bool can_protocol_upper_id(uint16_t id);
void can_protocol_reassembly_reset(can_protocol_reassembly_t *state);
can_protocol_result_t can_protocol_receive_record(
    can_protocol_reassembly_t *state, uint16_t id, const uint8_t data[8],
    uint16_t expected_token, uint32_t now_ms);
bool can_protocol_encode_record_frame(const can_protocol_record_t *record,
    uint16_t index, uint16_t *id, uint8_t data[8]);

#endif
