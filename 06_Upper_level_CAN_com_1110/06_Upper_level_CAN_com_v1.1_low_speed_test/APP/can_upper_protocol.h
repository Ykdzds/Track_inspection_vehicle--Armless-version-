#ifndef CAN_UPPER_PROTOCOL_H
#define CAN_UPPER_PROTOCOL_H
#include <stdint.h>

#include "can_upper_config.h"
#define UPPER_CAPABILITIES (0x40800003UL | (UPPER_CAN_MOTION_ENABLED ? 0x20000000UL : 0UL))
#define UPPER_RECORD_COUNT 18U
#define UPPER_LINK_TIMEOUT_MS 1000U

typedef enum { UPPER_WAIT_BOOT, UPPER_WAIT_INVITE, UPPER_WAIT_CONFIRM, UPPER_WAIT_HEARTBEAT, UPPER_READY, UPPER_VERSION_ERROR, UPPER_BOOT_ERROR } UpperLinkState_t;
typedef enum { UPPER_EVENT_LINK, UPPER_EVENT_REJECT, UPPER_EVENT_RECORD, UPPER_EVENT_TIMEOUT, UPPER_EVENT_RESTART, UPPER_EVENT_TX_ERROR } UpperEvent_t;

typedef struct
{
    uint8_t data[8];
    uint32_t timestamp;
    uint8_t valid;
} UpperBasic_t;

typedef struct
{
    uint8_t type;
    uint8_t valid;
    uint16_t length;
    uint16_t sequence;
    uint32_t timestamp;
    uint8_t payload[104];
} UpperRecord_t;

typedef struct
{
    UpperLinkState_t state;
    uint32_t upper_boot_id, lower_boot_id;
    uint16_t link_token, candidate_token, invite_sequence;
    uint8_t heartbeat_sequence, heartbeat_seen, heartbeat_streak, tx_heartbeat_sequence;
    uint8_t axes, control_source, lifecycle, gates;
    uint32_t heartbeat_at, confirm_at, last_rx_at;
    uint16_t last_rx_id;
    uint32_t rejected, record_errors, records_received;
    uint32_t lower_capabilities, config_revision;
    uint32_t snapshot_sequence, snapshot_ms, snapshot_received_ms, snapshot_source_age;
    uint8_t snapshot_valid;
    int16_t actual_mm_s, target_mm_s, left_rpm, right_rpm;
    uint16_t fault, left_fault, right_fault;
    UpperBasic_t basic[9];
    UpperRecord_t records[UPPER_RECORD_COUNT];
} UpperSnapshot_t;

typedef uint8_t (*UpperSend_t)(void *user, uint16_t id, const uint8_t data[8]);
typedef void (*UpperEventCallback_t)(void *user, UpperEvent_t event, uint32_t detail);

typedef struct
{
    UpperSnapshot_t snapshot;
    UpperSend_t send;
    UpperEventCallback_t event;
    void *user;
    uint32_t boot_at, heartbeat_tx_at, handshake_at;
    uint8_t boot_sent, heartbeat_sent, handshake_sent;
    uint8_t active_record, record_header[8], record_payload[256], segment_seen[64];
    uint32_t record_started_at;
    uint16_t last_record_sequence;
    uint16_t retired_link_token;
    uint8_t record_sequence_seen;
    uint8_t capability_active, capability_step, capability_waiting, capability_published;
    uint8_t capability_attempts, capability_confirmed;
    uint16_t capability_sequence, upper_record_sequence;
    uint8_t capability_header[8], capability_payload[32];
    uint32_t capability_at, capability_retry_at;
} UpperProtocol_t;

uint16_t Upper_ReadU16(const uint8_t *data);
uint32_t Upper_ReadU32(const uint8_t *data);
uint64_t Upper_ReadU64(const uint8_t *data);
uint32_t Upper_Crc32(const uint8_t *data, uint32_t length);
void Upper_Init(UpperProtocol_t *protocol, uint32_t boot_id, UpperSend_t send, UpperEventCallback_t event, void *user);
void Upper_ResetSession(UpperProtocol_t *protocol, uint32_t now);
void Upper_RetryCapabilities(UpperProtocol_t *protocol, uint32_t now);
void Upper_Receive(UpperProtocol_t *protocol, uint16_t id, uint8_t dlc, const uint8_t data[8], uint32_t received_at, uint32_t now);
void Upper_Tick(UpperProtocol_t *protocol, uint32_t now, uint8_t healthy);
void Upper_TxResult(UpperProtocol_t *protocol, uint16_t id, const uint8_t data[8], uint8_t success, uint32_t now);
const char *Upper_StateName(UpperLinkState_t state);
uint8_t Upper_BasicFresh(const UpperSnapshot_t *snapshot, uint8_t index, uint32_t now);
uint8_t Upper_RecordFresh(const UpperSnapshot_t *snapshot, const UpperRecord_t *record, uint32_t now);
#endif
