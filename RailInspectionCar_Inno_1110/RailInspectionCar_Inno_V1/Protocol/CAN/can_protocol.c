#include "can_protocol.h"
#include <string.h>

uint16_t can_protocol_read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

uint32_t can_protocol_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
        ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

void can_protocol_write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

void can_protocol_write_u32(uint8_t *p, uint32_t value)
{
    uint8_t i;
    for (i = 0U; i < 4U; ++i) p[i] = (uint8_t)(value >> (i * 8U));
}

void can_protocol_write_u64(uint8_t *p, uint64_t value)
{
    uint8_t i;
    for (i = 0U; i < 8U; ++i) p[i] = (uint8_t)(value >> (i * 8U));
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint16_t length)
{
    uint16_t i;
    uint8_t bit;
    for (i = 0U; i < length; ++i)
    {
        crc ^= p[i];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc;
}

uint32_t can_protocol_crc32(const uint8_t *p, uint16_t length)
{
    return crc32_update(0xFFFFFFFFUL, p, length) ^ 0xFFFFFFFFUL;
}

uint32_t can_protocol_record_crc32(const can_protocol_record_t *record)
{
    uint8_t header[8];
    uint32_t crc;
    header[0] = record->type;
    header[1] = 1U;
    can_protocol_write_u16(header + 2U, record->sequence);
    can_protocol_write_u16(header + 4U, record->length);
    can_protocol_write_u16(header + 6U, record->token);
    /* 起始帧参与校验，避免类型、序号或会话头损坏后仍被接受。 */
    crc = crc32_update(0xFFFFFFFFUL, header, sizeof(header));
    return crc32_update(crc, record->payload, record->length) ^ 0xFFFFFFFFUL;
}

bool can_protocol_sequence_newer(uint16_t value, uint16_t previous)
{
    uint16_t delta = (uint16_t)(value - previous);
    return delta != 0U && delta < 0x8000U;
}

bool can_protocol_upper_id(uint16_t id)
{
    switch (id)
    {
        case 0x080U: case 0x100U: case 0x102U: case 0x104U:
        case 0x105U: case 0x110U: case 0x111U: case 0x124U:
        case 0x310U: case 0x311U: case 0x312U:
            return true;
        default: return false;
    }
}

void can_protocol_reassembly_reset(can_protocol_reassembly_t *state)
{
    if (state != NULL) memset(state, 0, sizeof(*state));
}

can_protocol_result_t can_protocol_receive_record(
    can_protocol_reassembly_t *state, uint16_t id, const uint8_t data[8],
    uint16_t expected_token, uint32_t now_ms)
{
    uint16_t length, segment, count, bytes;
    uint64_t mask;
    if (state == NULL || data == NULL) return CAN_PROTOCOL_INVALID;
    if (id < 0x310U || id > 0x312U) return CAN_PROTOCOL_IGNORED;
    if (state->active &&
        (uint32_t)(now_ms - state->started_ms) > CAN_PROTOCOL_RECORD_TIMEOUT_MS)
    {
        state->active = false;
        if (id != 0x310U) return CAN_PROTOCOL_EXPIRED;
    }
    if (id == 0x310U)
    {
        length = can_protocol_read_u16(data + 4U);
        if (data[1] != 1U || length == 0U ||
            length > CAN_PROTOCOL_RECORD_MAX || expected_token == 0U ||
            expected_token == 0xFFFFU ||
            can_protocol_read_u16(data + 6U) != expected_token)
            return CAN_PROTOCOL_INVALID;
        /* 重复起始帧不能延长期限；异内容不得拼入已有记录。 */
        if (state->active)
        {
            if (state->record.sequence == can_protocol_read_u16(data + 2U) &&
                state->record.token == expected_token &&
                state->record.type == data[0] && state->record.length == length)
                return CAN_PROTOCOL_PROGRESS;
            if (state->record.sequence == can_protocol_read_u16(data + 2U) ||
                !can_protocol_sequence_newer(can_protocol_read_u16(data + 2U),
                                            state->record.sequence))
            {
                state->active = false;
                return CAN_PROTOCOL_INVALID;
            }
        }
        memset(state, 0, sizeof(*state));
        state->record.type = data[0];
        state->record.sequence = can_protocol_read_u16(data + 2U);
        state->record.token = expected_token;
        state->record.length = length;
        state->started_ms = now_ms;
        state->active = true;
        return CAN_PROTOCOL_PROGRESS;
    }
    if (!state->active || state->record.token != expected_token ||
        can_protocol_read_u16(data) != state->record.sequence)
        return CAN_PROTOCOL_INVALID;
    count = (uint16_t)((state->record.length + 3U) / 4U);
    if (id == 0x311U)
    {
        segment = can_protocol_read_u16(data + 2U);
        if (segment >= count)
        {
            state->active = false;
            return CAN_PROTOCOL_INVALID;
        }
        bytes = (uint16_t)(state->record.length - segment * 4U);
        if (bytes > 4U) bytes = 4U;
        mask = (uint64_t)1U << segment;
        if ((state->received & mask) != 0U)
        {
            if (memcmp(state->record.payload + segment * 4U, data + 4U, bytes) != 0)
            {
                state->active = false;
                return CAN_PROTOCOL_INVALID;
            }
        }
        else
        {
            memcpy(state->record.payload + segment * 4U, data + 4U, bytes);
            state->received |= mask;
        }
        return CAN_PROTOCOL_PROGRESS;
    }
    mask = count == 64U ? UINT64_MAX : (((uint64_t)1U << count) - 1U);
    state->active = false;
    if (can_protocol_read_u16(data + 2U) != state->record.length ||
        state->received != mask ||
        can_protocol_read_u32(data + 4U) !=
            can_protocol_record_crc32(&state->record))
        return CAN_PROTOCOL_INVALID;
    state->record.received_start_ms = state->started_ms;
    state->record.received_commit_ms = now_ms;
    return CAN_PROTOCOL_COMPLETE;
}

bool can_protocol_encode_record_frame(const can_protocol_record_t *record,
    uint16_t index, uint16_t *id, uint8_t data[8])
{
    uint16_t count, bytes;
    if (record == NULL || id == NULL || data == NULL ||
        record->length == 0U || record->length > CAN_PROTOCOL_RECORD_MAX)
        return false;
    count = (uint16_t)((record->length + 3U) / 4U);
    if (index > count + 1U) return false;
    memset(data, 0, 8U);
    if (index == 0U)
    {
        *id = 0x300U;
        data[0] = record->type;
        data[1] = 1U;
        can_protocol_write_u16(data + 2U, record->sequence);
        can_protocol_write_u16(data + 4U, record->length);
        can_protocol_write_u16(data + 6U, record->token);
    }
    else if (index == count + 1U)
    {
        *id = 0x302U;
        can_protocol_write_u16(data, record->sequence);
        can_protocol_write_u16(data + 2U, record->length);
        can_protocol_write_u32(data + 4U,
            can_protocol_record_crc32(record));
    }
    else
    {
        *id = 0x301U;
        can_protocol_write_u16(data, record->sequence);
        can_protocol_write_u16(data + 2U, (uint16_t)(index - 1U));
        bytes = (uint16_t)(record->length - (index - 1U) * 4U);
        if (bytes > 4U) bytes = 4U;
        memcpy(data + 4U, record->payload + (index - 1U) * 4U, bytes);
    }
    return true;
}
