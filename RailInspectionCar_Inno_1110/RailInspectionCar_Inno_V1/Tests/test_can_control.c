#include "app_can_control.h"
#include "app_control_arbiter.h"
#include "app_config.h"
#include "app_cli_servo_session.h"
#include "app_remote_servo.h"
#include "app_health.h"
#include "can_protocol.h"
#include "task.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "link_diag_capture.h"
#if APP_LINK_DIAG_ACTIVE
/* The CAN host fixture omits the F407 servo module; keep the production
 * startup-cause seam inert while still linking every CAN stop path. */
void aimtor_diag_startup_stop(uint8_t source, uint8_t axis,
    uint16_t reason, uint32_t detail)
{ (void)source; (void)axis; (void)reason; (void)detail; }
void app_hmi_link_diag_stop(const app_control_stop_t *stop,uint16_t kind)
{ (void)stop;(void)kind; }
void app_hmi_link_diag_upgrade(const app_control_stop_t *stop,uint16_t requester,
    uint32_t sequence,uint16_t command)
{(void)stop;(void)requester;(void)sequence;(void)command;}
#endif

static uint32_t tick, calls[2], emergency_calls;
static uint32_t remote_stops, cli_stops, hmi_stops;
static bool watchdog, k1, fresh_ok, write_fail, no_complete, move_feedback, enabled;
static servo_snapshot_t feedback_axis[2];
static uint32_t due[2];
#ifdef NORMAL_TIMING_TEST
static uint32_t timing_delay;
#endif
static int16_t write_rpm[2];
static uint16_t seq, request_seq;
static uint8_t failed_axes;
static app_can_control_status_t state;
uint32_t HAL_GetTick(void) { return tick; }
TickType_t xTaskGetTickCount(void) { return tick; }
bool app_health_watchdog_test_requested(void) { return watchdog; }
bool app_remote_servo_k1_latched(void) { return k1; }
void app_remote_servo_can_k5_stop_begin(uint32_t event, uint8_t axes) { (void)event; (void)axes; }
bool app_remote_servo_can_k5_stop_complete(uint32_t event, uint8_t axes)
{ (void)event; (void)axes; k1 = false; return true; }
uint8_t app_config_motion_hardware_ready(void) { return 1U; }
uint8_t app_config_servo_bus_ready(void) { return 1U; }
void aimtor_get_recovery_status(servo_recovery_status_t *out) { memset(out, 0, sizeof(*out)); }
servo_result_t aimtor_request_recovery(uint8_t source) { (void)source; return SERVO_NOT_READY; }
bool aimtor_watchdog_recovery_ready(void) { return true; }
bool aimtor_startup_stop_ready(void) { return true; }
bool aimtor_boot_zero_ready(void) { return true; }
void app_remote_servo_request_estop(void) { ++remote_stops; }
void app_cli_servo_session_stop(void) {}
servo_result_t app_cli_servo_session_off(void) { ++cli_stops; return SERVO_OK; }
bool app_cli_servo_session_get_status(cli_servo_session_status_t *out) { memset(out, 0, sizeof(*out)); return true; }
bool app_remote_servo_get_status(remote_servo_status_t *out) { memset(out, 0, sizeof(*out)); return true; }
void app_hmi_control_request_stop(void) { ++hmi_stops; }
servo_result_t aimtor_k1_hold_request(uint8_t mask) { (void)mask; return SERVO_OK; }
bool aimtor_k1_hold_get_status(servo_k1_hold_status_t *out) { memset(out, 0, sizeof(*out)); out->valid = 1U; return true; }
servo_result_t aimtor_k1_hold_release(void) { return SERVO_OK; }
void aimtor_set_can_control(bool authorized, uint8_t mask) { enabled = authorized && mask; }
void aimtor_can_revoke(uint32_t generation) { (void)generation; enabled = false; }
bool aimtor_safety_stop_active(void) { return false; }
void aimtor_request_controlled_safety_stop(uint8_t offline) { (void)offline; }
void aimtor_emergency_stop(void) { ++emergency_calls; }
bool aimtor_get_snapshot(servo_axis_t axis, servo_snapshot_t *out)
{ *out = feedback_axis[axis]; return true; }
bool aimtor_get_snapshot_pair(servo_snapshot_t *left, servo_snapshot_t *right)
{ *left = feedback_axis[0]; *right = feedback_axis[1]; return true; }
servo_result_t aimtor_can_velocity(servo_axis_t axis, int16_t rpm, uint32_t generation, uint32_t batch, bool stop)
{
    servo_snapshot_t *a = &feedback_axis[axis];
    (void)generation; (void)batch;
    assert(APP_SERVO_CONTROL_AXIS & (1U << axis));
    assert(enabled || stop);
    if (a->busy) return SERVO_BUSY;
#ifdef NORMAL_TIMING_TEST
    if (feedback_axis[0].busy || feedback_axis[1].busy) return SERVO_BUSY;
#endif
    if (stop) assert((rpm >= 0 && write_rpm[axis] >= rpm) || (rpm <= 0 && write_rpm[axis] <= rpm));
    ++calls[axis]; ++a->speed_write_sequence; a->busy = 1U;
    due[axis] = tick + 3U; write_rpm[axis] = rpm;
#ifdef NORMAL_TIMING_TEST
    if (timing_delay) due[axis] = tick + timing_delay;
#endif
    return SERVO_OK;
}
static void cycle(void)
{
    unsigned i, k;
    for (i = 0U; i < 2U; ++i)
    {
        servo_snapshot_t *a = &feedback_axis[i];
        if (a->busy && !no_complete && (int32_t)(tick - due[i]) >= 0)
        {
            a->busy = 0U; a->speed_write_completed = a->speed_write_sequence;
            a->speed_write_result = write_fail && (failed_axes & (1U << i)) ? SERVO_NOT_READY : SERVO_OK;
            if (!write_fail && move_feedback) a->actual_speed_rpm = write_rpm[i];
        }
        if (fresh_ok && tick % 10U == 0U)
        {
            ++a->speed_sequence; a->speed_sample_tick = tick;
            for (k = 1U; k < SERVO_SPEED_SAMPLE_CAPACITY; ++k) a->speed_samples[k-1] = a->speed_samples[k];
            a->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].speed_sequence = a->speed_sequence;
            a->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].sample_tick = tick;
            a->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].actual_speed_rpm = a->actual_speed_rpm;
            a->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].valid = 1U;
            a->enable_feedback_tick = tick;
        }
        a->speed_age_ms = tick - a->speed_sample_tick;
        a->fault_age_ms = fresh_ok ? 0U : a->speed_age_ms;
    }
    app_control_arbiter_service(); app_can_control_pre_service(); app_can_control_service();
    assert(app_can_control_get_status(&state));
    { app_can_control_result_t r; while (app_can_control_take_result(&r)) assert(r.phase >= 1U); }
}
static void advance(uint32_t ms, bool heartbeat)
{
    while (ms--) { ++tick; if (heartbeat) app_can_control_link(42U, 1U, 7U, tick, true, true); cycle(); }
}
static void reset(void)
{
    unsigned i;
    can_identity_t identity;
    memset(&identity, 0, sizeof(identity)); identity.result = CAN_IDENTITY_OK;
    identity.motion_first = 100U; identity.motion_last = 131U;
    tick = 100U; seq = request_seq = 0U;
    watchdog = k1 = write_fail = no_complete = enabled = false;
    fresh_ok = move_feedback = true; emergency_calls = 0U;
    failed_axes = 3U;
    remote_stops = cli_stops = hmi_stops = 0U;
    memset(calls, 0, sizeof(calls)); memset(feedback_axis, 0, sizeof(feedback_axis)); memset(write_rpm, 0, sizeof(write_rpm));
    for (i = 0U; i < 2U; ++i)
    {
        servo_snapshot_t *a = &feedback_axis[i];
        a->online = a->enabled = a->parameter_verified = a->speed_valid = a->fault_valid = 1U;
        a->enable_feedback_valid = a->enable_feedback = 1U;
        a->enable_feedback_tick = a->speed_sample_tick = tick;
        a->control_mode = SERVO_CONTROL_RS485_VELOCITY;
    }
    app_control_arbiter_init(); app_can_control_init(&identity);
    app_can_control_link(42U, 1U, 7U, tick, true, true); cycle();
}
static uint8_t request(uint8_t command, uint16_t param)
{
    uint8_t data[8] = {0}; data[0] = (uint8_t)++request_seq; data[1] = command;
    can_protocol_write_u16(data + 2U, param); can_protocol_write_u16(data + 6U, 42U);
    return app_can_control_submit(data, tick, 1U);
}
static bool speed(int16_t mm, uint16_t lease)
{
    uint8_t data[8]; can_protocol_write_u16(data, ++seq);
    can_protocol_write_u16(data + 2U, (uint16_t)mm);
    can_protocol_write_u16(data + 4U, state.motion_token);
    can_protocol_write_u16(data + 6U, lease);
    return app_can_control_speed(data, tick);
}
#if APP_CAN_CONTROL_ENABLED
static void grant_owner(app_control_owner_t owner)
{
    uint32_t ticket = app_control_arbiter_submit(owner, APP_SERVO_CONTROL_AXIS);
    assert(ticket); app_control_arbiter_service();
    assert(app_control_arbiter_poll(ticket, NULL) == 1);
    assert(app_control_arbiter_request(owner, APP_SERVO_CONTROL_AXIS, NULL));
}
static void acquire(void)
{
    assert(request(4U, 1U) == 0U); advance(3U, true);
    if (!state.authorized) { app_can_control_result_t r; printf("grant failed tick=%lu state=%u owner=%u lock=%u\n", (unsigned long)tick, (unsigned)state.state, (unsigned)app_control_arbiter_owner(), (unsigned)app_control_arbiter_lock_reason()); while (app_can_control_take_result(&r)) printf("result seq=%u code=%u phase=%u detail=%u\n",r.sequence,r.result,r.phase,r.detail); fflush(stdout); }
    assert(state.authorized && state.motion_token && app_control_arbiter_owner() == APP_CONTROL_OWNER_CAN);
    assert(!speed(100, 300U)); assert(speed(0, 300U)); advance(8U, true);
    assert(state.first_zero_done);
}
static void drive(int16_t target, uint32_t ms)
{
    while (ms) { assert(speed(target, 300U)); advance(ms < 100U ? ms : 100U, true); ms = ms > 100U ? ms-100U : 0U; }
}
static void test_stop(void)
{
    uint16_t old;
    reset(); acquire(); old = state.motion_token; drive(1000, 500U);
    assert(state.target_mm_s == 1000 && state.actual_valid);
    assert(request(4U, 0U) == 0U); advance(1U, true);
    assert(!state.authorized && state.motion_token == 0U && state.stop_phase);
    assert(!speed(100, 300U)); advance(4000U, true);
    assert(state.state == CAN_CONTROL_DONE && state.stop_phase == 4U && state.stop_result == 0U);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE && emergency_calls == 0U);
    acquire(); assert(state.motion_token != old);
    assert(calls[0] == 0U || (APP_SERVO_CONTROL_AXIS & 1U));
    assert(calls[1] == 0U || (APP_SERVO_CONTROL_AXIS & 2U));
}
static void test_timeout_fault(void)
{
    reset(); acquire(); drive(1000, 400U); advance(301U, true);
    assert(!state.authorized && state.stop_phase != 0U); advance(4000U, true);
    assert(state.stop_phase == 4U && !emergency_calls);
    reset(); acquire(); no_complete = true; assert(speed(500, 300U));
    advance(250U, true); assert(speed(500, 300U)); advance(250U, true);
    advance(1200U, true); assert(state.state == CAN_CONTROL_FAULT && emergency_calls);
    reset(); acquire(); drive(100, 100U); watchdog = true; cycle();
    assert(!state.authorized && state.state == CAN_CONTROL_FAULT);
    assert(app_control_arbiter_submit(APP_CONTROL_OWNER_CLI, 1U) == 0U);
}
static void test_fifo_and_nonowner_loss(void)
{
    static const app_control_owner_t owners[3] = {
        APP_CONTROL_OWNER_CAN, APP_CONTROL_OWNER_HMI, APP_CONTROL_OWNER_REMOTE
    };
    unsigned first, second, third, i;
    uint32_t tickets[3];
    uint8_t stop[8] = {1U, 0U, 42U, 0U, 1U, 3U, 0U, 0U};
    for (first = 0U; first < 3U; ++first)
        for (second = 0U; second < 3U; ++second)
        {
            if (first == second) continue;
            third = 3U - first - second;
            reset();
            tickets[0] = app_control_arbiter_submit(owners[first], APP_SERVO_CONTROL_AXIS);
            tickets[1] = app_control_arbiter_submit(owners[second], APP_SERVO_CONTROL_AXIS);
            tickets[2] = app_control_arbiter_submit(owners[third], APP_SERVO_CONTROL_AXIS);
            assert(tickets[0] && tickets[1] && tickets[2]);
            app_control_arbiter_service();
            assert(app_control_arbiter_owner() == owners[first]);
            assert(app_control_arbiter_poll(tickets[0], NULL) == 1);
            assert(app_control_arbiter_poll(tickets[1], NULL) < 0);
            assert(app_control_arbiter_poll(tickets[2], NULL) < 0);
        }
    for (i = 1U; i < 3U; ++i)
    {
        reset(); grant_owner(owners[i]);
        app_can_control_link(0U, 2U, 0U, tick, false, false);
        advance(APP_CAN_LINK_TIMEOUT_MS + 1U, false);
        assert(app_control_arbiter_owner() == owners[i]);
        assert(!emergency_calls && !remote_stops && !hmi_stops);
        assert(calls[0] + calls[1] == 0U);
    }
    reset(); assert(request(4U, 1U) == 0U); advance(3U, true);
    assert(state.authorized && !state.first_zero_done);
    assert(app_can_control_stop(stop, tick)); advance(1U, true);
    assert(!state.authorized && !state.motion_token);
    assert(!app_control_arbiter_submit(APP_CONTROL_OWNER_HMI, APP_SERVO_CONTROL_AXIS));
    advance(5000U, true);
    assert(state.stop_phase == 4U && app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    acquire();
    puts("PASS all six FIFO source orders, non-owner CAN loss isolation and emergency before first zero");
}

static void test_arbitration_and_sequences(void)
{
    uint8_t data[8] = {0}; uint32_t received;
    reset(); assert(app_control_arbiter_submit(APP_CONTROL_OWNER_REMOTE, APP_SERVO_CONTROL_AXIS));
    assert(request(4U, 1U) == 0U); advance(3U, true);
    assert(!state.authorized && app_control_arbiter_owner() == APP_CONTROL_OWNER_REMOTE);
    reset(); acquire(); drive(100, 100U); received = tick;
    can_protocol_write_u16(data, seq); can_protocol_write_u16(data + 2U, 100U);
    can_protocol_write_u16(data + 4U, state.motion_token); can_protocol_write_u16(data + 6U, 300U);
    assert(!app_can_control_speed(data, tick)); data[2] ^= 1U;
    assert(!app_can_control_speed(data, tick)); advance(201U, true);
    assert(!state.authorized && tick - received == 201U);
    reset(); app_can_control_link(42U, 1U, 7U, tick, true, false);
    assert(request(4U, 1U) == 0U); cycle(); assert(!state.authorized);
}
static void test_reverse_global_tick(void)
{
    uint8_t data[8] = {0};
    reset(); acquire(); drive(100, 400U); drive(-100, 1500U);
    assert(state.authorized && state.target_mm_s == -100 &&
        app_can_control_rpm_mm_s(state.commanded_rpm[APP_SERVO_CONTROL_AXIS == 2U ? 1U : 0U],
            APP_SERVO_CONTROL_AXIS == 2U ? 1U : 0U) < 0);
    can_protocol_write_u16(data, 1U); can_protocol_write_u16(data + 2U, 42U);
    assert(app_can_control_stop(data, tick)); advance(3000U, true);
    assert(state.stop_phase == 4U && state.stop_sequence == 1U);
    acquire(); assert(!app_can_control_stop(data, tick)); cycle(); assert(state.authorized);
    reset(); tick = UINT32_MAX - 40U;
    feedback_axis[0].speed_sample_tick = feedback_axis[1].speed_sample_tick = tick;
    feedback_axis[0].enable_feedback_tick = feedback_axis[1].enable_feedback_tick = tick;
    app_can_control_link(42U, 1U, 7U, tick, true, true);
    cycle(); acquire(); drive(100, 100U); assert(state.authorized);
    advance(301U, true); assert(!state.authorized);
}
static void test_extended_safety(void)
{
    uint8_t data[8] = {0}; unsigned i; uint32_t frozen, ticket;
    reset(); assert(request(4U, 1U) == 0U); advance(3U, true);
    assert(state.authorized && !state.first_zero_done);
    advance(1001U, true); assert(!state.authorized); advance(3000U, true);
    assert(state.stop_phase == 4U);
    reset(); acquire(); frozen = tick;
    for (i = 0U; i < 11U; ++i)
    {
        assert(speed(100, 300U)); advance(100U, false);
        if (tick - frozen >= APP_CAN_LINK_TIMEOUT_MS) break;
    }
    assert(!state.authorized && state.stop_reason == 2U);
    reset(); acquire(); drive(100, 100U);
    app_can_control_link(43U, 2U, 8U, tick, true, true); cycle();
    assert(!state.authorized && !speed(100, 300U));
    reset(); acquire(); drive(100, 100U); k1 = true; cycle();
    assert(!state.authorized && state.stop_reason == 1U); advance(3000U, true);
    assert(state.stop_phase == 4U && !emergency_calls);
    reset(); acquire(); drive(100, 100U); fresh_ok = false; advance(APP_SERVO_STATUS_MAX_AGE_MS + 1U, true);
    assert(state.state == CAN_CONTROL_FAULT && emergency_calls);
    reset(); assert(request(4U, 1U) == 0U); advance(3U, true);
    seq = 65534U; assert(speed(0, 300U)); advance(8U, true);
    assert(speed(0, 300U)); advance(5U, true); assert(state.speed_sequence == 0U);
    can_protocol_write_u16(data, 1U); can_protocol_write_u16(data + 4U, (uint16_t)(state.motion_token + 1U));
    can_protocol_write_u16(data + 6U, 300U); assert(!app_can_control_speed(data, tick));
    reset(); acquire(); write_fail = true; assert(speed(100, 300U)); advance(80U, true);
    assert(state.state == CAN_CONTROL_FAULT && state.failed_mask);
    reset(); ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_HMI, APP_SERVO_CONTROL_AXIS);
    assert(ticket); app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
    assert(app_control_arbiter_clear_safety()); app_control_arbiter_service();
    assert(app_control_arbiter_poll(ticket, NULL) < 0 && app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    reset(); acquire(); drive(100, 120000U); assert(state.authorized && !emergency_calls);
    assert(request(6U, 50U) == 0U); cycle(); assert(!state.authorized && state.limit_mm_s == 50U);
    advance(3000U, true); acquire(); assert(state.limit_mm_s == app_can_control_limit());
    reset(); for (i = 0U; i < 32U; ++i)
    {
        acquire(); assert(request(4U, 0U) == 0U); advance(3000U, true);
        assert(state.stop_phase == 4U);
    }
    assert(request(4U, 1U) == 0U); advance(3U, true); assert(!state.authorized);
    puts("PASS CAN extended: heartbeat/first zero/reboot/K1/stale/wrap/write failure/old reservation/120s/token exhaustion");
}
static void test_global_other_owners(void)
{
    uint8_t data[8] = {0};
    app_control_owner_t owners[] = {APP_CONTROL_OWNER_REMOTE, APP_CONTROL_OWNER_CLI, APP_CONTROL_OWNER_HMI};
    unsigned i;
    for (i = 0U; i < 3U; ++i)
    {
        reset(); grant_owner(owners[i]);
        can_protocol_write_u16(data, 1U); can_protocol_write_u16(data + 2U, 42U);
        assert(app_can_control_stop(data, tick)); cycle();
        assert(remote_stops + cli_stops + hmi_stops == 1U);
        assert(state.stop_phase != 4U && calls[0] + calls[1] == 0U);
        assert(!app_can_control_stop(data, tick));
        app_control_arbiter_release(owners[i]);
        advance(50U, true);
        if (owners[i] != APP_CONTROL_OWNER_REMOTE) assert(state.stop_phase == 4U);
    }
    puts("PASS global stop: dispatch original owner strategy; no CAN servo writes; no premature completion");
    reset(); grant_owner(APP_CONTROL_OWNER_HMI);
    assert(app_can_control_stop(data, tick)); cycle();
    app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
    advance(50U, true);
    assert(state.stop_phase == 5U && state.stop_result != 0U && emergency_calls);
    assert(app_control_arbiter_submit(APP_CONTROL_OWNER_CAN, APP_SERVO_CONTROL_AXIS) == 0U);
    reset(); assert(app_can_control_stop(data, tick)); advance(50U, true);
    assert(state.stop_phase == 4U);
    acquire();
    reset();
    {
        uint32_t old_ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_HMI, APP_SERVO_CONTROL_AXIS);
        uint32_t new_ticket;
        app_control_arbiter_service(); assert(app_control_arbiter_poll(old_ticket, NULL) == 1);
        app_control_arbiter_release(APP_CONTROL_OWNER_HMI);
        new_ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_HMI, APP_SERVO_CONTROL_AXIS);
        app_control_arbiter_service(); assert(app_control_arbiter_poll(new_ticket, NULL) == 1);
        assert(app_control_arbiter_poll(old_ticket, NULL) < 0);
        app_control_arbiter_cancel(old_ticket);
        assert(app_control_arbiter_poll(new_ticket, NULL) == 1);
    }
    puts("PASS safety lock excludes false stop success; old reservation cannot cancel new generation");
}
static void test_mechanical_and_partial_failure(void)
{
    unsigned axis;
    double max_rpm_time = 1000.0 / (APP_DESIGN_ACCELERATION_MPS2 * 1000.0) * 1000.0;
    reset(); acquire(); drive(1000, (uint32_t)(max_rpm_time + 3000.0));
    for (axis = 0U; axis < 2U; ++axis)
        if (APP_SERVO_CONTROL_AXIS & (1U << axis))
        {
            double physical = app_can_control_rpm_mm_s(state.commanded_rpm[axis], (uint8_t)axis);
            assert(fabs(physical) > 0 && fabs(fabs(physical) - 1000.0) < 30.0);
            assert(abs((int)state.commanded_rpm[axis]) <= APP_DEBUG_MAX_RPM);
        }
    drive(-1000, 5000U); /* Includes zero confirmation and sync-priority allocation. */
    for (axis = 0U; axis < 2U; ++axis)
        if (APP_SERVO_CONTROL_AXIS & (1U << axis))
            assert(app_can_control_rpm_mm_s(state.commanded_rpm[axis], (uint8_t)axis) < 0);
    if (APP_SERVO_CONTROL_AXIS == 3U)
    {
        reset(); acquire(); write_fail = true; failed_axes = 2U;
        assert(speed(100, 300U)); advance(40U, true);
        assert(state.state == CAN_CONTROL_FAULT && state.failed_mask == 2U && emergency_calls);
        assert(!state.authorized);
    }
    puts("PASS per-axis mechanics: wheel/gear/direction/limit, reversal and one-axis failure");
}
static void test_scheduler_and_zero_confirmation(void)
{
    uint8_t data[8] = {0}; unsigned i;
    reset(); acquire(); drive(100, 100U);
    /* CAN接收任务不运行，伺服服务必须独立按接收年龄撤权。 */
    tick += APP_CAN_SPEED_TIMEOUT_MS;
    for (i = 0U; i < 2U; ++i)
    {
        feedback_axis[i].speed_sample_tick = feedback_axis[i].enable_feedback_tick = tick;
        feedback_axis[i].speed_age_ms = feedback_axis[i].fault_age_ms = 0U;
    }
    app_can_control_pre_service(); app_can_control_service();
    assert(app_can_control_get_status(&state)); assert(!state.authorized && state.stop_phase);
    reset(); acquire(); drive(100, 100U);
    assert(request(5U, 0U) == 0U);
    for (i = 0U; i < 1000U; ++i)
    {
        advance(1U, true);
        if (state.state == CAN_CONTROL_ZERO && state.pending_mask == 0U) break;
    }
    assert(state.state == CAN_CONTROL_ZERO && state.stop_phase != 4U);
    /* 伪造新鲜年龄但不递增样本序号，不能通过两次实际零速门禁。 */
    for (i = 0U; i < 2U; ++i)
    {
        feedback_axis[i].actual_speed_rpm = 0;
        feedback_axis[i].busy = 0U;
        feedback_axis[i].speed_age_ms = feedback_axis[i].fault_age_ms = 0U;
    }
    for (i = 0U; i < 5U; ++i) app_can_control_service();
    assert(app_can_control_get_status(&state)); assert(state.stop_phase != 4U);
    reset(); acquire();
    can_protocol_write_u16(data, ++seq); can_protocol_write_u16(data + 2U, 100U);
    can_protocol_write_u16(data + 4U, state.motion_token); can_protocol_write_u16(data + 6U, 100U);
    assert(!app_can_control_speed(data, tick - 100U));
    puts("PASS stalled CAN task/expired queued speed/repeated zero samples cannot retain or finish motion");
}
static void test_communication_degraded_output(void)
{
    int16_t boundary[2];
    unsigned axis;
    reset(); acquire(); drive(400, 300U);
    advance(10U, true);
    for (axis = 0U; axis < 2U; ++axis)
    {
        boundary[axis] = write_rpm[axis];
        feedback_axis[axis].feedback_degraded = 1U;
        feedback_axis[axis].feedback_failures[0] = 4U;
        feedback_axis[axis].consecutive_failures = 4U;
    }
    drive(900, 150U);
    assert(state.authorized && !emergency_calls && !state.sync.trip);
    for (axis = 0U; axis < 2U; ++axis)
        if (APP_SERVO_CONTROL_AXIS & (1U << axis))
            assert(abs(write_rpm[axis]) <= abs(boundary[axis]));
    for (axis = 0U; axis < 2U; ++axis)
    {
        boundary[axis] = write_rpm[axis];
        feedback_axis[axis].feedback_degraded = 0U;
        feedback_axis[axis].feedback_failures[0] = 0U;
        feedback_axis[axis].consecutive_failures = 0U;
    }
    advance(10U, true);
    for (axis = 0U; axis < 2U; ++axis)
        if (APP_SERVO_CONTROL_AXIS & (1U << axis))
            assert(abs(write_rpm[axis] - boundary[axis]) <= 10);
    for (axis = 0U; axis < 2U; ++axis) feedback_axis[axis].feedback_degraded = 1U;
    drive(-400, 200U);
    assert(state.authorized && !emergency_calls);
    for (axis = 0U; axis < 2U; ++axis)
        if (APP_SERVO_CONTROL_AXIS & (1U << axis)) assert(write_rpm[axis] >= 0);
    puts("PASS CAN degraded: four failures retain session, bound output, no recovery jump or reversal");
}
static void test_quick_stop(void)
{
    uint8_t data[8] = {0};
    uint32_t event, deadline;
    unsigned owner_index;
    const app_control_owner_t owners[] = {APP_CONTROL_OWNER_HMI, APP_CONTROL_OWNER_REMOTE, APP_CONTROL_OWNER_CLI};
    app_control_stop_t stop; can_protocol_record_t binding_query,binding_reply;
    /* 已有仲裁事件的速率必须原样执行，500不再意味着快速停车。 */
    reset(); acquire(); drive(800, 8000U);
    assert(app_control_arbiter_submit_stop(4U, 500U, 1U, 0x10U));
    advance(20U, true);
    assert(fabs(state.stop_deceleration - 0.50f) < 0.00001f);
    reset(); acquire(); drive(800, 8000U);
    assert(request(5U, 0U) == 0U); advance(20U, true);
    assert(fabs(state.stop_deceleration - APP_CAN_NORMAL_STOP_DECEL_MPS2) < 0.00001f);
    event = state.stop_generation; deadline = state.stop_timeout_ms;
    can_protocol_write_u16(data, 1U); can_protocol_write_u16(data + 2U, 42U); data[4] = 1U;
    assert(app_can_control_stop(data, tick)); advance(10U, true);
    assert(fabs(state.stop_deceleration - APP_CAN_QUICK_STOP_DECEL_MPS2) < 0.00001f);
    assert(state.stop_generation == event && state.stop_timeout_ms == deadline);
    assert(!state.authorized && state.motion_token == 0U && state.stop_sequence == 1U);
    assert(!app_can_control_stop(data, tick));
    can_protocol_write_u16(data, 2U); data[4] = 0U;
    assert(app_can_control_stop(data, tick)); advance(10U, true);
    assert(fabs(state.stop_deceleration - APP_CAN_QUICK_STOP_DECEL_MPS2) < 0.00001f && state.stop_generation == event);
    advance(5000U, true); assert(state.stop_phase == 4U);
    acquire(); assert(!app_can_control_stop(data, tick)); advance(10U, true); assert(state.authorized);
    reset(); acquire(); assert(request(5U, 1U) == 0U); advance(30U, true);
    assert(fabs(state.stop_deceleration - APP_CAN_QUICK_STOP_DECEL_MPS2) < 0.00001f);
    for (owner_index = 0U; owner_index < sizeof(owners) / sizeof(owners[0]); ++owner_index)
    {
        reset(); grant_owner(owners[owner_index]);
        can_protocol_write_u16(data, 1U); data[4] = 1U;
        assert(app_can_control_stop(data, tick)); cycle();
        app_control_arbiter_get_stop(&stop);
        assert(stop.owner == owners[owner_index] && stop.deceleration_mm_s2 == (uint16_t)(APP_CAN_QUICK_STOP_DECEL_MPS2 * 1000.0f + 0.5f) && stop.command == 0x080U);
        assert(remote_stops + cli_stops + hmi_stops == 0U);
        memset(&binding_query,0,sizeof(binding_query));binding_query.type=0x5EU;binding_query.length=48U;binding_query.payload[8]=10U;
        can_protocol_write_u32(binding_query.payload,0x334D5241UL);can_protocol_write_u32(binding_query.payload+4U,1U);
        can_protocol_write_u32(binding_query.payload+24U,1U);can_protocol_write_u32(binding_query.payload+28U,can_protocol_crc32(data,8U));
        assert(app_can_control_stop_binding(&binding_query,&binding_reply,tick));
        assert(binding_reply.length==112U && binding_reply.payload[42]==(owners[owner_index]==APP_CONTROL_OWNER_REMOTE?2U:owners[owner_index]==APP_CONTROL_OWNER_HMI?3U:4U));
    }
    reset();
    { uint32_t ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_HMI, APP_SERVO_CONTROL_AXIS);
      assert(app_can_control_stop(data, tick)); app_can_control_pre_service();
      app_control_arbiter_service(); assert(app_control_arbiter_poll(ticket, NULL) < 0); }
    advance(50U, true); assert(state.stop_phase == 4U);
    puts("PASS quick stop: configured normal/quick rates, upgrade without deadline reset, no downgrade, source dispatch, reservation barrier, stale replay");
}

