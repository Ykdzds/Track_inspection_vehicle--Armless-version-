#include "app_can_control.h"
#include "app_config.h"
#include "app_motion_ramp.h"
#include "app_normal_ramp.h"
#include "app_control_arbiter.h"
#include "app_health.h"
#include "app_cli_servo_session.h"
#include "app_hmi_control.h"
#include "app_remote_servo.h"
#include "can_protocol.h"
#include "main.h"
#include "task.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static app_can_control_status_t s_public;
static app_normal_ramp_clock_t s_normal_clock;

double app_can_control_rpm_mm_s(int16_t rpm, uint8_t axis)
{
    int sign = axis == 0U ? APP_POSITION_LEFT_SIGN : APP_POSITION_RIGHT_SIGN;
    return (double)app_motion_ramp_motor_rpm_to_vehicle_mm_s_f(
        (int32_t)rpm * sign);
}

bool app_can_control_config_valid(void)
{
    double a = APP_CAN_NORMAL_STOP_DECEL_MPS2, b = APP_CAN_LINK_STOP_DECEL_MPS2;
    double quick = APP_CAN_QUICK_STOP_DECEL_MPS2;
    double speed = app_can_control_limit();
    return app_config_motion_ramp_valid() &&
        isfinite(a) && isfinite(b) && isfinite(quick) &&
        isfinite((double)APP_CAN_ACCELERATION_MPS2) && (double)APP_CAN_ACCELERATION_MPS2 > 0.0 &&
        isfinite((double)APP_CAN_DECELERATION_MPS2) && (double)APP_CAN_DECELERATION_MPS2 > 0.0 &&
        isfinite((double)APP_CAN_REVERSE_DECEL_MPS2) && (double)APP_CAN_REVERSE_DECEL_MPS2 > 0.0 &&
        (!APP_CAN_QUICK_STOP_ENABLED || (quick >= a && quick * 1000.0 <= 65534.0 &&
        ceil(speed / (quick * 1000.0) * 1000.0) + APP_CAN_STOP_CONFIRM_MARGIN_MS <= APP_CAN_STOP_TIMEOUT_MAX_MS)) &&
        a > 0.0 && a * 1000.0 <= 65534.0 && b > 0.0 && b * 1000.0 <= 65534.0 &&
        ceil(speed / (a * 1000.0) * 1000.0) + APP_CAN_STOP_CONFIRM_MARGIN_MS <= APP_CAN_STOP_TIMEOUT_MAX_MS &&
        ceil(speed / (b * 1000.0) * 1000.0) + APP_CAN_STOP_CONFIRM_MARGIN_MS <= APP_CAN_STOP_TIMEOUT_MAX_MS;
}

uint16_t app_can_control_limit(void)
{
    double limit = (double)APP_OPERATION_MAX_VELOCITY_MM_S;
    unsigned axis;
    for (axis = 0U; axis < 2U; ++axis)
        if ((APP_SERVO_CONTROL_AXIS & (1U << axis)) != 0U)
        {
            double v = fabs(app_can_control_rpm_mm_s(
                (int16_t)APP_OPERATION_MAX_MOTOR_RPM, (uint8_t)axis));
            if (v < limit) limit = v;
        }
    if (limit > 32767.0) limit = 32767.0;
    return limit > 0.0 ? (uint16_t)limit : 0U;
}

#if APP_CAN_CONTROL_ENABLED
typedef struct { uint8_t data[8]; uint32_t received, generation, ticket, control_generation, stop_event; bool defer_acquire; } control_request_t;
typedef struct { uint16_t token; uint32_t generation, boot, heartbeat; bool ready, strategy; } control_link_t;
static control_request_t s_requests[8], s_active;
static control_request_t s_priority;
static bool s_priority_pending;
static uint8_t s_qread, s_qwrite, s_qcount;
static app_can_control_result_t s_results[32];
static uint8_t s_rread, s_rwrite, s_rcount;
static app_can_control_status_t s;
static control_link_t s_link, s_incoming_link;
static uint8_t s_speed[8], s_stop[8], s_last_speed[8], s_last_stop[8];
static uint32_t s_speed_received, s_stop_received, s_granted, s_batch_tick, s_ramp_tick, s_stop_tick;
static uint32_t s_stop_clock_tick;
static uint32_t s_accepted_speed_ms, s_accepted_stop_generation, s_accepted_speed_generation;
static uint16_t s_accepted_lease;
static uint32_t s_rx_rejected, s_rx_duplicates, s_rx_conflicts, s_rx_full;
static uint16_t s_rx_detail;
static uint32_t s_zero_floor[2], s_zero_sequence[2], s_reverse_tick;
static uint16_t s_next_token, s_token_limit, s_lease;
static uint8_t s_zero_count[2], s_batch_mask, s_batch_done, s_batch_submitted;
static bool s_speed_pending, s_stop_pending, s_safety, s_have_speed, s_have_stop;
static bool s_active_valid, s_batch_active, s_batch_stop, s_force_zero, s_reverse;
static uint8_t s_active_phase;
static bool s_stop_started, s_reverse_zero;
static uint32_t s_authorized_link, s_authorized_boot;
static uint8_t s_axis_turn;
static double s_stop_rpm[2];
static bool s_stop_ramp_initialized;
static int16_t s_target_rpm[2];
static app_motion_ramp_t s_motion_ramp[2];
static servo_snapshot_t s_feedback[2], s_normalized[2];
static remote_servo_status_t s_other_remote;
static cli_servo_session_status_t s_other_cli;
static uint32_t s_other_tick;
static uint8_t s_other_mask, s_other_zero[2];
static uint32_t s_other_sequence[2];
static bool s_other_active;
static void other_stop_start(app_control_owner_t owner, uint32_t received, bool quick,
    uint32_t sequence, uint16_t command);

static void publish(void)
{
    s.sample_ms = HAL_GetTick();
    taskENTER_CRITICAL();
    s_public = s;
    s_public.rejected += s_rx_rejected; s_public.duplicates += s_rx_duplicates;
    s_public.conflicts += s_rx_conflicts; s_public.queue_full += s_rx_full;
    if (s_rx_detail) s_public.detail = s_rx_detail;
    taskEXIT_CRITICAL();
}

static void result(uint8_t code, uint8_t phase, uint16_t detail)
{
    if (!s_active_valid) return;
    if (code == 0U && phase == 2U && s_active_phase == 2U) return;
    s_active_phase = phase;
    taskENTER_CRITICAL();
    if (s_rcount < 32U)
    {
        app_can_control_result_t *r = &s_results[s_rwrite];
        r->sequence = s_active.data[0]; r->command = s_active.data[1];
        r->link_token = can_protocol_read_u16(s_active.data + 6U);
        r->link_generation = s_active.generation; r->result = code;
        r->phase = phase; r->detail = detail;
        s_rwrite = (uint8_t)((s_rwrite + 1U) % 32U); ++s_rcount;
    }
    else { ++s.queue_full; s_safety = true; }
    taskEXIT_CRITICAL();
    if (phase >= 3U || code != 0U)
    {
        if (code != 0U)
        {
            app_control_arbiter_cancel(s_active.ticket);
            if (s.state == CAN_CONTROL_PREPARING) s.state = CAN_CONTROL_IDLE;
        }
        s_active_valid = false;
    }
}

static bool fresh(const servo_snapshot_t *a)
{
    return a->online && a->speed_valid && a->fault_valid && !a->fault && !a->fault_code &&
        a->speed_age_ms < APP_SERVO_STATUS_MAX_AGE_MS && a->fault_age_ms < APP_SERVO_STATUS_MAX_AGE_MS;
}

static bool feedback(void)
{
    unsigned axis;
    if (!aimtor_get_snapshot_pair(&s_feedback[0], &s_feedback[1])) return false;
    for (axis = 0U; axis < 2U; ++axis)
        if ((s.axis_mask & (1U << axis)) && (!fresh(&s_feedback[axis]) ||
            !s_feedback[axis].enabled || s_feedback[axis].stop_latched ||
            !s_feedback[axis].parameter_verified ||
            s_feedback[axis].control_mode != SERVO_CONTROL_RS485_VELOCITY)) return false;
    return true;
}

