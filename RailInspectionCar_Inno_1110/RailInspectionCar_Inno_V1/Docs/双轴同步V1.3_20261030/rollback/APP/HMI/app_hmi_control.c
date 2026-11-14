#include "app_hmi_control.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "app_motion_ramp.h"
#include "app_normal_ramp.h"
#include "app_health.h"
#include "app_remote_servo.h"
#include "app_hmi_snapshot.h"
#include "app_cli_servo_session.h"
#include "aimtor_servo.h"
#include "main.h"

enum { CMD_ACQUIRE = 1U, CMD_HEARTBEAT, CMD_RELEASE, CMD_RECOVER = 0x11U,
       CMD_STOP = 0x10U, CMD_SPEED = 0x21U };
enum { RESULT_NONE, RESULT_ACCEPTED, RESULT_RUNNING, RESULT_SUCCEEDED,
       RESULT_REJECTED, RESULT_FAILED, RESULT_EXPIRED };
enum { ERR_NONE, ERR_READ_ONLY, ERR_BOOT, ERR_SESSION, ERR_EXPIRED, ERR_BUSY,
       ERR_SAFETY, ERR_FEEDBACK, ERR_RANGE, ERR_MODE, ERR_CONFLICT, ERR_SEQUENCE,
       ERR_QUEUE, ERR_TIMEOUT, ERR_STOP, ERR_COMMAND };
typedef struct { uint16_t words[24]; TickType_t received; } hmi_request_t;
static hmi_request_t s_inbox, s_estop_inbox, s_request, s_last_request;
static uint8_t s_pending, s_estop_pending, s_have_last, s_stopping;
static uint32_t s_session, s_generation, s_last_sequence;
static uint8_t s_preparing;
#if APP_HMI_CONTROL_ENABLED
static uint8_t s_prepare_axis, s_prepare_cancel;
static app_control_stop_t s_prepare_stop;
static TickType_t s_prepare_tick;
static hmi_request_t s_acquire_request;
#endif
static TickType_t s_lease_tick, s_target_tick, s_ramp_tick, s_stop_tick;
static uint16_t s_target_ttl, s_error, s_deceleration;
static uint32_t s_timeout;
static int16_t s_target, s_output[2], s_batch[2];
static uint32_t s_write[2], s_feedback_sequence[2];
static TickType_t s_write_tick[2], s_reverse_tick;
static uint32_t s_reverse_timeout;
static uint8_t s_zero[2], s_zero_written, s_batch_mask, s_output_mask;
static uint8_t s_reverse, s_zero_confirming;
static int8_t s_direction;
static app_motion_ramp_t s_ramp;
static app_normal_ramp_clock_t s_normal_clock;
static uint32_t s_normal_batch_tick;
static uint8_t s_normal_batch_active;
static remote_sync_status_t s_sync;
static app_hmi_control_status_t s_status, s_published;
static uint32_t s_result_session, s_result_sequence, s_result_received, s_result_completed;
static uint32_t s_stop_result_sequence, s_stop_result_session;
static uint32_t s_observed_event, s_observed_sequence, s_observed_session, s_observed_received;
static uint8_t s_observed_pending;
static uint16_t s_stop_result_command;
static uint16_t s_accepted_state, s_accepted_error;
static uint32_t s_accepted_session, s_accepted_completed;
static uint16_t s_result_command, s_result_state, s_result_error;

