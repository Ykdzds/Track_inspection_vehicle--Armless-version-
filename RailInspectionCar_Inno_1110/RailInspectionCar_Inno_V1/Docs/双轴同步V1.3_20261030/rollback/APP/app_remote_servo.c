#include "app_remote_servo.h"
#include "app_cli_servo_session.h"
#include "app_control_arbiter.h"

#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "app_motion_ramp.h"
#include "app_normal_ramp.h"
#include "app_health.h"
#include "motion_control.h"
#include "servo_debug_control.h"
#include <string.h>

static uint8_t s_k5_idle_verify;
static TickType_t s_k5_idle_verify_tick;
static uint32_t s_k5_idle_verify_event;

uint8_t app_remote_servo_k5_verify_pending(void)
{
    return s_k5_idle_verify;
}

static int8_t app_remote_servo_hmi_idle_k5(const app_control_stop_t *stop)
{
    servo_snapshot_t axes[2];
    uint8_t axis, waiting = 0U;
    uint8_t mask = app_config_servo_control_axis_mask();
    if (!mask || !stop->event || stop->result != 3U || stop->error ||
        stop->zero_mask != mask || stop->enabled_mask != mask ||
        app_control_arbiter_lock_reason() != APP_CONTROL_LOCK_NONE ||
        app_control_arbiter_preparing() || app_config_servo_readonly_test() ||
        !app_config_motion_hardware_ready() || !app_config_servo_bus_ready() ||
        !aimtor_boot_zero_ready() || !aimtor_startup_hold_ready())
        return -1;
    if (!aimtor_get_snapshot_pair(&axes[0], &axes[1])) return 0;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
    {
        const servo_snapshot_t *feedback = &axes[axis];
        if (!(mask & (1U << axis))) continue;
        if (feedback->feedback_degraded || !feedback->online || feedback->fault || feedback->fault_code ||
            feedback->stop_latched || !feedback->enabled ||
            feedback->consecutive_failures >= APP_REMOTE_SERVO_COMM_FAILURE_LIMIT ||
            !feedback->parameter_verified ||
            feedback->control_mode != SERVO_CONTROL_RS485_VELOCITY ||
            feedback->actual_speed_rpm != 0 ||
            (feedback->enable_feedback_valid && !feedback->enable_feedback))
            return -1;
        if (feedback->busy || !feedback->speed_valid || !feedback->fault_valid ||
            !feedback->enable_feedback_valid ||
            feedback->speed_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
            feedback->fault_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
            feedback->enable_feedback_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS)
            waiting = 1U;
    }
    return waiting ? 0 : 1;
}

static volatile remote_servo_state_t s_state;
static uint8_t s_authorized;
static uint32_t s_acquire_ticket;
static uint8_t s_prepare_started;
static uint8_t s_estop_latched;
static uint8_t s_link_valid;
static uint16_t s_lv;
static int16_t s_target_rpm;
static int16_t s_commanded_rpm;
static uint32_t s_last_frame_age_ms;
static uint32_t s_arm_count;
static uint32_t s_estop_count;
static uint32_t s_link_loss_estop_count;
static servo_result_t s_last_result;
static TickType_t s_last_speed_update_tick;
static app_motion_ramp_t s_motion_ramp;
static app_normal_ramp_clock_t s_normal_clock;
static uint8_t s_normal_reversing;
static uint32_t s_recovery_ready_generation;
static uint8_t s_recovery_ready_seen;
static nrf_remote_snapshot_t s_input;
static TickType_t s_input_tick;
static uint8_t s_stop_requested;
static uint32_t s_short_count;
static uint32_t s_long_count;
static uint8_t s_idle_acquire_pending;
static TickType_t s_idle_acquire_tick;
static uint32_t s_idle_acquire_generation;
static uint32_t s_k6_short_count;
static uint32_t s_k6_radio_generation;
static uint32_t s_radio_generation;
static uint32_t s_can_k5_event;
static uint8_t s_can_k5_axes;
static TickType_t s_arm_tick;
static remote_stop_reason_t s_stop_reason;
static servo_snapshot_t s_stop_snapshot;
static uint32_t s_stop_link_age_ms;
static remote_comm_state_t s_comm_state;
static uint16_t s_comm_fail_count;
static uint16_t s_speed_submit_fail_count;
static TickType_t s_last_comm_ok_tick;
static uint32_t s_pending_speed_sequence[SERVO_AXIS_COUNT];
static int16_t s_pending_speed_rpm[SERVO_AXIS_COUNT];
static int16_t s_axis_commanded_rpm[SERVO_AXIS_COUNT];
static uint8_t s_speed_axis_turn;
static uint8_t s_speed_batch_active;
static uint8_t s_speed_batch_success_mask;
static int16_t s_speed_batch_target_rpm;
static TickType_t s_speed_batch_tick;
static TickType_t s_batch_response_tick[SERVO_AXIS_COUNT];
static uint32_t s_batch_response_gap_ms;
static uint8_t s_stop_axis_mask;
static uint8_t s_failure_axis_mask;
static uint8_t s_arm_action_axis;
static remote_stop_reason_t s_arm_action_reason;
static remote_sync_status_t s_sync;
static int16_t s_batch_command[SERVO_AXIS_COUNT];
static int16_t s_batch_correction;
static uint8_t s_batch_required_mask;
static uint8_t s_confirmed_mask;
static uint8_t s_batch_written_mask;
static uint8_t s_batch_gap_valid;
static uint32_t s_batch_id;
static uint32_t s_authorization_generation;
static uint32_t s_batch_generation;
static uint32_t s_batch_sample_sequence[SERVO_AXIS_COUNT];
static uint8_t s_reverse_wait;
static TickType_t s_reverse_tick;
static int8_t s_motion_direction;
static uint8_t s_hold_stopping;
static TickType_t s_hold_stop_tick;
static uint32_t s_hold_stop_sequence[SERVO_AXIS_COUNT];
static uint8_t s_hold_step_active;
static uint8_t s_hold_step_mask;
static int16_t s_hold_step_base;
static int16_t s_hold_step_command[SERVO_AXIS_COUNT];
static uint8_t s_hold_zero_written_mask;
static uint8_t s_hold_zero_count[SERVO_AXIS_COUNT];
static remote_controlled_stop_state_t s_controlled_stop_state;
static remote_stop_reason_t s_controlled_stop_reason;
static float s_stop_deceleration;
static float s_stop_remaining_rpm;
static TickType_t s_stop_ramp_tick;
static TickType_t s_stop_clock_tick;
static uint8_t s_stop_ramp_anchored;
static uint8_t s_stop_keep_enabled;
static uint32_t s_stop_timeout_ms;
static uint32_t s_stop_generation;
static uint32_t s_stop_first_command_ms;
static uint32_t s_stop_zero_ms;
static uint32_t s_stop_complete_ms;
static uint8_t s_stop_first_command_valid;
static uint8_t s_stop_zero_time_valid;
static uint8_t s_stop_complete_time_valid;
static uint8_t s_arm_cancel;
static remote_stop_reason_t s_arm_cancel_reason;
static float s_arm_cancel_deceleration;

static void app_remote_servo_sync_cancel(void);
static uint8_t app_remote_servo_axis_selected(servo_axis_t axis);

static uint32_t app_remote_servo_tick_ms(TickType_t elapsed)
{
    return (uint32_t)((uint64_t)elapsed * 1000U / configTICK_RATE_HZ);
}

static int32_t app_remote_servo_abs_rpm(int16_t rpm)
{
    return rpm < 0 ? -(int32_t)rpm : (int32_t)rpm;
}

static float app_remote_servo_stop_rate(void)
{
    return s_stop_deceleration * 60.0f * APP_GEAR_REDUCTION_RATIO /
        (3.14159265358979323846f * APP_WHEEL_DIAMETER_M);
}

static uint8_t app_remote_servo_stop_snapshot_ready(const servo_snapshot_t *snapshot)
{
    return snapshot->online && snapshot->enabled &&
        snapshot->parameter_verified && !snapshot->stop_latched &&
        snapshot->speed_valid && snapshot->fault_valid && !snapshot->fault &&
        !snapshot->fault_code &&
        snapshot->control_mode == SERVO_CONTROL_RS485_VELOCITY &&
        snapshot->speed_age_ms < APP_SERVO_STATUS_MAX_AGE_MS &&
        snapshot->fault_age_ms < APP_SERVO_STATUS_MAX_AGE_MS &&
        snapshot->online;
}

void app_remote_servo_controlled_stop(uint8_t emergency)
{
    (void)app_control_arbiter_submit_stop(emergency ? 5U : 4U,
        emergency ? APP_REMOTE_SERVO_K5_STOP_DECEL_MM_S2 : APP_REMOTE_SERVO_LINK_STOP_DECEL_MM_S2,
        0U, 0U);
}