static void fail(uint8_t code, uint8_t reason)
{
    if (code == 9U || code == 10U) aimtor_request_controlled_safety_stop(1U);
    app_control_arbiter_finish_stop(APP_CONTROL_OWNER_CAN, 5U, code, 0U, 0U);
    app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
    s.motion_token = 0U; s.authorized = false; s.target_mm_s = 0;
    s.stop_phase = 5U; s.stop_result = code; s.stop_reason = reason;
    s.state = CAN_CONTROL_FAULT; s.actual_valid = false;
    s_stop_started = s_batch_active = false; s.pending_mask = 0U;
    aimtor_can_revoke(++s.generation); aimtor_set_can_control(false, 0U);
    app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
    if (!aimtor_safety_stop_active()) aimtor_emergency_stop();
    result(code, 4U, reason); publish();
}

static void begin_stop_rate(uint8_t reason, double deceleration)
{
    unsigned axis; double maximum = 0.0;
    app_control_stop_t event;
    if (app_control_arbiter_owner() != APP_CONTROL_OWNER_CAN) return;
    (void)app_control_arbiter_submit_global_stop(reason == 1U ?
        APP_CONTROL_OWNER_REMOTE : APP_CONTROL_OWNER_CAN, reason == 1U ? 5U : reason,
        (uint16_t)(deceleration * 1000.0 + 0.5), 0U, 0x10U);
    app_control_arbiter_get_stop(&event);
    if (s_stop_started)
    {
        if (deceleration > s.stop_deceleration) s.stop_deceleration = (float)deceleration;
        if (reason == 1U)
        {
            s.stop_reason = reason;
            app_remote_servo_can_k5_stop_begin(s.stop_generation, s.axis_mask);
        }
        return;
    }
    s_stop_started = true; s_reverse = false;
    app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
    memset(s_motion_ramp, 0, sizeof(s_motion_ramp));
    s.stop_external = false; s.stop_owner = APP_CONTROL_OWNER_CAN;
    s.motion_token = 0U; s.authorized = false; s.target_mm_s = 0;
    s.stop_reason = reason; s.stop_phase = 1U; s.stop_result = 0U;
    s.actual_valid = false; s.sync.active = 0U; s.sync.integer_correction = 0;
    s.stop_generation = event.event; ++s.generation; s_stop_tick = event.started_ms;
    s_stop_clock_tick = HAL_GetTick();
    if (reason == 1U) app_remote_servo_can_k5_stop_begin(s.stop_generation, s.axis_mask);
    s.first_command_ms = s.zero_ms = s.complete_ms = UINT32_MAX;
    s.stop_deceleration = (float)deceleration;
    app_control_arbiter_stop(APP_CONTROL_OWNER_CAN);
    aimtor_can_revoke(s.generation);
    if (!feedback()) { fail(9U, 6U); return; }
    for (axis = 0U; axis < 2U; ++axis)
        if (s.axis_mask & (1U << axis))
        {
            double v = fabs(app_can_control_rpm_mm_s(s.commanded_rpm[axis], (uint8_t)axis));
            double actual = fabs(app_can_control_rpm_mm_s(s_feedback[axis].actual_speed_rpm, (uint8_t)axis));
            double pending = fabs(app_can_control_rpm_mm_s(s.pending_rpm[axis], (uint8_t)axis));
            if (actual > v) v = actual;
            if (s.pending_mask & (1U << axis)) if (pending > v) v = pending;
            if (v > maximum) maximum = v;
            /* Keep the ramp state in motor rpm.  v above is vehicle mm/s and
             * is used only for the timeout budget. */
            s_stop_rpm[axis] = fabs((double)s.commanded_rpm[axis]);
            if (fabs((double)s_feedback[axis].actual_speed_rpm) > fabs(s_stop_rpm[axis]))
                s_stop_rpm[axis] = fabs((double)s_feedback[axis].actual_speed_rpm);
            if (s.pending_mask & (1U << axis) &&
                fabs((double)s.pending_rpm[axis]) > fabs(s_stop_rpm[axis]))
                s_stop_rpm[axis] = fabs((double)s.pending_rpm[axis]);
            if (s.commanded_rpm[axis] < 0 || s_feedback[axis].actual_speed_rpm < 0 || s.pending_rpm[axis] < 0)
                s_stop_rpm[axis] = -fabs(s_stop_rpm[axis]);
        }
    s_stop_ramp_initialized = true;
    /* Include a bounded service-cadence allowance for the first drain and
     * the final quantized zero write; this is separate from the 2 s feedback
     * confirmation margin and avoids declaring timeout while the monotonic
     * ramp is still issuing its last zero command. */
    s.stop_timeout_ms = (uint32_t)ceil(maximum / ((double)s.stop_deceleration * 1000.0) * 1000.0) +
        APP_CAN_STOP_CONFIRM_MARGIN_MS + 500U;
    app_control_arbiter_stop_timeout(APP_CONTROL_OWNER_CAN, s.stop_timeout_ms);
    if (s.stop_timeout_ms > APP_CAN_STOP_TIMEOUT_MAX_MS) { fail(10U, 7U); return; }
    s.state = CAN_CONTROL_DRAIN; s_force_zero = true;
    memset(s_zero_count, 0, sizeof(s_zero_count));
    taskENTER_CRITICAL(); s_speed_pending = false; taskEXIT_CRITICAL();
    publish();
}

static void begin_stop(uint8_t reason)
{
    begin_stop_rate(reason, reason == 1U ? APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 :
        reason == 2U || reason == 9U ? APP_CAN_LINK_STOP_DECEL_MPS2 : APP_CAN_NORMAL_STOP_DECEL_MPS2);
}

static bool consume_batch(void)
{
    unsigned axis;
    if (!s_batch_active) return true;
    for (axis = 0U; axis < 2U; ++axis)
    {
        servo_snapshot_t *a = &s_feedback[axis]; uint8_t bit = (uint8_t)(1U << axis);
        if (!(s.pending_mask & bit)) continue;
        if (a->speed_write_completed != s.write_sequence[axis] || a->busy) continue;
        s.pending_mask &= (uint8_t)~bit;
        if (a->speed_write_result != SERVO_OK)
        {
            if (s.state == CAN_CONTROL_DRAIN && a->speed_write_result == SERVO_NOT_READY)
            { s_batch_done |= bit; continue; }
            s.failed_mask |= bit; fail(9U, 7U); return false;
        }
        s.commanded_rpm[axis] = a->speed_write_rpm_valid ? a->speed_write_rpm : s.pending_rpm[axis];
        if (s.commanded_rpm[axis] != s.pending_rpm[axis])
            app_motion_ramp_reset(&s_motion_ramp[axis], s.commanded_rpm[axis]);
        s.pending_rpm[axis] = s.commanded_rpm[axis]; s_batch_done |= bit;
        if (s_batch_stop && s.pending_rpm[axis] == 0)
        { s_zero_floor[axis] = a->speed_sequence; s_zero_sequence[axis] = a->speed_sequence; }
    }
    if (s_batch_done == s_batch_mask)
    { if (!s_batch_stop) app_normal_ramp_batch_done(&s_normal_clock, HAL_GetTick() - s_batch_tick);
      s_batch_active = false; return true; }
    if ((uint32_t)(HAL_GetTick() - s_batch_tick) >= APP_CAN_WRITE_CONFIRM_TIMEOUT_MS)
    { fail(10U, 7U); return false; }
    return false;
}

static void submit_batch(void)
{
    unsigned i;
    if (!s_batch_active) return;
    for (i = 0U; i < 2U; ++i)
    {
        uint8_t axis = (uint8_t)((s_axis_turn + i) % 2U), bit = (uint8_t)(1U << axis);
        servo_result_t r; servo_snapshot_t a;
        if (!(s_batch_mask & bit) || (s_batch_submitted & bit)) continue;
        if (!s_batch_stop && aimtor_feedback_pair_degraded(&s_feedback[0], &s_feedback[1], s.axis_mask))
        {
            s.pending_rpm[axis] = aimtor_feedback_limit_rpm(1U, s.pending_rpm[axis], s.commanded_rpm[axis]);
            app_motion_ramp_reset(&s_motion_ramp[axis], s.pending_rpm[axis]);
        }
        r = aimtor_can_velocity((servo_axis_t)axis, s.pending_rpm[axis], s.generation, s.batch, s_batch_stop);
        if (r == SERVO_BUSY) continue;
        if (r != SERVO_OK) { s.failed_mask |= bit; fail(9U, 7U); return; }
        if (!aimtor_get_snapshot((servo_axis_t)axis, &a)) { fail(12U, 7U); return; }
        s.write_sequence[axis] = a.speed_write_sequence;
        s.pending_mask |= bit; s_batch_submitted |= bit;
        s_axis_turn = (uint8_t)(1U - axis);
        if (s_batch_stop && s.first_command_ms == UINT32_MAX)
            s.first_command_ms = HAL_GetTick() - s_stop_tick;
    }
}

