#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "app_control_arbiter.h"
#include "app_hmi_control.h"
#include "app_health.h"
#include "app_remote_servo.h"
#include "app_cli_servo_session.h"
#include "aimtor_servo.h"
#include "motion_control.h"

enum
{
    TEST_CMD_ACQUIRE = 1U,
    TEST_CMD_HEARTBEAT = 2U,
    TEST_CMD_RELEASE = 3U,
    TEST_CMD_STOP = 0x10U,
    TEST_CMD_SPEED = 0x21U
};

enum
{
    TEST_RESULT_NONE,
    TEST_RESULT_ACCEPTED,
    TEST_RESULT_RUNNING,
    TEST_RESULT_SUCCEEDED,
    TEST_RESULT_REJECTED,
    TEST_RESULT_FAILED,
    TEST_RESULT_EXPIRED
};

enum
{
    TEST_ERR_NONE,
    TEST_ERR_READ_ONLY,
    TEST_ERR_BOOT_MISMATCH,
    TEST_ERR_NO_SESSION,
    TEST_ERR_SESSION_EXPIRED,
    TEST_ERR_OWNER_BUSY,
    TEST_ERR_SAFETY_DENIED,
    TEST_ERR_FEEDBACK_STALE,
    TEST_ERR_OUT_OF_RANGE,
    TEST_ERR_BAD_MODE,
    TEST_ERR_SEQ_CONFLICT,
    TEST_ERR_OLD_SEQUENCE,
    TEST_ERR_QUEUE_FULL,
    TEST_ERR_EXEC_TIMEOUT,
    TEST_ERR_STOP_FAILED,
    TEST_ERR_UNSUPPORTED_CMD
};

static TickType_t g_tick;
static int32_t g_hal_offset;
static uint32_t g_boot_id = 0x12345678UL;
static uint8_t g_watchdog_requested;
static uint8_t g_k1_latched;
static uint8_t g_remote_active;
static uint8_t g_cli_active;
static uint8_t g_motion_ready = 1U;
static uint8_t g_feedback_fresh = 1U;
static uint8_t g_feedback_progress;
static uint8_t g_stop_progress = 1U;
static uint8_t g_stop_feedback_pending[SERVO_AXIS_COUNT];
static uint8_t g_hmi_active;
static uint8_t g_delayed_writes;
#ifdef NORMAL_TIMING_TEST
static uint32_t timing_delay, timing_due[2];
static int16_t timing_rpm[2];
#endif
static servo_snapshot_t g_snapshot[SERVO_AXIS_COUNT];
static uint32_t g_velocity_calls;
static uint32_t g_stop_calls;
static uint32_t g_emergency_calls;
static int16_t g_velocity_values[16U];
static uint8_t g_velocity_axes[16U];
static uint32_t g_result_calls;

typedef struct
{
    uint32_t session;
    uint32_t sequence;
    uint16_t command;
    uint16_t state;
    uint16_t error;
    uint32_t generation;
    uint16_t owner;
    uint16_t remaining_ms;
    uint16_t feedback;
} test_result_t;

static test_result_t g_result;

uint32_t HAL_GetTick(void)
{
    return (uint32_t)g_tick + (uint32_t)g_hal_offset;
}

TickType_t xTaskGetTickCount(void)
{
    return g_tick;
}

void app_hmi_snapshot_set_result(uint32_t session, uint32_t sequence,
                                 uint16_t command, uint16_t state,
                                 uint16_t error, uint32_t received_ms,
                                 uint32_t completed_ms, uint32_t generation,
                                 uint16_t owner, uint16_t remaining_ms,
                                 uint16_t feedback)
{
    (void)received_ms;
    (void)completed_ms;
    g_result.session = session;
    g_result.sequence = sequence;
    g_result.command = command;
    g_result.state = state;
    g_result.error = error;
    g_result.generation = generation;
    g_result.owner = owner;
    g_result.remaining_ms = remaining_ms;
    g_result.feedback = feedback;
    g_result_calls++;
}

uint32_t app_hmi_snapshot_boot_id(void)
{
    return g_boot_id;
}

bool app_health_watchdog_test_requested(void)
{
    return g_watchdog_requested != 0U;
}

uint8_t app_config_servo_control_axis_mask(void)
{
    return (uint8_t)APP_SERVO_CONTROL_AXIS;
}

uint8_t app_config_servo_axis_selected(uint8_t axis)
{
    return ((APP_SERVO_CONTROL_AXIS & (1U << axis)) != 0U) ? 1U : 0U;
}

uint8_t app_config_servo_readonly_test(void)
{
    return APP_SERVO_READONLY_TEST;
}

uint8_t app_config_motion_hardware_ready(void)
{
    return g_motion_ready;
}

uint8_t app_config_servo_bus_ready(void)
{
    return g_motion_ready;
}

#ifndef HMI_REMOTE_JOINT
uint8_t app_remote_servo_k5_verify_pending(void) { return 0U; }
bool app_remote_servo_k1_latched(void)
{
    return g_k1_latched != 0U;
}
#endif

bool aimtor_remote_control_active(void)
{
    return g_remote_active != 0U;
}

bool app_cli_servo_session_active(void)
{
    return g_cli_active != 0U;
}

bool aimtor_watchdog_recovery_ready(void)
{
    return g_motion_ready != 0U;
}

bool aimtor_startup_stop_ready(void)
{
    return g_motion_ready != 0U;
}

bool aimtor_boot_zero_ready(void)
{
    return g_motion_ready != 0U;
}

bool aimtor_startup_hold_ready(void)
{
    return g_motion_ready != 0U;
}

