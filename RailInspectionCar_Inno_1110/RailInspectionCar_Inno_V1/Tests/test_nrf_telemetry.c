#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "nrf_status_telemetry.h"
#include "nrf_remote_protocol.h"
#include "app_config.h"
#include "app_health.h"
#include "motion_control.h"
#include "NRF24L01.h"
#include "app_cli_servo_session.h"
#ifdef TEST_REMOTE_MODEL
#include "remote_status_model.h"
static remote_status_model_t receiver;
#endif

bool app_arm3_remote_active(void){return false;}
bool app_arm3_port_ack(uint8_t type,uint8_t p[32],uint32_t now){(void)type;(void)p;(void)now;return false;}
static uint16_t get16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] << 8U | p[1]); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] << 24U | (uint32_t)p[1] << 16U | (uint32_t)p[2] << 8U | p[3]; }
static uint16_t reference_crc(const uint8_t *p, unsigned length)
{
    uint16_t crc = 65535U;
    while (length--) {
        crc ^= (uint16_t)*p++ << 8U;
        for (unsigned bit = 0U; bit < 8U; bit++)
            crc = (uint16_t)((crc & 32768U) ?
                ((uint32_t)crc << 1U) ^ 0x1021U : (uint32_t)crc << 1U);
    }
    return crc;
}
static void check_common(const uint8_t frame[32], uint8_t type)
{
    assert(frame[0] == 0x5AU && frame[1] == 0xA5U && frame[2] == 1U);
    assert(frame[3] == type && frame[5] == 22U);
    if (type == 0x89U) assert(((frame[10] | frame[11]) & 0xF0U) == 0U);
    else assert(frame[10] == 0U && frame[11] == 0U);
    assert(frame[30] == 13U && frame[31] == 10U);
    assert(get16(frame + 28) == reference_crc(frame, 28U));
}

#if APP_NRF_TELEMETRY_ENABLED
static cli_servo_session_status_t cli_session;
bool app_cli_servo_session_get_status(cli_servo_session_status_t *out)
{ *out = cli_session; return true; }
/* 提供只读数据桩；不提供任何运动写接口，链接成功也约束回传不参与控制。 */
static TickType_t ticks;
static nrf_status_input_t live;
static uint8_t queued[32];
static uint8_t available = 1U, queue_fail;
static uint8_t read_fail;
static uint32_t health, generated[10], quick_gap, last_quick;
static uint32_t alarm_count;
#ifndef TEST_REAL_DRIVER
static uint32_t last_alarm;
static uint8_t last_alarm_sequence;
#endif
static uint8_t hold_active, stop_ready = 1U, zero_ready = 1U;
static uint8_t hold_ready = 1U, watchdog_ready = 1U;
static servo_startup_stop_status_t stop[SERVO_AXIS_COUNT];
static servo_boot_zero_status_t zero[SERVO_AXIS_COUNT];
static servo_startup_hold_status_t hold[SERVO_AXIS_COUNT];
static servo_wdg_recovery_status_t watchdog[SERVO_AXIS_COUNT];
static motion_state_t motion;
static uint32_t generation;
#ifndef TEST_REAL_DRIVER
void NRF24L01_GetStats(NRF24L01_Stats *out)
{
    memset(out, 0, sizeof(*out));
    out->recovery_generation = generation;
}
#endif