static void start_batch(bool stop)
{
    s_batch_active = true; s_batch_stop = stop; s_batch_mask = s.axis_mask;
    s_batch_done = s_batch_submitted = s.pending_mask = 0U;
    s_batch_tick = HAL_GetTick(); ++s.batch;
    submit_batch();
}

static int16_t mm_rpm(int16_t speed, uint8_t axis)
{
    double rpm = speed / app_can_control_rpm_mm_s(1, axis);
    if (rpm > 32767.0) rpm = 32767.0;
    if (rpm < -32767.0) rpm = -32767.0;
    return (int16_t)rpm;
}

static void synchronize(void)
{
    unsigned axis, j;
    servo_sync_policy_t policy;
    double unit = fabs(app_can_control_rpm_mm_s(1, 0U));
    if (aimtor_feedback_pair_degraded(&s_feedback[0], &s_feedback[1], s.axis_mask))
    { s.sync.active = s.sync.trip = 0U; s.sync.integer_correction = 0; s.sync.applied_correction = 0.0f;
      s.sync.over_limit = 0U; s.sync.over_limit_duration_ms = 0U;
      s.sync.calculation_tick = xTaskGetTickCount(); return; }
    app_servo_sync_policy_default(&policy);
    policy.maximum_rpm = (float)(app_can_control_limit() / unit);
    if (s.axis_mask != 3U) { s.sync.enabled = s.sync.active = 0U; return; }
    s_normalized[0] = s_feedback[0]; s_normalized[1] = s_feedback[1];
    for (axis = 0U; axis < 2U; ++axis)
    {
        double scale = app_can_control_rpm_mm_s(1, (uint8_t)axis) / unit;
        double value = s_feedback[axis].actual_speed_rpm * scale;
        if (value > 32767.0 || value < -32767.0) { fail(2U, 6U); return; }
        s_normalized[axis].actual_speed_rpm = (int16_t)value;
        for (j = 0U; j < SERVO_SPEED_SAMPLE_CAPACITY; ++j)
        {
            value = s_feedback[axis].speed_samples[j].actual_speed_rpm * scale;
            if (value > 32767.0 || value < -32767.0)
            { s_normalized[axis].speed_samples[j].valid = 0U; continue; }
            s_normalized[axis].speed_samples[j].actual_speed_rpm =
                (int16_t)value;
        }
    }
    app_remote_sync_collect(&s.sync, &s_normalized[0], &s_normalized[1], xTaskGetTickCount());
    if ((uint32_t)((uint64_t)(TickType_t)(xTaskGetTickCount() - s.sync.calculation_tick) * 1000U / configTICK_RATE_HZ) >= APP_REMOTE_SERVO_SYNC_PERIOD_MS)
        app_servo_sync_update(&s.sync, &s_normalized[0], &s_normalized[1],
            (int16_t)(s.target_mm_s / unit), true,
            APP_REMOTE_SERVO_SYNC_ENABLED && s.authorized && s.first_zero_done && !s_reverse,
            xTaskGetTickCount(), &policy);
    if (!s.sync.pair_valid) { s.sync.active = 0U; s.sync.applied_correction = 0.0f; s.sync.integer_correction = 0; }
    if (s.sync.trip) { fail(9U, 6U); return; }
    s.actual_valid = s.sync.pair_valid != 0U;
    if (s.actual_valid)
    {
        unsigned a, k; int16_t paired[2] = {0, 0}; bool found[2] = {false, false};
        for (a = 0U; a < 2U; ++a) for (k = 0U; k < SERVO_SPEED_SAMPLE_CAPACITY; ++k)
            if (s_feedback[a].speed_samples[k].valid &&
                s_feedback[a].speed_samples[k].speed_sequence == s.sync.sequence[a])
            { paired[a] = s_feedback[a].speed_samples[k].actual_speed_rpm; found[a] = true; }
        s.actual_valid = found[0] && found[1];
        if (!s.actual_valid) { s.sync.pair_valid = s.sync.active = 0U; }
        if (s.actual_valid) s.actual_mm_s = (int16_t)((app_can_control_rpm_mm_s(paired[0], 0U) +
            app_can_control_rpm_mm_s(paired[1], 1U)) / 2.0);
    }
}

static bool zero_feedback(void)
{
    unsigned axis; bool all = true; s.zero_mask = 0U;
    for (axis = 0U; axis < 2U; ++axis)
    {
        servo_snapshot_t *a = &s_feedback[axis];
        if (!(s.axis_mask & (1U << axis))) continue;
        if (!fresh(a) || a->actual_speed_rpm != 0 || a->busy)
            s_zero_count[axis] = 0U;
        else if ((int32_t)(a->speed_sequence - s_zero_floor[axis]) > 0 &&
            a->speed_sequence != s_zero_sequence[axis])
        {
            s_zero_sequence[axis] = a->speed_sequence;
            if (s_zero_count[axis] < 2U) ++s_zero_count[axis];
        }
        if (s_zero_count[axis] < 2U) all = false;
        else s.zero_mask |= (uint8_t)(1U << axis);
    }
    return all;
}

