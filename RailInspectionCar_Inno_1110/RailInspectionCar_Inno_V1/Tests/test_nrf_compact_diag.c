#define main driver_regression_main
#include "test_nrf_dma.c"
#undef main
int main(void)
{
    assert(driver_regression_main() == 0);
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
    {
        NRF24L01_Stats before, after;
        uint32_t words[24];
        uint8_t payload[32] = {0};
        NRF24L01_GetStats(&before);
        assert(NRF24L01_DiagnosticWords(words, 0U) == 12U);
        assert(words[0] == before.spi_errors && words[5] == before.dma_timeouts && words[11] == before.rx_packets);
        assert(NRF24L01_DiagnosticWords(words, 1U) == 16U);
        assert(words[0] == before.ack_queued && words[1] == before.ack_loaded);
        assert(words[10] == before.ack_clear_count && words[15] == before.event_overwrites);
        assert(NRF24L01_DiagnosticWords(words, 2U) == 16U);
        assert(words[0] == before.failure_snapshot.tick_ms && words[4] == before.failure_snapshot.spi_sr);
        assert((words[13] & 255U) == before.failure_snapshot.valid);
        NRF24L01_GetStats(&after); assert(memcmp(&before, &after, sizeof(before)) == 0);
#if APP_NRF_ACK_PAYLOAD_ENABLED
        /* Software queue acceptance is separate from hardware loading. */
        assert(NRF24L01_QueueAckPayload(payload));
        assert(NRF24L01_DiagnosticWords(words, 1U) == 16U);
        assert(words[0] == before.ack_queued + 1U && words[1] == before.ack_loaded && (words[9] & 1U));
#else
        (void)payload;
#endif
        assert(NRF24L01_DiagnosticWords(words, 3U) == 0U);
        assert(NRF24L01_DiagnosticWords(NULL, 0U) == 0U);
    }
#endif
    puts("PASS NRF driver compact diagnostics: cache equivalence, pending vs loaded, failure registers, no driver changes");
    return 0;
}
