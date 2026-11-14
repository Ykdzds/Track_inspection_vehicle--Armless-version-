#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_remote_servo.h"
#include "servo_debug_control.h"
#include "motion_control.h"
#include "task.h"
#include "app_config.h"
#include "app_cli_servo_session.h"
#include "app_health.h"
#include "app_control_arbiter.h"

static servo_recovery_status_t test_recovery;
static unsigned recovery_requests;
void aimtor_get_recovery_status(servo_recovery_status_t *out) { *out = test_recovery; }
servo_result_t aimtor_request_recovery(uint8_t source)
{
    if(test_recovery.state != SERVO_RECOVERY_READY) return SERVO_NOT_READY;
    test_recovery.source=source; test_recovery.state=SERVO_RECOVERY_REQUESTED;
    ++recovery_requests; return SERVO_OK;
}
static uint8_t watchdog_test_requested;
bool app_health_watchdog_test_requested(void) { return watchdog_test_requested != 0U; }
void app_health_request_watchdog_test(void)
{
    if (APP_WDG_TEST_K6_SHORT_ENABLED) watchdog_test_requested = 1U;
}

static uint8_t cli_active;
static cli_session_reason_t cli_abort;
bool app_cli_servo_session_active(void) { return cli_active != 0U; }
void app_cli_servo_session_abort(cli_session_reason_t reason) { cli_abort = reason; }
void app_cli_servo_session_external_stop(void) { cli_abort = CLI_EXIT_K1; }

#define TEST_AXIS ((APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_RIGHT) ? SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT)

static TickType_t ticks;
static servo_snapshot_t axes[SERVO_AXIS_COUNT];
static servo_debug_status_t debug;
static int16_t written;
static unsigned stops;
static unsigned prepares;
static unsigned writes[SERVO_AXIS_COUNT];
static uint8_t hold_response;
static uint8_t prepare_fail_mask;
static uint8_t enable_fail_mask;
static uint8_t boot_ready = 1U;
static uint8_t startup_hold;
static uint8_t hold_ready = 1U;
static uint8_t k1_rearm;
static unsigned rearms;
static uint8_t stop_gate;
static uint8_t disable_unconfirmed;
static servo_k1_hold_status_t k1_hold;
static unsigned enables;

static void reset_remote(void)
{
    memset(&k1_hold, 0, sizeof(k1_hold));
    disable_unconfirmed = 0U;
    app_remote_servo_init();
    app_control_arbiter_init();
}

servo_result_t aimtor_k1_hold_request(uint8_t mask)
{
    assert(mask == app_config_servo_control_axis_mask());
    k1_hold.axis_mask = mask;
    k1_hold.state = SERVO_K1_HOLD_CONFIRMING;
    return SERVO_OK;
}
bool aimtor_k1_hold_get_status(servo_k1_hold_status_t *out)
{
    unsigned id;
    *out = k1_hold;
    out->enabled_mask = out->zero_mask = 0U;
    out->disturbed = 0U;
    for (id = 0U; id < SERVO_AXIS_COUNT; id++)
    {
        if (!(out->axis_mask & (1U << id))) continue;
        if (axes[id].enabled) out->enabled_mask |= (uint8_t)(1U << id);
        if (axes[id].actual_speed_rpm == 0) out->zero_mask |= (uint8_t)(1U << id);
        else out->disturbed = 1U;
        if (!axes[id].online || !axes[id].enabled || axes[id].fault ||
            axes[id].fault_code || !axes[id].parameter_verified ||
            axes[id].speed_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
            axes[id].fault_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
            axes[id].control_mode != SERVO_CONTROL_RS485_VELOCITY)
            out->confirmed_mask &= (uint8_t)~(1U << id);
    }
    out->valid = out->state == SERVO_K1_HOLD_READY &&
        out->confirmed_mask == out->axis_mask;
    return true;
}
servo_result_t aimtor_k1_hold_release(void)
{
    servo_k1_hold_status_t out;
    aimtor_k1_hold_get_status(&out);
    if (!out.valid || out.zero_mask != out.axis_mask) return SERVO_NOT_READY;
    memset(&k1_hold, 0, sizeof(k1_hold));
    return SERVO_OK;
}

TickType_t xTaskGetTickCount(void) { return ticks; }
uint32_t HAL_GetTick(void) { return (uint32_t)ticks; }
bool aimtor_boot_zero_ready(void) { return boot_ready != 0U; }
bool aimtor_startup_hold_active(void) { return startup_hold != 0U; }
bool aimtor_startup_hold_ready(void) { return boot_ready && hold_ready; }
void aimtor_set_motion_authorized(bool authorized) { (void)authorized; }
servo_result_t aimtor_remote_prepare_mode(servo_axis_t axis)
{ return aimtor_set_control_mode(axis, SERVO_CONTROL_RS485_VELOCITY); }
servo_result_t aimtor_remote_prepare_enable(servo_axis_t axis)
{ return aimtor_enable(axis); }
void aimtor_set_remote_stop_active(bool active) { stop_gate = active; }
bool aimtor_get_snapshot(servo_axis_t id, servo_snapshot_t *out)
{
    assert(id < SERVO_AXIS_COUNT);
    *out = axes[id];
    return true;
}
bool aimtor_get_snapshot_pair(servo_snapshot_t *left, servo_snapshot_t *right)
{
    return aimtor_get_snapshot(SERVO_AXIS_LEFT, left) &&
           aimtor_get_snapshot(SERVO_AXIS_RIGHT, right);
}
bool servo_debug_get_status(servo_debug_status_t *out)
{
    *out = debug;
    return true;
}
bool aimtor_safety_stop_active(void) { return false; }
void aimtor_request_controlled_safety_stop(uint8_t offline) { (void)offline; }
void aimtor_emergency_stop(void)
{
    unsigned id;
    stops++;
    if (k1_hold.state == SERVO_K1_HOLD_CONFIRMING ||
        k1_hold.state == SERVO_K1_HOLD_READY)
        k1_hold.state = SERVO_K1_HOLD_FAILED;
    k1_rearm = 0U;
    if (startup_hold) hold_ready = 0U;
    for (id = 0; id < SERVO_AXIS_COUNT; id++)
    {
        if (!app_config_servo_axis_controllable(id)) continue;
        axes[id].enabled = 0;
        axes[id].parameter_verified = 0;
        axes[id].stop_latched = 1;
        axes[id].disable_verified = !disable_unconfirmed;
        axes[id].busy = 0;
    }
}
void aimtor_emergency_stop_k1(void)
{
    uint8_t allowed = hold_ready || k1_rearm;
    aimtor_emergency_stop();
    k1_rearm = allowed;
}
servo_result_t aimtor_startup_hold_request_rearm(void)
{
    unsigned id;
    if (!k1_rearm) return SERVO_NOT_READY;
    for (id = 0U; id < SERVO_AXIS_COUNT; id++)
    {
        if (!app_config_servo_axis_controllable(id)) continue;
        if (!axes[id].online || axes[id].fault || axes[id].actual_speed_rpm ||
            !axes[id].disable_verified || axes[id].busy) return SERVO_NOT_READY;
    }
    rearms++;
    k1_rearm = 0U;
    for (id = 0U; id < SERVO_AXIS_COUNT; id++)
    {
        if (!app_config_servo_axis_controllable(id)) continue;
        axes[id].enabled = axes[id].parameter_verified = 1U;
        axes[id].stop_latched = 0U;
    }
    hold_ready = 1U;
    return SERVO_OK;
}
void aimtor_set_remote_control_active(bool active)
{
    (void)active;
}
void aimtor_set_remote_control_mask(uint8_t mask)
{
    (void)mask;
}
bool aimtor_remote_control_active(void)
{
    return true;
}
void servo_debug_abort_from_fault(void) { debug.state = SERVO_DEBUG_FAULT; }
servo_result_t aimtor_clear_stop_latch(void)
{
    unsigned id;
    for (id = 0; id < SERVO_AXIS_COUNT; id++)
        axes[id].stop_latched = 0;
    return SERVO_OK;
}
servo_result_t servo_debug_prepare(servo_debug_mode_t mode)
{
    assert(mode == SERVO_DEBUG_MODE_RS485_SPEED);
    prepares++;
    debug.mode = mode;
    debug.state = SERVO_DEBUG_READY;
    axes[TEST_AXIS].parameter_verified = 1;
    axes[TEST_AXIS].control_mode = SERVO_CONTROL_RS485_VELOCITY;
    return SERVO_OK;
}
servo_result_t servo_debug_enable(void)
{
    axes[TEST_AXIS].enabled = 1;
    return SERVO_OK;
}
servo_result_t aimtor_set_control_mode(servo_axis_t id,
                                       servo_control_mode_t mode)
{
    unsigned i;
    assert(app_config_servo_axis_controllable(id));
    /* 模拟生产模式配置必须在全部轴失能时执行的门禁。 */
    for (i = 0; i < SERVO_AXIS_COUNT; i++) assert(!axes[i].enabled);
    prepares++;
    if (prepare_fail_mask & (1U << id)) return SERVO_NOT_READY;
    axes[id].control_mode = mode;
    axes[id].parameter_verified = 1U;
    return SERVO_OK;
}
servo_result_t aimtor_enable(servo_axis_t id)
{
    unsigned i;
    assert(app_config_servo_axis_controllable(id));
    for (i = 0; i < SERVO_AXIS_COUNT; i++)
        if (app_config_servo_axis_controllable(i)) assert(axes[i].parameter_verified);
    if (enable_fail_mask & (1U << id)) return SERVO_NOT_READY;
    enables++;
    axes[id].enabled = 1U;
    return SERVO_OK;
}
servo_result_t aimtor_set_velocity_rpm(servo_axis_t id, int16_t rpm)
{
#ifdef NORMAL_TIMING_TEST
    if (axes[0].busy || axes[1].busy) return SERVO_BUSY;
#endif
    assert(app_config_servo_axis_controllable(id));
    assert(!axes[id].busy);
    writes[id]++;
    written = rpm;
    axes[id].speed_write_sequence++;
    axes[id].busy = hold_response;
#ifdef NORMAL_TIMING_TEST
    if (timing_delay) {
        axes[id].busy = 1U; timing_due[id] = ticks + timing_delay; timing_rpm[id] = rpm;
    }
#endif
    if (!axes[id].busy)
    {
        axes[id].speed_write_completed = axes[id].speed_write_sequence;
        axes[id].speed_write_result = SERVO_OK;
    }
    return SERVO_OK;
}
servo_result_t aimtor_set_stop_velocity_rpm(servo_axis_t id, int16_t rpm)
{
    return aimtor_set_velocity_rpm(id, rpm);
}
motion_result_t motion_stop(motion_stop_mode_t mode)
{
    (void)mode;
    return MOTION_OK;
}
static void feed(nrf_remote_snapshot_t *input)
{
    unsigned id;
    for (id = 0; id < SERVO_AXIS_COUNT; id++)
    {
#ifdef NORMAL_TIMING_TEST
        if (axes[id].busy && (int32_t)(ticks - timing_due[id]) >= 0) {
            axes[id].busy = 0U;
            axes[id].speed_write_completed = axes[id].speed_write_sequence;
            axes[id].speed_write_result = SERVO_OK;
            axes[id].actual_speed_rpm = timing_rpm[id];
        }
#endif
        axes[id].speed_valid = axes[id].fault_valid = 1U;
        axes[id].enable_feedback_valid = 1U; axes[id].enable_feedback = axes[id].enabled;
        axes[id].enable_feedback_age_ms = 0U;
        axes[id].speed_sequence++;
        axes[id].speed_sample_tick = ticks;
    }
    if (k1_hold.state == SERVO_K1_HOLD_CONFIRMING && !disable_unconfirmed)
    {
        k1_hold.state = SERVO_K1_HOLD_READY;
        k1_hold.confirmed_mask = k1_hold.axis_mask;
    }
    if (k1_hold.state == SERVO_K1_HOLD_READY)
    {
        servo_k1_hold_status_t out;
        aimtor_k1_hold_get_status(&out);
        if (!out.valid)
        {
            k1_hold.failure = 1U;
            aimtor_emergency_stop();
        }
    }
    app_remote_servo_update(input);
    app_control_arbiter_service();
    app_remote_servo_service();
}
static remote_servo_status_t status(void)
{
    remote_servo_status_t out;
    assert(app_remote_servo_get_status(&out));
    return out;
}
/* 生成当前量程下可准确映射到目标转速的摇杆测试值。 */
static uint16_t joystick_for_rpm(int rpm)
{
    unsigned magnitude = (unsigned)(rpm < 0 ? -rpm : rpm);
    unsigned span = rpm < 0 ? APP_REMOTE_SERVO_NEUTRAL_LOW :
        2000U - APP_REMOTE_SERVO_NEUTRAL_HIGH;
    unsigned offset = (magnitude * span + APP_REMOTE_SERVO_MAX_RPM - 1U) /
        APP_REMOTE_SERVO_MAX_RPM;
    return (uint16_t)(rpm < 0 ? APP_REMOTE_SERVO_NEUTRAL_LOW - offset :
        APP_REMOTE_SERVO_NEUTRAL_HIGH + offset);
}