servo_result_t aimtor_remote_prepare_mode(servo_axis_t axis)
{ g_snapshot[axis].control_mode = SERVO_CONTROL_RS485_VELOCITY; g_snapshot[axis].parameter_verified = 1U; return SERVO_OK; }
servo_result_t aimtor_remote_prepare_enable(servo_axis_t axis)
{ g_snapshot[axis].enabled = g_snapshot[axis].enable_feedback = 1U; return SERVO_OK; }
#ifndef HMI_REMOTE_JOINT
bool aimtor_k1_hold_get_status(servo_k1_hold_status_t *status)
{ memset(status, 0, sizeof(*status)); return true; }
#endif
servo_result_t aimtor_k1_hold_release(void) { return SERVO_OK; }
bool aimtor_hmi_control_active(void)
{
    return g_hmi_active != 0U;
}

void aimtor_set_hmi_control(bool authorized, uint8_t mask)
{
    g_hmi_active = authorized && ((mask & APP_SERVO_CONTROL_AXIS) != 0U);
}

static void refresh_feedback(uint8_t axis)
{
#ifdef NORMAL_TIMING_TEST
    if (g_snapshot[axis].busy && (int32_t)(g_tick - timing_due[axis]) >= 0)
    {
        g_snapshot[axis].busy = 0U;
        g_snapshot[axis].speed_write_completed = g_snapshot[axis].speed_write_sequence;
        g_snapshot[axis].actual_speed_rpm = timing_rpm[axis];
    }
#endif
    if (g_feedback_progress != 0U)
    {
        g_snapshot[axis].speed_sequence++;
        if (g_snapshot[axis].speed_sequence == 0U)
            g_snapshot[axis].speed_sequence = 1U;
    }
}

bool aimtor_get_snapshot_pair(servo_snapshot_t *left, servo_snapshot_t *right)
{
    if (left == NULL || right == NULL) return false;
    refresh_feedback(SERVO_AXIS_LEFT); refresh_feedback(SERVO_AXIS_RIGHT);
    *left = g_snapshot[SERVO_AXIS_LEFT];
    *right = g_snapshot[SERVO_AXIS_RIGHT];
    return g_feedback_fresh != 0U;
}

bool aimtor_get_snapshot(servo_axis_t axis, servo_snapshot_t *snapshot)
{
    if (snapshot == NULL || axis >= SERVO_AXIS_COUNT) return false;
    refresh_feedback((uint8_t)axis);
    if (g_feedback_progress != 0U && g_stop_feedback_pending[axis] != 0U)
    {
        g_snapshot[axis].actual_speed_rpm = 0;
        g_snapshot[axis].speed_valid = 1U;
        g_snapshot[axis].fault_valid = 1U;
        g_snapshot[axis].busy = 0U;
        g_snapshot[axis].speed_sequence++;
        g_stop_feedback_pending[axis] = 0U;
    }
    *snapshot = g_snapshot[axis];
    return g_feedback_fresh != 0U;
}

servo_result_t aimtor_set_velocity_rpm(servo_axis_t axis, int16_t rpm)
{
    if (axis >= SERVO_AXIS_COUNT) return SERVO_INVALID_ARGUMENT;
#ifdef NORMAL_TIMING_TEST
    if (g_snapshot[0].busy || g_snapshot[1].busy) return SERVO_BUSY;
#endif
    if (g_velocity_calls < (sizeof(g_velocity_values) / sizeof(g_velocity_values[0])))
    {
        g_velocity_axes[g_velocity_calls] = (uint8_t)axis;
        g_velocity_values[g_velocity_calls] = rpm;
    }
    g_velocity_calls++;
    if (!g_motion_ready || !g_feedback_fresh) return SERVO_NOT_READY;
    g_snapshot[axis].speed_write_sequence++;
    if (!g_delayed_writes) g_snapshot[axis].speed_write_completed = g_snapshot[axis].speed_write_sequence;
    g_snapshot[axis].speed_write_result = SERVO_OK;
#ifdef NORMAL_TIMING_TEST
    if (!timing_delay) g_snapshot[axis].actual_speed_rpm = rpm;
#else
    g_snapshot[axis].actual_speed_rpm = rpm;
#endif
    g_snapshot[axis].speed_valid = 1U;
    g_snapshot[axis].fault_valid = 1U;
#ifdef NORMAL_TIMING_TEST
    if (timing_delay) {
        g_snapshot[axis].speed_write_completed = g_snapshot[axis].speed_write_sequence - 1U;
        g_snapshot[axis].busy = 1U; timing_due[axis] = g_tick + timing_delay;
        timing_rpm[axis] = rpm;
    }
#endif
    return SERVO_OK;
}

servo_result_t aimtor_set_stop_velocity_rpm(servo_axis_t axis, int16_t rpm)
{
    if (axis >= SERVO_AXIS_COUNT) return SERVO_INVALID_ARGUMENT;
    g_stop_calls++;
    if (!g_stop_progress) return SERVO_BUSY;
    g_snapshot[axis].speed_write_sequence++;
    g_snapshot[axis].speed_write_completed = g_snapshot[axis].speed_write_sequence;
    g_snapshot[axis].speed_write_result = SERVO_OK;
    g_snapshot[axis].actual_speed_rpm = rpm;
    if (!rpm) g_stop_feedback_pending[axis] = 1U;
    return SERVO_OK;
}

bool aimtor_safety_stop_active(void) { return false; }
void aimtor_request_controlled_safety_stop(uint8_t offline) { (void)offline; }
void aimtor_emergency_stop(void)
{
    g_emergency_calls++;
}

