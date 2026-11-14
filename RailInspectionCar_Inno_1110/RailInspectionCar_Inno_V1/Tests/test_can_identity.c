#include "can_identity.h"
#include "can_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t area[128U * 1024U], slot[64];
static can_identity_t identity;

int main(void)
{
    uint32_t cut;
    memset(area, 0xFF, sizeof(area));
    assert(can_identity_prepare(area, sizeof(area), 10U, &identity, slot) == CAN_IDENTITY_OK);
    assert(identity.boot_id == 1U && identity.first_token == 1U &&
           identity.last_token == 32U && identity.revision == 1U);
    assert(identity.motion_first == 1U && identity.motion_last == 32U);
    /* 兼容旧日志保留字段；旧记录仍消耗独立运动编号块。 */
    memset(slot + 24U, 0, 8U);
    can_protocol_write_u32(slot + 56U, can_protocol_crc32(slot, 56U));
    memcpy(area, slot, 64U);
    assert(can_identity_prepare(area, sizeof(area), 10U, &identity, slot) == CAN_IDENTITY_OK);
    assert(identity.boot_id == 2U && identity.first_token == 33U && identity.revision == 1U);
    assert(identity.motion_first == 33U && identity.motion_last == 64U);
    {
        uint8_t damaged[128];
        memset(damaged, 0xFF, sizeof(damaged));
        memcpy(damaged, slot, 64U);
        /* 构造首槽身份且CRC正确，仅运动域边界被破坏。 */
        can_protocol_write_u32(damaged + 8U, 1U);
        can_protocol_write_u16(damaged + 20U, 1U);
        can_protocol_write_u16(damaged + 22U, 32U);
        can_protocol_write_u16(damaged + 24U, 2U);
        can_protocol_write_u16(damaged + 26U, 32U);
        can_protocol_write_u32(damaged + 56U, can_protocol_crc32(damaged, 56U));
        assert(can_identity_prepare(damaged, sizeof(damaged), 10U, &identity, damaged + 64U) == CAN_IDENTITY_CORRUPT);
        assert(can_identity_prepare(area, sizeof(area), 10U, &identity, slot) == CAN_IDENTITY_OK);
    }
    memcpy(area + 64U, slot, 64U);
    assert(can_identity_prepare(area, sizeof(area), 11U, &identity, slot) == CAN_IDENTITY_OK);
    assert(identity.revision == 2U && identity.first_token == 65U);
    /* 每个字写入后掉电都跳过该编号块，不能将部分写入解释为已提交身份。 */
    for (cut = 4U; cut < 64U; cut += 4U)
    {
        uint8_t partial[64];
        memcpy(partial, slot, 64U);
        memset(area + 128U, 0xFF, sizeof(area) - 128U);
        memcpy(area + 128U, partial, cut);
        assert(can_identity_prepare(area, sizeof(area), 11U, &identity, slot) == CAN_IDENTITY_OK);
        assert(identity.boot_id == 4U && identity.first_token == 97U);
        memcpy(slot, partial, 64U);
    }
    memset(area + 128U, 0xFF, sizeof(area) - 128U);
    area[5] ^= 1U;
    assert(can_identity_prepare(area, sizeof(area), 10U, &identity, slot) == CAN_IDENTITY_CORRUPT);
    memset(area, 0xFF, sizeof(area));
    area[64] = 0U;
    assert(can_identity_prepare(area, sizeof(area), 10U, &identity, slot) == CAN_IDENTITY_CORRUPT);
    memset(area, 0, sizeof(area));
    /* 未提交槽也消耗编号块；最后一块不分配65535。 */
    for (cut = 0U; cut < sizeof(area); cut += 64U)
        memset(area + cut + 60U, 0xFF, 4U);
    memset(area + sizeof(area) - 64U, 0xFF, 64U);
    assert(can_identity_prepare(area, sizeof(area), 10U, &identity, slot) == CAN_IDENTITY_OK);
    assert(identity.first_token == 65505U && identity.last_token == 65534U);
    memcpy(area + sizeof(area) - 64U, slot, 64U);
    assert(can_identity_prepare(area, sizeof(area), 10U, &identity, slot) == CAN_IDENTITY_FULL);
    puts("PASS CAN identity revision, partial programming, corruption and exhaustion");
    return 0;
}
