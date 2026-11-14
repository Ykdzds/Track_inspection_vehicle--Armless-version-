#include "can_upper.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    can_upper_cache_t cache;
    can_upper_snapshot_t out;
    can_protocol_record_t record;
    can_upper_init(&cache);
    memset(&record, 0, sizeof(record));
    record.type = 0x41U;
    record.length = 32U;
    record.sequence = 1U;
    /* 保留输入位应忽略，不能使未来兼容数据被整体拒绝。 */
    can_protocol_write_u16(record.payload + 4U, 0xF001U);
    record.payload[17] = 4U;
    record.payload[18] = 12U;
    can_protocol_write_u32(record.payload + 28U, 1U);
    assert(can_upper_publish(&cache, &record, 0xFFFFFF00U, 1U, 2U));
    assert(can_upper_get(&cache, record.type, 0xFFFFFF00U + 3000U, &out));
    assert(out.valid && out.age_ms == 3000U);
    assert(can_upper_get(&cache, record.type, 0xFFFFFF00U + 3001U, &out));
    assert(!out.valid);
    record.payload[18] = 11U;
    assert(!can_upper_publish(&cache, &record, 200U, 1U, 2U));
    assert(can_upper_get(&cache, record.type, 0xFFFFFF00U, &out));
    assert(out.sequence == 1U && out.received_ms == 0xFFFFFF00U);
    memset(&record, 0, sizeof(record));
    record.type = 0x43U;
    record.length = 48U;
    record.payload[0] = 1U;
    record.payload[3] = 1U;
    can_protocol_write_u32(record.payload + 4U, 900000001U);
    assert(!can_upper_publish(&cache, &record, 0U, 1U, 2U));
    can_protocol_write_u32(record.payload + 4U, 900000000U);
    can_protocol_write_u32(record.payload + 40U, 1490U);
    assert(can_upper_publish(&cache, &record, 100U, 1U, 2U));
    assert(can_upper_get(&cache, record.type, 110U, &out) && out.valid);
    assert(can_upper_get(&cache, record.type, 111U, &out) && !out.valid);
    can_protocol_write_u32(record.payload + 40U, UINT32_MAX);
    assert(can_upper_publish(&cache, &record, 100U, 1U, 2U));
    assert(can_upper_get(&cache, record.type, 101U, &out));
    assert(out.age_ms == UINT32_MAX && !out.valid);
    record.payload[0] = 2U;
    assert(!can_upper_publish(&cache, &record, 200U, 1U, 2U));
    can_upper_init(&cache);
    assert(!can_upper_get(&cache, 0x43U, 0U, &out));
    puts("PASS CAN upper cache reserved bits, atomic rejection, age, rollover and RK producer");
    return 0;
}
