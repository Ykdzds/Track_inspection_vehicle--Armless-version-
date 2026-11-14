/* Existing hardware stubs and full regression drive the production controller.
 * Inclusion exposes no replacement implementation: hooks/getter below are the
 * exact source compiled into the firmware. */
#define main remote_regression_main
#include "test_remote_servo.c"
#undef main
#include "../APP/app_remote_servo.c"

/* The remote host fixture links the remote controller without the F407 servo
 * module.  Keep the production startup-cause seam observable but inert here. */
void aimtor_diag_startup_stop(uint8_t source, uint8_t axis,
    uint16_t reason, uint32_t detail)
{
    (void)source; (void)axis; (void)reason; (void)detail;
}

#if APP_LINK_DIAG_ACTIVE
void app_hmi_link_diag_stop(const app_control_stop_t *stop, uint16_t kind)
{ (void)stop; (void)kind; }
void app_hmi_link_diag_upgrade(const app_control_stop_t *stop,uint16_t requester,
    uint32_t sequence,uint16_t command)
{(void)stop;(void)requester;(void)sequence;(void)command;}
#endif

#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
static unsigned diag_events[0x42], freeze_calls;
static uint32_t frozen[16], last_publish, last_consume;
void app_link_diag_record(uint8_t source, uint16_t kind, uint32_t a, uint32_t b,
                         uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    assert(source == APP_LINK_REMOTE && kind < 0x42U);
    ++diag_events[kind];
    if (kind == APP_REMOTE_DIAG_PUBLISH) last_publish = a;
    if (kind == APP_REMOTE_DIAG_CONSUME) last_consume = a;
    (void)b; (void)c; (void)d; (void)e; (void)f;
}
void app_link_diag_freeze(uint8_t source, uint16_t kind, const uint32_t *words, uint8_t count)
{
    assert(source == APP_LINK_REMOTE && kind == APP_REMOTE_DIAG_FIRST_STOP && count == 16U);
    ++freeze_calls; memcpy(frozen, words, sizeof(frozen));
}
void app_nrf_remote_diag_snapshot(app_link_diag_snapshot_t *out, uint8_t phase)
{ (void)phase; out->count = 0U; }
#endif

static void verify_position_gate(void)
{
    remote_position_gate_t gate;
    remote_servo_status_t full;
    unsigned old_writes = writes[0] + writes[1], old_stops = stops;
    assert(!app_remote_servo_get_position_gate(NULL));
    assert(app_remote_servo_get_status(&full));
    memset(&gate, 0xA5, sizeof(gate));
    assert(app_remote_servo_get_position_gate(&gate));
    assert(gate.authorization_generation == full.authorization_generation);
    assert(gate.state == full.state && gate.authorized == full.authorized);
    assert(gate.hold_stopping == full.hold_stopping && gate.estop_latched == full.estop_latched);
    assert(gate.control_axis_mask == full.control_axis_mask);
    assert(writes[0] + writes[1] == old_writes && stops == old_stops);
}

int main(void)
{
    assert(remote_regression_main() == 0);
    verify_position_gate();
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
    {
        nrf_remote_snapshot_t input;
        app_link_diag_snapshot_t sample;
        uint32_t first[16];
        unsigned events_before, freezes_before;
        setup_stop_test(&input, 1);
        verify_position_gate();
        input.valid_frames = 16U; input.frame_sequence = 42U; input.event_sequence = 0x1234U;
        feed(&input);
        assert(last_publish == 16U && last_consume == 16U);
        events_before = diag_events[APP_REMOTE_DIAG_CONSUME];
        feed(&input);
        assert(diag_events[APP_REMOTE_DIAG_CONSUME] == events_before);
        app_remote_diag_snapshot(&sample, 0U);
        assert(sample.count == 24U && sample.words[3] == 16U);
        assert(sample.words[2] == 0x12342AU);
        assert((int16_t)sample.words[10] == 600 && (int16_t)(sample.words[10] >> 16U) == 600);
        ticks += 25U;
        app_remote_diag_snapshot(&sample, 0U);
        assert(sample.words[14] == 25U && sample.words[15] == 25U && sample.words[23] == 25U);
        freezes_before = freeze_calls;
        input.age_ms = APP_REMOTE_SERVO_LINK_TIMEOUT_MS;
        feed(&input);
        assert(status().hold_stopping && !status().authorized);
        verify_position_gate();
        assert(freeze_calls == freezes_before + 1U && frozen[0] == REMOTE_STOP_LINK);
        assert(frozen[1] == APP_REMOTE_SERVO_LINK_STOP_DECEL_MM_S2);
        assert(frozen[2] & (1U << 8U)); /* Authorization was still live at capture. */
        assert((int32_t)frozen[3] != 0 && (int16_t)frozen[4] == 600);
        memcpy(first, frozen, sizeof(first));
        input.age_ms = 0U; input.link_up = 1U;
        input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        feed(&input);
        assert(status().controlled_stop_reason == REMOTE_STOP_K1);
        assert(diag_events[APP_REMOTE_DIAG_STOP_UPGRADE] > 0U);
        assert(freeze_calls == freezes_before + 1U && memcmp(first, frozen, sizeof(first)) == 0);
        finish_stop(&input);
        verify_position_gate();
        assert(diag_events[APP_REMOTE_DIAG_STOP_DONE] > 0U);
        assert(freeze_calls == freezes_before + 1U);
        assert(diag_events[APP_REMOTE_DIAG_BATCH] && diag_events[APP_REMOTE_DIAG_AXIS_ACK] &&
               diag_events[APP_REMOTE_DIAG_BATCH_DONE] && diag_events[APP_REMOTE_DIAG_STATE]);
        /* A later event gets a new first cause, including the failing axis before
         * the emergency path destroys enable/verification evidence. */
        setup_stop_test(&input, 1);
        axes[1].fault = 1U; axes[1].fault_code = 0x42U;
        axes[1].last_poll_address = 0x0B00U; axes[1].last_poll_result = 4U;
        feed(&input);
        assert(freeze_calls == freezes_before + 2U && frozen[0] == REMOTE_STOP_SERVO_STATE);
        assert((frozen[15] >> 16U) == 0x42U);
        assert(frozen[14] == 0x0B000004U);
        assert(sizeof(s_remote_diag_feedback) == 28U);
    }
#endif
#if APP_LINK_DIAG_ACTIVE && APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED
    {
        app_link_diag_snapshot_t sample;
        unsigned old_writes = writes[0] + writes[1], old_stops = stops;
        app_remote_diag_snapshot(&sample, 0x80U);
        assert(sample.count == 24U && sample.words[5] == s_batch_id);
        assert(writes[0] + writes[1] == old_writes && stops == old_stops);
    }
#endif
    puts("PASS remote diagnostics: unchanged control regression, RX correlation, fresh ages, first cause, upgrade, completion, compact SYNC");
    return 0;
}