static uint8_t app_remote_servo_begin_controlled_stop_rate(remote_stop_reason_t reason, float deceleration)
{
    servo_snapshot_t left;
    servo_snapshot_t right;
    const servo_snapshot_t *snapshot;
    uint8_t axis;
    int32_t maximum = 0;
    float rate;
    float timeout_ms;

    rate = deceleration;
    if (!(rate > 0.0f) || !(rate < 1000000.0f)) return 0U;
    if (s_state == REMOTE_SERVO_ARMING && s_arm_cancel != 2U)
    {
        if (!s_arm_cancel || reason == REMOTE_STOP_K1) s_arm_cancel_reason = reason;
        if (!s_arm_cancel || deceleration > s_arm_cancel_deceleration)
            s_arm_cancel_deceleration = deceleration;
        s_arm_cancel = 1U; s_target_rpm = 0;
        {
            app_control_stop_t pending;
            app_control_arbiter_get_stop(&pending);
            if (pending.result != 2U || pending.owner != APP_CONTROL_OWNER_REMOTE ||
                pending.deceleration_mm_s2 < (uint16_t)(deceleration * 1000.0f + 0.5f))
                (void)app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_REMOTE,
                    reason == REMOTE_STOP_K1 ? 5U : 4U,
                    (uint16_t)(deceleration * 1000.0f + 0.5f), 0U, 0U);
        }
        return 1U;
    }
    if (s_hold_stopping)
    {
        /* K5升级结束动作，但不减弱已有制动或重置停车期限。 */
        if (reason == REMOTE_STOP_K1)
        {
            s_controlled_stop_reason = reason;
            s_stop_reason = reason;
            s_stop_keep_enabled = 1U;
            s_estop_latched = 1U;
        }
        /* 外部停车沿用仲裁事件的物理减速度；重复请求不得减弱制动。 */
        if (deceleration > s_stop_deceleration)
        {
            s_stop_deceleration = deceleration;
            (void)app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_REMOTE,
                reason == REMOTE_STOP_K1 ? 5U : 4U,
                (uint16_t)(deceleration * 1000.0f + 0.5f), 0U, 0x10U);
        }
        return 1U;
    }
    if (!((s_authorized &&
           (s_state == REMOTE_SERVO_ARMED || s_state == REMOTE_SERVO_RUNNING)) ||
          (reason == REMOTE_STOP_K1 && s_state == REMOTE_SERVO_DISABLED) ||
          (s_arm_cancel == 2U && s_state == REMOTE_SERVO_ARMING)) ||
        app_config_servo_readonly_test() ||
        !aimtor_boot_zero_ready() || !aimtor_startup_hold_ready() ||
        !app_config_motion_hardware_ready() ||
        !aimtor_get_snapshot_pair(&left, &right))
        return 0U;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_remote_servo_axis_selected((servo_axis_t)axis)) continue;
        snapshot = axis == SERVO_AXIS_LEFT ? &left : &right;
        if (!app_remote_servo_stop_snapshot_ready(snapshot) ||
            (snapshot->busy && !s_pending_speed_sequence[axis]) ||
            (!s_authorized && snapshot->actual_speed_rpm != 0))
            return 0U;
        if (app_remote_servo_abs_rpm(snapshot->actual_speed_rpm) > maximum)
            maximum = app_remote_servo_abs_rpm(snapshot->actual_speed_rpm);
        if (app_remote_servo_abs_rpm(s_axis_commanded_rpm[axis]) > maximum)
            maximum = app_remote_servo_abs_rpm(s_axis_commanded_rpm[axis]);
        if (s_pending_speed_sequence[axis] &&
            app_remote_servo_abs_rpm(s_pending_speed_rpm[axis]) > maximum)
            maximum = app_remote_servo_abs_rpm(s_pending_speed_rpm[axis]);
    }
    {
        app_control_stop_t stop;
        app_control_arbiter_get_stop(&stop);
        if (stop.result != 2U || stop.owner != APP_CONTROL_OWNER_REMOTE)
            (void)app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_REMOTE,
                reason == REMOTE_STOP_K1 ? 5U : 4U,
                (uint16_t)(deceleration * 1000.0f + 0.5f), 0U, 0U);
    }
    s_stop_deceleration = deceleration;
    rate = app_remote_servo_stop_rate();
    /* 非有限配置或异常反馈不能产生无界停车期限。 */
    if (!(rate > 0.0f) || !(rate < 1000000.0f))
        return 0U;
    timeout_ms = (float)maximum * 1000.0f / rate + 0.999f;
    /* 期限保持在无符号时间差可判定的半周期内，拒绝过小减速度。 */
    if (!(timeout_ms < 2147480000.0f - APP_REMOTE_SERVO_STOP_CONFIRM_MARGIN_MS))
        return 0U;
    s_stop_timeout_ms = (uint32_t)timeout_ms +
        APP_REMOTE_SERVO_STOP_CONFIRM_MARGIN_MS;
    if (s_stop_timeout_ms < 2000U) s_stop_timeout_ms = 2000U;
    s_authorized = 0U;
    aimtor_set_motion_authorized(false);
    aimtor_set_remote_stop_active(true);
    s_target_rpm = 0;
    s_hold_stopping = 1U;
    s_normal_reversing = 0U;
    app_motion_ramp_clear(&s_motion_ramp);
    app_normal_ramp_boundary(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount()));
    s_state = REMOTE_SERVO_STOPPING;
    s_controlled_stop_state = REMOTE_CONTROLLED_STOP_DECELERATING;
    s_controlled_stop_reason = reason;
    s_stop_reason = reason;
    s_stop_keep_enabled = 1U;
    if (reason == REMOTE_STOP_LINK) s_link_loss_estop_count++;
    if (reason == REMOTE_STOP_K1) s_estop_latched = 1U;
    s_hold_step_active = 0U;
    s_hold_step_mask = 0U;
    app_control_arbiter_stop_timeout(APP_CONTROL_OWNER_REMOTE, s_stop_timeout_ms);
    s_hold_stop_tick = xTaskGetTickCount();
    s_stop_ramp_tick = s_hold_stop_tick;
    s_stop_ramp_anchored = 0U;
    s_hold_zero_written_mask = 0U;
    s_stop_first_command_valid = 0U;
    s_stop_zero_time_valid = 0U;
    s_stop_complete_time_valid = 0U;
    s_stop_first_command_ms = s_stop_zero_ms = s_stop_complete_ms = 0U;
    memset(s_hold_zero_count, 0, sizeof(s_hold_zero_count));
    memset(s_hold_stop_sequence, 0, sizeof(s_hold_stop_sequence));
    memset(s_hold_step_command, 0, sizeof(s_hold_step_command));
    /* 已提交事务先收尾，不再补发旧批次中尚未提交的目标。 */
    s_speed_batch_active = 0U;
    s_speed_batch_success_mask = 0U;
    app_remote_servo_sync_cancel();
    s_stop_generation = s_authorization_generation;
    (void)motion_stop(MOTION_STOP_IMMEDIATE);
    return 1U;
}

static uint8_t app_remote_servo_begin_controlled_stop(remote_stop_reason_t reason)
{
    return app_remote_servo_begin_controlled_stop_rate(reason,
        reason == REMOTE_STOP_K1 ? APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 :
                                  APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2);
}

static uint8_t app_remote_servo_sync_enabled(void)
{
    return APP_REMOTE_SERVO_SYNC_ENABLED &&
        app_config_servo_control_axis_count() == 2U &&
        !app_config_servo_readonly_test();
}

static void app_remote_servo_sync_cancel(void)
{
    s_authorization_generation++;
    memset(&s_sync, 0, sizeof(s_sync));
    s_sync.enabled = APP_REMOTE_SERVO_SYNC_ENABLED;
    s_batch_required_mask = 0U;
    s_confirmed_mask = 0U;
    s_batch_written_mask = 0U;
    s_batch_gap_valid = 0U;
    s_reverse_wait = 0U;
    s_motion_direction = 0;
}

static uint8_t app_remote_servo_axis_selected(servo_axis_t axis)
{
    return app_config_servo_axis_controllable((uint8_t)axis);
}

/* 仅复制当前配置参与控制的轴，避免未选轴的历史快照影响遥控状态。 */
static void app_remote_servo_clear_unselected_snapshot(
    servo_axis_t axis,
    servo_snapshot_t *snapshot)
{
    if ((snapshot == NULL) ||
        app_remote_servo_axis_selected(axis))
    {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->query_selected = 0U;
    snapshot->speed_age_ms = UINT32_MAX;
    snapshot->fault_age_ms = UINT32_MAX;
    snapshot->position_age_ms = UINT32_MAX;
    snapshot->position_sequence = 0U;
    snapshot->last_result = SERVO_NOT_CONFIGURED;
    snapshot->speed_write_result = SERVO_NOT_CONFIGURED;
}

static uint8_t app_remote_servo_axes_ready(const servo_snapshot_t *left,
                                           const servo_snapshot_t *right)
{
    if (app_remote_servo_axis_selected(SERVO_AXIS_LEFT) &&
        ((left == NULL) || !left->parameter_verified ||
         !left->enabled ||
         (left->busy &&
          (s_pending_speed_sequence[SERVO_AXIS_LEFT] == 0U)) ||
         left->stop_latched || left->fault ||
         (left->control_mode != SERVO_CONTROL_RS485_VELOCITY)))
    {
        return 0U;
    }
    if (app_remote_servo_axis_selected(SERVO_AXIS_RIGHT) &&
        ((right == NULL) || !right->parameter_verified ||
         !right->enabled ||
         (right->busy &&
          (s_pending_speed_sequence[SERVO_AXIS_RIGHT] == 0U)) ||
         right->stop_latched || right->fault ||
         (right->control_mode != SERVO_CONTROL_RS485_VELOCITY)))
    {
        return 0U;
    }
    return 1U;
}

static uint8_t app_remote_servo_has_pending(void)
{
    uint8_t axis;

    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (s_pending_speed_sequence[axis] != 0U)
        {
            return 1U;
        }
    }
    return 0U;
}

static int16_t app_remote_servo_map_lv(uint16_t lv)
{
    int32_t rpm;

    if (lv < APP_REMOTE_SERVO_NEUTRAL_LOW)
    {
        rpm = -((int32_t)(APP_REMOTE_SERVO_NEUTRAL_LOW - lv) *
                (int32_t)(-APP_REMOTE_SERVO_MIN_RPM) /
                (int32_t)APP_REMOTE_SERVO_NEUTRAL_LOW);
    }
    else if (lv > APP_REMOTE_SERVO_NEUTRAL_HIGH)
    {
        rpm = ((int32_t)(lv - APP_REMOTE_SERVO_NEUTRAL_HIGH) *
                (int32_t)APP_REMOTE_SERVO_MAX_RPM /
                (int32_t)(2000U - APP_REMOTE_SERVO_NEUTRAL_HIGH));
    }
    else
    {
        rpm = 0;
    }
    if (rpm < APP_REMOTE_SERVO_MIN_RPM)
    {
        rpm = APP_REMOTE_SERVO_MIN_RPM;
    }
    if (rpm > APP_REMOTE_SERVO_MAX_RPM)
    {
        rpm = APP_REMOTE_SERVO_MAX_RPM;
    }
    return (int16_t)rpm;
}

static void app_remote_servo_enter_estop(remote_stop_reason_t reason)
{
    servo_snapshot_t right;

    if (s_hold_stopping ||
        s_controlled_stop_state == REMOTE_CONTROLLED_STOP_DISABLING)
    {
        if (s_controlled_stop_state != REMOTE_CONTROLLED_STOP_DISABLING ||
            reason != s_controlled_stop_reason)
            s_controlled_stop_state = REMOTE_CONTROLLED_STOP_FAILED;
    }
    if (s_state != REMOTE_SERVO_ESTOP)
    {
        s_estop_count++;
        s_stop_reason = reason;
        s_stop_axis_mask = s_failure_axis_mask;
        s_stop_link_age_ms = s_last_frame_age_ms;
        /* 必须在底层急停清除核验和使能之前保存现场。 */
        (void)aimtor_get_snapshot(
            app_config_servo_control_axis_count() > 1U ?
                SERVO_AXIS_LEFT :
                (app_config_servo_control_axis_mask() &
                 APP_SERVO_CONTROL_AXIS_RIGHT) ?
                    SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT,
            &s_stop_snapshot);
        /* 双轴联停优先保留异常轴，避免右轴故障被左轴正常状态掩盖。 */
        if ((s_stop_axis_mask == APP_SERVO_CONTROL_AXIS_RIGHT) &&
            aimtor_get_snapshot(SERVO_AXIS_RIGHT, &right))
        {
            s_stop_snapshot = right;
        }
        else if ((s_stop_axis_mask != APP_SERVO_CONTROL_AXIS_LEFT) &&
            (app_config_servo_control_axis_count() > 1U) &&
            aimtor_get_snapshot(SERVO_AXIS_RIGHT, &right) &&
            (right.fault || (right.fault_code != 0U) ||
             (right.consecutive_failures > s_stop_snapshot.consecutive_failures) ||
             (right.speed_age_ms > s_stop_snapshot.speed_age_ms) ||
             (right.fault_age_ms > s_stop_snapshot.fault_age_ms)))
        {
            s_stop_snapshot = right;
        }
        if (reason == REMOTE_STOP_LINK)
        {
            s_link_loss_estop_count++;
        }
    }
    else if (reason != REMOTE_STOP_K1)
    {
        /* 停车确认失败不得保留K5可恢复标记。 */
        s_stop_reason = reason;
    }
    s_state = REMOTE_SERVO_ESTOP;
    s_hold_stopping = 0U;
    s_arm_cancel = 0U; s_arm_cancel_reason = REMOTE_STOP_NONE;
    s_arm_cancel_deceleration = 0.0f;
    aimtor_set_remote_stop_active(false);
    aimtor_set_motion_authorized(false);
    app_remote_servo_sync_cancel();
    if (app_remote_servo_sync_enabled()) s_sync.state = REMOTE_SYNC_FAULT;
    s_authorized = 0U;
    s_target_rpm = 0;
    s_commanded_rpm = 0;
    s_normal_reversing = 0U;
    app_motion_ramp_clear(&s_motion_ramp);
    app_normal_ramp_boundary(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount()));
    memset(s_pending_speed_sequence, 0, sizeof(s_pending_speed_sequence));
    memset(s_pending_speed_rpm, 0, sizeof(s_pending_speed_rpm));
    memset(s_axis_commanded_rpm, 0, sizeof(s_axis_commanded_rpm));
    s_speed_batch_active = 0U;
    s_speed_batch_success_mask = 0U;
    s_estop_latched = 1U;
    s_last_result = SERVO_NOT_READY;
    s_comm_state = REMOTE_COMM_FAILED;
    /* 先立即切断本地输出，RS485失能由通信任务尽力完成。 */
    if (reason == REMOTE_STOP_TRANSACTION || reason == REMOTE_STOP_SERVO_STATE ||
        reason == REMOTE_STOP_SPEED_SUBMIT)
        aimtor_request_controlled_safety_stop(1U);
    if (reason == REMOTE_STOP_K1) aimtor_emergency_stop_k1();
    else if (!aimtor_safety_stop_active() || reason == REMOTE_STOP_MANUAL)
        aimtor_emergency_stop();
    app_control_arbiter_finish_stop(APP_CONTROL_OWNER_REMOTE, 5U, 14U, 0U, 0U);
    app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
    aimtor_set_remote_control_active(false);
    if (!aimtor_safety_stop_active()) servo_debug_abort_from_fault();
}

