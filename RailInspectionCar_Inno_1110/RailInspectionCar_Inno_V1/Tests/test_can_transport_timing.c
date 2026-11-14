/* Production lower record transport + production F103 reassembly/control.
 * The host bus completes at a later simulated millisecond, never inline in send.
 * Service gaps are injected scheduling conditions, not field measurements. */
#include "can_link.h"
#include "can_upper_protocol.h"
#include "can_upper_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static can_link_t lower;
static UpperProtocol_t upper;
static UpperControl_t control;
static struct { uint16_t id; uint8_t data[8]; uint32_t cookie, due; uint8_t busy, complete; } wire;
static uint32_t clock_ms, stop_commands, successful_frames, record_commits;
static uint32_t maximum_control_age, maximum_axis_age, maximum_record_duration;
static uint8_t heartbeat_sequence;

static uint8_t upper_send(void *context, uint16_t id, const uint8_t data[8])
{
    (void)context;
    if (id == 0x110U && data[1] == 5U && Upper_ReadU16(data + 2U) == 0U) ++stop_commands;
    if (id == 0x111U) UpperControl_TxResult(&control, id, data, 1U, clock_ms);
    return 1U;
}

static bool lower_send(void *context, uint16_t id, const uint8_t data[8], uint32_t cookie, uint32_t deadline)
{
    (void)context;
    assert((int32_t)(deadline - clock_ms) > 0);
    if (wire.busy) return false;
    wire.id = id; memcpy(wire.data, data, 8U); wire.cookie = cookie;
    wire.due = clock_ms + 1U; wire.busy = 1U;
    return true;
}

static bool build_record(void *context, uint8_t type, can_protocol_record_t *record, uint32_t now)
{
    (void)context;
    record->length = type == 0x11U || type == 0x13U ? 40U : 104U;
    if (type != 0x11U && type != 0x13U && type != 0x22U && type != 0x25U) return false;
    memset(record->payload, 0, sizeof(record->payload));
    if (type == 0x11U) {
        can_protocol_write_u32(record->payload, 1U);
        can_protocol_write_u16(record->payload + 4U, 23U);
        record->payload[6] = 4U; record->payload[7] = 3U; record->payload[10] = 3U;
        can_protocol_write_u32(record->payload + 12U, 1036U);
        can_protocol_write_u32(record->payload + 16U, 250U);
        can_protocol_write_u16(record->payload + 22U, 50U);
        can_protocol_write_u32(record->payload + 36U, now);
    } else if (type == 0x25U) {
        can_protocol_write_u32(record->payload, now | 1U);
        can_protocol_write_u32(record->payload + 4U, now);
        can_protocol_write_u32(record->payload + 8U, 42U);
        can_protocol_write_u32(record->payload + 12U, 1U);
    }
    return true;
}

static void completed(void *context, const can_protocol_record_t *record, uint32_t now)
{
    uint32_t duration = now - lower.record_started_ms;
    (void)context; (void)record;
    ++record_commits;
    if (duration > maximum_record_duration) maximum_record_duration = duration;
}

static void capture_wire(void)
{
    if (!wire.busy || wire.complete || (int32_t)(clock_ms - wire.due) < 0) return;
    wire.complete = 1U;
    Upper_Receive(&upper, wire.id, 8U, wire.data, wire.due, clock_ms);
    ++successful_frames;
}

static void process_result(void)
{
    uint32_t cookie, completed_ms;
    if (!wire.complete) return;
    cookie = wire.cookie; completed_ms = wire.due; wire.busy = wire.complete = 0U;
    can_link_tx_result(&lower, cookie, true, completed_ms);
}

static void offer_basics(uint8_t include_axes)
{
    uint8_t data[8] = {1U, 0U, 4U, 3U, 100U, 0U, 31U, 3U};
    data[1] = ++heartbeat_sequence;
    assert(can_link_offer_basic(&lower, 0x185U, data, clock_ms + 100U));
    if (include_axes) {
        memset(data, 0, sizeof(data)); data[0] = 31U; data[1] = 2U;
        can_protocol_write_u16(data + 2U, 100U); can_protocol_write_u16(data + 6U, 100U);
        assert(can_link_offer_basic(&lower, 0x186U, data, clock_ms + 100U));
        assert(can_link_offer_basic(&lower, 0x187U, data, clock_ms + 100U));
    }
}

