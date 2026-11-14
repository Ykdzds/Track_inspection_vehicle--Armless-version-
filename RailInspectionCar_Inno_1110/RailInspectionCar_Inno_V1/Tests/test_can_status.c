#include "app_can_status.h"
#include "app_can_config.h"
#include "app_config.h"
#include "app_health.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static app_can_status_input_t input;
static can_protocol_record_t record;

static void init(void)
{
    unsigned i;
    memset(&input, 0, sizeof(input));
    input.source_valid = true;
    input.selected = 3U;
    input.arbiter_axes = 3U;
    input.token = 100U;
    input.now_ms = input.snapshot_ms = input.sample_tick = 1000U;
    for (i = 0U; i < 2U; ++i)
    {
        input.axis[i].online = input.axis[i].enabled = 1U;
        input.axis[i].speed_valid = input.axis[i].fault_valid = 1U;
        input.axis[i].enable_feedback_valid = input.axis[i].enable_feedback = 1U;
        input.axis[i].parameter_verified = 1U;
        input.axis[i].control_mode = SERVO_CONTROL_RS485_VELOCITY;
    }
    app_can_status_describe(&input);
}

static void basic(uint16_t id, uint8_t data[8])
{
    assert(app_can_status_basic(&input, id, 9U, data));
}

static void extended(uint8_t type)
{
    assert(app_can_status_record(&input, type, &record));
    assert(record.type == type);
}

static void test_validity(void)
{
    uint8_t data[8];
    init();
    input.remote.authorized = true;
    input.cli.authorized = true;
    input.remote.estop_latched = true;
    input.remote.sync.active = true;
    input.axis[0].stop_latched = input.axis[0].disable_verified = 1U;
    input.position.axis[0].valid = input.position.axis[0].selected = 1U;
    input.position.rate_valid = input.position.skew_estimate_valid = 1U;
    input.source_valid = false;
    app_can_status_describe(&input);
    basic(0x180U, data);
    assert(data[0] == 255U && data[1] == 0U);
    assert(can_protocol_read_u16(data + 2U) == 0x8000U);
    basic(0x186U, data);
    assert(data[0] == 128U && data[1] == 255U);
    extended(0x11U);
    assert(record.payload[10] == 0U);
    assert((can_protocol_read_u32(record.payload + 12U) & (32U | 64U | 512U)) == 0U);
    extended(0x22U);
    assert((can_protocol_read_u32(record.payload) & 15U) == 0U);
    assert(can_protocol_read_u32(record.payload + 72U) == 0x80000000UL);
    assert(can_protocol_read_u32(record.payload + 76U) == UINT32_MAX);
    extended(0x23U);
    assert((can_protocol_read_u32(record.payload) & (2U | 4U | 64U)) == 0U);
    assert(can_protocol_read_u16(record.payload + 12U) == 0x8000U);
}

static void test_speed_and_config(void)
{
    uint8_t data[8], config[80];
    uint32_t fingerprint;
    init();
    input.axis[0].actual_speed_rpm = input.axis[1].actual_speed_rpm = 600;
    basic(0x180U, data);
    assert((data[1] & 1U) != 0U);
    assert(can_protocol_read_u16(data + 2U) == 2179U);
    assert(can_protocol_read_u16(data + 4U) == 0x8000U);
    input.axis[1].actual_speed_rpm = -600;
    basic(0x187U, data);
    assert((int16_t)can_protocol_read_u16(data + 2U) == -600);
    input.axis[0].fault_age_ms = APP_SERVO_STATUS_MAX_AGE_MS;
    input.now_ms += 1U;
    basic(0x186U, data);
    assert((data[0] & 8U) == 0U);
    app_can_config_encode(config, 7U);
    fingerprint = app_can_config_fingerprint();
    assert(fingerprint != 0U);
    assert(can_protocol_read_u32(config) == 7U);
    assert(config[4] == APP_SERVO_CONTROL_AXIS);
    assert(can_protocol_read_u32(config + 8U) == APP_POSITION_LEFT_WHEEL_DIAMETER_MM * 1000U);
    assert(can_protocol_read_u16(config + 42U) ==
        (uint16_t)((double)APP_REMOTE_SERVO_SYNC_KP * 10000.0 + 0.5));
    assert(can_protocol_read_u16(config + 58U) == 65535U);
    assert((app_can_capabilities() & ((1UL << 16U) | (1UL << 21U) | (1UL << 22U))) == 0U);
    assert((app_can_capabilities() & (1UL << 26U)) != 0U);
    assert((app_can_data_sources() & (1UL << 26U)) == 0U);
}