static void control_request(void)
{
    uint8_t command; int16_t p; uint32_t now = HAL_GetTick();
    taskENTER_CRITICAL();
    if (s_priority_pending)
    {
        if (s_active_valid) result(7U, 4U, 0U);
        s_active = s_priority; s_priority_pending = false; s_active_valid = true; s_active_phase = 0U;
    }
    taskEXIT_CRITICAL();
    if (!s_active_valid)
    {
        taskENTER_CRITICAL();
        if (s_qcount) { s_active = s_requests[s_qread]; s_qread = (uint8_t)((s_qread + 1U) % 8U); --s_qcount; s_active_valid = true; s_active_phase = 0U; }
        taskEXIT_CRITICAL();
    }
    if (!s_active_valid) return;
    now = HAL_GetTick();
    command = s_active.data[1]; p = (int16_t)can_protocol_read_u16(s_active.data + 2U);
    if (command == 9U)
    {
        servo_recovery_status_t recovery;
        aimtor_get_recovery_status(&recovery);
        if (s_active.generation == s_link.generation && s_link.ready &&
            can_protocol_read_u32(s_active.data + 2U) == recovery.generation &&
            can_protocol_read_u16(s_active.data + 6U) == s_link.token &&
            aimtor_request_recovery(4U) == SERVO_OK)
            result(0U, 1U, 0U);
        else result(4U, 4U, 0U);
        return;
    }
    if (s_active.generation != s_link.generation || !s_link.ready ||
        can_protocol_read_u16(s_active.data + 6U) != s_link.token)
    { result(7U, 4U, 0U); return; }
    if ((uint32_t)(now - s_active.received) >= APP_CAN_ACQUIRE_TIMEOUT_MS && command != 0x05U)
    { result(10U, 4U, 0U); return; }
    if (command == 4U && p == 1)
    {
        int8_t granted;
        if (!s_link.strategy) { result(1U, 4U, 13U); return; }
        if (s_other_active) return;
        if (s.authorized || s_stop_started) { result(5U, 4U, 0U); return; }
        if (s_active.defer_acquire)
        {
            s_active.defer_acquire = false;
            s_active.ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_CAN, s.axis_mask);
            if (s_active.ticket) return;
        }
        if (app_health_watchdog_test_requested() || app_remote_servo_k1_latched() ||
            !app_config_motion_hardware_ready() || !app_config_servo_bus_ready() ||
            !aimtor_watchdog_recovery_ready() || !aimtor_startup_stop_ready() || !aimtor_boot_zero_ready() ||
            !app_can_control_config_valid()) { result(4U, 4U, 0U); return; }
        if (!s_active.ticket) { result(5U,4U,0U); return; }
        granted = app_control_arbiter_poll(s_active.ticket, &s.generation);
        if (granted == 0) return;
        if (granted < 0 || app_control_arbiter_owner() != APP_CONTROL_OWNER_CAN)
        { result(5U,4U,0U); return; }
        if (!aimtor_get_snapshot_pair(&s_feedback[0], &s_feedback[1]))
        { result(4U, 4U, 0U); return; }
        {
            unsigned axis;
            for (axis = 0U; axis < 2U; ++axis)
            {
                servo_snapshot_t *a = &s_feedback[axis];
                servo_result_t prepared;
                if (!(s.axis_mask & (1U << axis))) continue;
                if (!fresh(a) || a->feedback_degraded || a->actual_speed_rpm || a->stop_latched)
                { result(4U, 4U, 0U); return; }
                if (a->busy) { s.state = CAN_CONTROL_PREPARING; return; }
                if (!a->parameter_verified || a->control_mode != SERVO_CONTROL_RS485_VELOCITY)
                {
                    prepared = aimtor_remote_prepare_mode((servo_axis_t)axis);
                    if (prepared != SERVO_OK && prepared != SERVO_BUSY) result(4U,4U,0U);
                    else s.state = CAN_CONTROL_PREPARING;
                    return;
                }
            }
            /* Configure every participating axis before enabling either axis. */
            for (axis = 0U; axis < 2U; ++axis)
                if ((s.axis_mask & (1U << axis)) && !s_feedback[axis].enabled)
                {
                    servo_result_t prepared = aimtor_remote_prepare_enable((servo_axis_t)axis);
                    if (prepared != SERVO_OK && prepared != SERVO_BUSY) result(4U,4U,0U);
                    else s.state = CAN_CONTROL_PREPARING;
                    return;
                }
        }
        { unsigned axis; for (axis = 0U; axis < 2U; ++axis)
            if ((s.axis_mask & (1U << axis)) && (!s_feedback[axis].enable_feedback_valid ||
                !s_feedback[axis].enable_feedback ||
                (uint32_t)((uint64_t)(TickType_t)(xTaskGetTickCount() - s_feedback[axis].enable_feedback_tick) *
                    1000U / configTICK_RATE_HZ) >= APP_SERVO_STATUS_MAX_AGE_MS))
            { s.state = CAN_CONTROL_PREPARING; return; } }
        if (s_active.ticket == 0U) { result(5U, 4U, 0U); return; }
        s.state = CAN_CONTROL_PREPARING;
        granted = app_control_arbiter_poll(s_active.ticket, &s.generation);
        if (granted == 0) return;
        if (granted < 0 || !app_control_arbiter_request(APP_CONTROL_OWNER_CAN, s.axis_mask, &s.generation))
        { s.state = CAN_CONTROL_IDLE; result(5U, 4U, 0U); return; }
        {
            servo_k1_hold_status_t hold;
            if (!aimtor_k1_hold_get_status(&hold))
            {
                app_control_arbiter_release(APP_CONTROL_OWNER_CAN);
                s.state = CAN_CONTROL_IDLE; result(4U, 4U, 6U); return;
            }
            if (hold.state != SERVO_K1_HOLD_INACTIVE)
            {
                if (!hold.valid || hold.zero_mask != hold.axis_mask ||
                    aimtor_k1_hold_release() != SERVO_OK)
                {
                    app_control_arbiter_release(APP_CONTROL_OWNER_CAN);
                    s.state = CAN_CONTROL_IDLE;
                    result(4U, 4U, 6U);
                    return;
                }
            }
        }
        if (s_next_token == 0U || s_next_token > s_token_limit)
        { app_control_arbiter_release(APP_CONTROL_OWNER_CAN); s.state = CAN_CONTROL_IDLE; result(12U, 4U, 0U); return; }
        s.motion_token = s_next_token++; ++s.grants;
        s.limit_mm_s = app_can_control_limit();
        s.authorized = true; s.first_zero_done = s.speed_seen = false;
        app_normal_ramp_begin(&s_normal_clock, now);
        memset(s_motion_ramp, 0, sizeof(s_motion_ramp)); s_ramp_tick = now;
        s.stop_phase = 0U; s.stop_result = 255U; s.work_mode = 2U;
        s.stop_external = false;
        s.state = CAN_CONTROL_FIRST_ZERO; s_granted = now;
        s_authorized_link = s_link.generation; s_authorized_boot = s_link.boot;
        taskENTER_CRITICAL(); s_have_speed = s_speed_pending = false; taskEXIT_CRITICAL();
        s_force_zero = false;
        aimtor_set_can_control(true, s.axis_mask);
        app_remote_sync_reset(&s.sync, &s_feedback[0], &s_feedback[1], xTaskGetTickCount());
        result(0U, 3U, 0U); return;
    }
    if ((command == 4U && p == 0) || command == 5U)
    {
        if (command == 5U && p == 1 && s_active.stop_event && s.stop_external &&
            s_active.stop_event == s.stop_generation)
        {
            if (s_other_active) { result(0U, 2U, 0U); return; }
            if (s.stop_phase == 4U) { result(0U, 3U, 0U); return; }
            if (s.stop_phase == 5U) { result(s.stop_result, 4U, 0U); return; }
        }
        if (s_active.control_generation != app_control_arbiter_generation() && !s_stop_started)
        { result(7U, 4U, 0U); return; }
        if (app_control_arbiter_owner() != APP_CONTROL_OWNER_CAN)
        { result(command == 4U ? 0U : 3U, 3U, 0U); return; }
        begin_stop_rate(5U, command == 5U && p == 1 ? APP_CAN_QUICK_STOP_DECEL_MPS2 : APP_CAN_NORMAL_STOP_DECEL_MPS2);
        if (s_stop_started) result(0U, 2U, 0U);
        if (s.state == CAN_CONTROL_DONE) result(0U, 3U, 0U);
        return;
    }
    if (command == 6U)
    {
        if (!s.authorized) { result(3U, 4U, 0U); return; }
        if ((uint16_t)p > s.limit_mm_s || p <= 0) { result(2U, 4U, 0U); return; }
        s.limit_mm_s = (uint16_t)p;
        if (abs((int)s.target_mm_s) > p) begin_stop(5U);
        result(0U, 3U, 0U); return;
    }
    if (command == 7U)
    {
        if (app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE || app_control_arbiter_lock_reason() != APP_CONTROL_LOCK_NONE)
        { result(5U, 4U, 0U); return; }
        s.work_mode = 2U; result(0U, 3U, 0U); return;
    }
    result(1U, 4U, 13U);
}

void app_can_control_init(const can_identity_t *identity)
{
    memset(&s, 0, sizeof(s)); memset(&s_link, 0, sizeof(s_link));
    memset(&s_incoming_link, 0, sizeof(s_incoming_link));
    s_rx_rejected = s_rx_duplicates = s_rx_conflicts = s_rx_full = s_rx_detail = 0U;
    s_accepted_speed_ms = s_accepted_speed_generation = s_accepted_stop_generation = 0U;
    s_accepted_lease = 0U;
    s.stop_result = 255U; s.limit_mm_s = app_can_control_limit();
    s.axis_mask = APP_SERVO_CONTROL_AXIS;
    s_next_token = identity && identity->result == CAN_IDENTITY_OK ? identity->motion_first : 0U;
    s_token_limit = identity ? identity->motion_last : 0U;
    s_qread = s_qwrite = s_qcount = s_rread = s_rwrite = s_rcount = 0U;
    s_speed_pending = s_stop_pending = s_safety = s_have_speed = s_have_stop = false;
    s_active_valid = s_batch_active = s_stop_started = s_reverse = s_reverse_zero = s_priority_pending = false;
    s_stop_ramp_initialized = false;
    s_force_zero = s_other_active = false; s_axis_turn = 0U;
    app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
    memset(s_motion_ramp, 0, sizeof(s_motion_ramp));
    publish();
}

void app_can_control_recovery_reset(void)
{
    uint16_t next = s_next_token, limit = s_token_limit;
    control_link_t link = s_incoming_link;
    app_can_control_init(NULL);
    s_next_token = next; s_token_limit = limit;
    s_link = s_incoming_link = link;
}

void app_can_control_link(uint16_t token, uint32_t generation, uint32_t boot,
                          uint32_t heartbeat_ms, bool ready, bool strategy)
{
    taskENTER_CRITICAL();
    if (s_incoming_link.generation != generation) s_have_stop = false;
    s_incoming_link.token = token; s_incoming_link.generation = generation; s_incoming_link.boot = boot;
    s_incoming_link.heartbeat = heartbeat_ms; s_incoming_link.ready = ready; s_incoming_link.strategy = strategy;
    taskEXIT_CRITICAL();
}

