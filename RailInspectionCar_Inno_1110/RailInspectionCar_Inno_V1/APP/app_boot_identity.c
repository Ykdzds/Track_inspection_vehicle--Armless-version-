#include "app_boot_identity.h"
#include "app_can_config.h"
#include "bsp_can_identity.h"

static can_identity_t s_identity;
static bool s_initialized;
static bool s_valid;

void app_boot_identity_init(void)
{
    if (s_initialized) return;
    s_initialized = true;
    s_valid = bsp_can_identity_startup(app_can_config_fingerprint(), &s_identity);
    if (!s_valid) s_identity.boot_id = 0U;
}

const can_identity_t *app_boot_identity_get(void) { return &s_identity; }
uint32_t app_boot_identity_id(void) { return s_valid ? s_identity.boot_id : 0xFFFFFFFFUL; }
bool app_boot_identity_valid(void) { return s_initialized && s_valid; }
