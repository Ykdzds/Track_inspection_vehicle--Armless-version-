#include "can_upper_protocol.h"
#include <string.h>

static const uint8_t record_types[UPPER_RECORD_COUNT] = {0x02,0x10,0x11,0x12,0x13,0x20,0x21,0x22,0x23,0x24,0x43,0x46,0x50,0x52,0x53,0x01,0x25,0x26};

uint16_t Upper_ReadU16(const uint8_t *data) { return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8)); }
uint32_t Upper_ReadU32(const uint8_t *data) { return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24); }
uint64_t Upper_ReadU64(const uint8_t *data) { return (uint64_t)Upper_ReadU32(data) | ((uint64_t)Upper_ReadU32(data + 4) << 32); }
static void Write16(uint8_t *data, uint16_t value) { data[0] = (uint8_t)value; data[1] = (uint8_t)(value >> 8); }
static void Write32(uint8_t *data, uint32_t value) { Write16(data, (uint16_t)value); Write16(data + 2, (uint16_t)(value >> 16)); }
static uint32_t CrcUpdate(uint32_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t index, bit;
    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc;
}
uint32_t Upper_Crc32(const uint8_t *data, uint32_t length) { return CrcUpdate(0xFFFFFFFFUL, data, length) ^ 0xFFFFFFFFUL; }
static uint32_t RecordCrc(const uint8_t *header, const uint8_t *payload, uint16_t length)
{
    return CrcUpdate(CrcUpdate(0xFFFFFFFFUL, header, 8U), payload, length) ^ 0xFFFFFFFFUL;
}
static void Event(UpperProtocol_t *protocol, UpperEvent_t event, uint32_t detail)
{
    if (protocol->event != 0) protocol->event(protocol->user, event, detail);
}
static void SetState(UpperProtocol_t *protocol, UpperLinkState_t state)
{
    if (protocol->snapshot.state != state)
    {
        protocol->snapshot.state = state;
        Event(protocol, UPPER_EVENT_LINK, (uint32_t)state);
    }
}
static void Reject(UpperProtocol_t *protocol, uint32_t reason)
{
    protocol->snapshot.rejected++;
    Event(protocol, UPPER_EVENT_REJECT, reason);
}
static void RecordError(UpperProtocol_t *protocol, uint32_t reason)
{
    protocol->active_record = 0U;
    protocol->snapshot.record_errors++;
    Event(protocol, UPPER_EVENT_REJECT, reason);
}
static uint16_t RecordLength(uint8_t type)
{
    switch (type)
    {
        case 0x01: case 0x52: return 24U;
        case 0x02: case 0x23: case 0x27: case 0x46: case 0x50: case 0x53: return 32U;
        case 0x10: return 80U;
        case 0x11: case 0x12: case 0x13: return 40U;
        case 0x20: case 0x21: return 56U;
        case 0x22: case 0x25: return 104U;
        case 0x26: return 64U;
        case 0x24: case 0x43: return 48U;
        default: return 0U;
    }
}
static uint8_t EnumValid(uint8_t value, uint8_t maximum) { return (uint8_t)(value <= maximum || value == 0xFFU); }
static uint8_t Send(UpperProtocol_t *protocol, uint16_t id, const uint8_t *data)
{
    return (uint8_t)(protocol->send != 0 && protocol->send(protocol->user, id, data));
}

void Upper_Init(UpperProtocol_t *protocol, uint32_t boot_id, UpperSend_t send, UpperEventCallback_t event, void *user)
{
    memset(protocol, 0, sizeof(*protocol));
    protocol->send = send;
    protocol->event = event;
    protocol->user = user;
    protocol->snapshot.upper_boot_id = boot_id;
    protocol->snapshot.state = boot_id ? UPPER_WAIT_BOOT : UPPER_BOOT_ERROR;
    protocol->snapshot.actual_mm_s = protocol->snapshot.target_mm_s = (int16_t)-32768;
    protocol->snapshot.left_rpm = protocol->snapshot.right_rpm = (int16_t)-32768;
    protocol->snapshot.fault = protocol->snapshot.left_fault = protocol->snapshot.right_fault = 0xFFFFU;
}