static void arm(nrf_remote_snapshot_t *input)
{
    unsigned i;
    input->left_vertical = 1000;
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    for (i = 0; i < 10; i++) feed(input);
}

static void finish_stop(nrf_remote_snapshot_t *input)
{
    unsigned i;
    for (i = 0U; i < 1800U; i++)
    {
        remote_servo_status_t out = status();
        if (!out.hold_stopping &&
            out.controlled_stop_state != REMOTE_CONTROLLED_STOP_DISABLING) return;
        ticks += 10U;
        feed(input);
    }
    assert(!"停车状态机未结束");
}

static void run_sync_integration(nrf_remote_snapshot_t *input)
{
    unsigned i;
    unsigned before[2];
    int16_t expected_correction;
    remote_servo_status_t out;
    if (!APP_REMOTE_SERVO_SYNC_ENABLED ||
        app_config_servo_control_axis_count() != 2U ||
        app_config_servo_readonly_test()) return;
    memset(axes, 0, sizeof(axes));
    memset(input, 0, sizeof(*input));
    axes[0].online = axes[1].online = 1;
    input->link_up = 1;
    hold_response = prepare_fail_mask = enable_fail_mask = 0;
    reset_remote();
    arm(input);
    assert(status().authorized);
    input->left_vertical = joystick_for_rpm(100); /* 整数映射为100rpm。 */
    for (i = 0; i < 600; i++) { ticks += 10; feed(input); }
    assert(status().commanded_rpm == 100);
    before[0] = writes[0]; before[1] = writes[1];
    for (i = 0; i < 20; i++) { ticks += 10; feed(input); }
    assert(writes[0] == before[0] && writes[1] == before[1]);
    axes[0].actual_speed_rpm = 106;
    axes[1].actual_speed_rpm = 100;
    for (i = 0; i < 20; i++) { ticks += 10; feed(input); }
    out = status();
    assert(out.commanded_rpm == 100);
    /* 恒速补偿预期随现场P增益配置计算，不固定使用旧增益的结果。 */
    expected_correction = (int16_t)(
        APP_REMOTE_SERVO_SYNC_KP * (6 - APP_REMOTE_SERVO_SYNC_DEADBAND_RPM) + 0.5f);
    assert(out.left_commanded_rpm == 100 - expected_correction &&
           out.right_commanded_rpm == 100 + expected_correction);
    assert(out.left_commanded_rpm - out.right_commanded_rpm == -2 * out.batch_correction);
    assert(writes[0] > before[0] && writes[1] > before[1]);
    /* 暂失效不联停，补偿平滑归零而共同基础速度保留。 */
    axes[0].speed_age_ms = 150;
    for (i = 0; i < 30; i++) { ticks += 10; feed(input); }
    assert(status().authorized && status().left_commanded_rpm == 100);
    assert(status().right_commanded_rpm == 100);
    axes[0].speed_age_ms = 0;
    /* 非零反馈期间不得发反向速度。 */
    input->left_vertical = joystick_for_rpm(-100); /* 映射为-100rpm。 */
    for (i = 0; i < 180; i++) { ticks += 10; feed(input); }
    assert(status().commanded_rpm == 0);
    assert(status().sync.state == REMOTE_SYNC_REVERSING);
    assert(status().left_commanded_rpm == 0 && status().right_commanded_rpm == 0);
    axes[0].actual_speed_rpm = axes[1].actual_speed_rpm = 0;
    for (i = 0; i < 700; i++) { ticks += 10; feed(input); }
    assert(status().commanded_rpm == -100);
    /* 再次换向但反馈不归零，按指定超时联停。 */
    axes[0].actual_speed_rpm = axes[1].actual_speed_rpm = -100;
    input->left_vertical = joystick_for_rpm(100);
    for (i = 0; i < 650; i++) { ticks += 10; feed(input); }
    assert(status().state == REMOTE_SERVO_ESTOP);
    assert(status().stop_reason == REMOTE_STOP_REVERSE_TIMEOUT);
    axes[0].actual_speed_rpm = axes[1].actual_speed_rpm = 0;
    arm(input);
    assert(status().authorized);
    /* 半完成批次急停后，迟到的成功应答不能推进共同速度。 */
    hold_response = 1;
    input->left_vertical = joystick_for_rpm(100);
    ticks += 10; feed(input);
    ticks += 10; feed(input);
    axes[0].speed_write_completed = axes[0].speed_write_sequence;
    axes[0].speed_write_result = SERVO_OK;
    axes[0].busy = 0;
    feed(input);
    assert(status().left_commanded_rpm >= 0 &&
           status().left_commanded_rpm <= 20 && status().commanded_rpm == 0);
    app_remote_servo_request_estop();
    feed(input);
    axes[1].speed_write_completed = axes[1].speed_write_sequence;
    axes[1].speed_write_result = SERVO_OK;
    feed(input);
    assert(!status().authorized && status().commanded_rpm == 0);
    assert(status().left_commanded_rpm == 0 && status().right_commanded_rpm == 0);
    hold_response = 0;
    puts("Remote synchronization integration tests passed");
}