TickType_t xTaskGetTickCount(void) { return ticks; }
uint32_t bsp_system_get_reset_reason(void) { return 0x28000000U; }
uint32_t app_health_get_diagnostic_faults(void) { return health; }
motion_state_t motion_get_state(void) { return motion; }
bool app_remote_servo_get_status(remote_servo_status_t *out)
{ if (read_fail) return false; *out = live.remote; return true; }
bool app_position_monitor_get_status(position_monitor_status_t *out)
{ *out = live.position; return true; }
bool aimtor_get_snapshot_pair(servo_snapshot_t *left, servo_snapshot_t *right)
{ *left = live.axis[0]; *right = live.axis[1]; return true; }
bool aimtor_watchdog_recovery_ready(void) { return watchdog_ready != 0U; }
bool aimtor_startup_stop_ready(void) { return stop_ready != 0U; }
bool aimtor_boot_zero_ready(void) { return zero_ready != 0U; }
bool aimtor_startup_hold_ready(void) { return hold_ready != 0U; }
bool aimtor_startup_hold_active(void) { return hold_active != 0U; }
bool aimtor_motion_allowed(void) { return true; }
bool aimtor_maintenance_enabled(void) { return false; }
bool aimtor_startup_stop_get_status(servo_axis_t axis, servo_startup_stop_status_t *out)
{ *out = stop[axis]; return true; }
bool aimtor_boot_zero_get_status(servo_axis_t axis, servo_boot_zero_status_t *out)
{ *out = zero[axis]; return true; }
bool aimtor_startup_hold_get_status(servo_axis_t axis, servo_startup_hold_status_t *out)
{ *out = hold[axis]; return true; }
bool aimtor_watchdog_recovery_get_status(servo_axis_t axis, servo_wdg_recovery_status_t *out)
{ *out = watchdog[axis]; return true; }
uint8_t app_system_snapshot_read_nrf(nrf_status_input_t *input,
    nrf_snapshot_context_t *context)
{
    if (read_fail) return 0U;
    input->remote = live.remote;
    input->position = live.position;
    memcpy(input->axis, live.axis, sizeof(input->axis));
    memset(context, 0, sizeof(*context));
    context->cli = cli_session;
    memcpy(context->startup, stop, sizeof(stop));
    memcpy(context->zero, zero, sizeof(zero));
    memcpy(context->hold, hold, sizeof(hold));
    memcpy(context->watchdog, watchdog, sizeof(watchdog));
    input->snapshot_sequence = 1U;
    context->reset = bsp_system_get_reset_reason();
    context->health = health;
    context->motion = motion;
    context->ready_mask = (stop_ready ? 1U : 0U) | (watchdog_ready ? 2U : 0U) |
        (zero_ready ? 4U : 0U) | (hold_ready ? 8U : 0U);
    context->hold_active = hold_active;
    context->motion_allowed = context->startup_valid = 1U;
    return 1U;
}
#ifndef TEST_REAL_DRIVER
bool NRF24L01_CanQueueAck(void) { return available != 0U; }
bool NRF24L01_QueueAckPayload(const uint8_t payload[32])
{
    uint8_t type = payload[3];
    if (queue_fail) return false;
    memcpy(queued, payload, 32U); check_common(payload, type);
    generated[type - 0x81U]++;
    if (type == 0x81U)
    {
        if (generated[0] > 1U && ticks - last_quick > quick_gap)
            quick_gap = ticks - last_quick;
        last_quick = ticks;
    }
    if (type == 0x87U)
    {
        if (alarm_count && payload[12] == last_alarm_sequence)
            assert(ticks - last_alarm >= APP_NRF_ALARM_PERIOD_MS);
        last_alarm_sequence = payload[12];
        last_alarm = ticks; alarm_count++;
    }
    return true;
}
#endif

static void test_sparse_opportunities(void);

static void reset_live(void)
{
    memset(&live, 0, sizeof(live));
    memset(&cli_session, 0, sizeof(cli_session));
    memset(stop, 0, sizeof(stop)); memset(zero, 0, sizeof(zero));
    memset(hold, 0, sizeof(hold)); memset(watchdog, 0, sizeof(watchdog));
    memset(generated, 0, sizeof(generated));
    ticks = health = quick_gap = last_quick = alarm_count = 0U;
    stop_ready = zero_ready = hold_ready = watchdog_ready = available = 1U;
    hold_active = queue_fail = read_fail = 0U; motion = MOTION_READY;
    live.remote.authorized = live.remote.link_valid = 1U;
    live.remote.state = REMOTE_SERVO_RUNNING;
    for (unsigned axis = 0U; axis < 2U; axis++)
    {
        live.axis[axis].online = live.axis[axis].speed_valid = live.axis[axis].fault_valid = 1U;
        live.axis[axis].control_mode = SERVO_CONTROL_RS485_VELOCITY;
    }
    nrf_status_telemetry_init(0x39U);
}
static void check_first(uint8_t system, uint8_t code)
{
    nrf_telemetry_stats_t stat;
    nrf_status_telemetry_service();
    if (queued[3] != 0x81U || queued[12] != system)
        fprintf(stderr, "quick state: expected=%u actual=%u type=%u\n",
                system, queued[12], queued[3]);
    assert(queued[3] == 0x81U && queued[12] == system);
    nrf_status_telemetry_get_stats(&stat); assert(stat.alarm_code == code);
}