static void reset_snapshot(uint8_t axis)
{
    memset(&g_snapshot[axis], 0, sizeof(g_snapshot[axis]));
    g_snapshot[axis].online = 1U;
    g_snapshot[axis].parameter_verified = 1U;
    g_snapshot[axis].enabled = 1U;
    g_snapshot[axis].enable_feedback_valid = 1U;
    g_snapshot[axis].enable_feedback = 1U;
    g_snapshot[axis].control_mode = SERVO_CONTROL_RS485_VELOCITY;
    g_snapshot[axis].speed_valid = 1U;
    g_snapshot[axis].fault_valid = 1U;
    g_snapshot[axis].speed_age_ms = 0U;
    g_snapshot[axis].fault_age_ms = 0U;
}

static void reset_fixture(void)
{
    uint8_t axis;
    g_tick = 0U;
    g_watchdog_requested = 0U;
    g_k1_latched = 0U;
    g_remote_active = 0U;
    g_cli_active = 0U;
    g_motion_ready = 1U;
    g_feedback_fresh = 1U;
    g_feedback_progress = 0U;
    g_stop_progress = 1U;
    memset(g_stop_feedback_pending, 0, sizeof(g_stop_feedback_pending));
    g_hmi_active = 0U; g_delayed_writes = 0U;
    g_velocity_calls = 0U;
    g_stop_calls = 0U;
    g_emergency_calls = 0U;
    g_result_calls = 0U;
    memset(&g_result, 0, sizeof(g_result));
    memset(g_velocity_values, 0, sizeof(g_velocity_values));
    memset(g_velocity_axes, 0, sizeof(g_velocity_axes));
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) reset_snapshot(axis);
    app_hmi_control_init();
    app_control_arbiter_init();
}

static void advance_ms(uint32_t ms)
{
    g_tick += ms;
}

static void put_u32(uint16_t *mailbox, uint8_t index, uint32_t value)
{
    mailbox[index] = (uint16_t)(value >> 16U);
    mailbox[index + 1U] = (uint16_t)value;
}

static void make_mailbox(uint16_t *mailbox, uint16_t command,
                         uint32_t session, uint32_t sequence,
                         int32_t speed_mmps, uint16_t hold_ms)
{
    memset(mailbox, 0, 24U * sizeof(*mailbox));
    mailbox[0U] = 0x484DU;
    mailbox[1U] = 1U;
    put_u32(mailbox, 2U, g_boot_id);
    put_u32(mailbox, 4U, session);
    put_u32(mailbox, 6U, sequence);
    mailbox[8U] = command;
    mailbox[9U] = 0U;
    put_u32(mailbox, 10U, (uint32_t)speed_mmps);
    mailbox[17U] = hold_ms;
    mailbox[22U] = APP_HMI_DICTIONARY_REVISION;
    mailbox[23U] = 0xA55AU;
}

static void submit_service(const uint16_t *mailbox)
{
    assert(app_hmi_control_submit(mailbox) == 1U);
    app_control_arbiter_service();
    app_hmi_control_service();
}

static void assert_rejected(uint16_t error)
{
    assert(g_result.state == TEST_RESULT_REJECTED);
    assert(g_result.error == error);
}

static void test_acquire_speed_and_heartbeat(void)
{
    uint16_t mailbox[24U];
    uint32_t session;

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    assert(g_result.state == TEST_RESULT_SUCCEEDED);
    assert(g_result.command == TEST_CMD_ACQUIRE);
    assert(g_result.owner == APP_CONTROL_OWNER_HMI);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_HMI);
    assert(g_hmi_active != 0U);
    session = app_hmi_control_session();
    assert(session != 0U);

    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 100, 500U);
    submit_service(mailbox);
    assert(g_result.state == TEST_RESULT_ACCEPTED);
    for (unsigned i = 0U; i < 100U &&
         g_velocity_calls < (APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_BOTH ? 2U : 1U); i++)
    {
        advance_ms(10U);
        app_hmi_control_service();
    }
    assert(g_velocity_calls >= 1U);
#if APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_RIGHT
    assert(g_velocity_axes[0] == SERVO_AXIS_RIGHT);
#else
    assert(g_velocity_axes[0] == SERVO_AXIS_LEFT);
#endif
    assert(g_velocity_values[g_velocity_calls - (APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_BOTH ? 2U : 1U)] <= 10);
#if APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_BOTH
    assert(g_velocity_calls >= 2U);
    assert(g_velocity_axes[g_velocity_calls - 1U] == SERVO_AXIS_RIGHT);
    assert(g_velocity_values[g_velocity_calls - 1U] <= 10);
#endif

    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 100, 500U);
    submit_service(mailbox);
    assert(g_velocity_calls == (APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_BOTH ? 2U : 1U));
    assert(g_result.state == TEST_RESULT_ACCEPTED || g_result.state == TEST_RESULT_SUCCEEDED);

    make_mailbox(mailbox, TEST_CMD_HEARTBEAT, session, 3U, 0, 0U);
    submit_service(mailbox);
    assert(g_result.state == TEST_RESULT_SUCCEEDED);
    assert(g_result.command == TEST_CMD_HEARTBEAT);
}

