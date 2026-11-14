#include "can_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_record(uint16_t length)
{
    can_protocol_record_t record;
    can_protocol_reassembly_t rx;
    uint8_t data[8];
    uint16_t id, i, count;
    memset(&record, 0, sizeof(record));
    record.type = 0x23U;
    record.sequence = 65535U;
    record.token = 42U;
    record.length = length;
    for (i = 0U; i < length; ++i) record.payload[i] = (uint8_t)i;
    can_protocol_reassembly_reset(&rx);
    count = (uint16_t)((length + 3U) / 4U);
    for (i = 0U; i < count + 2U; ++i)
    {
        assert(can_protocol_encode_record_frame(&record, i, &id, data));
        assert(can_protocol_receive_record(&rx, (uint16_t)(id + 0x10U), data,
            42U, 0xFFFFFFF0UL + i) ==
            (i == count + 1U ? CAN_PROTOCOL_COMPLETE : CAN_PROTOCOL_PROGRESS));
    }
    assert(memcmp(rx.record.payload, record.payload, length) == 0);
    assert(!can_protocol_encode_record_frame(&record, count + 2U, &id, data));
}

static void test_invalid(void)
{
    can_protocol_record_t record;
    can_protocol_reassembly_t rx;
    uint8_t data[8], start[8];
    uint16_t id;
    memset(&record, 0, sizeof(record));
    record.type = 0x23U;
    record.sequence = 1U;
    record.token = 42U;
    record.length = 8U;
    can_protocol_reassembly_reset(&rx);
    assert(can_protocol_encode_record_frame(&record, 0U, &id, start));
    assert(can_protocol_receive_record(&rx, 0x310U, start, 43U, 0U) == CAN_PROTOCOL_INVALID);
    assert(can_protocol_receive_record(&rx, 0x310U, start, 42U, 0U) == CAN_PROTOCOL_PROGRESS);
    assert(can_protocol_receive_record(&rx, 0x310U, start, 42U, 100U) == CAN_PROTOCOL_PROGRESS);
    assert(rx.started_ms == 0U);
    assert(can_protocol_encode_record_frame(&record, 3U, &id, data));
    assert(can_protocol_receive_record(&rx, 0x312U, data, 42U, 150U) == CAN_PROTOCOL_INVALID);
    assert(can_protocol_receive_record(&rx, 0x310U, start, 42U, 0U) == CAN_PROTOCOL_PROGRESS);
    assert(can_protocol_encode_record_frame(&record, 1U, &id, data));
    assert(can_protocol_receive_record(&rx, 0x311U, data, 42U, 200U) == CAN_PROTOCOL_PROGRESS);
    assert(can_protocol_receive_record(&rx, 0x311U, data, 42U, 201U) == CAN_PROTOCOL_EXPIRED);
    assert(can_protocol_receive_record(&rx, 0x310U, start, 42U, 0U) == CAN_PROTOCOL_PROGRESS);
    assert(can_protocol_receive_record(&rx, 0x311U, data, 42U, 1U) == CAN_PROTOCOL_PROGRESS);
    assert(can_protocol_receive_record(&rx, 0x311U, data, 42U, 2U) == CAN_PROTOCOL_PROGRESS);
    data[4] ^= 1U;
    assert(can_protocol_receive_record(&rx, 0x311U, data, 42U, 3U) == CAN_PROTOCOL_INVALID);
}

int main(void)
{
    uint8_t data[8];
    assert(can_protocol_crc32((const uint8_t *)"123456789", 9U) == 0xCBF43926UL);
    can_protocol_write_u32(data, 0x12345678UL);
    assert(data[0] == 0x78U && data[3] == 0x12U);
    assert(can_protocol_read_u32(data) == 0x12345678UL);
    can_protocol_write_u16(data, 0xABCDU);
    assert(can_protocol_read_u16(data) == 0xABCDU);
    assert(can_protocol_sequence_newer(0U, 65535U));
    assert(!can_protocol_sequence_newer(1U, 1U));
    assert(!can_protocol_sequence_newer(0x8000U, 0U));
    assert(!can_protocol_sequence_newer(65535U, 0U));
    assert(can_protocol_upper_id(0x080U));
    assert(!can_protocol_upper_id(0x180U));
    test_record(1U);
    test_record(32U);
    test_record(104U);
    test_record(255U);
    test_record(256U);
    test_invalid();
    puts("PASS CAN protocol encoding, CRC, reassembly and rollover");
    return 0;
}
