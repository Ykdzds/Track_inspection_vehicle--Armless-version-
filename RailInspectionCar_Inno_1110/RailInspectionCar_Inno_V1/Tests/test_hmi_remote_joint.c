#define HMI_REMOTE_JOINT 1
#define main hmi_regression_main
#define aimtor_k1_hold_release unused_hmi_hold_release
#define aimtor_recovery_blocks_motion unused_hmi_recovery_blocks_motion
#include "test_hmi_control.c"
#undef main
#undef aimtor_k1_hold_release
#undef aimtor_recovery_blocks_motion
#include "servo_debug_control.h"

static servo_recovery_status_t joint_recovery;
bool aimtor_recovery_blocks_motion(void) {return joint_recovery.state != SERVO_RECOVERY_NONE && joint_recovery.state != SERVO_RECOVERY_DONE;}
void aimtor_get_recovery_status(servo_recovery_status_t *status) { *status = joint_recovery; }
servo_result_t aimtor_request_recovery(uint8_t source)
{
    if (joint_recovery.state != SERVO_RECOVERY_READY) return SERVO_NOT_READY;
    joint_recovery.source = source; joint_recovery.state = SERVO_RECOVERY_REQUESTED; return SERVO_OK;
}
static nrf_remote_snapshot_t remote_input;
static uint32_t hold_requests;
static servo_k1_hold_status_t joint_hold;
static uint8_t joint_hold_progress;
bool aimtor_k1_hold_get_status(servo_k1_hold_status_t *status)
{
    if (joint_hold_progress && joint_hold.state == SERVO_K1_HOLD_CONFIRMING)
    { joint_hold.state = SERVO_K1_HOLD_READY; joint_hold.valid = 1U;
      joint_hold.zero_mask = joint_hold.enabled_mask = joint_hold.axis_mask; }
    *status = joint_hold;
    return true;
}
servo_result_t aimtor_k1_hold_release(void)
{
    servo_k1_hold_status_t hold;
    if (!aimtor_k1_hold_get_status(&hold) || !hold.valid ||
        hold.zero_mask != hold.axis_mask) return SERVO_NOT_READY;
    memset(&joint_hold, 0, sizeof(joint_hold));
    return SERVO_OK;
}

uint8_t app_config_servo_axis_controllable(uint8_t axis) { return app_config_servo_axis_selected(axis); }
uint8_t app_config_servo_control_axis_count(void) { return APP_SERVO_CONTROL_AXIS == 3U ? 2U : 1U; }
void app_health_request_watchdog_test(void) { g_watchdog_requested = 1U; }
void app_cli_servo_session_abort(cli_session_reason_t reason) { (void)reason; g_cli_active = 0U; }
void app_cli_servo_session_external_stop(void) { g_cli_active = 0U; }
bool aimtor_startup_hold_active(void) { return false; }
void aimtor_set_motion_authorized(bool authorized) { (void)authorized; }
void aimtor_set_remote_stop_active(bool active) { (void)active; }
void aimtor_set_remote_control_active(bool active) { g_remote_active = active; }
void aimtor_set_remote_control_mask(uint8_t mask) { g_remote_active = mask != 0U; }
servo_result_t aimtor_k1_hold_request(uint8_t mask) { hold_requests++; joint_hold.axis_mask = mask; joint_hold.state = SERVO_K1_HOLD_CONFIRMING; return SERVO_OK; }
void aimtor_emergency_stop_k1(void) { aimtor_emergency_stop(); }
void servo_debug_abort_from_fault(void) { }
bool servo_debug_get_status(servo_debug_status_t *status) { memset(status, 0, sizeof(*status)); return true; }
servo_result_t aimtor_clear_stop_latch(void) { return SERVO_OK; }
servo_result_t aimtor_startup_hold_request_rearm(void) { return SERVO_OK; }
motion_result_t motion_stop(motion_stop_mode_t mode) { (void)mode; return MOTION_OK; }