static void test_unknowns_and_lengths(void)
{
    static const uint8_t types[] = {2U, 0x10U, 0x11U, 0x12U, 0x13U, 0x20U,
        0x21U, 0x22U, 0x23U, 0x24U, 0x50U, 0x52U, 0x53U};
    static const uint16_t lengths[] = {32U, 80U, 40U, 40U, 40U, 56U,
        56U, 104U, 32U, 48U, 32U, 24U, 32U};
    unsigned i;
    uint8_t data[8];
    init();
    for (i = 0U; i < sizeof(types); ++i)
    {
        extended(types[i]);
        assert(record.length == lengths[i]);
    }
    basic(0x181U, data);
    assert(can_protocol_read_u16(data) == 65535U);
    assert(data[4] == 255U);
    basic(0x183U, data);
    for (i = 0U; i < 5U; ++i) assert(data[i] == 255U);
    assert(!app_can_status_record(&input, 0x43U, &record));
    extended(0x53U);
    assert(record.length == 32U && record.payload[8] == 0U &&
        record.payload[9] == 0U && can_protocol_read_u16(record.payload + 24U) == 0U &&
        can_protocol_read_u32(record.payload + 28U) == 0U);
    input.source_valid = false;
    extended(0x53U);
    assert(can_protocol_read_u32(record.payload + 16U) == 5U);
}

static void test_position_sample(void)
{
    init();
    input.position.axis[0].selected = input.position.axis[0].valid = 1U;
    input.position.axis[0].sequence = 22U;
    input.position.axis[0].generation = 2U;
    input.position.axis[0].raw_position = INT32_MIN;
    input.axis[0].position_samples[0].valid = 1U;
    input.axis[0].position_samples[0].position_sequence = 22U;
    input.axis[0].position_samples[0].position_generation = 2U;
    input.axis[0].position_samples[0].actual_position = INT32_MIN;
    input.axis[0].position_samples[0].sample_tick = 900U;
    extended(0x20U);
    assert(can_protocol_read_u32(record.payload) == 0x80000000UL);
    assert((can_protocol_read_u16(record.payload + 20U) & 2U) != 0U);
    assert(can_protocol_read_u32(record.payload + 12U) == 900U);
    assert(can_protocol_read_u32(record.payload + 16U) == 100U);
    input.sample_tick = 900U + APP_POSITION_MAX_AGE_MS + 1U;
    extended(0x20U);
    assert((can_protocol_read_u16(record.payload + 20U) & 2U) == 0U);
    input.axis[0].position_samples[0].position_generation = 1U;
    extended(0x20U);
    assert(can_protocol_read_u32(record.payload + 16U) == UINT32_MAX);
}

