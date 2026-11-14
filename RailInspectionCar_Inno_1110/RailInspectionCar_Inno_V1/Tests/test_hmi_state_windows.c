/* Keep the established dictionary cases and use the retained full encoder as
 * an independent output oracle for each isolated request window. */
#define main state_regression_main
#include "test_hmi_state.c"
#undef main
#include "../APP/HMI/app_hmi_state.c"
#include <math.h>

static unsigned paired_calls, observe_calls, full_calls;
static unsigned long checked_requests;
static uint16_t golden[HMI_STATE_REGISTER_COUNT];
static uint16_t relative[HMI_STATE_READ_MAX_REGISTERS + 2U];
static hmi_state_input_t saved_input;
static hmi_alarm_state_t saved_alarm;

void __cyg_profile_func_enter(void *function, void *caller)
    __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *function, void *caller)
    __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *function, void *caller)
{
    (void)caller;
    if (function == (void *)paired_speed) ++paired_calls;
    if (function == (void *)app_hmi_state_alarm_observe) ++observe_calls;
    if (function == (void *)app_hmi_state_build) ++full_calls;
}
void __cyg_profile_func_exit(void *function, void *caller)
{
    (void)function; (void)caller;
}

static const struct { uint16_t start, count; } windows[] = {
    {0x0000U, 64U}, {0x0050U, 32U}, {0x0070U, 32U},
    {0x0090U, 32U}, {0x00B0U, 32U}, {0x0200U, 24U},
    {0x0300U, 352U}, {0x0460U, 96U}
};

static void prepare_oracle(void)
{
    app_hmi_state_build(&in, &alarm, golden);
    saved_input = in;
    saved_alarm = alarm;
}

static void check_request(unsigned w, uint16_t start, uint16_t quantity)
{
    unsigned i;
    uint16_t first = start;
    uint16_t count = quantity;
    memset(relative, 0xA5, sizeof(relative));
    paired_calls = observe_calls = full_calls = 0U;
    assert(app_hmi_state_build_read_window(&in, &alarm, in.now_ms,
        start, quantity, &relative[1]));
    assert(relative[0] == 0xA5A5U);
    for (i = quantity + 1U; i < HMI_STATE_READ_MAX_REGISTERS + 2U; ++i)
        assert(relative[i] == 0xA5A5U);
    for (i = 0U; i < quantity; ++i)
        if (relative[i + 1U] != golden[start + i])
        {
            fprintf(stderr, "relative window %u request %04X/%u mismatch %04X: %04X != %04X\n",
                w, start, quantity, start + i, relative[i + 1U], golden[start + i]);
            assert(0);
        }
    assert(paired_calls == (w == 0U || w == 2U ? 1U : 0U));
    assert(observe_calls == 0U && full_calls == 0U);
    memset(r, 0xA5, sizeof(r));
    paired_calls = observe_calls = full_calls = 0U;
    assert(app_hmi_state_build_window(&in, &alarm, in.now_ms, start, quantity, r));
    if (memcmp(&r[first], &golden[first], count * sizeof(r[0])) != 0)
    {
        for (i = first; i < (unsigned)first + count; ++i)
            if (r[i] != golden[i])
                fprintf(stderr, "window %u request %04X/%u mismatch %04X: %04X != %04X\n",
                    w, start, quantity, i, r[i], golden[i]);
        assert(0);
    }
    for (i = 0U; i < first; ++i) assert(r[i] == 0xA5A5U);
    for (i = first + count; i < HMI_STATE_REGISTER_COUNT; ++i) assert(r[i] == 0xA5A5U);
    assert(paired_calls == (w == 0U || w == 2U ? 1U : 0U));
    assert(observe_calls == 0U && full_calls == 0U);
    ++checked_requests;
}

static void compare_windows(uint8_t exhaustive)
{
    unsigned w;
    uint16_t offset, quantity, remaining, limit;
    prepare_oracle();
    for (w = 0U; w < sizeof(windows) / sizeof(windows[0]); ++w)
        for (offset = 0U; offset < windows[w].count; ++offset)
        {
            remaining = (uint16_t)(windows[w].count - offset);
            limit = remaining < 125U ? remaining : 125U;
            check_request(w, (uint16_t)(windows[w].start + offset), 1U);
            if (exhaustive)
                for (quantity = 2U; quantity <= limit; ++quantity)
                    check_request(w, (uint16_t)(windows[w].start + offset), quantity);
            else if (limit != 1U)
                check_request(w, (uint16_t)(windows[w].start + offset), limit);
        }
    assert(memcmp(&in, &saved_input, sizeof(in)) == 0);
    assert(memcmp(&alarm, &saved_alarm, sizeof(alarm)) == 0);
}

