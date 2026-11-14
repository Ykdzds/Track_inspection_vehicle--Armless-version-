#ifndef CAN_UPPER_H
#define CAN_UPPER_H

#include "can_protocol.h"

#define CAN_UPPER_SLOT_COUNT 7U
#define CAN_UPPER_PAYLOAD_MAX 48U

typedef struct
{
    uint8_t type;
    uint16_t sequence, length;
    uint32_t received_ms, age_ms;
    bool present, valid;
    uint8_t payload[CAN_UPPER_PAYLOAD_MAX];
} can_upper_snapshot_t;

typedef struct
{
    can_upper_snapshot_t slots[CAN_UPPER_SLOT_COUNT];
} can_upper_cache_t;

void can_upper_init(can_upper_cache_t *cache);
bool can_upper_publish(can_upper_cache_t *cache,
    const can_protocol_record_t *record, uint32_t now_ms,
    uint32_t lower_boot, uint32_t upper_boot);
bool can_upper_get(const can_upper_cache_t *cache, uint8_t type,
    uint32_t now_ms, can_upper_snapshot_t *snapshot);

#endif
