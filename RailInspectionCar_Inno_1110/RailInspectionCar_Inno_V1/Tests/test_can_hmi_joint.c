#define HMI_REMOTE_JOINT_MAIN repeated_k5_regression_main
#include "test_hmi_remote_joint.c"
#include "app_can_status.h"
#include "can_protocol.h"

static uint8_t can_enabled;
static uint32_t can_writes;
static app_can_status_input_t telemetry;
static can_protocol_record_t can_record;

void aimtor_set_can_control(bool active, uint8_t mask)
{
    if (active) assert(joint_hold.state == SERVO_K1_HOLD_INACTIVE);
    can_enabled = active && mask;
}
void aimtor_can_revoke(uint32_t generation) { (void)generation; can_enabled = 0U; }
servo_result_t aimtor_can_velocity(servo_axis_t axis, int16_t rpm,
    uint32_t generation, uint32_t batch, bool stop)
{
    (void)generation; (void)batch;
    assert(can_enabled || stop);
    ++can_writes;
    return stop ? aimtor_set_stop_velocity_rpm(axis, rpm) : aimtor_set_velocity_rpm(axis, rpm);
}
servo_result_t app_cli_servo_session_off(void) { g_cli_active = 0U; return SERVO_OK; }
bool app_cli_servo_session_get_status(cli_servo_session_status_t *status)
{ memset(status, 0, sizeof(*status)); return true; }

static void can_joint_init(void)
{
    can_identity_t identity;
    memset(&identity, 0, sizeof(identity));
    identity.result = CAN_IDENTITY_OK;
    identity.motion_first = 100U; identity.motion_last = 131U;
    can_enabled = 0U; can_writes = 0U;
    memset(&telemetry, 0, sizeof(telemetry));
    app_can_control_init(&identity);
    app_can_control_link(42U, 1U, 7U, HAL_GetTick(), true, true);
}

static void sample_telemetry(void)
{
    telemetry.source_valid = aimtor_get_snapshot_pair(&telemetry.axis[0], &telemetry.axis[1]);
    assert(app_remote_servo_get_status(&telemetry.remote));
    assert(app_can_control_get_status(&telemetry.control));
    app_hmi_control_get_status(&telemetry.hmi);
    app_control_arbiter_get_stop(&telemetry.stop);
    telemetry.arbiter_owner = app_control_arbiter_owner();
    telemetry.arbiter_generation = app_control_arbiter_generation();
    telemetry.arbiter_axes = app_control_arbiter_axis_mask();
    telemetry.lock_reason = app_control_arbiter_lock_reason();
    telemetry.selected = APP_SERVO_CONTROL_AXIS;
    telemetry.gates = 31U; telemetry.can_link_ready = true;
    telemetry.now_ms = telemetry.snapshot_ms = telemetry.sample_tick = HAL_GetTick();
    telemetry.hmi_remaining_ms = app_hmi_control_remaining_ms();
    app_can_status_describe(&telemetry);
}

static void production_step(uint8_t presses)
{
    advance_ms(10U);
    for (unsigned axis = 0U; axis < 2U; ++axis)
        g_snapshot[axis].enable_feedback_tick = g_tick;
    app_can_control_link(42U, 1U, 7U, HAL_GetTick(), true, true);
    app_remote_servo_update(&remote_input);
    app_can_control_pre_service();
    app_control_arbiter_service();
    app_can_control_pre_service();
    remote_input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] += presses;
    app_remote_servo_update(&remote_input);
    app_remote_servo_service();
    app_hmi_control_service();
    app_can_control_service();
    sample_telemetry();
}

static void request_can(uint8_t sequence)
{
    uint8_t data[8] = {0};
    data[0] = sequence; data[1] = 4U;
    can_protocol_write_u16(data + 2U, 1U);
    can_protocol_write_u16(data + 6U, 42U);
    (void)app_can_control_submit(data, HAL_GetTick(), 1U);
}