static void wire32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U); p[3] = (uint8_t)(value >> 24U);
}

static void rich_input(void)
{
    unsigned i, word;
    uint32_t sources = (1UL << 15U) | (1UL << 16U) | (1UL << 17U) |
        (1UL << 18U) | (1UL << 19U) | (1UL << 20U) | (1UL << 25U);
    ready();
    in.owner = APP_CONTROL_OWNER_HMI;
    in.hmi_control.flags = 2U;
    in.hmi_control.output_mask = 3U;
    in.hmi_control.output[0] = 600;
    in.hmi_control.output[1] = 599;
    in.hmi_control.session = in.hmi_session = 0xA1234567UL;
    in.hmi_control.target = 601;
    in.hmi_age = 77U; in.hmi_remaining = 923U;
    in.hmi_control.last_sequence = 0xB1234567UL;
    in.position.enabled = in.position.vehicle_valid = in.position.pair_valid = 1U;
    in.position.vehicle_boot_net_mm = -1300.75;
    in.position.vehicle_boot_path_mm = 1712.25;
    in.position.vehicle_trip_path_mm = 1612.25;
    in.position.difference_mm = -21.5;
    in.position.lost_intervals = 0x12345678UL;
    in.position.baseline_generation = 0xABCD1234UL;
    in.can_valid = in.can.enabled = in.can.task_created = 1U;
    in.can.phase = CAN_LINK_READY; in.can.bus.phase = BSP_CAN_READY;
    in.can.upper_boot_id = 0x12349876UL;
    in.can.generation = 0xABCD1234UL;
    in.can.bus.rx_count = 0xABCDEF01UL;
    in.can.bus.tx_success = 0xFEDCBA98UL;
    in.can.bus.tx_failed = 0x12345678UL;
    in.can.bus.bus_off_count = 0x10203040UL;
    in.can.upper_invalid = 0x55667788UL;
    for (i = 0U; i < HMI_UPPER_COUNT; ++i)
    {
        in.upper[i].present = in.upper[i].valid = 1U;
        in.upper[i].length = i == 4U ? 48U : i == 6U ? 24U : 32U;
        in.upper[i].sequence = (uint16_t)(0x1234U + i);
        in.upper[i].received_ms = 0xABCDEF01UL + i;
        in.upper[i].age_ms = 21U + i;
        for (word = 0U; word < sizeof(in.upper[i].payload); ++word)
            in.upper[i].payload[word] = (uint8_t)(word + i * 31U);
    }
    wire32(in.upper[0].payload + 8U, sources);
    wire32(in.upper[0].payload + 12U, sources);
    for (i = 0U; i < 2U; ++i)
    {
        in.arm_valid[i] = in.arm[i].valid = 1U;
        in.arm[i].sample_age_ms = 5U;
        in.arm[i].state = 4U; in.arm[i].flags = 4U;
        in.axis[i].control_mode = SERVO_CONTROL_RS485_VELOCITY;
        in.axis[i].actual_speed_rpm = i ? -599 : 600;
        in.axis[i].speed_age_ms = 37U + i;
        in.axis[i].position_sequence = 0x76543210UL + i;
        in.axis[i].position_age_ms = 18U + i;
        in.axis[i].poll_failures = 0x1234U + i;
        in.axis[i].enabled = in.axis[i].enable_feedback = in.axis[i].enable_feedback_valid = 1U;
        in.axis[i].diagnostic_valid_mask = 15U;
        in.axis[i].diagnostic_stale_mask = 2U;
        for (word = 0U; word < SERVO_SPEED_SAMPLE_CAPACITY; ++word)
        {
            in.axis[i].speed_samples[word].valid = 1U;
            in.axis[i].speed_samples[word].sample_tick = 995U - word * 7U + i;
            in.axis[i].speed_samples[word].actual_speed_rpm = (int16_t)(600U + word + i);
        }
    }
    in.hmi_control.sync.active = in.hmi_control.sync.pair_valid = 1U;
    in.hmi_control.sync.raw_error = -13;
    in.hmi_control.sync.effective_error = -8;
    in.hmi_control.sync.integer_correction = 4;
    in.uart.requests = 0xABCD1234UL;
    in.uart.responses = 0x87654321UL;
    in.uart.request_seen = 1U; in.uart.last_request_ms = 993U;
    in.uart.crc_errors = 3U; in.uart.frame_errors = 7U;
    in.heartbeat_ms[0] = 65535U;
    in.heartbeat_ms[1] = UINT32_MAX;
}