static void run_startup_hold_integration(nrf_remote_snapshot_t *input)
{
    unsigned i;
    unsigned before;
    if (app_config_servo_readonly_test()) return;
    memset(axes, 0, sizeof(axes));
    memset(input, 0, sizeof(*input));
    hold_response = prepare_fail_mask = enable_fail_mask = 0U;
    startup_hold = 1U;
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
    {
        if (!app_config_servo_axis_controllable(i)) continue;
        axes[i].online = axes[i].enabled = axes[i].parameter_verified = 1U;
        axes[i].control_mode = SERVO_CONTROL_RS485_VELOCITY;
    }
    input->link_up = 1U;
    reset_remote();
    before = stops;
    arm(input);
    assert(status().authorized && !status().estop_latched);
    assert(stops == before);
    input->left_vertical = joystick_for_rpm(100);
    for (i = 0U; i < 600U; i++) { ticks += 10U; feed(input); }
    assert(status().commanded_rpm == 100);
    /* 制造未完成速度应答，恢复停车必须收尾而不能恢复原批次。 */
    hold_response = 1U;
    input->left_vertical = 1300;
    ticks += 30U; feed(input);
    /* 未观察到离线快照，恢复代次仍必须撤销旧授权并完成停车。 */
    input->recovery_generation++;
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    ticks += 10U;
    feed(input);
    assert(!status().authorized && status().hold_stopping);
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
    {
        if (!axes[i].busy) continue;
        axes[i].speed_write_completed = axes[i].speed_write_sequence;
        axes[i].speed_write_result = SERVO_OK;
        axes[i].busy = 0U;
    }
    hold_response = 0U;
    ticks += 10U; feed(input);
    /* 半完成旧批次已有轴间差，停车每次仅允许再叠加一个斜坡步长。 */
    unsigned stop_diff_bound = (unsigned)abs(
        status().left_commanded_rpm - status().right_commanded_rpm) +
        (unsigned)(APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2 *
            60.0f * APP_GEAR_REDUCTION_RATIO /
            (3.14159265358979323846f * APP_WHEEL_DIAMETER_M) *
            APP_REMOTE_SERVO_STOP_MAX_DT_MS / 1000.0f) + 1U;
    /* 链路中途恢复也不能重启旧运动，必须完成停车并重新长按授权。 */
    input->link_up = 1U;
    for (i = 0U; i < 400U; i++)
    {
        remote_servo_status_t previous = status();
        remote_servo_status_t current;
        ticks += 10U;
        feed(input);
        current = status();
        assert(current.left_commanded_rpm <= previous.left_commanded_rpm);
        assert(current.right_commanded_rpm <= previous.right_commanded_rpm);
        if (app_config_servo_control_axis_count() == 2U)
            assert(abs(current.left_commanded_rpm - current.right_commanded_rpm) <=
                   (int)stop_diff_bound);
    }
    assert(status().state == REMOTE_SERVO_DISABLED);
    assert(!status().authorized && !status().hold_stopping);
    assert(status().left_commanded_rpm == 0 && status().right_commanded_rpm == 0);
    assert(stops == before);
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
        if (app_config_servo_axis_controllable(i)) assert(axes[i].enabled);
    arm(input);
    assert(status().authorized && stops == before);
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    finish_stop(input);
    assert(status().state == REMOTE_SERVO_ESTOP && stops == before);
    arm(input);
    assert(status().authorized && hold_ready && rearms == 0U);
    assert(stops == before);
    /* 未授权零速使能待机收到K5，也确认保持且不制造失能窗口。 */
    reset_remote();
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
        if (app_config_servo_axis_controllable(i)) axes[i].enabled = 1U;
    feed(input);
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    finish_stop(input);
    assert(status().state == REMOTE_SERVO_ESTOP && stops == before);
    arm(input);
    assert(status().authorized && rearms == 0U);
    startup_hold = 0U;
    hold_ready = 1U;
    puts("Remote startup hold tests passed");
}

static void run_radio_recovery_integration(nrf_remote_snapshot_t *input)
{
    unsigned i;
    if (app_config_servo_readonly_test()) return;
    memset(axes, 0, sizeof(axes));
    memset(input, 0, sizeof(*input));
    hold_response = prepare_fail_mask = enable_fail_mask = 0U;
    axes[0].online = axes[1].online = 1U;
    input->link_up = 1U;
    reset_remote();
    arm(input);
    assert(status().authorized);
    hold_response = 1U;
    input->left_vertical = joystick_for_rpm(100);
    ticks += 10U; feed(input);
    ticks += 10U; feed(input);
    input->recovery_generation++;
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    assert(!status().authorized && status().stop_reason == REMOTE_STOP_LINK);
    /* 迟到应答和恢复中重复旧授权事件都不能复活旧命令。 */
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
    {
        axes[i].speed_write_completed = axes[i].speed_write_sequence;
        axes[i].speed_write_result = SERVO_OK;
        axes[i].busy = 0U;
    }
    input->left_vertical = 1000U;
    hold_response = 0U;
    finish_stop(input);
    assert(!status().authorized && status().commanded_rpm == 0);
    hold_response = 0U;
    arm(input);
    assert(status().authorized);
    puts("Remote radio recovery generation tests passed");
}

static void setup_stop_test(nrf_remote_snapshot_t *input, int direction)
{
    unsigned i;
    memset(axes, 0, sizeof(axes));
    memset(input, 0, sizeof(*input));
    memset(&debug, 0, sizeof(debug));
    startup_hold = hold_ready = 1U;
    hold_response = prepare_fail_mask = enable_fail_mask = 0U;
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
    {
        axes[i].online = axes[i].enabled = axes[i].parameter_verified = 1U;
        axes[i].control_mode = SERVO_CONTROL_RS485_VELOCITY;
    }
    input->link_up = 1U;
    reset_remote();
    arm(input);
    assert(status().authorized);
    input->left_vertical = joystick_for_rpm(direction * 600);
    for (i = 0U; i < 3200U; i++) { ticks += 10U; feed(input); }
    assert(status().commanded_rpm == direction * 600);
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
        axes[i].actual_speed_rpm = (int16_t)(direction * 600);
}

static void run_degraded_output_test(nrf_remote_snapshot_t *input)
{
    unsigned n, axis, old_stops;
    remote_servo_status_t boundary, current;
    if (app_config_servo_readonly_test()) return;
    setup_stop_test(input, 1);
    old_stops = stops; boundary = status();
    for (axis = 0U; axis < 2U; ++axis)
    {
        axes[axis].feedback_degraded = 1U;
        axes[axis].feedback_failures[0] = 4U;
        axes[axis].consecutive_failures = 4U;
    }
    input->left_vertical = joystick_for_rpm(900);
    for (n = 0U; n < 15U; ++n) { ticks += 10U; feed(input); }
    current = status();
    assert(current.authorized && stops == old_stops && !current.sync.trip);
    assert(current.left_commanded_rpm <= boundary.left_commanded_rpm);
    assert(current.right_commanded_rpm <= boundary.right_commanded_rpm);
    boundary = current;
    for (axis = 0U; axis < 2U; ++axis)
    {
        axes[axis].feedback_degraded = 0U;
        axes[axis].feedback_failures[0] = 0U;
        axes[axis].consecutive_failures = 0U;
    }
    ticks += 10U; feed(input); current = status();
    assert(current.left_commanded_rpm - boundary.left_commanded_rpm <= 10);
    assert(current.right_commanded_rpm - boundary.right_commanded_rpm <= 10);
    for (axis = 0U; axis < 2U; ++axis) axes[axis].feedback_degraded = 1U;
    input->left_vertical = joystick_for_rpm(-600);
    for (n = 0U; n < 20U; ++n) { ticks += 10U; feed(input); }
    current = status();
    assert(current.authorized && stops == old_stops);
    assert(current.left_commanded_rpm >= 0 && current.right_commanded_rpm >= 0);
    puts("PASS remote degraded: four failures retain session, no increase, resume jump or reversal");
}

static void follow_stop(nrf_remote_snapshot_t *input)
{
    remote_servo_status_t previous = status();
    unsigned i;
    axes[0].actual_speed_rpm = previous.left_commanded_rpm;
    axes[1].actual_speed_rpm = previous.right_commanded_rpm;
    ticks += 10U;
    feed(input);
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
    {
        int16_t old = i ? previous.right_commanded_rpm : previous.left_commanded_rpm;
        int16_t now = i ? status().right_commanded_rpm : status().left_commanded_rpm;
        assert(abs(now) <= abs(old));
        assert(old == 0 ? now == 0 : now == 0 || ((now > 0) == (old > 0)));
    }
}