static void test_watchdog_history(void)
{
    init();
    input.watchdog[0].eligible = input.watchdog[1].eligible = 1U;
    input.watchdog[0].state = input.watchdog[1].state = SERVO_WDG_RECOVERY_DONE;
    input.watchdog[0].start_speed_rpm = 600;
    input.remote.controlled_stop_state = REMOTE_CONTROLLED_STOP_DECELERATING;
    input.remote.controlled_stop_reason = REMOTE_STOP_K1;
    app_can_status_describe(&input);
    extended(0x13U);
    assert(record.payload[4] == 1U && record.payload[5] == 1U);
    assert(can_protocol_read_u16(record.payload + 12U) == 0x8000U);
    input.watchdog[0].state = SERVO_WDG_RECOVERY_FAILED;
    input.watchdog[1].state = SERVO_WDG_RECOVERY_DECELERATING;
    input.watchdog[1].ramp_verified = 1U;
    input.watchdog[1].decel_readback_ms = 10000U;
    input.watchdog[1].stop_timeout_ms = 8000U;
    input.watchdog[1].stop_elapsed_ms = 500U;
    app_can_status_describe(&input);
    extended(0x13U);
    assert(record.payload[4] == 8U && record.payload[5] == 5U);
    assert(record.payload[7] == 1U);
    assert(can_protocol_read_u32(record.payload + 32U) == 7500U);
}

static void test_can_control_snapshot(void)
{
    uint8_t data[8];
    init(); input.arbiter_owner = APP_CONTROL_OWNER_CAN;
    input.control.authorized = true; input.control.state = CAN_CONTROL_RUNNING;
    input.control.motion_token = 123U; input.control.generation = 7U; input.control.axis_mask = 3U;
    input.control.actual_valid = true; input.control.actual_mm_s = 120; input.control.target_mm_s = 130;
    input.control.limit_mm_s = 200U; input.control.remaining_ms = 250U;
    input.control.sync.active = input.control.sync.pair_valid = 1U;
    input.control.sync.integer_correction = 2; input.control.batch = 55U;
    for (unsigned axis = 0U; axis < 2U; ++axis)
    {
        input.control.sync.sequence[axis] = 1U;
        input.axis[axis].speed_samples[0].valid = 1U;
        input.axis[axis].speed_samples[0].speed_sequence = 1U;
        input.axis[axis].speed_samples[0].sample_tick = input.sample_tick;
    }
    app_can_status_describe(&input); assert(input.owner == 4U);
    basic(0x180U, data); assert(can_protocol_read_u16(data + 2U) == 120U);
    assert(can_protocol_read_u16(data + 4U) == 130U);
    extended(0x11U); assert(can_protocol_read_u16(record.payload + 4U) == 123U);
    assert(can_protocol_read_u32(record.payload) == 7U && record.payload[10] == 3U);
    extended(0x23U); assert(can_protocol_read_u16(record.payload + 12U) == 2U);
    assert(can_protocol_read_u32(record.payload + 28U) == 55U);
    input.control.authorized = false; input.control.state = CAN_CONTROL_DONE;
    input.control.stop_phase = 4U; input.remote.authorized = true; input.remote.state = REMOTE_SERVO_RUNNING;
    input.arbiter_owner = APP_CONTROL_OWNER_REMOTE;
    input.remote.left_commanded_rpm = input.remote.right_commanded_rpm = 20;
    app_can_status_describe(&input); assert(input.owner == 1U);
    extended(0x11U); assert(can_protocol_read_u16(record.payload + 4U) == 0U);
    basic(0x180U, data); assert(can_protocol_read_u16(data + 4U) != 130U);
    puts("PASS CAN control snapshots: own token/sync/target, historical stop cannot override remote");
}

static void hmi_running(void)
{
    init();
    input.arbiter_owner = APP_CONTROL_OWNER_HMI;
    input.arbiter_generation = 17U;
    input.hmi.session = 31U;
    input.hmi.state = 3U;
    input.hmi.flags = 2U;
    input.hmi.target = 100;
    input.hmi.output[0] = 80;
    input.hmi.output[1] = 81;
    input.hmi.output_mask = 3U;
    input.hmi_remaining_ms = 900U;
    input.hmi.last_sequence = 123456U;
    app_can_status_describe(&input);
}

static void hmi_stopping(uint8_t phase)
{
    hmi_running();
    input.hmi.flags = 4U;
    input.hmi.state = 4U;
    input.hmi.phase = phase;
    input.stop.event = 9U;
    input.stop.generation = 18U;
    input.arbiter_generation = 18U;
    input.stop.owner = APP_CONTROL_OWNER_HMI;
    input.stop.reason = 5U;
    input.stop.command = 0x10U;
    input.stop.result = 2U;
    input.stop.started_ms = 700U;
    input.stop.timeout_ms = 3000U;
    input.stop.deceleration_mm_s2 = 500U;
    input.hmi.stop = input.stop;
    app_can_status_describe(&input);
}