void Upper_ResetSession(UpperProtocol_t *protocol, uint32_t now)
{
    uint32_t index;
    uint16_t old_token = protocol->snapshot.link_token ? protocol->snapshot.link_token : protocol->snapshot.candidate_token;
    if (old_token > protocol->retired_link_token) protocol->retired_link_token = old_token;
    protocol->snapshot.link_token = 0U;
    protocol->snapshot.candidate_token = 0U;
    protocol->snapshot.heartbeat_seen = 0U;
    protocol->snapshot.heartbeat_streak = 0U;
    protocol->snapshot.axes = 0U;
    protocol->snapshot.control_source = protocol->snapshot.lifecycle = 0xFFU;
    protocol->snapshot.gates = 0U;
    protocol->snapshot.lower_capabilities = protocol->snapshot.config_revision = 0U;
    protocol->snapshot.snapshot_valid = 0U;
    protocol->snapshot.actual_mm_s = protocol->snapshot.target_mm_s = (int16_t)-32768;
    protocol->snapshot.left_rpm = protocol->snapshot.right_rpm = (int16_t)-32768;
    protocol->snapshot.fault = protocol->snapshot.left_fault = protocol->snapshot.right_fault = 0xFFFFU;
    for (index = 0U; index < 9U; index++) protocol->snapshot.basic[index].valid = 0U;
    for (index = 0U; index < UPPER_RECORD_COUNT; index++) protocol->snapshot.records[index].valid = 0U;
    protocol->active_record = protocol->record_sequence_seen = 0U;
    protocol->capability_active = protocol->capability_waiting = 0U;
    protocol->capability_published = 0U;
    protocol->capability_confirmed = 0U;
    protocol->capability_attempts = 0U;
    protocol->capability_retry_at = now;
    protocol->handshake_sent = 0U;
    SetState(protocol, protocol->snapshot.upper_boot_id ? UPPER_WAIT_BOOT : UPPER_BOOT_ERROR);
}

static void BeginCapabilities(UpperProtocol_t *protocol, uint32_t now)
{
    uint8_t *payload = protocol->capability_payload;
    memset(payload, 0, 32U);
    Write32(payload, protocol->snapshot.upper_boot_id);
    Write32(payload + 4, protocol->snapshot.lower_boot_id);
    Write32(payload + 8, UPPER_CAPABILITIES);
    payload[20] = 1U;
    payload[22] = 1U;
    ++protocol->capability_attempts;
    protocol->capability_sequence = protocol->upper_record_sequence++;
    memset(protocol->capability_header, 0, 8U);
    protocol->capability_header[0] = 0x02U;
    protocol->capability_header[1] = 1U;
    Write16(protocol->capability_header + 2, protocol->capability_sequence);
    Write16(protocol->capability_header + 4, 32U);
    Write16(protocol->capability_header + 6, protocol->snapshot.link_token);
    protocol->capability_active = 1U;
    protocol->capability_step = protocol->capability_waiting = 0U;
    protocol->capability_at = now;
}

void Upper_RetryCapabilities(UpperProtocol_t *protocol, uint32_t now)
{
    if (!protocol->capability_active)
    {
        protocol->capability_attempts = 0U;
        protocol->capability_retry_at = now;
    }
}