void app_remote_servo_init(void)
{
    s_arm_cancel = 0U;
    s_arm_cancel_reason = REMOTE_STOP_NONE;
    s_arm_cancel_deceleration = 0.0f;
    s_can_k5_event = 0U;
    s_can_k5_axes = 0U;
    /* 重新初始化遥控服务时只释放自身旧所有权，不影响其他控制源。 */
    app_control_arbiter_release(APP_CONTROL_OWNER_REMOTE);
    s_hold_stopping = 0U;
    s_k5_idle_verify = 0U;
    s_k5_idle_verify_tick = 0U;
    s_k5_idle_verify_event = 0U;
    aimtor_set_remote_stop_active(false);
    s_controlled_stop_state = REMOTE_CONTROLLED_STOP_IDLE;
    s_controlled_stop_reason = REMOTE_STOP_NONE;
    s_stop_timeout_ms = s_stop_generation = 0U;
    s_stop_deceleration = 0.0f;
    s_stop_first_command_valid = s_stop_zero_time_valid = s_stop_complete_time_valid = 0U;
    aimtor_set_motion_authorized(false);
    s_state = REMOTE_SERVO_DISABLED;
    s_authorized = 0U;
    s_estop_latched = 0U;
    s_link_valid = 0U;
    s_lv = 1000U;
    s_target_rpm = 0;
    s_commanded_rpm = 0;
    s_normal_reversing = 0U;
    app_motion_ramp_clear(&s_motion_ramp);
    app_normal_ramp_boundary(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount()));
    s_last_frame_age_ms = 0U;
    s_arm_count = 0U;
    s_estop_count = 0U;
    s_link_loss_estop_count = 0U;
    s_last_result = SERVO_OK;
    s_last_speed_update_tick = 0U;
    s_input.link_up = 0U;
    s_input_tick = xTaskGetTickCount();
    s_stop_requested = 0U;
    s_short_count = 0U;
    s_long_count = 0U;
    s_idle_acquire_pending = 0U;
    s_k6_short_count = 0U;
    s_k6_radio_generation = 0U;
    s_radio_generation = 0U;
    s_stop_reason = REMOTE_STOP_NONE;
    s_stop_link_age_ms = 0U;
    s_comm_state = REMOTE_COMM_OK;
    s_comm_fail_count = 0U;
    s_speed_submit_fail_count = 0U;
    s_last_comm_ok_tick = xTaskGetTickCount();
    memset(s_pending_speed_sequence, 0, sizeof(s_pending_speed_sequence));
    memset(s_pending_speed_rpm, 0, sizeof(s_pending_speed_rpm));
    memset(s_axis_commanded_rpm, 0, sizeof(s_axis_commanded_rpm));
    s_speed_axis_turn = 0U;
    s_speed_batch_active = 0U;
    s_speed_batch_success_mask = 0U;
    s_speed_batch_target_rpm = 0;
    s_speed_batch_tick = 0U;
    memset(s_batch_response_tick, 0, sizeof(s_batch_response_tick));
    s_batch_response_gap_ms = 0U;
    s_stop_axis_mask = 0U;
    s_failure_axis_mask = 0U;
    s_arm_action_axis = 0U;
    s_batch_id = 0U;
    s_authorization_generation = 0U;
    app_remote_servo_sync_cancel();
    aimtor_set_remote_control_mask(0U);
}

void app_remote_servo_update(const nrf_remote_snapshot_t *remote)
{
    if (remote != NULL)
    {
        /* 接收任务只发布最新快照；事件累计数避免FIFO多包覆盖K5事件。 */
        taskENTER_CRITICAL();
        s_input = *remote;
        s_input_tick = xTaskGetTickCount();
        taskEXIT_CRITICAL();
    }
}

void app_remote_servo_recovery_reset(void)
{
    nrf_remote_snapshot_t input = s_input;
    TickType_t tick = s_input_tick;
    app_remote_servo_init();
    s_input = input; s_input_tick = tick;
    s_short_count = input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
    s_long_count = input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
    s_radio_generation = input.recovery_generation;
}

void app_remote_servo_request_estop(void)
{
    taskENTER_CRITICAL();
    s_stop_requested = 1U;
    taskEXIT_CRITICAL();
}

static uint8_t app_remote_servo_k5_hold_completed(void)
{
    servo_k1_hold_status_t hold;
    if (s_state != REMOTE_SERVO_ESTOP || s_stop_reason != REMOTE_STOP_K1 ||
        s_controlled_stop_state != REMOTE_CONTROLLED_STOP_DONE ||
        s_authorized || s_hold_stopping ||
        app_control_arbiter_lock_reason() != APP_CONTROL_LOCK_NONE ||
        app_health_watchdog_test_requested())
        return 0U;
    /* 停稳后锁存显示已清除；重复短按按真实保持判断，扰动仍由底层补零。 */
    return aimtor_k1_hold_get_status(&hold) &&
        hold.state == SERVO_K1_HOLD_READY && hold.valid &&
        hold.axis_mask == app_config_servo_control_axis_mask() &&
        hold.enabled_mask == hold.axis_mask;
}

static void app_remote_servo_process_input(const nrf_remote_snapshot_t *remote)
{
    uint8_t recovered;
    if (remote == NULL)
    {
        return;
    }
    s_lv = remote->left_vertical;
    s_last_frame_age_ms = remote->age_ms;
    recovered = (uint8_t)(remote->recovery_generation != s_radio_generation);
    s_radio_generation = remote->recovery_generation;
    s_link_valid = recovered ? 0U : remote->link_up;
    if (recovered)
    {
        /* 即使停车分支提前返回，也先消费恢复前的授权事件。 */
        s_short_count = remote->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
    }

    if ((s_link_valid == 0U) &&
        ((s_state == REMOTE_SERVO_ARMING) ||
         (s_state == REMOTE_SERVO_ARMED) ||
         (s_state == REMOTE_SERVO_RUNNING)))
    {
        if (s_hold_stopping)
        {
            s_target_rpm = 0;
        }
        else if (app_remote_servo_begin_controlled_stop(REMOTE_STOP_LINK))
        {
            s_target_rpm = 0;
        }
        else
        {
            app_remote_servo_enter_estop(REMOTE_STOP_LINK);
            return;
        }
    }
    if (s_link_valid == 0U)
    {
        /* 失联期间消费历史事件，恢复链路后必须产生新的长按。 */
        s_short_count = remote->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        return;
    }

    if (s_target_rpm != (s_authorized ? app_remote_servo_map_lv(remote->left_vertical) : 0))
        app_normal_ramp_boundary(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount()));
    s_target_rpm = s_authorized ?
        app_remote_servo_map_lv(remote->left_vertical) : 0;
    if (s_controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED)
    {
        /* 真实异常或停车失败锁存不能被后续K5事件伪装成人工停机。 */
        s_short_count = remote->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        return;
    }
    if (s_k5_idle_verify || (remote->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] != s_short_count))
    {
        s_k5_idle_verify = 0U;
        s_short_count = remote->short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        if (s_controlled_stop_state == REMOTE_CONTROLLED_STOP_DISABLING)
            return;
        /* 已完成的人工保持不重复停车，也不刷新原停车时间。 */
        if (app_remote_servo_k5_hold_completed())
            return;
        if (!app_remote_servo_begin_controlled_stop(REMOTE_STOP_K1))
            app_remote_servo_enter_estop(REMOTE_STOP_K1);
        return;
    }
    /* 停车和失能确认期间消费长按，不缓存停完后的授权。 */
    if (s_hold_stopping ||
        s_controlled_stop_state == REMOTE_CONTROLLED_STOP_DISABLING)
    {
        s_long_count = remote->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        return;
    }
    if (remote->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] == s_long_count)
    {
        s_idle_acquire_pending = 0U;
        return;
    }
    if (s_idle_acquire_pending &&
        ((app_control_arbiter_generation() != s_idle_acquire_generation &&
          (app_control_arbiter_stopping() || app_control_arbiter_generation() != s_idle_acquire_generation + 1U)) ||
         app_control_arbiter_lock_reason() != APP_CONTROL_LOCK_NONE ||
         app_remote_servo_tick_ms(xTaskGetTickCount() - s_idle_acquire_tick) >= APP_SERVO_STATUS_MAX_AGE_MS))
    {
        s_idle_acquire_pending = 0U;
        s_long_count = remote->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        return;
    }
    if (app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE &&
        app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_NONE &&
        app_control_arbiter_stopping())
    {
        if (!s_idle_acquire_pending)
        {
            s_idle_acquire_pending = 1U;
            s_idle_acquire_tick = xTaskGetTickCount();
            s_idle_acquire_generation = app_control_arbiter_generation();
        }
        return;
    }
    s_idle_acquire_pending = 0U;
    s_long_count = remote->long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
    if (!aimtor_boot_zero_ready() ||
        (!aimtor_startup_hold_ready() &&
         !(s_state == REMOTE_SERVO_ESTOP && s_stop_reason == REMOTE_STOP_K1)) ||
        !app_config_motion_hardware_ready())
    {
        s_last_result = SERVO_NOT_READY;
        return;
    }
    /* 只读配置只接收遥控数据，禁止进入授权和任何控制事务。 */
    if (app_config_servo_readonly_test() ||
        (app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE &&
         app_control_arbiter_owner() != APP_CONTROL_OWNER_REMOTE))
    {
        s_last_result = SERVO_NOT_CONFIGURED;
        return;
    }
    if (
        (remote->left_vertical >= APP_REMOTE_SERVO_NEUTRAL_LOW) &&
        (remote->left_vertical <= APP_REMOTE_SERVO_NEUTRAL_HIGH) &&
        ((s_state == REMOTE_SERVO_DISABLED) ||
         (s_state == REMOTE_SERVO_ESTOP)))
    {
        servo_k1_hold_status_t hold;
        servo_snapshot_t left;
        servo_snapshot_t right;
        if (s_state == REMOTE_SERVO_ESTOP)
        {
            uint8_t axis;
            if (!aimtor_get_snapshot_pair(&left, &right)) return;
            for (axis = 0U; axis < 2U; ++axis)
            {
                const servo_snapshot_t *a = axis == 0U ? &left : &right;
                if (app_config_servo_axis_selected(axis) &&
                    (!a->online || a->fault || !a->speed_valid || !a->fault_valid ||
                     a->speed_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
                     a->fault_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS || a->actual_speed_rpm)) return;
            }
            (void)app_control_arbiter_clear_safety();
        }
        s_acquire_ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_REMOTE,
            app_config_servo_control_axis_mask());
        if (!s_acquire_ticket) { s_last_result = SERVO_BUSY; return; }
        (void)hold; (void)left; (void)right;
        /* 各类复位的适用待机均可直接接管；关闭待机时沿用重新准备流程。 */
        s_prepare_started = 0U; s_arm_cancel = 0U;
        s_state = REMOTE_SERVO_ARMING;
        app_remote_servo_sync_cancel();
        s_authorized = 0U;
        s_estop_latched = aimtor_startup_hold_active() ? 0U : 1U;
        s_target_rpm = 0;
        s_commanded_rpm = 0;
        s_normal_reversing = 0U;
        app_motion_ramp_clear(&s_motion_ramp);
        app_normal_ramp_boundary(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount()));
        memset(s_pending_speed_sequence, 0, sizeof(s_pending_speed_sequence));
        memset(s_pending_speed_rpm, 0, sizeof(s_pending_speed_rpm));
        memset(s_axis_commanded_rpm, 0, sizeof(s_axis_commanded_rpm));
        s_speed_axis_turn = 0U;
        s_speed_batch_active = 0U;
        s_speed_batch_success_mask = 0U;
        s_comm_fail_count = 0U;
        s_speed_submit_fail_count = 0U;
        s_comm_state = REMOTE_COMM_OK;
        s_last_comm_ok_tick = xTaskGetTickCount();
        s_last_result = SERVO_BUSY;
        s_arm_tick = xTaskGetTickCount();
        s_arm_action_axis = 0U;
    }
}