static void test_validation_and_conflicts(void)
{
    uint16_t mailbox[24U];

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    mailbox[2U] ^= 1U;
    submit_service(mailbox);
    assert_rejected(TEST_ERR_BOOT_MISMATCH);

    reset_fixture();
    assert(app_control_arbiter_submit(APP_CONTROL_OWNER_REMOTE, (uint8_t)APP_SERVO_CONTROL_AXIS));
    app_control_arbiter_service();
    assert(app_control_arbiter_request(APP_CONTROL_OWNER_REMOTE,
                                        (uint8_t)APP_SERVO_CONTROL_AXIS, NULL) == 1U);
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_OWNER_BUSY);

    reset_fixture();
    g_cli_active = 1U;
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_OWNER_BUSY);

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    make_mailbox(mailbox, TEST_CMD_HEARTBEAT, app_hmi_control_session() + 1U,
                 2U, 0, 0U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_NO_SESSION);

    reset_fixture();
    g_snapshot[APP_SERVO_CONTROL_AXIS == 2U ? 1U : 0U].fault = 1U;
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_FEEDBACK_STALE);

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    assert(g_result.state == TEST_RESULT_SUCCEEDED);
    make_mailbox(mailbox, TEST_CMD_HEARTBEAT, app_hmi_control_session(), 2U, 0, 0U);
    submit_service(mailbox);
    assert(g_result.state == TEST_RESULT_SUCCEEDED);
    make_mailbox(mailbox, TEST_CMD_HEARTBEAT, app_hmi_control_session(), 2U, 0, 0U);
    mailbox[22U] = 2U;
    mailbox[8U] = TEST_CMD_RELEASE;
    submit_service(mailbox);
    assert_rejected(TEST_ERR_SEQ_CONFLICT);
    make_mailbox(mailbox, TEST_CMD_HEARTBEAT, app_hmi_control_session(), 2U, 0, 0U);
    submit_service(mailbox); assert(g_result.state == TEST_RESULT_SUCCEEDED);

    make_mailbox(mailbox, TEST_CMD_HEARTBEAT, app_hmi_control_session(), 1U, 0, 0U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_OLD_SEQUENCE);

    reset_fixture();
    g_watchdog_requested = 1U;
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_SAFETY_DENIED);

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    mailbox[9U] = 1U;
    submit_service(mailbox);
    assert_rejected(TEST_ERR_OUT_OF_RANGE);

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    make_mailbox(mailbox, TEST_CMD_SPEED, app_hmi_control_session(), 2U,
                 10000, 500U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_OUT_OF_RANGE);

    make_mailbox(mailbox, TEST_CMD_SPEED, app_hmi_control_session(), 3U,
                 100, APP_HMI_SPEED_HOLD_MAX_MS + 1U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_OUT_OF_RANGE);
}

static void test_hold_timeout_and_release(void)
{
    uint16_t mailbox[24U];
    uint32_t session;
    uint16_t i;

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    session = app_hmi_control_session();
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 100, 500U);
    submit_service(mailbox);
    g_feedback_progress = 1U;
    for (i = 0U; i < 60U; i++)
    {
        advance_ms(10U);
        app_hmi_control_service();
    }
    for (i = 0U; i < 2000U && g_result.state != TEST_RESULT_SUCCEEDED; i++)
    {
        advance_ms(10U);
        app_hmi_control_service();
    }
    assert(g_stop_calls > 0U);
    assert(g_result.state == TEST_RESULT_SUCCEEDED);
    assert(app_hmi_control_session() == 0U);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 3U, 0, 0U);
    submit_service(mailbox);
    session = app_hmi_control_session();
    assert(session != 0U);
    make_mailbox(mailbox, TEST_CMD_RELEASE, session, 4U, 0, 0U);
    submit_service(mailbox);
    assert(g_result.state == TEST_RESULT_RUNNING);
    for (i = 0U; i < 6U && g_result.state != TEST_RESULT_SUCCEEDED; i++)
        app_hmi_control_service();
    assert(g_result.state == TEST_RESULT_SUCCEEDED);
    assert(app_hmi_control_session() == 0U);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    assert(app_control_arbiter_submit(APP_CONTROL_OWNER_REMOTE, (uint8_t)APP_SERVO_CONTROL_AXIS));
    app_control_arbiter_service();
    assert(app_control_arbiter_request(APP_CONTROL_OWNER_REMOTE,
                                        (uint8_t)APP_SERVO_CONTROL_AXIS, NULL) == 1U);
}

static void test_stop_failure_and_stale_feedback(void)
{
    uint16_t mailbox[24U];
    uint32_t session;

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    session = app_hmi_control_session();
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 100, 500U);
    submit_service(mailbox);
    app_hmi_control_service();
#if APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_BOTH
    app_hmi_control_service();
#endif
    g_stop_progress = 0U;
    advance_ms(501U);
    app_hmi_control_service();
    advance_ms(5001U);
    app_hmi_control_service();
    assert(g_result.state == TEST_RESULT_FAILED);
    assert(g_result.error == TEST_ERR_STOP_FAILED);
    assert(app_hmi_control_session() == 0U);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);

    reset_fixture();
    g_feedback_fresh = 0U;
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_FEEDBACK_STALE);
}

static void test_session_lease_and_late_speed(void)
{
    uint16_t mailbox[24U];
    uint32_t session;
    uint8_t i;

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    session = app_hmi_control_session();
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 100, 500U);
    submit_service(mailbox);

    /* 会话租期独立于速度保持期限，心跳停止后应按租期停车并释放。 */
    advance_ms(1001U);
    g_feedback_progress = 1U;
    for (i = 0U; i < 8U && app_hmi_control_session() != 0U; i++)
        app_hmi_control_service();
    assert(g_stop_calls > 0U);
    assert(app_hmi_control_session() == 0U);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    session = app_hmi_control_session();
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 100, 500U);
    submit_service(mailbox);
    make_mailbox(mailbox, TEST_CMD_STOP, session, 3U, 0, 0U);
    submit_service(mailbox);
    assert(g_result.state == TEST_RESULT_RUNNING);

    /* 停车事务未完成时，迟到速度命令不能恢复旧会话。 */
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 4U, 100, 500U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_SESSION_EXPIRED);

    g_feedback_progress = 1U;
    for (i = 0U; i < 8U && g_result.state != TEST_RESULT_SUCCEEDED; i++)
        app_hmi_control_service();
    assert(g_result.state == TEST_RESULT_SUCCEEDED);
}

