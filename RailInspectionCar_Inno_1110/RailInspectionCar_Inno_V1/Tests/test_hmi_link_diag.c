#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "app_hmi_link_diag.h"
#if APP_LINK_DIAG_ACTIVE
#include "app_hmi_control.h"
#include "app_tune_diagnostics.h"
#include "bsp_hmi_uart.h"
#include "bsp_system.h"
#include "app_hmi_snapshot.h"
#include "app_lcd_show.h"
#include "lcd_port.h"

static uint32_t now, boot_id = 77U;
static uint8_t blocked, normal_load;
static const char *reject_line;
static unsigned reject_remaining;
static char output[800000];
static unsigned output_count, output_bytes, second_bytes, max_second, first_writes;
static app_hmi_control_status_t status;
static app_hmi_snapshot_diag_t snapshot_status;
static app_lcd_event_state_t lcd_status;
static lcd_port_stats_t lcd_stats;
static unsigned reject_acks;
void app_hmi_snapshot_diag_read(app_hmi_snapshot_diag_t *out) { *out = snapshot_status; }
void app_hmi_snapshot_diag_ack_reject(uint32_t id)
{
    assert(snapshot_status.reject_pending && id == snapshot_status.reject.id);
    snapshot_status.reject_pending = 0U; ++reject_acks;
}
void app_lcd_get_event_state(app_lcd_event_state_t *out)
{
#if !APP_CHASSIS_LCD_ENABLED
    assert(0 && "LCD getter called in a build without chassis LCD");
#endif
    *out = lcd_status;
}
void lcd_port_get_stats(lcd_port_stats_t *out)
{
#if !APP_CHASSIS_LCD_ENABLED
    assert(0 && "LCD port getter called in a build without chassis LCD");
#endif
    *out = lcd_stats;
}
uint32_t HAL_GetTick(void) { return now; }
uint32_t app_boot_identity_id(void) { return boot_id; }
uint32_t bsp_system_get_reset_reason(void) { return 0x20000000U; }
uint8_t bsp_system_get_previous_fault(bsp_system_fault_record_t *out)
{ memset(out, 0, sizeof(*out)); out->fault_code = 2U; out->task_id = 6U; return 1U; }
uint8_t bsp_system_get_previous_exception(bsp_system_fault_record_t *out)
{ memset(out, 0, sizeof(*out)); out->pc = 0x08012345U; out->cfsr = 0x02000000U; return 1U; }
void app_hmi_control_get_status(app_hmi_control_status_t *out) { *out = status; }
uint16_t app_hmi_control_remaining_ms(void) { return 800U; }
app_control_owner_t app_control_arbiter_owner(void) { return APP_CONTROL_OWNER_HMI; }
size_t xPortGetFreeHeapSize(void) { return 777U; }
size_t xPortGetMinimumEverFreeHeapSize(void) { return 666U; }
uint8_t app_system_snapshot_read_diagnostic(app_tune_diag_system_t *out)
{
    unsigned i;
    memset(out, 0, sizeof(*out)); out->reset_flags = 0x20000000U; out->snapshot_fresh = 1U;
    for (i = 0; i < 12; ++i) out->stack_words[i] = 100U + i;
    out->stack_words[11] = UINT32_MAX;
    return 1U;
}
void bsp_hmi_uart_get_stats(hmi_uart_stats_t *out)
{
    memset(out, 0, sizeof(*out)); out->requests = normal_load ? now / 50U : 99U;
    if (normal_load) out->responses = out->requests;
}
void bsp_hmi_uart_get_diag(hmi_uart_diag_t *out)
{
    memset(out, 0, sizeof(*out)); out->boundaries = 55U;
    if (normal_load) {
        out->boundaries = now / 50U; out->queue_high_water = 192U; out->event_high_water = 24U;
        out->rx_last_ms = now - 20U; out->tx_last_ms = now - 15U;
        out->rx_epoch = now / 50U; out->tx_epoch = now / 50U;
        out->rx_armed = 1U; out->service_max_ms = 2U;
        out->response_delay_max_cycles = 672000U; out->parse_max_cycles = 42000U;
        out->capture_max_cycles = 8400U; out->quiet_elapsed_min_cycles = 168000U;
        out->raw_last_cycles = now * 168000U; out->tx_complete_cycles = (now - 15U) * 168000U;
    }
}
uint32_t bsp_hmi_uart_clock_hz(void) { return 168000000U; }
uint8_t debug_uart_try_write_frame(const uint8_t *data, uint16_t length, uint16_t reserve)
{
    assert(length > 0U && length < 256U && reserve == 512U);
    assert(data[length - 2U] == '\r' && data[length - 1U] == '\n');
    if (blocked) return 0U;
    if(reject_line && reject_remaining && strstr((const char *)data,reject_line)) {
        --reject_remaining;return 0U;
    }
    assert(output_bytes + length < sizeof(output));
    memcpy(output + output_bytes, data, length); output_bytes += length;
    output[output_bytes] = 0;
    ++output_count; second_bytes += length;
    if (strstr((const char *)data, " FIRST ")) ++first_writes;
    return 1U;
}
static void tick(unsigned ms)
{
    unsigned i;
    for (i = 0; i < ms; ++i) {
        if (now && now % 1000U == 0U) {
            if (second_bytes > max_second) max_second = second_bytes;
            assert(second_bytes <= 6000U); second_bytes = 0U;
        }
        app_hmi_link_diag_service(); ++now;
    }
}
static void reset(void)
{
    now = output_count = output_bytes = second_bytes = max_second = first_writes = 0U;
    blocked = normal_load = 0U; boot_id = 77U; output[0] = 0; memset(&status, 0, sizeof(status));
    reject_line=NULL;reject_remaining=0U;
    status.session = 123U; status.target = -751; status.output[0] = -749; status.output[1] = -750;
    memset(&snapshot_status, 0, sizeof(snapshot_status));
    memset(&lcd_status, 0, sizeof(lcd_status)); memset(&lcd_stats, 0, sizeof(lcd_stats));
    reject_acks = 0U;
    app_hmi_link_diag_init();
}
static app_control_stop_t make_stop(uint32_t event, uint16_t reason)
{
    app_control_stop_t stop;
    memset(&stop, 0, sizeof(stop)); stop.event = event; stop.owner = APP_CONTROL_OWNER_HMI;
    stop.requester = APP_CONTROL_OWNER_CAN; stop.reason = reason;
    stop.request_sequence = 91U; stop.command = 16U; stop.deceleration_mm_s2 = 500U;
    stop.started_ms = 123U; stop.generation = 42U; stop.result = 2U;
    return stop;
}
static void test_boot_status_memory(void)
{
    reset(); tick(1100U);
    assert(strstr(output, "BOOT build="));
    assert(strstr(output, "PREV type=0 valid=1"));
    assert(strstr(output, "PREV type=1 valid=1"));
    assert(strstr(output, "pc=08012345"));
    assert(strstr(output, " S 1,7B,0,0,0,320,0,FD11,FD13,FD12,0,0,0,0,0,0"));
    assert(strstr(output, "MEM free=777 min=666"));
    assert(strstr(output, "STACK bytes=400,404,408,412,416,420,424,428,432,436,440,NA"));
    assert(strstr(output, " UART 63,0,0,0,0,0,0,0,0"));
    assert(strstr(output, " TIMING "));
    assert(strstr(output, " QUIET 0,0,0,0"));
}
static void test_first_survives_full_fifo_and_uart_congestion(void)
{
    app_control_stop_t stop = make_stop(12U, 3U);
    unsigned i;
    reset(); blocked = 1U;
    for (i = 0; i < 40U; ++i) app_hmi_link_diag_record(APP_HMI_LINK_RX_REQUEST, i, 0, 0, 0, 0, 0);
    app_hmi_link_diag_stop(&stop, APP_HMI_LINK_STOP_FIRST);
    tick(25U); assert(output_bytes == 0U);
    stop.reason = 2U; stop.deceleration_mm_s2 = 600U;
    app_hmi_link_diag_stop(&stop, APP_HMI_LINK_STOP_UPGRADE);
    blocked = 0U; tick(1200U);
    assert(strstr(output, "FIRST at=123 owner=1 requester=5 reason=3 seq=91 cmd=16 decel=500 gen=42"));
    assert(first_writes == 1U);
    assert(strstr(output, "fifo_lost=10"));
    tick(1000U); assert(first_writes == 1U);
    stop.event = 13U;
    app_hmi_link_diag_stop(&stop, APP_HMI_LINK_STOP_FIRST);
    tick(2U); assert(first_writes == 2U);
}
static void test_summary_retries_failed_page(void)
{
    static const char *const names[]={" MEM "," STACK "," TIMING "};
    unsigned i;
    for(i=0U;i<sizeof(names)/sizeof(names[0]);++i) {
        reset();reject_line=names[i];reject_remaining=9U;tick(1200U);
        assert(reject_remaining==0U);
        assert(strstr(output,names[i]));
        assert(strstr(output," LOGGER "));
    }
}
static void test_screen_validation_duplicate_missing_and_backpressure(void)
{
    uint16_t words[24] = {0x4844U, 0x0100U, 0U, 7U};
    reset();
    assert(!app_hmi_link_diag_screen(words, 23U));
    words[0] = 0U; assert(!app_hmi_link_diag_screen(words, 24U)); words[0] = 0x4844U;
    words[1] = 0x0102U; assert(!app_hmi_link_diag_screen(words, 24U)); words[1] = 0x0100U;
    assert(app_hmi_link_diag_screen(words, 24U));
    assert(app_hmi_link_diag_screen(words, 24U));
    words[4] = 1U; assert(!app_hmi_link_diag_screen(words, 24U)); words[4] = 0U;
    words[3] = 8U; assert(!app_hmi_link_diag_screen(words, 24U)); words[3] = 7U;
    tick(20U);
    assert(strstr(output, " 7 SCREEN page=0 mask=1 incomplete=1"));
    words[1] = 0x0101U;
    assert(app_hmi_link_diag_screen(words, 24U)); tick(2U);
    assert(strstr(output, " 7 SCREEN page=1 mask=3 incomplete=0"));
    assert(app_hmi_link_diag_screen(words, 24U));
    words[1] = 0x0100U; words[3] = 8U;
    assert(app_hmi_link_diag_screen(words, 24U)); tick(2U);
    assert(strstr(output, " 8 SCREEN page=0 mask=1 incomplete=1"));
    /* All diagnostic operations leave the mock controller untouched. */
    assert(status.session == 123U && status.target == -751);
}
static void test_rate_limit(void)
{
    unsigned i;
    reset();
    for (i = 0; i < 4000U; ++i) {
        app_hmi_link_diag_record(APP_HMI_LINK_CONSUME, UINT32_MAX, UINT32_MAX, UINT32_MAX,
            UINT32_MAX, UINT32_MAX, UINT32_MAX);
        tick(1U);
    }
    assert(max_second > 4000U && max_second <= 6000U);
    assert(second_bytes <= 6000U);
    assert(strstr(output, " E3 FFFFFFFF"));
}
static void test_screen_reboot_reuses_id_only_after_complete_print(void)
{
    uint16_t words[24] = {0x4844U, 0x0100U, 0U, 1U};
    reset();
    assert(app_hmi_link_diag_screen(words, 24U)); tick(20U);
    words[4] = 1U;
    assert(!app_hmi_link_diag_screen(words, 24U)); /* Incomplete old pair. */
    words[1] = 0x0101U;
    assert(app_hmi_link_diag_screen(words, 24U));
    words[1] = 0x0100U;
    assert(!app_hmi_link_diag_screen(words, 24U)); /* Old page 1 not printed. */
    tick(2U);
    assert(app_hmi_link_diag_screen(words, 24U)); /* New generation page 0. */
    words[4] = 2U;
    assert(!app_hmi_link_diag_screen(words, 24U)); /* Still pending new pair. */
    words[4] = 1U; words[1] = 0x0101U;
    assert(app_hmi_link_diag_screen(words, 24U)); tick(2U);
    words[4] = 2U;
    assert(!app_hmi_link_diag_screen(words, 24U)); /* Changed page 1 never resets. */
    tick(1100U);
    assert(strstr(output, "screen_reused=1"));
    assert(strstr(output, "screen_conflict=4"));
}
static void test_normal_70hz_at_two_hours_no_fifo_loss(void)
{
    unsigned i, generated = 0U, emitted = 0U;
    const char *p;
    reset(); now = 7200000U; boot_id = 0xF1234567U; normal_load = 1U;
    status.session = 0x7D9ABC03U; status.state = 3U; status.flags = 0x0123U;
    status.last_sequence = 36000U; status.target_age_ms = 80U;
    status.stop.event = 123U; status.stop.reason = 2U; status.phase = 3U;
    app_hmi_link_diag_init();
    app_hmi_link_diag_test_seed_serial(504000U);
    lcd_status.revision = 1U; lcd_status.ready = 1U;
    snapshot_status.full_sequence = snapshot_status.critical_sequence = UINT32_MAX;
    snapshot_status.full_source_ms = snapshot_status.critical_source_ms = 7200000U;
    snapshot_status.last_copy_cycles = snapshot_status.max_copy_cycles = 10000U;
    snapshot_status.last_critical_cycles = snapshot_status.max_critical_cycles = 50000U;
    snapshot_status.last_full_cycles = snapshot_status.max_full_cycles = 7000000U;
    /* 20 transactions/s: 10 critical reads, 5 SPEEDs, 5 ordinary reads.
       RX + submit + local completion = 60, mailbox + consume = 10 events/s. */
    for (i = 0U; i < 60000U; ++i) {
        unsigned phase = i % 100U;
        uint8_t speed = ((i / 100U) & 1U) == 0U;
        uint32_t seq = 36000U + i / 200U;
        uint32_t cycles = now * 168000U;
        uint32_t start = phase < 50U ? 0x0460U : (speed ? 0x0100U : 0x0200U);
        uint32_t fc = phase < 50U || !speed ? 3U : 16U;
        uint32_t quantity = phase < 50U ? 18U : 24U;
        uint32_t length = fc == 3U ? 5U + quantity * 2U : 8U;
        uint32_t region = start == 0x0200U ? 5U : 6U;
        if (phase == 0U || phase == 50U) {
            app_hmi_link_diag_record(APP_HMI_LINK_RX_REQUEST, start, fc, quantity,
                cycles - 16800U, cycles, now); ++generated;
        } else if (speed && phase == 51U) {
            app_hmi_link_diag_record(APP_HMI_LINK_MAILBOX, status.session, seq, 0x21U,
                751U, 500U, now); ++generated;
        } else if (speed && phase == 52U) {
            app_hmi_link_diag_record(APP_HMI_LINK_CONSUME, status.session, seq, 0x21U,
                now - 1U, 1U, 0U); ++generated;
        } else if (phase == 3U || phase == 53U) {
            app_hmi_link_diag_record(APP_HMI_LINK_TX_SUBMIT, start, fc, length,
                1U, cycles - 33600U, cycles); ++generated;
        } else if (phase == 5U || phase == 55U) {
            app_hmi_link_diag_record(APP_HMI_LINK_TX_COMPLETE, (region << 16U) | length,
                now / 50U, now, cycles, 1U, 0U); ++generated;
        }
        status.last_sequence = seq; tick(1U);
        if (i && i % 1000U == 0U) {
            ++lcd_status.revision; lcd_status.at_ms = now;
            ++lcd_stats.errors; lcd_stats.last.timestamp_ms = now;
        }
    }
    tick(1100U); /* Also emit final logger counters after the load. */
    assert(generated == 4200U);
    p = output;
    while (p && *p) {
        unsigned long t, b, serial;
        unsigned kind;
        if (sscanf(p, "HLD %lx %lx %lx E%x", &t, &b, &serial, &kind) == 4) {
            ++emitted; assert(serial == 504000U + emitted); assert(b == boot_id); assert(t >= 7200000U);
            assert(kind == APP_HMI_LINK_RX_REQUEST || kind == APP_HMI_LINK_MAILBOX ||
                kind == APP_HMI_LINK_CONSUME || kind == APP_HMI_LINK_TX_SUBMIT ||
                kind == APP_HMI_LINK_TX_COMPLETE);
        }
        p = strchr(p, '\n');
        if (p) ++p;
    }
    { const char *q=output,*last=NULL; unsigned long skipped;
      while((q=strstr(q,"sampled="))!=NULL){last=q;q+=8;}
      assert(last);skipped=strtoul(last+8,NULL,10);
      assert(emitted>0U && emitted<generated && emitted+skipped==generated);
    }
    assert(!strstr(output, "fifo_lost=1"));
    assert(strstr(output, "fifo_lost=0 uart_drop=0"));
    assert(strstr(output, " SNAP seq="));
    assert(strstr(output, " LCD ui "));
    assert(max_second <= 6000U && second_bytes <= 6000U);
    printf("PASS normal 70 events/s for 60 s after 504000 prior events at uptime 2 h: %u/%u events, max %u B/s, successful-event sampling, no FIFO/UART loss\n",
        emitted, generated, max_second);
}
static void test_snapshot_and_lcd_pending_priority(void)
{
    app_control_stop_t stop = make_stop(19U, 2U);
    uint16_t words[24] = {0x4844U, 0x0100U, 0U, 99U};
    const char *first, *screen_line, *reject_line, *lcd_line;
    reset(); tick(20U); output_bytes = 0U; output[0] = 0;
    blocked = 1U;
    lcd_status.revision = 10U; lcd_status.at_ms = 100U;
    lcd_stats.errors = 1U; lcd_stats.first.reason = LCD_ERROR_TIMEOUT;
    lcd_stats.last = lcd_stats.first;
    snapshot_status.reject_pending = 1U;
    snapshot_status.reject.id = 7U; snapshot_status.reject.at_ms = 0x123U;
    snapshot_status.reject.start = 0x460U; snapshot_status.reject.quantity = 64U;
    snapshot_status.reject.reason = 4U; snapshot_status.reject.exception = 2U;
    snapshot_status.reject.cache_age_ms = UINT32_MAX;
    snapshot_status.reject.full_sequence = 0x1234U;
    snapshot_status.reject.critical_sequence = 0x5678U;
    app_hmi_link_diag_stop(&stop, APP_HMI_LINK_STOP_FIRST);
    assert(app_hmi_link_diag_screen(words, 24U));
    tick(20U); assert(!reject_acks && !output_bytes);
    lcd_status.revision = 11U; lcd_status.at_ms = 200U; lcd_stats.errors = 2U;
    tick(20U); blocked = 0U; tick(5000U);
    first = strstr(output, " FIRST "); screen_line = strstr(output, " SCREEN ");
    reject_line = strstr(output, " SNAP_REJECT "); lcd_line = strstr(output, " LCD ui ");
    assert(first && screen_line && reject_line && lcd_line);
    assert(first < screen_line && screen_line < reject_line && reject_line < lcd_line);
    assert(strstr(output, " A LCD ui at=100")); /* First frozen UI revision survives. */
    assert(strstr(output, " B LCD ui at=200")); /* Most recent change follows. */
    assert(reject_acks == 1U && !snapshot_status.reject_pending);
    assert(strstr(output, "why=4 ex=2 cache=FFFFFFFF"));
    assert(strstr(output, "full=1234 critical=5678"));
    assert(strstr(output, "merged=1"));
    /* Independent LCD line budget remains one accepted line per 1000 ms. */
    {
        const char *p = output;
        unsigned long previous = 0U;
        unsigned found = 0U;
        while (p && *p) {
            const char *end = strchr(p, '\n');
            const char *tag = strstr(p, " LCD ");
            if (tag && (!end || tag < end)) {
                unsigned long at;
                assert(sscanf(p, "HLD %lx", &at) == 1);
                if (found) assert(at - previous >= 1000U);
                previous = at; ++found;
            }
            p = end ? end + 1 : NULL;
        }
        assert(found >= 5U);
    }
    reset(); tick(4000U); assert(!strstr(output, " LCD "));
    puts("PASS snapshot/LCD immutable pending, blocked emit retry, priority, merge and one line/s");
}

