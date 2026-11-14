/* Production snapshot/parser/UART with controllable source/encoder preemption. */
#include "app_hmi_state.h"
static uint8_t delayed_window_build(const hmi_state_input_t *in,
    const hmi_alarm_state_t *alarm, uint32_t read_ms, uint16_t start,
    uint16_t quantity, uint16_t *registers);
static void observed_alarm(const hmi_state_input_t *in, hmi_alarm_state_t *alarm);
#define app_hmi_state_build_read_window delayed_window_build
#define app_hmi_state_alarm_observe observed_alarm
#define TEST_HMI_READ_SOURCE_HOOK
#define main existing_hmi_integration_main
#include "test_hmi_integration.c"
#undef main
#undef app_system_snapshot_read_with_meta
#undef app_hmi_state_build_read_window
#undef app_hmi_state_alarm_observe

static uint32_t window_delay_us, copy_delay_us, window_calls, source_calls, alarm_calls;
static uint8_t require_response_before_alarm, saw_response_before_alarm;
static uint8_t publish_after_failed_read, request_during_alarm;
static uint8_t test_hmi_source_read(app_system_snapshot_t *snapshot,
    uint32_t *source_ms, uint32_t *sequence, uint32_t *source_age_ms,
    uint8_t *initialized)
{
    uint8_t valid;
    source_calls++;
    valid = app_system_snapshot_read_with_meta(snapshot,
        source_ms, sequence, source_age_ms, initialized);
    if (!valid && publish_after_failed_read)
    {
        publish_after_failed_read = 0U;
        app_system_snapshot_service();
    }
    if (copy_delay_us) advance(copy_delay_us);
    return valid;
}
static uint8_t delayed_window_build(const hmi_state_input_t *in,
    const hmi_alarm_state_t *alarm, uint32_t read_ms, uint16_t start,
    uint16_t quantity, uint16_t *registers)
{
    uint8_t valid;
    window_calls++;
    valid = app_hmi_state_build_read_window(in, alarm, read_ms, start, quantity, registers);
    if (window_delay_us) advance(window_delay_us);
    return valid;
}
static void observed_alarm(const hmi_state_input_t *in, hmi_alarm_state_t *alarm)
{
    uint8_t frame[8];
    alarm_calls++;
    if (require_response_before_alarm)
    {
        assert(response_size > 5U && response[1] == 3U);
        saw_response_before_alarm = 1U;
    }
    app_hmi_state_alarm_observe(in, alarm);
    if (request_during_alarm)
    {
        request_during_alarm = 0U;
        request(frame, 0x0460U, 64U); inject(frame, 8U); boundary();
    }
}
static void clock_at(uint32_t ms)
{
    tick = ms; simulated_us = (uint64_t)ms * 1000U;
}
static void start_at(uint32_t ms)
{
    window_delay_us = copy_delay_us = 0U;
    publish_after_failed_read = request_during_alarm = 0U;
    require_response_before_alarm = saw_response_before_alarm = 0U;
    fresh(); clock_at(ms); app_system_snapshot_init(); app_hmi_snapshot_init(identity_id);
    window_calls = source_calls = alarm_calls = 0U;
}
static const uint16_t window_start[] = {
    0x0000U, 0x0050U, 0x0070U, 0x0090U, 0x00B0U,
    APP_HMI_METADATA_START, APP_HMI_DETAIL_START, HMI_CONTROL_REGISTER_START,
    0x0004U, 0x002BU, 0x005BU, 0x0073U, 0x0097U, 0x00B1U, 0x00CDU,
    0x0205U, 0x035FU, 0x03E3U, 0x0463U, 0x047AU, 0x04BFU
};
static const uint16_t window_qty[] = {
    64U, 32U, 32U, 32U, 32U, 24U, 32U, 96U,
    1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 125U, 125U, 2U, 1U, 1U
};