static void PublishRecord(UpperProtocol_t *protocol, uint32_t now)
{
    uint8_t type = protocol->record_header[0];
    uint8_t *payload = protocol->record_payload;
    uint16_t sequence = Upper_ReadU16(protocol->record_header + 2);
    uint16_t token;
    uint32_t index;
    UpperRecord_t *record;
    if (type == 0x01U)
    {
        token = Upper_ReadU16(payload + 8);
        if (Upper_ReadU32(payload) != protocol->snapshot.lower_boot_id ||
            Upper_ReadU32(payload + 4) != protocol->snapshot.upper_boot_id ||
            payload[10] != 1U || payload[11] != 0U || token == 0U || token == 0xFFFFU || token <= protocol->retired_link_token)
        {
            RecordError(protocol, 20U);
            return;
        }
        if (protocol->snapshot.link_token != 0U)
        {
            if (token != protocol->snapshot.link_token || sequence != protocol->snapshot.invite_sequence) Reject(protocol, 21U);
            else
            {
                uint8_t confirmation[8] = {0};
                Write16(confirmation, token);
                Write16(confirmation + 2, sequence);
                Write32(confirmation + 4, protocol->snapshot.upper_boot_id);
                Send(protocol, 0x105U, confirmation);
            }
            return;
        }
        if (protocol->snapshot.state != UPPER_WAIT_INVITE && protocol->snapshot.state != UPPER_WAIT_CONFIRM)
        {
            Reject(protocol, 22U);
            return;
        }
        if (protocol->snapshot.candidate_token != 0U &&
            (protocol->snapshot.candidate_token != token || protocol->snapshot.invite_sequence != sequence))
        {
            Reject(protocol, 23U);
            return;
        }
        protocol->snapshot.candidate_token = token;
        protocol->snapshot.invite_sequence = sequence;
        protocol->snapshot.lower_capabilities = Upper_ReadU32(payload + 12);
        protocol->snapshot.config_revision = Upper_ReadU32(payload + 16);
        protocol->handshake_sent = 0U;
        SetState(protocol, UPPER_WAIT_CONFIRM);
        return;
    }
    if (type == 0x02U && (Upper_ReadU32(payload) != protocol->snapshot.lower_boot_id ||
        Upper_ReadU32(payload + 4) != protocol->snapshot.upper_boot_id || payload[20] != 1U || payload[22] != 0U))
    {
        RecordError(protocol, 24U);
        return;
    }
    /* The lower controller also publishes the existing recovery diagnostic
     * record. Validate its CRC/sequence without allocating another UI cache. */
    if (type == 0x27U)
    {
        protocol->last_record_sequence = sequence;
        protocol->record_sequence_seen = 1U;
        return;
    }
    for (index = 0U; index < UPPER_RECORD_COUNT; index++)
    {
        if (record_types[index] != type) continue;
        record = &protocol->snapshot.records[index];
        record->type = type;
        record->length = Upper_ReadU16(protocol->record_header + 4);
        record->sequence = sequence;
        record->timestamp = type == 0x25U || type == 0x26U ? protocol->record_started_at : now;
        memcpy(record->payload, payload, record->length);
        record->valid = 1U;
        if (type == 0x02U) protocol->snapshot.lower_capabilities = Upper_ReadU32(payload + 8);
        if (type == 0x25U)
        {
            protocol->snapshot.snapshot_sequence = Upper_ReadU32(payload);
            protocol->snapshot.snapshot_ms = Upper_ReadU32(payload + 4U);
            protocol->snapshot.snapshot_received_ms = now;
            protocol->snapshot.snapshot_source_age = Upper_ReadU32(payload + 100U);
            protocol->snapshot.snapshot_valid = (uint8_t)(Upper_ReadU32(payload + 8U) ==
                protocol->snapshot.lower_boot_id && (Upper_ReadU32(payload + 12U) & 1U) != 0U &&
                protocol->snapshot.snapshot_sequence != 0U);
        }
        protocol->snapshot.records_received++;
        protocol->last_record_sequence = sequence;
        protocol->record_sequence_seen = 1U;
        Event(protocol, UPPER_EVENT_RECORD, type);
        break;
    }
}