static void init(uint32_t start)
{
    can_link_callbacks_t callbacks;
    UpperRecord_t *record;
    unsigned axis;
    memset(&callbacks, 0, sizeof(callbacks)); callbacks.send = lower_send;
    callbacks.build_record = build_record; callbacks.record_complete = completed;
    memset(&wire, 0, sizeof(wire));
    stop_commands = successful_frames = record_commits = 0U;
    maximum_control_age = maximum_axis_age = maximum_record_duration = 0U; heartbeat_sequence = 0U;
    clock_ms = start;
    can_link_init(&lower, &callbacks, 42U, 100U, 200U, 7U, 7U, 1000U, start);
    lower.phase = CAN_LINK_READY; lower.token = 100U; lower.upper_boot_id = 84U;
    lower.heartbeat_seen = true; lower.last_heartbeat_ms = start;
    lower.next_boot_ms = start + 500U;
    Upper_Init(&upper, 84U, upper_send, 0, 0); UpperControl_Init(&control, upper_send, 0);
    upper.snapshot.state = UPPER_READY; upper.snapshot.lower_boot_id = 42U; upper.snapshot.link_token = 100U;
    upper.snapshot.heartbeat_seen = 1U; upper.snapshot.heartbeat_at = start;
    upper.snapshot.control_source = 4U; upper.snapshot.lifecycle = 3U; upper.snapshot.axes = 3U;
    upper.snapshot.basic[5].valid = 1U; upper.snapshot.basic[5].timestamp = start;
    for (axis = 0U; axis < 2U; ++axis) {
        UpperBasic_t *basic = &upper.snapshot.basic[6U + axis];
        basic->valid = 1U; basic->timestamp = start; basic->data[0] = 31U; basic->data[1] = 2U;
    }
    /* Seed one valid scene at the beginning; every later refresh goes over the
     * asynchronous wire and the production CRC/reassembly path. */
    record = &upper.snapshot.records[2]; record->valid = 1U; record->type = 0x11U;
    record->length = 40U; record->timestamp = start;
    can_protocol_write_u32(record->payload, 1U); can_protocol_write_u16(record->payload + 4U, 23U);
    record->payload[6] = 4U; record->payload[7] = 3U; record->payload[10] = 3U;
    can_protocol_write_u32(record->payload + 12U, 1036U);
    control.state = SIM_RUNNING; control.motion_token = control.last_motion_token = 23U;
    control.motion_generation = 1U; control.target_rpm = control.target_mm_s = 100;
    control.axes = 3U; control.link_token = 100U; control.lower_boot = 42U; control.upper_boot = 84U;
    control.request_at = control.speed_at = control.tx_done_at = start;
}

static void run(uint32_t duration, uint32_t service_gap, uint8_t records, uint8_t axes)
{
    uint32_t elapsed;
    for (elapsed = 0U; elapsed < duration; ++elapsed, ++clock_ms) {
        const UpperRecord_t *record;
        unsigned axis;
        /* Result capture and task processing are distinct simulated events. */
        capture_wire();
        if (elapsed % service_gap == 0U) {
            process_result();
            lower.last_heartbeat_ms = clock_ms;
            if (elapsed % 100U == 0U) offer_basics(axes);
            if (records && elapsed % 50U == 0U) assert(can_link_request_record(&lower, 0x11U));
            if (records && elapsed % 100U == 0U) assert(can_link_request_record(&lower, 0x25U));
            if (records && elapsed % 150U == 0U) assert(can_link_request_record(&lower, 0x22U));
            can_link_service(&lower, clock_ms);
        }
        UpperControl_Tick(&control, &upper, clock_ms);
        record = UpperControl_Record(&upper.snapshot, 0x11U);
        if (record && clock_ms - record->timestamp > maximum_control_age) maximum_control_age = clock_ms - record->timestamp;
        for (axis = 0U; axis < 2U; ++axis)
            if (clock_ms - upper.snapshot.basic[6U + axis].timestamp > maximum_axis_age)
                maximum_axis_age = clock_ms - upper.snapshot.basic[6U + axis].timestamp;
        if (stop_commands) return;
    }
}

int main(void)
{
    unsigned gap;
    for (gap = 1U; gap <= 2U; ++gap) {
        init(1000U); run(10000U, gap, 1U, 1U);
        assert(control.state == SIM_RUNNING && !stop_commands);
        assert(!lower.record_failed && !lower.queue_dropped && upper.snapshot.records_received > 100U);
        assert(maximum_control_age <= 200U && maximum_axis_age <= 200U && maximum_record_duration < 200U);
        printf("PASS asynchronous gap=%u control_age=%lu axis_age=%lu record_duration=%lu commits=%lu\n", gap,
            (unsigned long)maximum_control_age, (unsigned long)maximum_axis_age,
            (unsigned long)maximum_record_duration, (unsigned long)record_commits);
    }
    init(0xFFFFFC00UL); run(3000U, 1U, 1U, 1U);
    assert(control.state == SIM_RUNNING && !stop_commands && !lower.record_failed);
    puts("PASS asynchronous transport across millisecond wrap");
    init(1000U); run(1000U, 10U, 1U, 1U);
    assert(lower.record_failed && successful_frames && stop_commands == 1U);
    assert(control.recovery_reason == 7U && control.detail == 116U);
    puts("PASS delayed task progression: hardware TX_OK does not imply a complete fresh record; F103 requests STOP");
    init(1000U); run(600U, 1U, 0U, 1U);
    assert(stop_commands == 1U && control.recovery_reason == 7U && control.detail == 116U);
    init(1000U); run(600U, 1U, 1U, 0U);
    assert(stop_commands == 1U && control.recovery_reason == 8U && control.detail == 116U);
    puts("PASS genuine stale control/axis samples retain original STOP protection");
    return 0;
}