static void test_slow_deadline_freeze_and_wrap(void)
{
    app_hmi_link_transaction_t t;
    app_control_stop_t stop = make_stop(30U, 2U);
    uint16_t words[24] = {0x4844U, 0x0100U, 0U, 91U};
    const char *first, *screen_line, *p0, *p1, *ordinary;
    reset(); tick(20U); output_bytes = 0U; output[0] = 0;
    memset(&t, 0xFF, sizeof(t));
    t.capture = UINT32_MAX - 100U;
    t.completed = t.capture + 6720000U - 1U; /* <40ms: no warning */
    app_hmi_link_diag_complete(&t); tick(1U);
    assert(!strstr(output, " E C ") && !strstr(output, " SLOW "));
    t.completed = t.capture + 6720000U; /* exactly40ms */
    app_hmi_link_diag_complete(&t); tick(1U);
    assert(strstr(output, " EC ") && !strstr(output, " SLOW "));
    output_bytes = 0U; output[0] = 0;
    blocked = 1U;
    t.completed = t.capture + 8400000U; /* exactly50ms, DWT wrap */
    app_hmi_link_diag_complete(&t);
    tick(2U); assert(!output_bytes);
    t.id = 2U; t.start = 0x50U; t.quantity = 32U;
    app_hmi_link_diag_complete(&t); /* Must not overwrite first pending record. */
    app_hmi_link_diag_stop(&stop, APP_HMI_LINK_STOP_FIRST);
    assert(app_hmi_link_diag_screen(words, 24U));
    blocked = 0U; tick(1U); /* FIRST */
    tick(1U); /* SCREEN */
    tick(1U); /* SLOW p0 */
    assert(strstr(output, "FFFFFFFF SLOW p=0") && !strstr(output, " SLOW p=1"));
    blocked = 1U;
    t.id = 3U; app_hmi_link_diag_complete(&t); tick(5U);
    assert(!strstr(output, " SLOW p=1"));
    blocked = 0U; tick(40U);
    first = strstr(output, " FIRST "); screen_line = strstr(output, " SCREEN ");
    p0 = strstr(output, "FFFFFFFF SLOW p=0"); p1 = strstr(output, "FFFFFFFF SLOW p=1");
    ordinary = strstr(output, " EC ");
    assert(first && screen_line && p0 && p1 && ordinary);
    assert(first < screen_line && screen_line < p0 && p0 < p1 && p1 < ordinary);
    assert(strstr(p0, "req=FFFF/FFFF fc=FFFF len=FFFF"));
    assert(strstr(p1, "total=802C80 hz=A037A00 warn=4 deadline=3 merged=2"));
    assert(!strstr(output, " 2 SLOW ") && !strstr(output, " 3 SLOW "));
    t.id = 4U; app_hmi_link_diag_complete(&t); tick(10U);
    assert(strstr(output, " 4 SLOW p=0") && strstr(output, " 4 SLOW p=1"));
    assert(strstr(output, "full_off=1") || now < 5000U);
    puts("PASS slow transactions: 40/50ms boundaries, wrap, immutable two-page retry, priority and merge");
}
int main(void)
{
    test_boot_status_memory();
    test_first_survives_full_fifo_and_uart_congestion();
    test_summary_retries_failed_page();
    test_screen_validation_duplicate_missing_and_backpressure();
    test_screen_reboot_reuses_id_only_after_complete_print();
    test_rate_limit();
    test_snapshot_and_lcd_pending_priority();
    test_slow_deadline_freeze_and_wrap();
    test_normal_70hz_at_two_hours_no_fifo_loss();
    puts("PASS HMI link diagnostics: first cause, FIFO/UART congestion, rate budget, screen pages, memory and boot");
    return 0;
}
#else
int main(void)
{
    uint16_t page[24] = {0};
    app_hmi_link_diag_init(); app_hmi_link_diag_service();
    app_hmi_link_diag_record(APP_HMI_LINK_RX_REQUEST, 1, 2, 3, 4, 5, 6);
    app_hmi_link_diag_stop(0, APP_HMI_LINK_STOP_FIRST);
    assert(!app_hmi_link_diag_screen(page, 24U));
    puts("PASS HMI link diagnostics disabled: no runtime dependencies/output");
    return 0;
}
#endif