static void test_schedule(void)
{
    nrf_telemetry_stats_t stat;
    reset_live();
    for (ticks = 0U; ticks <= 10000U; ticks += 10U) nrf_status_telemetry_service();
    assert(generated[0] >= 90U && quick_gap <= 150U);
    for (unsigned type = 1U; type < 6U; type++)
        assert(generated[type] > 0U);
    nrf_status_telemetry_get_stats(&stat);
    assert(stat.session == 0x39U && stat.enabled == 1U);
    assert(stat.generated == generated[0] + generated[1] + generated[2] +
        generated[3] + generated[4] + generated[5] + generated[6] +
        generated[7] + generated[8] + generated[9]);
    assert(generated[8] >= 18U && generated[8] <= 21U);
    /* 连续重发旧缓存时保留有效数值，但不能宣称采样仍新鲜。 */
    live.axis[0].diagnostic_valid_mask = 15U;
    live.axis[0].phase_current_deciamp = 123;
    ticks += 1000U;
    for (unsigned tries = 0U; tries < 10U; tries++) {
        nrf_status_telemetry_service();
        if (queued[3] == 0x89U) break;
        ticks += 10U;
    }
    if (app_config_servo_axis_selected(0U)) {
        assert(queued[3] == 0x89U && queued[10] == 15U);
        assert(get16(queued + 16U) == (0x8000U | 123U));
    }
    nrf_status_telemetry_get_stats(&stat);
    /* 软件队列不可用时不重复采集或构造历史帧；失败不污染运动。 */
    available = 0U;
    for (unsigned i = 0U; i < 100U; i++) { ticks += 10U; nrf_status_telemetry_service(); }
    nrf_telemetry_stats_t after;
    nrf_status_telemetry_get_stats(&after); assert(after.generated == stat.generated);
    available = queue_fail = 1U; ticks += 100U;
    nrf_status_telemetry_service();
    nrf_status_telemetry_get_stats(&after); assert(after.queue_failures == 1U);
    /* 来源读取失败不能生成全零的伪健康帧，恢复后继续当前状态。 */
    reset_live(); read_fail = 1U; nrf_status_telemetry_service();
    nrf_status_telemetry_get_stats(&after); assert(after.generated == 0U);
    read_fail = 0U; nrf_status_telemetry_service();
    nrf_status_telemetry_get_stats(&after); assert(after.generated == 1U);
    /* 持续告警限频，并保留快速和后台帧的刷新机会。 */
    reset_live(); live.remote.link_valid = 0U;
    for (ticks = 0U; ticks <= 2000U; ticks += 10U) nrf_status_telemetry_service();
    assert(alarm_count >= 2U && alarm_count <= 3U && quick_gap <= 150U);
    assert(generated[1] > 0U && generated[2] > 0U);
    /* 时间和状态序号自然回绕，仍按当前时刻刷新。 */
    reset_live(); ticks = UINT32_MAX - 20U; nrf_status_telemetry_service();
    ticks += 10U; nrf_status_telemetry_service(); assert(queued[3] == 0x86U);
    ticks += 90U;
    for (unsigned retry = 0U; retry < 11U && generated[0] < 2U; retry++) {
        nrf_status_telemetry_service(); ticks += 10U;
    }
    assert(generated[0] == 2U);
    reset_live();
    for (unsigned i = 0U; i < 300U; i++) { ticks += 50U; nrf_status_telemetry_service(); }
    nrf_status_telemetry_get_stats(&after); assert(after.last_sequence == (uint8_t)after.generated);
    /* 重建后优先快速帧，强制采集当前状态，保留会话和累计序号。 */
    stat = after; generation++; live.remote.authorized = 0U;
    live.remote.state = REMOTE_SERVO_DISABLED;
    nrf_status_telemetry_service();
    nrf_status_telemetry_get_stats(&after);
    assert(queued[3] == 0x81U && !(queued[14] & 2U));
    assert(after.session == stat.session &&
        after.generated == stat.generated + 1U &&
        after.last_sequence == (uint8_t)(stat.last_sequence + 1U));
}