static void test_safety_takeover_invalidates_motion(void)
{
    uint16_t mailbox[24U];
    uint32_t session;
    uint32_t before;

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    session = app_hmi_control_session();
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 100, 500U);
    submit_service(mailbox);
    before = g_velocity_calls;

    /* 看门狗、DI3、真实故障和RS485异常最终都必须经过统一安全接管接口。 */
    g_watchdog_requested = 1U;
    app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
    aimtor_set_hmi_control(false, 0U);
    app_hmi_control_service();
    assert(app_hmi_control_session() == 0U);
    assert(g_velocity_calls == before);
    assert(g_emergency_calls == 0U);

    make_mailbox(mailbox, TEST_CMD_SPEED, session, 3U, 100, 500U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_NO_SESSION);

    reset_fixture();
    g_feedback_fresh = 0U;
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_FEEDBACK_STALE);
}

static void test_control_loss_invalidates_old_session(void)
{
    uint16_t mailbox[24U];
    uint32_t session;
    uint32_t before;

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    session = app_hmi_control_session();
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 100, 500U);
    submit_service(mailbox);
    before = g_velocity_calls;
    app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
    aimtor_set_hmi_control(false, 0U);
    app_hmi_control_service();
    assert(app_hmi_control_session() == 0U);
    assert(g_velocity_calls == before);

    make_mailbox(mailbox, TEST_CMD_SPEED, session, 3U, 100, 500U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_NO_SESSION);
}

static void test_r2_boundaries_and_lifetime(void)
{
    uint16_t mailbox[24];
    app_hmi_control_status_t status;
    uint32_t session;
    unsigned index;
    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    mailbox[22] = 1U; submit_service(mailbox);
    assert_rejected(TEST_ERR_OUT_OF_RANGE);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    mailbox[22] = 2U; submit_service(mailbox);
    session = app_hmi_control_session(); assert(session);
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 1600, 500U); submit_service(mailbox);
    assert(g_result.state == TEST_RESULT_ACCEPTED);
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 3U, -1600, 500U); submit_service(mailbox);
    assert(g_result.state == TEST_RESULT_ACCEPTED);
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 4U, 1601, 500U); submit_service(mailbox);
    assert_rejected(TEST_ERR_OUT_OF_RANGE);
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 4U, -1601, 500U); submit_service(mailbox);
    assert_rejected(TEST_ERR_OUT_OF_RANGE);
    make_mailbox(mailbox, 0x20U, session, 4U, 0, 500U); submit_service(mailbox);
    assert_rejected(TEST_ERR_UNSUPPORTED_CMD);
    for (index = 0U; index < 4U; ++index)
    {
        advance_ms(100U);
        make_mailbox(mailbox, TEST_CMD_HEARTBEAT, session, 4U + index, 0, 0U);
        submit_service(mailbox);
    }
    advance_ms(100U); app_hmi_control_service();
    app_hmi_control_get_status(&status);
    assert(!(status.flags & 2U) && (status.flags & 4U));
    assert(status.stop.reason == 3U && status.stop.deceleration_mm_s2 == APP_HMI_LINK_STOP_DECEL_MM_S2);
    assert(status.timeout_ms >= 2000U);
    advance_ms(1U);
    make_mailbox(mailbox, TEST_CMD_STOP, 0U, 8U, 0, 0U); submit_service(mailbox);
    {
        uint32_t timeout = status.timeout_ms, event = status.stop.event;
        app_hmi_control_get_status(&status);
        assert(status.stop.deceleration_mm_s2 == APP_HMI_EMERGENCY_STOP_DECEL_MM_S2);
        assert(status.timeout_ms == timeout && status.stop.event == event);
    }
    g_feedback_progress = 1U;
    for (index = 0U; index < 1000U && app_hmi_control_session(); ++index)
    { advance_ms(10U); app_hmi_control_service(); }
    assert(!app_hmi_control_session()); app_hmi_control_get_status(&status);
    assert(status.stop.result == TEST_RESULT_SUCCEEDED && status.stop.command == TEST_CMD_STOP);
    assert(status.stop.request_sequence == 8U && status.stop.zero_mask == APP_SERVO_CONTROL_AXIS);
    app_hmi_control_service(); app_hmi_control_get_status(&status);
    assert(status.stop.result == TEST_RESULT_SUCCEEDED);
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 7U, 0, 0U); submit_service(mailbox);
    assert_rejected(TEST_ERR_OLD_SEQUENCE);

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U); submit_service(mailbox);
    advance_ms(450U); submit_service(mailbox);
    advance_ms(50U); app_hmi_control_service(); app_hmi_control_get_status(&status);
    assert(status.stop.reason == 3U && !(status.flags & 2U));

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U); submit_service(mailbox);
    session = app_hmi_control_session();
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 100, 500U);
    assert(app_hmi_control_submit(mailbox)); advance_ms(501U); app_hmi_control_service();
    assert(g_velocity_calls <= (APP_SERVO_CONTROL_AXIS == 3U ? 2U : 1U));
    app_hmi_control_get_status(&status); assert(!(status.flags & 2U));
}