static uint8_t legal_range(uint16_t start, uint16_t quantity)
{
    unsigned w;
    if (quantity == 0U || quantity > 125U) return 0U;
    for (w = 0U; w < sizeof(windows) / sizeof(windows[0]); ++w)
        if (start >= windows[w].start &&
            (uint32_t)start + quantity <= (uint32_t)windows[w].start + windows[w].count)
            return 1U;
    return 0U;
}

static void reject_range(uint16_t start, uint16_t quantity)
{
    memset(r, 0xA5, sizeof(r));
    memcpy(golden, r, sizeof(r));
    assert(!app_hmi_state_build_window(&in, &alarm, in.now_ms, start, quantity, r));
    assert(memcmp(r, golden, sizeof(r)) == 0);
    memset(relative, 0xA5, sizeof(relative));
    assert(!app_hmi_state_build_read_window(&in, &alarm, in.now_ms,
        start, quantity, &relative[1]));
    {
        unsigned i;
        for (i = 0U; i < HMI_STATE_READ_MAX_REGISTERS + 2U; ++i)
            assert(relative[i] == 0xA5A5U);
    }
}

static void test_invalid(void)
{
    uint32_t start;
    uint16_t quantity;
    ready();
    for (start = 0U; start <= UINT16_MAX; ++start)
        if (!legal_range((uint16_t)start, 1U)) reject_range((uint16_t)start, 1U);
    for (start = 0U; start < HMI_STATE_REGISTER_COUNT; ++start)
        for (quantity = 0U; quantity <= 126U; ++quantity)
            if (!legal_range((uint16_t)start, quantity))
                reject_range((uint16_t)start, quantity);
    reject_range(0xFFFFU, 125U); reject_range(0xFFFFU, 0xFFFFU);
    memset(r, 0xA5, sizeof(r)); memcpy(golden, r, sizeof(r));
    assert(!app_hmi_state_build_window(NULL, &alarm, 0U, 0U, 1U, r));
    assert(!app_hmi_state_build_window(&in, NULL, 0U, 0U, 1U, r));
    assert(!app_hmi_state_build_window(&in, &alarm, 0U, 0U, 1U, NULL));
    assert(!app_hmi_state_build_read_window(NULL, &alarm, 0U, 0U, 1U, relative));
    assert(!app_hmi_state_build_read_window(&in, NULL, 0U, 0U, 1U, relative));
    assert(!app_hmi_state_build_read_window(&in, &alarm, 0U, 0U, 1U, NULL));
    assert(memcmp(r, golden, sizeof(r)) == 0);
}

static void test_alarm_observation(void)
{
    uint32_t event, event_ms;
    unsigned i;
    ready();
    assert(sizeof(alarm) == 32U);
    assert(app_hmi_state_build_window(&in, &alarm, 1000U, 4U, 1U, r));
    assert(r[4U] == 0xFFFFU && !alarm.initialized);
    app_hmi_state_alarm_observe(&in, &alarm);
    event = alarm.sequence; event_ms = alarm.time_ms;
    assert(event == 1U && alarm.observed_ms == 1000U);
    in.axis[0].fault_code = 123U;
    in.remote.hold_stopping = 1U; in.sequence++;
    saved_alarm = alarm;
    for (i = 0U; i < 50U; ++i)
    {
        assert(app_hmi_state_build_window(&in, &alarm, 1100U, 0x90U, 32U, r));
        assert(r[0x90U] == event && r[0x91U] == 0U && r[0x92U] == 0U);
        assert(r[0x9BU] == 1U && r[0x9DU] == 1U);
        assert(r[0x9EU] == (uint16_t)(in.sequence >> 16U));
        assert(r[0x9FU] == (uint16_t)in.sequence);
    }
    assert(memcmp(&alarm, &saved_alarm, sizeof(alarm)) == 0);
    assert(app_hmi_state_build_window(&in, &alarm, 1101U, 0x90U, 32U, r));
    assert(r[0x91U] == 0xFFFFU && r[0x92U] == 0xFFFFU && r[0x93U] == 0xFFFFU);
    assert(r[0x94U] == 0xFFFFU && r[0x9CU] == 0xFFFFU && r[0x9DU] == 0U);
    assert(r[0x9BU] == 1U && r[0x90U] == event);
    assert((((uint32_t)r[0x96U] << 16U) | r[0x97U]) == event_ms);
    assert(app_hmi_state_build_window(&in, &alarm, 1101U, 4U, 1U, r));
    assert(r[4U] == 0xFFFFU);
    assert(app_hmi_state_build_window(&in, &alarm, 1101U, 0xCDU, 1U, r));
    assert(r[0xCDU] == 0xFFFFU);
    in.now_ms = 1101U;
    app_hmi_state_alarm_observe(&in, &alarm);
    assert(alarm.sequence == event + 1U && alarm.time_ms == 1101U);
    assert(app_hmi_state_build_window(&in, &alarm, 1101U, 0x90U, 32U, r));
    assert(r[0x91U] == 3U && r[0x92U] == 0x0201U);
    /* A summary-only change refreshes observation without inventing an event. */
    ready(); in.diagnostic = APP_DIAG_STACK_OVERFLOW;
    app_hmi_state_alarm_observe(&in, &alarm);
    event = alarm.sequence; event_ms = alarm.time_ms;
    in.now_ms += 50U; in.axis[0].fault_code = 123U;
    app_hmi_state_alarm_observe(&in, &alarm);
    assert(alarm.sequence == event && alarm.time_ms == event_ms);
    assert((alarm.summary & (1U << 2U)) && alarm.observed_ms == 1050U);
    assert(app_hmi_state_build_window(&in, &alarm, 1150U, 0x90U, 32U, r));
    assert(r[0x91U] == 4U && (r[0x9CU] & (1U << 2U)));
    /* Observation age is unsigned across millisecond wrap. */
    ready(); in.now_ms = UINT32_MAX - 30U;
    alarm.sequence = UINT32_MAX;
    app_hmi_state_alarm_observe(&in, &alarm);
    assert(alarm.sequence == 0U);
    assert(app_hmi_state_build_window(&in, &alarm, 69U, 0x90U, 32U, r));
    assert(r[0x91U] == 0U && r[0x9DU] == 1U);
    assert(app_hmi_state_build_window(&in, &alarm, 70U, 0x90U, 32U, r));
    assert(r[0x91U] == 0xFFFFU && r[0x9DU] == 0U);
    app_hmi_state_alarm_observe(NULL, &alarm);
    app_hmi_state_alarm_observe(&in, NULL);
}