static void test_sparse_opportunities(void)
{
    const uint32_t intervals[] = {10U, 22U, 50U, 100U};
    nrf_telemetry_stats_t stats;
    for (unsigned scenario = 0U; scenario < 4U; scenario++) {
        reset_live();
#ifdef TEST_REMOTE_MODEL
        remote_status_init(&receiver);
#endif
        for (ticks = 0U; ticks <= 120000U; ticks += 1U) {
            uint32_t count = generated[0] + generated[1] + generated[2] + generated[3] +
                generated[4] + generated[5] + generated[6] + generated[7] + generated[8] + generated[9];
            available = (uint8_t)(ticks % intervals[scenario] == 0U);
            nrf_status_telemetry_service();
#ifdef TEST_REMOTE_MODEL
            nrf_status_telemetry_get_stats(&stats);
            if (stats.generated != count) {
                remote_status_note_payload(&receiver, ticks);
                assert(remote_status_accept(&receiver, queued, 32U, ticks));
                assert(!remote_status_accept(&receiver, queued, 32U, ticks));
            }
            remote_status_tick(&receiver, ticks);
#else
            (void)count;
#endif
        }
        nrf_status_telemetry_get_stats(&stats);
        for (unsigned index = 0U; index < 10U; index++) {

#if !APP_NRF_VEHICLE_SPEED_STATUS_ENABLED
            if (index == 7U) continue;
#endif
            if (stats.queued_by_type[index] < 2U)
                fprintf(stderr, "sparse interval=%lu type=%02X queued=%lu gap=%lu\n",
                    (unsigned long)intervals[scenario], 0x81U + index,
                    (unsigned long)stats.queued_by_type[index],
                    (unsigned long)stats.max_age_ms[index]);
            assert(stats.queued_by_type[index] >= 2U);
            assert(stats.max_age_ms[index] > 0U);
            printf("sparse interval=%lu type=%02X queued=%lu max_gap=%lu overdue=%lu\n",
                (unsigned long)intervals[scenario], 0x81U + index,
                (unsigned long)stats.queued_by_type[index],
                (unsigned long)stats.max_age_ms[index],
                (unsigned long)stats.overdue_count[index]);
        }
        if (intervals[scenario] == 22U) {
            assert(stats.max_age_ms[0] <= 150U && stats.max_age_ms[7] <= 150U);
            assert(stats.max_age_ms[1] <= 300U);
            assert(stats.max_age_ms[2] <= 400U && stats.max_age_ms[3] <= 400U);
            assert(stats.max_age_ms[8] <= 800U);
        }
        assert(quick_gap <= (intervals[scenario] <= 22U ? 150U : 1000U));
#ifdef TEST_REMOTE_MODEL
        assert(receiver.stats.link_losses == 0U);
        /* ACK opportunities may be idle between deadlines. The fast frame is
         * periodic, not a 50 ms heartbeat; round its period to the next ACK
         * opportunity. Disabling optional 0x88 must retain the same contract. */
        printf("accepted_gap interval=%lu actual=%lu bound=%lu\n",
            (unsigned long)intervals[scenario], (unsigned long)receiver.max_accept_gap_ms,
            (unsigned long)(((APP_NRF_FAST_STATUS_PERIOD_MS + intervals[scenario] - 1U) /
                intervals[scenario]) * intervals[scenario]));
        assert(receiver.max_accept_gap_ms <=
            ((APP_NRF_FAST_STATUS_PERIOD_MS + intervals[scenario] - 1U) /
                intervals[scenario]) * intervals[scenario]);
        assert(receiver.stats.accepted == stats.generated);
        if (scenario == 3U) {
            uint32_t previous_accept = receiver.last_received_ms;
            ticks += 400U;
            remote_status_tick(&receiver, ticks);
            assert(remote_status_link(&receiver, ticks) == REMOTE_RETURN_LOST);
            assert(!remote_status_accept(&receiver, queued, 32U, ticks));
            assert(receiver.last_received_ms == previous_accept);
            available = 1U;
            nrf_status_telemetry_service();
            assert(remote_status_accept(&receiver, queued, 32U, ticks));
            assert(receiver.stats.recoveries == 1U);
        }
#endif
    }
    reset_live();
    nrf_status_telemetry_service();
    ticks = 10U; live.remote.link_valid = 0U;
    for (; ticks <= 2000U && queued[3] != 0x87U; ticks += 10U)
        nrf_status_telemetry_service();
    assert(queued[3] == 0x87U);
    nrf_status_telemetry_get_stats(&stats);
    assert(stats.alarm_event_priority_count > 0U);
}

