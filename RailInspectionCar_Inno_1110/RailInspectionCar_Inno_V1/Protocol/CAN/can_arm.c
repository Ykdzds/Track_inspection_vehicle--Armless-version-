#include "can_arm.h"

#include <string.h>

#define CAN_ARM_SUMMARY_TIMEOUT_MS (1000U)
#define CAN_ARM_RESULT_TIMEOUT_MS  (3000U)

static bool valid_age(uint32_t source_age, uint32_t received_ms,
                     uint32_t now_ms, uint32_t timeout)
{
    uint32_t cache_age = now_ms - received_ms;
    return source_age != UINT32_MAX &&
        source_age <= timeout && cache_age <= timeout - source_age;
}

static bool valid_arm(uint8_t arm_id)
{
    return arm_id == 1U || arm_id == 2U;
}

static bool valid_mask(uint8_t mask)
{
    return mask >= 1U && mask <= 3U;
}

static bool valid_summary(const uint8_t *p)
{
    uint16_t flags = can_protocol_read_u16(p + 2U);
    uint32_t operation_id = can_protocol_read_u32(p + 4U);
    uint32_t safety_generation = can_protocol_read_u32(p + 24U);
    uint16_t last_result = can_protocol_read_u16(p + 28U);
    uint16_t joint_count = can_protocol_read_u16(p + 30U);
    return valid_arm(p[0]) && (p[1] <= 6U || p[1] == 255U) &&
        (flags & 0xFF00U) == 0U &&
        ((operation_id != 0U && safety_generation != 0U) || (p[1]==0U && operation_id==0U && safety_generation==0U)) &&
        (last_result <= 14U || last_result == 65535U) &&
        (joint_count <= 32U || joint_count == 65535U);
}

static bool valid_joint(const uint8_t *p)
{
    return valid_arm(p[0]) && p[1] < 32U && p[2] >= 1U && p[2] <= 32U &&
        (p[3] & 0xE0U) == 0U && can_protocol_read_u32(p + 28U) != 0U;
}

static bool valid_result(const uint8_t *p)
{
    uint16_t progress = can_protocol_read_u16(p + 16U);
    return can_protocol_read_u32(p) != 0U &&
        valid_mask(p[8]) &&
        p[9] <= 3U && p[10] <= 14U && (p[11] & 0xF8U) == 0U &&
        can_protocol_read_u16(p + 18U) == 0U &&
        (progress <= 100U || progress == 65535U);
}

void can_arm_init(can_arm_cache_t *cache)
{
    if (cache != NULL) memset(cache, 0, sizeof(*cache));
}

void can_arm_invalidate(can_arm_cache_t *cache)
{
    uint32_t stats[4];
    if (cache == NULL) return;
    stats[0] = cache->stats.invalid_records;
    stats[1] = cache->stats.joint_replaced;
    stats[2] = cache->stats.result_updates;
    stats[3] = cache->stats.config_changes;
    memset(cache, 0, sizeof(*cache));
    cache->stats.invalid_records = stats[0];
    cache->stats.joint_replaced = stats[1];
    cache->stats.result_updates = stats[2];
    cache->stats.config_changes = stats[3];
}