static void run_controlled_stop_tests(nrf_remote_snapshot_t *input)
{
    unsigned i;
    int direction;
    unsigned before;
    uint32_t deadline;
    TickType_t start;
    int16_t previous_command;
    int32_t maximum_step;
    float expected_deceleration;
    float expected_zero_ms;
    float link_zero_ms;
    if (app_config_servo_readonly_test()) return;
    for (direction = -1; direction <= 1; direction += 2)
    {
        setup_stop_test(input, direction);
        before = stops;
        input->link_up = 0U;
        feed(input);
        assert(stop_gate && status().state == REMOTE_SERVO_STOPPING);
        assert(!status().authorized && !status().sync.active);
        assert(status().stop_deceleration_mps2 == APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2);
        {
            app_control_stop_t event;
            app_control_arbiter_get_stop(&event);
            assert(event.reason == 4U);
            assert(event.deceleration_mm_s2 == APP_REMOTE_SERVO_LINK_STOP_DECEL_MM_S2);
        }
        deadline = status().stop_timeout_ms;
        link_zero_ms = 600.0f * 1000.0f * 3.14159265358979323846f *
            APP_WHEEL_DIAMETER_M /
            (APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2 * 60.0f * APP_GEAR_REDUCTION_RATIO);
        assert(abs((int32_t)deadline - (int32_t)(link_zero_ms +
            APP_REMOTE_SERVO_STOP_CONFIRM_MARGIN_MS)) <= 2);
        /* 恢复链路及停车中长按不能授权；重复K5不延长期限。 */
        input->link_up = 1U;
        input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        follow_stop(input);
        assert(status().controlled_stop_reason == REMOTE_STOP_LINK);
        assert(!status().estop_latched);
        input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        follow_stop(input);
        assert(status().controlled_stop_reason == REMOTE_STOP_K1);
        expected_deceleration = APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 >
            APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2 ?
            APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 : APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2;
        assert(status().stop_deceleration_mps2 == expected_deceleration);
        assert(status().stop_timeout_ms == deadline);
        input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        follow_stop(input);
        assert(status().stop_timeout_ms == deadline);
        for (i = 0U; i < 1500U && status().hold_stopping; i++) follow_stop(input);
        assert(!status().hold_stopping && status().state == REMOTE_SERVO_ESTOP);
        feed(input);
        assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_DONE);
        assert(stops == before && !stop_gate);
        assert(status().stop_zero_time_valid && status().stop_complete_time_valid);
        assert(status().stop_first_command_valid && status().stop_first_command_ms <= 50U);
        expected_zero_ms = 600.0f * 1000.0f * 3.14159265358979323846f *
            APP_WHEEL_DIAMETER_M /
            (expected_deceleration * 60.0f * APP_GEAR_REDUCTION_RATIO);
        assert(status().stop_zero_ms + 10U >= (uint32_t)expected_zero_ms &&
               status().stop_zero_ms < (uint32_t)expected_zero_ms + 900U);
        feed(input);
        assert(!status().authorized);
        input->left_vertical = 1000U;
        arm(input);
        assert(status().authorized);
    }
    /* 实际反馈略超600rpm仍能停车，期限按实际初速而非命令量程计算。 */
    setup_stop_test(input, 1);
    axes[TEST_AXIS].actual_speed_rpm = 610;
    input->link_up = 0U;
    feed(input);
    assert(status().state == REMOTE_SERVO_STOPPING);
    assert(abs((int32_t)status().stop_timeout_ms - (int32_t)(
        610.0f * 1000.0f * 3.14159265358979323846f * APP_WHEEL_DIAMETER_M /
        (APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2 * 60.0f * APP_GEAR_REDUCTION_RATIO) +
        APP_REMOTE_SERVO_STOP_CONFIRM_MARGIN_MS)) <= 2);
    /* Stop integration uses elapsed wall time, including service stalls. */
    setup_stop_test(input, 1);
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    maximum_step = (int32_t)(APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 *
        60.0f * APP_GEAR_REDUCTION_RATIO /
        (3.14159265358979323846f * APP_WHEEL_DIAMETER_M) *
        410.0f / 1000.0f) + 2;
    for (i = 0U; i < 2U; i++)
    {
        previous_command = TEST_AXIS == SERVO_AXIS_LEFT ?
            status().left_commanded_rpm : status().right_commanded_rpm;
        ticks += 400U; feed(input);
        /* 收到应答更新确认值，不能把两个独立50ms周期算成一个。 */
        ticks += 10U; feed(input);
        assert(previous_command - (TEST_AXIS == SERVO_AXIS_LEFT ?
            status().left_commanded_rpm : status().right_commanded_rpm) <= maximum_step);
    }
    /* 实际速度始终非零，期限到期只能失败，后续K5不能绕过锁存。 */
    start = ticks;
    deadline = status().stop_timeout_ms;
    for (i = 0U; i < 1500U && status().hold_stopping; i++)
    {
        ticks += 10U; feed(input);
    }
    assert((TickType_t)(ticks - start) < deadline + 20U);
    assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED);
    assert(status().stop_reason == REMOTE_STOP_TRANSACTION && !status().stop_zero_time_valid);
    axes[0].actual_speed_rpm = axes[1].actual_speed_rpm = 0;
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++; feed(input);
    arm(input);
    assert(!status().authorized && status().controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED);
    /* 零速必须来自写0成功后的两次新反馈，重复快照不增加计数。 */
    setup_stop_test(input, 1);
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    for (i = 0U; i < 1400U; i++)
    {
        ticks += 10U; feed(input);
        if (status().left_commanded_rpm == 0 && status().right_commanded_rpm == 0 &&
            status().controlled_stop_state == REMOTE_CONTROLLED_STOP_VERIFYING) break;
    }
    assert(i < 1400U);
    /* 序号0是回绕后的合法反馈，不可作为无效哨兵。 */
    axes[0].speed_sequence = axes[1].speed_sequence = UINT32_MAX;
    axes[0].actual_speed_rpm = axes[1].actual_speed_rpm = 0;
    feed(input);
    assert(status().hold_stopping);
    for (i = 0U; i < 5U; i++)
    {
        app_remote_servo_update(input);
        app_remote_servo_service();
        assert(status().hold_stopping);
    }
    feed(input);
    assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING);
    feed(input);
    assert(!status().hold_stopping);
    assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_DONE);
    /* 使能确认尚未完成，重复K5不得重启事务或刷新期限。 */
    setup_stop_test(input, 1);
    disable_unconfirmed = 1U;
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    for (i = 0U; i < 1500U &&
         status().controlled_stop_state != REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING; i++)
        follow_stop(input);
    assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING);
    assert(!status().stop_complete_time_valid);
    before = stops;
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    assert(stops == before && !status().authorized);
    ticks += status().stop_timeout_ms;
    feed(input);
    assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED);
    assert(!status().stop_complete_time_valid && !status().authorized);
    disable_unconfirmed = 0U;
    /* 通信故障、真实故障及人工急停抢占斜坡。 */
    for (i = 0U; i < 4U; i++)
    {
        setup_stop_test(input, 1);
        input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        feed(input);
        if (i == 0U) axes[TEST_AXIS].fault = 1U;
        else if (i == 1U) axes[TEST_AXIS].speed_age_ms = APP_SERVO_STATUS_MAX_AGE_MS;
        else if (i == 2U) app_remote_servo_request_estop();
        else
        {
            hold_response = 1U;
            ticks += 20U; feed(input);
            ticks += 20U; feed(input);
            axes[TEST_AXIS].speed_write_completed = axes[TEST_AXIS].speed_write_sequence;
            axes[TEST_AXIS].speed_write_result = SERVO_NOT_READY;
            axes[TEST_AXIS].busy = 0U;
        }
        feed(input);
        assert(status().state == REMOTE_SERVO_ESTOP);
        assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED);
        assert(!axes[TEST_AXIS].enabled);
    }
    /* 时间计数跨回绕仍完成失联停车，POR保留零速使能。 */
    setup_stop_test(input, -1);
    ticks = UINT32_MAX - 100U;
    input->link_up = 0U;
    feed(input);
    for (i = 0U; i < 1500U && status().hold_stopping; i++) follow_stop(input);
    assert(status().state == REMOTE_SERVO_DISABLED && !stop_gate);
    assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_DONE);
    assert(axes[TEST_AXIS].enabled);
    startup_hold = 0U;
    hold_ready = 1U;
    puts("PASS controlled stop: +/-600, independent ramp, recovery/K5 priority, fresh zero, timeout/fault and wrap");
}
static void run_cli_event_tests(nrf_remote_snapshot_t *input)
{
    unsigned before;
    memset(axes, 0, sizeof(axes));
    memset(input, 0, sizeof(*input));
    axes[0].online = axes[1].online = 1U;
    input->left_vertical = 1000U;
    input->link_up = 1U;
    boot_ready = hold_ready = 1U;
    startup_hold = 0U;
    reset_remote();
    feed(input);
    cli_active = 1U;
    cli_abort = CLI_EXIT_NONE;
    before = stops;
    /* CLI占用时旧K1至K4事件也不得退出会话。 */
    for (unsigned key_index = 0U; key_index < 4U; key_index++)
    {
        input->short_counts[key_index]++;
        input->long_counts[key_index]++;
        input->double_counts[key_index]++;
        feed(input);
    }
    assert(cli_abort == CLI_EXIT_NONE && !status().authorized);
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    assert(!status().authorized && cli_abort == CLI_EXIT_NONE);
    input->link_up = 0U;
    ticks += APP_REMOTE_SERVO_LINK_TIMEOUT_MS + 1U;
    feed(input);
    assert(cli_abort == CLI_EXIT_NONE && stops == before);
    input->link_up = 1U;
    assert(app_control_arbiter_submit(APP_CONTROL_OWNER_CLI, app_config_servo_control_axis_mask()));
    app_control_arbiter_service();
    assert(app_control_arbiter_request(APP_CONTROL_OWNER_CLI, app_config_servo_control_axis_mask(), NULL));
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    assert(cli_abort == CLI_EXIT_K1);
    {
        app_control_stop_t stop;
        app_control_arbiter_get_stop(&stop);
        assert(stop.owner == APP_CONTROL_OWNER_CLI && stop.requester == APP_CONTROL_OWNER_REMOTE);
        assert(stop.deceleration_mm_s2 == APP_REMOTE_SERVO_K5_STOP_DECEL_MM_S2);
    }
    app_control_arbiter_release(APP_CONTROL_OWNER_CLI);
    cli_abort = CLI_EXIT_NONE;
    feed(input);
    assert(cli_abort == CLI_EXIT_NONE);
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    cli_active = 0U;
    feed(input);
    assert(!status().authorized);
    if (!app_config_servo_readonly_test())
    {
        arm(input);
        assert(status().authorized);
    }
    puts("PASS CLI events: long press excluded, link loss ignored, new K5 abort, history consumed");
}