uint8_t app_can_control_submit(const uint8_t request[8], uint32_t received_ms, uint32_t generation)
{
    uint8_t command = request[1]; int16_t p = (int16_t)can_protocol_read_u16(request + 2U);
    control_request_t *q;
    if (aimtor_recovery_blocks_motion() && command != 9U) return 4U;
    if (can_protocol_read_u16(request + 4U) != 0U ||
        (command == 4U && p != 0 && p != 1) ||
        (command == 5U && p != 0 && (p != 1 || !APP_CAN_QUICK_STOP_ENABLED)) ||
        (command == 6U && p <= 0) || (command == 7U && p != 2)) return 2U;
    if ((command < 4U || command > 7U) && command != 9U) return 1U;
    taskENTER_CRITICAL();
    if (command == 5U || command == 9U || (command == 4U && p == 0))
    {
        if (s_priority_pending) { taskEXIT_CRITICAL(); return 5U; }
        memcpy(s_priority.data, request, 8U); s_priority.received = received_ms;
        s_priority.stop_event = 0U;
        s_priority.defer_acquire = false;
        s_priority.generation = generation; s_priority.ticket = 0U;
        s_priority.control_generation = app_control_arbiter_generation(); s_priority_pending = true;
        taskEXIT_CRITICAL(); return 0U;
    }
    if (s_qcount == 8U) { ++s_rx_full; taskEXIT_CRITICAL(); return 5U; }
    q = &s_requests[s_qwrite]; memcpy(q->data, request, 8U);
    q->received = received_ms; q->generation = generation; q->ticket = 0U;
    q->stop_event = 0U;
    q->defer_acquire = command == 4U && p == 1 && s_other_active;
    q->control_generation = app_control_arbiter_generation();
    if (command == 4U && p == 1 && !q->defer_acquire && s_incoming_link.ready && s_incoming_link.strategy)
        q->ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_CAN, APP_SERVO_CONTROL_AXIS);
    s_qwrite = (uint8_t)((s_qwrite + 1U) % 8U); ++s_qcount;
    taskEXIT_CRITICAL(); return 0U;
}

bool app_can_control_speed(const uint8_t data[8], uint32_t received_ms)
{
    uint16_t seq = can_protocol_read_u16(data), lease = can_protocol_read_u16(data + 6U);
    int16_t target = (int16_t)can_protocol_read_u16(data + 2U);
    bool ok = false;
    taskENTER_CRITICAL();
    if (aimtor_recovery_blocks_motion() || !s_public.authorized || !s_incoming_link.ready || !s_incoming_link.strategy ||
        s_incoming_link.generation != s_authorized_link || s_incoming_link.boot != s_authorized_boot ||
        (s_have_speed && (uint32_t)(HAL_GetTick() - s_accepted_speed_ms) >= s_accepted_lease) ||
        (uint32_t)(HAL_GetTick() - s_incoming_link.heartbeat) >= APP_CAN_LINK_TIMEOUT_MS ||
        can_protocol_read_u16(data + 4U) != s_public.motion_token ||
        lease < 100U || lease > APP_CAN_SPEED_TIMEOUT_MS ||
        abs((int)target) > s_public.limit_mm_s || (!APP_CAN_CONTROL_ALLOW_REVERSE && target < 0) ||
        (!s_public.first_zero_done && target != 0)) s_rx_detail = 2U;
    else if (s_have_speed && seq == can_protocol_read_u16(s_last_speed))
    {
        if (memcmp(data, s_last_speed, 8U) == 0) ++s_rx_duplicates;
        else { ++s_rx_conflicts; s_rx_detail = 14U; }
    }
    else if (s_have_speed && !can_protocol_sequence_newer(seq, can_protocol_read_u16(s_last_speed))) s_rx_detail = 6U;
    else if ((uint32_t)(HAL_GetTick() - received_ms) >= lease) s_rx_detail = 10U;
    else
    {
        memcpy(s_speed, data, 8U); memcpy(s_last_speed, data, 8U);
        s_have_speed = s_speed_pending = true; s_accepted_speed_ms = received_ms;
        s_accepted_lease = lease; s_accepted_speed_generation = s_public.generation; ok = true; s_rx_detail = 0U;
    }
    if (!ok) ++s_rx_rejected;
    taskEXIT_CRITICAL(); return ok;
}

bool app_can_control_stop(const uint8_t data[8], uint32_t received_ms)
{
    uint16_t seq = can_protocol_read_u16(data); bool ok = false;
    taskENTER_CRITICAL();
    if (s_incoming_link.token == 0U || can_protocol_read_u16(data + 2U) != s_incoming_link.token ||
        (data[4] != 0U && (data[4] != 1U || !APP_CAN_QUICK_STOP_ENABLED)) ||
        data[5] > 3U || data[6] || data[7]) s_rx_detail = 2U;
    else if (s_have_stop && seq == can_protocol_read_u16(s_last_stop))
    {
        if (memcmp(data, s_last_stop, 8U) == 0) ++s_rx_duplicates;
        else { ++s_rx_conflicts; s_rx_detail = 14U; }
    }
    else if (s_have_stop && !can_protocol_sequence_newer(seq, can_protocol_read_u16(s_last_stop))) s_rx_detail = 6U;
    else { uint8_t strategy = s_stop_pending && s_stop[4] > data[4] ? s_stop[4] : data[4];
        memcpy(s_stop, data, 8U); s_stop[4] = strategy; memcpy(s_last_stop, data, 8U); s_have_stop = s_stop_pending = true;
        s_stop_received = received_ms; s_accepted_stop_generation = app_control_arbiter_generation(); ok = true; s_rx_detail = 0U; }
    if (!ok) ++s_rx_rejected;
    taskEXIT_CRITICAL(); return ok;
}

void app_can_control_safety(void) { taskENTER_CRITICAL(); s_safety = true; taskEXIT_CRITICAL(); }

static void other_stop_start(app_control_owner_t owner, uint32_t received, bool quick,
    uint32_t sequence, uint16_t command)
{
    unsigned axis;
    app_control_stop_t event;
    if (quick && owner != APP_CONTROL_OWNER_NONE)
        (void)app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_CAN, 5U,
            (uint16_t)((double)APP_CAN_QUICK_STOP_DECEL_MPS2 * 1000.0 + 0.5),
            sequence, command);
    if (s_other_active)
    {
        if (quick) s.stop_deceleration = APP_CAN_QUICK_STOP_DECEL_MPS2;
        return;
    }
    app_control_arbiter_stop(owner);
    s_other_active = true; s_other_tick = received;
    s_other_mask = app_control_arbiter_axis_mask();
    if (!s_other_mask) s_other_mask = APP_SERVO_CONTROL_AXIS;
    s.stop_owner = (uint8_t)owner; s.stop_external = true; ++s.stop_generation;
    app_control_arbiter_get_stop(&event);
    if (quick && event.owner == owner && event.result == 2U)
    {
        s.stop_generation = event.event;
        s_other_tick = event.started_ms;
    }
    s.zero_mask = 0U;
    s.stop_deceleration = quick ? APP_CAN_QUICK_STOP_DECEL_MPS2 : 0.0f;
    s.stop_phase = 1U; s.stop_result = 0U; s.stop_reason = 5U;
    if (!aimtor_get_snapshot_pair(&s_feedback[0], &s_feedback[1]))
    {
        s.stop_phase = 5U; s.stop_result = 12U; s_other_active = false;
        app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
        aimtor_request_controlled_safety_stop(1U);
        if (!aimtor_safety_stop_active()) aimtor_emergency_stop();
        return;
    }
    s.stop_timeout_ms = APP_CAN_STOP_TIMEOUT_MAX_MS;
    s.first_command_ms = s.zero_ms = s.complete_ms = UINT32_MAX;
    memset(s_other_zero, 0, sizeof(s_other_zero));
    for (axis = 0U; axis < 2U; ++axis) s_other_sequence[axis] = s_feedback[axis].speed_sequence;
    if (!quick)
    {
        if (owner == APP_CONTROL_OWNER_REMOTE) app_remote_servo_request_estop();
        else if (owner == APP_CONTROL_OWNER_CLI) (void)app_cli_servo_session_off();
        else if (owner == APP_CONTROL_OWNER_HMI) app_hmi_control_request_stop();
    }
}