void app_remote_servo_service(void)
{
    servo_snapshot_t left;
    servo_snapshot_t right;
    servo_snapshot_t *snapshot;
    servo_result_t result = SERVO_OK;
    TickType_t now;
    uint32_t elapsed_update_ms;
    int16_t next_rpm;
    nrf_remote_snapshot_t remote;
    uint8_t stop_requested;
    TickType_t input_tick;
    uint8_t axis_index;
    uint8_t pending;
    uint8_t candidate;
    uint8_t submitted;
    servo_k1_hold_status_t k1_hold;
    servo_recovery_status_t recovery;

    s_failure_axis_mask = 0U;
    taskENTER_CRITICAL();
    remote = s_input;
    input_tick = s_input_tick;
    stop_requested = s_stop_requested;
    s_stop_requested = 0U;
    taskEXIT_CRITICAL();
    remote.age_ms += (uint32_t)((uint64_t)(TickType_t)(
        xTaskGetTickCount() - input_tick) * 1000U / configTICK_RATE_HZ);
    if (remote.age_ms >= APP_REMOTE_SERVO_LINK_TIMEOUT_MS)
    {
        remote.link_up = 0U;
    }
    /* 通信类故障恢复期间，K5长按只提交一次复位请求，绝不转成授权。 */
    aimtor_get_recovery_status(&recovery);
    if (recovery.state != SERVO_RECOVERY_NONE &&
        recovery.state != SERVO_RECOVERY_DONE)
    {
        if (recovery.state != SERVO_RECOVERY_READY || !s_recovery_ready_seen ||
            s_recovery_ready_generation != recovery.generation ||
            remote.recovery_generation != s_radio_generation)
        {
            s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
            s_recovery_ready_seen = recovery.state == SERVO_RECOVERY_READY;
            s_recovery_ready_generation = recovery.generation;
        }
        if (remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] != s_long_count)
        {
            uint8_t neutral = (uint8_t)(remote.left_vertical >= APP_REMOTE_SERVO_NEUTRAL_LOW &&
                remote.left_vertical <= APP_REMOTE_SERVO_NEUTRAL_HIGH &&
                remote.left_horizontal >= APP_REMOTE_SERVO_NEUTRAL_LOW && remote.left_horizontal <= APP_REMOTE_SERVO_NEUTRAL_HIGH &&
                remote.right_vertical >= APP_REMOTE_SERVO_NEUTRAL_LOW && remote.right_vertical <= APP_REMOTE_SERVO_NEUTRAL_HIGH &&
                remote.right_horizontal >= APP_REMOTE_SERVO_NEUTRAL_LOW && remote.right_horizontal <= APP_REMOTE_SERVO_NEUTRAL_HIGH);
            s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
            if (recovery.state == SERVO_RECOVERY_READY && remote.link_up && neutral)
                (void)aimtor_request_recovery(1U);
        }
        s_short_count = remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_radio_generation = remote.recovery_generation;
        s_link_valid = remote.link_up;
        s_last_frame_age_ms = remote.age_ms;
        s_target_rpm = 0;
        s_authorized = 0U;
        return;
    }
    s_recovery_ready_seen = 0U;
    /* 只消费去重后的遥控器K6短按，恢复首帧及失联历史事件不触发。 */
    if (remote.link_up &&
        remote.recovery_generation == s_k6_radio_generation &&
        remote.short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX] != s_k6_short_count)
    {
#if (APP_WDG_TEST_K6_SHORT_ENABLED == 1U)
        if (!app_health_watchdog_test_requested())
        {
            app_health_request_watchdog_test();
            app_remote_servo_enter_estop(REMOTE_STOP_MANUAL);
            app_cli_servo_session_abort(CLI_EXIT_SAFETY);
        }
#endif
    }
    s_k6_short_count = remote.short_counts[APP_REMOTE_WATCHDOG_KEY_INDEX];
    s_k6_radio_generation = remote.recovery_generation;
    if (app_health_watchdog_test_requested())
    {
        /* 等待真实复位期间持续消费按键，不允许长按或迟到应答重启。 */
        s_short_count = remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_radio_generation = remote.recovery_generation;
        s_link_valid = remote.link_up;
        s_last_frame_age_ms = remote.age_ms;
        return;
    }
    if (aimtor_safety_stop_active())
    {
        if (stop_requested) { app_remote_servo_enter_estop(REMOTE_STOP_MANUAL); return; }
        if (s_authorized || s_hold_stopping)
            app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
        s_short_count = remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_radio_generation = remote.recovery_generation;
        return;
    }
    if (APP_REMOTE_SERVO_ENABLE == 0U)
    {
        return;
    }
    (void)aimtor_k1_hold_get_status(&k1_hold);
    if (k1_hold.state == SERVO_K1_HOLD_FAILED &&
        s_controlled_stop_state != REMOTE_CONTROLLED_STOP_FAILED)
    {
        s_controlled_stop_state = REMOTE_CONTROLLED_STOP_FAILED;
        app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
        s_short_count = remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        return;
    }
    if (s_k5_idle_verify && stop_requested)
    {
        s_k5_idle_verify = 0U;
        app_remote_servo_enter_estop(REMOTE_STOP_MANUAL);
        return;
    }
    if (s_k5_idle_verify && (remote.recovery_generation != s_radio_generation || !remote.link_up))
    {
        s_short_count = remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
    }
    if (app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE && !app_cli_servo_session_active() &&
        (s_k5_idle_verify || (remote.recovery_generation == s_radio_generation &&
         remote.link_up && remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] != s_short_count)))
    {
        app_control_stop_t stop;
        app_control_arbiter_get_stop(&stop);
        if (s_k5_idle_verify || (stop.event && stop.result == 3U &&
            stop.owner != APP_CONTROL_OWNER_REMOTE))
        {
            int8_t idle = app_remote_servo_hmi_idle_k5(&stop);
            if (!s_k5_idle_verify)
            {
                s_k5_idle_verify = 1U;
                s_k5_idle_verify_tick = xTaskGetTickCount();
                s_k5_idle_verify_event = stop.event;
            }
            s_short_count = remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
            s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
            s_radio_generation = remote.recovery_generation;
            s_link_valid = remote.link_up; s_last_frame_age_ms = remote.age_ms;
            if (idle < 0 || stop.event != s_k5_idle_verify_event ||
                app_remote_servo_tick_ms(xTaskGetTickCount() - s_k5_idle_verify_tick) >= APP_SERVO_STATUS_MAX_AGE_MS)
            {
                s_k5_idle_verify = 0U;
                app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
            }
            else if (idle > 0) s_k5_idle_verify = 0U;
            return;
        }
    }
    if (app_control_arbiter_owner() == APP_CONTROL_OWNER_HMI)
    {
        if (s_k5_idle_verify || (remote.recovery_generation == s_radio_generation && remote.link_up &&
            remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] != s_short_count))
            (void)app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_REMOTE,
                5U, APP_REMOTE_SERVO_K5_STOP_DECEL_MM_S2, 0U, 0x10U);
        s_short_count = remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_radio_generation = remote.recovery_generation;
        s_k5_idle_verify = 0U;
        s_link_valid = remote.link_up; s_authorized = 0U;
        s_state = REMOTE_SERVO_DISABLED; s_estop_latched = 0U;
        return;
    }
    {
        app_control_stop_t stop;
        if (app_control_arbiter_take_stop(APP_CONTROL_OWNER_REMOTE, &stop))
        {
            if (!app_remote_servo_begin_controlled_stop_rate(
                    stop.reason == 4U ? REMOTE_STOP_LINK : REMOTE_STOP_K1,
                    (float)stop.deceleration_mm_s2 / 1000.0f))
            {
                app_control_arbiter_finish_stop(APP_CONTROL_OWNER_REMOTE, 5U, 14U, 0U, 0U);
                app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
            }
        }
    }
    if (app_cli_servo_session_active())
    {
        /* CLI期间无线只保留新短按急停，长按及失联不能接管运动。 */
        if (s_k5_idle_verify || (remote.recovery_generation == s_radio_generation &&
            remote.link_up && remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] != s_short_count))
        {
            (void)app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_REMOTE,
                5U, APP_REMOTE_SERVO_K5_STOP_DECEL_MM_S2,
                remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX], 0x10U);
            app_cli_servo_session_external_stop();
        }
        s_k5_idle_verify = 0U;
        if (stop_requested) app_cli_servo_session_abort(CLI_EXIT_MANUAL);
        s_short_count = remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_radio_generation = remote.recovery_generation;
        s_link_valid = remote.link_up;
        s_authorized = 0U;
        s_target_rpm = 0;
        /* 清除旧遥控人工锁存展示，不允许其再驱动待机恢复。 */
        s_state = REMOTE_SERVO_DISABLED;
        s_estop_latched = 0U;
        return;
    }
    if (app_control_arbiter_owner() == APP_CONTROL_OWNER_CAN)
    {
        /* CAN拥有运动时，遥控只传播新K5锁存；不能启动自己的准备或停车写。 */
        if (s_k5_idle_verify || (remote.recovery_generation == s_radio_generation && remote.link_up &&
            remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] != s_short_count))
        {
            s_estop_latched = 1U; s_stop_reason = REMOTE_STOP_K1;
            s_state = REMOTE_SERVO_ESTOP; s_controlled_stop_state = REMOTE_CONTROLLED_STOP_DONE;
            (void)app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_REMOTE,
                5U, APP_REMOTE_SERVO_K5_STOP_DECEL_MM_S2, remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX], 0x10U);
        }
        s_short_count = remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]; s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_radio_generation = remote.recovery_generation; s_link_valid = remote.link_up;
        s_k5_idle_verify = 0U;
        s_last_frame_age_ms = remote.age_ms;
        return;
    }
    if (stop_requested && (s_state != REMOTE_SERVO_DISABLED ||
                           aimtor_startup_hold_active()))
    {
        app_remote_servo_enter_estop(REMOTE_STOP_MANUAL);
        s_short_count = remote.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        s_long_count = remote.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
    }
    else
    {
        app_remote_servo_process_input(&remote);
    }
    if (s_state == REMOTE_SERVO_DISABLED ||
        s_state == REMOTE_SERVO_ESTOP)
    {
        if (s_controlled_stop_state == REMOTE_CONTROLLED_STOP_DISABLING)
        {
            uint8_t disabled = 1U;
            now = xTaskGetTickCount();
            if (!aimtor_get_snapshot_pair(&left, &right)) disabled = 0U;
            else for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
            {
                if (!app_remote_servo_axis_selected((servo_axis_t)axis_index)) continue;
                snapshot = axis_index == SERVO_AXIS_LEFT ? &left : &right;
                if (snapshot->fault || snapshot->fault_code || !snapshot->online)
                {
                    app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
                    break;
                }
                /* enabled的软件清零不等于失能应答；等待底层核验完成。 */
                if (snapshot->enabled || !snapshot->disable_verified || snapshot->busy ||
                    !snapshot->speed_valid || !snapshot->fault_valid ||
                    snapshot->actual_speed_rpm != 0 ||
                    snapshot->speed_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
                    snapshot->fault_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS)
                    disabled = 0U;
            }
            if (s_controlled_stop_state == REMOTE_CONTROLLED_STOP_DISABLING)
            {
                if (disabled)
                {
                    s_controlled_stop_state = REMOTE_CONTROLLED_STOP_DONE;
                    s_stop_complete_ms = app_remote_servo_tick_ms(now - s_hold_stop_tick);
                    s_stop_complete_time_valid = 1U;
                }
                else if (app_remote_servo_tick_ms(now - s_hold_stop_tick) >= s_stop_timeout_ms)
                    app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
            }
        }
        return;
    }
    if (!s_hold_stopping && ((s_link_valid == 0U) ||
        (s_last_frame_age_ms >= APP_REMOTE_SERVO_LINK_TIMEOUT_MS))
       )
    {
        app_remote_servo_enter_estop(REMOTE_STOP_LINK);
        return;
    }
    if (!aimtor_get_snapshot_pair(&left, &right))
    {
        s_last_result = SERVO_NOT_READY;
        if (s_hold_stopping) app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
        return;
    }
    app_remote_servo_clear_unselected_snapshot(SERVO_AXIS_LEFT, &left);
    app_remote_servo_clear_unselected_snapshot(SERVO_AXIS_RIGHT, &right);
    if (app_remote_servo_sync_enabled() && s_authorized)
        app_remote_sync_collect(&s_sync, &left, &right, xTaskGetTickCount());

    if ((app_remote_servo_axis_selected(SERVO_AXIS_LEFT) && left.fault) ||
        (app_remote_servo_axis_selected(SERVO_AXIS_RIGHT) && right.fault))
    {
        s_failure_axis_mask = (uint8_t)(
            (app_remote_servo_axis_selected(SERVO_AXIS_LEFT) && left.fault ?
             APP_SERVO_CONTROL_AXIS_LEFT : 0U) |
            (app_remote_servo_axis_selected(SERVO_AXIS_RIGHT) && right.fault ?
             APP_SERVO_CONTROL_AXIS_RIGHT : 0U));
        app_remote_servo_enter_estop(REMOTE_STOP_SERVO_STATE);
        return;
    }

    /* 先处理已经完成的速度事务，两个轴始终共用一个目标转速。 */
    pending = 0U;
    for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
    {
        if (!app_remote_servo_axis_selected((servo_axis_t)axis_index))
        {
            continue;
        }
        snapshot = axis_index == SERVO_AXIS_LEFT ? &left : &right;
        if (s_pending_speed_sequence[axis_index] == 0U)
        {
            continue;
        }
        /* 停车仅跟踪已发送应答并递减到零，不提交取消的旧运动批次。 */
        if (app_remote_servo_sync_enabled() && !s_hold_stopping &&
            s_batch_generation != s_authorization_generation)
        {
            s_pending_speed_sequence[axis_index] = 0U;
            continue;
        }
        if (snapshot->speed_write_completed !=
            s_pending_speed_sequence[axis_index])
        {
            pending = 1U;
            continue;
        }
        if (snapshot->speed_write_result == SERVO_OK)
        {
            s_pending_speed_sequence[axis_index] = 0U;
            if (snapshot->speed_write_rpm_valid &&
                s_pending_speed_rpm[axis_index] != snapshot->speed_write_rpm)
            {
                s_pending_speed_rpm[axis_index] = snapshot->speed_write_rpm;
                s_speed_batch_target_rpm = snapshot->speed_write_rpm;
                app_motion_ramp_reset(&s_motion_ramp, snapshot->speed_write_rpm);
            }
            s_axis_commanded_rpm[axis_index] = s_pending_speed_rpm[axis_index];
            if (s_hold_stopping && s_pending_speed_rpm[axis_index] == 0)
            {
                s_hold_stop_sequence[axis_index] = snapshot->speed_sequence;
                s_hold_zero_written_mask |= (uint8_t)(1U << axis_index);
                s_hold_zero_count[axis_index] = 0U;
            }
            if (s_hold_stopping && s_hold_step_active &&
                s_pending_speed_rpm[axis_index] == s_hold_step_command[axis_index])
                s_hold_step_mask &= (uint8_t)~(1U << axis_index);
            s_confirmed_mask |= (uint8_t)(1U << axis_index);
            if (app_config_servo_control_axis_count() > 1U)
            {
                s_speed_batch_success_mask |=
                    (uint8_t)(1U << axis_index);
                s_batch_response_tick[axis_index] = xTaskGetTickCount();
            }
            else if (!s_hold_stopping)
            {
                s_commanded_rpm = s_pending_speed_rpm[axis_index];
                s_speed_submit_fail_count = 0U;
                s_comm_state = REMOTE_COMM_OK;
                s_last_comm_ok_tick = xTaskGetTickCount();
                app_normal_ramp_batch_done(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount() - s_speed_batch_tick));
            }
        }
        else
        {
            s_pending_speed_sequence[axis_index] = 0U;
            if (s_hold_stopping)
            {
                s_failure_axis_mask = (uint8_t)(1U << axis_index);
                app_remote_servo_enter_estop(REMOTE_STOP_SPEED_SUBMIT);
                return;
            }
            /*
             * 当前批次只保留已经成功的轴，失败轴清除成功标志，
             * 下一轮只重试失败轴，避免成功轴被重复写入。
             */
            if (app_config_servo_control_axis_count() > 1U)
            {
                s_speed_batch_success_mask &= (uint8_t)~(1U << axis_index);
            }
            if (s_speed_submit_fail_count < 0xFFFFU)
            {
                s_speed_submit_fail_count++;
            }
            s_comm_state = REMOTE_COMM_DEGRADED;
            if (s_speed_submit_fail_count >=
                APP_REMOTE_SERVO_COMM_FAILURE_LIMIT)
            {
                s_failure_axis_mask = (uint8_t)(1U << axis_index);
                s_comm_state = REMOTE_COMM_FAILED;
                app_remote_servo_enter_estop(REMOTE_STOP_SPEED_SUBMIT);
                return;
            }
        }
    }
    if (!app_remote_servo_has_pending() &&
        (app_config_servo_control_axis_count() > 1U) &&
        (s_speed_batch_active != 0U) &&
        ((s_speed_batch_success_mask &
          app_config_servo_control_axis_mask()) ==
         app_config_servo_control_axis_mask()))
    {
        s_commanded_rpm = s_speed_batch_target_rpm;
        s_batch_gap_valid = !app_remote_servo_sync_enabled() ||
                           s_batch_written_mask == APP_SERVO_CONTROL_AXIS_BOTH;
        {
            int32_t gap = (int32_t)(s_batch_response_tick[SERVO_AXIS_LEFT] -
                                    s_batch_response_tick[SERVO_AXIS_RIGHT]);
            s_batch_response_gap_ms = (uint32_t)((uint64_t)(
                gap < 0 ? -(int64_t)gap : (int64_t)gap) *
                1000U / configTICK_RATE_HZ);
        }
        app_normal_ramp_batch_done(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount() - s_speed_batch_tick));
        s_speed_batch_active = 0U;
        s_speed_batch_success_mask = 0U;
        s_speed_submit_fail_count = 0U;
        s_comm_state = REMOTE_COMM_OK;
        s_last_comm_ok_tick = xTaskGetTickCount();
    }
    if (s_hold_stopping)
    {
        uint8_t stopped = 1U;
        now = xTaskGetTickCount();
        if (app_remote_servo_tick_ms(now - s_hold_stop_tick) >=
            s_stop_timeout_ms || !aimtor_startup_hold_ready() ||
            !aimtor_boot_zero_ready() || !app_config_motion_hardware_ready())
        {
            app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
            return;
        }

        /* The physical stop clock advances on every control-service pass,
         * including the time spent draining an in-flight Modbus write.  The
         * old code started this clock only after the write completed, so a
         * 60 ms response repeatedly lost its elapsed time and hit the fixed
         * deadline before reaching zero.  Anchor once to the greatest
         * commanded/feedback/pending speed and then integrate continuously. */
        if (!s_stop_ramp_anchored)
        {
            int16_t maximum = 0;
            for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
            {
                if (!app_remote_servo_axis_selected((servo_axis_t)axis_index)) continue;
                snapshot = axis_index == SERVO_AXIS_LEFT ? &left : &right;
                if (app_remote_servo_abs_rpm(s_axis_commanded_rpm[axis_index]) > maximum)
                    maximum = (int16_t)app_remote_servo_abs_rpm(s_axis_commanded_rpm[axis_index]);
                if (app_remote_servo_abs_rpm(snapshot->actual_speed_rpm) > maximum)
                    maximum = (int16_t)app_remote_servo_abs_rpm(snapshot->actual_speed_rpm);
                if (app_remote_servo_abs_rpm(s_pending_speed_rpm[axis_index]) > maximum)
                    maximum = (int16_t)app_remote_servo_abs_rpm(s_pending_speed_rpm[axis_index]);
            }
            s_stop_remaining_rpm = (float)maximum;
            s_stop_ramp_tick = now;
            s_stop_clock_tick = now;
            s_stop_ramp_anchored = 1U;
        }
        else
        {
            uint32_t dt_ms = app_remote_servo_tick_ms(now - s_stop_clock_tick);
            if (dt_ms != 0U)
            {
                s_stop_clock_tick = now;
                s_stop_remaining_rpm -= app_remote_servo_stop_rate() * (float)dt_ms / 1000.0f;
                if (s_stop_remaining_rpm < 0.0f) s_stop_remaining_rpm = 0.0f;
            }
        }
        for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
        {
            if (!app_remote_servo_axis_selected((servo_axis_t)axis_index))
                continue;
            snapshot = axis_index == SERVO_AXIS_LEFT ? &left : &right;
            if (!app_remote_servo_stop_snapshot_ready(snapshot))
            {
                s_failure_axis_mask = (uint8_t)(1U << axis_index);
                app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
                return;
            }
            if (s_pending_speed_sequence[axis_index] || snapshot->busy)
            {
                stopped = 0U;
                continue;
            }
            if ((s_hold_zero_written_mask & (1U << axis_index)) &&
                snapshot->speed_sequence != s_hold_stop_sequence[axis_index])
            {
                s_hold_stop_sequence[axis_index] = snapshot->speed_sequence;
                if (snapshot->actual_speed_rpm != 0)
                    s_hold_zero_count[axis_index] = 0U;
                else if (s_hold_zero_count[axis_index] < 2U)
                    s_hold_zero_count[axis_index]++;
            }
            if (s_axis_commanded_rpm[axis_index] != 0 ||
                !(s_hold_zero_written_mask & (1U << axis_index)) ||
                s_hold_zero_count[axis_index] < 2U ||
                snapshot->actual_speed_rpm != 0 || !snapshot->enable_feedback_valid ||
                !snapshot->enable_feedback || snapshot->enable_feedback_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS)
                stopped = 0U;
        }
        if (stopped)
        {
            if (!s_stop_zero_time_valid)
            {
                s_stop_zero_ms = app_remote_servo_tick_ms(now - s_hold_stop_tick);
                s_stop_zero_time_valid = 1U;
            }
            if (!s_stop_keep_enabled)
            {
                s_controlled_stop_state = REMOTE_CONTROLLED_STOP_DISABLING;
                app_remote_servo_enter_estop(s_controlled_stop_reason);
                return;
            }
            if (s_controlled_stop_reason == REMOTE_STOP_K1)
            {
                (void)aimtor_k1_hold_get_status(&k1_hold);
                if (k1_hold.state == SERVO_K1_HOLD_INACTIVE)
                {
                    result = aimtor_k1_hold_request(app_config_servo_control_axis_mask());
                    if (result != SERVO_OK)
                    {
                        app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
                        return;
                    }
                }
                s_controlled_stop_state = REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING;
                (void)aimtor_k1_hold_get_status(&k1_hold);
                if (!k1_hold.valid || k1_hold.zero_mask != k1_hold.axis_mask)
                    return;
            }
            s_hold_stopping = 0U;
            aimtor_set_remote_stop_active(false);
            s_state = s_controlled_stop_reason == REMOTE_STOP_K1 ?
                REMOTE_SERVO_ESTOP : REMOTE_SERVO_DISABLED;
            s_commanded_rpm = 0;
            s_normal_reversing = 0U;
            app_motion_ramp_clear(&s_motion_ramp);
            app_normal_ramp_boundary(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount()));
            s_estop_latched = 0U;
            app_control_arbiter_finish_stop(APP_CONTROL_OWNER_REMOTE, 3U, 0U,
                app_config_servo_control_axis_mask(), app_config_servo_control_axis_mask());
            app_remote_servo_sync_cancel();
            aimtor_set_remote_control_mask(0U);
            s_controlled_stop_state = REMOTE_CONTROLLED_STOP_DONE;
            app_control_arbiter_release(APP_CONTROL_OWNER_REMOTE);
            s_stop_complete_ms = app_remote_servo_tick_ms(now - s_hold_stop_tick);
            s_stop_complete_time_valid = 1U;
            s_last_result = SERVO_OK;
            return;
        }
        if (s_controlled_stop_state == REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING)
            return;
        /* 未完成轴可继续排队；总线底层仍只允许一个活动事务。 */
        if (s_hold_step_active && !s_hold_step_mask)
        {
            s_commanded_rpm = s_hold_step_base;
            s_hold_step_active = 0U;
        }
        if (!s_hold_step_active)
        {
            uint32_t dt_ms;
            int16_t magnitude;
            if (app_remote_servo_has_pending() || left.busy || right.busy) return;
            dt_ms = app_remote_servo_tick_ms(now - s_stop_ramp_tick);
            if (dt_ms < APP_REMOTE_SERVO_STOP_UPDATE_PERIOD_MS) return;
            s_stop_ramp_tick = now;
            /* 向远离零方向量化，避免小数提前归零；保留余数到下个周期。 */
            magnitude = (int16_t)s_stop_remaining_rpm;
            if ((float)magnitude < s_stop_remaining_rpm) magnitude++;
            s_hold_step_base = 0;
            s_hold_step_mask = 0U;
            for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
            {
                int16_t previous = s_axis_commanded_rpm[axis_index];
                int16_t command = previous < 0 ? (int16_t)-magnitude : magnitude;
                if (!app_remote_servo_axis_selected((servo_axis_t)axis_index))
                    continue;
                /* 半完成旧批次或同步补偿不能使停车指令增速、换向。 */
                if (previous > 0)
                    command = command < 0 ? 0 : command > previous ? previous : command;
                else if (previous < 0)
                    command = command > 0 ? 0 : command < previous ? previous : command;
                else command = 0;
                s_hold_step_command[axis_index] = command;
                if (app_remote_servo_abs_rpm(command) >
                    app_remote_servo_abs_rpm(s_hold_step_base))
                    s_hold_step_base = command;
                if (command != previous ||
                    (command == 0 && !(s_hold_zero_written_mask & (1U << axis_index))))
                    s_hold_step_mask |= (uint8_t)(1U << axis_index);
            }
            if (!s_hold_step_mask) return;
            s_hold_step_active = 1U;
            s_batch_id++;
            if (magnitude == 0)
                s_controlled_stop_state = REMOTE_CONTROLLED_STOP_VERIFYING;
        }
        for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
        {
            candidate = (uint8_t)((s_speed_axis_turn + axis_index) % SERVO_AXIS_COUNT);
            if (!(s_hold_step_mask & (1U << candidate)) ||
                s_pending_speed_sequence[candidate]) continue;
            snapshot = candidate == SERVO_AXIS_LEFT ? &left : &right;
            if (snapshot->busy) continue;
            result = aimtor_set_stop_velocity_rpm(
                (servo_axis_t)candidate, s_hold_step_command[candidate]);
            if (result == SERVO_OK)
            {
                s_pending_speed_sequence[candidate] = snapshot->speed_write_sequence + 1U;
                s_pending_speed_rpm[candidate] = s_hold_step_command[candidate];
                s_speed_axis_turn = (uint8_t)((candidate + 1U) % SERVO_AXIS_COUNT);
                if (!s_stop_first_command_valid)
                {
                    s_stop_first_command_ms = app_remote_servo_tick_ms(now - s_hold_stop_tick);
                    s_stop_first_command_valid = 1U;
                }
            }
            else if (result != SERVO_BUSY)
            {
                s_failure_axis_mask = (uint8_t)(1U << candidate);
                app_remote_servo_enter_estop(REMOTE_STOP_SPEED_SUBMIT);
            }
            break;
        }
        return;
    }
    if (s_state == REMOTE_SERVO_ARMING)
    {
        int8_t granted = s_arm_cancel && app_control_arbiter_owner() == APP_CONTROL_OWNER_REMOTE &&
            app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_NONE ? 1 :
            app_control_arbiter_poll(s_acquire_ticket, &s_authorization_generation);
        if (granted == 0) return;
        if (granted < 0) { s_state = REMOTE_SERVO_DISABLED; s_last_result = SERVO_BUSY; return; }
        if (!s_prepare_started)
        {
            servo_k1_hold_status_t hold;
            (void)aimtor_k1_hold_get_status(&hold);
            if (hold.state != SERVO_K1_HOLD_INACTIVE)
            {
                if (!hold.valid || hold.zero_mask != hold.axis_mask || aimtor_k1_hold_release() != SERVO_OK)
                { app_control_arbiter_release(APP_CONTROL_OWNER_REMOTE); s_state = REMOTE_SERVO_DISABLED; return; }
                s_estop_latched = 0U;
            }
            else if (!aimtor_startup_hold_active() && !app_remote_servo_axes_ready(&left, &right))
            {
                aimtor_emergency_stop();
                servo_debug_abort_from_fault(); s_prepare_started = 1U;
                return;
            }
            servo_debug_abort_from_fault(); s_prepare_started = 1U;
        }
        if ((xTaskGetTickCount() - s_arm_tick) >= pdMS_TO_TICKS(2000U) ||
            s_lv < APP_REMOTE_SERVO_NEUTRAL_LOW ||
            s_lv > APP_REMOTE_SERVO_NEUTRAL_HIGH)
        {
            app_remote_servo_enter_estop(REMOTE_STOP_ARMING);
            return;
        }
        if (aimtor_startup_hold_active() && !aimtor_startup_hold_ready())
        {
            /* 一次长按开启异步重新待机；停稳前不清锁存、不开放授权。 */
            (void)aimtor_startup_hold_request_rearm();
            return;
        }
        /* 异步事务失败后结束本次授权，不自动重复配置或使能。 */
        if (s_arm_action_axis != 0U)
        {
            snapshot = s_arm_action_axis == 1U ? &left : &right;
            if (snapshot->busy)
            {
                return;
            }
            if (snapshot->last_result != SERVO_OK)
            {
                s_failure_axis_mask = (uint8_t)(1U << (s_arm_action_axis - 1U));
                app_remote_servo_enter_estop(s_arm_action_reason);
                return;
            }
            s_arm_action_axis = 0U;
        }
        if ((left.stop_latched && app_remote_servo_axis_selected(SERVO_AXIS_LEFT)) ||
            (right.stop_latched && app_remote_servo_axis_selected(SERVO_AXIS_RIGHT)))
        {
            result = aimtor_clear_stop_latch();
            s_last_result = result;
            if (result != SERVO_OK)
            {
                return;
            }
            s_estop_latched = 0U;
        }
        /*
         * 双轴授权分两阶段执行：
         * 第一阶段确认所有轴已经完成速度模式配置；
         * 第二阶段再逐轴使能，避免一轴已使能后阻塞另一轴模式配置。
         */
        for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
        {
            if (!app_remote_servo_axis_selected((servo_axis_t)axis_index))
            {
                continue;
            }
            snapshot = axis_index == SERVO_AXIS_LEFT ? &left : &right;
            if (!snapshot->parameter_verified ||
                snapshot->control_mode != SERVO_CONTROL_RS485_VELOCITY)
            {
                result = aimtor_remote_prepare_mode((servo_axis_t)axis_index);
                s_last_result = result;
                if (result == SERVO_OK)
                {
                    s_arm_action_axis = (uint8_t)(axis_index + 1U);
                    s_arm_action_reason = REMOTE_STOP_PREPARE;
                }
                if ((result != SERVO_OK) && (result != SERVO_BUSY))
                {
                    s_failure_axis_mask = (uint8_t)(1U << axis_index);
                    app_remote_servo_enter_estop(REMOTE_STOP_PREPARE);
                }
                return;
            }
        }
        for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
        {
            if (!app_remote_servo_axis_selected((servo_axis_t)axis_index))
            {
                continue;
            }
            snapshot = axis_index == SERVO_AXIS_LEFT ? &left : &right;
            if (snapshot->busy)
            {
                return;
            }
            if (!snapshot->enabled)
            {
                result = aimtor_remote_prepare_enable((servo_axis_t)axis_index);
                s_last_result = result;
                if (result == SERVO_OK)
                {
                    s_arm_action_axis = (uint8_t)(axis_index + 1U);
                    s_arm_action_reason = REMOTE_STOP_ENABLE;
                }
                if ((result != SERVO_OK) && (result != SERVO_BUSY))
                {
                    s_failure_axis_mask = (uint8_t)(1U << axis_index);
                    app_remote_servo_enter_estop(REMOTE_STOP_ENABLE);
                }
                return;
            }
            if (snapshot->busy)
            {
                return;
            }
        }
        /* 只有两轴状态有效且零速时才开放共同遥控授权。 */
        for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
        {
            if (!app_remote_servo_axis_selected((servo_axis_t)axis_index))
            {
                continue;
            }
            snapshot = axis_index == SERVO_AXIS_LEFT ? &left : &right;
            if (snapshot->feedback_degraded || !snapshot->online ||
                !snapshot->enable_feedback_valid || !snapshot->enable_feedback ||
                snapshot->enable_feedback_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
                snapshot->speed_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
                snapshot->fault_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
                snapshot->actual_speed_rpm != 0)
            {
                return;
            }
        }
        if (s_arm_cancel)
        {
            s_arm_cancel = 2U;
            aimtor_set_remote_control_mask(app_config_servo_control_axis_mask());
            if (!app_remote_servo_begin_controlled_stop_rate(s_arm_cancel_reason, s_arm_cancel_deceleration))
                app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
            return;
        }
        if (!app_control_arbiter_request(APP_CONTROL_OWNER_REMOTE,
                                          app_config_servo_control_axis_mask(),
                                          NULL))
        {
            s_last_result = SERVO_BUSY;
            return;
        }
        app_normal_ramp_begin(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount()));
        s_authorized = 1U;
        aimtor_set_motion_authorized(true);
        app_remote_sync_reset(&s_sync, &left, &right, xTaskGetTickCount());
        s_state = REMOTE_SERVO_ARMED;
        aimtor_set_remote_control_mask(app_config_servo_control_axis_mask());
        s_arm_count++;
        s_last_result = SERVO_OK;
        s_last_speed_update_tick = xTaskGetTickCount();
        return;
    }

    /*
     * 选中轴必须共同保持有效状态。瞬时通信异常进入降级，连续异常
     * 达到设备逐项阈值或可信年龄上限后请求受控停车。
     */
    s_comm_fail_count = 0U;
    {
        uint8_t communication_good = 1U;
    for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
    {
        if (!app_remote_servo_axis_selected((servo_axis_t)axis_index))
        {
            continue;
        }
        snapshot = axis_index == SERVO_AXIS_LEFT ? &left : &right;
        if (snapshot->speed_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
            snapshot->fault_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
            !snapshot->online)
        {
            s_failure_axis_mask = (uint8_t)(1U << axis_index);
            s_comm_fail_count = snapshot->consecutive_failures;
            app_remote_servo_enter_estop(REMOTE_STOP_TRANSACTION);
            return;
        }
        if (snapshot->consecutive_failures > s_comm_fail_count)
        {
            s_comm_fail_count = snapshot->consecutive_failures;
        }
        if (!snapshot->online ||
            snapshot->speed_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
            snapshot->fault_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS)
        {
            communication_good = 0U;
            s_comm_state = REMOTE_COMM_DEGRADED;
        }
    }
    if ((communication_good != 0U) &&
        (s_comm_state == REMOTE_COMM_DEGRADED) &&
        (s_comm_fail_count == 0U) &&
        (s_speed_submit_fail_count == 0U) &&
        !app_remote_servo_has_pending())
    {
        s_comm_state = REMOTE_COMM_OK;
        s_speed_submit_fail_count = 0U;
        s_last_comm_ok_tick = xTaskGetTickCount();
    }
    }
    /* Runtime expiry and per-item failures are decided by the device snapshot. */
    /*
     * ARMING阶段正在依次完成模式配置和使能，参与控制的轴此时
     * 必然可能处于失能或忙状态，不能提前套用RUNNING阶段的就绪门禁。
     * 否则K5长按后的第一轮服务会把正常授权流程误判为伺服状态故障。
     */
    if ((s_state != REMOTE_SERVO_ARMING) &&
        (!aimtor_boot_zero_ready() || !app_config_motion_hardware_ready() ||
         !app_remote_servo_axes_ready(&left, &right)))
    {
        /* 分轴检查门禁，保留明确的异常来源。 */
        s_failure_axis_mask = 0U;
        for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
        {
            if (!app_remote_servo_axis_selected((servo_axis_t)axis_index))
            {
                continue;
            }
            snapshot = axis_index == SERVO_AXIS_LEFT ? &left : &right;
            if (!snapshot->parameter_verified || !snapshot->enabled ||
                snapshot->stop_latched || snapshot->fault ||
                (snapshot->busy && !s_pending_speed_sequence[axis_index]) ||
                snapshot->control_mode != SERVO_CONTROL_RS485_VELOCITY)
            {
                s_failure_axis_mask |= (uint8_t)(1U << axis_index);
            }
        }
        app_remote_servo_enter_estop(REMOTE_STOP_SERVO_STATE);
        return;
    }
    /* Normal and reversing deceleration integrate at service cadence, before bus waits. */
    if (!s_reverse_wait && !(s_motion_direction && s_target_rpm &&
        (s_target_rpm > 0 ? 1 : -1) != s_motion_direction) &&
        !(s_motion_ramp.current_rpm * (float)s_target_rpm < 0.0f) &&
        s_axis_commanded_rpm[0] * (int32_t)s_target_rpm >= 0 &&
        s_axis_commanded_rpm[1] * (int32_t)s_target_rpm >= 0 &&
        (!s_speed_batch_active || s_speed_batch_target_rpm * (int32_t)s_target_rpm >= 0))
    {
        uint32_t dt;
        int16_t value;
        s_normal_reversing = 0U;
        dt = app_normal_ramp_elapsed(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount()));
        value = app_motion_ramp_step(&s_motion_ramp, s_target_rpm, dt,
            APP_DESIGN_ACCELERATION_MPS2, APP_DESIGN_DECELERATION_MPS2);
        if (aimtor_feedback_pair_degraded(&left, &right, app_config_servo_control_axis_mask()) ||
            s_comm_state == REMOTE_COMM_DEGRADED) {
            for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; ++axis_index)
                if (app_remote_servo_axis_selected((servo_axis_t)axis_index))
                    value = aimtor_feedback_limit_rpm(1U, value, s_axis_commanded_rpm[axis_index]);
            app_motion_ramp_reset(&s_motion_ramp, value);
        }
    }
    else if (s_reverse_wait) {
        /* Real-zero confirmation never accumulates time for the new direction. */
        app_motion_ramp_reset(&s_motion_ramp, 0);
        app_normal_ramp_boundary(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount()));
    }
    else {
        if (!s_normal_reversing) {
            int16_t anchor = s_commanded_rpm;
            s_normal_reversing = 1U;
            if (s_speed_batch_active && app_remote_servo_abs_rpm(s_speed_batch_target_rpm) > app_remote_servo_abs_rpm(anchor))
                anchor = s_speed_batch_target_rpm;
            app_motion_ramp_reset(&s_motion_ramp, anchor);
            app_normal_ramp_boundary(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount()));
        }
        (void)app_motion_ramp_step(&s_motion_ramp, 0,
            app_normal_ramp_elapsed(&s_normal_clock, app_remote_servo_tick_ms(xTaskGetTickCount())),
            APP_DESIGN_ACCELERATION_MPS2, APP_DESIGN_DECELERATION_MPS2);
    }
    /*
     * 单轴模式必须等待当前速度事务完成；双轴模式允许一个轴的速度
     * 事务在等待应答时，继续提交另一个尚未提交的轴。
     */
    if ((app_config_servo_control_axis_count() == 1U) &&
        (pending || left.busy || right.busy))
    {
        return;
    }
    if ((app_config_servo_control_axis_count() > 1U) &&
        !pending && (left.busy || right.busy))
    {
        return;
    }
    now = xTaskGetTickCount();
    elapsed_update_ms = app_remote_servo_tick_ms(now - s_last_speed_update_tick);
    if (elapsed_update_ms < APP_REMOTE_SERVO_UPDATE_PERIOD_MS)
    {
        return;
    }
    if (!s_speed_batch_active) s_last_speed_update_tick = now;
    /*
     * 双轴一个批次必须使用同一个目标值。第一轴已经成功而第二轴
     * 尚未提交时，暂时锁定本批次目标，避免摇杆变化造成两轴目标不一致。
     */
    next_rpm = s_speed_batch_active ? s_speed_batch_target_rpm :
        app_motion_ramp_round_rpm(s_motion_ramp.current_rpm);
    if (aimtor_feedback_pair_degraded(&left, &right, app_config_servo_control_axis_mask()))
    {
        for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; ++axis_index)
            if (app_remote_servo_axis_selected((servo_axis_t)axis_index))
                next_rpm = aimtor_feedback_limit_rpm(1U, next_rpm, s_axis_commanded_rpm[axis_index]);
        app_motion_ramp_reset(&s_motion_ramp, next_rpm);
        if (s_speed_batch_active) s_speed_batch_target_rpm = next_rpm;
    }
    if (app_remote_servo_sync_enabled())
    {
        /* 活动批次锁存两轴最终指令，不消费新配对或重算补偿。 */
        if (!s_speed_batch_active)
        {
            int16_t target = s_target_rpm;
            int16_t correction;
            int16_t limit;
            uint8_t mask = 0U;
            uint8_t reversing = target != 0 && s_motion_direction != 0 &&
                ((target > 0 ? 1 : -1) != s_motion_direction);
            if (reversing || s_reverse_wait)
                target = 0;
            app_remote_sync_update(&s_sync, &left, &right, next_rpm,
                s_authorized && aimtor_boot_zero_ready(), now);
            if (s_sync.trip)
            {
                app_remote_servo_enter_estop(REMOTE_STOP_SYNC_ERROR);
                return;
            }
            if (s_target_rpm == 0 || reversing || s_reverse_wait)
            {
                s_sync.applied_correction = 0.0f;
                s_sync.integer_correction = 0;
                s_sync.active = 0U;
            }
            if ((reversing || s_reverse_wait) && s_commanded_rpm == 0 &&
                s_axis_commanded_rpm[0] == 0 && s_axis_commanded_rpm[1] == 0 &&
                s_confirmed_mask == APP_SERVO_CONTROL_AXIS_BOTH)
            {
                if (!s_reverse_wait)
                {
                    s_reverse_wait = 1U;
                    s_reverse_tick = now;
                    s_sync.zero_pairs = 0U;
                }
                s_sync.state = REMOTE_SYNC_REVERSING;
                if (s_sync.zero_pairs >= 2U && s_sync.new_pair)
                {
                    s_reverse_wait = 0U;
                    s_motion_direction = 0;
                    s_normal_reversing = 0U;
                    app_normal_ramp_boundary(&s_normal_clock, app_remote_servo_tick_ms(now));
                    /* 本轮仍为零，下一轮才放行新的方向。 */
                }
                else if ((now - s_reverse_tick) >=
                         pdMS_TO_TICKS(APP_REMOTE_SERVO_SYNC_REVERSE_MS))
                {
                    app_remote_servo_enter_estop(REMOTE_STOP_REVERSE_TIMEOUT);
                    return;
                }
            }
            correction = s_sync.integer_correction;
            limit = (int16_t)(APP_OPERATION_MAX_MOTOR_RPM -
                (next_rpm < 0 ? -next_rpm : next_rpm));
            if (correction > limit) correction = limit;
            if (correction < -limit) correction = (int16_t)-limit;
            s_batch_command[0] = (int16_t)(next_rpm - correction);
            s_batch_command[1] = (int16_t)(next_rpm + correction);
            for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
            {
                if (!(s_confirmed_mask & (1U << axis_index)) ||
                    s_axis_commanded_rpm[axis_index] != s_batch_command[axis_index])
                    mask |= (uint8_t)(1U << axis_index);
            }
            if (!mask)
            {
                s_commanded_rpm = next_rpm;
                s_state = next_rpm == 0 ? REMOTE_SERVO_ARMED : REMOTE_SERVO_RUNNING;
                return;
            }
            s_speed_batch_active = 1U;
            s_speed_batch_target_rpm = next_rpm;
            s_speed_batch_tick = now;
            s_batch_required_mask = mask;
            s_speed_batch_success_mask = (uint8_t)(APP_SERVO_CONTROL_AXIS_BOTH & ~mask);
            s_batch_written_mask = 0U;
            s_batch_correction = correction;
            s_batch_generation = s_authorization_generation;
            s_batch_sample_sequence[0] = s_sync.sequence[0];
            s_batch_sample_sequence[1] = s_sync.sequence[1];
            s_batch_id++;
            if (next_rpm != 0)
                s_motion_direction = next_rpm > 0 ? 1 : -1;
        }
        next_rpm = s_speed_batch_target_rpm;
    }
    if (!app_remote_servo_sync_enabled() && !s_speed_batch_active &&
        next_rpm == s_commanded_rpm)
    {
        s_state = next_rpm == 0 ? REMOTE_SERVO_ARMED :
                                  REMOTE_SERVO_RUNNING;
        return;
    }

    /*
     * USART2只能串行发送；双轴每次从上次结束位置继续轮转。
     * submitted用于区分“本轮没有可提交轴”和“本轮提交失败”，
     * 避免沿用上一次调用留下的结果。
     */
    submitted = 0U;
    result = SERVO_BUSY;
        for (axis_index = 0U; axis_index < SERVO_AXIS_COUNT; axis_index++)
        {
            candidate = (uint8_t)((s_speed_axis_turn + axis_index) %
                                  SERVO_AXIS_COUNT);
            if (app_remote_servo_axis_selected((servo_axis_t)candidate) &&
                s_pending_speed_sequence[candidate] == 0U &&
                ((app_config_servo_control_axis_count() == 1U) ||
                 (s_speed_batch_active == 0U) ||
                 ((s_speed_batch_success_mask &
                   (uint8_t)(1U << candidate)) == 0U)))
            {
            snapshot = candidate == SERVO_AXIS_LEFT ? &left : &right;
            if (aimtor_feedback_pair_degraded(&left, &right, app_config_servo_control_axis_mask()))
            {
                s_batch_command[candidate] = aimtor_feedback_limit_rpm(1U,
                    s_batch_command[candidate], s_axis_commanded_rpm[candidate]);
                app_motion_ramp_reset(&s_motion_ramp, next_rpm);
            }
            result = aimtor_set_velocity_rpm((servo_axis_t)candidate,
                app_remote_servo_sync_enabled() ? s_batch_command[candidate] : next_rpm);
            /*
             * USART2严格串行；当前轮到的轴可能仍有其他事务占用总线。
             * 双轴模式下继续尝试另一轴，避免一轴短暂忙导致另一轴
             * 长时间没有速度给定。单轴模式保持原有结果处理。
             */
            if ((result == SERVO_BUSY) &&
                (app_config_servo_control_axis_count() > 1U))
            {
                continue;
            }
            s_last_result = result;
            submitted = 1U;
            if (result == SERVO_OK)
            {
                s_pending_speed_sequence[candidate] =
                    snapshot->speed_write_sequence + 1U;
                s_pending_speed_rpm[candidate] = app_remote_servo_sync_enabled() ?
                    s_batch_command[candidate] : next_rpm;
                s_batch_written_mask |= (uint8_t)(1U << candidate);
                if (app_config_servo_control_axis_count() == 1U) s_speed_batch_tick = now;
                if (app_config_servo_control_axis_count() > 1U)
                {
                    if (s_speed_batch_active == 0U)
                    {
                        s_speed_batch_active = 1U;
                        s_speed_batch_success_mask = 0U;
                        s_speed_batch_target_rpm = next_rpm;
                        s_speed_batch_tick = xTaskGetTickCount();
                    }
                    s_speed_axis_turn = (uint8_t)((candidate + 1U) %
                                                  SERVO_AXIS_COUNT);
                }
            }
            break;
        }
    }
    if (submitted == 0U)
    {
        /* 当前轴仍有未完成事务，本轮不产生新的提交结果。 */
        return;
    }
    if (result == SERVO_OK)
    {
        s_state = next_rpm == 0 ? REMOTE_SERVO_ARMED :
                                  REMOTE_SERVO_RUNNING;
    }
    else if (result != SERVO_BUSY)
    {
        s_comm_state = REMOTE_COMM_DEGRADED;
        if (s_speed_submit_fail_count < 0xFFFFU)
        {
            s_speed_submit_fail_count++;
        }
        if (s_speed_submit_fail_count >= APP_REMOTE_SERVO_COMM_FAILURE_LIMIT)
        {
            s_failure_axis_mask = (uint8_t)(1U << candidate);
            s_comm_state = REMOTE_COMM_FAILED;
            app_remote_servo_enter_estop(REMOTE_STOP_SPEED_SUBMIT);
        }
    }
}