static void hmi_stopped(void)
{
    hmi_stopping(1U);
    input.arbiter_owner = APP_CONTROL_OWNER_NONE;
    input.hmi.session = 0U;
    input.hmi.state = 0U;
    input.hmi.flags = 8U;
    input.hmi.phase = 4U;
    input.stop.result = 3U;
    input.stop.completed_ms = 950U;
    input.stop.zero_mask = input.stop.enabled_mask = 3U;
    input.hmi.stop = input.stop;
    app_can_status_describe(&input);
}

static void test_owner_mapping_and_hmi(void)
{
    static const app_control_owner_t owners[] = {APP_CONTROL_OWNER_NONE,
        APP_CONTROL_OWNER_REMOTE, APP_CONTROL_OWNER_CLI, APP_CONTROL_OWNER_HMI,
        APP_CONTROL_OWNER_CAN, APP_CONTROL_OWNER_AUTO};
    static const uint8_t wire[] = {0U, 1U, 2U, 3U, 4U, 255U};
    uint8_t data[8];
    unsigned index;
    for (index = 0U; index < sizeof(wire); ++index)
    {
        init();
        input.arbiter_owner = owners[index];
        input.remote.authorized = input.cli.authorized = input.control.authorized = true;
        input.hmi.flags = 2U; input.hmi.session = 1U; input.hmi.state = 3U;
        app_can_status_describe(&input);
        assert(input.owner == wire[index]);
        basic(0x185U, data); assert(data[2] == wire[index]);
        basic(0x188U, data); assert(data[2] == wire[index]);
        extended(0x11U); assert(record.payload[6] == wire[index]);
    }
    hmi_running();
    input.control.motion_token = 345U;
    input.control.authorized = true;
    input.control.stop_seen = true;
    input.control.stop_phase = 5U;
    input.control.stop_result = 9U;
    input.control.stop_external = true;
    input.remote.estop_latched = 1U;
    app_can_status_describe(&input);
    assert(input.owner == 3U && input.life == 3U);
    basic(0x180U, data);
    assert((data[1] & 10U) == 10U && (data[1] & 64U) == 0U);
    assert(can_protocol_read_u16(data + 4U) == 363U);
    extended(0x11U);
    assert(can_protocol_read_u16(record.payload + 4U) == 0U);
    assert(can_protocol_read_u32(record.payload) == 17U);
    assert(record.payload[10] == 3U && record.payload[34] == 0U && record.payload[35] == 255U);
    assert((can_protocol_read_u32(record.payload + 12U) & (128U | 8U)) == 128U);
    assert(can_protocol_read_u32(record.payload + 28U) == 0U);
    assert(can_protocol_read_u32(record.payload + 16U) == 900U);
    basic(0x188U, data); assert((data[7] & (128U | 4U)) == 128U);
    input.hmi.state = 1U; input.hmi.flags = 0U;
    app_can_status_describe(&input); assert(input.life == 2U);
    input.hmi.state = 2U; input.hmi.flags = 2U; input.hmi.target = 0;
    app_can_status_describe(&input);
    basic(0x180U, data); assert(can_protocol_read_u16(data + 4U) == 0U);
    input.hmi.target_age_ms = APP_HMI_SPEED_HOLD_MAX_MS;
    basic(0x180U, data); assert((data[1] & 2U) == 0U);
    input.now_ms += APP_SERVO_STATUS_MAX_AGE_MS + 1U;
    app_can_status_describe(&input); assert(input.owner == 255U);
    basic(0x180U, data); assert((data[1] & 11U) == 0U);
    extended(0x11U); assert(record.payload[10] == 0U);
    puts("PASS HMI ownership: explicit mapping, preparing/zero/running, no CAN token or stale authorization");
}