static void test_states(void)
{
    uint8_t selected = APP_SERVO_CONTROL_AXIS == 2U ? 1U : 0U;
    reset_live(); stop_ready = 0U;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 1U, 0U);
    reset_live(); watchdog_ready = 0U;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 2U, 0U);
    reset_live(); watchdog_ready = 0U; watchdog[selected].state = SERVO_WDG_RECOVERY_FAILED;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 10U, 8U);
    reset_live(); zero_ready = 0U; zero[selected].state = SERVO_BOOT_ZERO_FAILED;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 10U, 7U);
    reset_live(); live.remote.authorized = 0U;
    live.remote.estop_latched = 1U; live.remote.stop_reason = REMOTE_STOP_K1;
    hold_ready = 0U; hold[selected].state = SERVO_STARTUP_HOLD_FAILED;
    hold[selected].failure = 5U;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 9U, 2U);
    reset_live(); live.axis[selected].fault = 1U; live.axis[selected].fault_code = 0x35U;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 7U, (uint8_t)(3U + selected));
    reset_live(); health = APP_DIAG_STACK_OVERFLOW;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 10U, 12U);
    reset_live(); hold_active = 1U; live.remote.authorized = 0U;
    live.remote.state = REMOTE_SERVO_DISABLED;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 4U, 0U);
    assert(get16(queued + 20) == (APP_SERVO_CONTROL_AXIS == 2U ? 0x7FFFU : 0U) &&
        get16(queued + 24) ==
        (APP_SERVO_CONTROL_AXIS == 1U ? 0x7FFFU : 0U));
    reset_live(); live.remote.authorized = 0U;
    live.remote.state = REMOTE_SERVO_DISABLED; live.remote.left_commanded_rpm = 300;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 5U, 0U);
    assert(get16(queued + 20) == 0x7FFFU);
}

static void test_cli_states(void)
{
    uint8_t selected = APP_SERVO_CONTROL_AXIS == 2U ? 1U : 0U;
    const servo_control_mode_t modes[] = {
        SERVO_CONTROL_RS485_VELOCITY,
        SERVO_CONTROL_RS485_SEGMENT_POSITION,
        SERVO_CONTROL_PULSE_POSITION
    };
    for (unsigned index = 0U; index < 3U; index++)
    {
        reset_live();
        live.remote.authorized = 0U;
        live.remote.state = REMOTE_SERVO_DISABLED;
        live.remote.target_rpm = live.remote.left_commanded_rpm = 150;
        live.remote.sync.active = live.remote.sync.pair_valid = 1U;
        hold_active = 1U;
        cli_session.state = CLI_SESSION_READY;
        cli_session.authorized = 1U;
        cli_session.axis = (servo_axis_t)selected;
        cli_session.debug_state = SERVO_DEBUG_RUNNING;
        live.axis[selected].control_mode = modes[index];
        check_first(APP_SERVO_READONLY_TEST ? 12U : 7U, 0U);
        assert(queued[13] == (uint8_t)modes[index]);
        assert(!(queued[14] & (2U | 8U | 64U)));
        assert(get16(queued + 16) == 32767U &&
               get16(queued + 18) == 32767U &&
               get16(queued + 20) == 32767U &&
               get16(queued + 24) == 32767U);
    }
    reset_live();
    live.remote.authorized = 0U;
    cli_session.axis = (servo_axis_t)selected;
    cli_session.state = CLI_SESSION_STOPPING;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 8U, 0U);
    reset_live();
    live.remote.authorized = 0U;
    cli_session.axis = (servo_axis_t)selected;
    cli_session.state = CLI_SESSION_LOCKED;
    cli_session.reason = CLI_EXIT_K1;
    cli_session.recoverable = 1U;
    motion = MOTION_FAULT;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 9U, 2U);
    assert(queued[14] & 16U);
    reset_live();
    live.remote.authorized = 0U;
    cli_session.axis = (servo_axis_t)selected;
    cli_session.state = CLI_SESSION_LOCKED;
    cli_session.reason = CLI_EXIT_SAFETY;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 10U, 0U);
}