static void run_k5_runtime_tests(nrf_remote_snapshot_t *input)
{
    unsigned i, before, before_enable, before_prepare;
    uint32_t generation, timeout;
    if (app_config_servo_readonly_test()) return;
    setup_stop_test(input, 1);
    startup_hold = APP_SERVO_STARTUP_HOLD_ENABLED;
    before = stops;
    before_enable = enables;
    before_prepare = prepares;
    generation = status().authorization_generation;
    assert(!app_remote_servo_k1_latched());
    /* 已运行时旧按键三类事件不能停车或撤销授权。 */
    for (unsigned key_index = 0U; key_index < 4U; key_index++)
    {
        input->short_counts[key_index]++;
        input->long_counts[key_index]++;
        input->double_counts[key_index]++;
        feed(input);
    }
    assert(status().authorized && stops == before);
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    assert(app_remote_servo_k1_latched());
    timeout = status().stop_timeout_ms;
    disable_unconfirmed = 1U;
    for (i = 0U; i < 1500U &&
         status().controlled_stop_state != REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING; i++)
        follow_stop(input);
    assert(status().hold_stopping && !status().k1_hold.valid);
    input->left_vertical = 1000U;
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    assert(!status().authorized && status().stop_timeout_ms == timeout);
    disable_unconfirmed = 0U;
    finish_stop(input);
    feed(input);
    assert(status().k1_hold.valid && !status().estop_latched && !status().authorized);
    assert(!app_remote_servo_k1_latched());
    assert(stops == before && enables == before_enable && prepares == before_prepare);
    /* 停车完成后再次短按不得从有效保持升级为失能急停。 */
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    assert(stops == before && status().k1_hold.valid && !status().authorized);
    input->link_up = 0U;
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    input->link_up = 1U;
    input->recovery_generation++;
    feed(input);
    assert(!status().authorized && status().k1_hold.valid);
    /* 保持后的旧长按、未回中及扰动期间的新长按都不能开放运动。 */
    feed(input);
    input->left_vertical = 1400U;
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    assert(!status().authorized);
    input->left_vertical = 1000U;
    axes[TEST_AXIS].actual_speed_rpm = 1;
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    assert(!status().authorized && status().k1_hold.valid && axes[TEST_AXIS].enabled);
    axes[TEST_AXIS].actual_speed_rpm = 0;
    feed(input);
    assert(!status().authorized);
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    /* 授权申请先进入统一仲裁队列，下一轮服务才完成准备预约。 */
    assert(!status().authorized && status().state == REMOTE_SERVO_ARMING);
    feed(input);
    assert(status().authorized && !status().estop_latched);
    assert(!app_remote_servo_k1_latched());
    assert(status().commanded_rpm == 0 && status().authorization_generation > generation);
    assert(stops == before && enables == before_enable && prepares == before_prepare);
    /* 保持后真实使能/反馈异常，不得继续显示正常保持或被长按绕过。 */
    for (i = 0U; i < 4U; i++)
    {
        setup_stop_test(input, 1);
        input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        feed(input);
        for (unsigned stop_cycle = 0U; stop_cycle < 1500U && status().hold_stopping; stop_cycle++)
            follow_stop(input);
        assert(!status().hold_stopping);
        assert(status().k1_hold.valid);
        if (i == 0U) axes[TEST_AXIS].enabled = 0U;
        else if (i == 1U) axes[TEST_AXIS].fault_age_ms = APP_SERVO_STATUS_MAX_AGE_MS;
        else if (i == 2U) axes[TEST_AXIS].online = 0U;
        else axes[TEST_AXIS].parameter_verified = 0U;
        feed(input);
        assert(!status().k1_hold.valid &&
               status().controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED);
        for (unsigned repeat = 0U; repeat < 3U; repeat++)
        {
            input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
            feed(input);
            assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED);
            assert(!status().authorized && !status().k1_hold.valid);
        }
        arm(input);
        assert(!status().authorized);
    }
    startup_hold = 0U;
    hold_ready = 1U;
    puts("PASS K5 runtime: fresh confirmation, old/new long, disturbance, radio recovery, direct zero takeover, no disable/enable/config, safety failures");
}

static void repeat_k5_stop(nrf_remote_snapshot_t *input, uint8_t completed)
{
    remote_servo_status_t original = status();
    app_control_stop_t stop_before, stop_after;
    unsigned i, before = stops, before_enable = enables, before_prepare = prepares;
    unsigned before_writes = writes[0] + writes[1];
    uint32_t generation = app_control_arbiter_generation();
    app_control_arbiter_get_stop(&stop_before);
    for (i = 0U; i < 20U; i++)
    {
        input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        feed(input);
        assert(!status().authorized && stops == before);
        assert(enables == before_enable && prepares == before_prepare);
        assert(status().stop_generation == original.stop_generation);
        assert(status().stop_timeout_ms == original.stop_timeout_ms);
        assert(status().stop_first_command_ms == original.stop_first_command_ms);
        assert(app_control_arbiter_generation() == generation);
        if (completed)
        {
            assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_DONE);
            assert(status().k1_hold.valid && !status().estop_latched);
            assert(status().stop_complete_ms == original.stop_complete_ms);
            assert(writes[0] + writes[1] == before_writes);
        }
        app_control_arbiter_get_stop(&stop_after);
        assert(stop_after.event == stop_before.event);
        assert(stop_after.started_ms == stop_before.started_ms);
        assert(stop_after.timeout_ms == stop_before.timeout_ms);
        assert(stop_after.completed_ms == stop_before.completed_ms);
    }
}