static void test_joint_recovery(int16_t rpm, uint8_t presses)
{
    uint16_t mailbox[24];
    unsigned index;
    joint_setup(rpm); can_joint_init(); sample_telemetry();
    assert(telemetry.owner == 3U);
    request_can(1U); production_step(0U);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_HMI && !can_enabled);
    production_step(1U);
    assert(telemetry.life == 4U && telemetry.owner == 3U);
    for (index = 0U; index < 500U && app_hmi_control_session(); ++index)
        production_step(presses);
    assert(!app_hmi_control_session() && !g_emergency_calls && !can_writes);
    sample_telemetry();
    assert(app_can_status_record(&telemetry, 0x13U, &can_record));
    assert(can_record.payload[5] == 4U && telemetry.owner == 0U);
    for (index = 0U; index < 20U; ++index) production_step(presses);
    assert(!app_remote_servo_k1_latched());
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 20U, 0, 0U);
    assert(app_hmi_control_submit(mailbox));
    for (index = 0U; index < 20U && !app_hmi_control_session(); ++index) production_step(0U);
    assert(app_hmi_control_session() && telemetry.owner == 3U && telemetry.hmi.target == 0);
    assert(app_can_status_record(&telemetry, 0x11U, &can_record));
    assert(can_record.payload[34] == 0U);
    make_mailbox(mailbox, TEST_CMD_RELEASE, app_hmi_control_session(), 21U, 0, 0U);
    assert(app_hmi_control_submit(mailbox));
    for (index = 0U; index < 500U && app_hmi_control_session(); ++index) production_step(0U);
    assert(!app_hmi_control_session());
    request_can(2U);
    for (index = 0U; index < 10U && !can_enabled; ++index) production_step(0U);
    assert(can_enabled && telemetry.owner == 4U);
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 22U, 0, 0U);
    assert(app_hmi_control_submit(mailbox)); production_step(0U);
    assert(!app_hmi_control_session() && app_control_arbiter_owner() == APP_CONTROL_OWNER_CAN);
    assert(app_can_status_record(&telemetry, 0x11U, &can_record));
    assert(can_protocol_read_u16(can_record.payload + 4U) != 0U);
}

static void test_can_global_quick(void)
{
    uint8_t data[8] = {1U, 0U, 42U, 0U, 1U, 0U, 0U, 0U};
    unsigned index;
    joint_setup(200); can_joint_init(); sample_telemetry();
    assert(app_can_control_stop(data, HAL_GetTick()));
    for (index = 0U; index < 800U && app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE; ++index)
        production_step(0U);
    for (index = 0U; index < 10U; ++index) production_step(0U);
    assert(!app_hmi_control_session() && !g_emergency_calls && !can_writes);
    assert(telemetry.control.stop_phase == 4U && telemetry.control.stop_external);
    assert(app_can_status_record(&telemetry, 0x11U, &can_record));
    assert(can_protocol_read_u16(can_record.payload + 32U) == 1U && can_record.payload[34] == 4U);
    assert(app_can_status_record(&telemetry, 0x13U, &can_record));
    assert(can_record.payload[5] == 4U && can_protocol_read_u32(can_record.payload + 16U) ==
        (uint32_t)(APP_CAN_QUICK_STOP_DECEL_MPS2 * 1000.0f + 0.5f));
    request_can(9U);
    for (index = 0U; index < 10U && !can_enabled; ++index) production_step(0U);
    assert(can_enabled);
    puts("PASS CAN global quick -> real HMI stop/release/current 0x11/0x13 -> CAN regrant");
}
static void test_can_global_quick_remote(void)
{
    uint8_t data[8] = {1U, 0U, 42U, 0U, 1U, 0U, 0U, 0U};
    remote_servo_status_t remote;
    unsigned index;
    joint_setup(0); joint_stop(0U); can_joint_init();
    remote_input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    for (index = 0U; index < 35U; ++index) production_step(0U);
    assert(app_remote_servo_get_status(&remote));
    assert(remote.authorized && app_control_arbiter_owner() == APP_CONTROL_OWNER_REMOTE);
    joint_hold_progress = 1U;
    remote_input.left_vertical = 1400U;
    for (index = 0U; index < 30U; ++index) production_step(0U);
    assert(app_can_control_stop(data, HAL_GetTick()));
    for (index = 0U; index < 800U && app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE; ++index)
        production_step(0U);
    for (index = 0U; index < 10U; ++index) production_step(0U);
    assert(app_remote_servo_get_status(&remote));
    printf("REMOTE quick phase=%u result=%u owner=%u remote=%u stop=%u reason=%u emergency=%lu elapsed=%lu timeout=%lu\n",telemetry.control.stop_phase,telemetry.control.stop_result,app_control_arbiter_owner(),remote.state,remote.controlled_stop_state,remote.controlled_stop_reason,(unsigned long)g_emergency_calls,(unsigned long)telemetry.control.stop_elapsed_ms,(unsigned long)telemetry.control.stop_timeout_ms);
    assert(telemetry.control.stop_phase == 4U && !g_emergency_calls);
    assert(remote.controlled_stop_reason == REMOTE_STOP_CAN);
    assert(remote.state == REMOTE_SERVO_DISABLED && !hold_requests);
    assert(telemetry.control.cross_stop_result == CAN_CROSS_STOP_SUCCEEDED_RELEASED);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    assert(app_can_status_record(&telemetry, 0x11U, &can_record));
    assert(can_protocol_read_u16(can_record.payload + 32U) == 1U && can_record.payload[34] == 4U);
    assert(app_can_status_record(&telemetry, 0x13U, &can_record));
    assert(can_record.payload[5] == 4U && can_protocol_read_u32(can_record.payload + 16U) ==
        (uint32_t)(APP_CAN_QUICK_STOP_DECEL_MPS2 * 1000.0f + 0.5f));
    puts("PASS CAN global quick -> real REMOTE stop/release/configured telemetry");
}