int main(void)
{
    unsigned selected, owner, i;
    static const uint16_t owners[] = {APP_CONTROL_OWNER_NONE, APP_CONTROL_OWNER_HMI,
        APP_CONTROL_OWNER_REMOTE, APP_CONTROL_OWNER_AUTO, APP_CONTROL_OWNER_CAN,
        APP_CONTROL_OWNER_CLI, 0xFFFFU};
    state_regression_main();
    ready(); compare_windows(1U);
    rich_input(); compare_windows(1U);
    for (selected = 0U; selected < 4U; ++selected)
        for (owner = 0U; owner < sizeof(owners) / sizeof(owners[0]); ++owner)
        {
            rich_input(); in.selected = (uint8_t)selected; in.owner = owners[owner];
            in.remote.authorized = in.cli.authorized = 1U;
            in.cli.axis = 1U; in.cli.mode = in.debug.mode = SERVO_DEBUG_MODE_RS485_SPEED;
            in.remote.left_commanded_rpm = -123; in.remote.right_commanded_rpm = 124;
            in.can_control.commanded_rpm[0] = -300; in.can_control.commanded_rpm[1] = 301;
            compare_windows(0U);
        }
    rich_input();
    in.axes_valid = in.health_valid = in.position_valid = in.owner_valid = 0U;
    in.remote_valid = in.cli_valid = in.debug_valid = in.k1_valid = in.startup_valid = 0U;
    in.can_valid = in.identity_valid = 0U;
    compare_windows(0U);
    rich_input();
    for (i = 0U; i < HMI_UPPER_COUNT; ++i) in.upper[i].valid = 0U;
    in.arm_valid[1] = 0U; compare_windows(0U);
    rich_input(); in.upper[0].payload[14] = in.upper[0].payload[15] = 0U;
    compare_windows(0U);
    rich_input(); in.can.bus.phase = BSP_CAN_BACKOFF; compare_windows(0U);
    rich_input(); in.position.vehicle_boot_net_mm = NAN;
    in.position.vehicle_boot_path_mm = 2147483647.0;
    in.position.vehicle_trip_path_mm = -1.0;
    in.position.difference_mm = NAN;
    in.axis[0].speed_age_ms = in.axis[0].fault_age_ms = UINT32_MAX;
    in.axis[1].position_age_ms = UINT32_MAX;
    in.now_tick = 5U; in.boot_id = in.sequence = UINT32_MAX;
    compare_windows(0U);
    test_invalid();
    test_alarm_observation();
    printf("PASS HMI relative/absolute windows: %lu isolated requests each; all legal subranges, exact qty footprint with125-word guard, no cross-window dependency, paired<=1, const reads and alarm maintenance\n", checked_requests);
    return 0;
}