static void ReceiveRecord(UpperProtocol_t *protocol, uint16_t id, const uint8_t *data, uint32_t received_at, uint32_t now)
{
    uint16_t sequence, length, segment, index, count, advance;
    if (id == 0x300U)
    {
        length = Upper_ReadU16(data + 4);
        sequence = Upper_ReadU16(data + 2);
        if (data[1] != 1U || length == 0U || length > 256U || RecordLength(data[0]) != length)
        {
            RecordError(protocol, 30U);
            return;
        }
        if (data[0] == 0x01U)
        {
            if (Upper_ReadU16(data + 6) != 0U || protocol->snapshot.lower_boot_id == 0U)
            { RecordError(protocol, 31U); return; }
        }
        else
        {
            if (protocol->snapshot.link_token == 0U || Upper_ReadU16(data + 6) != protocol->snapshot.link_token)
            { RecordError(protocol, 32U); return; }
            if (protocol->record_sequence_seen && (uint16_t)(sequence - protocol->last_record_sequence) - 1U >= 32767U)
            { RecordError(protocol, 33U); return; }
        }
        if (protocol->active_record && memcmp(protocol->record_header, data, 8U) == 0) return;
        if (protocol->active_record && Upper_ReadU16(protocol->record_header + 2) == sequence)
        { RecordError(protocol, 34U); return; }
        if (protocol->active_record)
        {
            advance = (uint16_t)(sequence - Upper_ReadU16(protocol->record_header + 2));
            if (advance == 0U || advance >= 32768U) { Reject(protocol, 43U); return; }
        }
        memcpy(protocol->record_header, data, 8U);
        memset(protocol->segment_seen, 0, sizeof(protocol->segment_seen));
        protocol->record_started_at = received_at;
        protocol->active_record = 1U;
        return;
    }
    if (!protocol->active_record) { RecordError(protocol, 35U); return; }
    if ((uint32_t)(now - protocol->record_started_at) > 200U) { RecordError(protocol, 36U); return; }
    sequence = Upper_ReadU16(protocol->record_header + 2);
    length = Upper_ReadU16(protocol->record_header + 4);
    if (Upper_ReadU16(data) != sequence) { RecordError(protocol, 37U); return; }
    if (id == 0x301U)
    {
        segment = Upper_ReadU16(data + 2);
        if (segment >= (length + 3U) / 4U) { RecordError(protocol, 38U); return; }
        count = (uint16_t)((length - segment * 4U >= 4U) ? 4U : length - segment * 4U);
        if (protocol->segment_seen[segment] && memcmp(protocol->record_payload + segment * 4U, data + 4, count) != 0)
        { RecordError(protocol, 39U); return; }
        memcpy(protocol->record_payload + segment * 4U, data + 4, count);
        protocol->segment_seen[segment] = 1U;
        return;
    }
    if (Upper_ReadU16(data + 2) != length) { RecordError(protocol, 40U); return; }
    for (index = 0U; index < (length + 3U) / 4U; index++)
        if (!protocol->segment_seen[index]) { RecordError(protocol, 41U); return; }
    if (RecordCrc(protocol->record_header, protocol->record_payload, length) != Upper_ReadU32(data + 4))
    { RecordError(protocol, 42U); return; }
    PublishRecord(protocol, now);
    protocol->active_record = 0U;
}

