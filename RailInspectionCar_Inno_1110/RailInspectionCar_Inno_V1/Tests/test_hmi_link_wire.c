/* Real DMA parser/router plus the real logger, sharing the integration fixture. */
#define main hmi_existing_wire_suite
#include "test_hmi_integration.c"
#undef main
#include "bsp_system.h"
#include "app_lcd_show.h"
#include "lcd_port.h"
void app_lcd_get_event_state(app_lcd_event_state_t *out) { memset(out, 0, sizeof(*out)); }
void lcd_port_get_stats(lcd_port_stats_t *out) { memset(out, 0, sizeof(*out)); }

static char captured[12000];
static size_t captured_length;
uint32_t bsp_system_get_reset_reason(void) { return 0U; }
uint8_t bsp_system_get_previous_fault(bsp_system_fault_record_t *out)
{ (void)out; return 0U; }
uint8_t bsp_system_get_previous_exception(bsp_system_fault_record_t *out)
{ (void)out; return 0U; }
size_t xPortGetFreeHeapSize(void) { return 10000U; }
size_t xPortGetMinimumEverFreeHeapSize(void) { return 10000U; }
uint8_t debug_uart_try_write_frame(const uint8_t *data, uint16_t length, uint16_t reserve)
{
    assert(reserve == 512U && length <= 256U);
    assert(captured_length + length + 1U < sizeof(captured));
    memcpy(captured + captured_length, data, length); captured_length += length;
    captured[captured_length] = '\0'; return 1U;
}
static void send_diag_page(uint16_t page, uint16_t event)
{
    uint8_t frame[57] = {1U, 0x10U, 7U, 0U, 0U, 24U, 48U};
    uint16_t crc;
    frame[7] = 0x48U; frame[8] = 0x44U; frame[9] = 1U;
    frame[10] = (uint8_t)page; frame[13] = (uint8_t)(event >> 8U); frame[14] = (uint8_t)event;
    crc = hmi_modbus_crc16(frame, 55U); frame[55] = (uint8_t)crc; frame[56] = (uint8_t)(crc >> 8U);
    inject(frame, sizeof(frame)); flush();
    assert(response_size == 8U && response[1] == 0x10U && !mailbox_calls);
    complete();
}
static void drain(void)
{
    unsigned i;
    for (i = 0; i < 50U; ++i) { app_hmi_link_diag_service(); advance(1000U); }
}
static void slow_wire_transaction(void)
{
    uint8_t frame[8];
    uint32_t captured_cycles, source_sequence;
    fresh(); captured_length = 0U;
    request(frame, 0x0460U, 96U); inject(frame, sizeof(frame)); boundary();
    captured_cycles = s_last_rx_cycles;
    advance(32000U); /* Controlled task delay, not an on-device measurement. */
    app_hmi_service();
    assert(response_size == 197U && response[1] == 3U && s_diag_active.id == 1U);
    assert(s_diag_active.capture == captured_cycles);
    assert(s_diag_active.boundary - captured_cycles == 1750U);
    assert(s_diag_active.service - s_diag_active.boundary == 32000U);
    assert(s_diag_active.flags == 1U && s_diag_active.source_sequence != 0U);
    source_sequence = s_diag_active.source_sequence;
    /* A second request cannot replace the still-active response's metadata. */
    request(frame, 0x0050U, 32U); inject(frame, sizeof(frame)); flush();
    assert(s_stats.tx_busy == 1U && s_diag_active.id == 1U);
    assert(s_diag_active.start == 0x0460U && s_diag_active.length == 197U);
    advance(17102U); complete(); app_hmi_service(); drain();
    assert(strstr(captured, " 1 SLOW p=0") && strstr(captured, " 1 SLOW p=1"));
    assert(strstr(captured, "req=460/60 fc=3 len=C5") && strstr(captured, "flags=1"));
    assert(!strstr(captured, " 2 SLOW "));
    assert(s_diag_active.id == 0U && source_sequence != 0U);
    assert(!mailbox_calls && !unsafe_calls);
    puts("PASS real transaction stages/TC: 96 words, delayed task, TX busy correlation, immutable source metadata");
}
int main(void)
{
    fresh(); captured_length = 0U;
    send_diag_page(0U, 77U); drain();
    assert(strstr(captured, "SCREEN page=0 mask=1 incomplete=1"));
    send_diag_page(1U, 77U); drain();
    assert(strstr(captured, "SCREEN page=1 mask=3 incomplete=0"));
    assert(!unsafe_calls && !mailbox_calls);
    slow_wire_transaction();
    puts("PASS real UART5 DMA -> Modbus diagnostic sink -> USART1 text, both pages and no motion path");
    return 0;
}