static void test_arm3_binding_and_lock(void)
{
    uint8_t d[8]={0U}; can_protocol_record_t q,r; uint32_t ticket;
    reset();
    ticket=app_control_arbiter_submit(APP_CONTROL_OWNER_HMI,APP_SERVO_CONTROL_AXIS);
    assert(ticket); assert(app_control_arbiter_arm_begin());
    app_control_arbiter_service(); assert(app_control_arbiter_poll(ticket,NULL)<0);
    assert(!app_control_arbiter_submit(APP_CONTROL_OWNER_REMOTE,APP_SERVO_CONTROL_AXIS));
    assert(!app_control_arbiter_parameter_begin());
    app_control_arbiter_arm_lock(0U); assert(app_control_arbiter_parameter_begin());
    assert(!app_control_arbiter_arm_begin()); app_control_arbiter_parameter_end();
    ticket=app_control_arbiter_submit(APP_CONTROL_OWNER_REMOTE,APP_SERVO_CONTROL_AXIS);assert(ticket);
    app_control_arbiter_arm_lock(1U);app_control_arbiter_service();assert(app_control_arbiter_poll(ticket,NULL)<0);
    app_control_arbiter_arm_lock(0U);app_control_arbiter_service();assert(app_control_arbiter_poll(ticket,NULL)<0);
    reset(); acquire(); drive(400,4000U);
    can_protocol_write_u16(d,1U);can_protocol_write_u16(d+2U,42U);
    assert(app_can_control_stop(d,tick)); advance(5000U,true);
    memset(&q,0,sizeof(q));q.type=0x5EU;q.length=48U;q.payload[8]=10U;
    can_protocol_write_u32(q.payload,0x334D5241UL);can_protocol_write_u32(q.payload+4U,1U);
    can_protocol_write_u32(q.payload+24U,1U);can_protocol_write_u32(q.payload+28U,can_protocol_crc32(d,8U));
    assert(app_can_control_stop_binding(&q,&r,tick));
    assert(r.length==112U && r.payload[9]==0U);
    assert(can_protocol_read_u16(r.payload+40U)==1U && can_protocol_read_u32(r.payload+32U));
    assert(can_protocol_read_u32(r.payload+56U)==state.stop_generation);
    assert(r.payload[43]==2U && can_protocol_read_u16(r.payload+76U)==1U);
    assert((can_protocol_read_u32(r.payload+96U)&0xC79U)==0xC79U);
    q.payload[28]^=1U;assert(app_can_control_stop_binding(&q,&r,tick)&&r.payload[9]==8U);
    /* A valid idle stop has a real static-check producer, even without an arm. */
    reset();app_can_control_arm3_revision(true);
    memset(d,0,sizeof(d));can_protocol_write_u16(d,65535U);can_protocol_write_u16(d+2U,42U);
    assert(app_can_control_stop(d,tick));advance(100U,true);
    memset(&q,0,sizeof(q));q.type=0x5EU;q.length=48U;q.payload[8]=10U;
    can_protocol_write_u32(q.payload,0x334D5241UL);can_protocol_write_u32(q.payload+4U,2U);
    can_protocol_write_u32(q.payload+24U,65535U);can_protocol_write_u32(q.payload+28U,can_protocol_crc32(d,8U));
    assert(app_can_control_stop_binding(&q,&r,tick));
    assert(r.length==112U && r.payload[42]==6U && r.payload[43]==2U);
    can_protocol_write_u16(d,1U);assert(app_can_control_stop(d,tick));advance(100U,true);
    can_protocol_write_u16(d,65535U);assert(!app_can_control_stop(d,tick));
    puts("PASS ARM3 atomic arm/parameter exclusion, idle binding and non-reusable stop sequence");
}