static void run_k5_repeated_stop_tests(nrf_remote_snapshot_t *input)
{
    int direction;
    unsigned i, before;
    uint32_t generation;
    remote_servo_status_t reference;
    if (app_config_servo_readonly_test()) return;
    for (direction = -1; direction <= 1; direction++)
    {
        setup_stop_test(input, direction);
        before = stops;
        generation = status().authorization_generation;
        input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        feed(input);
        assert(status().hold_stopping);
        repeat_k5_stop(input, 0U);
        for (i = 0U; i < 1500U && status().controlled_stop_state !=
             REMOTE_CONTROLLED_STOP_VERIFYING; i++) follow_stop(input);
        assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_VERIFYING);
        /* 轻微非零反馈延迟零速确认，重复短按仍沿用同一次停车。 */
        axes[TEST_AXIS].actual_speed_rpm = 1;
        repeat_k5_stop(input, 0U);
        axes[0].actual_speed_rpm = axes[1].actual_speed_rpm = 0;
        disable_unconfirmed = 1U;
        for (i = 0U; i < 10U && status().controlled_stop_state !=
             REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING; i++) feed(input);
        assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING);
        repeat_k5_stop(input, 0U);
        /* 停车中长按已消费，完成后不能自动授权。 */
        input->left_vertical = 1000U;
        input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        feed(input);
        disable_unconfirmed = 0U;
        finish_stop(input);
        assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_DONE);
        repeat_k5_stop(input, 1U);
        /* 计数跳变、回绕、重复快照及短长同批均不能产生运动授权。 */
        input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] = UINT32_MAX - 2U;
        input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        feed(input);
        repeat_k5_stop(input, 1U);
        feed(input);
        assert(!status().authorized && status().k1_hold.valid);
        axes[TEST_AXIS].actual_speed_rpm = 1;
        repeat_k5_stop(input, 1U);
        input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        feed(input);
        assert(!status().authorized && status().k1_hold.valid);
        axes[TEST_AXIS].actual_speed_rpm = 0;
        feed(input);
        assert(!status().authorized);
        input->left_vertical = 1400U;
        input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        feed(input);
        assert(!status().authorized);
        input->left_vertical = 1000U;
        feed(input);
        assert(!status().authorized);
        input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        feed(input);
        feed(input);
        assert(status().authorized && status().commanded_rpm == 0);
        assert(status().authorization_generation > generation && stops == before);
    }
    /* 相同反馈轨迹下逐轮追加短按，停车完成时间必须与不重复按键一致。 */
    for (direction = -1; direction <= 1; direction++)
    {
        memset(&reference, 0, sizeof(reference));
        for (unsigned repeated = 0U; repeated < 2U; repeated++)
        {
            setup_stop_test(input, direction);
            before = stops;
            input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
            feed(input);
            for (i = 0U; i < 1500U && status().hold_stopping; i++)
            {
                if (repeated) input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
                follow_stop(input);
            }
            assert(status().controlled_stop_state == REMOTE_CONTROLLED_STOP_DONE);
            assert(stops == before && status().k1_hold.valid);
            if (!repeated) reference = status();
            else
            {
                assert(status().stop_complete_ms == reference.stop_complete_ms);
                assert(status().stop_zero_ms == reference.stop_zero_ms);
                assert(status().stop_timeout_ms == reference.stop_timeout_ms);
            }
        }
    }
    startup_hold = 0U;
    hold_ready = 1U;
    puts("PASS K5 repeated stop: forward/reverse/zero, 20 presses per stage, unchanged transaction/deadline, wrap/jump, disturbance, fresh centered reauthorization");
}

static void run_can_key_mapping_tests(nrf_remote_snapshot_t *input)
{
    uint32_t ticket, generation;
    unsigned before;
    memset(input, 0, sizeof(*input));
    input->link_up = 1U;
    input->left_vertical = 1000U;
    reset_remote();
    feed(input);
    ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_CAN,
        app_config_servo_control_axis_mask());
    assert(ticket != 0U);
    app_control_arbiter_service();
    assert(app_control_arbiter_poll(ticket, &generation) == 1);
    assert(app_control_arbiter_request(APP_CONTROL_OWNER_CAN,
        app_config_servo_control_axis_mask(), &generation));
    before = stops;
    for (unsigned key_index = 0U; key_index < 4U; key_index++)
    {
        input->short_counts[key_index]++;
        input->long_counts[key_index]++;
        input->double_counts[key_index]++;
        feed(input);
    }
    assert(!app_remote_servo_k1_latched() && stops == before);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_CAN);
    input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(input);
    assert(app_remote_servo_k1_latched() && !status().authorized);
    /* CAN自己的停车状态机读取人工锁存；遥控入口不抢写伺服。 */
    assert(stops == before && app_control_arbiter_owner() == APP_CONTROL_OWNER_CAN);
    app_control_arbiter_release(APP_CONTROL_OWNER_CAN);
    feed(input);
    assert(!status().authorized);
    puts("PASS CAN owner mapping: K1-K4 idle, K5 lock, simultaneous long consumed");
}

static void run_k6_watchdog_tests(nrf_remote_snapshot_t *input)
{
    unsigned before, before_writes, before_prepares;
    watchdog_test_requested = 0U;
    cli_active = 0U;
    cli_abort = CLI_EXIT_NONE;
    startup_hold = 0U;
    boot_ready = hold_ready = 1U;
    memset(axes, 0, sizeof(axes));
    axes[0].online = axes[1].online = 1U;
    memset(input, 0, sizeof(*input));
    input->link_up = 1U;
    input->left_vertical = 1000U;
    reset_remote();
    feed(input);
    before = stops;
    /* 旧K2短按仍可累计，但绝不能启动看门狗测试。 */
    input->short_counts[1]++;
    feed(input);
    assert(!watchdog_test_requested && stops == before);
    /* K6长按、双击均不会改变短按累计数，不触发测试。 */
    input->long_counts[APP_REMOTE_WATCHDOG_KEY_INDEX]++;
    input->double_counts[APP_REMOTE_WATCHDOG_KEY_INDEX]++;
    feed(input);
    assert(!watchdog_test_requested && stops == before);
    /* 失联或恢复代次变化时消费旧K6，恢复后不能自动复位。 */
    input->link_up = 0U;
    input->short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX]++;
    feed(input);
    input->link_up = 1U;
    feed(input);
    input->recovery_generation++;
    input->short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX] = UINT32_MAX;
    feed(input);
    feed(input);
    assert(!watchdog_test_requested && stops == before);
    /* 本地快照已过期，即使link_up残留为1也不触发。 */
    input->age_ms = APP_REMOTE_SERVO_LINK_TIMEOUT_MS;
    input->short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX] = 0U;
    feed(input);
    input->age_ms = 0U;
    feed(input);
    assert(!watchdog_test_requested && stops == before);

    /* CLI期间也必须响应远程K6，不被CLI提前返回分支遮蔽。 */
    cli_active = 1U;
    input->short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX]++;
    feed(input);
    if (APP_WDG_TEST_K6_SHORT_ENABLED)
    {
        assert(watchdog_test_requested && stops == before + 1U);
        assert(cli_abort == CLI_EXIT_SAFETY);
        assert(!status().authorized && !status().batch_active);
        before_writes = writes[0] + writes[1];
        before_prepares = prepares;
        cli_active = 0U;
        for (unsigned i = 0U; i < 10U; i++)
        {
            input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
            input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
            input->short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX]++;
            /* 新斜坡在低速首周期可能仍量化为0，推进到足够产生首个rpm命令。 */
            ticks += 100U;
            feed(input);
        }
        assert(stops == before + 1U);
        assert(!status().authorized && !status().batch_active);
        assert(writes[0] + writes[1] == before_writes && prepares == before_prepares);
        /* 模拟复位后的新启动，累计计数回绕仍只触发一次新短按。 */
        watchdog_test_requested = 0U;
        reset_remote();
        input->recovery_generation++;
        input->short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX] = UINT32_MAX;
        feed(input);
        assert(!watchdog_test_requested);
        input->short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX] = 0U;
        feed(input);
        assert(watchdog_test_requested && stops == before + 2U);
        feed(input);
        assert(stops == before + 2U);
        if (!app_config_servo_readonly_test())
        {
            /* 遥控普通速度批次尚未应答时触发，迟到成功应答不能续发旧批次。 */
            watchdog_test_requested = cli_active = 0U;
            memset(input, 0, sizeof(*input));
            memset(axes, 0, sizeof(axes));
            axes[0].online = axes[1].online = 1U;
            input->link_up = 1U;
            input->left_vertical = 1000U;
            reset_remote();
            feed(input);
            arm(input);
            assert(status().authorized);
            hold_response = 1U;
            input->left_vertical = 2000U;
            /* 新斜坡在低速首周期可能仍量化为0，推进到足够产生首个rpm命令。 */
            ticks += 100U;
            feed(input);
            /* A changed target starts a fresh clock; no retroactive 100ms step. */
            for (unsigned wait = 0; wait < 100U && writes[0] + writes[1] == 0U; ++wait) {
                ticks += 1U; feed(input);
            }
            before_writes = writes[0] + writes[1];
            assert(before_writes != 0U);
            input->short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX]++;
            feed(input);
            assert(watchdog_test_requested && !status().authorized);
            for (unsigned axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
            {
                axes[axis].speed_write_completed = axes[axis].speed_write_sequence;
                axes[axis].speed_write_result = SERVO_OK;
                axes[axis].busy = 0U;
            }
            input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
            ticks += 20U;
            feed(input);
            assert(!status().authorized && !status().batch_active);
            assert(writes[0] + writes[1] == before_writes);
            hold_response = 0U;
            /* 已完成的K5使能保持也必须被真实看门狗测试安全抢占。 */
            watchdog_test_requested = 0U;
            setup_stop_test(input, 1);
            input->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
            feed(input);
            for (unsigned i = 0U; i < 1500U && status().hold_stopping; i++)
                follow_stop(input);
            assert(status().k1_hold.valid);
            before = stops;
            input->short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX]++;
            feed(input);
            assert(watchdog_test_requested && stops == before + 1U);
            assert(!status().k1_hold.valid && !status().authorized);
            input->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
            feed(input);
            assert(!status().authorized);
        }
    }
    else
    {
        assert(!watchdog_test_requested && stops == before);
        assert(cli_abort == CLI_EXIT_NONE);
        /* 关闭开关仍消费K6，重复快照不产生控制副作用。 */
        feed(input);
        assert(stops == before);
    }
    watchdog_test_requested = cli_active = 0U;
    puts("PASS remote K6 watchdog: macro, duplicate/wrap/stale/recovery, CLI and reauthorization gate");
}