static void all_window_freshness_test(void)
{
    uint16_t values[125];
    uint32_t origin, i, j, expected;
    app_hmi_snapshot_diag_t diag;
    app_hmi_snapshot_read_meta_t meta;
    hmi_alarm_state_t alarm_before;
    for (j = 0U; j < 2U; j++)
    {
        origin = j ? UINT32_MAX - 50U : 1000U;
        for (i = 0U; i < sizeof(window_start) / sizeof(window_start[0]); i++)
        {
            start_at(origin); expected = sys_buffer[sys_active].sequence;
            memcpy(&alarm_before, &s_alarm, sizeof(s_alarm));
            clock_at(origin + 100U);
            assert(app_hmi_snapshot_read(window_start[i], window_qty[i], values));
            assert(source_calls == 1U && window_calls == 1U && !alarm_calls);
            assert(app_hmi_snapshot_last_read(&meta));
            assert(meta.source_ms == origin && meta.sequence == expected);
            assert(app_hmi_snapshot_publish_sequence() == expected);
            assert(memcmp(&alarm_before, &s_alarm, sizeof(s_alarm)) == 0);
            clock_at(origin + 101U);
            assert(!app_hmi_snapshot_read(window_start[i], window_qty[i], values));
            assert(source_calls == 2U && window_calls == 1U);
            assert(!app_hmi_snapshot_last_read(&meta));
            app_hmi_snapshot_diag_read(&diag);
            assert(diag.reject.reason == APP_HMI_SNAPSHOT_SOURCE_EXPIRED);
            assert(diag.reject.source_age_ms == 101U);
            assert(diag.full_builds == 0U && diag.last_full_cycles == 0U &&
                diag.max_full_cycles == 0U && diag.full_source_ms == 0U && diag.full_sequence == 0U);
            assert(diag.critical_builds ==
                (window_start[i] >= HMI_CONTROL_REGISTER_START ? 1U : 0U));
            assert(diag.ordinary_builds ==
                (window_start[i] >= HMI_CONTROL_REGISTER_START ? 0U : 1U));
        }
    }
    /* A new ordinary read publishes its own source sequence, independent of control reads. */
    start_at(2000U); assert(app_hmi_snapshot_read(0x0460U, 64U, values));
    expected = app_hmi_snapshot_publish_sequence();
    clock_at(2010U); app_system_snapshot_service();
    assert(app_hmi_snapshot_read(0x0200U, 24U, values));
    assert(app_hmi_snapshot_publish_sequence() > expected);
    assert(app_hmi_snapshot_last_read(&meta) && meta.source_ms == 2010U);
    expected = app_hmi_snapshot_publish_sequence();
    assert(app_hmi_snapshot_read(APP_HMI_RESULT_START, APP_HMI_RESULT_REGISTERS, values));
    assert(!app_hmi_snapshot_last_read(&meta));
    assert(app_hmi_snapshot_publish_sequence() == expected);
    puts("PASS all state windows/subreads/max125: one source copy, 100/101ms normal/wrap, pure alarms, no full cache, latest successful sequence");
}
static void rejection_test(void)
{
    uint16_t values[96];
    app_hmi_snapshot_diag_t first, after;
    app_hmi_snapshot_read_meta_t meta;
    uint32_t calls;
    start_at(2000U); clock_at(2101U);
    assert(!app_hmi_snapshot_read(0x0460U, 64U, values));
    app_hmi_snapshot_diag_read(&first);
    assert(first.reject_pending && first.reject.reason == APP_HMI_SNAPSHOT_SOURCE_EXPIRED);
    assert(first.reject.source_age_ms == 101U && first.reject.encode_cycles == 0U);
    calls = source_calls;
    assert(!app_hmi_snapshot_read(0x04BFU, 2U, values));
    app_hmi_snapshot_diag_read(&after);
    assert(after.merged == 1U && memcmp(&after.reject, &first.reject, sizeof(first.reject)) == 0);
    app_hmi_snapshot_diag_ack_reject(first.reject.id + 1U);
    app_hmi_snapshot_diag_read(&after); assert(after.reject_pending);
    app_hmi_snapshot_diag_ack_reject(first.reject.id);
    assert(!app_hmi_snapshot_read(0x0460U, 0U, values));
    app_hmi_snapshot_diag_read(&after);
    assert(after.reject.reason == APP_HMI_SNAPSHOT_BAD_PARAMETER && after.reject.id > first.reject.id);
    app_hmi_snapshot_diag_ack_reject(after.reject.id);
    assert(!app_hmi_snapshot_read(0x04BFU, 2U, values));
    app_hmi_snapshot_diag_read(&after); assert(after.reject.reason == APP_HMI_SNAPSHOT_BAD_RANGE);
    assert(!app_hmi_snapshot_read(0x0200U, 24U, NULL));
    assert(source_calls == calls && !window_calls && !app_hmi_snapshot_last_read(&meta));
    assert(app_hmi_snapshot_read(APP_HMI_RESULT_START, APP_HMI_RESULT_REGISTERS, values));
    assert(!unsafe_calls && !mailbox_calls);
    puts("PASS invalid requests bypass copy/encoder; sticky first rejection and explicit ACK; result area independent");
}
static void failed_read_publication_test(void)
{
    uint16_t values[96];
    app_hmi_snapshot_diag_t diag;
    uint32_t old_sequence;
    start_at(4000U); old_sequence = sys_buffer[sys_active].sequence;
    clock_at(4101U); publish_after_failed_read = 1U;
    assert(!app_hmi_snapshot_read(0x0460U, 64U, values));
    app_hmi_snapshot_diag_read(&diag);
    assert(source_calls == 1U && !window_calls && sys_buffer[sys_active].now_ms == 4101U);
    assert(diag.reject.reason == APP_HMI_SNAPSHOT_SOURCE_EXPIRED);
    assert(diag.reject.source_age_ms == 101U && diag.reject.source_sequence == old_sequence);
    app_hmi_snapshot_diag_ack_reject(diag.reject.id);
    sys_initialized = 0U; publish_after_failed_read = 1U;
    assert(!app_hmi_snapshot_read(0x0200U, 24U, values));
    app_hmi_snapshot_diag_read(&diag);
    assert(source_calls == 2U && !window_calls && sys_initialized);
    assert(diag.reject.reason == APP_HMI_SNAPSHOT_NO_SOURCE);
    assert(diag.reject.source_age_ms == UINT32_MAX);
    puts("PASS atomic failed-copy evidence survives immediate Health publication; no retry/substitution");
}
static void preemption_test(void)
{
    uint16_t values[125];
    uint32_t i, expected;
    app_hmi_snapshot_diag_t diag;
    app_hmi_snapshot_read_meta_t meta;
    for (i = 0U; i < sizeof(window_start) / sizeof(window_start[0]); i++)
    {
        start_at(2500U); expected = sys_buffer[sys_active].sequence;
        clock_at(2599U); copy_delay_us = 1000U;
        assert(app_hmi_snapshot_read(window_start[i], window_qty[i], values));
        copy_delay_us = 0U;
        assert(source_calls == 1U && window_calls == 1U);
        assert(app_hmi_snapshot_last_read(&meta) && meta.sequence == expected && meta.source_ms == 2500U);
        assert(sys_buffer[sys_active].sequence > expected && app_hmi_snapshot_publish_sequence() == expected);
        start_at(2750U); expected = sys_buffer[sys_active].sequence;
        clock_at(2849U); window_delay_us = 1000U;
        assert(app_hmi_snapshot_read(window_start[i], window_qty[i], values));
        window_delay_us = 0U;
        assert(source_calls == 1U && window_calls == 1U);
        assert(app_hmi_snapshot_last_read(&meta) && meta.sequence == expected && meta.encode_cycles == 1000U);
        assert(sys_buffer[sys_active].sequence > expected && app_hmi_snapshot_publish_sequence() == expected);
        start_at(3000U); expected = sys_buffer[sys_active].sequence;
        clock_at(3099U); copy_delay_us = 2000U;
        assert(!app_hmi_snapshot_read(window_start[i], window_qty[i], values));
        copy_delay_us = 0U; app_hmi_snapshot_diag_read(&diag);
        assert(diag.reject.reason == APP_HMI_SNAPSHOT_SOURCE_EXPIRED);
        assert(diag.reject.age_before_ms == 101U && diag.reject.encode_cycles == 0U);
        assert(source_calls == 1U && !window_calls);
        assert(app_hmi_snapshot_last_read(&meta) && meta.sequence == expected && meta.source_ms == 3000U);
        start_at(5000U); expected = sys_buffer[sys_active].sequence;
        clock_at(5099U); window_delay_us = 2000U;
        memset(values, 0xA5, sizeof(values));
        assert(!app_hmi_snapshot_read(window_start[i], window_qty[i], values));
        window_delay_us = 0U; app_hmi_snapshot_diag_read(&diag);
        assert(diag.reject.reason == APP_HMI_SNAPSHOT_ENCODE_EXPIRED);
        assert(diag.reject.age_before_ms == 99U && diag.reject.age_after_ms == 101U);
        assert(diag.reject.encode_cycles == 2000U && values[0] == 0xA5A5U);
        assert(source_calls == 1U && window_calls == 1U);
        assert(app_hmi_snapshot_last_read(&meta) && meta.sequence == expected && meta.source_ms == 5000U);
        assert(meta.encode_cycles == 2000U && app_hmi_snapshot_publish_sequence() == 0U);
        assert(sys_buffer[sys_active].sequence > expected);
        assert(app_hmi_snapshot_read(window_start[i], window_qty[i], values));
        assert(app_hmi_snapshot_last_read(&meta) && meta.source_ms == 5101U);
    }
    puts("PASS all windows: postcopy and postencode expiry, copied metadata retained, response withheld, no retry/new-source substitution");
}
static void alarm_maintenance_test(void)
{
    uint16_t values[96];
    app_hmi_snapshot_diag_t diag;
    app_hmi_snapshot_read_meta_t meta;
    start_at(6000U);
    assert(!source_calls && !window_calls && !alarm_calls);
    assert(app_hmi_snapshot_read(0x0200U, 24U, values));
    assert(app_hmi_snapshot_last_read(&meta));
    app_hmi_snapshot_service();
    assert(alarm_calls == 1U && !app_hmi_snapshot_last_read(&meta));
    app_hmi_snapshot_service(); assert(alarm_calls == 1U);
    clock_at(6049U); app_hmi_snapshot_service(); assert(alarm_calls == 1U);
    clock_at(6050U); app_hmi_snapshot_service(); assert(alarm_calls == 2U);
    clock_at(7000U); app_system_snapshot_service(); app_hmi_snapshot_service();
    assert(alarm_calls == 3U); app_hmi_snapshot_service(); assert(alarm_calls == 3U);
    app_hmi_snapshot_diag_read(&diag);
    assert(!diag.full_builds && !diag.last_full_cycles && !diag.max_full_cycles);
    puts("PASS idle maintenance: one alarm observation per50ms, no catch-up/full builder or transaction metadata reuse");
}
static void request_priority_test(void)
{
    uint8_t frame[8];
    uint32_t calls;
    start_at(8000U); advance(60000U); response_size = 0U;
    request(frame, 0x0460U, 64U); inject(frame, 8U); boundary();
    require_response_before_alarm = 1U; saw_response_before_alarm = 0U;
    app_hmi_service(); require_response_before_alarm = 0U;
    assert(saw_response_before_alarm && response_size == 133U); complete();
    advance(60000U); calls = alarm_calls;
    request(frame, 0x0460U, 64U); inject(frame, 4U); idle(); app_hmi_service();
    assert(s_length == 4U && alarm_calls == calls);
    inject(frame + 4U, 4U); flush(); assert(response_size == 133U); complete();
    start_at(9000U); request_during_alarm = 1U;
    app_hmi_service(); assert(!response_size && app_hmi_work_pending());
    app_hmi_service(); assert(response_size == 133U && !app_hmi_work_pending()); complete();
    assert(alarm_calls == 1U);
    puts("PASS queued/partial request priority; request arriving during bounded maintenance survives for immediate next service");
}
static void fifo_multiple_service_test(void)
{
    uint8_t frame[8];
    start_at(10000U);
    request(frame, 0x0460U, 64U); inject(frame, 8U); boundary();
    request(frame, 0x0200U, 24U); inject(frame, 8U); boundary();
    window_delay_us = 1000U;
    app_hmi_service();
    assert(s_stats.requests == 1U && response_size == 133U && app_hmi_work_pending());
    assert(!s_stats.tx_busy && s_tx_active); complete();
    app_hmi_service();
    assert(s_stats.requests == 2U && response_size == 53U && !app_hmi_work_pending());
    assert(!s_stats.tx_busy && !s_diag.tx_dropped); complete();
    window_delay_us = 0U;
    puts("PASS 200us budget preserves queued request FIFO across service calls without extra sleep");
}
static void tx_completion_test(void)
{
    uint8_t frame[8];
    uint32_t captured, expected, saved;
    start_at(12000U); request(frame, 0x0460U, 64U); inject(frame, 8U); boundary();
    app_hmi_service(); captured = s_tx_request_cycles;
    advance(12000U); expected = test_dwt.CYCCNT - captured; complete();
    assert(s_diag.response_complete_last_cycles == expected);
    assert(s_diag.response_complete_max_cycles == expected);
    start_at(13000U); test_dwt.CYCCNT = UINT32_MAX - 3000U;
    request(frame, 0x0460U, 64U); inject(frame, 8U); boundary(); app_hmi_service();
    captured = s_tx_request_cycles; advance(12000U);
    expected = test_dwt.CYCCNT - captured; complete();
    assert(s_diag.response_complete_last_cycles == expected && expected < 50000U);
    saved = s_diag.response_complete_last_cycles;
    request(frame, 0x0460U, 64U); inject(frame, 8U); flush();
    bsp_rx_generation++; advance(12000U); complete();
    assert(s_diag.response_complete_last_cycles == saved);
    puts("PASS last-byte to UART TC timing, DWT wrap and recovery generation isolation");
}
int main(void)
{
    assert(sizeof(s_registers) == 250U);
    SystemCoreClock = 1000000U;
    all_window_freshness_test(); rejection_test(); failed_read_publication_test(); preemption_test();
    alarm_maintenance_test(); request_priority_test(); fifo_multiple_service_test(); tx_completion_test();
    printf("PASS snapshot diag sizeof=%u, alarm sizeof=%u; no new task/buffer/allocation\n",
        (unsigned)sizeof(app_hmi_snapshot_diag_t), (unsigned)sizeof(hmi_alarm_state_t));
    return 0;
}
