#define main telemetry_regression_main
#include "test_nrf_telemetry.c"
#undef main
int main(void)
{
    assert(telemetry_regression_main() == 0);
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
    {
        nrf_telemetry_stats_t before, after;
        uint32_t words[24];
        nrf_status_telemetry_get_stats(&before);
        assert(nrf_status_telemetry_diag_words(words, 0U) == 8U);
        assert(words[0] == before.generated && words[1] == before.queue_failures);
        assert(words[4] == before.alarm_event_priority_count && words[5] == before.fast_burst_limited);
        for (uint8_t page = 1U; page <= 2U; ++page) {
            assert(nrf_status_telemetry_diag_words(words, page) == 20U);
            for (uint8_t i = 0U; i < 5U; ++i) {
                uint8_t index = (uint8_t)((page - 1U) * 5U + i);
                assert(words[i * 4U] == before.generated_by_type[index]);
                assert(words[i * 4U + 1U] == before.queued_by_type[index]);
                assert(words[i * 4U + 2U] == before.max_age_ms[index]);
                assert(words[i * 4U + 3U] == before.overdue_count[index]);
            }
        }
        nrf_status_telemetry_get_stats(&after); assert(memcmp(&before, &after, sizeof(before)) == 0);
        assert(nrf_status_telemetry_diag_words(words, 3U) == 0U);
    }
#endif
    puts("PASS telemetry compact diagnostics: all types, cached evidence only, no scheduling side effects");
    return 0;
}