static void joint_step(uint8_t presses)
{
    advance_ms(10U);
    remote_input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] += presses;
    app_remote_servo_update(&remote_input);
    app_control_arbiter_service();
    app_remote_servo_service();
    app_hmi_control_service();
}
static void joint_setup(int16_t rpm)
{
    uint16_t mailbox[24U];
    uint32_t session;
    unsigned index;
    reset_fixture();
    memset(&remote_input, 0, sizeof(remote_input));
    memset(&joint_hold, 0, sizeof(joint_hold));
    joint_hold_progress = 0U;
    hold_requests = 0U;
    app_remote_servo_init();
    remote_input.link_up = 1U; remote_input.left_vertical = 1000U;
    g_feedback_progress = 1U;
    joint_step(0U);
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    assert(app_hmi_control_submit(mailbox));
    for (index = 0U; index < 20U && !app_hmi_control_session(); ++index) joint_step(0U);
    session = app_hmi_control_session(); assert(session);
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, rpm, 500U);
    assert(app_hmi_control_submit(mailbox));
    for (index = 0U; index < 20U; ++index) joint_step(0U);
}
static void joint_stop(uint8_t repeat)
{
    unsigned index;
    app_control_stop_t initial, current;
    joint_step(1U);
    app_control_arbiter_get_stop(&initial);
    assert(initial.owner == APP_CONTROL_OWNER_HMI && initial.result == TEST_RESULT_RUNNING);
    assert(initial.deceleration_mm_s2 == APP_REMOTE_SERVO_K5_STOP_DECEL_MM_S2);
    for (index = 0U; index < 300U && app_hmi_control_session(); ++index)
    {
        joint_step(repeat);
        app_control_arbiter_get_stop(&current);
        assert(current.event == initial.event && current.started_ms == initial.started_ms);
        assert(current.timeout_ms == initial.timeout_ms);
    }
    app_control_arbiter_get_stop(&current);
    assert(current.result == TEST_RESULT_SUCCEEDED);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    assert(!app_hmi_control_session());
}
static void repeat_after_release(uint8_t busy, int16_t rpm, uint8_t presses)
{
    unsigned index;
    uint16_t mailbox[24U];
    uint32_t writes;
    app_control_stop_t before, after;
    joint_setup(rpm);
    joint_stop(1U);
    app_control_arbiter_get_stop(&before); writes = g_stop_calls + g_velocity_calls;
    if (busy) g_snapshot[0].busy = g_snapshot[1].busy = 1U;
    for (index = 0U; index < 20U; ++index)
    {
        if (index == 3U) g_snapshot[0].busy = g_snapshot[1].busy = 0U;
        joint_step(presses);
    }
    app_control_arbiter_get_stop(&after);
    assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_NONE);
    assert(!app_remote_servo_k1_latched());
    assert(!hold_requests && !g_emergency_calls);
    assert(g_stop_calls + g_velocity_calls == writes);
    assert(after.event == before.event && after.result == TEST_RESULT_SUCCEEDED);
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 3U, 0, 0U);
    assert(app_hmi_control_submit(mailbox));
    for (index = 0U; index < 20U && !app_hmi_control_session(); ++index) joint_step(0U);
    assert(app_hmi_control_session());
}
static void test_delayed_feedback(void)
{
    unsigned index;
    joint_setup(100); joint_stop(1U);
    g_snapshot[0].speed_valid = g_snapshot[1].speed_valid = 0U;
    joint_step(1U); assert(app_remote_servo_k5_verify_pending());
    for (index = 0U; index < 4U; ++index) joint_step(5U);
    g_snapshot[0].speed_valid = g_snapshot[1].speed_valid = 1U;
    joint_step(0U); assert(!app_remote_servo_k5_verify_pending());
    assert(!hold_requests && !g_emergency_calls);
}
static void test_wait_and_failure(unsigned scenario)
{
    unsigned index;
    uint16_t mailbox[24U];
    app_control_stop_t before, after;
    joint_setup(-100); joint_stop(1U);
    app_control_arbiter_get_stop(&before);
    g_snapshot[0].busy = g_snapshot[1].busy = 1U;
    joint_step(1U);
    assert(app_remote_servo_k5_verify_pending());
    if (scenario == 0U)
    {
        make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 3U, 0, 0U);
        assert(app_hmi_control_submit(mailbox)); joint_step(1U);
        assert(!app_hmi_control_session()); assert(g_result.error == TEST_ERR_OWNER_BUSY);
        g_snapshot[0].busy = g_snapshot[1].busy = 0U; joint_step(0U);
        assert(!app_remote_servo_k5_verify_pending());
        assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_NONE);
        return;
    }
    if (scenario == 1U)
    {
        for (index = 0U; index < APP_SERVO_STATUS_MAX_AGE_MS / 10U + 1U; ++index) joint_step(1U);
    }
    else
    {
        g_snapshot[0].busy = g_snapshot[1].busy = 0U;
        if (scenario == 2U) g_snapshot[0].actual_speed_rpm = g_snapshot[1].actual_speed_rpm = 10;
        if (scenario == 3U) g_snapshot[0].fault = g_snapshot[1].fault = 1U;
        if (scenario == 4U) g_snapshot[0].enable_feedback = g_snapshot[1].enable_feedback = 0U;
        if (scenario == 5U) app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
        if (scenario == 6U) g_snapshot[0].speed_valid = g_snapshot[1].speed_valid = 0U;
        joint_step(1U);
        if (scenario == 6U)
            for (index = 0U; index < APP_SERVO_STATUS_MAX_AGE_MS / 10U + 1U; ++index) joint_step(1U);
    }
    assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_SAFETY);
    assert(g_emergency_calls);
    app_control_arbiter_get_stop(&after); assert(after.event == before.event);
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 3U, 0, 0U);
    assert(app_hmi_control_submit(mailbox)); joint_step(0U);
    assert(!app_hmi_control_session());
}
static void test_fixed_deadline_at_recovery(void)
{
    joint_setup(100); joint_stop(1U);
    g_snapshot[0].busy = g_snapshot[1].busy = 1U; joint_step(1U);
    assert(app_remote_servo_k5_verify_pending());
    advance_ms(APP_SERVO_STATUS_MAX_AGE_MS);
    g_snapshot[0].busy = g_snapshot[1].busy = 0U;
    joint_step(0U);
    assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_SAFETY);
    assert(g_emergency_calls);
}
static void test_owner_change(app_control_owner_t owner)
{
    uint32_t ticket;
    app_control_stop_t stop;
    joint_setup(100); joint_stop(1U);
    g_snapshot[0].busy = g_snapshot[1].busy = 1U; joint_step(1U);
    assert(app_remote_servo_k5_verify_pending());
    ticket = app_control_arbiter_submit(owner, APP_SERVO_CONTROL_AXIS); assert(ticket);
    app_control_arbiter_service();
    g_snapshot[0].busy = g_snapshot[1].busy = 0U;
    if (owner == APP_CONTROL_OWNER_HMI)
    {
        app_remote_servo_update(&remote_input); app_remote_servo_service();
        app_control_arbiter_get_stop(&stop);
        assert(stop.owner == owner && stop.result == TEST_RESULT_RUNNING);
    }
    else if (owner == APP_CONTROL_OWNER_REMOTE)
    {
        app_remote_servo_update(&remote_input); app_remote_servo_service();
        app_control_arbiter_get_stop(&stop);
        assert(stop.owner == owner && stop.result == TEST_RESULT_RUNNING);
    }
    else if (owner == APP_CONTROL_OWNER_CLI)
    {
        g_cli_active = 1U;
        app_remote_servo_update(&remote_input); app_remote_servo_service();
        assert(!g_cli_active);
    }
    else if (owner == APP_CONTROL_OWNER_CAN)
    {
        app_remote_servo_update(&remote_input); app_remote_servo_service();
        assert(app_remote_servo_k1_latched());
    }
    assert(!app_remote_servo_k5_verify_pending());
}
static void test_radio_history(void)
{
    app_control_stop_t before, after;
    joint_setup(100); joint_stop(1U);
    app_control_arbiter_get_stop(&before);
    remote_input.recovery_generation++;
    remote_input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] = UINT32_MAX;
    joint_step(0U); assert(!app_remote_servo_k5_verify_pending());
    joint_step(1U); joint_step(5U);
    app_control_arbiter_get_stop(&after);
    assert(after.event == before.event && !g_emergency_calls && !hold_requests);
}
#ifndef HMI_REMOTE_JOINT_MAIN
#define HMI_REMOTE_JOINT_MAIN main
#endif
int HMI_REMOTE_JOINT_MAIN(void)
{
    (void)hmi_regression_main;
    setbuf(stdout, NULL);
    unsigned repeat, scenario;
    for (repeat = 0U; repeat < 3U; ++repeat)
    {
        uint8_t presses = repeat == 0U ? 1U : (repeat == 1U ? 5U : 20U);
        repeat_after_release(0U, 100, presses);
        repeat_after_release(1U, -100, presses);
    }
    for (scenario = 0U; scenario <= 6U; ++scenario) test_wait_and_failure(scenario);
    test_owner_change(APP_CONTROL_OWNER_HMI);
    test_owner_change(APP_CONTROL_OWNER_CAN);
    test_owner_change(APP_CONTROL_OWNER_REMOTE);
    test_owner_change(APP_CONTROL_OWNER_CLI);
    test_radio_history();
    test_delayed_feedback();
    test_fixed_deadline_at_recovery();
    puts("PASS joint repeated K5 across HMI release: no extra writes, hold or safety lock; one acquire restores zero");
    return 0;
}