static void test_authorization_preparation(void)
{
    uint16_t mailbox[24];
    app_hmi_control_status_t status;
    unsigned axis, index;
    reset_fixture();
    for (axis = 0U; axis < 2U; ++axis)
    { g_snapshot[axis].enabled = g_snapshot[axis].parameter_verified = 0U; g_snapshot[axis].control_mode = SERVO_CONTROL_NONE; }
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U); submit_service(mailbox);
    app_hmi_control_get_status(&status);
    assert(status.state == 1U && !(status.flags & 2U));
    assert(!app_control_arbiter_submit(APP_CONTROL_OWNER_REMOTE, APP_SERVO_CONTROL_AXIS));
    for (index = 0U; index < 12U && !app_hmi_control_session(); ++index) app_hmi_control_service();
    assert(app_hmi_control_session() && g_hmi_active);
    app_hmi_control_get_status(&status); assert(status.target == 0 && (status.flags & 2U));

    reset_fixture();
    for (axis = 0U; axis < 2U; ++axis) g_snapshot[axis].parameter_verified = 0U;
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U); submit_service(mailbox);
    make_mailbox(mailbox, TEST_CMD_STOP, 0U, 2U, 0, 0U); submit_service(mailbox);
    g_feedback_progress = 1U;
    for (index = 0U; index < 30U; ++index) app_hmi_control_service();
    assert(!app_hmi_control_session() && app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    app_hmi_control_get_status(&status); assert(status.stop.result == TEST_RESULT_SUCCEEDED);
}

static void test_stop_priority_and_write_completion(void)
{
    uint16_t mailbox[24];
    app_hmi_control_status_t status;
    unsigned axis, index;
    reset_fixture(); g_delayed_writes = 1U;
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U); submit_service(mailbox);
    make_mailbox(mailbox, TEST_CMD_HEARTBEAT, app_hmi_control_session(), 2U, 0, 0U);
    assert(app_hmi_control_submit(mailbox));
    make_mailbox(mailbox, TEST_CMD_STOP, 0U, 3U, 0, 0U);
    assert(app_hmi_control_submit(mailbox)); app_hmi_control_service();
    app_hmi_control_get_status(&status);
    assert(!(status.flags & 2U) && status.stop.result == TEST_RESULT_RUNNING);
    g_feedback_progress = 1U;
    for (index = 0U; index < 5U; ++index) app_hmi_control_service();
    assert(app_hmi_control_session());
    for (axis = 0U; axis < 2U; ++axis)
    { g_snapshot[axis].speed_write_completed = g_snapshot[axis].speed_write_sequence; }
    g_delayed_writes = 0U;
    for (index = 0U; index < 20U && app_hmi_control_session(); ++index) app_hmi_control_service();
    assert(!app_hmi_control_session());

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U); submit_service(mailbox);
    make_mailbox(mailbox, TEST_CMD_RELEASE, app_hmi_control_session(), 2U, 0, 0U); submit_service(mailbox);
    for (axis = 0U; axis < 2U; ++axis) g_snapshot[axis].enable_feedback_valid = 0U;
    app_hmi_control_service();
    app_hmi_control_get_status(&status);
    assert(status.stop.result == TEST_RESULT_FAILED);
    assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_SAFETY);
}

static void test_repeated_hmi_estop_after_confirmed_stop(void)
{
    uint16_t mailbox[24U];
    app_hmi_control_status_t status;
    uint32_t session;
    uint32_t event;
    uint32_t stop_calls;
    unsigned index;

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    session = app_hmi_control_session();
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 100, 500U);
    submit_service(mailbox);
    make_mailbox(mailbox, TEST_CMD_STOP, session, 3U, 0, 0U);
    submit_service(mailbox);
    g_feedback_progress = 1U;
    for (index = 0U; index < 1000U && app_hmi_control_session(); ++index)
    {
        advance_ms(10U);
        app_hmi_control_service();
    }
    assert(app_hmi_control_session() == 0U);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    app_hmi_control_get_status(&status);
    assert(status.stop.result == TEST_RESULT_SUCCEEDED);
    assert(status.stop.command == TEST_CMD_STOP);
    assert(status.stop.zero_mask == APP_SERVO_CONTROL_AXIS);
    assert(status.stop.enabled_mask == APP_SERVO_CONTROL_AXIS);
    event = status.stop.event;
    stop_calls = g_stop_calls;

    /* 停车完成后重复急停必须幂等，不得创建新事件或重新写入停车速度。 */
    for (index = 0U; index < 20U; ++index)
    {
        make_mailbox(mailbox, TEST_CMD_STOP, 0U, 10U + index, 0, 0U);
        submit_service(mailbox);
        assert(g_result.state == TEST_RESULT_SUCCEEDED);
        app_hmi_control_get_status(&status);
        assert(status.stop.event == event);
        assert(status.stop.result == TEST_RESULT_SUCCEEDED);
        assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    }
    assert(g_stop_calls == stop_calls);

    /* 重复急停后一次新授权即可恢复，且不继承旧目标速度。 */
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 40U, 0, 0U);
    submit_service(mailbox);
    assert(app_hmi_control_session() != 0U);
    app_hmi_control_get_status(&status);
    assert(status.target == 0);
}

static void test_repeated_hmi_estop_during_stop(void)
{
    uint16_t mailbox[24U];
    app_hmi_control_status_t status;
    uint32_t event;
    unsigned index;

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    make_mailbox(mailbox, TEST_CMD_SPEED, app_hmi_control_session(), 2U, 100, 500U);
    submit_service(mailbox);
    make_mailbox(mailbox, TEST_CMD_STOP, app_hmi_control_session(), 3U, 0, 0U);
    submit_service(mailbox);
    app_hmi_control_get_status(&status);
    assert(status.stop.result == TEST_RESULT_RUNNING);
    event = status.stop.event;

    for (index = 0U; index < 20U; ++index)
    {
        make_mailbox(mailbox, TEST_CMD_STOP, 0U, 10U + index, 0, 0U);
        submit_service(mailbox);
        app_hmi_control_get_status(&status);
        assert(g_result.state == TEST_RESULT_RUNNING);
        assert(status.stop.event == event);
        assert(status.stop.result == TEST_RESULT_RUNNING);
    }
}