bool can_arm_publish(can_arm_cache_t *cache,
                     const can_protocol_record_t *record, uint32_t now_ms)
{
    const uint8_t *p;
    uint8_t index, i;
    if (cache == NULL || record == NULL) return false;
    p = record->payload;
    if (record->type == 0x47U)
    {
        if (record->length != 32U || !valid_summary(p))
        {
            ++cache->stats.invalid_records;
            return false;
        }
        index = (uint8_t)(p[0] - 1U);
        if (cache->summary[index].present &&
            cache->summary[index].config_version != can_protocol_read_u32(p + 20U))
        {
            for (i = 0U; i < CAN_ARM_JOINT_CACHE_COUNT; ++i)
                if (cache->joints[i].present &&
                    cache->joints[i].arm_id == p[0])
                    cache->joints[i].present = false;
            ++cache->stats.config_changes;
        }
        memset(&cache->summary[index], 0, sizeof(cache->summary[index]));
        cache->summary[index].arm_id = p[0];
        cache->summary[index].state = p[1];
        cache->summary[index].flags = can_protocol_read_u16(p + 2U);
        cache->summary[index].operation_id = can_protocol_read_u32(p + 4U);
        cache->summary[index].sample_sequence = can_protocol_read_u32(p + 8U);
        cache->summary[index].sample_age_ms = can_protocol_read_u32(p + 12U);
        cache->summary[index].fault_code = can_protocol_read_u32(p + 16U);
        cache->summary[index].config_version = can_protocol_read_u32(p + 20U);
        cache->summary[index].safety_generation = can_protocol_read_u32(p + 24U);
        cache->summary[index].last_result = can_protocol_read_u16(p + 28U);
        cache->summary[index].joint_count = can_protocol_read_u16(p + 30U);
        cache->summary[index].received_ms = now_ms;
        cache->summary[index].present = true;
        cache->summary[index].valid = true;
        return true;
    }
    if (record->type == 0x48U)
    {
        if (record->length != 32U || !valid_joint(p))
        {
            ++cache->stats.invalid_records;
            return false;
        }
        for (i = 0U; i < CAN_ARM_JOINT_CACHE_COUNT; ++i)
            if (cache->joints[i].present && cache->joints[i].arm_id == p[0] &&
                cache->joints[i].joint_id == p[1]) break;
        if (i == CAN_ARM_JOINT_CACHE_COUNT)
        {
            for (i = 0U; i < CAN_ARM_JOINT_CACHE_COUNT; ++i)
                if (!cache->joints[i].present) break;
            if (i == CAN_ARM_JOINT_CACHE_COUNT)
            {
                uint8_t oldest = 0U;
                for (i = 1U; i < CAN_ARM_JOINT_CACHE_COUNT; ++i)
                    if ((uint32_t)(cache->joints[i].received_ms -
                                   cache->joints[oldest].received_ms) > 0x80000000UL)
                        oldest = i;
                i = oldest;
                ++cache->stats.joint_replaced;
            }
        }
        memset(&cache->joints[i], 0, sizeof(cache->joints[i]));
        cache->joints[i].arm_id = p[0]; cache->joints[i].joint_id = p[1];
        cache->joints[i].joint_count = p[2]; cache->joints[i].flags = p[3];
        cache->joints[i].actual_angle = (int32_t)can_protocol_read_u32(p + 4U);
        cache->joints[i].target_angle = (int32_t)can_protocol_read_u32(p + 8U);
        cache->joints[i].actual_angular_speed = (int32_t)can_protocol_read_u32(p + 12U);
        cache->joints[i].action_group_id = can_protocol_read_u32(p + 16U);
        cache->joints[i].fault_code = can_protocol_read_u32(p + 20U);
        cache->joints[i].sample_age_ms = can_protocol_read_u32(p + 24U);
        cache->joints[i].config_version = can_protocol_read_u32(p + 28U);
        cache->joints[i].received_ms = now_ms;
        cache->joints[i].present = true;
        cache->joints[i].valid = true;
        return true;
    }
    if (record->type == 0x55U)
    {
        if (record->length != 32U || !valid_result(p))
        {
            ++cache->stats.invalid_records;
            return false;
        }
        memset(&cache->result, 0, sizeof(cache->result));
        cache->result.operation_id = can_protocol_read_u32(p);
        cache->result.safety_generation = can_protocol_read_u32(p + 4U);
        cache->result.arm_mask = p[8]; cache->result.phase = p[9];
        cache->result.result = p[10]; cache->result.flags = p[11];
        cache->result.detail = can_protocol_read_u32(p + 12U);
        cache->result.progress = can_protocol_read_u16(p + 16U);
        cache->result.sample_age_ms = can_protocol_read_u32(p + 20U);
        cache->result.config_version = can_protocol_read_u32(p + 24U);
        cache->result.result_sequence = can_protocol_read_u32(p + 28U);
        cache->result.received_ms = now_ms;
        cache->result.present = true;
        cache->result.valid = true;
        ++cache->stats.result_updates;
        return true;
    }
    return false;
}

bool can_arm_get_summary(const can_arm_cache_t *cache, uint8_t arm_id,
                         uint32_t now_ms, can_arm_summary_t *summary)
{
    uint8_t index;
    uint32_t source_age;
    if (cache == NULL || summary == NULL || !valid_arm(arm_id)) return false;
    index = (uint8_t)(arm_id - 1U);
    *summary = cache->summary[index];
    if (!summary->present) return false;
    source_age = summary->sample_age_ms;
    summary->sample_age_ms = summary->sample_age_ms > UINT32_MAX -
        (now_ms - summary->received_ms) ? UINT32_MAX :
        summary->sample_age_ms + now_ms - summary->received_ms;
    summary->valid = valid_age(source_age, summary->received_ms, now_ms,
        CAN_ARM_SUMMARY_TIMEOUT_MS);
    return true;
}

bool can_arm_get_joint(const can_arm_cache_t *cache, uint8_t arm_id,
                       uint8_t joint_id, uint32_t now_ms, can_arm_joint_t *joint)
{
    uint8_t i;
    uint32_t source_age;
    if (cache == NULL || joint == NULL || !valid_arm(arm_id) || joint_id >= 32U)
        return false;
    for (i = 0U; i < CAN_ARM_JOINT_CACHE_COUNT; ++i)
        if (cache->joints[i].present && cache->joints[i].arm_id == arm_id &&
            cache->joints[i].joint_id == joint_id)
        {
            *joint = cache->joints[i];
            source_age = joint->sample_age_ms;
            joint->sample_age_ms = joint->sample_age_ms > UINT32_MAX -
                (now_ms - joint->received_ms) ? UINT32_MAX :
                joint->sample_age_ms + now_ms - joint->received_ms;
            joint->valid = valid_age(source_age, joint->received_ms, now_ms,
                CAN_ARM_SUMMARY_TIMEOUT_MS);
            return true;
        }
    return false;
}

bool can_arm_get_result(const can_arm_cache_t *cache, uint32_t now_ms,
                        can_arm_result_t *result)
{
    uint32_t source_age;
    if (cache == NULL || result == NULL || !cache->result.present) return false;
    *result = cache->result;
    source_age = result->sample_age_ms;
    result->sample_age_ms = result->sample_age_ms > UINT32_MAX -
        (now_ms - result->received_ms) ? UINT32_MAX :
        result->sample_age_ms + now_ms - result->received_ms;
    result->valid = valid_age(source_age, result->received_ms, now_ms,
        CAN_ARM_RESULT_TIMEOUT_MS);
    return true;
}

void can_arm_get_stats(const can_arm_cache_t *cache, can_arm_stats_t *stats)
{
    if (cache != NULL && stats != NULL) *stats = cache->stats;
}