static void test_pb10_fresh_hmi_session(void)
{
    uint16_t mailbox[24];
    uint8_t data[8] = {1U, 0U, 42U, 0U, 1U, 0U, 0U, 0U};
    uint32_t old_session, new_session;
    unsigned index;
    joint_setup(200); can_joint_init();
    old_session = app_hmi_control_session();
    assert(app_can_control_stop(data, HAL_GetTick()));
    for (index = 0U; index < 800U; ++index)
    {
        production_step(0U);
        if (telemetry.control.cross_stop_result == CAN_CROSS_STOP_SUCCEEDED_RELEASED) break;
    }
    assert(index < 800U && app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_NONE);
    assert(!telemetry.control.motion_token && !telemetry.control.target_mm_s);
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 3U, 0, 0U);
    assert(app_hmi_control_submit(mailbox));
    for (index = 0U; index < 30U && !app_hmi_control_session(); ++index) production_step(0U);
    new_session = app_hmi_control_session();
    assert(new_session && new_session != old_session && telemetry.hmi.target == 0);
    make_mailbox(mailbox, TEST_CMD_SPEED, old_session, 4U, 200, 500U);
    assert(app_hmi_control_submit(mailbox)); production_step(0U);
    assert(g_result.state == TEST_RESULT_REJECTED && telemetry.hmi.target == 0);
    make_mailbox(mailbox, TEST_CMD_SPEED, new_session, 2U, 200, 500U);
    assert(app_hmi_control_submit(mailbox)); production_step(0U);
    assert(g_result.state == TEST_RESULT_REJECTED && telemetry.hmi.target == 0);
    puts("PASS PB10 -> direct HMI fresh grant, old session/sequence cannot restore target");
}

static void test_pb10_safety_stays_locked(void)
{
    uint8_t data[8] = {1U, 0U, 42U, 0U, 1U, 0U, 0U, 0U};
    unsigned index;
    joint_setup(200); can_joint_init();
    assert(app_can_control_stop(data, HAL_GetTick())); production_step(0U);
    joint_recovery.state = SERVO_RECOVERY_WAIT_HEALTH;
    app_can_control_pre_service();
    app_can_control_service(); /* Read the published snapshot, not private state. */
    assert(app_can_control_get_status(&telemetry.control));
    assert(telemetry.control.cross_stop_result == CAN_CROSS_STOP_FAILED_SAFETY);
    assert(telemetry.control.stop_phase == 5U);
    assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_SAFETY);
    for (index = 0U; index < 10U; ++index) production_step(0U);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_SAFETY);
    memset(&joint_recovery, 0, sizeof(joint_recovery));
    puts("PASS PB10 interrupted by real recovery finalizes FAILED_SAFETY without unlocking");
}

#ifndef CAN_HMI_JOINT_MAIN
#define CAN_HMI_JOINT_MAIN main
#endif
int CAN_HMI_JOINT_MAIN(void)
{
    (void)repeated_k5_regression_main;
    for (unsigned index = 0U; index < 3U; ++index)
    {
        uint8_t presses = index == 0U ? 1U : index == 1U ? 5U : 20U;
        test_joint_recovery(100, presses);
        test_joint_recovery(-100, presses);
    }
    puts("PASS real HMI/remote/CAN services: K5 repeat, telemetry, one-acquire recovery and mutual exclusion");
    test_can_global_quick();
    test_can_global_quick_remote();
    test_pb10_fresh_hmi_session();
    test_pb10_safety_stays_locked();
    return 0;
}