static uint32_t elapsed(TickType_t now, TickType_t before)
{ return (uint32_t)((uint64_t)(now - before) * 1000U / configTICK_RATE_HZ); }
static uint32_t word32(const uint16_t *words, uint8_t offset)
{ return ((uint32_t)words[offset] << 16U) | words[offset + 1U]; }
static int32_t magnitude(int16_t value) { return value < 0 ? -(int32_t)value : value; }
static uint8_t selected(uint8_t axis)
{ return (app_config_servo_control_axis_mask() & (1U << axis)) != 0U; }
static void begin_stop(const app_control_stop_t *request);
static uint8_t feedback_safe(const servo_snapshot_t *axis)
{
    return axis->online && !axis->fault && axis->speed_valid && axis->fault_valid &&
        axis->speed_age_ms < APP_SERVO_STATUS_MAX_AGE_MS &&
        axis->fault_age_ms < APP_SERVO_STATUS_MAX_AGE_MS;
}
static uint8_t enabled(const servo_snapshot_t *axis)
{
    return axis->enabled && axis->enable_feedback_valid && axis->enable_feedback &&
        axis->enable_feedback_age_ms < APP_SERVO_STATUS_MAX_AGE_MS;
}
#if APP_HMI_CONTROL_ENABLED
static uint8_t hmi_axes_safe_idle(uint8_t mask)
{
    servo_snapshot_t axes[2];
    uint8_t axis;
    if (mask == 0U || mask != app_config_servo_control_axis_mask()) return 0U;
    if (!aimtor_get_snapshot_pair(&axes[0], &axes[1])) return 0U;
    for (axis = 0U; axis < 2U; ++axis)
    {
        if (!selected(axis)) continue;
        if (axes[axis].feedback_degraded || !feedback_safe(&axes[axis]) || !axes[axis].enable_feedback_valid ||
            axes[axis].enable_feedback_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
            axes[axis].actual_speed_rpm != 0 || axes[axis].busy ||
            axes[axis].stop_latched)
            return 0U;
    }
    return 1U;
}
static uint8_t hmi_stop_confirmed_idle(uint8_t mask)
{
    app_control_stop_t stop;
    app_control_arbiter_get_stop(&stop);
    if (app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE ||
        app_control_arbiter_lock_reason() != APP_CONTROL_LOCK_NONE ||
        app_health_watchdog_test_requested() || stop.owner != APP_CONTROL_OWNER_HMI ||
        stop.command != CMD_STOP || stop.result != RESULT_SUCCEEDED ||
        stop.zero_mask != mask || stop.enabled_mask != mask)
        return 0U;
    return hmi_axes_safe_idle(mask);
}
static uint8_t hmi_safe_idle_stop(uint8_t mask)
{
    app_control_stop_t stop;
    app_control_arbiter_get_stop(&stop);
    if (app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE ||
        app_control_arbiter_lock_reason() != APP_CONTROL_LOCK_NONE ||
        app_control_arbiter_preparing() || app_health_watchdog_test_requested() ||
        !app_config_motion_hardware_ready() || !app_config_servo_bus_ready() ||
        stop.result == RESULT_RUNNING || (stop.event && stop.result != RESULT_SUCCEEDED))
        return 0U;
    return hmi_axes_safe_idle(mask);
}
#endif
uint32_t app_hmi_control_session(void) { return s_session; }
uint16_t app_hmi_control_remaining_ms(void)
{
    uint32_t age = elapsed(xTaskGetTickCount(), s_lease_tick);
    return !s_session || s_stopping || age >= APP_HMI_SESSION_LEASE_MS ? 0U :
        (uint16_t)(APP_HMI_SESSION_LEASE_MS - age);
}
static void publish_result(uint32_t session, uint32_t sequence, uint16_t command,
    uint16_t result, uint16_t error, uint32_t received, uint32_t completed)
{
    uint16_t zero = (s_zero[0] >= 2U ? 1U : 0U) | (s_zero[1] >= 2U ? 2U : 0U);
    app_hmi_snapshot_set_result(session, sequence, command, result, error,
        received, completed, app_control_arbiter_generation(),
        app_control_arbiter_owner() == APP_CONTROL_OWNER_CAN ? 3U :
            (uint16_t)app_control_arbiter_owner(),
        app_hmi_control_remaining_ms(), zero | (error == ERR_STOP ? 4U : 0U));
}
static void result(uint16_t state, uint16_t error, uint32_t completed)
{
    s_result_state = state; s_result_error = error; s_result_completed = completed;
    s_error = error;
    publish_result(s_result_session, s_result_sequence, s_result_command,
        state, error, s_result_received, completed);
    if (s_result_sequence == s_last_sequence && state != RESULT_REJECTED)
    {
        s_accepted_session = s_result_session; s_accepted_state = state;
        s_accepted_error = error; s_accepted_completed = completed;
    }
}
static void fail_stop(uint16_t error)
{
    app_control_stop_t current;
    app_control_arbiter_get_stop(&current);
    if (current.owner != APP_CONTROL_OWNER_HMI || current.result != RESULT_RUNNING)
        (void)app_control_arbiter_submit_stop(error == ERR_FEEDBACK ? 6U : 7U, APP_HMI_LINK_STOP_DECEL_MM_S2,
            s_result_sequence, s_result_command);
    app_control_arbiter_finish_stop(APP_CONTROL_OWNER_HMI, RESULT_FAILED, error, 0U, 0U);
    if (!s_preparing)
    {
        s_result_sequence = s_stop_result_sequence; s_result_session = s_stop_result_session;
        s_result_command = s_stop_result_command;
    }
    result(RESULT_FAILED, error, HAL_GetTick());
    if (!s_preparing && (error == ERR_FEEDBACK || error == ERR_STOP || error == ERR_TIMEOUT))
        aimtor_request_controlled_safety_stop(1U);
    if (!aimtor_safety_stop_active()) aimtor_emergency_stop();
    aimtor_set_hmi_control(false, 0U);
    app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
    app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
    s_session = 0U; s_stopping = 0U; s_target = 0; s_output_mask = 0U;
    s_status.state = 6U;
}
static void update_status(void)
{
    s_status.session = s_session; s_status.last_sequence = s_last_sequence;
    s_status.target_age_ms = s_session ? elapsed(xTaskGetTickCount(), s_target_tick) : 0xFFFFU;
    s_status.target = s_target;
    memcpy(s_status.output, s_output, sizeof(s_output));
    s_status.output_mask = s_output_mask;
    s_status.zero_mask = (s_zero[0] >= 2U ? 1U : 0U) | (s_zero[1] >= 2U ? 2U : 0U);
    s_status.error = s_error; s_status.sync = s_sync;
    app_control_arbiter_get_stop(&s_status.stop);
    if (s_observed_event && s_status.stop.event == s_observed_event)
    {
        s_status.stop.request_sequence = s_observed_sequence;
        s_status.stop.command = CMD_STOP;
    }
    s_status.timeout_ms = s_timeout;
    s_status.elapsed_ms = s_stopping ? elapsed(xTaskGetTickCount(), s_stop_tick) :
        (s_status.stop.completed_ms - s_status.stop.started_ms);
    if (s_status.stop.owner != APP_CONTROL_OWNER_HMI && s_status.stop.event)
        s_status.elapsed_ms = s_status.stop.result == RESULT_RUNNING ?
            HAL_GetTick() - s_status.stop.started_ms : s_status.stop.completed_ms - s_status.stop.started_ms;
    s_status.phase = s_stopping ? (s_zero_confirming ? 3U :
        (s_zero_written == app_config_servo_control_axis_mask() ? 2U : 1U)) :
        (s_status.stop.result == RESULT_SUCCEEDED ? 4U : (s_status.state == 6U ? 5U : 0U));
    if (s_status.stop.result == RESULT_RUNNING && s_status.stop.owner != APP_CONTROL_OWNER_HMI)
        s_status.phase = 1U;
    if (s_status.stop.result == RESULT_FAILED) s_status.phase = 5U;
    if (s_status.state != 6U)
        s_status.state = s_preparing ? 1U : s_stopping ? (s_status.phase >= 2U ? 5U : 4U) :
            (s_session ? (s_target || s_output[0] || s_output[1] ? 3U : 2U) : 0U);
    s_status.flags = (APP_HMI_CONTROL_ENABLED ? 1U : 0U) | 128U |
        (s_session && !s_stopping ? 2U : 0U) | (s_status.stop.result == RESULT_RUNNING ? 4U : 0U) |
        (s_status.stop.result == RESULT_SUCCEEDED && !s_status.stop.error &&
            s_status.stop.zero_mask && s_status.stop.zero_mask == s_status.stop.enabled_mask ? 8U : 0U) |
        (s_reverse ? 16U : 0U) | (s_status.state == 6U ? 96U : 0U);
    taskENTER_CRITICAL(); s_published = s_status; taskEXIT_CRITICAL();
}
void app_hmi_control_get_status(app_hmi_control_status_t *status)
{ if (status) { taskENTER_CRITICAL(); *status = s_published; taskEXIT_CRITICAL(); } }
void app_hmi_control_init(void)
{
    app_normal_ramp_begin(&s_normal_clock, HAL_GetTick());
    s_normal_batch_active = 0U;
    memset(&s_inbox, 0, sizeof(s_inbox)); memset(&s_status, 0, sizeof(s_status));
    memset(&s_published, 0, sizeof(s_published)); memset(&s_sync, 0, sizeof(s_sync));
    memset(s_output, 0, sizeof(s_output)); memset(s_batch, 0, sizeof(s_batch));
    memset(s_write, 0, sizeof(s_write)); memset(s_zero, 0, sizeof(s_zero));
    memset(s_write_tick, 0, sizeof(s_write_tick)); s_reverse_tick = 0U; s_reverse_timeout = 0U;
    s_pending = s_estop_pending = s_have_last = s_stopping = 0U;
    s_preparing = 0U;
#if APP_HMI_CONTROL_ENABLED
    s_prepare_axis = s_prepare_cancel = 0U;
#endif
    s_session = s_generation = s_last_sequence = 0U;
    s_result_session = s_result_sequence = s_result_received = s_result_completed = 0U;
    s_stop_result_sequence = s_stop_result_session = 0U; s_stop_result_command = 0U;
    s_observed_event = s_observed_sequence = s_observed_session = s_observed_received = 0U;
    s_observed_pending = 0U;
    s_result_command = s_result_state = s_result_error = 0U;
    s_accepted_state = s_accepted_error = 0U; s_accepted_session = s_accepted_completed = 0U;
    s_error = s_deceleration = 0U; s_timeout = 0U;
    s_target = 0; s_batch_mask = s_output_mask = s_zero_written = s_reverse = 0U;
    s_direction = 0; s_zero_confirming = 0U; s_target_ttl = APP_HMI_SPEED_HOLD_MAX_MS;
    app_motion_ramp_clear(&s_ramp);
    aimtor_set_hmi_control(false, 0U);
    update_status();
}
uint8_t app_hmi_control_submit(const uint16_t *mailbox)
{
    hmi_request_t *slot;
    if (!mailbox || (aimtor_recovery_blocks_motion() && mailbox[8] != CMD_RECOVER)) return 0U;
    taskENTER_CRITICAL();
    if (mailbox[8] == CMD_STOP)
    {
        if (s_estop_pending) { taskEXIT_CRITICAL(); return 0U; }
        slot = &s_estop_inbox; s_estop_pending = 1U;
    }
    else
    {
        if (s_pending && !(mailbox[8] == CMD_SPEED && s_inbox.words[8] == CMD_SPEED &&
            word32(mailbox, 6U) > word32(s_inbox.words, 6U)))
        { taskEXIT_CRITICAL(); return 0U; }
        slot = &s_inbox; s_pending = 1U;
    }
    memcpy(slot->words, mailbox, sizeof(slot->words));
    slot->received = xTaskGetTickCount();
    taskEXIT_CRITICAL(); return 1U;
}
void app_hmi_control_request_stop(void)
{ (void)app_control_arbiter_submit_stop(5U, APP_HMI_EMERGENCY_STOP_DECEL_MM_S2, 0U, CMD_STOP); }
static uint16_t validate(const hmi_request_t *request)
{
    const uint16_t *words = request->words;
    uint16_t command = words[8], offset;
    if (words[0] != 0x484DU || words[1] != 1U || words[22] != 2U ||
        words[23] != 0xA55AU || words[9] != 0U) return ERR_RANGE;
    if (word32(words, 2U) != app_hmi_snapshot_boot_id()) return ERR_BOOT;
    for (offset = 12U; offset <= 21U; ++offset)
        if (offset != 12U && offset != 13U && offset != 17U && words[offset]) return ERR_RANGE;
    if (command != CMD_ACQUIRE && command != CMD_HEARTBEAT && command != CMD_RELEASE &&
        command != CMD_RECOVER && command != CMD_STOP && command != CMD_SPEED) return ERR_COMMAND;
    if (command == CMD_RECOVER)
    {
        /* session is deliberately zero; words 12/13 carry the caller's
         * recovery generation and all other payload words remain reserved. */
        if (words[4] || words[5] || !word32(words, 12U)) return ERR_RANGE;
    }
    if (command == CMD_SPEED)
    {
        int32_t target = (int32_t)word32(words, 10U);
        if (!words[17] || words[17] > APP_HMI_SPEED_HOLD_MAX_MS ||
            target > APP_OPERATION_MAX_MOTOR_RPM || target < -APP_OPERATION_MAX_MOTOR_RPM)
            return ERR_RANGE;
    }
    else if (words[10] || words[11] || words[17]) return ERR_RANGE;
    return ERR_NONE;
}
#if APP_HMI_CONTROL_ENABLED
static uint16_t acquire(void)
{
    servo_snapshot_t axes[2];
    uint32_t ticket;
    uint8_t axis;
    if (s_session || s_preparing || app_remote_servo_k5_verify_pending() || app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE ||
        app_cli_servo_session_active()) return ERR_BUSY;
    if (app_control_arbiter_lock_reason() != APP_CONTROL_LOCK_NONE ||
        app_health_watchdog_test_requested() || app_config_servo_readonly_test() ||
        !app_config_motion_hardware_ready() || !aimtor_watchdog_recovery_ready() ||
        !aimtor_startup_stop_ready() || !aimtor_boot_zero_ready() ||
        !aimtor_startup_hold_ready()) return ERR_SAFETY;
    if (!aimtor_get_snapshot_pair(&axes[0], &axes[1])) return ERR_FEEDBACK;
    for (axis = 0U; axis < 2U; ++axis)
    {
        if (!selected(axis)) continue;
        if (!feedback_safe(&axes[axis])) return ERR_FEEDBACK;
        if (axes[axis].busy || axes[axis].actual_speed_rpm || axes[axis].stop_latched) return ERR_SAFETY;
    }
    ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_HMI, app_config_servo_control_axis_mask());
    app_control_arbiter_service();
    if (!ticket || app_control_arbiter_poll(ticket, &s_generation) != 1)
    { app_control_arbiter_cancel(ticket); return ERR_BUSY; }
    s_preparing = 1U; s_prepare_axis = 0U;
    aimtor_set_hmi_control(false, app_config_servo_control_axis_mask());
    s_prepare_cancel = 0U; s_prepare_tick = xTaskGetTickCount(); s_acquire_request = s_request;
    return ERR_NONE;
}