void app_remote_servo_motion_service(void)
{
    TickType_t age;
    uint8_t link;
    if (aimtor_safety_stop_active()) return;

    taskENTER_CRITICAL();
    age = xTaskGetTickCount() - s_input_tick;
    link = s_input.link_up;
    if (s_input.age_ms >= APP_REMOTE_SERVO_LINK_TIMEOUT_MS ||
        age >= pdMS_TO_TICKS(APP_REMOTE_SERVO_LINK_TIMEOUT_MS -
              (s_input.age_ms < APP_REMOTE_SERVO_LINK_TIMEOUT_MS ?
               s_input.age_ms : APP_REMOTE_SERVO_LINK_TIMEOUT_MS)))
    {
        link = 0U;
    }
    taskEXIT_CRITICAL();
    if ((s_state == REMOTE_SERVO_ARMING) ||
        (s_state == REMOTE_SERVO_STOPPING) ||
        (s_state == REMOTE_SERVO_ESTOP) ||
        ((s_state != REMOTE_SERVO_DISABLED) && !link))
    {
        (void)motion_stop(MOTION_STOP_IMMEDIATE);
    }
}

bool app_remote_servo_get_status(remote_servo_status_t *status)
{
    servo_snapshot_t left;
    servo_snapshot_t right;

    if (status == NULL)
    {
        return false;
    }
    if (!aimtor_get_snapshot_pair(&left, &right))
    {
        return false;
    }
    app_remote_servo_clear_unselected_snapshot(SERVO_AXIS_LEFT, &left);
    app_remote_servo_clear_unselected_snapshot(SERVO_AXIS_RIGHT, &right);
    taskENTER_CRITICAL();
    status->state = s_state;
    status->authorized = s_authorized;
    status->hold_stopping = s_hold_stopping;
    (void)aimtor_k1_hold_get_status(&status->k1_hold);
    status->estop_latched = s_estop_latched;
    status->link_valid = s_link_valid;
    status->lv = s_lv;
    status->target_rpm = s_target_rpm;
    status->commanded_rpm = s_commanded_rpm;
    status->actual_rpm = app_remote_servo_axis_selected(SERVO_AXIS_RIGHT) ?
                         right.actual_speed_rpm : left.actual_speed_rpm;
    status->control_axis_mask = app_config_servo_control_axis_mask();
    /* 各轴下发值仅在收到成功应答后更新，不把排队目标当成已生效值。 */
    status->left_commanded_rpm = s_axis_commanded_rpm[SERVO_AXIS_LEFT];
    status->right_commanded_rpm = s_axis_commanded_rpm[SERVO_AXIS_RIGHT];
    if (!app_remote_servo_axis_selected(SERVO_AXIS_LEFT))
    {
        status->left_commanded_rpm = 0;
    }
    if (!app_remote_servo_axis_selected(SERVO_AXIS_RIGHT))
    {
        status->right_commanded_rpm = 0;
    }
    status->left_actual_rpm = left.actual_speed_rpm;
    status->right_actual_rpm = right.actual_speed_rpm;
    status->left_result = left.last_result;
    status->right_result = right.last_result;
    status->left_fault = left.fault_code;
    status->right_fault = right.fault_code;
    status->left_speed_age_ms = left.speed_age_ms;
    status->right_speed_age_ms = right.speed_age_ms;
    status->left_verified = left.parameter_verified;
    status->right_verified = right.parameter_verified;
    status->left_enabled = left.enabled;
    status->right_enabled = right.enabled;
    status->left_busy = left.busy;
    status->right_busy = right.busy;
    status->left_latched = left.stop_latched;
    status->right_latched = right.stop_latched;
    status->batch_active = s_speed_batch_active;
    status->batch_success_mask = s_speed_batch_success_mask;
    status->batch_required_mask = app_remote_servo_sync_enabled() ?
        s_batch_required_mask : app_config_servo_control_axis_mask();
    status->batch_response_gap_valid = s_batch_gap_valid;
    status->batch_id = s_batch_id;
    status->authorization_generation = s_authorization_generation;
    status->batch_generation = s_batch_generation;
    status->batch_sample_sequence[0] = s_batch_sample_sequence[0];
    status->batch_sample_sequence[1] = s_batch_sample_sequence[1];
    status->batch_base_rpm = s_speed_batch_target_rpm;
    status->batch_command[0] = s_batch_command[0];
    status->batch_command[1] = s_batch_command[1];
    status->batch_correction = s_batch_correction;
    status->sync = s_sync;
    status->batch_age_ms = s_speed_batch_active ? (uint32_t)(
        (uint64_t)(TickType_t)(xTaskGetTickCount() - s_speed_batch_tick) *
        1000U / configTICK_RATE_HZ) : 0U;
    status->batch_response_gap_ms = s_batch_response_gap_ms;
    status->stop_axis_mask = s_stop_axis_mask;
    status->last_frame_age_ms = s_last_frame_age_ms;
    status->arm_count = s_arm_count;
    status->estop_count = s_estop_count;
    status->link_loss_estop_count = s_link_loss_estop_count;
    status->last_result = s_last_result;
    status->stop_reason = s_stop_reason;
    status->stop_link_age_ms = s_stop_link_age_ms;
    status->stop_speed_age_ms = s_stop_snapshot.speed_age_ms;
    status->stop_fault_age_ms = s_stop_snapshot.fault_age_ms;
    status->stop_poll_result = s_stop_snapshot.last_poll_result;
    status->stop_poll_address = s_stop_snapshot.last_poll_address;
    status->stop_fault_code = s_stop_snapshot.fault_code;
    status->stop_transaction_result = s_stop_snapshot.last_result;
    status->comm_state = s_comm_state;
    status->comm_fail_count = s_comm_fail_count;
    status->speed_submit_fail_count = s_speed_submit_fail_count;
    status->last_comm_ok_age_ms = (uint32_t)(((uint64_t)(
        xTaskGetTickCount() - s_last_comm_ok_tick) * 1000ULL) /
        configTICK_RATE_HZ);
    status->controlled_stop_state = s_controlled_stop_state;
    status->controlled_stop_reason = s_controlled_stop_reason;
    status->stop_deceleration_mps2 = s_stop_deceleration;
    status->stop_generation = s_stop_generation;
    status->stop_elapsed_ms = s_stop_complete_time_valid ? s_stop_complete_ms :
        s_controlled_stop_state == REMOTE_CONTROLLED_STOP_IDLE ? 0U :
        app_remote_servo_tick_ms(xTaskGetTickCount() - s_hold_stop_tick);
    status->stop_timeout_ms = s_stop_timeout_ms;
    status->stop_batch_command[0] = s_hold_step_command[0];
    status->stop_batch_command[1] = s_hold_step_command[1];
    status->stop_zero_mask = (uint8_t)(
        (s_hold_zero_count[0] >= 2U ? APP_SERVO_CONTROL_AXIS_LEFT : 0U) |
        (s_hold_zero_count[1] >= 2U ? APP_SERVO_CONTROL_AXIS_RIGHT : 0U));
    status->stop_first_command_valid = s_stop_first_command_valid;
    status->stop_zero_time_valid = s_stop_zero_time_valid;
    status->stop_complete_time_valid = s_stop_complete_time_valid;
    status->stop_first_command_ms = s_stop_first_command_ms;
    status->stop_zero_ms = s_stop_zero_ms;
    status->stop_complete_ms = s_stop_complete_ms;
    taskEXIT_CRITICAL();
    return true;
}