void Upper_Receive(UpperProtocol_t *protocol, uint16_t id, uint8_t dlc, const uint8_t data[8], uint32_t received_at, uint32_t now)
{
    UpperSnapshot_t *snapshot = &protocol->snapshot;
    uint32_t boot;
    uint8_t index, delta;
    uint16_t token;
    if (snapshot->upper_boot_id == 0U) return;
    if (dlc != 8U || data == 0 || (uint32_t)(now - received_at) > 200U) { Reject(protocol, 1U); return; }
    if (snapshot->link_token && (uint32_t)(now - (snapshot->heartbeat_seen ? snapshot->heartbeat_at : snapshot->confirm_at)) >= UPPER_LINK_TIMEOUT_MS)
    {
        Event(protocol, UPPER_EVENT_TIMEOUT, 0x185U);
        Upper_ResetSession(protocol, now);
    }
    snapshot->last_rx_at = received_at;
    snapshot->last_rx_id = id;
    if (id == 0x101U)
    {
        boot = Upper_ReadU32(data);
        if (boot == 0U || data[4] != 1U)
        {
            if (!snapshot->link_token) SetState(protocol, UPPER_VERSION_ERROR);
            Reject(protocol, 2U);
            return;
        }
        if (snapshot->lower_boot_id != boot)
        {
            Upper_ResetSession(protocol, now);
            snapshot->lower_boot_id = boot;
            Event(protocol, UPPER_EVENT_RESTART, boot);
        }
        if (snapshot->state == UPPER_WAIT_BOOT || snapshot->state == UPPER_VERSION_ERROR) SetState(protocol, UPPER_WAIT_INVITE);
        return;
    }
    if (id == 0x106U)
    {
        token = Upper_ReadU16(data);
        if (Upper_ReadU32(data + 4) != snapshot->lower_boot_id || data[2] != 0U ||
            snapshot->candidate_token == 0U || token != snapshot->candidate_token)
        { Reject(protocol, 3U); return; }
        if (snapshot->link_token == token) return;
        if (snapshot->state != UPPER_WAIT_CONFIRM) { Reject(protocol, 4U); return; }
        snapshot->link_token = token;
        snapshot->heartbeat_seen = snapshot->heartbeat_streak = 0U;
        snapshot->confirm_at = now;
        protocol->record_sequence_seen = 0U;
        SetState(protocol, UPPER_WAIT_HEARTBEAT);
        return;
    }
    if (id == 0x120U || id == 0x121U)
    {
        if (snapshot->link_token == 0U || Upper_ReadU16(data + 4U) != snapshot->link_token)
        { Reject(protocol, 6U); return; }
        if ((id == 0x120U && (data[2] > 12U || (data[7] & 240U))) ||
            (id == 0x121U && (data[2] > 6U || data[3] > 12U))) Reject(protocol, 7U);
        return;
    }
    if (id >= 0x300U && id <= 0x302U) { ReceiveRecord(protocol, id, data, received_at, now); return; }
    if (id < 0x180U || id > 0x188U) { Reject(protocol, 5U); return; }
    index = (uint8_t)(id - 0x180U);
    token = Upper_ReadU16(data + ((id == 0x185U || id == 0x188U) ? 4U : 6U));
    if (snapshot->link_token == 0U || token != snapshot->link_token) { Reject(protocol, 6U); return; }
    if ((id == 0x180U && !EnumValid(data[0], 6U)) ||
        (id == 0x181U && data[4] > 100U && data[4] != 0xFFU) ||
        (id == 0x183U && (!EnumValid(data[0], 1U) || (data[1] > 100U && data[1] != 0xFFU) ||
            !EnumValid(data[2], 6U) || !EnumValid(data[3], 6U) || !EnumValid(data[4], 3U))) ||
        ((id == 0x186U || id == 0x187U) && !EnumValid(data[1], 3U)) ||
        (id == 0x188U && (!EnumValid(data[0], 7U) || !EnumValid(data[1], 3U) || !EnumValid(data[2], 5U))))
    { Reject(protocol, 7U); return; }
    if (id == 0x185U)
    {
        if (data[0] != 1U || !EnumValid(data[2], 5U) || !EnumValid(data[3], 7U) || data[7] > 3U)
        { Reject(protocol, 8U); return; }
        delta = (uint8_t)(data[1] - snapshot->heartbeat_sequence);
        if (snapshot->heartbeat_seen && (delta == 0U || delta >= 128U)) { Reject(protocol, 9U); return; }
        snapshot->heartbeat_sequence = data[1];
        snapshot->heartbeat_seen = 1U;
        snapshot->heartbeat_at = received_at;
        snapshot->axes = data[7];
        snapshot->control_source = data[2];
        snapshot->lifecycle = data[3];
        snapshot->gates = data[6];
        if (data[2] != 0xFFU && data[3] != 0xFFU)
        {
            if (snapshot->heartbeat_streak < 3U) snapshot->heartbeat_streak++;
            if (snapshot->heartbeat_streak >= 3U) SetState(protocol, UPPER_READY);
        }
        else
        {
            snapshot->heartbeat_streak = 0U;
            SetState(protocol, UPPER_WAIT_HEARTBEAT);
        }
    }
    if (id == 0x184U) { Reject(protocol, 10U); return; }
    memcpy(snapshot->basic[index].data, data, 8U);
    snapshot->basic[index].timestamp = received_at;
    snapshot->basic[index].valid = 1U;
    if (id == 0x180U)
    {
        snapshot->actual_mm_s = (data[1] & 1U) ? (int16_t)Upper_ReadU16(data + 2) : (int16_t)-32768;
        snapshot->target_mm_s = (data[1] & 2U) ? (int16_t)Upper_ReadU16(data + 4) : (int16_t)-32768;
    }
    if (id == 0x182U) snapshot->fault = Upper_ReadU16(data);
    if (id == 0x186U || id == 0x187U)
    {
        int16_t rpm = (data[0] & 5U) == 5U ? (int16_t)Upper_ReadU16(data + 2) : (int16_t)-32768;
        uint16_t fault = (data[0] & 9U) == 9U ? Upper_ReadU16(data + 4) : 0xFFFFU;
        if (id == 0x186U) { snapshot->left_rpm = rpm; snapshot->left_fault = fault; }
        else { snapshot->right_rpm = rpm; snapshot->right_fault = fault; }
    }
}

