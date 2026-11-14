#define main can_app_regression_main
#include "test_can_app.c"
#undef main
#include "can_upper_protocol.h"

static uint32_t pending_cookie, completed[96], last_completed[96], maximum_gap[96];
static uint32_t wire_frames, maximum_snapshot_age, snapshot_complete_at, snapshot_source_age;
static can_protocol_reassembly_t receiver;
static bool snapshot_seen;
static UpperProtocol_t consumer;
static uint32_t dropped_frames;
static bool inject_loss, pending_success;

static bool complete_send(void *context, uint16_t id, const uint8_t data[8],
    uint32_t cookie, uint32_t deadline)
{
    (void)context; (void)deadline;
    pending_success = !inject_loss || wire_frames % 997U != 996U;
    if (!pending_success) ++dropped_frames;
    if (pending_success) Upper_Receive(&consumer, id, 8U, data, tick, tick);
    if (pending_success && id >= 0x300U && id <= 0x302U &&
        can_protocol_receive_record(&receiver, (uint16_t)(id + 0x10U), data, 1U, tick) == CAN_PROTOCOL_COMPLETE && receiver.record.type == 0x25U)
    {
        snapshot_source_age = can_protocol_read_u32(receiver.record.payload + 100U) + tick - s_link.record_started_ms;
        snapshot_complete_at = tick; snapshot_seen = true;
    }
    assert(pending_cookie == 0U);
    pending_cookie = cookie;
    wire_frames++;
    return true;
}

static void simulate(unsigned step, bool loss)
{
    uint32_t now, elapsed_us;
    unsigned type;
    can_link_callbacks_t callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.send = complete_send;
    callbacks.build_record = build_record;
    memset(completed, 0, sizeof(completed));
    memset(last_completed, 0, sizeof(last_completed));
    memset(maximum_gap, 0, sizeof(maximum_gap));
    memset(s_basic_due, 0, sizeof(s_basic_due));
    memset(s_record_due, 0, sizeof(s_record_due));
    memset(&s_input, 0, sizeof(s_input));
    wire_frames = pending_cookie = maximum_snapshot_age = dropped_frames = 0U;
    inject_loss = loss;
    Upper_Init(&consumer, 7U, NULL, NULL, NULL);
    consumer.snapshot.state = UPPER_READY;
    consumer.snapshot.lower_boot_id = 1U; consumer.snapshot.link_token = 1U;
    consumer.snapshot.confirm_at = 1000U;
    snapshot_seen = false; memset(&receiver, 0, sizeof(receiver));
    can_link_init(&s_link, &callbacks, 1U, 1U, 100U, 1U, 0U, 1000U, 1000U);
    s_link.phase = CAN_LINK_READY;
    s_link.token = 1U;
    s_link.generation = 88U;
    s_link.next_boot_ms = 1000U;
    s_input.source_valid = true;
    s_input.boot_id = 1U; s_input.upper_boot_id = 7U; s_input.token = 1U;
    s_snapshot_supported = true;
    s_input.selected = 3U;
    s_input.life = 4U;
    s_input.stop.event = 1U;
    for (elapsed_us = 0U; elapsed_us < 20000000U; elapsed_us += step)
    {
        now = 1000U + elapsed_us / 1000U;
        tick = now;
        s_input.now_ms = now; s_input.snapshot_ms = s_input.sample_tick = now - 10U;
        if (snapshot_seen)
        {
            uint32_t age = snapshot_source_age + now - snapshot_complete_at;
            if (age > maximum_snapshot_age) maximum_snapshot_age = age;
            assert(age < 300U);
        }
        if (consumer.snapshot.snapshot_valid)
            assert(Upper_RecordFresh(&consumer.snapshot, &consumer.snapshot.records[16], now));
        s_input.snapshot_sequence++;
        s_link.last_heartbeat_ms = now;
        s_link.heartbeat_seen = true;
        schedule(now);
        can_link_service(&s_link, now);
        if (pending_cookie != 0U)
        {
            uint32_t cookie = pending_cookie;
            bool record = s_link.record_pending && s_link.record_cookie == cookie;
            type = s_link.record.type;
            pending_cookie = 0U;
            can_link_tx_result(&s_link, cookie, pending_success, now);
            if (record && !s_link.record_active)
            {
                if (last_completed[type] && now - last_completed[type] > maximum_gap[type])
                    maximum_gap[type] = now - last_completed[type];
                last_completed[type] = now;
                completed[type]++;
            }
        }
    }
    printf("CAN transport step=%uus loss=%u frames/s=%lu failures=%lu\n", step, loss,
        (unsigned long)(wire_frames / 20U), (unsigned long)s_link.record_failed);
    for (type = 0U; type < 96U; type++)
        if (completed[type] || type == 0x25U || type == 0x26U)
            printf(" type=%02X count=%lu max_gap=%lums\n", type,
                (unsigned long)completed[type], (unsigned long)maximum_gap[type]);
    printf(" snapshot receiver total age max=%lums\n", (unsigned long)maximum_snapshot_age);
    printf(" F103 decoded records=%lu rejected=%lu record_errors=%lu retried_frames=%lu\n",
        (unsigned long)consumer.snapshot.records_received, (unsigned long)consumer.snapshot.rejected,
        (unsigned long)consumer.snapshot.record_errors, (unsigned long)dropped_frames);
    assert(snapshot_seen && consumer.snapshot.snapshot_valid && s_link.record_failed == 0U);
    assert(consumer.snapshot.rejected == 0U && consumer.snapshot.record_errors == 0U);
    if (loss) assert(dropped_frames > 0U);
    assert(!Upper_RecordFresh(&consumer.snapshot, &consumer.snapshot.records[16], tick + 300U));
    assert(completed[0x25U] > 0U && completed[0x26U] > 0U);
    assert(completed[0x23U] > 0U && completed[0x53U] > 0U);
    if (step <= 300U) {
        assert(maximum_gap[0x25U] <= 100U);
        assert(maximum_gap[0x11U] <= 100U);
        assert(maximum_gap[0x23U] <= 200U);
        assert(maximum_gap[0x20U] <= 200U);
        assert(maximum_gap[0x21U] <= 200U);
        assert(maximum_gap[0x22U] <= 200U);
        assert(maximum_gap[0x26U] <= 500U);
    } else {
        assert(maximum_gap[0x11U] <= 200U);
        assert(maximum_gap[0x13U] <= 200U);
        assert(maximum_gap[0x25U] <= 200U);
        assert(maximum_gap[0x26U] < 5000U);
    }
}

int main(void)
{
    s_clock_tick = 100U; s_monotonic_ms = 100U;
    assert(monotonic(101U) == 101000U);
    assert(monotonic(100U) == 101000U);
    assert(monotonic(102U) == 102000U);
    s_clock_tick = 0xFFFFFFF0U; s_monotonic_ms = 0xFFFFFFF0ULL;
    assert(monotonic(20U) == (0x100000000ULL + 20U) * 1000U);
    puts("PASS monotonic rejects old samples and extends real wrap");
    simulate(300U, false);
    simulate(2000U, false);
    simulate(300U, true);
    return 0;
}