bool app_remote_servo_is_active(void)
{
    return s_state != REMOTE_SERVO_DISABLED;
}

bool app_remote_servo_k1_latched(void)
{
    bool latched;
    taskENTER_CRITICAL();
    latched = s_estop_latched && s_stop_reason == REMOTE_STOP_K1;
    taskEXIT_CRITICAL();
    return latched;
}

void app_remote_servo_can_k5_stop_begin(uint32_t event, uint8_t axes)
{
    if (!s_estop_latched || s_stop_reason != REMOTE_STOP_K1 ||
        app_control_arbiter_owner() != APP_CONTROL_OWNER_CAN ||
        axes != app_config_servo_control_axis_mask()) return;
    s_can_k5_event = event;
    s_can_k5_axes = axes;
}

bool app_remote_servo_can_k5_stop_complete(uint32_t event, uint8_t axes)
{
    servo_k1_hold_status_t hold;
    bool current;
    if (s_can_k5_axes == 0U || event != s_can_k5_event || axes != s_can_k5_axes ||
        !s_estop_latched || s_stop_reason != REMOTE_STOP_K1 || s_authorized ||
        s_controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED ||
        app_control_arbiter_owner() != APP_CONTROL_OWNER_CAN ||
        app_control_arbiter_lock_reason() != APP_CONTROL_LOCK_NONE ||
        app_health_watchdog_test_requested() || s_stop_requested ||
        !aimtor_k1_hold_get_status(&hold) || !hold.valid ||
        hold.state != SERVO_K1_HOLD_READY || hold.axis_mask != axes ||
        hold.zero_mask != axes || hold.enabled_mask != axes) return false;
    taskENTER_CRITICAL();
    current = s_input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX] == s_short_count &&
        s_input.recovery_generation == s_radio_generation && !s_stop_requested;
    if (current)
    {
        s_estop_latched = 0U;
        s_state = REMOTE_SERVO_DISABLED;
        s_can_k5_axes = 0U;
    }
    taskEXIT_CRITICAL();
    return current;
}

