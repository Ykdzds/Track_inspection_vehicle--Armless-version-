#include "can_identity.h"
#include "can_protocol.h"
#include <string.h>

#define IDENTITY_MAGIC 0x43414E4AUL
#define MOTION_SCHEMA 0x4D544B31UL

can_identity_result_t can_identity_prepare(const uint8_t *area, uint32_t bytes,
    uint32_t fingerprint, can_identity_t *identity, uint8_t slot[64])
{
    uint32_t i, j, count, next = 0U, revision = 0U, previous = 0U;
    bool blank_seen = false, committed = false;
    memset(identity, 0, sizeof(*identity));
    identity->result = CAN_IDENTITY_CORRUPT;
    if (area == NULL || slot == NULL || bytes == 0U || bytes % 64U != 0U)
        return identity->result;
    count = bytes / 64U;
    for (i = 0U; i < count; ++i)
    {
        const uint8_t *entry = area + i * 64U;
        bool blank = true;
        for (j = 0U; j < 64U; ++j)
            if (entry[j] != 0xFFU) { blank = false; break; }
        if (blank)
        {
            blank_seen = true;
            continue;
        }
        /* 已写记录后出现空洞再出现记录，不能作为正常追加日志接受。 */
        if (blank_seen) return identity->result;
        next = i + 1U;
        if (can_protocol_read_u32(entry + 60U) == CAN_IDENTITY_COMMIT)
        {
            uint32_t first = i * 32U + 1U;
            uint32_t last = first + 31U;
            if (last > 65534U) last = 65534U;
            if (can_protocol_read_u32(entry + 28U) == MOTION_SCHEMA)
            {
                if (can_protocol_read_u16(entry + 24U) != first ||
                    can_protocol_read_u16(entry + 26U) != last)
                    return identity->result;
            }
            else if (can_protocol_read_u32(entry + 24U) != 0U ||
                     can_protocol_read_u32(entry + 28U) != 0U)
                return identity->result;
            if (can_protocol_read_u32(entry) != IDENTITY_MAGIC ||
                can_protocol_read_u32(entry + 4U) != 1U ||
                can_protocol_read_u32(entry + 8U) != i + 1U ||
                can_protocol_read_u16(entry + 20U) != first ||
                can_protocol_read_u16(entry + 22U) != last ||
                can_protocol_read_u32(entry + 16U) == 0U ||
                can_protocol_read_u32(entry + 56U) !=
                    can_protocol_crc32(entry, 56U))
                return identity->result;
            previous = can_protocol_read_u32(entry + 12U);
            revision = can_protocol_read_u32(entry + 16U);
            committed = true;
        }
        else if (can_protocol_read_u32(entry + 60U) != 0xFFFFFFFFUL)
        {
            /* 提交标记撕裂可来自掉电；有效CRC主体才允许安全跳过。 */
            if (can_protocol_read_u32(entry + 56U) !=
                can_protocol_crc32(entry, 56U)) return identity->result;
        }
    }
    if (next >= count) return identity->result = CAN_IDENTITY_FULL;
    if (next * 32U + 1U >= 65535U)
        return identity->result = CAN_IDENTITY_EXHAUSTED;
    if (!committed) revision = 1U;
    else if (previous != fingerprint)
    {
        if (revision == 0xFFFFFFFFUL) return identity->result;
        ++revision;
    }
    identity->slot = next;
    identity->boot_id = next + 1U;
    identity->fingerprint = fingerprint;
    identity->revision = revision;
    identity->first_token = (uint16_t)(next * 32U + 1U);
    identity->last_token = (uint16_t)(next * 32U + 32U > 65534U ?
                                      65534U : next * 32U + 32U);
    identity->motion_first = identity->first_token;
    identity->motion_last = identity->last_token;
    memset(slot, 0, 64U);
    can_protocol_write_u32(slot, IDENTITY_MAGIC);
    can_protocol_write_u32(slot + 4U, 1U);
    can_protocol_write_u32(slot + 8U, identity->boot_id);
    can_protocol_write_u32(slot + 12U, fingerprint);
    can_protocol_write_u32(slot + 16U, revision);
    can_protocol_write_u16(slot + 20U, identity->first_token);
    can_protocol_write_u16(slot + 22U, identity->last_token);
    can_protocol_write_u16(slot + 24U, identity->motion_first);
    can_protocol_write_u16(slot + 26U, identity->motion_last);
    can_protocol_write_u32(slot + 28U, MOTION_SCHEMA);
    can_protocol_write_u32(slot + 56U, can_protocol_crc32(slot, 56U));
    can_protocol_write_u32(slot + 60U, CAN_IDENTITY_COMMIT);
    return identity->result = CAN_IDENTITY_OK;
}