static void test_hmi_stop_records(void)
{
    uint8_t data[8], phase;
    for (phase = 1U; phase <= 3U; ++phase)
    {
        hmi_stopping(phase);
        input.control.stop_generation = 77U;
        input.control.stop_phase = 4U;
        app_can_status_describe(&input);
        assert(input.owner == 3U && input.life == 4U && input.stop_reason == 5U);
        basic(0x180U, data);
        assert((data[1] & 16U) && !(data[1] & 10U));
        assert(can_protocol_read_u16(data + 4U) == 0x8000U);
        extended(0x11U);
        assert(record.payload[34] == phase && record.payload[35] == 0U);
        extended(0x13U);
        assert(can_protocol_read_u32(record.payload) == 9U && record.payload[5] == phase);
        assert(can_protocol_read_u32(record.payload + 16U) == 500U);
        assert(can_protocol_read_u32(record.payload + 32U) == 2700U);
        input.hmi.stop.event--;
        extended(0x13U); assert(record.payload[5] == 255U);
    }
    hmi_stopped();
    extended(0x13U);
    assert(record.payload[5] == 4U && record.payload[6] == 3U);
    assert(can_protocol_read_u32(record.payload + 28U) == 250U);
    assert(can_protocol_read_u32(record.payload + 20U) == UINT32_MAX);
    assert(input.owner == 0U);
    extended(0x11U); assert(record.payload[34] == 4U && record.payload[35] == 0U);
    input.axis[0].enable_feedback_valid = 0U;
    extended(0x13U); assert(record.payload[5] == 255U);
    hmi_stopped(); input.axis[1].actual_speed_rpm = 1;
    extended(0x13U); assert(record.payload[5] == 255U);
    hmi_stopped(); input.stop.enabled_mask = 1U; input.hmi.stop = input.stop;
    extended(0x13U); assert(record.payload[5] == 255U);
    hmi_stopped(); input.lock_reason = APP_CONTROL_LOCK_SAFETY;
    app_can_status_describe(&input); assert(input.life == 5U && input.owner == 0U);
    extended(0x13U); assert(record.payload[5] != 4U);
    hmi_stopping(1U); input.stop.result = 5U; input.stop.error = 14U;
    input.hmi.stop = input.stop; input.hmi.state = 6U; input.hmi.phase = 5U;
    app_can_status_describe(&input);
    extended(0x13U); assert(record.payload[5] == 5U && record.payload[7] == 3U);
    extended(0x11U); assert(record.payload[34] == 5U && record.payload[35] != 0U);
    hmi_stopped(); input.arbiter_owner = APP_CONTROL_OWNER_CAN;
    input.control.authorized = true; input.control.state = CAN_CONTROL_RUNNING;
    input.control.motion_token = 88U;
    app_can_status_describe(&input); assert(input.owner == 4U);
    extended(0x11U); assert(can_protocol_read_u16(record.payload + 4U) == 88U);
    assert(record.payload[34] != 4U);
    hmi_stopped(); input.arbiter_owner = APP_CONTROL_OWNER_HMI;
    input.arbiter_generation = 19U; input.hmi.session = 32U;
    input.hmi.flags = 2U; input.hmi.state = 2U;
    app_can_status_describe(&input);
    extended(0x11U); assert(record.payload[34] == 0U && record.payload[35] == 255U);
    hmi_stopped(); input.stop.owner = APP_CONTROL_OWNER_REMOTE;
    input.remote.hold_stopping = 1U; input.remote.controlled_stop_state = REMOTE_CONTROLLED_STOP_DECELERATING;
    app_can_status_describe(&input);
    extended(0x13U); assert(record.payload[5] == 1U);
    hmi_stopped();
    input.arbiter_owner = APP_CONTROL_OWNER_CAN;
    input.control.stop_generation = 1U; input.control.stop_phase = 1U;
    app_can_status_describe(&input);
    extended(0x13U); assert(record.payload[5] == 1U);
    input.arbiter_owner = APP_CONTROL_OWNER_NONE; input.control.stop_phase = 4U;
    app_can_status_describe(&input);
    extended(0x13U); assert(can_protocol_read_u32(record.payload) == 1U);
    assert(record.payload[5] == 4U);
    extended(0x11U); assert(record.payload[34] == 4U && record.payload[35] == 0U);
    input.boot_id++;
    memset(&input.stop, 0, sizeof(input.stop));
    memset(&input.control, 0, sizeof(input.control));
    memset(&input.hmi, 0, sizeof(input.hmi));
    app_can_status_describe(&input);
    assert(input.stop_source == APP_CONTROL_OWNER_NONE);
    extended(0x13U); assert(record.payload[5] == 0U);
    input.now_ms += APP_SERVO_STATUS_MAX_AGE_MS + 1U;
    extended(0x13U);
    assert(record.payload[5] == 255U);
    assert(can_protocol_read_u16(record.payload + 8U) == 0x8000U);
    puts("PASS HMI stop: K5 event association, active stop beats history, zero/enable/release checks and failure");
}