static void other_stop_service(uint32_t now)
{
    unsigned axis; bool complete = true, failed = false;
    if (!s_other_active) return;
    if (s.stop_deceleration > 0.0f && s.stop_owner != APP_CONTROL_OWNER_NONE)
    {
        app_control_stop_t event;
        app_control_arbiter_get_stop(&event);
        if (event.event != s.stop_generation || event.owner != s.stop_owner ||
            (event.result != 2U && event.result != 3U)) failed = true;
        if (event.timeout_ms) s.stop_timeout_ms = event.timeout_ms;
        if (event.result != 3U || event.error || event.zero_mask != s_other_mask ||
            event.enabled_mask != s_other_mask) complete = false;
        else
            for (axis = 0U; axis < 2U; ++axis)
                if (s_other_mask & (1U << axis)) s_other_zero[axis] = 2U;
    }
    if (app_control_arbiter_lock_reason() != APP_CONTROL_LOCK_NONE) failed = true;
    s.stop_elapsed_ms = now - s_other_tick;
    if (!aimtor_get_snapshot_pair(&s_feedback[0], &s_feedback[1])) failed = true;
    for (axis = 0U; axis < 2U; ++axis)
    {
        servo_snapshot_t *a = &s_feedback[axis];
        if (!(s_other_mask & (1U << axis))) continue;
        if (!fresh(a) || a->actual_speed_rpm != 0 || a->busy) s_other_zero[axis] = 0U;
        else if (a->speed_sequence != s_other_sequence[axis])
        { if (s_other_zero[axis] < 2U) ++s_other_zero[axis]; }
        s_other_sequence[axis] = a->speed_sequence;
        if (!fresh(a)) failed = true;
        if (s.stop_deceleration > 0.0f && (!a->enable_feedback_valid ||
            (uint32_t)((uint64_t)(TickType_t)(xTaskGetTickCount() - a->enable_feedback_tick) *
                1000U / configTICK_RATE_HZ) >= APP_SERVO_STATUS_MAX_AGE_MS ||
            (a->control_mode == SERVO_CONTROL_RS485_VELOCITY && (!a->enabled || !a->enable_feedback))))
            complete = false;
        if (s_other_zero[axis] < 2U) { complete = false; s.zero_mask &= (uint8_t)~(1U << axis); }
        else s.zero_mask |= (uint8_t)(1U << axis);
    }
    if (s.stop_owner == APP_CONTROL_OWNER_REMOTE)
    {
        if (!app_remote_servo_get_status(&s_other_remote)) failed = true;
        if (s_other_remote.controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED) failed = true;
        if (s_other_remote.authorized || s_other_remote.hold_stopping ||
            (s_other_remote.controlled_stop_state != REMOTE_CONTROLLED_STOP_DONE &&
             s_other_remote.state != REMOTE_SERVO_ESTOP)) complete = false;
    }
    else if (s.stop_owner == APP_CONTROL_OWNER_CLI)
    {
        if (!app_cli_servo_session_get_status(&s_other_cli) || s_other_cli.state == CLI_SESSION_LOCKED) failed = true;
        if (s_other_cli.state != CLI_SESSION_IDLE) complete = false;
    }
    else if (s.stop_owner == APP_CONTROL_OWNER_HMI && app_control_arbiter_owner() == APP_CONTROL_OWNER_HMI) complete = false;
    if (app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE) complete = false;

    if (failed || s.stop_elapsed_ms >= s.stop_timeout_ms)
    {
        s.stop_phase = 5U; s.stop_result = failed ? 9U : 10U; s_other_active = false;
        app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
        aimtor_request_controlled_safety_stop(1U);
        if (!aimtor_safety_stop_active()) aimtor_emergency_stop();
    }
    else if (complete)
    {
        s.stop_phase = 4U; s.stop_result = 0U; s.zero_ms = s.complete_ms = s.stop_elapsed_ms; s_other_active = false;
        if (s.stop_owner == APP_CONTROL_OWNER_NONE) app_control_arbiter_release(APP_CONTROL_OWNER_NONE);
    }
    else s.stop_phase = 2U;
}

void app_can_control_pre_service(void)
{
    uint32_t now = HAL_GetTick(), stop_generation = 0U; bool emergency, stop, priority;
    app_control_stop_t arbiter_stop;
    bool arbiter_stop_ready = false;
    uint8_t stop_data[8]; uint32_t event_ms;
    control_request_t request;
    if (aimtor_safety_stop_active() || aimtor_recovery_blocks_motion())
    {
        if (s_safety || app_health_watchdog_test_requested())
        {
            aimtor_emergency_stop();
            s_safety = false;
            fail(4U, 1U);
            return;
        }
        if ((s.authorized || s_stop_started) && !aimtor_recovery_blocks_motion()) fail(9U, 6U);
        return;
    }
    taskENTER_CRITICAL();
    s_link = s_incoming_link; emergency = s_safety; s_safety = false;
    stop = s_stop_pending; s_stop_pending = false;
    memcpy(stop_data, s_stop, sizeof(stop_data));
    stop_generation = s_accepted_stop_generation; event_ms = s_stop_received;
    priority = s_priority_pending; request = s_priority;
    taskEXIT_CRITICAL();
    if (app_control_arbiter_owner() == APP_CONTROL_OWNER_CAN)
        arbiter_stop_ready = app_control_arbiter_take_stop(APP_CONTROL_OWNER_CAN, &arbiter_stop);
    if (arbiter_stop_ready)
    {
        begin_stop_rate(arbiter_stop.requester == APP_CONTROL_OWNER_REMOTE ? 1U : 5U,
            (double)arbiter_stop.deceleration_mm_s2 / 1000.0);
    }
    /* 先作废运动权限，再让底层事务调度；不能等到普通请求处理阶段。 */
    if (stop && stop_generation == app_control_arbiter_generation() &&
        s_link.token != 0U && can_protocol_read_u16(stop_data + 2U) == s_link.token)
    {
        s.stop_seen = true; s.stop_sequence = can_protocol_read_u16(stop_data);
        s_other_active = s_other_active && s.stop_phase >= 1U && s.stop_phase <= 3U;
        if (s_active_valid && s_active.data[1] == 4U && can_protocol_read_u16(s_active.data + 2U) == 1U)
        { app_control_arbiter_cancel(s_active.ticket); result(7U, 4U, 0U); }
        taskENTER_CRITICAL();
        while (s_qcount)
        {
            app_control_arbiter_cancel(s_requests[s_qread].ticket);
            s_qread = (uint8_t)((s_qread + 1U) % 8U); --s_qcount;
        }
        taskEXIT_CRITICAL();
        if (app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE)
            app_control_arbiter_cancel_pending();
        if (app_control_arbiter_owner() == APP_CONTROL_OWNER_CAN)
        { begin_stop_rate(5U, stop_data[4] == 1U ? APP_CAN_QUICK_STOP_DECEL_MPS2 : APP_CAN_NORMAL_STOP_DECEL_MPS2); }
        else other_stop_start(app_control_arbiter_owner(), event_ms, stop_data[4] == 1U,
            s.stop_sequence, 0x080U);
    }
    if (priority && request.control_generation == app_control_arbiter_generation() &&
        request.generation == s_link.generation && s_link.ready &&
        can_protocol_read_u16(request.data + 6U) == s_link.token)
    {
        bool quick = request.data[1] == 5U &&
            can_protocol_read_u16(request.data + 2U) == 1U;
        if (app_control_arbiter_owner() == APP_CONTROL_OWNER_CAN)
        {
            begin_stop_rate(5U, quick ? APP_CAN_QUICK_STOP_DECEL_MPS2 :
                APP_CAN_NORMAL_STOP_DECEL_MPS2);
        }
        else if (quick)
        {
            other_stop_start(app_control_arbiter_owner(), request.received, quick,
                request.data[0], 0x110U);
            taskENTER_CRITICAL();
            if (s_priority_pending && s_priority.received == request.received &&
                s_priority.generation == request.generation &&
                memcmp(s_priority.data, request.data, 8U) == 0)
                s_priority.stop_event = s.stop_generation;
            taskEXIT_CRITICAL();
        }
    }
    now = HAL_GetTick();
    other_stop_service(now);
    if (s.state == CAN_CONTROL_FAULT) return;
    if (app_control_arbiter_owner() != APP_CONTROL_OWNER_CAN)
    {
        if (s.authorized || s_stop_started) fail(4U, 1U);
        return;
    }
    if (emergency || app_health_watchdog_test_requested() ||
        app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_SAFETY)
    { fail(4U, 1U); return; }
    if (app_remote_servo_k1_latched()) begin_stop(1U);
    if ((s.authorized || s_stop_started) && !feedback()) { fail(9U, 6U); return; }
    if (s.authorized && (!s_link.ready || !s_link.strategy || s_link.generation != s_authorized_link ||
        s_link.boot != s_authorized_boot || (uint32_t)(now - s_link.heartbeat) >= APP_CAN_LINK_TIMEOUT_MS)) begin_stop(2U);
    if (s.authorized && !s.first_zero_done && (uint32_t)(now - s_granted) >= APP_CAN_FIRST_SPEED_TIMEOUT_MS) begin_stop(9U);
    taskENTER_CRITICAL();
    now = HAL_GetTick();
    emergency = s_have_speed && (uint32_t)(now - s_accepted_speed_ms) >= s_accepted_lease;
    taskEXIT_CRITICAL();
    if (s.authorized && emergency) begin_stop(9U);
}

