#define main nrf_regression_main
#include "test_nrf_remote.c"
#undef main

#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
static unsigned events[5];
void app_link_diag_record(uint8_t source, uint16_t kind, uint32_t a, uint32_t b,
                         uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    assert(source == APP_LINK_REMOTE && kind < 5U);
    ++events[kind];
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
}
void app_link_diag_freeze(uint8_t source, uint16_t kind, const uint32_t *words, uint8_t count)
{ (void)source; (void)kind; (void)words; (void)count; assert(!"RX path must not consume the motion first-stop record"); }
uint8_t NRF24L01_DiagnosticWords(uint32_t *words, uint8_t phase)
{
    unsigned count = phase == 0U ? 12U : 16U;
    for (unsigned i = 0U; i < count; ++i) words[i] = phase * 100U + i;
    return (uint8_t)count;
}
uint8_t nrf_status_telemetry_diag_words(uint32_t *words, uint8_t phase)
{
    unsigned count = phase ? 20U : 8U;
    for (unsigned i = 0U; i < count; ++i) words[i] = 1000U + phase * 100U + i;
    return (uint8_t)count;
}
#endif
int main(void)
{
    assert(nrf_regression_main() == 0);
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
    {
        app_link_diag_snapshot_t sample;
        nrf_remote_snapshot_t before, after;
        unsigned drops, rejects;
        assert(events[APP_REMOTE_DIAG_RX] && events[APP_REMOTE_DIAG_LINK_DOWN] &&
               events[APP_REMOTE_DIAG_RECOVERY]);
        rejects = events[APP_REMOTE_DIAG_REJECT];
        NRF24L01_RxPacket[28] ^= 1U; pending = 1U;
        app_nrf_remote_service();
        assert(events[APP_REMOTE_DIAG_REJECT] == rejects + 1U);
        send_frame(31U, 55U, 1U);
        app_nrf_remote_get_snapshot(&before);
        drops = events[APP_REMOTE_DIAG_LINK_DOWN];
        tick += APP_REMOTE_SERVO_LINK_TIMEOUT_MS - 1U;
        app_nrf_remote_service(); assert(events[APP_REMOTE_DIAG_LINK_DOWN] == drops);
        tick += 1U; app_nrf_remote_service();
        assert(events[APP_REMOTE_DIAG_LINK_DOWN] == drops + 1U);
        app_nrf_remote_service(); assert(events[APP_REMOTE_DIAG_LINK_DOWN] == drops + 1U);
        app_nrf_remote_diag_snapshot(&sample, 1U);
        assert(sample.count == 24U && sample.words[0] == before.valid_frames && sample.words[12] == 0U);
        app_nrf_remote_diag_snapshot(&sample, 2U);
        assert(sample.count == 24U && sample.words[0] == 100U && sample.words[1] == 101U && sample.words[16] == 1000U);
        app_nrf_remote_diag_snapshot(&sample, 3U); assert(sample.count == 16U);
        app_nrf_remote_diag_snapshot(&sample, 4U); assert(sample.count == 20U && sample.words[0] == 1100U);
        app_nrf_remote_diag_snapshot(&sample, 5U); assert(sample.count == 20U && sample.words[0] == 1200U);
        app_nrf_remote_diag_snapshot(&sample, 6U); assert(sample.count == 0U);
        app_nrf_remote_get_snapshot(&after);
        assert(after.valid_frames == before.valid_frames && after.event_batches == before.event_batches);
    }
#endif
    puts("PASS NRF diagnostics: 150ms boundary, recovery/rebaseline, CRC, independent ACK stages and compact projections without side effects");
    return 0;
}