void Upper_TxResult(UpperProtocol_t *protocol, uint16_t id, const uint8_t data[8], uint8_t success, uint32_t now)
{
    if (!protocol->capability_active || !protocol->capability_waiting || id < 0x310U || id > 0x312U) return;
    if ((id == 0x310U && memcmp(data, protocol->capability_header, 8U) != 0) ||
        (id != 0x310U && Upper_ReadU16(data) != protocol->capability_sequence)) return;
    protocol->capability_waiting = 0U;
    if (!success)
    {
        protocol->capability_active = 0U;
        protocol->capability_retry_at = now + 500U;
        Event(protocol, UPPER_EVENT_TX_ERROR, id);
    }
    else if (++protocol->capability_step > 9U)
    {
        protocol->capability_active = 0U;
        protocol->capability_published = 1U;
        protocol->capability_retry_at = now + 1000U;
    }
}

void Upper_Tick(UpperProtocol_t *protocol, uint32_t now, uint8_t healthy)
{
    uint8_t data[8] = {0};
    uint8_t step;
    UpperSnapshot_t *snapshot = &protocol->snapshot;
    if (!snapshot->upper_boot_id) return;
    if (snapshot->link_token != 0U && (uint32_t)(now - (snapshot->heartbeat_seen ? snapshot->heartbeat_at : snapshot->confirm_at)) >= UPPER_LINK_TIMEOUT_MS)
    {
        Event(protocol, UPPER_EVENT_TIMEOUT, 0x185U);
        Upper_ResetSession(protocol, now);
    }
    if (protocol->active_record && (uint32_t)(now - protocol->record_started_at) > 200U) RecordError(protocol, 36U);
    if (!protocol->boot_sent || (uint32_t)(now - protocol->boot_at) >= 500U)
    {
        Write32(data, snapshot->upper_boot_id);
        data[4] = 1U;
        data[5] = 0U;
        Write16(data + 6, 3U);
        if (Send(protocol, 0x102U, data)) { protocol->boot_sent = 1U; protocol->boot_at = now; }
    }
    if (!protocol->heartbeat_sent || (uint32_t)(now - protocol->heartbeat_tx_at) >= 100U)
    {
        memset(data, 0, 8U);
        data[0] = 1U;
        data[1] = snapshot->tx_heartbeat_sequence;
        data[2] = healthy ? 1U : 0x40U;
        data[3] = 0xFFU;
        Write16(data + 4, snapshot->link_token);
        if (Send(protocol, 0x100U, data))
        {
            snapshot->tx_heartbeat_sequence++;
            protocol->heartbeat_sent = 1U;
            protocol->heartbeat_tx_at = now;
        }
    }
    if ((snapshot->state == UPPER_WAIT_INVITE || snapshot->state == UPPER_WAIT_CONFIRM) &&
        (!protocol->handshake_sent || (uint32_t)(now - protocol->handshake_at) >= 500U))
    {
        memset(data, 0, 8U);
        if (snapshot->state == UPPER_WAIT_INVITE)
        {
            Write32(data, snapshot->lower_boot_id);
            Write32(data + 4, snapshot->upper_boot_id);
            if (Send(protocol, 0x104U, data)) { protocol->handshake_sent = 1U; protocol->handshake_at = now; }
        }
        else
        {
            Write16(data, snapshot->candidate_token);
            Write16(data + 2, snapshot->invite_sequence);
            Write32(data + 4, snapshot->upper_boot_id);
            if (Send(protocol, 0x105U, data)) { protocol->handshake_sent = 1U; protocol->handshake_at = now; }
        }
    }
    if (snapshot->link_token == 0U) return;
    protocol->capability_confirmed = (uint8_t)(snapshot->state == UPPER_READY &&
        snapshot->records[2].valid && snapshot->records[2].type == 0x11U && snapshot->records[2].length == 40U &&
        Upper_RecordFresh(snapshot, &snapshot->records[2], now) &&
        (uint32_t)(now - snapshot->records[2].timestamp) < UPPER_CONTROL_MAX_AGE_MS &&
        (Upper_ReadU32(snapshot->records[2].payload + 12U) & 1024U) != 0U);
    if (protocol->capability_active && (uint32_t)(now - protocol->capability_at) > 200U)
    {
        protocol->capability_active = protocol->capability_waiting = 0U;
        protocol->capability_retry_at = now + 500U;
        Event(protocol, UPPER_EVENT_TX_ERROR, 0x310U);
    }
    if (!protocol->capability_active && (!protocol->capability_published || !protocol->capability_confirmed) && protocol->capability_attempts < 3U && snapshot->state == UPPER_READY && snapshot->heartbeat_seen && protocol->snapshot.records[0].valid && (int32_t)(now - protocol->capability_retry_at) >= 0) BeginCapabilities(protocol, now);
    if (!protocol->capability_active || protocol->capability_waiting) return;
    step = protocol->capability_step;
    memset(data, 0, 8U);
    if (step == 0U) memcpy(data, protocol->capability_header, 8U);
    else
    {
        Write16(data, protocol->capability_sequence);
        if (step <= 8U)
        {
            Write16(data + 2, step - 1U);
            memcpy(data + 4, protocol->capability_payload + (step - 1U) * 4U, 4U);
        }
        else
        {
            Write16(data + 2, 32U);
            Write32(data + 4, RecordCrc(protocol->capability_header, protocol->capability_payload, 32U));
        }
    }
    if (Send(protocol, step == 0U ? 0x310U : (step <= 8U ? 0x311U : 0x312U), data)) protocol->capability_waiting = 1U;
}