void app_can_control_service(void)
{
    uint32_t now = HAL_GetTick(), dt, speed_generation, received; unsigned axis; bool speed, moving = false;
    uint8_t speed_data[8];
    app_can_control_pre_service();
    if (aimtor_safety_stop_active() || aimtor_recovery_blocks_motion()) { publish(); return; }
    other_stop_service(HAL_GetTick());
    taskENTER_CRITICAL(); speed = s_speed_pending; s_speed_pending = false;
    memcpy(speed_data, s_speed, sizeof(speed_data)); received = s_accepted_speed_ms;
    speed_generation = s_accepted_speed_generation; taskEXIT_CRITICAL();
    s.strategy_ready = s_link.strategy && s_link.ready;
    if (!s.authorized && !s_stop_started && !s_active_valid && s_link.generation != s_authorized_link)
    { s.limit_mm_s = app_can_control_limit(); s_authorized_link = s_link.generation; }
    control_request();
    now = HAL_GetTick();
    if (!s.authorized && !s_stop_started) { publish(); return; }
    if (s_stop_started && now - s_stop_tick >= s.stop_timeout_ms) { fail(10U, 7U); return; }
    if (!feedback()) { fail(9U, 6U); return; }
    if (speed && s.authorized && speed_generation == s.generation &&
        (uint32_t)(now - received) < can_protocol_read_u16(speed_data + 6U))
    {
        if (abs((int)(int16_t)can_protocol_read_u16(speed_data + 2U)) > s.limit_mm_s)
        { begin_stop(5U); publish(); return; }
        s.speed_sequence = can_protocol_read_u16(speed_data); s.speed_seen = true;
        s_speed_received = received; s_lease = can_protocol_read_u16(speed_data + 6U);
        if (s.target_mm_s != (int16_t)can_protocol_read_u16(speed_data + 2U))
            app_normal_ramp_boundary(&s_normal_clock, now);
        s.target_mm_s = (int16_t)can_protocol_read_u16(speed_data + 2U);
        for (axis = 0U; axis < 2U; ++axis)
        {
            s_target_rpm[axis] = mm_rpm(s.target_mm_s, (uint8_t)axis);
            if ((s.axis_mask & (1U << axis)) && abs((int)s_target_rpm[axis]) > APP_OPERATION_MAX_MOTOR_RPM)
            { fail(2U, 6U); return; }
        }
        if (!s.first_zero_done) s_force_zero = true;
    }
    s.speed_age_ms = s.speed_seen ? now - s_speed_received : UINT32_MAX;
    s.remaining_ms = !s.authorized ? 0U : !s.first_zero_done ?
        APP_CAN_FIRST_SPEED_TIMEOUT_MS - (now - s_granted) :
        s_lease > s.speed_age_ms ? s_lease - s.speed_age_ms : 0U;
    if (s.authorized)
    {
        uint32_t heartbeat_left = now - s_link.heartbeat < APP_CAN_LINK_TIMEOUT_MS ? APP_CAN_LINK_TIMEOUT_MS - (now - s_link.heartbeat) : 0U;
        if (heartbeat_left < s.remaining_ms) s.remaining_ms = heartbeat_left;
    }

    if (s.axis_mask != 3U)
    {
        axis = s.axis_mask == 1U ? 0U : 1U;
        s.actual_valid = fresh(&s_feedback[axis]);
        s.actual_mm_s = (int16_t)app_can_control_rpm_mm_s(s_feedback[axis].actual_speed_rpm, (uint8_t)axis);
    }
    if (s.state == CAN_CONTROL_FAULT) return;
    if (s.first_zero_done && s.axis_mask == 3U && (s_stop_started || s_reverse)) synchronize();
    if (s.state == CAN_CONTROL_FAULT) return;
    /* Advance only healthy normal motion; keep pending batch words immutable. */
    {
        bool normal = s.authorized && s.first_zero_done && !s_stop_started && !s_reverse;
        for (axis = 0U; axis < 2U; ++axis)
            if ((s.axis_mask & (1U << axis)) &&
                ((s.commanded_rpm[axis] * (int32_t)s_target_rpm[axis] < 0) ||
                 (s_feedback[axis].actual_speed_rpm * (int32_t)s_target_rpm[axis] < 0))) normal = false;
        if (normal) {
            uint32_t normal_dt = app_normal_ramp_elapsed(&s_normal_clock, now);
            for (axis = 0U; axis < 2U; ++axis) {
                int16_t value = app_motion_ramp_step_rate(&s_motion_ramp[axis], s_target_rpm[axis], normal_dt,
                    APP_CAN_ACCELERATION_MPS2, APP_CAN_DECELERATION_MPS2,
                    (float)(1000.0 / fabs(app_can_control_rpm_mm_s(1, (uint8_t)axis))));
                if (aimtor_feedback_pair_degraded(&s_feedback[0], &s_feedback[1], s.axis_mask)) {
                    value = aimtor_feedback_limit_rpm(1U, value, s.commanded_rpm[axis]);
                    app_motion_ramp_reset(&s_motion_ramp[axis], value);
                }
            }
        } else app_normal_ramp_boundary(&s_normal_clock, now);
    }
    /* Advance stop/reversal ramp before consuming an outstanding write. */
    if ((s_stop_started || s_reverse) && s_stop_ramp_initialized &&
        (now - s_stop_clock_tick) >= APP_REMOTE_SERVO_STOP_UPDATE_PERIOD_MS)
    {
        uint32_t clock_dt = now - s_stop_clock_tick;
        s_stop_clock_tick = now;
        for (axis = 0U; axis < 2U; ++axis)
        {
            double step = (s_stop_started ? s.stop_deceleration : APP_CAN_REVERSE_DECEL_MPS2) * clock_dt /
                fabs(app_can_control_rpm_mm_s(1, (uint8_t)axis));
            double value = s_stop_rpm[axis];
            value = value > 0.0 ? value - step : value < 0.0 ? value + step : 0.0;
            if ((s_stop_rpm[axis] > 0.0 && value < 0.0) || (s_stop_rpm[axis] < 0.0 && value > 0.0)) value = 0.0;
            s_stop_rpm[axis] = value;
        }
    }
    if (s_batch_active)
    {
        if (s.state == CAN_CONTROL_DRAIN)
        { s_batch_done |= (uint8_t)(s_batch_mask & ~s_batch_submitted); }
        else submit_batch();
        if (!consume_batch()) { publish(); return; }
        if (s.state == CAN_CONTROL_FAULT) return;
        if (s_force_zero && !s_stop_started && !s_reverse)
        { s.first_zero_done = true; s_force_zero = false; s.state = CAN_CONTROL_READY; }
    }
    if (s.first_zero_done && s.axis_mask == 3U && !s_stop_started && !s_reverse) synchronize();
    if (s.state == CAN_CONTROL_FAULT) return;
    if (s.state == CAN_CONTROL_DRAIN)
    {
        /* Keep the anchor captured in begin_stop(); an in-flight response
         * must not reset the stop clock or restore an older command. */
        s.state = CAN_CONTROL_DECEL; s_ramp_tick = now;
    }
    if (s_stop_started && now - s_stop_tick >= s.stop_timeout_ms) { fail(10U, 7U); return; }
    if (s_stop_started) s.stop_elapsed_ms = now - s_stop_tick;
    if (s.state == CAN_CONTROL_ZERO || (s_reverse && s_reverse_zero))
    {
        if (s_reverse && now - s_reverse_tick >= APP_CAN_REVERSE_CONFIRM_TIMEOUT_MS) { fail(10U, 7U); return; }
        if (!zero_feedback()) { publish(); return; }
        if (s_reverse)
        {
            s_reverse = s_reverse_zero = false;
            app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
            memset(s_motion_ramp, 0, sizeof(s_motion_ramp));
            s.state = CAN_CONTROL_READY; s_ramp_tick = now;
        }
        else { s.zero_ms = now - s_stop_tick; s.stop_phase = 3U; s.state = CAN_CONTROL_HOLD; }
    }
    if (s.state == CAN_CONTROL_HOLD)
    {
        if (!zero_feedback()) { s.state = CAN_CONTROL_ZERO; s.stop_phase = 2U; publish(); return; }
        if (s.stop_reason == 1U)
        {
            servo_k1_hold_status_t hold;
            if (aimtor_k1_hold_request(s.axis_mask) != SERVO_OK) { fail(9U, 6U); return; }
            if (!aimtor_k1_hold_get_status(&hold) || !hold.valid) { publish(); return; }
        }
        for (axis = 0U; axis < 2U; ++axis)
            if ((s.axis_mask & (1U << axis)) && (!s_feedback[axis].enable_feedback_valid ||
                !s_feedback[axis].enable_feedback ||
                (uint32_t)((uint64_t)(TickType_t)(xTaskGetTickCount() - s_feedback[axis].enable_feedback_tick) * 1000U / configTICK_RATE_HZ) >= APP_SERVO_STATUS_MAX_AGE_MS))
            { publish(); return; }
        if (s.stop_reason == 1U &&
            !app_remote_servo_can_k5_stop_complete(s.stop_generation, s.axis_mask))
        { publish(); return; }
        s.stop_phase = 4U; s.stop_result = 0U; s.state = CAN_CONTROL_DONE;
        s.complete_ms = now - s_stop_tick; s_stop_started = false;
        app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
        memset(s_motion_ramp, 0, sizeof(s_motion_ramp));
        app_control_arbiter_finish_stop(APP_CONTROL_OWNER_CAN, 3U, 0U,
            s.zero_mask, s.axis_mask);
        aimtor_set_can_control(false, 0U); app_control_arbiter_release(APP_CONTROL_OWNER_CAN);
        result(0U, 3U, 0U); publish(); return;
    }
    if (s_stop_started || s_reverse)
    {
        bool zero = true;
        /* Account for service time while a write is pending.  The previous
         * path only integrated after consume_batch(), making the physical
         * ramp slower than its timeout budget. */
        dt = now - s_ramp_tick;
        if (dt < APP_REMOTE_SERVO_STOP_UPDATE_PERIOD_MS) { publish(); return; }
        s_ramp_tick = now;
        for (axis = 0U; axis < 2U; ++axis)
        {
            double value = s_stop_rpm[axis];
            s.pending_rpm[axis] = (int16_t)(value > 0.0 ? ceil(value) : floor(value));
            if ((s.axis_mask & (1U << axis)) && s.pending_rpm[axis] != 0) zero = false;
        }
        start_batch(true);
        if (zero)
        {
            if (s_reverse) { s_reverse_zero = true; s_reverse_tick = now; }
            else { s.state = CAN_CONTROL_ZERO; s.stop_phase = 2U; }
        }
        publish(); return;
    }
    if (!s.first_zero_done)
    {
        if (s_force_zero) { s.pending_rpm[0] = s.pending_rpm[1] = 0; start_batch(false); }
        publish(); return;
    }
    for (axis = 0U; axis < 2U; ++axis)
        if ((s.axis_mask & (1U << axis)) &&
            ((s.commanded_rpm[axis] > 0 && s_target_rpm[axis] < 0) ||
             (s.commanded_rpm[axis] < 0 && s_target_rpm[axis] > 0) ||
             (s_feedback[axis].actual_speed_rpm > 0 && s_target_rpm[axis] < 0) ||
             (s_feedback[axis].actual_speed_rpm < 0 && s_target_rpm[axis] > 0)))
        {
            s_reverse = true; s_reverse_zero = false; s.state = CAN_CONTROL_REVERSING;
            memset(s_zero_count, 0, sizeof(s_zero_count));
            s_stop_rpm[0] = s.commanded_rpm[0]; s_stop_rpm[1] = s.commanded_rpm[1];
            s_stop_ramp_initialized = true;
            s_ramp_tick = now; s_stop_clock_tick = now; publish(); return;
        }
    if (s.axis_mask != 3U)
    {
        axis = s.axis_mask == 1U ? 0U : 1U;
        s.actual_valid = fresh(&s_feedback[axis]);
        s.actual_mm_s = (int16_t)app_can_control_rpm_mm_s(s_feedback[axis].actual_speed_rpm, (uint8_t)axis);
    }
    dt = now - s_ramp_tick;
    if (dt < APP_REMOTE_SERVO_UPDATE_PERIOD_MS) { publish(); return; }
    if (dt > 50U) dt = 50U;
    s_ramp_tick = now;
    for (axis = 0U; axis < 2U; ++axis)
    {
        int32_t target = app_motion_ramp_round_rpm(s_motion_ramp[axis].current_rpm);
        int16_t next;
        if (s.sync.active)
        {
            double correction = s.sync.integer_correction * fabs(app_can_control_rpm_mm_s(1, 0U)) /
                app_can_control_rpm_mm_s(1, (uint8_t)axis);
            target += (int32_t)(axis == 0U ? -correction : correction);
            if (target > APP_OPERATION_MAX_MOTOR_RPM) target = APP_OPERATION_MAX_MOTOR_RPM;
            if (target < -APP_OPERATION_MAX_MOTOR_RPM) target = -APP_OPERATION_MAX_MOTOR_RPM;
            if ((s_target_rpm[axis] >= 0 && target < 0) || (s_target_rpm[axis] <= 0 && target > 0)) target = 0;
        }
        next = (int16_t)target;
        if (aimtor_feedback_pair_degraded(&s_feedback[0], &s_feedback[1], s.axis_mask))
        {
            next = aimtor_feedback_limit_rpm(1U, next, s.commanded_rpm[axis]);
            app_motion_ramp_reset(&s_motion_ramp[axis], next);
        }
        s.pending_rpm[axis] = next;
        if ((s.axis_mask & (1U << axis)) && next != s.commanded_rpm[axis]) moving = true;
    }
    s.state = s.target_mm_s == 0 ? CAN_CONTROL_READY : CAN_CONTROL_RUNNING;
    if (moving) start_batch(false);
    publish();
}

