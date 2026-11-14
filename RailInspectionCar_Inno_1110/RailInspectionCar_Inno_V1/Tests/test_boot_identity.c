#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_boot_identity.h"
/* 在测试进程模拟复位RAM，Flash日志保持不变。 */
#include "../APP/app_boot_identity.c"
static uint8_t area[64U * 4U];
static unsigned calls;
static bool fail_write;
uint32_t app_can_config_fingerprint(void) { return 123U; }
bool bsp_can_identity_startup(uint32_t fingerprint, can_identity_t *identity)
{
    uint8_t slot[64];
    ++calls;
    if (can_identity_prepare(area, sizeof(area), fingerprint, identity, slot) != CAN_IDENTITY_OK) return false;
    if (fail_write)
    {
        memcpy(area + identity->slot * 64U, slot, 12U);
        identity->result = CAN_IDENTITY_WRITE_FAILED;
        return false;
    }
    memcpy(area + identity->slot * 64U, slot, 64U);
    return true;
}
static void reset_ram(void) { s_initialized = false; s_valid = false; memset(&s_identity, 0, sizeof(s_identity)); }
int main(void)
{
    unsigned i;
    memset(area, 0xFF, sizeof(area));
    for (i = 0; i < 2; i++)
    {
        reset_ram(); app_boot_identity_init(); app_boot_identity_init();
        assert(app_boot_identity_valid() && app_boot_identity_id() == i + 1 && calls == i + 1);
    }
    fail_write = true; reset_ram(); app_boot_identity_init();
    assert(!app_boot_identity_valid() && app_boot_identity_id() == UINT32_MAX);
    fail_write = false; reset_ram(); app_boot_identity_init();
    assert(app_boot_identity_id() == 4U);
    reset_ram(); app_boot_identity_init(); assert(!app_boot_identity_valid());
    assert(app_boot_identity_get()->result == CAN_IDENTITY_FULL);
    memset(area, 0xFF, sizeof(area)); reset_ram(); app_boot_identity_init();
    area[0] ^= 1; reset_ram(); app_boot_identity_init();
    assert(!app_boot_identity_valid() && app_boot_identity_get()->result == CAN_IDENTITY_CORRUPT);
    puts("PASS shared boot identity: once per boot, partial write skipped, full/corrupt fail closed; CAN independent");
    return 0;
}