const char *Upper_StateName(UpperLinkState_t state)
{
    static const char *names[] = {"WAIT_BOOT","WAIT_INVITE","WAIT_CONFIRM","WAIT_HB","READY","VER_ERR","BOOT_ERR"};
    return state <= UPPER_BOOT_ERROR ? names[state] : "UNKNOWN";
}

uint8_t Upper_BasicFresh(const UpperSnapshot_t *snapshot, uint8_t index, uint32_t now)
{
    uint32_t timeout;
    if (index >= 9U || !snapshot->basic[index].valid || !snapshot->link_token) return 0U;
    timeout = (index == 1U || index == 3U) ? 1500U : (index == 2U ? 1500U : (index == 5U ? UPPER_LINK_TIMEOUT_MS : 300U));
    return (uint8_t)((uint32_t)(now - snapshot->basic[index].timestamp) < timeout);
}

uint8_t Upper_RecordFresh(const UpperSnapshot_t *snapshot, const UpperRecord_t *record, uint32_t now)
{
    uint32_t age, source_age = 0U, timeout = 1500U;
    if (!record->valid || !snapshot->link_token) return 0U;
    age = now - record->timestamp;
    if (record->type == 0x02U || record->type == 0x10U) return 1U;
    if (record->type == 0x20U || record->type == 0x21U) source_age = Upper_ReadU32(record->payload + 16);
    if (record->type == 0x22U) source_age = Upper_ReadU16(record->payload + 20);
    if (record->type == 0x11U) timeout = 1500U;
    if (record->type == 0x23U) timeout = 600U;
    if (record->type == 0x25U)
    {
        source_age = Upper_ReadU32(record->payload + 100U);
        timeout = 300U;
        if (!snapshot->snapshot_valid) return 0U;
    }
    if (record->type == 0x26U)
    {
        source_age = Upper_ReadU32(record->payload + 60U);
        timeout = 1000U;
        if (Upper_ReadU32(record->payload + 56U) != snapshot->lower_boot_id) return 0U;
    }
    return (uint8_t)(source_age < timeout && age < timeout - source_age);
}