static void test_hmi_sync(void)
{
    unsigned axis;
    hmi_running();
    input.hmi.sync.enabled = input.hmi.sync.active = input.hmi.sync.pair_valid = 1U;
    input.hmi.sync.sequence[0] = 11U; input.hmi.sync.sequence[1] = 12U;
    input.hmi.sync.integer_correction = -2;
    input.hmi.sync.raw_error = 3;
    for (axis = 0U; axis < 2U; ++axis)
    {
        servo_speed_sample_t *sample = &input.axis[axis].speed_samples[0];
        sample->valid = 1U; sample->sample_tick = input.sample_tick;
        sample->speed_sequence = input.hmi.sync.sequence[axis];
        sample->actual_speed_rpm = axis == 0U ? 90 : 87;
    }
    extended(0x23U);
    assert((can_protocol_read_u32(record.payload) & (2U | 4U | 64U | 128U)) == 6U);
    assert((int16_t)can_protocol_read_u16(record.payload + 12U) == -2);
    assert(can_protocol_read_u16(record.payload + 14U) == 80U);
    assert(can_protocol_read_u32(record.payload + 28U) == 123456U);
    input.sample_tick += APP_REMOTE_SERVO_SYNC_MAX_AGE_MS + 1U;
    extended(0x23U); assert(!(can_protocol_read_u32(record.payload) & 4U));
    assert(can_protocol_read_u16(record.payload + 12U) == 0x8000U);
    input.arbiter_owner = APP_CONTROL_OWNER_NONE;
    app_can_status_describe(&input);
    extended(0x23U);
    assert((can_protocol_read_u32(record.payload) & (2U | 4U | 64U | 128U)) == 0U);
    assert(can_protocol_read_u16(record.payload + 14U) == 0x8000U);
    puts("PASS HMI sync: owner selection, fresh sample pairing, reserved source bits and unknown outputs");
}

static void test_display_warning_severity(void)
{
    unsigned bit;
    uint8_t data[8];
    uint32_t generation;
    init(); extended(0x53U); generation=can_protocol_read_u32(record.payload+4U);
    for (bit=0U;bit<32U;++bit) {
        uint32_t diag=1UL<<bit;
        int fault=(diag & APP_DIAG_DISPLAY_WARNING_MASK)==0U;
        init(); input.diagnostic=diag; app_can_status_describe(&input);
        assert((input.life==5U)==fault);
        basic(0x182U,data);
        assert(!!(can_protocol_read_u16(data+2U)&512U)==fault);
        extended(0x12U); assert(can_protocol_read_u32(record.payload+8U)==diag);
        extended(0x25U); assert(can_protocol_read_u32(record.payload+84U)==diag);
        extended(0x53U);
        assert(!!(can_protocol_read_u16(record.payload+10U)&2U)==fault);
        assert(can_protocol_read_u32(record.payload+16U)==(fault?3U:1U));
    }
    init(); extended(0x53U); generation=can_protocol_read_u32(record.payload+4U);
    input.diagnostic=APP_DIAG_DISPLAY_WARNING_MASK; app_can_status_describe(&input);
    extended(0x53U); assert(can_protocol_read_u32(record.payload+4U)==generation);
    input.lock_reason=APP_CONTROL_LOCK_SAFETY; app_can_status_describe(&input); assert(input.life==5U);
    init(); input.diagnostic=12U; input.axis[0].fault=1U; app_can_status_describe(&input); assert(input.life==5U);
    puts("PASS diagnostic severity: 32 bits, raw telemetry retained, LCD summary stable, real faults/locks retained");
}