static void test_communication_recovery_key(void)
{
    nrf_remote_snapshot_t input;
    memset(&input,0,sizeof(input)); reset_remote();
    input.link_up=1U;
    input.left_vertical=input.left_horizontal=input.right_vertical=input.right_horizontal=1000U;
    test_recovery.state=SERVO_RECOVERY_WAIT_HEALTH; test_recovery.generation=10U;
    input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++; feed(&input);
    test_recovery.state=SERVO_RECOVERY_READY; feed(&input); feed(&input);
    assert(!recovery_requests && !status().authorized);
    input.left_horizontal=1500U; input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++; feed(&input);
    assert(!recovery_requests);
    input.left_horizontal=1000U; feed(&input); assert(!recovery_requests);
    input.link_up=0U; input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++; feed(&input);
    input.link_up=1U; input.recovery_generation++; feed(&input); assert(!recovery_requests);
    input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++; feed(&input);
    assert(recovery_requests==1U && test_recovery.source==1U && !status().authorized);
    feed(&input); assert(recovery_requests==1U);
    test_recovery.state=SERVO_RECOVERY_DONE;
    app_remote_servo_recovery_reset(); feed(&input); assert(!status().authorized);
    memset(&test_recovery,0,sizeof(test_recovery)); reset_remote();
    puts("PASS communication K5: READY edge, all sticks centered, live link, consume old/reconnect events, reset only");
}

