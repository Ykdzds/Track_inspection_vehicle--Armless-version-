#define main driver_regression_main
#include "test_nrf_dma.c"
#undef main
#define TEST_REAL_DRIVER 1
#define TEST_REMOTE_MODEL 1
#define main telemetry_regression_main
#include "test_nrf_telemetry.c"
#undef main

int main(void)
{
    uint32_t received, previous_accept;
    uint8_t stale_payload[32];
    NRF24L01_BindTask((void *)1);
    init_chip(0U, 0U);
    reset_live();
    remote_status_init(&receiver);
    memset(packet, 0x39, sizeof(packet));
    previous_accept = 0U;
    received = 0U;
    for (unsigned elapsed = 0U; elapsed < 10000U; elapsed++) {
        ticks = now;
        if (elapsed % 10U == 0U && ce) {
            if (ack_count != 0U) {
                ack_count--;
                if (elapsed < 4000U || elapsed >= 4500U) {
                    remote_status_note_payload(&receiver, now);
                    if (remote_status_accept(&receiver, ack, 32U, now)) received++;
                    assert(!remote_status_accept(&receiver, ack, 32U, now));
                    memcpy(stale_payload, ack, sizeof(stale_payload));
                }
            }
            if (rx_count < 3U) rx_count++;
            status |= 0x40U;
        }
        if (elapsed == 4000U) previous_accept = receiver.last_received_ms;
        if (elapsed == 4400U) {
            assert(remote_status_link(&receiver, now) == REMOTE_RETURN_LOST);
            assert(!remote_status_accept(&receiver, stale_payload, 32U, now));
            assert(receiver.last_received_ms == previous_accept);
        }
        NRF24L01_Service();
        if (NRF24L01_Receive() == 1U) NRF24L01_ReportControlFrame();
        nrf_status_telemetry_service();
        if (pending) complete();
        remote_status_tick(&receiver, now);
        now++;
    }
    assert(NRF24L01_IsRadioReady() && received > 100U);
    assert(receiver.stats.link_losses == 1U && receiver.stats.recoveries == 1U);
    assert(receiver.stats.crc_errors == 0U && receiver.stats.format_errors == 0U);
    assert(receiver.max_accept_gap_ms >= 500U && receiver.max_accept_gap_ms <= 600U);
    assert(stats().rx_packets > 800U && stats().dma_timeouts == 0U);
    puts("PASS end-to-end: production scheduler + DMA/FIFO driver + remote model, duplicates and return loss");
    return 0;
}