static void prepare_service(void)
{
    servo_snapshot_t axes[2];
    uint8_t axis;
    servo_result_t prepared;
    if (!s_preparing) return;
    if (app_control_arbiter_owner() != APP_CONTROL_OWNER_HMI ||
        app_control_arbiter_lock_reason() != APP_CONTROL_LOCK_NONE)
    { s_preparing = 0U; return; }
    if (!aimtor_get_snapshot_pair(&axes[0], &axes[1])) { fail_stop(ERR_FEEDBACK); s_preparing = 0U; return; }
    for (axis = 0U; axis < 2U; ++axis)
        if (selected(axis) && (!feedback_safe(&axes[axis]) || axes[axis].actual_speed_rpm))
        { fail_stop(ERR_FEEDBACK); s_preparing = 0U; return; }
    if (elapsed(xTaskGetTickCount(), s_prepare_tick) >= 2000U)
    { fail_stop(ERR_TIMEOUT); s_preparing = 0U; return; }
    if (s_prepare_axis)
    {
        const servo_snapshot_t *feedback = &axes[s_prepare_axis - 1U];
        if (feedback->busy) return;
        if (feedback->last_result != SERVO_OK) { fail_stop(ERR_STOP); s_preparing = 0U; return; }
        s_prepare_axis = 0U;
    }
    for (axis = 0U; axis < 2U; ++axis)
    {
        if (!selected(axis)) continue;
        if (!axes[axis].parameter_verified || axes[axis].control_mode != SERVO_CONTROL_RS485_VELOCITY)
        {
            prepared = aimtor_remote_prepare_mode((servo_axis_t)axis);
            if (prepared == SERVO_OK) s_prepare_axis = axis + 1U;
            else if (prepared != SERVO_BUSY) { fail_stop(ERR_STOP); s_preparing = 0U; }
            return;
        }
    }
    for (axis = 0U; axis < 2U; ++axis)
    {
        if (!selected(axis)) continue;
        if (!axes[axis].enabled)
        {
            prepared = aimtor_remote_prepare_enable((servo_axis_t)axis);
            if (prepared == SERVO_OK) s_prepare_axis = axis + 1U;
            else if (prepared != SERVO_BUSY) { fail_stop(ERR_STOP); s_preparing = 0U; }
            return;
        }
        if (axes[axis].busy || !enabled(&axes[axis])) return;
    }
    if (s_prepare_cancel)
    {
        s_preparing = 0U; s_session = s_generation;
        memset(s_output, 0, sizeof(s_output)); memset(s_write, 0, sizeof(s_write));
        s_output_mask = app_config_servo_control_axis_mask();
        begin_stop(&s_prepare_stop);
        return;
    }
    if (!app_control_arbiter_request(APP_CONTROL_OWNER_HMI, app_config_servo_control_axis_mask(), &s_generation))
    { fail_stop(ERR_BUSY); s_preparing = 0U; return; }
    {
        servo_k1_hold_status_t hold;
        if (aimtor_k1_hold_get_status(&hold) && hold.state != SERVO_K1_HOLD_INACTIVE &&
            aimtor_k1_hold_release() != SERVO_OK)
        { fail_stop(ERR_SAFETY); s_preparing = 0U; return; }
    }
    app_normal_ramp_begin(&s_normal_clock, HAL_GetTick());
    s_normal_batch_active = 0U;
    s_session = s_generation; s_target = 0;
    memset(s_output, 0, sizeof(s_output)); memset(s_write, 0, sizeof(s_write));
    memset(s_zero, 0, sizeof(s_zero)); s_zero_written = s_batch_mask = 0U;
    s_output_mask = app_config_servo_control_axis_mask();
    s_direction = 0; s_reverse = 0U; s_status.state = 2U;
    s_lease_tick = s_target_tick = xTaskGetTickCount();
    s_target_ttl = APP_HMI_SPEED_HOLD_MAX_MS; s_ramp_tick = xTaskGetTickCount();
    app_motion_ramp_clear(&s_ramp);
    app_remote_sync_reset(&s_sync, &axes[0], &axes[1], xTaskGetTickCount());
    aimtor_set_hmi_control(true, app_config_servo_control_axis_mask());
    s_preparing = 0U;
    s_result_sequence = word32(s_acquire_request.words, 6U);
    s_result_session = s_session; s_result_command = CMD_ACQUIRE;
    result(RESULT_SUCCEEDED, 0U, HAL_GetTick());
    s_accepted_session = s_session; s_accepted_state = RESULT_SUCCEEDED;
    s_accepted_completed = HAL_GetTick();
}
#endif
static void process_request(void)
{
    const uint16_t *words = s_request.words;
    uint32_t sequence = word32(words, 6U), session = word32(words, 4U);
    uint16_t command = words[8], error = validate(&s_request);
    uint8_t stop_running = 0U, stop_succeeded = 0U;
    (void)session;
    s_result_session = session; s_result_sequence = sequence;
    s_result_command = command;
    s_result_received = HAL_GetTick() - elapsed(xTaskGetTickCount(), s_request.received);
    if (!error && (!sequence || sequence <= s_last_sequence))
    {
        if (sequence == s_last_sequence && s_have_last &&
            !memcmp(words, s_last_request.words, sizeof(s_last_request.words)))
        {
            publish_result(s_accepted_session, sequence, command, s_accepted_state,
                s_accepted_error, s_result_received, s_accepted_completed); return;
        }
        error = sequence == s_last_sequence ? ERR_CONFLICT : ERR_SEQUENCE;
    }
#if !APP_HMI_CONTROL_ENABLED
    if (!error) error = ERR_READ_ONLY;
#else
    if (!error && command == CMD_RECOVER)
    {
        servo_recovery_status_t recovery;
        aimtor_get_recovery_status(&recovery);
        if (word32(words, 12U) != recovery.generation ||
            aimtor_request_recovery(3U) != SERVO_OK) error = ERR_SAFETY;
        else
        {
            s_last_sequence = sequence; s_last_request = s_request; s_have_last = 1U;
            result(RESULT_RUNNING, 0U, HAL_GetTick());
            return;
        }
    }
    else if (!error && command == CMD_ACQUIRE)
    {
        if (session) error = ERR_SESSION;
        else if (elapsed(xTaskGetTickCount(), s_request.received) >= APP_HMI_SPEED_HOLD_MAX_MS) error = ERR_TIMEOUT;
        else error = acquire();
        if (!error) s_result_session = s_session;
    }
    else if (!error && command == CMD_STOP && session == 0U)
    {
        taskENTER_CRITICAL(); s_pending = 0U; taskEXIT_CRITICAL();
        if (hmi_stop_confirmed_idle(app_config_servo_control_axis_mask()))
            stop_succeeded = 1U;
        else if (hmi_safe_idle_stop(app_config_servo_control_axis_mask()))
            stop_succeeded = 1U;
        else if (app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_HMI,
            2U, APP_HMI_EMERGENCY_STOP_DECEL_MM_S2, sequence, command))
            stop_running = 1U;
        else
            error = ERR_STOP;
    }
    else if (!error)
    {
        if (!s_session || session != s_session) error = ERR_SESSION;
        else if (command == CMD_STOP)
        {
            if (app_control_arbiter_submit_stop(2U, APP_HMI_EMERGENCY_STOP_DECEL_MM_S2, sequence, command))
                stop_running = 1U;
            else
                error = ERR_STOP;
        }
        else if (s_stopping) error = ERR_EXPIRED;
        else if (command == CMD_RELEASE)
            (void)app_control_arbiter_submit_stop(1U, APP_HMI_NORMAL_STOP_DECEL_MM_S2, sequence, command);
        else if (command == CMD_SPEED)
        {
            if (elapsed(xTaskGetTickCount(), s_request.received) >= words[17]) error = ERR_TIMEOUT;
            else
            {
                if (s_target != (int16_t)(int32_t)word32(words, 10U))
                    app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
                s_target = (int16_t)(int32_t)word32(words, 10U);
                s_target_tick = s_lease_tick = s_request.received; s_target_ttl = words[17];
            }
        }
        else if (command == CMD_HEARTBEAT) s_lease_tick = s_request.received;
    }