static void test_k1_hold_states(void)
{
    uint8_t mask = app_config_servo_control_axis_mask();
    reset_live();
    live.remote.authorized = 0U;
    live.remote.estop_latched = 1U;
    live.remote.stop_reason = REMOTE_STOP_K1;
    live.remote.hold_stopping = 1U;
    live.remote.k1_hold.state = SERVO_K1_HOLD_CONFIRMING;
    live.remote.k1_hold.axis_mask = mask;
    hold_active = 1U;
    check_first(APP_SERVO_READONLY_TEST ? 12U : 8U, 2U);
    assert(!(queued[14] & (2U | 8U)) && (queued[14] & 16U));
    for (unsigned variant = 0U; variant < 5U; variant++)
    {
        reset_live();
        live.remote.authorized = 0U;
        live.remote.state = REMOTE_SERVO_ESTOP;
        live.remote.estop_latched = 1U;
        live.remote.stop_reason = REMOTE_STOP_K1;
        live.remote.k1_hold.state = SERVO_K1_HOLD_READY;
        live.remote.k1_hold.axis_mask = mask;
        live.remote.k1_hold.valid = 1U;
        live.remote.k1_hold.zero_mask = mask;
        for (unsigned axis = 0U; axis < 2U; axis++)
            live.axis[axis].enabled = 1U;
        if (variant == 1U)
        {
            live.remote.k1_hold.zero_mask = 0U;
            live.remote.k1_hold.disturbed = 1U;
        }
        else if (variant == 2U)
        {
            live.remote.k1_hold.valid = 0U;
            live.remote.k1_hold.state = SERVO_K1_HOLD_FAILED;
            motion = MOTION_FAULT;
        }
        else if (variant == 3U)
            health = APP_DIAG_HEARTBEAT_TIMEOUT;
        else if (variant == 4U)
            stop[APP_SERVO_CONTROL_AXIS == 2U ? 1U : 0U].state =
                SERVO_STARTUP_STOP_FAILED;
        check_first(APP_SERVO_READONLY_TEST ? 12U : variant >= 2U ? 10U : 9U,
                    variant == 3U ? 12U : 2U);
        assert(!(queued[14] & 2U) && (queued[14] & 16U));
        assert((queued[14] & 8U) == (variant == 0U ? 8U : 0U));
        if (variant == 0U) assert(queued[13] == 7U);
        /* 告警及低频轮询可能先返回其他帧，等待轴状态而不假定相邻帧类型。 */
        for (unsigned attempt = 0U; attempt < 10U && queued[3] != 0x82U; attempt++)
        {
            ticks++;
            nrf_status_telemetry_service();
        }
        assert(queued[3] == 0x82U);
        for (unsigned axis = 0U; axis < 2U; axis++)
            if (mask & (1U << axis)) assert(queued[12U + axis] & 2U);
    }
}
#endif

