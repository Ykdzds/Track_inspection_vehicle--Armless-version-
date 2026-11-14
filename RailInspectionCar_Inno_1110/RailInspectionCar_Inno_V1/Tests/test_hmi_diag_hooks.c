/* Exercise the real control/arbiter hook sites without UART formatting. */
#define main hmi_existing_control_suite
#include "test_hmi_control.c"
#undef main
#include "app_hmi_link_diag.h"

static app_control_stop_t frozen_first;
static unsigned first_calls, upgrade_calls, complete_calls, consume_calls;
static uint32_t upgrade_reason, upgrade_source;
void app_hmi_link_diag_init(void) { }
void app_hmi_link_diag_service(void) { }
uint8_t app_hmi_link_diag_screen(const uint16_t *words, uint16_t count)
{ (void)words; (void)count; return 0U; }
void app_hmi_link_diag_record(uint16_t kind, uint32_t a, uint32_t b,
    uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    (void)a; (void)d; (void)e; (void)f;
    if (kind == APP_HMI_LINK_STOP_UPGRADE) {
        ++upgrade_calls; upgrade_reason = b; upgrade_source = c & 0xFFFFU;
    }
    if (kind == APP_HMI_LINK_CONSUME) ++consume_calls;
}
void app_hmi_link_diag_stop(const app_control_stop_t *stop, uint16_t kind)
{
    if (kind == APP_HMI_LINK_STOP_FIRST) { frozen_first = *stop; ++first_calls; }
    if (kind == APP_HMI_LINK_STOP_COMPLETE) ++complete_calls;
}

void app_hmi_link_diag_upgrade(const app_control_stop_t *stop,uint16_t requester,
    uint32_t sequence,uint16_t command)
{
    app_hmi_link_diag_record(APP_HMI_LINK_STOP_UPGRADE,stop->event,stop->reason,
        ((uint32_t)stop->owner<<16U)|requester,sequence,
        ((uint32_t)command<<16U)|stop->deceleration_mm_s2,0U);
}

/* The production HMI stop hook records the cross-module startup cause.  This
 * focused host test does not link the servo module, so provide the same
 * side-effect-free seam used by the other host fixtures. */
void aimtor_diag_startup_stop(uint8_t source, uint8_t axis,
    uint16_t reason, uint32_t detail)
{
    (void)source; (void)axis; (void)reason; (void)detail;
}

int main(void)
{
    uint16_t mailbox[24];
    uint32_t session;
    app_control_stop_t current;
    unsigned i;
    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U); submit_service(mailbox);
    session = app_hmi_control_session(); assert(session);
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 120, 500U); submit_service(mailbox);
    advance_ms(499U);
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 3U, 120, 500U);
    assert(app_hmi_control_submit(mailbox));
    advance_ms(1U); app_hmi_control_service();
    assert(first_calls == 1U && frozen_first.reason == 3U);
    assert(frozen_first.deceleration_mm_s2 == APP_HMI_LINK_STOP_DECEL_MM_S2);
    assert(frozen_first.owner == APP_CONTROL_OWNER_HMI);
    /* Observe the existing 499/500 behavior; this delivery must not alter it. */
    assert_rejected(TEST_ERR_SESSION_EXPIRED);
    assert(app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_REMOTE, 5U,
        APP_HMI_EMERGENCY_STOP_DECEL_MM_S2, 90U, TEST_CMD_STOP));
    assert(upgrade_calls == 1U && upgrade_reason == 5U);
    assert(upgrade_source == APP_CONTROL_OWNER_REMOTE);
    app_control_arbiter_get_stop(&current);
    assert(current.reason == 5U && current.event == frozen_first.event);
    assert(frozen_first.reason == 3U && first_calls == 1U);
    g_feedback_progress = 1U;
    for (i = 0U; i < 2000U && app_hmi_control_session(); ++i) {
        advance_ms(10U); app_hmi_control_service();
    }
    assert(!app_hmi_control_session() && complete_calls == 1U);
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 4U, 0, 0U); submit_service(mailbox);
    assert(app_hmi_control_session() && first_calls == 1U && frozen_first.reason == 3U);
    assert(consume_calls >= 4U);
    puts("PASS production HMI hooks: first TTL cause, cross-source upgrade, completion, reauthorization and unchanged 499/500 boundary");
    return 0;
}