#endif
    if (error) { result(RESULT_REJECTED, error, 0U); return; }
    if (stop_running && command == CMD_STOP)
    {
        app_control_stop_t observed;
        app_control_arbiter_get_stop(&observed);
        s_observed_event = observed.event; s_observed_sequence = sequence;
        s_observed_session = session; s_observed_received = s_result_received;
        s_observed_pending = 1U;
    }
    s_last_sequence = sequence; s_last_request = s_request; s_have_last = 1U;
    result(stop_succeeded ? RESULT_SUCCEEDED :
        (command == CMD_RELEASE || stop_running || s_preparing ? RESULT_RUNNING :
        (command == CMD_SPEED ? RESULT_ACCEPTED : RESULT_SUCCEEDED)), 0U, HAL_GetTick());
    s_accepted_session = s_result_session; s_accepted_state = s_result_state;
    s_accepted_error = s_result_error; s_accepted_completed = s_result_completed;
}
static void begin_stop(const app_control_stop_t *request)
{
    uint8_t axis;
    int16_t maximum = 0;
    s_stop_result_session = s_session; s_stop_result_sequence = request->request_sequence;
    s_stop_result_command = request->command;
    if (s_stopping)
    { if (request->deceleration_mm_s2 > s_deceleration) s_deceleration = request->deceleration_mm_s2; return; }
    app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
    s_normal_batch_active = 0U;
    s_stopping = 1U; s_target = 0; s_batch_mask = 0U;
    s_stop_tick = xTaskGetTickCount(); s_ramp_tick = s_stop_tick;
    s_deceleration = request->deceleration_mm_s2;
    aimtor_set_hmi_control(false, app_config_servo_control_axis_mask());
    for (axis = 0U; axis < 2U; ++axis)
    {
        servo_snapshot_t feedback;
        if (!selected(axis)) continue;
        if (magnitude(s_output[axis]) > magnitude(maximum)) maximum = s_output[axis];
        if (aimtor_get_snapshot((servo_axis_t)axis, &feedback) &&
            magnitude(feedback.actual_speed_rpm) > magnitude(maximum)) maximum = feedback.actual_speed_rpm;
    }
    s_timeout = (uint32_t)((float)magnitude(maximum) * APP_WHEEL_CIRCUMFERENCE_M /
        (60.0f * APP_GEAR_REDUCTION_RATIO) / ((float)s_deceleration / 1000.0f) * 1000.0f) + 2000U;
    app_control_arbiter_stop_timeout(APP_CONTROL_OWNER_HMI, s_timeout);
    app_motion_ramp_reset(&s_ramp, maximum);
    memset(s_zero, 0, sizeof(s_zero)); s_zero_written = 0U; s_zero_confirming = 0U;
}
static uint8_t complete_writes(servo_snapshot_t *axes)
{
    uint8_t axis, waiting = 0U;
    for (axis = 0U; axis < 2U; ++axis)
    {
        if (!selected(axis) || !s_write[axis]) continue;
        if (axes[axis].speed_write_completed != s_write[axis])
        {
            if (elapsed(xTaskGetTickCount(), s_write_tick[axis]) >= APP_HMI_WRITE_TIMEOUT_MS)
            { fail_stop(ERR_TIMEOUT); return 0U; }
            waiting = 1U; continue;
        }
        if (axes[axis].speed_write_result != SERVO_OK) { fail_stop(ERR_STOP); return 0U; }
        s_write[axis] = 0U;
        if (axes[axis].speed_write_rpm_valid && s_output[axis] != axes[axis].speed_write_rpm)
        {
            s_output[axis] = axes[axis].speed_write_rpm;
            /* During stop/reversal the service clock may have advanced well
             * past this acknowledged command.  Re-anchoring here would
             * throw away that elapsed time and recreate the timeout bug. */
            if (!s_stopping && !s_reverse)
                app_motion_ramp_reset(&s_ramp, s_output[axis]);
        }
        if (!s_output[axis])
        {
            s_zero_written |= (uint8_t)(1U << axis);
            s_feedback_sequence[axis] = axes[axis].speed_sequence;
            s_zero[axis] = 0U;
        }
    }
    return !waiting;
}
static void execute(void)
{
    servo_snapshot_t axes[2];
    uint8_t axis, zero_mask = 0U, mask = app_config_servo_control_axis_mask();
    int16_t base, target = s_target;
    servo_result_t submitted;
    TickType_t now = xTaskGetTickCount();
    if (!s_session) { app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick()); return; }
    if (!aimtor_get_snapshot_pair(&axes[0], &axes[1])) { fail_stop(ERR_FEEDBACK); return; }
    for (axis = 0U; axis < 2U; ++axis)
        if (selected(axis) && (!feedback_safe(&axes[axis]) || !enabled(&axes[axis])))
        { fail_stop(ERR_FEEDBACK); return; }
    if (s_stopping && elapsed(now, s_stop_tick) >= s_timeout) { fail_stop(ERR_STOP); return; }
    if (s_reverse && !s_stopping && elapsed(now, s_reverse_tick) >= s_reverse_timeout)
    { fail_stop(ERR_TIMEOUT); return; }

    /*
     * The stop/reversal clock is a service clock, not a Modbus transaction
     * clock.  A speed write can remain in flight for several service passes;
     * advancing the ramp only after complete_writes() used to discard that
     * time and made the physical deceleration slower while the deadline kept
     * running.  Integrate before looking at the outstanding batch and keep the
     * latched batch immutable until its acknowledgement arrives.
     */
    if (s_stopping || s_reverse)
    {
        uint32_t stop_dt = elapsed(now, s_ramp_tick);
        if (stop_dt != 0U)
        {
            s_ramp_tick = now;
            (void)app_motion_ramp_step(&s_ramp, 0, stop_dt,
                APP_DESIGN_ACCELERATION_MPS2,
                s_stopping ? (float)s_deceleration / 1000.0f :
                             APP_DESIGN_DECELERATION_MPS2);
        }
    }
    /* Integrate healthy normal motion even while a previously latched batch waits. */
    if (!s_stopping && !s_reverse &&
        !(s_direction && target && (target > 0 ? 1 : -1) != s_direction))
    {
        uint32_t dt = app_normal_ramp_elapsed(&s_normal_clock, HAL_GetTick());
        int16_t normal = app_motion_ramp_step(&s_ramp, target, dt,
            APP_DESIGN_ACCELERATION_MPS2, APP_DESIGN_DECELERATION_MPS2);
        if (aimtor_feedback_pair_degraded(&axes[0], &axes[1], mask)) {
            for (axis = 0U; axis < 2U; ++axis)
                if (selected(axis)) normal = aimtor_feedback_limit_rpm(1U, normal, s_output[axis]);
            app_motion_ramp_reset(&s_ramp, normal);
        }
    }
    else app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
    if (!complete_writes(axes)) return;
    if (s_normal_batch_active && !s_batch_mask) {
        app_normal_ramp_batch_done(&s_normal_clock, HAL_GetTick() - s_normal_batch_tick);
        s_normal_batch_active = 0U;
    }
    app_remote_sync_collect(&s_sync, &axes[0], &axes[1], now);
    if (s_reverse || s_stopping)
    {
        for (axis = 0U; axis < 2U; ++axis)
        {
            if (!selected(axis)) continue;
            if (axes[axis].speed_sequence != s_feedback_sequence[axis])
            {
                s_feedback_sequence[axis] = axes[axis].speed_sequence;
                if ((s_zero_written & (1U << axis)) && !axes[axis].actual_speed_rpm &&
                    !axes[axis].busy && enabled(&axes[axis]))
                { if (s_zero[axis] < 2U) ++s_zero[axis]; }
                else s_zero[axis] = 0U;
            }
            if (s_zero[axis] >= 2U && enabled(&axes[axis]) && !axes[axis].actual_speed_rpm &&
                !axes[axis].busy) zero_mask |= (uint8_t)(1U << axis);
        }
        if (zero_mask == mask)
        {
            if (s_stopping)
            {
                if (!s_zero_confirming) { s_zero_confirming = 1U; return; }
                app_control_arbiter_finish_stop(APP_CONTROL_OWNER_HMI, RESULT_SUCCEEDED, 0U, mask, mask);
                s_result_sequence = s_stop_result_sequence; s_result_session = s_stop_result_session;
                s_result_command = s_stop_result_command;
                result(RESULT_SUCCEEDED, 0U, HAL_GetTick());
                aimtor_set_hmi_control(false, 0U); app_control_arbiter_release(APP_CONTROL_OWNER_HMI);
                s_session = 0U; s_stopping = s_reverse = 0U; s_output_mask = 0U; return;
            }
            s_reverse = 0U; s_direction = 0; app_motion_ramp_clear(&s_ramp); s_ramp_tick = now;
            app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
        }
    }
    if (s_stopping && zero_mask != mask) s_zero_confirming = 0U;
    if (!s_stopping && !s_reverse && s_direction && target && (target > 0 ? 1 : -1) != s_direction)
    {
        int32_t maximum = magnitude(s_output[0]);
        for (axis = 0U; axis < 2U; ++axis)
        {
            if (!selected(axis)) continue;
            if (magnitude(s_output[axis]) > maximum) maximum = magnitude(s_output[axis]);
            if (magnitude(axes[axis].actual_speed_rpm) > maximum) maximum = magnitude(axes[axis].actual_speed_rpm);
        }
        /* Discard unsent normal progress before entering the original reversal ramp. */
        app_motion_ramp_reset(&s_ramp, s_direction * (int16_t)maximum);
        app_normal_ramp_boundary(&s_normal_clock, HAL_GetTick());
        s_reverse = 1U; target = 0; s_reverse_tick = now; s_ramp_tick = now;
        s_reverse_timeout = (uint32_t)((float)maximum * APP_WHEEL_CIRCUMFERENCE_M /
            (60.0f * APP_GEAR_REDUCTION_RATIO * APP_DESIGN_DECELERATION_MPS2) * 1000.0f) + 2000U;
    }
    if (s_reverse || s_stopping) target = 0;
    if (!s_batch_mask)
    {
        servo_sync_policy_t policy;
        base = !s_stopping && !s_reverse ? app_motion_ramp_round_rpm(s_ramp.current_rpm) :
            app_motion_ramp_step(&s_ramp, target, elapsed(now, s_ramp_tick),
            APP_DESIGN_ACCELERATION_MPS2, s_stopping ? (float)s_deceleration / 1000.0f : APP_DESIGN_DECELERATION_MPS2);
        if (aimtor_feedback_pair_degraded(&axes[0], &axes[1], mask))
        {
            for (axis = 0U; axis < 2U; ++axis)
                if (selected(axis)) base = aimtor_feedback_limit_rpm(1U, base, s_output[axis]);
            app_motion_ramp_reset(&s_ramp, base);
        }
        s_ramp_tick = now;
        app_servo_sync_policy_default(&policy);
        app_servo_sync_update(&s_sync, &axes[0], &axes[1], base,
            mask == 3U && APP_REMOTE_SERVO_SYNC_ENABLED,
            mask == 3U && APP_REMOTE_SERVO_SYNC_ENABLED && !s_stopping && !s_reverse, now, &policy);
        if (s_sync.trip) { fail_stop(ERR_SAFETY); return; }
        if (s_stopping || s_reverse || !base) s_sync.integer_correction = 0;
        s_batch[0] = base - s_sync.integer_correction;
        s_batch[1] = base + s_sync.integer_correction;
        for (axis = 0U; axis < 2U; ++axis)
        {
            if (!selected(axis)) continue;
            if (s_batch[axis] > APP_OPERATION_MAX_MOTOR_RPM) s_batch[axis] = APP_OPERATION_MAX_MOTOR_RPM;
            if (s_batch[axis] < -APP_OPERATION_MAX_MOTOR_RPM) s_batch[axis] = -APP_OPERATION_MAX_MOTOR_RPM;
            if (s_stopping)
            {
                int16_t previous = s_output[axis];
                if (!previous || (previous > 0 && s_batch[axis] < 0) ||
                    (previous < 0 && s_batch[axis] > 0)) s_batch[axis] = 0;
                else if (magnitude(s_batch[axis]) > magnitude(previous)) s_batch[axis] = previous;
            }
            if (s_output[axis] != s_batch[axis] ||
                (!s_batch[axis] && !(s_zero_written & (1U << axis))))
                s_batch_mask |= (uint8_t)(1U << axis);
        }
    }
    if (s_batch_mask && !s_stopping && !s_reverse && !s_normal_batch_active) {
        s_normal_batch_tick = HAL_GetTick(); s_normal_batch_active = 1U;
    }
    for (axis = 0U; axis < 2U; ++axis)
    {
        if (!(s_batch_mask & (1U << axis))) continue;
        if (axes[axis].busy) return;
        if (aimtor_feedback_pair_degraded(&axes[0], &axes[1], mask))
        {
            s_batch[axis] = aimtor_feedback_limit_rpm(1U, s_batch[axis], s_output[axis]);
            app_motion_ramp_reset(&s_ramp, s_batch[axis]);
        }
        submitted = s_stopping ? aimtor_set_stop_velocity_rpm((servo_axis_t)axis, s_batch[axis]) :
            aimtor_set_velocity_rpm((servo_axis_t)axis, s_batch[axis]);
        if (submitted == SERVO_OK)
        {
            servo_snapshot_t after;
            if (!aimtor_get_snapshot((servo_axis_t)axis, &after)) { fail_stop(ERR_STOP); return; }
            s_write[axis] = after.speed_write_sequence; s_write_tick[axis] = now;
            if (!s_write[axis]) { fail_stop(ERR_STOP); return; }
            s_output[axis] = s_batch[axis]; s_batch_mask &= (uint8_t)~(1U << axis);
            if (s_output[axis]) { s_direction = s_output[axis] > 0 ? 1 : -1; s_zero_written &= (uint8_t)~(1U << axis); s_zero[axis] = 0U; }
        }
        else if (submitted != SERVO_BUSY) fail_stop(ERR_STOP);
        return;
    }
}
void app_hmi_control_service(void)
{
    app_control_stop_t stop;
    uint8_t have_request = 0U;
    if (aimtor_safety_stop_active() || aimtor_recovery_blocks_motion())
    {
        /* Recovery has its own mailbox path and must be consumed before the
         * normal motion-blocking early return.  Ordinary requests remain
         * discarded while recovery is active. */
        taskENTER_CRITICAL();
        if (s_pending && s_inbox.words[8] == CMD_RECOVER)
        { s_request = s_inbox; s_pending = 0U; have_request = 1U; }
        else s_pending = 0U;
        s_estop_pending = 0U;
        taskEXIT_CRITICAL();
        if (have_request) process_request();
        if ((s_session || s_preparing) && !aimtor_recovery_blocks_motion()) fail_stop(ERR_FEEDBACK);
        s_preparing = 0U;
        taskENTER_CRITICAL();
        s_pending = s_estop_pending = 0U;
        taskEXIT_CRITICAL();
        update_status();
        return;
    }
    if (s_session && (app_control_arbiter_owner() != APP_CONTROL_OWNER_HMI ||
        (!s_stopping && app_control_arbiter_generation() != s_generation)))
    {
        app_control_arbiter_get_stop(&stop);
        if (!(stop.result == RESULT_RUNNING && stop.owner == APP_CONTROL_OWNER_HMI))
        { s_session = 0U; s_target = 0; s_output_mask = 0U; s_stopping = 0U; aimtor_set_hmi_control(false, 0U); }
    }
    if (s_session && !s_stopping && !aimtor_hmi_control_active()) fail_stop(ERR_SAFETY);
    if (s_session && !s_stopping)
    {
        if (app_health_watchdog_test_requested()) fail_stop(ERR_SAFETY);
        else if (elapsed(xTaskGetTickCount(), s_target_tick) >= s_target_ttl)
            (void)app_control_arbiter_submit_stop(3U, APP_HMI_LINK_STOP_DECEL_MM_S2, 0U, 0U);
        else if (!app_hmi_control_remaining_ms())
            (void)app_control_arbiter_submit_stop(4U, APP_HMI_LINK_STOP_DECEL_MM_S2, 0U, 0U);
    }
    if (app_control_arbiter_take_stop(APP_CONTROL_OWNER_HMI, &stop))
    {
        if (s_session) begin_stop(&stop);
#if APP_HMI_CONTROL_ENABLED
        else if (s_preparing) { s_prepare_cancel = 1U; s_prepare_stop = stop; }
#endif
    }
    taskENTER_CRITICAL();
    if (s_estop_pending) { s_request = s_estop_inbox; s_estop_pending = 0U; have_request = 1U; }
    else if (s_pending && !(s_inbox.words[8] == CMD_ACQUIRE &&
        app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE &&
        app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_NONE &&
        app_control_arbiter_stopping() &&
        elapsed(xTaskGetTickCount(), s_inbox.received) < APP_HMI_SPEED_HOLD_MAX_MS))
    { s_request = s_inbox; s_pending = 0U; have_request = 1U; }
    taskEXIT_CRITICAL();
    if (have_request) process_request();
    if (app_control_arbiter_take_stop(APP_CONTROL_OWNER_HMI, &stop))
    {
        if (s_session) begin_stop(&stop);
#if APP_HMI_CONTROL_ENABLED
        else if (s_preparing) { s_prepare_cancel = 1U; s_prepare_stop = stop; }
#endif
    }
#if APP_HMI_CONTROL_ENABLED
    prepare_service();
#endif
    execute();
    if (s_observed_pending)
    {
        app_control_arbiter_get_stop(&stop);
        if (stop.event == s_observed_event && stop.result != RESULT_RUNNING)
        {
            uint16_t confirmed = stop.result == RESULT_SUCCEEDED && !stop.error &&
                stop.zero_mask == app_config_servo_control_axis_mask() &&
                stop.enabled_mask == stop.zero_mask &&
                app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE &&
                app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_NONE;
            publish_result(s_observed_session, s_observed_sequence, CMD_STOP,
                confirmed ? RESULT_SUCCEEDED : RESULT_FAILED,
                confirmed ? 0U : ERR_STOP, s_observed_received, HAL_GetTick());
            if (s_last_sequence == s_observed_sequence)
            {
                s_accepted_session = s_observed_session;
                s_accepted_state = confirmed ? RESULT_SUCCEEDED : RESULT_FAILED;
                s_accepted_error = confirmed ? 0U : ERR_STOP;
                s_accepted_completed = HAL_GetTick();
            }
            s_observed_pending = 0U;
        }
    }
    update_status();
}

void app_hmi_control_ramp_status(app_normal_ramp_status_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL();
    memset(out, 0, sizeof(*out)); out->clock = s_normal_clock;
    out->target[0] = out->target[1] = s_target;
    out->internal[0] = out->internal[1] = app_motion_ramp_round_rpm(s_ramp.current_rpm);
    memcpy(out->batch, s_batch, sizeof(s_batch));
    out->authorized = s_session && !s_stopping; out->phase = (uint8_t)s_published.state;
    taskEXIT_CRITICAL();
}