const char *app_remote_servo_state_name(remote_servo_state_t state)
{
    if (state == REMOTE_SERVO_STOPPING) return "stopping";
    if (state == REMOTE_SERVO_ARMING)
    {
        return "arming";
    }
    if (state == REMOTE_SERVO_ARMED)
    {
        return "armed";
    }
    if (state == REMOTE_SERVO_RUNNING)
    {
        return "running";
    }
    if (state == REMOTE_SERVO_ESTOP)
    {
        return "estop";
    }
    return "disabled";
}

const char *app_remote_servo_controlled_stop_state_name(remote_controlled_stop_state_t state)
{
    switch (state)
    {
        case REMOTE_CONTROLLED_STOP_DECELERATING: return "decelerating";
        case REMOTE_CONTROLLED_STOP_VERIFYING: return "verifying";
        case REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING: return "hold_confirming";
        case REMOTE_CONTROLLED_STOP_DISABLING: return "disabling";
        case REMOTE_CONTROLLED_STOP_DONE: return "done";
        case REMOTE_CONTROLLED_STOP_FAILED: return "failed";
        default: return "idle";
    }
}

const char *app_remote_servo_stop_reason_name(remote_stop_reason_t reason)
{
    switch (reason)
    {
        case REMOTE_STOP_K1: return "k5";
        case REMOTE_STOP_LINK: return "link_timeout";
        case REMOTE_STOP_MANUAL: return "manual";
        case REMOTE_STOP_ARMING: return "arming";
        case REMOTE_STOP_PREPARE: return "prepare";
        case REMOTE_STOP_ENABLE: return "enable";
        case REMOTE_STOP_SERVO_STATE: return "servo_state";
        case REMOTE_STOP_TRANSACTION: return "transaction";
        case REMOTE_STOP_SPEED_SUBMIT: return "speed_submit";
        case REMOTE_STOP_SYNC_ERROR: return "sync_error";
        case REMOTE_STOP_REVERSE_TIMEOUT: return "reverse_timeout";
        default: return "none";
    }
}

void app_remote_servo_ramp_status(app_normal_ramp_status_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL();
    memset(out, 0, sizeof(*out)); out->clock = s_normal_clock;
    out->target[0] = out->target[1] = s_target_rpm;
    out->internal[0] = out->internal[1] = app_motion_ramp_round_rpm(s_motion_ramp.current_rpm);
    out->batch[0] = app_remote_servo_sync_enabled() ? s_batch_command[0] : s_speed_batch_target_rpm;
    out->batch[1] = app_remote_servo_sync_enabled() ? s_batch_command[1] : s_speed_batch_target_rpm;
    out->authorized = s_authorized; out->phase = (uint8_t)s_state;
    taskEXIT_CRITICAL();
}