#endif
#if APP_LINK_DIAG_ACTIVE && APP_CAN_SERVO_DIAG_ENABLED && APP_CAN_CONTROL_ENABLED
static void test_link_diagnostic_evidence(void)
{
    uint8_t data[8];uint32_t words[24],sampled,first_ms;
    app_can_control_status_t before,after;
    reset();acquire();diag_capture_reset();
    can_protocol_write_u16(data,++seq);can_protocol_write_u16(data+2U,100U);
    can_protocol_write_u16(data+4U,(uint16_t)(state.motion_token+1U));can_protocol_write_u16(data+6U,300U);
    assert(!app_can_control_speed(data,tick));
    assert(diag_capture_count[0x101U]==1U && diag_capture_first_kind[APP_LINK_CAN_SERVO]==0x101U);
    assert(diag_capture_first[APP_LINK_CAN_SERVO][4]==9U); /* Exact token predicate. */
    first_ms=diag_capture_first[APP_LINK_CAN_SERVO][1];
    can_protocol_write_u16(data+4U,state.motion_token);
    assert(app_can_control_speed(data,tick-7U));advance(1U,true);
    assert(diag_capture_count[0x103U]==1U); /* Admission and actual consumption are distinct. */
    assert(!app_can_control_speed(data,tick)); /* Duplicate cannot renew its receive time. */
    assert(diag_capture_first[APP_LINK_CAN_SERVO][1]==first_ms && diag_capture_first[APP_LINK_CAN_SERVO][4]==9U);
    assert(app_can_control_get_status(&before));
    assert(app_can_control_diag_words(words,0U,&sampled)==16U && sampled==before.sample_ms);
    assert(words[1]==before.generation && words[3]==before.speed_sequence && words[4]==before.speed_age_ms);
    assert(app_can_control_diag_words(words,1U,&sampled)==16U);
    assert(app_can_control_get_status(&after) && !memcmp(&before,&after,sizeof(before)));
    puts("PASS CAN diagnostic: exact reject, receive/consume, frozen first evidence, read-only projection");
}
static void test_stop_diagnostic_dedup(void)
{
    uint32_t words[24],sampled;unsigned before;app_control_stop_t event;
    reset();acquire();drive(800,8000U);diag_capture_reset();
    assert(request(5U,0U)==0U);advance(1U,true);
    assert(diag_capture_count[0x105U]==1U && diag_capture_count[0x10CU]==1U);
    app_control_arbiter_get_stop(&event);
    assert(event.requester==APP_CONTROL_OWNER_CAN && event.deceleration_mm_s2==500U);
    before=diag_capture_count[0x105U];advance(100U,true);
    assert(diag_capture_count[0x105U]==before && diag_capture_count[0x10CU]==1U);
    assert(app_can_control_diag_words(words,1U,&sampled)==16U && words[15]>0U);
    assert(app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_HMI,2U,600U,77U,0x100U));
    advance(1U,true);assert(diag_capture_count[0x105U]==before+1U);
    app_control_arbiter_get_stop(&event);
    assert(event.event==state.stop_generation && event.deceleration_mm_s2==600U);
    before=diag_capture_count[0x105U];advance(80U,true);
    assert(diag_capture_count[0x105U]==before);
    puts("PASS STOP first/real upgrade only, wire identity correlated once, repeated services counted without flood");
}
#endif
int main(void)
{
#if APP_LINK_DIAG_ACTIVE && APP_CAN_SERVO_DIAG_ENABLED && APP_CAN_CONTROL_ENABLED
    test_link_diagnostic_evidence();
    test_stop_diagnostic_dedup();
#endif
    assert(app_can_control_limit() > 1000U && app_can_control_config_valid());
#ifndef TEST_CAN_MECHANICS_VARIANT
    assert(app_can_control_limit() == APP_OPERATION_MAX_VELOCITY_MM_S);
#endif
#if APP_CAN_CONTROL_ENABLED
    reset();
    for(unsigned a=0U;a<2U;++a) {feedback_axis[a].enabled=0U;feedback_axis[a].enable_feedback=0U;feedback_axis[a].parameter_verified=0U;}
    assert(request(4U,1U)==0U); advance(20U,true);
    assert(state.authorized && state.motion_token); assert(!speed(100,300U));
    assert(speed(0,300U)); advance(8U,true);
    assert(state.first_zero_done && state.target_mm_s==0);
    puts("PASS CAN explicit regrant from disabled: existing prepare, fresh enable and first zero");
    test_fifo_and_nonowner_loss();
    test_stop(); test_timeout_fault(); test_arbitration_and_sequences(); test_reverse_global_tick();
    test_extended_safety();
    test_global_other_owners();
    test_mechanical_and_partial_failure();
    test_scheduler_and_zero_confirmation();
    test_communication_degraded_output();
    test_quick_stop();
    test_arm3_binding_and_lock();
    puts("PASS CAN control production: grant/first zero/async batches/stop hold/lease/reverse/arbitration/safety/wrap");
#else
    reset(); advance(1U, false); assert(request(4U,1U) == 1U); assert(!speed(100,300U));
    assert(calls[0] + calls[1] == 0U); puts("PASS CAN macro0: no motion writes");
#endif
    return 0;
}

bool aimtor_recovery_blocks_motion(void) { return false; }

servo_result_t aimtor_remote_prepare_mode(servo_axis_t axis)
{ feedback_axis[axis].parameter_verified=1U; feedback_axis[axis].control_mode=SERVO_CONTROL_RS485_VELOCITY; return SERVO_OK; }
servo_result_t aimtor_remote_prepare_enable(servo_axis_t axis)
{ feedback_axis[axis].enabled=feedback_axis[axis].enable_feedback=1U; return SERVO_OK; }