bool app_can_control_take_result(app_can_control_result_t *r)
{
    bool found = false; if (!r) return false;
    taskENTER_CRITICAL();
    if (s_rcount) { *r = s_results[s_rread]; s_rread = (uint8_t)((s_rread + 1U) % 32U); --s_rcount; found = true; }
    taskEXIT_CRITICAL(); return found;
}
#else
void app_can_control_init(const can_identity_t *identity) { (void)identity; memset(&s_public, 0, sizeof(s_public)); s_public.stop_result = 255U; }
void app_can_control_link(uint16_t t, uint32_t g, uint32_t b, uint32_t h, bool r, bool s) { (void)t; (void)g; (void)b; (void)h; (void)r; (void)s; }
uint8_t app_can_control_submit(const uint8_t r[8], uint32_t t, uint32_t g) { (void)r; (void)t; (void)g; return 1U; }
bool app_can_control_speed(const uint8_t d[8], uint32_t t) { (void)d; (void)t; return false; }
bool app_can_control_stop(const uint8_t d[8], uint32_t t) { (void)d; (void)t; return false; }
void app_can_control_safety(void) {}
void app_can_control_recovery_reset(void) {}
void app_can_control_pre_service(void) {}
void app_can_control_service(void) {}
bool app_can_control_take_result(app_can_control_result_t *r) { (void)r; return false; }
#endif

bool app_can_control_get_status(app_can_control_status_t *status)
{
    if (!status) return false;
    taskENTER_CRITICAL(); *status = s_public; taskEXIT_CRITICAL(); return true;
}

void app_can_control_ramp_status(app_normal_ramp_status_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL();
    memset(out, 0, sizeof(*out)); out->clock = s_normal_clock;
#if APP_CAN_CONTROL_ENABLED
    memcpy(out->target, s_target_rpm, sizeof(s_target_rpm));
    out->internal[0] = app_motion_ramp_round_rpm(s_motion_ramp[0].current_rpm);
    out->internal[1] = app_motion_ramp_round_rpm(s_motion_ramp[1].current_rpm);
    memcpy(out->batch, s.pending_rpm, sizeof(s.pending_rpm));
    out->authorized = s.authorized; out->phase = (uint8_t)s.state;
#endif
    taskEXIT_CRITICAL();
}
