#include "can_arm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void summary_record(can_protocol_record_t *record, uint8_t arm,
                           uint32_t config, uint32_t age, uint8_t state,
                           uint16_t flags)
{
    memset(record, 0, sizeof(*record));
    record->type = 0x47U;
    record->length = 32U;
    record->payload[0] = arm;
    record->payload[1] = state;
    can_protocol_write_u16(record->payload + 2U, flags);
    can_protocol_write_u32(record->payload + 4U, 1U);
    can_protocol_write_u32(record->payload + 8U, 2U);
    can_protocol_write_u32(record->payload + 12U, age);
    can_protocol_write_u32(record->payload + 16U, 0U);
    can_protocol_write_u32(record->payload + 20U, config);
    can_protocol_write_u32(record->payload + 24U, 1U);
    can_protocol_write_u16(record->payload + 28U, 65535U);
    can_protocol_write_u16(record->payload + 30U, 6U);
}

static void joint_record(can_protocol_record_t *record, uint8_t arm,
                         uint8_t joint, uint32_t config, uint32_t age)
{
    memset(record, 0, sizeof(*record));
    record->type = 0x48U;
    record->length = 32U;
    record->payload[0] = arm;
    record->payload[1] = joint;
    record->payload[2] = 6U;
    record->payload[3] = 1U;
    can_protocol_write_u32(record->payload + 4U, 100U);
    can_protocol_write_u32(record->payload + 8U, 200U);
    can_protocol_write_u32(record->payload + 12U, 3U);
    can_protocol_write_u32(record->payload + 16U, 4U);
    can_protocol_write_u32(record->payload + 20U, 0U);
    can_protocol_write_u32(record->payload + 24U, age);
    can_protocol_write_u32(record->payload + 28U, config);
}

static void result_record(can_protocol_record_t *record)
{
    memset(record, 0, sizeof(*record));
    record->type = 0x55U;
    record->length = 32U;
    can_protocol_write_u32(record->payload, 5U);
    can_protocol_write_u32(record->payload + 4U, 1U);
    record->payload[8] = 3U;
    record->payload[9] = 2U;
    record->payload[10] = 1U;
    record->payload[11] = 3U;
    can_protocol_write_u32(record->payload + 12U, 0x1234U);
    can_protocol_write_u16(record->payload + 16U, 100U);
    can_protocol_write_u32(record->payload + 20U, 0U);
    can_protocol_write_u32(record->payload + 24U, 1U);
    can_protocol_write_u32(record->payload + 28U, 7U);
}

int main(void)
{
    can_arm_cache_t cache;
    can_protocol_record_t record;
    can_arm_summary_t summary;
    can_arm_joint_t joint;
    can_arm_result_t result;
    can_arm_stats_t stats;
    uint8_t i;

    can_arm_init(&cache);
    summary_record(&record, 1U, 10U, 50U, 1U, 3U);
    assert(can_arm_publish(&cache, &record, 1000U));
    assert(can_arm_get_summary(&cache, 1U, 1400U, &summary));
    assert(summary.valid && summary.sample_age_ms == 450U);
    assert(can_arm_get_summary(&cache, 1U, 2000U, &summary));
    assert(!summary.valid);

    joint_record(&record, 1U, 2U, 10U, 20U);
    assert(can_arm_publish(&cache, &record, 100U));
    assert(can_arm_get_joint(&cache, 1U, 2U, 500U, &joint));
    assert(joint.valid && joint.actual_angle == 100);
    summary_record(&record, 1U, 11U, 0U, 1U, 3U);
    assert(can_arm_publish(&cache, &record, 600U));
    assert(!can_arm_get_joint(&cache, 1U, 2U, 600U, &joint));

    for (i = 0U; i < CAN_ARM_JOINT_CACHE_COUNT; ++i)
    {
        joint_record(&record, (uint8_t)(i < 16U ? 1U : 2U),
            (uint8_t)(i % 16U), 12U, 0U);
        assert(can_arm_publish(&cache, &record, (uint32_t)(1000U + i)));
    }
    joint_record(&record, 1U, 31U, 12U, 0U);
    assert(can_arm_publish(&cache, &record, 2000U));
    can_arm_get_stats(&cache, &stats);
    assert(stats.joint_replaced == 1U);

    result_record(&record);
    assert(can_arm_publish(&cache, &record, 3000U));
    assert(can_arm_get_result(&cache, 3500U, &result));
    assert(result.valid && result.result == 1U);

    summary_record(&record, 3U, 10U, 0U, 1U, 3U);
    assert(!can_arm_publish(&cache, &record, 4000U));
    puts("PASS CAN arm summary, joint cache, result, config invalidation and age");
    return 0;
}