int main(void)
{
    nrf_status_input_t in;
    uint8_t frame[32];
    memset(&in, 0, sizeof(in));
    in.axis_flags = 0x37U; in.valid_flags = 0x7FU; in.gate_ready = 1U;
    in.remote.target_rpm = -600; in.remote.commanded_rpm = -100;
    in.remote.left_commanded_rpm = -99; in.remote.right_commanded_rpm = -101;
    in.system_state = 7U; in.mode = 4U;
    in.axis[0].actual_speed_rpm = -98; in.axis[1].actual_speed_rpm = -102;
    for (unsigned axis = 0U; axis < 2U; axis++) {
        in.axis[axis].online = in.axis[axis].enabled = 1U;
        in.axis[axis].fault_valid = in.axis[axis].speed_valid = 1U;
        in.axis[axis].parameter_verified = 1U; in.axis[axis].speed_age_ms = 30U;
    }
    assert(nrf_remote_crc16((const uint8_t *)"123456789", 9U) == 0x29B1U);
    for (uint8_t type = 0x81U; type <= 0x89U; type++) {
        assert(nrf_status_encode(frame, type, 255U, 0x2CU, &in));
        check_common(frame, type); assert(frame[4] == 255U && frame[6] == 0x2CU);
        assert(!(frame[8] & 64U));
    }
    assert(!nrf_status_encode(frame, 0x80U, 0U, 0U, &in));
    assert(!nrf_status_encode(frame, 0x8BU, 0U, 0U, &in));
    assert(!nrf_status_encode(NULL, 0x81U, 0U, 0U, &in));
    nrf_status_encode(frame, 0x81U, 0U, 0U, &in);
    assert((int16_t)get16(frame + 16) == -600 && (int16_t)get16(frame + 22) == -98);
    nrf_status_encode(frame, 0x82U, 0U, 0U, &in);
    assert(frame[12] == 91U && frame[13] == 91U && get16(frame + 22) == 30U);
    in.axis[0].speed_age_ms = APP_SERVO_STATUS_MAX_AGE_MS;
    in.axis[0].fault_age_ms = APP_SERVO_STATUS_MAX_AGE_MS;
    in.axis[1].poll_failures = 999U;
    nrf_status_encode(frame, 0x82U, 0U, 0U, &in);
    assert(get16(frame + 14) == 65535U && get16(frame + 18) == 32767U && frame[27] == 255U);
    in.axis_flags = 1U;
    nrf_status_encode(frame, 0x81U, 0U, 0U, &in);
    assert(get16(frame + 24) == 32767U && get16(frame + 26) == 32767U);
    nrf_status_encode(frame, 0x84U, 0U, 0U, &in); assert(frame[13] == 6U);
    in.position.axis[0].raw_position = INT32_MIN + 4;
    in.position.axis[1].raw_position = INT32_MAX - 4;
    in.position.difference_mm = -52.6;
    in.position.vehicle_boot_net_mm = -1850.0;
    in.position.vehicle_boot_path_mm = 2500.0; in.position.vehicle_trip_path_mm = 50.0;
    nrf_status_encode(frame, 0x83U, 0U, 0U, &in);
    assert((int32_t)get32(frame + 12) == INT32_MIN + 4);
    assert((int32_t)get32(frame + 16) == INT32_MAX - 4);
    assert((int16_t)get16(frame + 20) == -53 && (int16_t)get16(frame + 22) == -19);
    assert(get16(frame + 24) == 25U && get16(frame + 26) == 1U);
    in.position.difference_mm = 99999.0; in.position.vehicle_boot_path_mm = 9e10;
    nrf_status_encode(frame, 0x83U, 0U, 0U, &in);
    assert(get16(frame + 20) == 32767U && get16(frame + 24) == 65535U && frame[8] & 128U);
    nrf_status_encode(frame, 0x85U, 0U, 0U, &in);
    assert(frame[12] == 0U && frame[13] == 255U && !(frame[8] & 64U));
    nrf_status_encode(frame, 0x86U, 0U, 0U, &in);
    assert(get16(frame + 12) == 10000U && get16(frame + 16) == 185U);
    assert(get16(frame + 22) == 2667U && frame[26] == 1U && frame[27] == 1U);
    in.alarm_sequence = 255U; in.alarm_level = 4U; in.alarm_code = 8U;
    in.alarm_tick_ms = 0x12345678U; in.alarm_detail = 0x98765432U;
    nrf_status_encode(frame, 0x87U, 0U, 0U, &in);
    assert(frame[12] == 255U && frame[14] == 8U && get32(frame + 20) == in.alarm_tick_ms);
    assert(get32(frame + 24) == in.alarm_detail);
#if APP_NRF_TELEMETRY_ENABLED
    test_schedule(); test_sparse_opportunities(); test_states(); test_cli_states(); test_k1_hold_states();
#else
    nrf_telemetry_stats_t stat;
    nrf_status_telemetry_init(0x44U); nrf_status_telemetry_service();
    nrf_status_telemetry_get_stats(&stat); assert(!stat.enabled && !stat.generated);
#endif
    puts("PASS NRF telemetry: 32-byte layouts/CRC/units/invalid/saturation/scheduling/states");
    return 0;
}