int main(void)
{
    test_communication_recovery_key();
#ifdef TEST_COMMUNICATION_KEY_ONLY
    return 0;
#endif
    nrf_remote_snapshot_t input;
    unsigned index;
    memset(&input, 0, sizeof(input));
    axes[SERVO_AXIS_LEFT].online = 1;
    axes[SERVO_AXIS_RIGHT].online = 1;
    input.link_up = 1;
    input.left_vertical = 2000;
    reset_remote();
    feed(&input);
#ifdef TEST_K6_WATCHDOG_ONLY
    run_k6_watchdog_tests(&input);
    return 0;
#endif
    assert(status().state == REMOTE_SERVO_DISABLED && written == 0);
    /* K1至K4仍可上传，但所有事件均不能停车、授权或触发看门狗。 */
    input.left_vertical = 1000U;
    for (unsigned key_index = 0U; key_index < 4U; key_index++)
    {
        input.short_counts[key_index]++;
        input.long_counts[key_index]++;
        input.double_counts[key_index]++;
        feed(&input);
    }
    assert(!status().authorized && !status().estop_latched);
    assert(!watchdog_test_requested && written == 0 && stops == 0U);
    input.double_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(&input);
    assert(!status().authorized && !status().estop_latched);
    input.left_vertical = 2000U;
    /* 非中位长按不能授权，旧事件回中后不能重新执行。 */
    input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(&input);
    input.left_vertical = 1000;
    feed(&input);
    assert(!status().authorized);
    /* 上电设零门禁拒绝授权，完成后不能重放已消费的长按事件。 */
    boot_ready = 0U;
    input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(&input);
    assert(!status().authorized && prepares == 0U);
    boot_ready = 1U;
    feed(&input);
    assert(!status().authorized && prepares == 0U);
    input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(&input);
    for (index = 0; index < 8; index++) feed(&input);
    if (app_config_servo_readonly_test())
    {
        assert(!status().authorized && status().state == REMOTE_SERVO_DISABLED);
        assert(prepares == 0 && stops == 0);
        assert(writes[0] == 0 && writes[1] == 0);
        run_k6_watchdog_tests(&input);
        puts("Readonly remote authorization tests passed");
        return 0;
    }
    assert(status().authorized && prepares == app_config_servo_control_axis_count());
    input.left_vertical = 2000;
    /*
     * 双轴共享USART2时，一个斜坡步进需要依次完成两个地址的事务，
     * 因此等待轮次约为单轴的两倍。
     */
    for (index = 0; index < 4000U; index++)
    {
        ticks += 10;
        feed(&input);
    }
    assert(written == APP_REMOTE_SERVO_MAX_RPM);
    input.left_vertical = 0;
    for (index = 0; index < 7000U; index++)
    {
        ticks += 10;
        feed(&input);
    }
    assert(written == APP_REMOTE_SERVO_MIN_RPM);
    input.left_vertical = 940;
    ticks += 10;
    feed(&input);
    /* 目标已进入死区；底层写入值受速度斜坡限制，不要求本次立即归零。 */
    assert(status().target_rpm == 0);
    input.left_vertical = 1060;
    feed(&input);
    assert(status().target_rpm == 0);
    input.left_vertical = 939;
    feed(&input);
    /* 死区外一个码按当前量程量化，不固定假定为零。 */
    assert(status().target_rpm ==
           -(int)(APP_REMOTE_SERVO_MAX_RPM / APP_REMOTE_SERVO_NEUTRAL_LOW));
    input.left_vertical = 1061;
    feed(&input);
    assert(status().target_rpm ==
           (int)(APP_REMOTE_SERVO_MAX_RPM / (2000U - APP_REMOTE_SERVO_NEUTRAL_HIGH)));
    input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(&input);
    assert(status().state == REMOTE_SERVO_STOPPING && !status().authorized);
    finish_stop(&input);
    assert(status().state == REMOTE_SERVO_ESTOP &&
           axes[TEST_AXIS].enabled);
    feed(&input);
    assert(status().state == REMOTE_SERVO_ESTOP);
    input.left_vertical = 1000;
    input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(&input);
    for (index = 0; index < 8; index++) feed(&input);
    assert(status().authorized);
    /* 即使接收任务停止发布，快照年龄仍随系统时钟增长。 */
    ticks += 300;
    app_remote_servo_service();
    assert(status().state == REMOTE_SERVO_STOPPING);
    input.link_up = 0U;
    finish_stop(&input);
    assert(status().state == REMOTE_SERVO_DISABLED);
    assert(status().link_loss_estop_count == 1);
    input.link_up = 1U;
    feed(&input);
    assert(status().state == REMOTE_SERVO_DISABLED);
    input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(&input);
    for (index = 0; index < 8; index++) feed(&input);
    {
        unsigned before_stop = stops;
        app_remote_servo_request_estop();
        feed(&input);
        assert(status().state == REMOTE_SERVO_ESTOP && stops == before_stop + 1U);
    }
    for (index = 0; index < SERVO_AXIS_COUNT; index++)
        if (!app_config_servo_axis_controllable(index)) assert(writes[index] == 0);
    /* 异步事务失败不提前推进斜坡，双轴仅重试尚未成功的轴。 */
    input.left_vertical = 1000;
    input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(&input);
    for (index = 0; index < 8; index++) feed(&input);
    assert(status().authorized);
    hold_response = 1;
    memset(writes, 0, sizeof(writes));
    input.left_vertical = 2000;
    ticks += 10;
    feed(&input);
    assert(status().commanded_rpm == 0);
    if (app_config_servo_control_axis_count() == 2)
    {
        /* Wait for the first quantized batch after the new-target clock boundary. */
        for (index = 0U; index < 100U && writes[0] == 0U; ++index) {
            ticks += 1U; feed(&input);
        }
        assert(writes[0] == 1U);
        uint32_t pending_batch_id;
        int16_t pending_batch_rpm;
        int16_t pending_left, pending_right;
        ticks += 10;
        feed(&input);
        axes[SERVO_AXIS_LEFT].busy = 0;
        axes[SERVO_AXIS_LEFT].speed_write_completed =
            axes[SERVO_AXIS_LEFT].speed_write_sequence;
        axes[SERVO_AXIS_LEFT].speed_write_result = SERVO_OK;
        axes[SERVO_AXIS_RIGHT].busy = 0;
        axes[SERVO_AXIS_RIGHT].speed_write_completed =
            axes[SERVO_AXIS_RIGHT].speed_write_sequence;
        axes[SERVO_AXIS_RIGHT].speed_write_result = SERVO_NOT_READY;
        ticks += 10;
        feed(&input);
        for (index = 0U; index < 100U && writes[SERVO_AXIS_RIGHT] == 0U; index++)
        {
            ticks += 10;
            feed(&input);
        }
        assert(writes[SERVO_AXIS_RIGHT] > 0U);
        axes[SERVO_AXIS_RIGHT].busy = 0;
        axes[SERVO_AXIS_RIGHT].speed_write_completed =
            axes[SERVO_AXIS_RIGHT].speed_write_sequence;
        axes[SERVO_AXIS_RIGHT].speed_write_result = SERVO_NOT_READY;
        ticks += 10;
        feed(&input);
        assert(writes[SERVO_AXIS_LEFT] == 1);
        assert(writes[SERVO_AXIS_RIGHT] >= 2);
        assert(status().authorized && status().commanded_rpm == 0);
        assert(status().left_commanded_rpm >= 0 &&
               status().left_commanded_rpm <= 20);
        assert(status().right_commanded_rpm == 0);
        assert(status().batch_active || status().batch_success_mask == 1);
        pending_batch_id = status().batch_id;
        pending_batch_rpm = status().batch_base_rpm;
        pending_left = APP_REMOTE_SERVO_SYNC_ENABLED ? status().batch_command[0] : pending_batch_rpm;
        pending_right = APP_REMOTE_SERVO_SYNC_ENABLED ? status().batch_command[1] : pending_batch_rpm;
        /* 批次尚未完成时改变摇杆，不得改变右轴重试的批次目标。 */
        input.left_vertical = 0;
        axes[SERVO_AXIS_RIGHT].busy = 0;
        axes[SERVO_AXIS_RIGHT].speed_write_completed =
            axes[SERVO_AXIS_RIGHT].speed_write_sequence;
        axes[SERVO_AXIS_RIGHT].speed_write_result = SERVO_OK;
        ticks += 10;
        feed(&input);
        for (index = 0U; index < 100U && status().commanded_rpm == 0; index++)
        {
            axes[SERVO_AXIS_RIGHT].busy = 0;
            axes[SERVO_AXIS_RIGHT].speed_write_completed =
                axes[SERVO_AXIS_RIGHT].speed_write_sequence;
            axes[SERVO_AXIS_RIGHT].speed_write_result = SERVO_OK;
            ticks += 10;
            feed(&input);
        }
        assert(status().commanded_rpm >= 0 && status().commanded_rpm <= 20);
        /* 完成应答的同轮可启动新批次，不以全局空闲作为原批次完成条件。 */
        assert(status().commanded_rpm == pending_batch_rpm);
        assert(!status().batch_active || !APP_REMOTE_SERVO_SYNC_ENABLED ||
            status().batch_id != pending_batch_id);
        assert(status().left_commanded_rpm == pending_left ||
            (status().batch_active &&
             (!APP_REMOTE_SERVO_SYNC_ENABLED || status().batch_id != pending_batch_id) &&
             status().left_commanded_rpm == (APP_REMOTE_SERVO_SYNC_ENABLED ?
                 status().batch_command[0] : status().batch_base_rpm)));
        assert(status().right_commanded_rpm == pending_right ||
            (status().batch_active &&
             (!APP_REMOTE_SERVO_SYNC_ENABLED || status().batch_id != pending_batch_id) &&
             status().right_commanded_rpm == (APP_REMOTE_SERVO_SYNC_ENABLED ?
                 status().batch_command[1] : status().batch_base_rpm)));
    }
    else
    {
        axes[TEST_AXIS].busy = 0;
        axes[TEST_AXIS].speed_write_completed = axes[TEST_AXIS].speed_write_sequence;
        axes[TEST_AXIS].speed_write_result = SERVO_NOT_READY;
        ticks += 10;
        feed(&input);
        assert(status().authorized && status().commanded_rpm == 0);
        axes[TEST_AXIS].busy = 0;
        axes[TEST_AXIS].speed_write_completed = axes[TEST_AXIS].speed_write_sequence;
        axes[TEST_AXIS].speed_write_result = SERVO_OK;
        feed(&input);
        assert(status().commanded_rpm >= 0 && status().commanded_rpm <= 20);
    }
    /* 任一参与轴真实故障必须撤销所有参与轴的授权和使能。 */
    axes[TEST_AXIS].fault = 1;
    axes[TEST_AXIS].fault_code = 0x1234;
    feed(&input);
    assert(status().state == REMOTE_SERVO_ESTOP);
    for (index = 0; index < SERVO_AXIS_COUNT; index++)
        if (app_config_servo_axis_controllable(index)) assert(!axes[index].enabled);
    assert((status().stop_axis_mask & (1U << TEST_AXIS)) != 0U);
    hold_response = 0;
    /* 每个参与轴分别验证配置失败、使能失败、状态过期及真实故障联停。 */
    for (index = 0; index < SERVO_AXIS_COUNT; index++)
    {
        unsigned i;
        if (!app_config_servo_axis_controllable(index)) continue;
        memset(axes, 0, sizeof(axes));
        axes[0].online = axes[1].online = 1;
        reset_remote();
        memset(input.short_counts, 0, sizeof(input.short_counts));
        memset(input.long_counts, 0, sizeof(input.long_counts));
        prepare_fail_mask = (uint8_t)(1U << index);
        arm(&input);
        assert(status().state == REMOTE_SERVO_ESTOP && !status().authorized);
        assert(status().stop_axis_mask == (1U << index));
        prepare_fail_mask = 0;
        enable_fail_mask = (uint8_t)(1U << index);
        arm(&input);
        assert(status().state == REMOTE_SERVO_ESTOP && !status().authorized);
        assert(!axes[0].enabled && !axes[1].enabled);
        assert(status().stop_axis_mask == (1U << index));
        enable_fail_mask = 0;
        arm(&input);
        assert(status().authorized);
        axes[index].speed_age_ms = 500;
        feed(&input);
        assert(status().state == REMOTE_SERVO_ESTOP);
        assert(status().stop_axis_mask == (1U << index));
        assert(!axes[0].enabled && !axes[1].enabled);
        axes[index].speed_age_ms = 0;
        feed(&input);
        assert(!status().authorized);
        arm(&input);
        assert(status().authorized);
        axes[index].fault = 1;
        axes[index].fault_code = 0x1234;
        feed(&input);
        assert(status().stop_axis_mask == (1U << index));
        for (i = 0; i < SERVO_AXIS_COUNT; i++) assert(!axes[i].enabled);
    }
    run_sync_integration(&input);
    run_startup_hold_integration(&input);
    run_radio_recovery_integration(&input);
    run_controlled_stop_tests(&input);
    run_cli_event_tests(&input);
    run_k5_runtime_tests(&input);
    run_k5_repeated_stop_tests(&input);
    run_can_key_mapping_tests(&input);
    run_k6_watchdog_tests(&input);
    setup_stop_test(&input, 1);
    axes[0].actual_speed_rpm = axes[1].actual_speed_rpm = 0;
    input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    feed(&input); finish_stop(&input);
    assert(!status().authorized && !status().estop_latched);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    assert(app_control_arbiter_submit(APP_CONTROL_OWNER_HMI, app_config_servo_control_axis_mask()));
    app_control_arbiter_service();
    assert(app_control_arbiter_request(APP_CONTROL_OWNER_HMI, app_config_servo_control_axis_mask(), NULL));
    {
        unsigned before = stops, before_writes = writes[0] + writes[1];
        input.link_up = 0U; feed(&input);
        input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++; feed(&input);
        input.link_up = 1U; feed(&input);
        assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_HMI);
        assert(stops == before && writes[0] + writes[1] == before_writes);
        input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++; feed(&input);
        {
            app_control_stop_t stop;
            assert(app_control_arbiter_take_stop(APP_CONTROL_OWNER_HMI, &stop));
            assert(stop.deceleration_mm_s2 == APP_REMOTE_SERVO_K5_STOP_DECEL_MM_S2);
        }
        assert(stops == before && writes[0] + writes[1] == before_writes);
    }
    puts("Remote servo tests passed, including HMI ownership isolation");
    {
        unsigned attempt;
        setup_stop_test(&input, 1);
        axes[0].actual_speed_rpm = axes[1].actual_speed_rpm = 0;
        reset_remote();
        input.left_vertical = 1000U;
        input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++; feed(&input);
        assert(status().state == REMOTE_SERVO_ARMING);
        app_control_arbiter_service();
        assert(app_control_arbiter_submit_stop(2U, 500U, 99U, 0x10U));
        for (attempt = 0U; attempt < 20U && status().state == REMOTE_SERVO_ARMING; ++attempt)
            feed(&input);
        assert(!status().authorized && status().hold_stopping);
        finish_stop(&input);
        {
            app_control_stop_t stopped; app_control_arbiter_get_stop(&stopped);
            assert(stopped.result == 3U && stopped.request_sequence == 99U);
            assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
        }
        puts("PASS remote preparation cancellation: no authorization, cleanup, zero enable, preserve request");
    }
    run_degraded_output_test(&input);
    /* 同样的500mm/s²必须按原因区分失联与人工停车，外部速率不可替换为K5宏。 */
    for (unsigned reason = 4U; reason <= 5U; ++reason)
    {
        setup_stop_test(&input, 1);
        assert(app_control_arbiter_submit_stop((uint16_t)reason, 500U, 101U, 0x10U));
        feed(&input);
        assert(status().stop_deceleration_mps2 == 0.50f);
        assert(status().controlled_stop_reason == (reason == 4U ? REMOTE_STOP_LINK : REMOTE_STOP_K1));
        assert(status().estop_latched == (reason == 5U));
        finish_stop(&input);
    }
    return 0;
}