int main(void)
{
    test_display_warning_severity();
    init();
    input.snapshot_sequence = 0x12345678UL;
    extended(0x25U);
    assert(record.length == 104U && can_protocol_read_u32(record.payload) == input.snapshot_sequence);
    assert(can_protocol_read_u32(record.payload + 4U) == input.snapshot_ms);
    assert(can_protocol_read_u32(record.payload + 12U) == 1U);
    input.arbiter_owner = APP_CONTROL_OWNER_CAN;
    input.control.target_mm_s = 100;
    input.control.authorized = false;
    extended(0x25U); assert(can_protocol_read_u16(record.payload + 24U) == 0U);
    input.control.authorized = true;
    extended(0x25U); assert(can_protocol_read_u16(record.payload + 24U) == 100U);
    input.control.sync.pair_valid = input.control.sync.active = 1U;
    input.control.sync.integer_correction = -2;
    input.axis[0].speed_samples[0].valid = input.axis[1].speed_samples[0].valid = 1U;
    input.axis[0].speed_samples[0].sample_tick = input.axis[1].speed_samples[0].sample_tick = input.sample_tick;
    extended(0x25U);
    assert(can_protocol_read_u32(record.payload + 68U) == 1U);
    assert((int16_t)can_protocol_read_u16(record.payload + 74U) == -2);
    input.control.authorized = false;
    extended(0x25U);
    assert(can_protocol_read_u32(record.payload + 68U) == 0U);
    assert(can_protocol_read_u16(record.payload + 74U) == 0U);
    input.control.authorized = true;
    input.control.sync.sequence[0]++;
    extended(0x25U); assert(can_protocol_read_u32(record.payload + 68U) == 0U);
    input.control.sync.sequence[0]--;
    input.control.sync.active = 0U;
    extended(0x25U);
    assert(can_protocol_read_u32(record.payload + 68U) == 1U);
    assert(can_protocol_read_u16(record.payload + 74U) == 0U);
    input.lock_reason = APP_CONTROL_LOCK_SAFETY;
    extended(0x25U); assert(can_protocol_read_u16(record.payload + 24U) == 0U);
    assert(can_protocol_read_u32(record.payload + 68U) == 0U);
    input.lock_reason = APP_CONTROL_LOCK_NONE;
    input.axis[0].diagnostic_valid_mask = 15U;
    input.axis[0].diagnostic_sample_tick[0] = input.sample_tick;
    input.axis[0].phase_current_deciamp = -123;
    extended(0x26U);
    assert(record.length == 64U && record.payload[8] == 15U);
    assert((int16_t)can_protocol_read_u16(record.payload + 16U) == -123);
    input.source_valid = false;
    extended(0x25U); assert(can_protocol_read_u32(record.payload + 12U) == 0U);
    extended(0x26U); assert(record.payload[8] == 0U);
    test_validity();
    test_speed_and_config();
    test_unknowns_and_lengths();
    test_position_sample();
    test_watchdog_history();
    test_can_control_snapshot();
    test_owner_mapping_and_hmi();
    test_hmi_stop_records();
    test_hmi_sync();
    puts("PASS CAN status validity, unknowns, configuration, sample matching and recovery history");
    return 0;
}