static void test_idle_hmi_estop_is_safe_noop(void)
{
    uint16_t mailbox[24U];
    app_hmi_control_status_t status;

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_STOP, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    app_hmi_control_get_status(&status);
    assert(g_result.state == TEST_RESULT_SUCCEEDED);
    assert(g_result.error == TEST_ERR_NONE);
    assert(status.stop.event == 0U);
    assert(status.stop.result == TEST_RESULT_NONE);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_NONE);

    reset_fixture();
    g_snapshot[0].enable_feedback = 0U;
    g_snapshot[1].enable_feedback = 0U;
    make_mailbox(mailbox, TEST_CMD_STOP, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    assert(g_result.state == TEST_RESULT_SUCCEEDED);
    assert(g_result.error == TEST_ERR_NONE);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);

    reset_fixture();
    g_feedback_fresh = 0U;
    make_mailbox(mailbox, TEST_CMD_STOP, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    assert_rejected(TEST_ERR_STOP_FAILED);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_NONE);

    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_STOP, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 2U, 0, 0U);
    submit_service(mailbox);
    assert(app_hmi_control_session() != 0U);
    app_hmi_control_get_status(&status);
    assert(status.target == 0);
}

static void test_k5_cross_source_stop(void)
{
    const int16_t speeds[] = { 100, -100 };
    unsigned direction;
    for (direction = 0U; direction < 2U; ++direction)
    {
        uint16_t mailbox[24U];
        app_hmi_control_status_t status;
        app_control_stop_t initial;
        uint32_t session;
        unsigned index;
        reset_fixture();
        make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
        submit_service(mailbox);
        session = app_hmi_control_session();
        make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, speeds[direction], 500U);
        submit_service(mailbox);
        for (index = 0U; index < 20U; ++index)
        {
            advance_ms(10U);
            app_hmi_control_service();
        }
        assert(app_control_arbiter_submit_stop(5U, 500U, 0U, TEST_CMD_STOP));
        app_hmi_control_service();
        app_control_arbiter_get_stop(&initial);
        assert(initial.owner == APP_CONTROL_OWNER_HMI);
        assert(initial.result == TEST_RESULT_RUNNING && initial.request_sequence == 0U);
        assert(initial.timeout_ms != 0U);
        for (index = 0U; index < 20U; ++index)
        {
            app_control_stop_t current;
            advance_ms(1U);
            assert(app_control_arbiter_submit_stop(5U, 500U, 0U, TEST_CMD_STOP));
            app_hmi_control_service();
            app_control_arbiter_get_stop(&current);
            assert(current.event == initial.event && current.started_ms == initial.started_ms);
            assert(current.timeout_ms == initial.timeout_ms);
            assert(current.deceleration_mm_s2 >= initial.deceleration_mm_s2);
        }
        make_mailbox(mailbox, TEST_CMD_STOP, 0U, 3U, 0, 0U);
        submit_service(mailbox);
        make_mailbox(mailbox, TEST_CMD_SPEED, session, 4U, 100, 500U);
        submit_service(mailbox);
        assert_rejected(TEST_ERR_SESSION_EXPIRED);
        g_feedback_progress = 1U;
        for (index = 0U; index < 1000U && app_hmi_control_session(); ++index)
        {
            advance_ms(10U);
            app_hmi_control_service();
        }
        app_hmi_control_get_status(&status);
        assert(!status.session && app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
        assert(status.stop.event == initial.event && status.stop.result == TEST_RESULT_SUCCEEDED);
        assert(status.stop.zero_mask == APP_SERVO_CONTROL_AXIS);
        assert(status.stop.enabled_mask == APP_SERVO_CONTROL_AXIS);
        assert(status.phase == 4U && g_result.sequence == 3U);
        for (index = 0U; index < 20U; ++index) app_hmi_control_service();
        app_hmi_control_get_status(&status);
        assert(status.stop.event == initial.event && status.stop.result == TEST_RESULT_SUCCEEDED);
        make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 5U, 0, 0U);
        submit_service(mailbox);
        assert(app_hmi_control_session() != 0U);
        app_hmi_control_get_status(&status);
        assert(status.target == 0);
    }
    puts("PASS K5 cross-source stop: repeat deadline, late speed, screen upgrade, release and reauthorize");
}

static void test_degraded_output_and_resume(void)
{
    uint16_t mailbox[24U];
    int16_t boundary[2];
    uint32_t session;
    unsigned axis, n;
    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    session = app_hmi_control_session(); assert(session);
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 2U, 200, 500U);
    submit_service(mailbox);
    for (n = 0U; n < 10U; ++n) { advance_ms(10U); app_hmi_control_service(); }
    for (axis = 0U; axis < 2U; ++axis)
    {
        boundary[axis] = g_snapshot[axis].actual_speed_rpm;
        g_snapshot[axis].feedback_degraded = 1U;
        g_snapshot[axis].feedback_failures[1] = 4U;
        g_snapshot[axis].consecutive_failures = 4U;
    }
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 3U, 400, 500U);
    submit_service(mailbox);
    for (n = 0U; n < 15U; ++n) { advance_ms(10U); app_hmi_control_service(); }
    assert(app_hmi_control_session() == session && !g_emergency_calls);
    for (axis = 0U; axis < 2U; ++axis)
    {
        if (APP_SERVO_CONTROL_AXIS & (1U << axis))
            assert(g_snapshot[axis].actual_speed_rpm <= boundary[axis]);
        boundary[axis] = g_snapshot[axis].actual_speed_rpm;
        g_snapshot[axis].feedback_degraded = 0U;
        g_snapshot[axis].feedback_failures[1] = 0U;
        g_snapshot[axis].consecutive_failures = 0U;
    }
    advance_ms(10U); app_hmi_control_service();
    for (axis = 0U; axis < 2U; ++axis)
    {
        assert(g_snapshot[axis].actual_speed_rpm - boundary[axis] <= 10);
        g_snapshot[axis].feedback_degraded = 1U;
    }
    make_mailbox(mailbox, TEST_CMD_SPEED, session, 4U, -200, 500U);
    submit_service(mailbox);
    for (n = 0U; n < 10U; ++n) { advance_ms(10U); app_hmi_control_service(); }
    assert(app_hmi_control_session() == session && !g_emergency_calls);
    for (axis = 0U; axis < 2U; ++axis) assert(g_snapshot[axis].actual_speed_rpm >= 0);
    puts("PASS HMI degraded: session retained, no increase, resume jump or reversal");
}

