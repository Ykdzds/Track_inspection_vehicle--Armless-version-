#include "can_upper.h"
#include <string.h>

static int slot_index(uint8_t type)
{
    if (type == 0x02U) return 0;
    return type >= 0x40U && type <= 0x45U ? (int)(type - 0x40U + 1U) : -1;
}

static bool enum_unknown(uint8_t value, uint8_t maximum)
{
    return value <= maximum || value == 255U;
}

void can_upper_init(can_upper_cache_t *cache)
{
    memset(cache, 0, sizeof(*cache));
}

bool can_upper_publish(can_upper_cache_t *cache,
    const can_protocol_record_t *record, uint32_t now_ms,
    uint32_t lower_boot, uint32_t upper_boot)
{
    const uint8_t *p = record->payload;
    int index = slot_index(record->type);
    uint16_t length = record->type == 0x43U ? 48U :
                      record->type == 0x45U ? 24U : 32U;
    can_upper_snapshot_t *out;
    if (index < 0 || record->length != length) return false;
    switch (record->type)
    {
        case 0x02U:
            if (can_protocol_read_u32(p) != upper_boot ||
                can_protocol_read_u32(p + 4U) != lower_boot ||
                p[20] != 1U || p[21] != 0U || p[22] != 1U || p[23] != 0U)
                return false;
            break;
        case 0x40U:
            if (p[8] > 7U || !enum_unknown(p[9], 6U) || p[10] > 5U ||
                p[11] > 5U || !enum_unknown(p[12], 100U)) return false;
            break;
        case 0x41U:
            if (p[19] > 3U ||
                ((can_protocol_read_u32(p + 28U) & 1U) != 0U &&
                 (p[17] != 4U || p[18] != 12U))) return false;
            break;
        case 0x42U:
            if (!enum_unknown(p[20], 3U) || !enum_unknown(p[21], 5U) ||
                !enum_unknown(p[22], 100U)) return false;
            break;
        case 0x43U:
            /* 第一阶段定位生产者固定为RK，不接收或镜像其他生产者。 */
            if (p[0] != 1U || p[1] > 3U || p[2] > 3U) return false;
            if ((p[3] & 1U) != 0U)
            {
                int32_t lat = (int32_t)can_protocol_read_u32(p + 4U);
                int32_t lon = (int32_t)can_protocol_read_u32(p + 8U);
                if (lat < -900000000L || lat > 900000000L ||
                    lon < -1800000000L || lon > 1800000000L) return false;
            }
            break;
        default:
            break;
    }
    out = &cache->slots[index];
    memset(out, 0, sizeof(*out));
    out->type = record->type;
    out->length = length;
    out->sequence = record->sequence;
    out->received_ms = now_ms;
    out->present = true;
    memcpy(out->payload, p, length);
    return true;
}

bool can_upper_get(const can_upper_cache_t *cache, uint8_t type,
    uint32_t now_ms, can_upper_snapshot_t *out)
{
    int index = slot_index(type);
    uint32_t limit, source_age = 0U, elapsed;
    if (index < 0 || out == NULL) return false;
    *out = cache->slots[index];
    if (!out->present) return false;
    elapsed = now_ms - out->received_ms;
    if (type == 0x43U) source_age = can_protocol_read_u32(out->payload + 40U);
    out->age_ms = source_age > UINT32_MAX - elapsed ? UINT32_MAX : source_age + elapsed;
    limit = type == 0x40U || type == 0x43U ? 1500U : 3000U;
    /* 能力记录在会话内有效；其他记录还需分别检查业务有效标志。 */
    out->valid = type == 0x02U || out->age_ms <= limit;
    return true;
}