static void test_trace_ring_wrap_and_target_intervals(void)
{
    uint16_t mailbox[24];
    uint32_t session, sequence;
    app_hmi_trace_stats_t stats;
    app_hmi_trace_event_t entry, prior;
    unsigned i;
    reset_fixture();
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U); submit_service(mailbox);
    session = app_hmi_control_session(); assert(session);
    for (sequence = 2U; sequence < 74U; ++sequence)
    {
        advance_ms(sequence == 3U ? 250U : 10U);
        make_mailbox(mailbox, TEST_CMD_SPEED, session, sequence, 0, 500U); submit_service(mailbox);
        assert(g_result.state == TEST_RESULT_ACCEPTED);
    }
    app_hmi_control_trace_stats(&stats);
    assert(stats.count == APP_HMI_TRACE_CAPACITY && stats.total > stats.count);
    assert(stats.max_target_gap_ms == 250U && stats.first_anomaly_gap_ms == 250U);
    assert(stats.first_anomaly_ms > 0U);
    memset(&prior, 0, sizeof(prior));
    for (i = 0; i < stats.count; ++i)
    {
        assert(app_hmi_control_trace_read((uint16_t)i, &entry));
        assert(entry.kind == APP_HMI_TRACE_TARGET && entry.session == session);
        assert(entry.sequence == 10U + i && entry.at_ms >= prior.at_ms);
        prior = entry;
    }
    assert(!app_hmi_control_trace_read(APP_HMI_TRACE_CAPACITY, &entry));
    assert(!app_hmi_control_trace_read(0U, NULL));
    app_hmi_control_trace_stats(NULL);
    reset_fixture(); app_hmi_control_trace_stats(&stats); assert(!stats.total && !stats.count);
    puts("PASS HMI trace: fixed ring wrap, current sequence, timing maximum, first anomaly and reset");
}

static void test_reauthorize_does_not_publish_historical_stop_as_current(void)
{
    uint16_t mailbox[24];
    uint32_t session, event;
    unsigned n;
    app_hmi_control_status_t status;
    reset_fixture();
    g_feedback_progress = 1U;
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U);
    submit_service(mailbox);
    session = app_hmi_control_session(); assert(session);
    make_mailbox(mailbox, TEST_CMD_STOP, session, 2U, 0, 0U);
    submit_service(mailbox);
    for (n = 0U; n < 2000U && app_hmi_control_session(); ++n)
    { advance_ms(10U); app_hmi_control_service(); }
    app_hmi_control_get_status(&status);
    assert(!status.session && status.phase == 4U && (status.flags & 8U));
    event = status.stop.event;
    make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 3U, 0, 0U);
    submit_service(mailbox);
    app_hmi_control_get_status(&status);
    assert(status.session && status.phase == 0U && !(status.flags & 12U));
    assert(status.stop.event == event && status.stop.result == TEST_RESULT_SUCCEEDED);
    puts("PASS new authorization publishes current phase, retains historical stop");
}

int main(void)
{
    setbuf(stdout, NULL);
    if (!APP_HMI_CONTROL_ENABLED || APP_SERVO_READONLY_TEST)
    {
        uint16_t mailbox[24];
        reset_fixture();
        make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 1U, 0, 0U); submit_service(mailbox);
        assert_rejected(!APP_HMI_CONTROL_ENABLED ? TEST_ERR_READ_ONLY : TEST_ERR_SAFETY_DENIED);
        assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
        assert(!g_velocity_calls && !g_stop_calls && !app_hmi_control_session());
        puts("PASS HMI disabled/readonly rejects authorization"); return 0;
    }
    test_trace_ring_wrap_and_target_intervals();
    test_authorization_preparation();
    test_reauthorize_does_not_publish_historical_stop_as_current();
    test_r2_boundaries_and_lifetime();
    test_stop_priority_and_write_completion();
    test_repeated_hmi_estop_after_confirmed_stop();
    test_repeated_hmi_estop_during_stop();
    test_idle_hmi_estop_is_safe_noop();
    test_acquire_speed_and_heartbeat();
    test_validation_and_conflicts();
    test_hold_timeout_and_release();
    test_stop_failure_and_stale_feedback();
    test_session_lease_and_late_speed();
    test_safety_takeover_invalidates_motion();
    test_control_loss_invalidates_old_session();
    test_k5_cross_source_stop();
    test_degraded_output_and_resume();
    printf("PASS HMI control state machine: acquire, speed, heartbeat, sequence, arbitration, timeout, stop and safety takeover\n");
    return 0;
}

bool aimtor_recovery_blocks_motion(void) { return false; }

#ifndef HMI_REMOTE_JOINT
void aimtor_get_recovery_status(servo_recovery_status_t *out) { memset(out, 0, sizeof(*out)); }
servo_result_t aimtor_request_recovery(uint8_t source) { (void)source; return SERVO_NOT_READY; }
#endif
