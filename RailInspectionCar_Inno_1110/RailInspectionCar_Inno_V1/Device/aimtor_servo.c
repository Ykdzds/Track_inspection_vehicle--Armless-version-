#include "aimtor_servo.h"

#include <string.h>

#include "task.h"
#include "app_config.h"
#include "app_sync_params.h"
#include "HMI/app_control_arbiter.h"
#include "app_health.h"
#include "aimtor_registers.h"
#include "modbus_rtu.h"
#include "motion_control.h"
#include "servo_pulse.h"

#define AIMTOR_OWNER_ID (1U)
#define AIMTOR_SEQUENCE_CAPACITY (20U)
#define AIMTOR_TAG_SEQUENCE (1U)
#define AIMTOR_TAG_SPEED (2U)
#define AIMTOR_TAG_FAULT (3U)
#define AIMTOR_TAG_POSITION (4U)
#define AIMTOR_TAG_STATE (5U)
#define AIMTOR_TAG_STARTUP_OFF (6U)
#define AIMTOR_TAG_WDG_VERIFY (7U)
#define AIMTOR_TAG_WDG_OFF (8U)
#define AIMTOR_TAG_HOLD_ENABLE (9U)
#define AIMTOR_TAG_WDG_RAMP (10U)
#define AIMTOR_TAG_WDG_ZERO (11U)
#define AIMTOR_TAG_DIAG_TORQUE (12U)
#define AIMTOR_TAG_DIAG_LOAD (13U)
#define AIMTOR_TAG_DIAG_CURRENT (14U)
#define AIMTOR_TAG_DIAG_BUS_VOLTAGE (15U)
#define AIMTOR_TAG_SAFETY_WRITE (16U)
#define AIMTOR_TAG_COLD_CLEAR (17U)

typedef enum
{
    ACTION_NONE = 0,
    ACTION_CONFIGURE,
    ACTION_VERIFY,
    ACTION_ENABLE,
    ACTION_DISABLE,
    ACTION_SPEED,
    ACTION_SEGMENT,
    ACTION_ZERO,
    ACTION_BOOT_ZERO,
    ACTION_STARTUP_DISABLE,
    ACTION_WDG_RECOVERY,
    ACTION_STARTUP_HOLD,
    ACTION_HOLD_ZERO
} aimtor_action_t;

typedef struct
{
    uint16_t address;
    uint16_t words[4];
    uint8_t count;
    uint8_t write;
    uint8_t compare;
} aimtor_step_t;

typedef struct
{
    servo_snapshot_t snapshot;
    aimtor_step_t steps[AIMTOR_SEQUENCE_CAPACITY];
    aimtor_action_t action;
    servo_control_mode_t requested_mode;
    uint8_t count;
    uint8_t index;
    uint8_t reading_back;
    uint8_t speed_valid;
    uint8_t fault_valid;
    uint8_t disable_pending;
    uint8_t segment_latched;
    uint8_t position_running;
    TickType_t speed_tick;
    TickType_t fault_tick;
    TickType_t position_tick;
    TickType_t next_speed_poll;
    TickType_t next_fault_poll;
    TickType_t next_position_poll;
    TickType_t next_state_poll;
    TickType_t next_diagnostic_poll[4];
    TickType_t next_probe_tick;
    uint8_t probe_pending;
    uint32_t position_sequence;
    uint32_t speed_write_sequence;
    uint32_t speed_sequence;
    uint8_t position_priority;
} aimtor_axis_state_t;

static aimtor_axis_state_t s_axes[SERVO_AXIS_COUNT];
static uint32_t s_transaction_id;
static uint32_t s_pending_id;
#if APP_LINK_DIAG_ACTIVE
/* Only nonzero while a matched response is consumed under the same critical
 * section.  The business pending ID must be cleared before follow-up submits. */
static uint32_t s_diag_response_id;
#define AIMTOR_RESPONSE_EXIT_CRITICAL() do { s_diag_response_id = 0U; taskEXIT_CRITICAL(); } while (0)
#else
#define AIMTOR_RESPONSE_EXIT_CRITICAL() taskEXIT_CRITICAL()
#endif
static uint8_t s_pending_axis;
static uint8_t s_pending_cancelled;
static uint8_t s_maintenance_enabled;
static uint8_t s_poll_axis_turn;
static uint8_t s_poll_tag_turn[SERVO_AXIS_COUNT];
static uint8_t s_action_axis_turn;
static uint8_t s_critical_poll_streak;
static uint8_t s_background_critical, s_background_turn;
/* One critical read may borrow a slot only after a whole parameter step. */
static uint8_t s_startup_feedback_slot, s_startup_poll_turn;
static servo_poll_status_t s_poll_stats[SERVO_AXIS_COUNT][SERVO_POLL_CLASSES];
static TickType_t s_poll_sample[SERVO_AXIS_COUNT][SERVO_POLL_CLASSES];

static uint8_t aimtor_poll_item(uint16_t tag)
{
    if (tag == AIMTOR_TAG_SPEED) return 0U;
    if (tag == AIMTOR_TAG_FAULT) return 1U;
    if (tag == AIMTOR_TAG_HOLD_ENABLE) return 2U;
    if (tag == AIMTOR_TAG_POSITION) return 3U;
    if (tag == AIMTOR_TAG_STATE) return 4U;
    if (tag >= AIMTOR_TAG_DIAG_TORQUE && tag <= AIMTOR_TAG_DIAG_BUS_VOLTAGE)
        return (uint8_t)(5U + tag - AIMTOR_TAG_DIAG_TORQUE);
    return SERVO_POLL_CLASSES;
}
static void aimtor_poll_record(uint8_t axis, uint16_t tag, modbus_status_t result)
{
    uint8_t item = aimtor_poll_item(tag);
    servo_poll_status_t *stat;
    if (item >= SERVO_POLL_CLASSES) return;
    stat = &s_poll_stats[axis][item];
    stat->last_result = result;
    if (result == MODBUS_OK) { stat->succeeded++; s_poll_sample[axis][item] = xTaskGetTickCount(); }
    else stat->failed++;
}
bool aimtor_get_poll_status(servo_axis_t axis, uint8_t item, servo_poll_status_t *out)
{
    if (axis >= SERVO_AXIS_COUNT || item >= SERVO_POLL_CLASSES || !out) return false;
    taskENTER_CRITICAL();
    *out = s_poll_stats[axis][item];
    out->age_ms = out->succeeded ? (uint32_t)((uint64_t)(xTaskGetTickCount() -
        s_poll_sample[axis][item]) * 1000U / configTICK_RATE_HZ) : UINT32_MAX;
    taskEXIT_CRITICAL();
    return true;
}
static uint8_t s_emergency_pending;
static uint8_t s_feedback_stop_pending;
static uint8_t s_enable_feedback_failures[SERVO_AXIS_COUNT];
static uint8_t s_feedback_failures[SERVO_AXIS_COUNT][2];
static uint32_t s_feedback_transaction[2][3], s_feedback_sequence[2][3];
static uint32_t s_feedback_failure_tick[2][3], s_feedback_result[2][3];
static uint32_t s_feedback_first_transaction[2][3], s_feedback_first_result[2][3];
static servo_recovery_status_t s_recovery;
static uint8_t s_recovery_disqualified, s_recovery_observing;
static uint8_t s_recovery_candidate;
static uint8_t s_recovery_request, s_recovery_isolated;
static uint16_t s_pending_tag;
static uint8_t s_degraded_active;
static int16_t s_degraded_limit[2];
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
static uint32_t s_link_health_flags[2];
void aimtor_rs485_diag_snapshot(app_link_diag_snapshot_t *out)
{
    unsigned axis,at=3U;
    taskENTER_CRITICAL();
    out->sampled_ms=modbus_diag_millis();
    out->words[0]=(uint32_t)app_control_arbiter_owner();out->words[1]=s_pending_id;
    out->words[2]=((uint32_t)s_pending_cancelled<<24U)|((uint32_t)s_pending_axis<<16U)|s_pending_tag;
    for(axis=0U;axis<2U;++axis) {
        const servo_snapshot_t *s=&s_axes[axis].snapshot;
        out->words[at++]=((uint32_t)(uint16_t)s->actual_speed_rpm<<16U)|(uint16_t)s->speed_write_rpm;
        out->words[at++]=s_axes[axis].speed_sequence;
        out->words[at++]=(uint32_t)((uint64_t)(xTaskGetTickCount()-s_axes[axis].speed_tick)*1000U/configTICK_RATE_HZ);
        out->words[at++]=s->speed_write_sequence;out->words[at++]=s->speed_write_completed;
        out->words[at++]=s->speed_write_tick;
        out->words[at++]=((uint32_t)s->speed_write_result<<24U)|((uint32_t)s->speed_write_rpm_valid<<16U)|
            ((uint32_t)s_axes[axis].speed_valid<<8U)|s->stop_latched;
        out->words[at++]=((uint32_t)s->status_word<<16U)|s->fault_code;
        out->words[at++]=((uint32_t)s->enable_feedback_valid<<24U)|((uint32_t)s->enable_feedback<<16U)|
            ((uint32_t)s->online<<8U)|s->enabled;
    }
    out->words[at++]=((uint32_t)s_axes[0].steps[0].words[0]<<16U)|s_axes[1].steps[0].words[0];
    out->words[at++]=((uint32_t)s_axes[0].action<<24U)|((uint32_t)s_axes[1].action<<16U)|
        ((uint32_t)s_axes[0].snapshot.busy<<8U)|s_axes[1].snapshot.busy;
    out->words[at++]=((uint32_t)s_axes[0].snapshot.consecutive_failures<<16U)|s_axes[1].snapshot.consecutive_failures;
    out->count=(uint8_t)at;
    taskEXIT_CRITICAL();
}
static void aimtor_diag_speed(const modbus_response_t *r,aimtor_axis_state_t *state)
{
    modbus_diag_speed(r,state->snapshot.actual_speed_rpm,
        state->snapshot.speed_write_rpm,state->snapshot.speed_write_rpm_valid,
        (uint16_t)(((uint16_t)state->snapshot.enabled<<8U)|state->snapshot.stop_latched));
}
#else
#define aimtor_diag_speed(r,s) ((void)0)
#endif
static servo_health_level_t aimtor_axis_health(servo_axis_t axis, servo_axis_health_t *health);
static uint8_t aimtor_degraded(void);
static void aimtor_feedback_valid(uint8_t axis, uint8_t item, uint32_t transaction, TickType_t now);
static TickType_t s_recovery_healthy_tick, s_recovery_prepare_tick;
static uint8_t s_recovery_samples[2][3];
static uint32_t s_recovery_floor[2][3];
static void aimtor_recovery_service(TickType_t now);
static uint8_t s_remote_control_mask;
static uint8_t s_hmi_mask;
static uint8_t s_can_mask;
static uint8_t s_sync_poll_phase;
#if (APP_POSITION_MONITOR_ENABLED == 1U)
static uint8_t s_position_pair_pending;
static servo_axis_t s_position_pair_axis;
#endif

static uint8_t aimtor_sync_poll_active(void)
{
    return APP_REMOTE_SERVO_SYNC_ENABLED &&
           !app_config_servo_readonly_test() &&
           (s_remote_control_mask == APP_SERVO_CONTROL_AXIS_BOTH ||
            /* Revoking motion permission starts a stop; the retained mask
             * still needs paired feedback until zero/release is confirmed. */
            s_hmi_mask == APP_SERVO_CONTROL_AXIS_BOTH ||
            (APP_CAN_CONTROL_ENABLED && s_can_mask == APP_SERVO_CONTROL_AXIS_BOTH));
}



static uint8_t aimtor_is_diagnostic_tag(uint16_t tag)
{
    return (uint8_t)(tag >= AIMTOR_TAG_DIAG_TORQUE &&
                     tag <= AIMTOR_TAG_DIAG_BUS_VOLTAGE);
}

static uint8_t aimtor_diagnostic_index(uint16_t tag)
{
    return (uint8_t)(tag - AIMTOR_TAG_DIAG_TORQUE);
}

static uint16_t aimtor_diagnostic_address(uint16_t tag)
{
    switch (tag)
    {
    case AIMTOR_TAG_DIAG_TORQUE: return AIMTOR_REG_REALTIME_TORQUE;
    case AIMTOR_TAG_DIAG_LOAD: return AIMTOR_REG_LOAD_RATIO;
    case AIMTOR_TAG_DIAG_CURRENT: return AIMTOR_REG_PHASE_CURRENT_RMS;
    default: return AIMTOR_REG_BUS_VOLTAGE;
    }
}

static uint8_t aimtor_diagnostic_count(uint16_t tag)
{
    return tag == AIMTOR_TAG_DIAG_CURRENT ? 2U : 1U;
}

static void aimtor_record_speed_sample(aimtor_axis_state_t *state)
{
    servo_speed_sample_t *samples = state->snapshot.speed_samples;
    memmove(samples, samples + 1U,
            (SERVO_SPEED_SAMPLE_CAPACITY - 1U) * sizeof(*samples));
    samples[SERVO_SPEED_SAMPLE_CAPACITY - 1U].actual_speed_rpm =
        state->snapshot.actual_speed_rpm;
    samples[SERVO_SPEED_SAMPLE_CAPACITY - 1U].speed_sequence =
        state->speed_sequence;
    samples[SERVO_SPEED_SAMPLE_CAPACITY - 1U].sample_tick = state->speed_tick;
    samples[SERVO_SPEED_SAMPLE_CAPACITY - 1U].valid = 1U;
}
static servo_boot_zero_status_t s_boot_zero[SERVO_AXIS_COUNT];
static uint8_t s_boot_zero_internal;
static uint8_t s_boot_stage[SERVO_AXIS_COUNT];
static TickType_t s_boot_deadline[SERVO_AXIS_COUNT];
static TickType_t s_boot_retry[SERVO_AXIS_COUNT];
static uint32_t s_boot_position_sequence[SERVO_AXIS_COUNT];
static servo_startup_stop_status_t s_startup_stop[SERVO_AXIS_COUNT];
static TickType_t s_startup_deadline[SERVO_AXIS_COUNT];
static servo_cold_clear_status_t s_cold[2];
static servo_cold_clear_state_t s_cold_phase;
static uint8_t s_cold_initialized, s_cold_started, s_cold_online, s_cold_turn;
static uint8_t s_cold_read_step[2], s_cold_seen[2], s_cold_post[2];
static TickType_t s_cold_start, s_cold_online_tick, s_cold_zero_tick[2];
static uint16_t s_cold_pending_address, s_cold_pending_value;
static uint8_t s_cold_pending_write;
static void aimtor_cold_clear_service(TickType_t now);
static void aimtor_cold_clear_response(const modbus_response_t *response);

static TickType_t s_startup_retry[SERVO_AXIS_COUNT];
static TickType_t s_startup_zero_tick[SERVO_AXIS_COUNT];
static uint32_t s_startup_speed_sequence[SERVO_AXIS_COUNT];
static uint8_t s_startup_zero_samples[SERVO_AXIS_COUNT];
static servo_wdg_recovery_status_t s_wdg_recovery[SERVO_AXIS_COUNT];
static TickType_t s_wdg_deadline[SERVO_AXIS_COUNT];
static TickType_t s_wdg_retry[SERVO_AXIS_COUNT];
static uint8_t s_wdg_poll_phase[SERVO_AXIS_COUNT];
static uint8_t s_wdg_step_attempts[SERVO_AXIS_COUNT];
static uint8_t s_wdg_manual_attempts[SERVO_AXIS_COUNT];
static TickType_t s_wdg_stop_tick[SERVO_AXIS_COUNT];
static TickType_t s_wdg_zero_tick[SERVO_AXIS_COUNT];
static uint8_t s_wdg_axis_turn;
static const uint16_t s_wdg_read_addresses[] = {
    AIMTOR_REG_ACTUAL_SPEED, AIMTOR_REG_FAULT_CODE,
    AIMTOR_REG_SERVO_ENABLE, AIMTOR_REG_CONTROL_MODE,
    AIMTOR_REG_SPEED_SELECTION, AIMTOR_REG_SPEED_SOURCE,
    AIMTOR_REG_SPEED_COMMAND
};
static servo_startup_hold_status_t s_hold[SERVO_AXIS_COUNT];
static servo_k1_hold_status_t s_k1_hold;
static uint32_t s_k1_enable_floor;
static TickType_t s_hold_deadline;
static uint32_t s_hold_speed_sequence[SERVO_AXIS_COUNT];
static TickType_t s_hold_verify_tick[SERVO_AXIS_COUNT];
static TickType_t s_hold_enable_tick[SERVO_AXIS_COUNT];
static TickType_t s_hold_enable_poll[SERVO_AXIS_COUNT];
static uint8_t s_hold_enable_confirmed[SERVO_AXIS_COUNT];
static uint8_t s_motion_authorized;
static uint8_t s_stop_write_internal;
static uint8_t s_remote_stop_active;
static int16_t s_last_velocity[SERVO_AXIS_COUNT];
static uint8_t s_hold_k1_rearm;
static TickType_t s_hold_zero_retry[SERVO_AXIS_COUNT];
static uint8_t s_cli_mask;
static uint8_t s_cli_authorized;
static uint8_t s_cli_internal;
static uint8_t s_cli_gate_enabled;
static uint8_t s_cli_restore;
static uint8_t s_cli_abort;
static uint8_t s_remote_prepare_internal;
static uint8_t s_hmi_authorized;
static uint8_t s_can_authorized;
static uint32_t s_can_generation;
static uint32_t s_can_action_generation[2];
static uint32_t s_can_action_batch[2], s_can_action_sequence[2];
static struct { uint32_t transaction, generation, batch, sequence; uint8_t axis, stop, valid; } s_can_pending;
static uint8_t s_can_action[2], s_can_action_stop[2], s_can_internal;
static uint8_t s_controlled_safety_active;
static uint8_t s_controlled_safety_offline;
static int16_t s_controlled_safety_command[SERVO_AXIS_COUNT];
static int16_t s_safety_initial[SERVO_AXIS_COUNT];
static int16_t s_safety_pending_rpm;
static TickType_t s_safety_started, s_safety_zero_tick[2];
static uint32_t s_safety_speed_sequence[2];
static uint8_t s_safety_zero_samples[2], s_safety_zero_written[2];
static uint8_t s_safety_turn, s_safety_phase;
static servo_safety_stop_status_t s_safety_status;
static uint8_t aimtor_runtime_active(const aimtor_axis_state_t *state)
{
    return (uint8_t)(state->snapshot.enabled &&
        (state->snapshot.actual_speed_rpm != 0 ||
         s_last_velocity[(uint8_t)(state - s_axes)] != 0 ||
         s_motion_authorized || s_hmi_authorized || s_can_authorized ||
         s_cli_authorized || s_remote_control_mask));
}

static uint8_t aimtor_runtime_feedback_ok(const aimtor_axis_state_t *state)
{
    uint8_t axis = (uint8_t)(state - s_axes);
    return s_feedback_failures[axis][0] < APP_SERVO_RUNTIME_COMM_FAILURES &&
        s_feedback_failures[axis][1] < APP_SERVO_RUNTIME_COMM_FAILURES &&
        s_enable_feedback_failures[axis] < APP_SERVO_RUNTIME_COMM_FAILURES;
}

const char *aimtor_recovery_state_name(servo_recovery_state_t state)
{
    static const char *const names[] = {
        "none", "wait_health", "ready", "requested",
        "preparing", "commit", "done", "failed"
    };
    return state <= SERVO_RECOVERY_FAILED ? names[state] : "invalid";
}


static modbus_status_t aimtor_submit_step(servo_axis_t axis, uint16_t tag,
                                         aimtor_step_t *step, uint8_t read);

void aimtor_cli_gate_init(void)
{
    s_cli_mask = 0U;
    s_cli_authorized = 0U;
    s_cli_internal = 0U;
    s_cli_restore = 0U;
    s_cli_abort = 0U;
    s_cli_gate_enabled = 1U;
    s_remote_prepare_internal = 0U;
    s_hmi_mask = 0U;
    s_hmi_authorized = 0U;
    s_can_mask = 0U;
    s_can_authorized = 0U;
    memset(&s_can_pending, 0, sizeof(s_can_pending));
}

void aimtor_cli_take_axis(servo_axis_t axis)
{
    taskENTER_CRITICAL();
    s_cli_mask = axis < SERVO_AXIS_COUNT ? (uint8_t)(1U << axis) : 0U;
    s_cli_authorized = 0U;
    s_cli_restore = 0U;
    s_cli_abort = 0U;
    /* Do not adopt a software-ramped axis for direct CLI RPM with the 1ms guard.
       The first speed request must use the existing zero-speed mode preparation. */
    if (axis < SERVO_AXIS_COUNT &&
        s_axes[axis].snapshot.control_mode == SERVO_CONTROL_RS485_VELOCITY &&
        s_axes[axis].snapshot.ramp_requested_ms[0] != APP_SERVO_DIRECT_ACCELERATION_RAMP_MS)
        s_axes[axis].snapshot.parameter_verified = 0U;
    taskEXIT_CRITICAL();
}

void aimtor_cli_release_axis(void)
{
    taskENTER_CRITICAL();
    {
        uint8_t axis;
        for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
            if ((s_cli_mask & (1U << axis)) &&
                s_axes[axis].snapshot.ramp_requested_ms[0] != APP_SERVO_SPEED_ACCELERATION_RAMP_MS)
                s_axes[axis].snapshot.parameter_verified = 0U;
    }
    s_cli_mask = s_cli_authorized = s_cli_internal = s_cli_restore = 0U;
    taskEXIT_CRITICAL();
}

void aimtor_cli_authorize(bool authorized)
{
    s_cli_authorized = authorized && aimtor_cold_clear_ready() && !aimtor_recovery_blocks_motion() && !aimtor_degraded() && s_cli_mask &&
        !app_health_watchdog_test_requested() ? 1U : 0U;
}

void aimtor_cli_internal(bool active)
{
    s_cli_internal = active && s_cli_mask ? 1U : 0U;
}

bool aimtor_cli_control_active(void) { return s_cli_mask != 0U; }
bool aimtor_cli_axis_owned(servo_axis_t axis)
{
    return axis < SERVO_AXIS_COUNT && (s_cli_mask & (1U << axis)) != 0U;
}
bool aimtor_cli_restoring_hold(void) { return s_cli_restore != 0U; }

/* 遥控准备仅开放模式和使能，不把准备阶段当作普通运动授权。 */
static servo_result_t aimtor_remote_prepare(servo_axis_t axis, uint8_t enable)
{
    uint8_t index;
    servo_result_t result = SERVO_NOT_READY;
    taskENTER_CRITICAL();
    if (!app_health_watchdog_test_requested() &&
        !s_cli_mask && !s_remote_stop_active && !s_hmi_authorized &&
        !s_can_authorized &&
        aimtor_watchdog_recovery_ready() && aimtor_startup_stop_ready() &&
        aimtor_boot_zero_ready() && aimtor_startup_hold_ready())
    {
        for (index = 0U; index < SERVO_AXIS_COUNT; index++)
        {
            servo_snapshot_t snapshot;
            if (!app_config_servo_axis_selected(index)) continue;
            (void)aimtor_get_snapshot((servo_axis_t)index, &snapshot);
            if (snapshot.feedback_degraded || !snapshot.online || snapshot.fault ||
                !snapshot.speed_valid || !snapshot.fault_valid ||
                snapshot.speed_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
                snapshot.fault_age_ms >= APP_SERVO_STATUS_MAX_AGE_MS ||
                snapshot.actual_speed_rpm != 0 || snapshot.stop_latched)
                break;
        }
        if (index == SERVO_AXIS_COUNT)
        {
            s_remote_prepare_internal = 1U;
            result = enable ? aimtor_enable(axis) :
                aimtor_set_control_mode(axis, SERVO_CONTROL_RS485_VELOCITY);
            s_remote_prepare_internal = 0U;
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

servo_result_t aimtor_remote_prepare_mode(servo_axis_t axis)
{
    return aimtor_remote_prepare(axis, 0U);
}

servo_result_t aimtor_remote_prepare_enable(servo_axis_t axis)
{
    return aimtor_remote_prepare(axis, 1U);
}

void aimtor_cli_request_abort(uint8_t reason)
{
    taskENTER_CRITICAL();
    /* 安全故障不得被后续人工事件覆盖为可解锁原因。 */
    if (!s_cli_abort || reason == 4U) s_cli_abort = reason;
    s_cli_authorized = 0U;
    taskEXIT_CRITICAL();
}

uint8_t aimtor_cli_take_abort(void)
{
    uint8_t reason;
    taskENTER_CRITICAL();
    reason = s_cli_abort;
    s_cli_abort = 0U;
    taskEXIT_CRITICAL();
    return reason;
}

bool aimtor_startup_hold_active(void)
{
    uint8_t axis;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        if (s_hold[axis].eligible) return true;
    return false;
}

bool aimtor_startup_hold_ready(void)
{
    uint8_t axis;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        if (s_hold[axis].eligible &&
            !(aimtor_cli_axis_owned((servo_axis_t)axis) && !s_cli_restore) &&
            s_hold[axis].state != SERVO_STARTUP_HOLD_READY) return false;
    return true;
}

void aimtor_set_motion_authorized(bool authorized)
{
    taskENTER_CRITICAL();
    s_motion_authorized = authorized && aimtor_cold_clear_ready() && modbus_motion_permitted() && !aimtor_recovery_blocks_motion() && !aimtor_degraded() &&
        !app_health_watchdog_test_requested() &&
        s_k1_hold.state == SERVO_K1_HOLD_INACTIVE &&
        aimtor_boot_zero_ready() &&
        aimtor_startup_hold_ready() ? 1U : 0U;
    taskEXIT_CRITICAL();
}

bool aimtor_motion_allowed(void)
{
    if (app_sync_params_maintenance() || app_control_arbiter_arm_locked()) return false;
    return aimtor_cold_clear_ready() && modbus_motion_permitted() && !s_controlled_safety_active && !aimtor_recovery_blocks_motion() &&
        !app_health_watchdog_test_requested() && !s_remote_stop_active &&
        (!s_can_mask || s_can_internal) &&
        s_k1_hold.state == SERVO_K1_HOLD_INACTIVE &&
        aimtor_boot_zero_ready() &&
        (aimtor_startup_hold_ready() || (s_cli_internal && s_cli_restore)) &&
        (s_cli_mask ? (s_cli_authorized || s_cli_internal) :
        ((s_hmi_authorized || s_can_authorized) ? 1U :
          (s_cli_gate_enabled ? s_motion_authorized :
           (!aimtor_startup_hold_active() || s_motion_authorized))));
}

void aimtor_set_remote_stop_active(bool active)
{
    taskENTER_CRITICAL();
    s_remote_stop_active = active ? 1U : 0U;
    taskEXIT_CRITICAL();
}

void aimtor_set_can_control(bool authorized, uint8_t mask)
{
    taskENTER_CRITICAL();
    s_can_authorized = authorized && aimtor_cold_clear_ready() && !aimtor_recovery_blocks_motion() && !aimtor_degraded() && mask && !app_health_watchdog_test_requested() ? 1U : 0U;
    s_can_mask = s_can_authorized ? (uint8_t)(mask & app_config_servo_control_axis_mask()) : 0U;
    taskEXIT_CRITICAL();
}

bool aimtor_can_control_active(void) { return s_can_authorized != 0U; }

bool aimtor_startup_hold_get_status(
    servo_axis_t axis, servo_startup_hold_status_t *status)
{
    if (axis >= SERVO_AXIS_COUNT || status == NULL) return false;
    taskENTER_CRITICAL();
    *status = s_hold[axis];
    taskEXIT_CRITICAL();
    return true;
}

const char *aimtor_startup_hold_state_name(servo_startup_hold_state_t state)
{
    static const char *const names[] = {
        "disabled", "waiting", "preparing", "zero", "enabling",
        "verifying", "ready", "failed"
    };
    return state <= SERVO_STARTUP_HOLD_FAILED ? names[state] : "invalid";
}

/* 看门狗恢复服务位于事务提交函数之前，先声明异步提交接口。 */
static modbus_status_t aimtor_submit_step(servo_axis_t axis,
                                          uint16_t tag,
                                          aimtor_step_t *step,
                                          uint8_t read);

static void aimtor_watchdog_recovery_fail(servo_axis_t axis,
                                          uint16_t reason)
{
    s_wdg_recovery[axis].state = SERVO_WDG_RECOVERY_FAILED;
    s_wdg_recovery[axis].failure_reason = reason;
    s_axes[axis].snapshot.busy = 0U;
    s_axes[axis].action = ACTION_NONE;
    s_axes[axis].snapshot.last_result = SERVO_NOT_READY;
    s_axes[axis].snapshot.stop_latched = 1U;
    s_axes[axis].snapshot.parameter_verified = 0U;
    s_axes[axis].disable_pending = 1U;
    s_wdg_retry[axis] = xTaskGetTickCount();
    s_motion_authorized = s_cli_authorized = 0U;
    (void)servo_pulse_set_enable(axis, false);
}

bool aimtor_startup_stop_ready(void)
{
    uint8_t axis;
    if (!aimtor_cold_clear_ready()) return false;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        if (s_startup_stop[axis].eligible &&
            s_startup_stop[axis].state != SERVO_STARTUP_STOP_DONE)
            return false;
    return true;
}

bool aimtor_watchdog_recovery_ready(void)
{
    uint8_t axis;

    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (s_wdg_recovery[axis].eligible &&
            s_wdg_recovery[axis].state != SERVO_WDG_RECOVERY_DONE)
        {
            return false;
        }
    }
    return true;
}

void aimtor_watchdog_recovery_init(uint8_t is_watchdog_reset)
{
    uint8_t axis;
    if (!APP_UNPROTECTED_MOTION_TEST_ENABLED) is_watchdog_reset = 0U;

    memset(s_wdg_recovery, 0, sizeof(s_wdg_recovery));
    memset(s_wdg_deadline, 0, sizeof(s_wdg_deadline));
    memset(s_wdg_retry, 0, sizeof(s_wdg_retry));
    memset(s_wdg_poll_phase, 0, sizeof(s_wdg_poll_phase));
    memset(s_wdg_step_attempts, 0, sizeof(s_wdg_step_attempts));
    memset(s_wdg_manual_attempts, 0, sizeof(s_wdg_manual_attempts));
    memset(s_wdg_stop_tick, 0, sizeof(s_wdg_stop_tick));
    memset(s_wdg_zero_tick, 0, sizeof(s_wdg_zero_tick));
    s_wdg_axis_turn = 0U;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_config_servo_axis_selected(axis) ||
            app_config_servo_readonly_test())
        {
            s_wdg_recovery[axis].state = SERVO_WDG_RECOVERY_SKIPPED;
            continue;
        }
        s_wdg_recovery[axis].eligible = is_watchdog_reset != 0U ? 1U : 0U;
        s_wdg_recovery[axis].state = is_watchdog_reset != 0U ?
            SERVO_WDG_RECOVERY_STOP_LOCAL : SERVO_WDG_RECOVERY_SKIPPED;
        if (is_watchdog_reset != 0U)
        {
            /* 看门狗恢复的本地处理不依赖总线，先关闭脉冲和旧授权。 */
            (void)servo_pulse_set_enable((servo_axis_t)axis, false);
            s_axes[axis].snapshot.enabled = 0U;
            s_axes[axis].snapshot.disable_verified = 0U;
            s_axes[axis].snapshot.parameter_verified = 0U;
            s_axes[axis].snapshot.stop_latched = 0U;
            s_axes[axis].snapshot.busy = 0U;
            s_axes[axis].snapshot.last_result = SERVO_NOT_READY;
            s_axes[axis].action = ACTION_NONE;
            s_axes[axis].disable_pending = 0U;
            s_axes[axis].speed_valid = s_axes[axis].fault_valid = 0U;
            s_axes[axis].snapshot.speed_valid = 0U;
            s_axes[axis].snapshot.fault_valid = 0U;
        }
    }
    s_remote_control_mask = 0U;
    s_motion_authorized = s_cli_authorized = s_cli_internal = 0U;
    s_remote_stop_active = 0U;
    motion_stop(MOTION_STOP_IMMEDIATE);
    motion_set_system_ready(0U);
}

bool aimtor_watchdog_recovery_get_status(
    servo_axis_t axis, servo_wdg_recovery_status_t *status)
{
    if (axis >= SERVO_AXIS_COUNT || status == NULL) return false;
    taskENTER_CRITICAL();
    *status = s_wdg_recovery[axis];
    taskEXIT_CRITICAL();
    return true;
}

const char *aimtor_watchdog_recovery_state_name(
    servo_wdg_recovery_state_t state)
{
    static const char *const names[] = {
        "skipped", "waiting", "stop_local", "wait_online",
        "write_decel", "verify_decel", "writing_zero",
        "decelerating", "confirm_zero", "cleanup", "disabling", "verifying",
        "done", "failed"
    };
    return state <= SERVO_WDG_RECOVERY_FAILED ? names[state] : "invalid";
}

void aimtor_startup_stop_init(void)
{
    uint8_t axis;
    memset(s_startup_stop, 0, sizeof(s_startup_stop));
    memset(s_startup_zero_samples, 0, sizeof(s_startup_zero_samples));
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (app_config_servo_axis_selected(axis) &&
            !app_config_servo_readonly_test())
        {
            s_startup_stop[axis].eligible = 1U;
            s_startup_stop[axis].state = SERVO_STARTUP_STOP_WAITING;
            (void)servo_pulse_set_enable((servo_axis_t)axis, false);
        }
    }
}

bool aimtor_startup_stop_get_status(servo_axis_t axis,
                                  servo_startup_stop_status_t *status)
{
    if (axis >= SERVO_AXIS_COUNT || status == NULL) return false;
    taskENTER_CRITICAL();
    *status = s_startup_stop[axis];
    taskEXIT_CRITICAL();
    return true;
}

const char *aimtor_startup_stop_state_name(servo_startup_stop_state_t state)
{
    static const char *const names[] = {
        "skipped", "waiting", "disabling", "waiting_zero",
        "configuring", "verifying", "done", "failed"
    };
    return state <= SERVO_STARTUP_STOP_FAILED ? names[state] : "invalid";
}

bool aimtor_boot_zero_ready(void)
{
    uint8_t axis;
    /* 统一运动门禁，非POR启动也必须完成安全停车。 */
    if (!aimtor_watchdog_recovery_ready()) return false;
    if (!aimtor_startup_stop_ready()) return false;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (s_boot_zero[axis].eligible &&
            s_boot_zero[axis].state != SERVO_BOOT_ZERO_DONE)
        {
            return false;
        }
    }
    return true;
}

void aimtor_boot_zero_init(uint32_t reset_reason)
{
    uint8_t axis;
    /* 仅真正POR设零；PIN/BOR伴随上电允许，软件或看门狗标志排除。 */
    memset(s_boot_zero, 0, sizeof(s_boot_zero));
    memset(s_boot_stage, 0, sizeof(s_boot_stage));
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if ((reset_reason & (1UL << 27U)) &&
            !(reset_reason & ((1UL << 28U) | (1UL << 29U) | (1UL << 30U))) &&
            !app_config_servo_readonly_test() &&
            app_config_servo_axis_selected(axis))
        {
            s_boot_zero[axis].eligible = 1U;
            s_boot_zero[axis].state = SERVO_BOOT_ZERO_WAITING;
        }
    }
}

static void aimtor_startup_hold_reset(void)
{
    memset(s_hold, 0, sizeof(s_hold));
    memset(s_last_velocity, 0, sizeof(s_last_velocity));
    memset(s_hold_enable_confirmed, 0, sizeof(s_hold_enable_confirmed));
    memset(s_hold_enable_poll, 0, sizeof(s_hold_enable_poll));
    s_hold_deadline = 0U;
    s_hold_k1_rearm = 0U;
    memset(s_hold_zero_retry, 0, sizeof(s_hold_zero_retry));
    s_motion_authorized = 0U;
    s_stop_write_internal = 0U;
}

void aimtor_startup_hold_init(void)
{
    uint8_t axis;
    aimtor_startup_hold_reset();
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (APP_SERVO_STARTUP_HOLD_ENABLED &&
            !app_config_servo_readonly_test() &&
            app_config_servo_axis_selected(axis))
        {
            s_hold[axis].eligible = 1U;
            s_hold[axis].state = SERVO_STARTUP_HOLD_WAITING;
        }
    }
}

bool aimtor_boot_zero_get_status(servo_axis_t axis,
                                servo_boot_zero_status_t *status)
{
    if (axis >= SERVO_AXIS_COUNT || status == NULL) return false;
    taskENTER_CRITICAL();
    *status = s_boot_zero[axis];
    taskEXIT_CRITICAL();
    return true;
}

const char *aimtor_boot_zero_state_name(servo_boot_zero_state_t state)
{
    static const char *const names[] = {
        "skipped_non_power_reset", "waiting", "preparing", "writing",
        "verifying", "done", "failed"
    };
    return state <= SERVO_BOOT_ZERO_FAILED ? names[state] : "invalid";
}

static uint8_t aimtor_fresh(aimtor_axis_state_t *state);

static uint16_t aimtor_read_u16(const uint8_t *data, uint16_t index)
{
    return (uint16_t)(((uint16_t)data[1U + index * 2U] << 8U) |
                     data[2U + index * 2U]);
}

static int32_t aimtor_read_i32(const uint8_t *data)
{
    uint32_t value = (uint32_t)aimtor_read_u16(data, 0U) |
                     ((uint32_t)aimtor_read_u16(data, 1U) << 16U);
    return value <= 0x7FFFFFFFUL ? (int32_t)value :
           -1L - (int32_t)(0xFFFFFFFFUL - value);
}

static int16_t aimtor_round_deciamp(int32_t centiamp)
{
    if (centiamp >= 0) return (int16_t)((centiamp + 5) / 10);
    return (int16_t)(-((-centiamp + 5) / 10));
}

/* 使用有符号差值判断Tick是否到期，兼容Tick计数回绕。 */
static uint8_t aimtor_tick_due(TickType_t now, TickType_t deadline)
{
    return (uint8_t)(((int32_t)(now - deadline)) >= 0);
}

/* 将Tick年龄换算为毫秒；未采集过的数据由调用方使用UINT32_MAX表示。 */
static uint32_t aimtor_tick_age_ms(TickType_t now, TickType_t tick)
{
    return (uint32_t)(((uint64_t)(now - tick) * 1000ULL) /
                      (uint64_t)configTICK_RATE_HZ);
}

static servo_health_level_t aimtor_axis_health(servo_axis_t axis, servo_axis_health_t *health)
{
    uint8_t item;
    TickType_t now = xTaskGetTickCount();
    servo_health_level_t level = SERVO_HEALTH_NORMAL;
    for (item = 0U; item < 3U; ++item)
    {
        TickType_t tick = item == 0U ? s_axes[axis].speed_tick : item == 1U ?
            s_axes[axis].fault_tick : s_axes[axis].snapshot.enable_feedback_tick;
        uint8_t valid = item == 0U ? s_axes[axis].speed_valid : item == 1U ?
            s_axes[axis].fault_valid : s_axes[axis].snapshot.enable_feedback_valid;
        uint32_t age = valid ? aimtor_tick_age_ms(now, tick) : UINT32_MAX;
        uint8_t failures = item == 2U ? s_enable_feedback_failures[axis] : s_feedback_failures[axis][item];
        servo_health_level_t current = failures ? SERVO_HEALTH_DEGRADED : SERVO_HEALTH_NORMAL;
        if (failures >= APP_SERVO_RUNTIME_COMM_FAILURES || age >= APP_SERVO_STATUS_MAX_AGE_MS)
            current = SERVO_HEALTH_STOP;
        if (current > level) level = current;
        if (health)
        {
            servo_feedback_health_t *h = &health->item[item];
            h->valid_tick = tick; h->valid = valid; h->age_ms = age; h->failures = failures;
            h->valid_sequence = s_feedback_sequence[axis][item];
            h->transaction = s_feedback_transaction[axis][item];
            h->result = s_feedback_result[axis][item];
            h->address = item == 0U ? AIMTOR_REG_ACTUAL_SPEED : item == 1U ? AIMTOR_REG_FAULT_CODE : AIMTOR_REG_SERVO_ENABLE;
            h->first_failure_tick = s_feedback_failure_tick[axis][item]; h->level = current;
        }
    }
    if (health) health->level = level;
    return level;
}

#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
/* A separate task-level observer keeps logging off the deep writer/stop chain.
 * Only cached scalars are read; it never submits a feedback query. */
__attribute__((noinline)) void aimtor_rs485_diag_observe(void)
{
    uint8_t axis,item;
    uint32_t flags,old,counts;
    TickType_t now=xTaskGetTickCount();
    taskENTER_CRITICAL();
    for(axis=0U;axis<2U;++axis) {
        const aimtor_axis_state_t *state=&s_axes[axis];
        if(!app_config_servo_axis_selected(axis))continue;
        counts=((uint32_t)s_feedback_stop_pending<<24U)|((uint32_t)s_enable_feedback_failures[axis]<<16U)|
            ((uint32_t)s_feedback_failures[axis][1]<<8U)|s_feedback_failures[axis][0];
        flags=0x80000000UL|state->snapshot.fault_code|
            ((uint32_t)state->snapshot.enable_feedback<<16U)|((uint32_t)state->snapshot.enabled<<17U)|
            ((uint32_t)state->speed_valid<<18U)|((uint32_t)state->fault_valid<<19U)|
            ((uint32_t)state->snapshot.enable_feedback_valid<<20U)|((uint32_t)state->snapshot.online<<21U)|
            ((uint32_t)state->snapshot.stop_latched<<22U)|((uint32_t)((counts&0xFFFFFFU)!=0U)<<23U)|
            ((uint32_t)aimtor_axis_health((servo_axis_t)axis,NULL)<<24U)|
            ((uint32_t)s_controlled_safety_active<<26U)|((uint32_t)s_controlled_safety_offline<<27U);
        old=s_link_health_flags[axis];
        if(flags==old)continue;
        s_link_health_flags[axis]=flags;
        item=(state->snapshot.fault_code && state->fault_valid)?1U:
            (s_feedback_failures[axis][0] || !state->speed_valid ||
             aimtor_tick_age_ms(now,state->speed_tick)>=APP_SERVO_STATUS_MAX_AGE_MS)?0U:
            (s_feedback_failures[axis][1] || !state->fault_valid ||
             aimtor_tick_age_ms(now,state->fault_tick)>=APP_SERVO_STATUS_MAX_AGE_MS)?1U:2U;
        app_link_diag_record(APP_LINK_RS485,0x22U,
            ((uint32_t)app_control_arbiter_owner()<<24U)|((uint32_t)item<<16U)|axis,
            old,flags,counts,s_feedback_transaction[axis][item],
            aimtor_tick_age_ms(now,item==0U?state->speed_tick:item==1U?state->fault_tick:state->snapshot.enable_feedback_tick));
        /* Initial invalid/unknown feedback is a baseline, not a run failure.
         * Established validity loss, rising health severity, a new valid fault,
         * or an unexpected enable loss retains the first pending evidence. */
        if(((flags&0x00080000UL) && (flags&0xFFFFU) && (uint16_t)flags!=(uint16_t)old) ||
           ((flags&0x00800000UL) && !(old&0x00800000UL)) ||
           ((flags&0x00130000UL)==0x00120000UL &&
            (!(old&0x80000000UL) || (old&0x00010000UL))) ||
           ((old&0x80000000UL) &&
            (((old&~flags)&0x001C0000UL) ||
             ((flags&0x03000000UL)>(old&0x03000000UL)) ||
             ((flags&0x04000000UL) && !(old&0x04000000UL)))))
            app_link_diag_freeze6(APP_LINK_RS485,0x22U,
                ((uint32_t)app_control_arbiter_owner()<<24U)|((uint32_t)item<<16U)|axis,
                old,flags,counts,s_feedback_transaction[axis][item],
                aimtor_tick_age_ms(now,item==0U?state->speed_tick:item==1U?state->fault_tick:state->snapshot.enable_feedback_tick));
    }
    taskEXIT_CRITICAL();
}
#endif

bool aimtor_get_health(servo_axis_t axis, servo_axis_health_t *health)
{
    if (axis >= SERVO_AXIS_COUNT || !health) return false;
    taskENTER_CRITICAL();
    (void)aimtor_axis_health(axis, health);
    taskEXIT_CRITICAL();
    return true;
}

static uint8_t aimtor_degraded(void)
{
    uint8_t axis;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
        if (app_config_servo_axis_selected(axis) &&
            (s_feedback_failures[axis][0] || s_feedback_failures[axis][1] || s_enable_feedback_failures[axis]))
            return 1U;
    return 0U;
}

static void aimtor_feedback_valid(uint8_t axis, uint8_t item, uint32_t transaction, TickType_t now)
{
    if (!transaction || s_feedback_transaction[axis][item] == transaction) return;
    s_feedback_transaction[axis][item] = transaction;
    s_feedback_result[axis][item] = MODBUS_OK;
    (void)now;
    ++s_feedback_sequence[axis][item];
    if (item == 2U) s_enable_feedback_failures[axis] = 0U;
    else s_feedback_failures[axis][item] = 0U;
}

static TickType_t aimtor_deadline_after(TickType_t now, uint32_t milliseconds)
{
    TickType_t ticks;

    ticks = pdMS_TO_TICKS(milliseconds);
    if (ticks == 0U)
    {
        ticks = 1U;
    }
    return now + ticks;
}

static uint32_t aimtor_watchdog_stop_timeout_ms(int16_t speed_rpm,
                                                int16_t command_rpm)
{
    uint32_t speed;
    uint64_t timeout;

    speed = (uint32_t)(speed_rpm < 0 ? -speed_rpm : speed_rpm);
    if ((uint32_t)(command_rpm < 0 ? -command_rpm : command_rpm) > speed)
    {
        speed = (uint32_t)(command_rpm < 0 ? -command_rpm : command_rpm);
    }
    timeout = ((uint64_t)speed * (uint64_t)APP_WDG_RECOVERY_DECEL_MS) /
              1000ULL;
    timeout += (uint64_t)APP_WDG_RECOVERY_STOP_MARGIN_MS;
    if (timeout < 2000ULL) timeout = 2000ULL;
    if (timeout > 0x3FFFFFFFULL) timeout = 0x3FFFFFFFULL;
    return (uint32_t)timeout;
}

static uint16_t aimtor_poll_address(uint16_t tag)
{
    if (tag == AIMTOR_TAG_STARTUP_OFF) return AIMTOR_REG_SERVO_ENABLE;
    if (tag == AIMTOR_TAG_SPEED)
    {
        return AIMTOR_REG_ACTUAL_SPEED;
    }
    if (tag == AIMTOR_TAG_FAULT)
    {
        return AIMTOR_REG_FAULT_CODE;
    }
    if (tag == AIMTOR_TAG_HOLD_ENABLE) return AIMTOR_REG_SERVO_ENABLE;
    if (tag == AIMTOR_TAG_POSITION)
    {
        return AIMTOR_REG_ABSOLUTE_POSITION;
    }
    if (aimtor_is_diagnostic_tag(tag))
    {
        return aimtor_diagnostic_address(tag);
    }
    return AIMTOR_REG_SERVO_STATE;
}

static uint8_t aimtor_poll_count(uint16_t tag)
{
    if (tag == AIMTOR_TAG_STARTUP_OFF) return 1U;
    if (tag == AIMTOR_TAG_POSITION) return 2U;
    if (aimtor_is_diagnostic_tag(tag)) return aimtor_diagnostic_count(tag);
    return 1U;
}

/* 记录只读轮询成功，并安排下一次同类查询。 */
static void aimtor_poll_success(aimtor_axis_state_t *state, uint16_t tag,
                                TickType_t now)
{
    aimtor_poll_record((uint8_t)(state - s_axes), tag, MODBUS_OK);
    if (aimtor_is_diagnostic_tag(tag))
    {
        state->next_diagnostic_poll[aimtor_diagnostic_index(tag)] =
            aimtor_deadline_after(now, APP_SERVO_DIAGNOSTIC_POLL_MS);
        return;
    }
    state->snapshot.poll_successes++;
    state->snapshot.last_poll_result = MODBUS_OK;
    state->snapshot.last_poll_address = aimtor_poll_address(tag);

    state->snapshot.update_tick = now;
    if (tag == AIMTOR_TAG_SPEED)
    {
        state->next_speed_poll =
            aimtor_deadline_after(now, APP_SERVO_CRITICAL_POLL_MS);
        if (state->probe_pending)
        {
            state->next_fault_poll = now;
        }
    }
    else if (tag == AIMTOR_TAG_FAULT)
    {
        state->next_fault_poll =
            aimtor_deadline_after(now, APP_SERVO_CRITICAL_POLL_MS);
        if (state->probe_pending && state->speed_valid)
        {
            state->probe_pending = 0U;
            state->snapshot.probe_pending = 0U;
        }
    }
    else if (tag == AIMTOR_TAG_POSITION)
    {
        state->next_position_poll =
            aimtor_deadline_after(now, APP_SERVO_BACKGROUND_POLL_MS);
        state->position_priority = 0U;
    }
    else
    {
        state->next_state_poll =
            aimtor_deadline_after(now, 1000U);
    }
}

/* 偶发查询失败保留仍有效的样本；真正离线才进入低频探测。 */
static void aimtor_poll_failure(aimtor_axis_state_t *state, uint16_t tag,
                                modbus_status_t status, uint32_t transaction_id,
                                TickType_t now)
{
    uint8_t axis = (uint8_t)(state - s_axes);
    if (status == MODBUS_QUEUE_FULL || !transaction_id) return;
    aimtor_poll_record(axis, tag, status);
    if (tag == AIMTOR_TAG_SPEED || tag == AIMTOR_TAG_FAULT)
    {
        uint8_t item = tag == AIMTOR_TAG_SPEED ? 0U : 1U;
        if (transaction_id && s_feedback_transaction[axis][item] == transaction_id) return;
        if (!aimtor_degraded())
        { memcpy(s_degraded_limit, s_last_velocity, sizeof(s_degraded_limit)); s_degraded_active = 1U; }
        s_feedback_transaction[axis][item] = transaction_id;
        s_feedback_result[axis][item] = (uint32_t)status;
        if (!s_feedback_failures[axis][item])
        { s_feedback_failure_tick[axis][item] = now;
          s_feedback_first_transaction[axis][item] = transaction_id;
          s_feedback_first_result[axis][item] = status; }
        s_recovery_observing = 0U;
        memset(s_recovery_samples, 0, sizeof(s_recovery_samples));
        memcpy(s_recovery_floor, s_feedback_sequence, sizeof(s_recovery_floor));
        s_recovery.healthy_ms = 0U;
        if (s_recovery.state == SERVO_RECOVERY_READY) s_recovery.state = SERVO_RECOVERY_WAIT_HEALTH;
        if (s_recovery.state == SERVO_RECOVERY_PREPARING || s_recovery.state == SERVO_RECOVERY_COMMIT)
        {
            s_recovery.state = SERVO_RECOVERY_FAILED;
            s_recovery.eligible = 0U;
            s_recovery.failure = 2U;
            s_emergency_pending = 1U;
        }
    }
    if (aimtor_is_diagnostic_tag(tag))
    {
        if (state->snapshot.diagnostic_poll_failures < 0xFFFFFFFFUL)
            state->snapshot.diagnostic_poll_failures++;
        state->snapshot.diagnostic_stale_mask |=
            (uint8_t)(1U << aimtor_diagnostic_index(tag));
        state->next_diagnostic_poll[aimtor_diagnostic_index(tag)] =
            aimtor_deadline_after(now, APP_SERVO_DIAGNOSTIC_RETRY_MS);
        return;
    }
    if (state->snapshot.poll_failures < 0xFFFFFFFFUL)
    {
        state->snapshot.poll_failures++;
    }
    if (state->snapshot.consecutive_failures < 0xFFFFU)
    {
        state->snapshot.consecutive_failures++;
    }
    if ((tag == AIMTOR_TAG_SPEED || tag == AIMTOR_TAG_FAULT) &&
        s_feedback_failures[(uint8_t)(state - s_axes)][tag == AIMTOR_TAG_SPEED ? 0U : 1U] < 255U)
        s_feedback_failures[(uint8_t)(state - s_axes)][tag == AIMTOR_TAG_SPEED ? 0U : 1U]++;
    state->snapshot.last_poll_result = (uint32_t)status;
    state->snapshot.last_poll_address = aimtor_poll_address(tag);
    /*
     * 在清除本次失败对象有效标志前判断旧样本是否仍在500ms有效期内。
     * 一次偶发坏帧不应立即让遥控控制进入急停。
     */
    if (state->speed_valid && state->fault_valid &&
        (aimtor_runtime_active(state) ? aimtor_runtime_feedback_ok(state) :
            state->snapshot.consecutive_failures < APP_SERVO_OFFLINE_FAILURES) &&
        (xTaskGetTickCount() - state->speed_tick) <
            pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS) &&
        (xTaskGetTickCount() - state->fault_tick) <
            pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS))
    {
        /*
         * 不把单次坏帧等同于状态失效，不延长已有样本的寿命。
         * 保持关键查询调度，并在短间隔后再次读取失败的对象。
         */
        if (tag == AIMTOR_TAG_SPEED)
        {
            state->next_speed_poll = aimtor_deadline_after(now, 20U);
        }
        else if (tag == AIMTOR_TAG_FAULT)
        {
            state->next_fault_poll = aimtor_deadline_after(now, 20U);
        }
        else if (tag == AIMTOR_TAG_POSITION)
        {
            state->next_position_poll =
                aimtor_deadline_after(now, APP_SERVO_BACKGROUND_POLL_MS);
        }
        else
        {
            state->next_state_poll =
                aimtor_deadline_after(now, APP_SERVO_BACKGROUND_POLL_MS);
        }
        return;
    }
    /*
     * 只有关键数据自身的查询失败才立即撤销对应有效标志。
     * 后台位置或状态查询失败时保留最近一次有效速度和故障数据，
     * 由数据年龄和后续探测决定在线状态，避免低优先级丢帧放大影响。
     */
    if (tag == AIMTOR_TAG_SPEED)
    {
        state->speed_valid = 0U;
        state->snapshot.speed_valid = 0U;
    }
    else if (tag == AIMTOR_TAG_FAULT)
    {
        state->fault_valid = 0U;
        state->snapshot.fault_valid = 0U;
    }
    if (aimtor_runtime_active(state) || s_controlled_safety_active || aimtor_recovery_blocks_motion())
    {
        state->probe_pending = state->snapshot.probe_pending = 0U;
        state->next_speed_poll = state->next_fault_poll = aimtor_deadline_after(now, 20U);
        return;
    }
    state->probe_pending = 1U;
    state->snapshot.probe_pending = 1U;
    state->next_probe_tick =
        aimtor_deadline_after(now, APP_SERVO_OFFLINE_PROBE_MS);
    state->next_speed_poll = state->next_probe_tick;
    state->next_fault_poll = state->next_probe_tick;
    state->next_position_poll = state->next_probe_tick;
    state->next_state_poll = state->next_probe_tick;
}

static uint8_t aimtor_poll_due(aimtor_axis_state_t *state,
                               uint16_t tag, TickType_t now)
{
    if (state->probe_pending && !aimtor_runtime_active(state) && !aimtor_recovery_blocks_motion())
    {
        if (!aimtor_tick_due(now, state->next_probe_tick))
        {
            return 0U;
        }
        if (!state->speed_valid)
        {
            return tag == AIMTOR_TAG_SPEED;
        }
        return tag == AIMTOR_TAG_FAULT;
    }
    if (tag == AIMTOR_TAG_SPEED)
    {
        return aimtor_tick_due(now, state->next_speed_poll);
    }
    if (tag == AIMTOR_TAG_FAULT)
    {
        return aimtor_tick_due(now, state->next_fault_poll);
    }
    if (!aimtor_fresh(state))
    {
        return 0U;
    }
    if (tag == AIMTOR_TAG_POSITION)
    {
        return aimtor_tick_due(now, state->next_position_poll);
    }
    if (aimtor_is_diagnostic_tag(tag))
    {
        return aimtor_tick_due(now,
            state->next_diagnostic_poll[aimtor_diagnostic_index(tag)]);
    }
    return aimtor_tick_due(now, state->next_state_poll);
}

/* A bounded read may only borrow a slot while every critical sample has room
 * for a full response timeout. Never widen the runtime protection thresholds. */
static uint8_t aimtor_background_safe(TickType_t now)
{
    uint8_t axis;
    uint32_t limit = aimtor_sync_poll_active() ? APP_REMOTE_SERVO_SYNC_MAX_AGE_MS : APP_SERVO_STATUS_MAX_AGE_MS;
    uint32_t reserve = APP_MODBUS_RESPONSE_TIMEOUT_MS + 5U;
    if (!aimtor_startup_stop_ready() || !aimtor_watchdog_recovery_ready() ||
        !aimtor_boot_zero_ready() || !aimtor_startup_hold_ready() ||
        aimtor_degraded() || s_remote_stop_active ||
        (s_hmi_mask && !s_hmi_authorized) || (s_can_mask && !s_can_authorized) ||
        aimtor_recovery_blocks_motion() ||
        (aimtor_sync_poll_active() && s_sync_poll_phase == 1U)) return 0U;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) {
        aimtor_axis_state_t *state = &s_axes[axis];
        if (!app_config_servo_axis_selected(axis)) continue;
        if (!state->speed_valid || !state->fault_valid || !state->snapshot.enable_feedback_valid ||
            state->snapshot.busy || state->disable_pending) return 0U;
        if ((uint64_t)(now - state->speed_tick) * 1000U / configTICK_RATE_HZ + reserve >= limit ||
            (uint64_t)(now - state->fault_tick) * 1000U / configTICK_RATE_HZ + reserve >= APP_SERVO_STATUS_MAX_AGE_MS ||
            (uint64_t)(now - state->snapshot.enable_feedback_tick) * 1000U / configTICK_RATE_HZ + reserve >= APP_SERVO_STATUS_MAX_AGE_MS) return 0U;
    }
    return 1U;
}
static uint8_t aimtor_pick_background(servo_axis_t *axis, uint16_t *tag, TickType_t now)
{
    uint8_t offset, found = 0U, best = 0U;
    TickType_t best_wait = 0U;
    static const uint16_t tags[6] = { AIMTOR_TAG_POSITION, AIMTOR_TAG_STATE,
        AIMTOR_TAG_DIAG_TORQUE, AIMTOR_TAG_DIAG_LOAD, AIMTOR_TAG_DIAG_CURRENT, AIMTOR_TAG_DIAG_BUS_VOLTAGE };
#if (APP_POSITION_MONITOR_ENABLED == 1U)
    if (s_position_pair_pending) {
        s_position_pair_pending = 0U;
        if (app_config_servo_axis_selected((uint8_t)s_position_pair_axis) &&
            aimtor_poll_due(&s_axes[s_position_pair_axis], AIMTOR_TAG_POSITION, now)) {
            *axis = s_position_pair_axis; *tag = AIMTOR_TAG_POSITION; return 1U;
        }
    }
#endif
    for (offset = 0U; offset < 12U; offset++) {
        uint8_t slot = (uint8_t)((s_background_turn + offset) % 12U);
        uint8_t a = slot % 2U, item = slot / 2U;
        aimtor_axis_state_t *state = &s_axes[a];
        if (s_startup_stop[a].state == SERVO_STARTUP_STOP_DISABLING ||
            s_startup_stop[a].state == SERVO_STARTUP_STOP_WAITING_ZERO)
            continue; /* dedicated stop proof below owns the enable read */
        TickType_t due, wait;
        if (!app_config_servo_axis_selected(a) || !aimtor_poll_due(state, tags[item], now)) continue;
        if (item >= 2U && (s_remote_stop_active || !aimtor_startup_stop_ready() ||
            !aimtor_watchdog_recovery_ready() || !aimtor_boot_zero_ready())) continue;
        due = item == 0U ? state->next_position_poll : item == 1U ? state->next_state_poll : state->next_diagnostic_poll[item - 2U];
        wait = now - due;
        if (!found || wait > best_wait) { found = 1U; best = slot; best_wait = wait; }
    }
    if (!found) return 0U;
    *axis = (servo_axis_t)(best % 2U); *tag = tags[best / 2U];
#if APP_POSITION_MONITOR_ENABLED
    if (*tag == AIMTOR_TAG_POSITION && app_config_servo_axis_selected((uint8_t)(1U - *axis)) &&
        aimtor_poll_due(&s_axes[1U - *axis], AIMTOR_TAG_POSITION, now)) {
        s_position_pair_axis = (servo_axis_t)(1U - *axis); s_position_pair_pending = 1U;
    }
#endif
    s_background_turn = (uint8_t)((best + 1U) % 12U);
    return 1U;
}

/* 同一优先级按轴轮转，避免双轴模式下固定优先左轴。 */
static uint8_t aimtor_pick_poll(servo_axis_t *axis, uint16_t *tag,
                                TickType_t now, uint8_t critical_only)
{
    uint8_t offset;
    uint8_t tag_index;
    uint8_t candidate;
    static const uint16_t critical_tags[2] = {
        AIMTOR_TAG_SPEED, AIMTOR_TAG_FAULT
    };

    if (aimtor_sync_poll_active())
    {
        /* 双轴速度成对连续读取，随后给两轴故障各一个到期机会。 */
        for (candidate = 0U; candidate < 4U; candidate++)
        {
            uint8_t phase = s_sync_poll_phase;
            servo_axis_t selected = (servo_axis_t)(phase & 1U);
            uint16_t selected_tag = phase < 2U ? AIMTOR_TAG_SPEED : AIMTOR_TAG_FAULT;
            if ((phase == 1U) ||
                aimtor_poll_due(&s_axes[selected], selected_tag, now))
            {
                *axis = selected;
                *tag = selected_tag;
                s_sync_poll_phase = (uint8_t)((phase + 1U) % 4U);
                return 1U;
            }
            s_sync_poll_phase = phase == 0U ? 2U : (uint8_t)((phase + 1U) % 4U);
        }
        if (critical_only) return 0U;
    }

    /*
     * 定脉冲停止确认期间优先读取编码器位置，避免沿用后台旧样本。
     * 每次只提交一个位置事务，关键速度和故障轮询仍保持优先。
     */
    if (!critical_only && !aimtor_degraded())
    {
        for (offset = 0U; offset < SERVO_AXIS_COUNT; offset++)
        {
            uint8_t selected_axis;

            selected_axis = (uint8_t)((s_poll_axis_turn + offset) %
                                      SERVO_AXIS_COUNT);
            if (!app_config_servo_axis_selected(selected_axis) ||
                s_axes[selected_axis].position_priority == 0U ||
                !aimtor_poll_due(&s_axes[selected_axis],
                                 AIMTOR_TAG_POSITION, now))
            {
                continue;
            }
            *axis = (servo_axis_t)selected_axis;
            *tag = AIMTOR_TAG_POSITION;
            s_poll_axis_turn = (uint8_t)((selected_axis + 1U) %
                                         SERVO_AXIS_COUNT);
            return 1U;
        }
    }

    if (!aimtor_sync_poll_active()) {
        for (offset = 0U; offset < SERVO_AXIS_COUNT; offset++) {
            uint8_t a = (uint8_t)((s_poll_axis_turn + offset) % SERVO_AXIS_COUNT);
            if (!app_config_servo_axis_selected(a)) continue;
            for (tag_index = 0U; tag_index < 2U; tag_index++) {
                uint16_t current = critical_tags[(s_poll_tag_turn[a] + tag_index) % 2U];
                if (!aimtor_poll_due(&s_axes[a], current, now)) continue;
                *axis = (servo_axis_t)a; *tag = current;
                s_poll_axis_turn = (uint8_t)((a + 1U) % SERVO_AXIS_COUNT);
                s_poll_tag_turn[a] = current == AIMTOR_TAG_SPEED ? 1U : 0U;
                return 1U;
            }
        }
    }
    if (!critical_only && !aimtor_degraded()) return aimtor_pick_background(axis, tag, now);
    return 0U;
}

uint8_t aimtor_get_slave_address(servo_axis_t axis)
{
    return axis == SERVO_AXIS_LEFT ? APP_SERVO_LEFT_SLAVE_ADDRESS :
           axis == SERVO_AXIS_RIGHT ? APP_SERVO_RIGHT_SLAVE_ADDRESS : 0U;
}

bool aimtor_register_map_valid(void)
{
    return AIMTOR_REGISTER_MAP_VALID != 0U;
}

/* 状态写入都使用短临界区；序列只由通信任务提交，调用方不等待总线。 */
static uint8_t aimtor_fresh(aimtor_axis_state_t *state)
{
    TickType_t now;

    if (!app_config_servo_axis_selected((uint8_t)(state - s_axes)))
    {
        return 0U;
    }
    now = xTaskGetTickCount();
    return (uint8_t)(state->speed_valid && state->fault_valid &&
        ((aimtor_runtime_active(state) || s_controlled_safety_active || aimtor_recovery_blocks_motion()) ? aimtor_runtime_feedback_ok(state) :
        state->snapshot.consecutive_failures < APP_SERVO_OFFLINE_FAILURES) &&
        now - state->speed_tick < pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS) &&
        now - state->fault_tick < pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS) &&
        (!aimtor_runtime_active(state) || (state->snapshot.enable_feedback_valid &&
         now - state->snapshot.enable_feedback_tick < pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS))));
}

/* These reads use the ordinary bus and genuine response timestamps.  A
 * parameter write/readback pair remains indivisible for this arbitration. */
static uint8_t aimtor_startup_feedback_active(void)
{
    uint8_t axis;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
        if (app_config_servo_axis_selected(axis) &&
            ((s_startup_stop[axis].eligible &&
              (s_startup_stop[axis].state == SERVO_STARTUP_STOP_DISABLING ||
               s_startup_stop[axis].state == SERVO_STARTUP_STOP_WAITING_ZERO ||
               s_startup_stop[axis].state == SERVO_STARTUP_STOP_CONFIGURING ||
               s_startup_stop[axis].state == SERVO_STARTUP_STOP_VERIFYING)) ||
             (s_boot_zero[axis].eligible &&
              (s_boot_zero[axis].state == SERVO_BOOT_ZERO_PREPARING ||
               s_boot_zero[axis].state == SERVO_BOOT_ZERO_WRITING ||
               s_boot_zero[axis].state == SERVO_BOOT_ZERO_VERIFYING)) ||
             (s_hold[axis].eligible &&
              (s_hold[axis].state == SERVO_STARTUP_HOLD_PREPARING ||
               s_hold[axis].state == SERVO_STARTUP_HOLD_WRITING_ZERO ||
               s_hold[axis].state == SERVO_STARTUP_HOLD_ENABLING ||
               s_hold[axis].state == SERVO_STARTUP_HOLD_VERIFYING)))) return 1U;
    return 0U;
}

static uint8_t aimtor_pick_startup_feedback(servo_axis_t *axis, uint16_t *tag,
                                           TickType_t now)
{
    uint8_t offset, found = 0U, best = 0U, best_invalid = 0U;
    TickType_t best_age = 0U;
    static const uint16_t tags[3] = {
        AIMTOR_TAG_SPEED, AIMTOR_TAG_FAULT, AIMTOR_TAG_HOLD_ENABLE
    };
    /* A home-write acknowledgement starts a fixed 1 s position deadline.
     * Do not let the other axis' parameter sequence consume that window. */
    for (offset = 0U; offset < SERVO_AXIS_COUNT; ++offset) {
        uint8_t a = (uint8_t)((s_startup_poll_turn + offset) % SERVO_AXIS_COUNT);
        if (s_boot_zero[a].state == SERVO_BOOT_ZERO_VERIFYING &&
            s_axes[a].position_sequence == s_boot_position_sequence[a]) {
            *axis = (servo_axis_t)a; *tag = AIMTOR_TAG_POSITION;
            s_startup_poll_turn = (uint8_t)((a + 1U) % 6U);
            return 1U;
        }
    }
    for (offset = 0U; offset < 6U; ++offset) {
        uint8_t slot = (uint8_t)((s_startup_poll_turn + offset) % 6U);
        uint8_t a = slot % SERVO_AXIS_COUNT, item = slot / SERVO_AXIS_COUNT;
        aimtor_axis_state_t *state = &s_axes[a];
        uint8_t valid = item == 0U ? state->speed_valid : item == 1U ?
            state->fault_valid : state->snapshot.enable_feedback_valid;
        TickType_t age = now - (item == 0U ? state->speed_tick : item == 1U ?
            state->fault_tick : state->snapshot.enable_feedback_tick);
        if (!app_config_servo_axis_selected(a) || state->disable_pending) continue;
        if (item == 2U) {
            if (valid && !aimtor_tick_due(now, s_hold_enable_poll[a])) continue;
        } else if (!aimtor_poll_due(state, tags[item], now) &&
                   (valid && age < pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS - 100U)))
            continue;
        if (!found || (!valid && !best_invalid) ||
            (((!valid) == best_invalid) && age > best_age)) {
            found = 1U; best = slot; best_invalid = (uint8_t)!valid; best_age = age;
        }
    }
    if (!found) return 0U;
    *axis = (servo_axis_t)(best % SERVO_AXIS_COUNT);
    *tag = tags[best / SERVO_AXIS_COUNT];
    s_startup_poll_turn = (uint8_t)((best + 1U) % 6U);
    return 1U;
}

#if APP_LINK_DIAG_ACTIVE
#if defined(__CC_ARM) || defined(__GNUC__)
__attribute__((noinline))
#endif
void aimtor_diag_startup_stop(uint8_t source, uint8_t axis,
                             uint16_t reason, uint32_t detail)
{
    /* All writers hold the task critical section through publication.  A
     * shared scratch avoids adding 96 B to every safety caller's stack. */
    static uint32_t words[24];
    uint8_t a;
    TickType_t now;
    taskENTER_CRITICAL();
    /* Time and feedback stamps must belong to the same protected observation. */
    now = xTaskGetTickCount();
    words[0] = 0x53540118UL;
    words[1] = ((uint32_t)source << 24U) | ((uint32_t)axis << 16U) | reason;
    words[2] = detail; words[3] = aimtor_tick_age_ms(now, 0U);
    for (a = 0U; a < SERVO_AXIS_COUNT; ++a) {
        const aimtor_axis_state_t *state = &s_axes[a];
        const servo_snapshot_t *s = &state->snapshot;
        words[4U+a] = ((uint32_t)s_startup_stop[a].state << 24U) |
            ((uint32_t)s_boot_zero[a].state << 16U) |
            ((uint32_t)s_hold[a].state << 8U) | s_cold[a].state;
        words[6U+a] = ((uint32_t)state->action << 24U) |
            ((uint32_t)state->index << 16U) | ((uint32_t)state->count << 8U) |
            state->reading_back;
        words[10U+a] = state->snapshot.busy && state->index < state->count ?
            ((uint32_t)state->steps[state->index].address << 16U) |
            state->steps[state->index].words[0] : 0U;
        words[12U+a] = ((uint32_t)(uint16_t)s->actual_speed_rpm << 16U) | s->fault_code;
        words[14U+a*3U] = state->speed_valid ? aimtor_tick_age_ms(now, state->speed_tick) : UINT32_MAX;
        words[15U+a*3U] = state->fault_valid ? aimtor_tick_age_ms(now, state->fault_tick) : UINT32_MAX;
        words[16U+a*3U] = s->enable_feedback_valid ? aimtor_tick_age_ms(now, s->enable_feedback_tick) : UINT32_MAX;
        words[20U+a] = (uint32_t)s->parameter_verified | ((uint32_t)s->disable_verified << 1U) |
            ((uint32_t)s->enabled << 2U) | ((uint32_t)s->enable_feedback_valid << 3U) |
            ((uint32_t)s->enable_feedback << 4U) | ((uint32_t)s->stop_latched << 5U) |
            ((uint32_t)s->busy << 6U) | ((uint32_t)state->speed_valid << 7U) |
            ((uint32_t)state->fault_valid << 8U) | ((uint32_t)s->zero_verified << 9U);
    }
    words[8] = s_diag_response_id ? s_diag_response_id : s_pending_id;
    words[9] = ((uint32_t)s_pending_cancelled << 24U) |
        ((uint32_t)s_pending_axis << 16U) | s_pending_tag;
    words[22] = (uint32_t)modbus_business_allowed() | ((uint32_t)modbus_motion_permitted() << 1U) |
        ((uint32_t)aimtor_startup_stop_ready() << 2U) | ((uint32_t)aimtor_boot_zero_ready() << 3U) |
        ((uint32_t)aimtor_startup_hold_ready() << 4U) | ((uint32_t)s_motion_authorized << 5U) |
        ((uint32_t)s_hmi_authorized << 6U) | ((uint32_t)s_can_authorized << 7U) |
        ((uint32_t)s_remote_control_mask << 8U) | ((uint32_t)app_config_motion_hardware_ready() << 16U);
    words[23] = ((uint32_t)s_startup_stop[0].failure << 24U) |
        ((uint32_t)s_startup_stop[1].failure << 16U) |
        ((uint32_t)s_boot_zero[0].failure << 8U) | s_boot_zero[1].failure;
    app_link_diag_startup_first(words, 24U);
    taskEXIT_CRITICAL();
}
#endif

bool aimtor_get_snapshot(servo_axis_t axis, servo_snapshot_t *snapshot)
{
    TickType_t now;

    if ((axis >= SERVO_AXIS_COUNT) || (snapshot == NULL))
    {
        return false;
    }
    now = xTaskGetTickCount();
    taskENTER_CRITICAL();
    *snapshot = s_axes[axis].snapshot;
    snapshot->feedback_failures[0] = s_feedback_failures[axis][0];
    snapshot->feedback_failures[1] = s_feedback_failures[axis][1];
    snapshot->feedback_failures[2] = s_enable_feedback_failures[axis];
    snapshot->feedback_degraded = (uint8_t)(s_feedback_failures[axis][0] ||
        s_feedback_failures[axis][1] || s_enable_feedback_failures[axis]);
    snapshot->query_selected =
        app_config_servo_axis_selected((uint8_t)axis);
    snapshot->speed_age_ms = snapshot->speed_valid ?
        aimtor_tick_age_ms(now, s_axes[axis].speed_tick) : UINT32_MAX;
    snapshot->fault_age_ms = snapshot->fault_valid ?
        aimtor_tick_age_ms(now, s_axes[axis].fault_tick) : UINT32_MAX;
    snapshot->enable_feedback_age_ms = snapshot->enable_feedback_valid ?
        aimtor_tick_age_ms(now, snapshot->enable_feedback_tick) : UINT32_MAX;
    snapshot->position_age_ms = s_axes[axis].snapshot.position_valid ?
        aimtor_tick_age_ms(now, s_axes[axis].position_tick) : UINT32_MAX;
    snapshot->position_sequence = s_axes[axis].position_sequence;
    snapshot->position_sample_tick = s_axes[axis].position_tick;
    snapshot->speed_sequence = s_axes[axis].speed_sequence;
    snapshot->speed_sample_tick = s_axes[axis].speed_tick;
    snapshot->online = aimtor_fresh(&s_axes[axis]);
    if (!snapshot->query_selected)
    {
        /* 未参与查询的轴不能伪造在线、使能、核验或运动状态。 */
        snapshot->online = 0U;
        snapshot->enabled = 0U;
        snapshot->parameter_verified = 0U;
        snapshot->control_mode = SERVO_CONTROL_NONE;
        snapshot->speed_valid = 0U;
        snapshot->fault_valid = 0U;
        snapshot->position_valid = 0U;
        snapshot->position_age_ms = UINT32_MAX;
        snapshot->position_sequence = 0U;
        memset(snapshot->position_samples, 0, sizeof(snapshot->position_samples));
        snapshot->speed_sequence = 0U;
        snapshot->probe_pending = 0U;
        snapshot->speed_age_ms = UINT32_MAX;
        snapshot->fault_age_ms = UINT32_MAX;
    }
    taskEXIT_CRITICAL();
    return true;
}

bool aimtor_get_snapshot_pair(servo_snapshot_t *left, servo_snapshot_t *right)
{
    bool result;
    /* 外层临界区保证左右快照来自同一不可被任务更新的观察窗口。 */
    taskENTER_CRITICAL();
    result = aimtor_get_snapshot(SERVO_AXIS_LEFT, left) &&
             aimtor_get_snapshot(SERVO_AXIS_RIGHT, right);
    taskEXIT_CRITICAL();
    return result;
}

static void aimtor_position_projection(servo_axis_t axis,
    servo_position_monitor_axis_t *out, TickType_t now)
{
    const servo_snapshot_t *in = &s_axes[axis].snapshot;
    memcpy(out->position_samples, in->position_samples, sizeof(out->position_samples));
    out->position_sequence = s_axes[axis].position_sequence;
    out->position_generation = in->position_generation;
    out->speed_age_ms = in->speed_valid ? aimtor_tick_age_ms(now, s_axes[axis].speed_tick) : UINT32_MAX;
    out->position_age_ms = in->position_valid ? aimtor_tick_age_ms(now, s_axes[axis].position_tick) : UINT32_MAX;
    out->actual_speed_rpm = in->actual_speed_rpm;
    out->online = aimtor_fresh(&s_axes[axis]);
    out->fault = in->fault;
    out->parameter_verified = in->parameter_verified;
    out->control_mode = (uint8_t)in->control_mode;
    out->speed_valid = in->speed_valid;
    out->position_valid = in->position_valid;
    out->busy = in->busy;
    if (!app_config_servo_axis_selected((uint8_t)axis))
    {
        out->online = out->parameter_verified = 0U;
        out->control_mode = SERVO_CONTROL_NONE;
        out->speed_valid = out->position_valid = 0U;
        out->position_age_ms = out->speed_age_ms = UINT32_MAX;
        out->position_sequence = 0U;
        memset(out->position_samples, 0, sizeof(out->position_samples));
    }
}

bool aimtor_get_position_monitor_pair(servo_position_monitor_axis_t *left,
                                     servo_position_monitor_axis_t *right)
{
    TickType_t now;
    if (left == NULL || right == NULL) return false;
    taskENTER_CRITICAL();
    now = xTaskGetTickCount();
    aimtor_position_projection(SERVO_AXIS_LEFT, left, now);
    aimtor_position_projection(SERVO_AXIS_RIGHT, right, now);
    taskEXIT_CRITICAL();
    return true;
}

static uint8_t aimtor_k1_hold_active(void)
{
    return s_k1_hold.state == SERVO_K1_HOLD_CONFIRMING ||
        s_k1_hold.state == SERVO_K1_HOLD_READY;
}

static uint8_t aimtor_k1_axis_safe(servo_axis_t axis)
{
    servo_snapshot_t *snapshot = &s_axes[axis].snapshot;
    return aimtor_fresh(&s_axes[axis]) && snapshot->enabled &&
        !snapshot->fault && !snapshot->fault_code && !snapshot->stop_latched &&
        snapshot->parameter_verified &&
        snapshot->control_mode == SERVO_CONTROL_RS485_VELOCITY;
}

bool aimtor_k1_hold_get_status(servo_k1_hold_status_t *status)
{
    uint8_t axis;
    TickType_t now = xTaskGetTickCount();
    if (status == NULL) return false;
    taskENTER_CRITICAL();
    *status = s_k1_hold;
    status->confirmed_mask = status->enabled_mask = status->zero_mask = 0U;
    status->disturbed = 0U;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        uint8_t bit = (uint8_t)(1U << axis);
        if (!(status->axis_mask & bit)) continue;
        if (s_axes[axis].snapshot.enabled) status->enabled_mask |= bit;
        if (aimtor_k1_axis_safe((servo_axis_t)axis))
        {
            if (s_axes[axis].snapshot.actual_speed_rpm == 0)
                status->zero_mask |= bit;
            else status->disturbed = 1U;
            if (s_hold_enable_confirmed[axis] &&
                aimtor_tick_age_ms(now, s_hold_enable_tick[axis]) <
                    APP_SERVO_STATUS_MAX_AGE_MS)
                status->confirmed_mask |= bit;
        }
    }
    status->valid = status->state == SERVO_K1_HOLD_READY &&
        status->axis_mask && status->confirmed_mask == status->axis_mask;
    taskEXIT_CRITICAL();
    return true;
}

servo_result_t aimtor_k1_hold_request(uint8_t axis_mask)
{
    uint8_t axis;
    if (!axis_mask || axis_mask != app_config_servo_control_axis_mask())
        return SERVO_INVALID_ARGUMENT;
    taskENTER_CRITICAL();
    if (aimtor_k1_hold_active())
    {
        taskEXIT_CRITICAL();
        return SERVO_OK;
    }
    if (s_k1_hold.state == SERVO_K1_HOLD_FAILED || s_cli_mask ||
        app_health_watchdog_test_requested() || app_config_servo_readonly_test() ||
        !aimtor_boot_zero_ready() || !aimtor_startup_hold_ready() ||
        !app_config_motion_hardware_ready() || s_motion_authorized)
    {
        taskEXIT_CRITICAL();
        return SERVO_NOT_READY;
    }
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        if ((axis_mask & (1U << axis)) &&
            (!aimtor_k1_axis_safe((servo_axis_t)axis) ||
             s_axes[axis].snapshot.actual_speed_rpm != 0 ||
             s_axes[axis].snapshot.busy || s_axes[axis].disable_pending))
        {
            taskEXIT_CRITICAL();
            return SERVO_NOT_READY;
        }
    memset(&s_k1_hold, 0, sizeof(s_k1_hold));
    s_k1_hold.state = SERVO_K1_HOLD_CONFIRMING;
    s_k1_hold.axis_mask = axis_mask;
    s_k1_enable_floor = s_transaction_id;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        if (axis_mask & (1U << axis))
        {
            /* 清除旧回读证明，确认必须来自本次申请之后的新事务。 */
            s_hold_enable_confirmed[axis] = 0U;
            s_hold_enable_poll[axis] = xTaskGetTickCount();
        }
    taskEXIT_CRITICAL();
    return SERVO_OK;
}

servo_result_t aimtor_k1_hold_release(void)
{
    servo_k1_hold_status_t status;
    uint8_t axis;
    taskENTER_CRITICAL();
    (void)aimtor_k1_hold_get_status(&status);
    if (!status.valid || status.zero_mask != status.axis_mask ||
        app_health_watchdog_test_requested() || !aimtor_boot_zero_ready() ||
        !aimtor_startup_hold_ready() || !app_config_motion_hardware_ready() ||
        app_config_servo_readonly_test() || s_emergency_pending)
    {
        taskEXIT_CRITICAL();
        return SERVO_NOT_READY;
    }
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        if ((status.axis_mask & (1U << axis)) &&
            (s_axes[axis].snapshot.busy || s_axes[axis].disable_pending))
        {
            taskEXIT_CRITICAL();
            return SERVO_BUSY;
        }
    memset(&s_k1_hold, 0, sizeof(s_k1_hold));
    taskEXIT_CRITICAL();
    return SERVO_OK;
}

void aimtor_init(void)
{
    uint8_t axis;

    memset(s_axes, 0, sizeof(s_axes));
#if APP_LINK_DIAG_ACTIVE
    s_diag_response_id = 0U;
#endif
    memset(s_enable_feedback_failures, 0, sizeof(s_enable_feedback_failures));
    s_recovery_request = s_recovery_isolated = s_degraded_active = 0U;
    memset(s_feedback_failures, 0, sizeof(s_feedback_failures));
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    memset(s_link_health_flags,0,sizeof(s_link_health_flags));
#endif
    memset(s_feedback_transaction, 0, sizeof(s_feedback_transaction));
    memset(s_feedback_sequence, 0, sizeof(s_feedback_sequence));
    memset(s_feedback_failure_tick, 0, sizeof(s_feedback_failure_tick));
    memset(s_feedback_result, 0, sizeof(s_feedback_result));
    memset(s_feedback_first_transaction, 0, sizeof(s_feedback_first_transaction));
    memset(s_feedback_first_result, 0, sizeof(s_feedback_first_result));
    memset(&s_recovery, 0, sizeof(s_recovery));
    s_recovery_disqualified = s_recovery_observing = s_recovery_candidate = 0U;
    memset(&s_k1_hold, 0, sizeof(s_k1_hold));
    memset(s_cold, 0, sizeof(s_cold));
    s_cold_phase = SERVO_COLD_CLEAR_SKIPPED;
    s_cold_initialized = s_cold_started = s_cold_online = s_cold_turn = 0U;
    memset(s_cold_read_step, 0, sizeof(s_cold_read_step));
    memset(s_cold_seen, 0, sizeof(s_cold_seen));
    memset(s_cold_post, 0, sizeof(s_cold_post));
    memset(s_startup_stop, 0, sizeof(s_startup_stop));
    memset(s_wdg_recovery, 0, sizeof(s_wdg_recovery));
    aimtor_boot_zero_init(0U);
    aimtor_startup_hold_reset();
    s_boot_zero_internal = 0U;
    s_transaction_id = 0U;
    s_pending_id = 0U;
    s_pending_cancelled = 0U;
    s_maintenance_enabled = 0U;
    s_poll_axis_turn = SERVO_AXIS_LEFT;
    memset(s_poll_tag_turn, 0, sizeof(s_poll_tag_turn));
    s_action_axis_turn = SERVO_AXIS_LEFT;
    s_critical_poll_streak = 0U;
    s_background_critical = s_background_turn = 0U;
    s_startup_feedback_slot = 0U; s_startup_poll_turn = 0U;
    memset(s_poll_stats, 0, sizeof(s_poll_stats));
    memset(s_poll_sample, 0, sizeof(s_poll_sample));
    s_emergency_pending = 0U;
    s_feedback_stop_pending = 0U;
    s_remote_control_mask = 0U;
    s_sync_poll_phase = 0U;
    s_remote_stop_active = 0U;
    s_controlled_safety_active = 0U;
    s_controlled_safety_offline = 0U;
    memset(s_controlled_safety_command, 0, sizeof(s_controlled_safety_command));
    memset(&s_safety_status, 0, sizeof(s_safety_status));
#if (APP_POSITION_MONITOR_ENABLED == 1U)
    s_position_pair_pending = 0U;
#endif
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        s_wdg_recovery[axis].state = SERVO_WDG_RECOVERY_SKIPPED;
    }
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        s_axes[axis].snapshot.query_selected =
            app_config_servo_axis_selected(axis);
        s_axes[axis].snapshot.speed_age_ms = UINT32_MAX;
        s_axes[axis].snapshot.fault_age_ms = UINT32_MAX;
        s_axes[axis].snapshot.position_age_ms = UINT32_MAX;
        s_axes[axis].snapshot.position_sequence = 0U;
        s_axes[axis].snapshot.speed_sequence = 0U;
        s_axes[axis].snapshot.last_poll_result = MODBUS_OK;
        s_axes[axis].snapshot.last_poll_address = 0U;
        s_axes[axis].snapshot.diagnostic_valid_mask = 0U;
        s_axes[axis].snapshot.diagnostic_update_tick = 0U;
        s_axes[axis].snapshot.diagnostic_poll_failures = 0U;
        memset(s_axes[axis].next_diagnostic_poll, 0,
               sizeof(s_axes[axis].next_diagnostic_poll));
        s_axes[axis].next_probe_tick = 0U;
    }
}

/* 静止配置必须两轴均失能；已排队的使能同样视为忙。 */
static uint8_t aimtor_both_idle(void)
{
    uint8_t axis;

    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        /* 单轴调试时，未选中的轴不参与当前配置的忙状态判断。 */
        if (!app_config_servo_axis_selected(axis))
        {
            continue;
        }
        if (s_axes[axis].snapshot.enabled || s_axes[axis].snapshot.busy ||
            s_axes[axis].disable_pending ||
            servo_pulse_get_frequency((servo_axis_t)axis) != 0U)
        {
            return 0U;
        }
    }
    return 1U;
}

/* 会话内仅目标轴必须失能，另一轴仍须新鲜零速且没有活动事务。 */
static uint8_t aimtor_configuration_idle(servo_axis_t target)
{
    uint8_t axis;
    if (!aimtor_cli_axis_owned(target)) return aimtor_both_idle();
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_config_servo_axis_selected(axis)) continue;
        if (s_axes[axis].snapshot.busy || s_axes[axis].disable_pending ||
            servo_pulse_get_frequency((servo_axis_t)axis) ||
            !aimtor_fresh(&s_axes[axis]) ||
            s_axes[axis].snapshot.actual_speed_rpm ||
            s_axes[axis].snapshot.fault ||
            (axis == (uint8_t)target && s_axes[axis].snapshot.enabled))
            return 0U;
    }
    return 1U;
}

static servo_result_t aimtor_can_write(servo_axis_t axis)
{
    if (axis >= SERVO_AXIS_COUNT)
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (!aimtor_cold_clear_ready()) return SERVO_NOT_READY;
    if (s_controlled_safety_active || (aimtor_recovery_blocks_motion() && !s_boot_zero_internal)) return SERVO_NOT_READY;
    if (s_can_mask && !s_can_internal && !s_boot_zero_internal) return SERVO_NOT_READY;
    if (s_cli_mask && !s_boot_zero_internal &&
        !aimtor_cli_axis_owned(axis)) return SERVO_NOT_READY;
    if (!app_config_servo_axis_selected((uint8_t)axis) ||
        app_config_servo_readonly_test() ||
        !aimtor_register_map_valid() || !app_config_servo_bus_ready() ||
        !APP_SERVO_SOFT_ENABLE_CONFIRMED)
    {
        return SERVO_NOT_CONFIGURED;
    }
    if (s_axes[axis].snapshot.stop_latched ||
        !aimtor_fresh(&s_axes[axis]) ||
        s_axes[axis].snapshot.fault)
    {
        return SERVO_NOT_READY;
    }
    return s_axes[axis].snapshot.busy ? SERVO_BUSY : SERVO_OK;
}


static void aimtor_begin(servo_axis_t axis, aimtor_action_t action)
{
    s_can_action[axis] = 0U;
    if (action == ACTION_ZERO || action == ACTION_BOOT_ZERO)
    {
        /* 设零可能在应答丢失时已经执行，开始流程就隔断旧位置轨迹。 */
        s_axes[axis].snapshot.position_generation++;
        s_axes[axis].snapshot.position_valid = 0U;
        memset(s_axes[axis].snapshot.position_samples, 0,
               sizeof(s_axes[axis].snapshot.position_samples));
    }
    if (action == ACTION_CONFIGURE || action == ACTION_VERIFY || action == ACTION_WDG_RECOVERY)
    {
        s_axes[axis].snapshot.ramp_read_mask = 0U;
        s_axes[axis].snapshot.ramp_match_mask = 0U;
        s_axes[axis].snapshot.ramp_requested_ms[0] = APP_SERVO_SPEED_ACCELERATION_RAMP_MS;
        s_axes[axis].snapshot.ramp_requested_ms[1] = APP_SERVO_SPEED_DECELERATION_RAMP_MS;
    }
    s_axes[axis].action = action;
    s_axes[axis].count = 0U;
    s_axes[axis].index = 0U;
    s_axes[axis].reading_back = 0U;
    s_axes[axis].snapshot.busy = 1U;
    s_axes[axis].snapshot.last_result = SERVO_BUSY;
}

static void aimtor_add(servo_axis_t axis, uint16_t address, uint32_t value,
                       uint8_t count, uint8_t write, uint8_t compare)
{
    aimtor_step_t *step;

    configASSERT(s_axes[axis].count < AIMTOR_SEQUENCE_CAPACITY);
    step = &s_axes[axis].steps[s_axes[axis].count++];
    memset(step, 0, sizeof(*step));
    step->address = address;
    step->words[0] = (uint16_t)value;
    step->words[1] = (uint16_t)(value >> 16U);
    step->count = count;
    step->write = write;
    step->compare = compare;
}

/* 先核验低字在前，再访问双字对象；不跨保留寄存器读取大状态块。 */
static void aimtor_configuration(servo_axis_t axis,
                                 servo_control_mode_t mode, uint8_t write)
{
    uint8_t compare;
    /* CLI/maintenance submits direct RPM, so keep its original driver ramp. */
    uint32_t acceleration_ms = ((s_cli_mask && !s_cli_restore) ||
        (!s_cli_gate_enabled && s_maintenance_enabled)) ?
        APP_SERVO_DIRECT_ACCELERATION_RAMP_MS : APP_SERVO_SPEED_ACCELERATION_RAMP_MS;
    s_axes[axis].snapshot.ramp_requested_ms[0] = (uint16_t)acceleration_ms;

    /*
     * 普通运行配置按 F103 成功工程处理：写命令只检查 Modbus 回显。
     * 只有显式参数核验命令才读取并比较寄存器，避免把驱动器内部的
     * 参数规范化或延迟生效误判为运动故障。
     */
    /* H06_05/H06_06 below always read back; other parameters retain the proven policy. */
    compare = write ? 0U : 1U;

    /*
     * 字序和脉冲格式是驱动器基础参数。
     * 普通模式切换不能插入这两个只读请求：成功工程的控制脚本不访问
     * 这两个地址，部分驱动器也可能不开放对应参数的读取。若这里读取
     * 超时，原实现会把一次无关读取失败升级为急停，随后报出“模式准备
     * 失败/使能失败”，甚至留下驱动器故障。
     *
     * 显式执行 servo verify 时，write 为0，才读取并比较基础参数。
     */
    if (!write)
    {
        aimtor_add(axis, AIMTOR_REG_WORD_ORDER, 1U, 1U, 0U, 1U);
        aimtor_add(axis, AIMTOR_REG_PULSE_FORMAT,
                   APP_AIMTOR_PULSE_FORMAT_VALUE, 1U, 0U, 1U);
    }
    /*
     * DI1功能和OFF停机方式已经在驱动器中确认，不在每次模式切换时
     * 重写。F103工程也只在初始化阶段配置一次，运行脚本不重复修改。
     */
    aimtor_add(axis, AIMTOR_REG_SERVO_ENABLE, 0U, 1U, write, compare);
    aimtor_add(axis, AIMTOR_REG_CONTROL_MODE,
        mode == SERVO_CONTROL_RS485_VELOCITY ? 0U : 1U, 1U, write, compare);
    if (mode == SERVO_CONTROL_RS485_VELOCITY)
    {
        /* 清除驱动器残留给定，再允许使能，防止切模式后突然运行。 */
        aimtor_add(axis, AIMTOR_REG_SPEED_SELECTION, 0U, 1U, write, compare);
        aimtor_add(axis, AIMTOR_REG_SPEED_SOURCE, 0U, 1U, write, compare);
        aimtor_add(axis, AIMTOR_REG_SPEED_COMMAND, 0U, 1U, write, compare);
        aimtor_add(axis, AIMTOR_REG_ACCELERATION_TIME,
                   acceleration_ms, 1U, write, 1U);
        aimtor_add(axis, AIMTOR_REG_DECELERATION_TIME,
                   APP_SERVO_SPEED_DECELERATION_RAMP_MS, 1U, write, 1U);
    }
    else
    {
        aimtor_add(axis, AIMTOR_REG_POSITION_SOURCE,
            mode == SERVO_CONTROL_PULSE_POSITION ? 0U : 2U, 1U, write, compare);
        if (mode == SERVO_CONTROL_PULSE_POSITION)
        {
            /*
             * 从内部多段位置切换到外部脉冲前，必须清除上一轮
             * H03_05 的触发电平。否则重新使能时驱动器可能再次执行
             * 上一条残留的位置段，表现为未发送脉冲却自行转动。
             */
            aimtor_add(axis, AIMTOR_REG_SEGMENT_TRIGGER,
                       0U, 1U, write, compare);
        }
        else if (mode == SERVO_CONTROL_RS485_SEGMENT_POSITION)
        {
            /*
             * 进入内部多段位置模式时先清除上一次的触发状态。
             * 驱动器要求再次运行必须产生 H03_05 的 0 -> 1 上升沿；
             * 若保留上次的 1 状态，后续仅写入伺服使能可能执行残留位置段。
             */
            aimtor_add(axis, AIMTOR_REG_SEGMENT_TRIGGER, 0U, 1U, write, compare);
            aimtor_add(axis, AIMTOR_REG_DI2_FUNCTION, 28U, 1U, write, compare);
            aimtor_add(axis, AIMTOR_REG_SEGMENT_CONTROL, 0U, 1U, write, compare);
            aimtor_add(axis, AIMTOR_REG_SEGMENT_MODE, 1U, 1U, write, compare);
            aimtor_add(axis, AIMTOR_REG_SEGMENT_SELECT, 0U, 1U, write, compare);
            /* 实际位置命令提交时仍需再次写 0 -> 1 产生启动沿。 */
        }
    }
}

servo_result_t aimtor_set_control_mode(servo_axis_t axis,
                                       servo_control_mode_t mode)
{
    servo_result_t result;

#if !APP_SERVO_PULSE_ENABLED
    if (mode == SERVO_CONTROL_PULSE_POSITION) return SERVO_UNSUPPORTED;
#endif

    if (!APP_UNPROTECTED_MOTION_TEST_ENABLED && mode != SERVO_CONTROL_RS485_VELOCITY)
        return SERVO_NOT_CONFIGURED;
    if (app_health_watchdog_test_requested() ||
        !app_config_motion_ramp_valid()) return SERVO_NOT_READY;
    if (!s_boot_zero_internal && !s_remote_prepare_internal &&
        !aimtor_motion_allowed())
        return SERVO_NOT_READY;

    if ((mode < SERVO_CONTROL_PULSE_POSITION) ||
        (mode > SERVO_CONTROL_RS485_SEGMENT_POSITION))
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (!app_config_servo_axis_controllable((uint8_t)axis))
    {
        return SERVO_NOT_CONFIGURED;
    }
    taskENTER_CRITICAL();
    result = aimtor_can_write(axis);
    if ((result == SERVO_OK) && !aimtor_configuration_idle(axis))
    {
        result = SERVO_BUSY;
    }
    if ((result == SERVO_OK) &&
        (s_axes[axis].snapshot.actual_speed_rpm != 0))
    {
        result = SERVO_NOT_READY;
    }
    if (result == SERVO_OK)
    {
        s_axes[axis].snapshot.parameter_verified = 0U;
        s_axes[axis].snapshot.disable_verified = 0U;
        s_axes[axis].requested_mode = mode;
        /*
         * 从提交模式配置开始，状态显示目标模式而不是沿用上一次模式。
         * 配置事务完成后由 aimtor_finish() 将其标记为已核验；
         * 配置失败时 aimtor_fail() 会清回 NONE。
         */
        s_axes[axis].snapshot.control_mode = mode;
        s_axes[axis].segment_latched = 0U;
        s_axes[axis].position_running = 0U;
        aimtor_begin(axis, ACTION_CONFIGURE);
        aimtor_configuration(axis, mode, 1U);
    }
    taskEXIT_CRITICAL();
    return result;
}

servo_result_t aimtor_request_parameter_verify(servo_axis_t axis)
{
    servo_result_t result;

    if (app_health_watchdog_test_requested() ||
        !app_config_motion_ramp_valid()) return SERVO_NOT_READY;
    if (!s_boot_zero_internal && !aimtor_motion_allowed())
        return SERVO_NOT_READY;

    if (axis >= SERVO_AXIS_COUNT)
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (s_cli_mask && !aimtor_cli_axis_owned(axis)) return SERVO_NOT_READY;
    if (!app_config_servo_axis_selected((uint8_t)axis) ||
        !aimtor_register_map_valid() || !app_config_servo_bus_ready())
    {
        return SERVO_NOT_CONFIGURED;
    }
    taskENTER_CRITICAL();
    result = aimtor_configuration_idle(axis) ? SERVO_OK : SERVO_BUSY;
#if !APP_SERVO_PULSE_ENABLED
    if (s_axes[axis].snapshot.control_mode == SERVO_CONTROL_PULSE_POSITION)
        result = SERVO_UNSUPPORTED;
#endif
    if (result == SERVO_OK)
    {
        s_axes[axis].snapshot.parameter_verified = 0U;
        s_axes[axis].snapshot.disable_verified = 0U;
        /*
         * Verify the current mode. RS485-only builds never fall back to pulse.
         */
        s_axes[axis].requested_mode =
            s_axes[axis].snapshot.control_mode != SERVO_CONTROL_NONE ?
            s_axes[axis].snapshot.control_mode :
            (APP_SERVO_PULSE_ENABLED ? SERVO_CONTROL_PULSE_POSITION : SERVO_CONTROL_RS485_VELOCITY);
        aimtor_begin(axis, ACTION_VERIFY);
        aimtor_configuration(axis, s_axes[axis].requested_mode, 0U);
    }
    taskEXIT_CRITICAL();
    return result;
}

servo_result_t aimtor_request_status(servo_axis_t axis)
{
    TickType_t now;

    if (axis >= SERVO_AXIS_COUNT)
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (!app_config_servo_axis_selected((uint8_t)axis))
    {
        return SERVO_NOT_CONFIGURED;
    }
    /* 手动探测必须从速度查询重新开始，避免沿用旧的有效状态。 */
    taskENTER_CRITICAL();
    now = xTaskGetTickCount();
    s_axes[axis].speed_valid = 0U;
    s_axes[axis].fault_valid = 0U;
    s_axes[axis].snapshot.speed_valid = 0U;
    s_axes[axis].snapshot.fault_valid = 0U;
    s_axes[axis].probe_pending = 1U;
    s_axes[axis].snapshot.probe_pending = 1U;
    s_axes[axis].next_probe_tick = now;
    s_axes[axis].next_speed_poll = now;
    s_axes[axis].next_fault_poll = now;
    taskEXIT_CRITICAL();
    return SERVO_OK;
}

servo_result_t aimtor_request_position(servo_axis_t axis)
{
    TickType_t now;

    if (axis >= SERVO_AXIS_COUNT)
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (!app_config_servo_axis_selected((uint8_t)axis))
    {
        return SERVO_NOT_CONFIGURED;
    }
    taskENTER_CRITICAL();
    now = xTaskGetTickCount();
    /*
     * 立即安排位置查询。位置查询完成后由正常轮询逻辑恢复后台周期，
     * 定脉冲完成确认下一轮会再次按需请求。
     */
    s_axes[axis].next_position_poll = now;
    s_axes[axis].position_priority = 1U;
    taskEXIT_CRITICAL();
    return SERVO_OK;
}

servo_result_t aimtor_enable(servo_axis_t axis)
{
    servo_result_t result;
    servo_snapshot_t *snapshot;

    if (app_sync_params_maintenance()) return SERVO_NOT_READY;
    if (app_health_watchdog_test_requested() || aimtor_degraded()) return SERVO_NOT_READY;
    if (!modbus_motion_permitted()) return SERVO_NOT_READY;
    if (!s_remote_prepare_internal && !aimtor_motion_allowed())
        return SERVO_NOT_READY;

    if (!app_config_servo_axis_controllable((uint8_t)axis))
    {
        return SERVO_NOT_CONFIGURED;
    }
    taskENTER_CRITICAL();
    result = aimtor_can_write(axis);
    if (result == SERVO_OK)
    {
        snapshot = &s_axes[axis].snapshot;
        if (!snapshot->parameter_verified ||
            !app_config_motion_hardware_ready() ||
            (snapshot->control_mode == SERVO_CONTROL_NONE))
        {
            result = SERVO_NOT_READY;
        }
        else
        {
            snapshot->disable_verified = 0U;
            aimtor_begin(axis, ACTION_ENABLE);
            /*
             * 使能以06H写入应答作为完成依据。
             * F103成功工程同样不对H03_03追加读回：部分驱动器在
             * 使能切换瞬间读回值仍为0，会把实际成功的使能误判为失败，
             * 进而清除verify并触发无谓的故障锁存。
             */
            aimtor_add(axis, AIMTOR_REG_SERVO_ENABLE, 1U, 1U, 1U, 0U);
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

void aimtor_request_controlled_safety_stop(uint8_t offline)
{
    uint8_t axis;
    int32_t maximum = 0;
    int32_t speed;
    TickType_t now;
    taskENTER_CRITICAL();
    if (app_config_servo_readonly_test() || !app_config_servo_control_axis_count())
    {
        taskEXIT_CRITICAL();
        return;
    }
    if (s_controlled_safety_active == 0U &&
        motion_get_state() != MOTION_FAULT && motion_get_state() != MOTION_OFFLINE)
    {
        now = xTaskGetTickCount();
        memset(&s_safety_status, 0, sizeof(s_safety_status));
        s_recovery.generation++;
        s_recovery.eligible = 0U;
        s_recovery.state = SERVO_RECOVERY_NONE;
        s_recovery.failure = 0U;
        s_recovery.first_axis = s_recovery.first_item = 255U;
        s_recovery.first_failures = 0U;
        s_recovery.first_transaction = s_recovery.first_result = s_recovery.first_age_ms = 0U;
        s_recovery.first_failure_ms = 0U; s_recovery.first_address = 0U;
        s_recovery_observing = 0U;
        memset(s_recovery_samples, 0, sizeof(s_recovery_samples));
        for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
        {
            uint8_t item;
            if (!app_config_servo_axis_selected(axis)) continue;
            for (item = 0U; item < 3U; ++item)
            {
                uint8_t failures = item == 2U ? s_enable_feedback_failures[axis] : s_feedback_failures[axis][item];
                if (failures && (s_recovery.first_axis == 255U ||
                    (int32_t)(s_feedback_failure_tick[axis][item] -
                    s_feedback_failure_tick[s_recovery.first_axis][s_recovery.first_item]) < 0))
                {
                    s_recovery.first_axis = axis; s_recovery.first_item = item;
                    s_recovery.first_failures = failures;
                    s_recovery.first_transaction = s_feedback_first_transaction[axis][item];
                    s_recovery.first_result = s_feedback_first_result[axis][item];
                    s_recovery.first_failure_ms = aimtor_tick_age_ms(s_feedback_failure_tick[axis][item], 0U);
                    s_recovery.first_age_ms = item == 0U ? aimtor_tick_age_ms(now, s_axes[axis].speed_tick) :
                        item == 1U ? aimtor_tick_age_ms(now, s_axes[axis].fault_tick) :
                        aimtor_tick_age_ms(now, s_hold_enable_tick[axis]);
                }
            }
        }
        if (!s_recovery_candidate)
            for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
                if (app_config_servo_axis_selected(axis) &&
                    aimtor_axis_health((servo_axis_t)axis, NULL) == SERVO_HEALTH_STOP)
                    s_recovery_candidate = 1U;
        s_recovery.eligible = s_recovery_candidate && !s_recovery_disqualified;
        if (s_recovery.first_axis == 255U && s_recovery_candidate)
            for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
            {
                servo_axis_health_t h;
                uint8_t item;
                if (!app_config_servo_axis_selected(axis)) continue;
                (void)aimtor_axis_health((servo_axis_t)axis, &h);
                for (item = 0U; item < 3U; ++item)
                    if (s_recovery.first_axis == 255U && h.item[item].level == SERVO_HEALTH_STOP)
                    {
                        s_recovery.first_axis = axis; s_recovery.first_item = item;
                        s_recovery.first_transaction = h.item[item].transaction;
                        s_recovery.first_result = h.item[item].result;
                        s_recovery.first_age_ms = h.item[item].age_ms;
                        s_recovery.first_failure_ms = aimtor_tick_age_ms(now, 0U);
                    }
            }
        if (s_recovery.first_item < 3U)
            s_recovery.first_address = s_recovery.first_item == 0U ? AIMTOR_REG_ACTUAL_SPEED :
                s_recovery.first_item == 1U ? AIMTOR_REG_FAULT_CODE : AIMTOR_REG_SERVO_ENABLE;
        s_recovery_candidate = 0U;
        memset(s_safety_zero_samples, 0, sizeof(s_safety_zero_samples));
        memset(s_safety_zero_written, 0, sizeof(s_safety_zero_written));
        s_safety_status.source = s_can_mask ? 3U : s_hmi_mask ? 2U :
            s_cli_mask ? 4U : s_remote_control_mask ? 1U : 0U;
        s_controlled_safety_active = 1U;
        s_controlled_safety_offline = offline ? 1U : 0U;
        /* A controlled stop is a transition from valid runtime feedback.  It
         * must not leave the startup latch from the pre-stop cache in place;
         * the new stop service owns safety state until fresh proofs arrive. */
        s_safety_started = now;
        s_safety_status.started_ms = aimtor_tick_age_ms(now, 0U);
        s_safety_status.active = 1U;
        s_safety_status.initial_reason = MOTION_FAULT_REASON_FEEDBACK_STALE;
        s_safety_status.final_reason = MOTION_FAULT_REASON_FEEDBACK_STALE;
        s_safety_turn = s_safety_phase = 0U;
        for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; ++axis)
        {
            if (!app_config_servo_axis_selected(axis)) continue;
            if (!aimtor_fresh(&s_axes[axis]))
                s_safety_status.axis_mask |= (uint8_t)(1U << axis);
            if (s_axes[axis].snapshot.consecutive_failures >=
                (aimtor_runtime_active(&s_axes[axis]) ? APP_SERVO_RUNTIME_COMM_FAILURES : APP_SERVO_OFFLINE_FAILURES))
            {
                s_safety_status.initial_reason = MOTION_FAULT_REASON_SERVO_OFFLINE;
                s_safety_status.final_reason = MOTION_FAULT_REASON_SERVO_OFFLINE;
            }
            s_safety_status.actual_rpm[axis] = s_axes[axis].snapshot.actual_speed_rpm;
            s_safety_status.command_rpm[axis] = s_last_velocity[axis];
            s_safety_status.speed_age_ms[axis] = s_axes[axis].speed_valid ?
                aimtor_tick_age_ms(now, s_axes[axis].speed_tick) : UINT32_MAX;
            s_safety_status.fault_age_ms[axis] = s_axes[axis].fault_valid ?
                aimtor_tick_age_ms(now, s_axes[axis].fault_tick) : UINT32_MAX;
            s_safety_status.failures[axis] = s_axes[axis].snapshot.consecutive_failures;
            s_safety_status.last_modbus_result[axis] = s_axes[axis].snapshot.last_poll_result;
            s_safety_status.last_address[axis] = s_axes[axis].snapshot.last_poll_address;
            s_safety_status.mode[axis] = (uint8_t)s_axes[axis].snapshot.control_mode;
            s_safety_status.fault_code[axis] = s_axes[axis].snapshot.fault_code;
            s_controlled_safety_command[axis] = s_last_velocity[axis];
            if (s_controlled_safety_command[axis] == 0 &&
                s_axes[axis].speed_write_sequence == 0U &&
                s_axes[axis].snapshot.actual_speed_rpm != 0)
                s_controlled_safety_command[axis] =
                    s_axes[axis].snapshot.actual_speed_rpm;
            s_safety_initial[axis] = s_controlled_safety_command[axis];
            if (s_axes[axis].snapshot.control_mode == SERVO_CONTROL_PULSE_POSITION)
                s_safety_initial[axis] = (int16_t)(motion_get_command_velocity(axis) *
                    60.0f * APP_GEAR_REDUCTION_RATIO / APP_WHEEL_CIRCUMFERENCE_M);
            speed = s_safety_initial[axis];
            if (speed < 0) speed = -speed;
            if (speed > maximum) maximum = speed;
            s_safety_speed_sequence[axis] = s_axes[axis].speed_sequence;
            s_axes[axis].snapshot.busy = 0U;
            s_axes[axis].action = ACTION_NONE;
            s_axes[axis].disable_pending = 0U;
            s_can_action[axis] = 0U;
        }
        s_safety_status.timeout_ms = (uint32_t)((float)maximum *
            APP_WHEEL_CIRCUMFERENCE_M * 1000.0f /
            (60.0f * APP_GEAR_REDUCTION_RATIO * APP_SERVO_SAFETY_STOP_DECEL_MPS2)) + 5000U;
        if (s_safety_status.timeout_ms < 25000U &&
            (s_axes[0].snapshot.control_mode == SERVO_CONTROL_PULSE_POSITION ||
             s_axes[1].snapshot.control_mode == SERVO_CONTROL_PULSE_POSITION))
            s_safety_status.timeout_ms = 25000U;
        s_motion_authorized = s_hmi_authorized = s_can_authorized = s_cli_authorized = 0U;
        ++s_can_generation;
        memset(&s_can_pending, 0, sizeof(s_can_pending));
        modbus_cancel_pending();
        s_pending_cancelled = 1U;
        s_pending_id = 0U;
        motion_request_controlled_safety_stop(
            (motion_fault_reason_t)s_safety_status.initial_reason, offline);
    }
    taskEXIT_CRITICAL();
}

bool aimtor_get_safety_stop_status(servo_safety_stop_status_t *status)
{
    if (status == NULL) return false;
    taskENTER_CRITICAL();
    *status = s_safety_status;
    taskEXIT_CRITICAL();
    return true;
}

bool aimtor_safety_stop_active(void)
{
    return s_controlled_safety_active != 0U;
}

void aimtor_record_immediate_safety_stop(uint8_t reason)
{
    uint8_t axis;
    TickType_t now = xTaskGetTickCount();
    taskENTER_CRITICAL();
    if (s_safety_status.initial_reason == MOTION_FAULT_REASON_NONE)
    {
        s_safety_status.started_ms = aimtor_tick_age_ms(now, 0U);
        s_safety_status.source = s_can_mask ? 3U : s_hmi_mask ? 2U :
            s_cli_mask ? 4U : s_remote_control_mask ? 1U : 0U;
        s_safety_status.initial_reason = reason;
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!app_config_servo_axis_selected(axis)) continue;
            s_safety_status.actual_rpm[axis] = s_axes[axis].snapshot.actual_speed_rpm;
            s_safety_status.command_rpm[axis] = s_last_velocity[axis];
            s_safety_status.speed_age_ms[axis] = s_axes[axis].speed_valid ?
                aimtor_tick_age_ms(now, s_axes[axis].speed_tick) : UINT32_MAX;
            s_safety_status.fault_age_ms[axis] = s_axes[axis].fault_valid ?
                aimtor_tick_age_ms(now, s_axes[axis].fault_tick) : UINT32_MAX;
            s_safety_status.last_modbus_result[axis] = s_axes[axis].snapshot.last_poll_result;
            s_safety_status.last_address[axis] = s_axes[axis].snapshot.last_poll_address;
            s_safety_status.failures[axis] = s_axes[axis].snapshot.consecutive_failures;
            s_safety_status.mode[axis] = (uint8_t)s_axes[axis].snapshot.control_mode;
            s_safety_status.fault_code[axis] = s_axes[axis].snapshot.fault_code;
            if (s_axes[axis].snapshot.fault) s_safety_status.axis_mask |= (uint8_t)(1U << axis);
        }
    }
    s_safety_status.final_reason = reason;
    s_recovery.eligible = 0U;
    s_recovery_disqualified = 1U;
    if (s_recovery.state != SERVO_RECOVERY_NONE && s_recovery.state != SERVO_RECOVERY_DONE)
        s_recovery.state = SERVO_RECOVERY_FAILED;
    s_safety_status.completed_ms = aimtor_tick_age_ms(now, 0U);
    taskEXIT_CRITICAL();
}

static void aimtor_safety_escalate(motion_fault_reason_t reason)
{
    s_safety_status.escalated = 1U;
    s_safety_status.final_reason = (uint8_t)reason;
    motion_request_immediate_safety_stop(reason, s_controlled_safety_offline);
}

void aimtor_controlled_stop_service(void)
{
    aimtor_step_t step;
    uint8_t axis;
    uint8_t all_zero = 1U;
    int32_t magnitude;
    int32_t reduction;
    TickType_t now = xTaskGetTickCount();
    if (s_controlled_safety_active == 0U) return;
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; ++axis)
    {
        if (!app_config_servo_axis_selected(axis)) continue;
        if (s_axes[axis].snapshot.fault || s_axes[axis].snapshot.fault_code)
        {
            aimtor_safety_escalate(MOTION_FAULT_REASON_SERVO_FAULT);
            return;
        }
        if (s_safety_status.write_failures[axis] >= APP_SERVO_OFFLINE_FAILURES)
        {
            aimtor_safety_escalate(MOTION_FAULT_REASON_OUTPUT_FAILURE);
            return;
        }
        if (s_axes[axis].snapshot.control_mode != SERVO_CONTROL_RS485_VELOCITY &&
            s_axes[axis].snapshot.control_mode != SERVO_CONTROL_PULSE_POSITION)
        {
            aimtor_safety_escalate(MOTION_FAULT_REASON_UNSUPPORTED_MODE);
            return;
        }
        if (!aimtor_fresh(&s_axes[axis]) || s_safety_zero_samples[axis] < 2U)
            all_zero = 0U;
    }
    if (all_zero)
    {
        s_safety_status.completed = 1U;
        motion_complete_controlled_safety_stop(s_controlled_safety_offline);
        return;
    }
    if (aimtor_tick_age_ms(now, s_safety_started) >= s_safety_status.timeout_ms)
    {
        aimtor_safety_escalate(MOTION_FAULT_REASON_STOP_TIMEOUT);
        return;
    }
    if (s_pending_id) return;
    modbus_resume_transport();
    while (!app_config_servo_axis_selected(s_safety_turn))
        s_safety_turn = (uint8_t)((s_safety_turn + 1U) % SERVO_AXIS_COUNT);
    axis = s_safety_turn;
    memset(&step, 0, sizeof(step));
    step.count = 1U;
    if (s_safety_phase == 0U &&
        s_axes[axis].snapshot.control_mode == SERVO_CONTROL_RS485_VELOCITY)
    {
        reduction = (int32_t)(APP_SERVO_SAFETY_STOP_DECEL_MPS2 *
            60.0f * APP_GEAR_REDUCTION_RATIO / APP_WHEEL_CIRCUMFERENCE_M *
            (float)aimtor_tick_age_ms(now, s_safety_started) / 1000.0f);
        magnitude = s_safety_initial[axis];
        if (magnitude < 0) magnitude = -magnitude;
        magnitude = magnitude > reduction ? magnitude - reduction : 0;
        s_safety_pending_rpm = (int16_t)(s_safety_initial[axis] < 0 ? -magnitude : magnitude);
        step.address = AIMTOR_REG_SPEED_COMMAND;
        step.words[0] = (uint16_t)s_safety_pending_rpm;
        if (aimtor_submit_step((servo_axis_t)axis, AIMTOR_TAG_SAFETY_WRITE, &step, 0U) != MODBUS_OK)
            s_safety_status.write_failures[axis]++;
    }
    else
    {
        uint16_t tag = s_safety_phase == 2U ? AIMTOR_TAG_FAULT : AIMTOR_TAG_SPEED;
        step.address = tag == AIMTOR_TAG_SPEED ? AIMTOR_REG_ACTUAL_SPEED : AIMTOR_REG_FAULT_CODE;
        (void)aimtor_submit_step((servo_axis_t)axis, tag, &step, 1U);
    }
    s_safety_turn = (uint8_t)((axis + 1U) % SERVO_AXIS_COUNT);
    if (s_safety_turn == 0U || !app_config_servo_axis_selected(s_safety_turn))
    {
        s_safety_turn = 0U;
        s_safety_phase = (uint8_t)((s_safety_phase + 1U) % 3U);
    }
}

void aimtor_emergency_stop(void)
{
    uint8_t axis;

    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_UNKNOWN, 0xFFU, 0U, 0U);
    taskENTER_CRITICAL();
    s_feedback_stop_pending = 0U;
    if (!s_controlled_safety_active && aimtor_recovery_blocks_motion())
    {
        s_recovery.state = SERVO_RECOVERY_FAILED;
        s_recovery.eligible = 0U;
        if (!s_recovery.failure) s_recovery.failure = 5U;
    }
    if (s_controlled_safety_active)
    {
        s_safety_status.active = 0U;
        s_safety_status.completed_ms = aimtor_tick_age_ms(xTaskGetTickCount(), 0U);
        if (!s_safety_status.completed) s_safety_status.escalated = 1U;
    }
    s_controlled_safety_active = 0U;
    if (s_safety_status.completed && !s_safety_status.escalated && s_recovery.eligible &&
        s_recovery.state == SERVO_RECOVERY_NONE)
    {
        s_recovery.state = SERVO_RECOVERY_WAIT_HEALTH;
        s_recovery_observing = 0U;
        memset(s_recovery_samples, 0, sizeof(s_recovery_samples));
        memcpy(s_recovery_floor, s_feedback_sequence, sizeof(s_recovery_floor));
    }
    if (s_recovery.state == SERVO_RECOVERY_FAILED) s_recovery.eligible = 0U;
    if (s_cli_mask) aimtor_cli_request_abort(4U);
    s_cli_authorized = s_cli_internal = 0U;
    s_hold_k1_rearm = 0U;
    s_motion_authorized = 0U;
    s_hmi_authorized = 0U; s_hmi_mask = 0U;
    s_can_authorized = 0U;
    s_can_mask = 0U; ++s_can_generation;
    s_remote_stop_active = 0U;
    if (aimtor_k1_hold_active())
    {
        s_k1_hold.state = SERVO_K1_HOLD_FAILED;
        if (!s_k1_hold.failure) s_k1_hold.failure = 4U;
    }
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (s_hold[axis].eligible)
        {
            s_hold[axis].state = SERVO_STARTUP_HOLD_FAILED;
            if (!s_hold[axis].failure) s_hold[axis].failure = 5U;
            s_hold[axis].verified = 0U;
        }
    }
    /* 先切本地输出并清除规划目标；总线停机仅为尽力发送。 */
    (void)motion_stop(MOTION_STOP_IMMEDIATE);
    motion_set_system_ready(0U);
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (app_config_servo_axis_selected(axis))
        {
            (void)servo_pulse_set_enable((servo_axis_t)axis, false);
        }
    }
    modbus_cancel_pending();
    s_pending_cancelled = 1U;
    s_pending_id = 0U;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_config_servo_axis_selected(axis))
        {
            continue;
        }
        s_axes[axis].snapshot.enabled = 0U;
        s_axes[axis].snapshot.disable_verified = 0U;
        s_axes[axis].snapshot.parameter_verified = 0U;
        s_axes[axis].snapshot.stop_latched = 1U;
        s_axes[axis].snapshot.busy = 0U;
        s_axes[axis].snapshot.last_result = SERVO_NOT_READY;
        s_axes[axis].action = ACTION_NONE;
        s_axes[axis].disable_pending = 1U;
        s_axes[axis].segment_latched = 0U;
        s_axes[axis].position_running = 0U;
        if (s_wdg_recovery[axis].eligible &&
            s_wdg_recovery[axis].state != SERVO_WDG_RECOVERY_DONE &&
            s_wdg_recovery[axis].state != SERVO_WDG_RECOVERY_FAILED)
        {
            aimtor_watchdog_recovery_fail((servo_axis_t)axis, 8U);
        }
    }
    taskEXIT_CRITICAL();
}

void aimtor_emergency_stop_k1(void)
{
    uint8_t allowed;
    taskENTER_CRITICAL();
    /* 已有真实待机失败不能被一次物理K5事件转换成可恢复停止。 */
    allowed = aimtor_startup_hold_ready() || s_hold_k1_rearm;
    if (s_cli_mask && s_cli_restore)
    {
        uint8_t axis;
        allowed = 1U;
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
            if (s_hold[axis].eligible &&
                s_hold[axis].state == SERVO_STARTUP_HOLD_FAILED)
                allowed = 0U;
    }
    aimtor_emergency_stop();
    s_hold_k1_rearm = allowed;
    taskEXIT_CRITICAL();
}

servo_result_t aimtor_startup_hold_request_rearm(void)
{
    servo_result_t result;
    uint8_t axis;
    taskENTER_CRITICAL();
    if (!s_hold_k1_rearm || !aimtor_boot_zero_ready() ||
        !aimtor_watchdog_recovery_ready() || app_config_servo_readonly_test())
    {
        taskEXIT_CRITICAL();
        return SERVO_NOT_READY;
    }
    /* 等待失能应答、新鲜零速及无故障；不通过软件快照假装停稳。 */
    result = aimtor_clear_stop_latch();
    if (result == SERVO_OK)
    {
        s_hold_deadline = 0U;
        s_hold_k1_rearm = 0U;
        s_motion_authorized = 0U;
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!s_hold[axis].eligible) continue;
            memset(&s_hold[axis], 0, sizeof(s_hold[axis]));
            s_hold[axis].eligible = 1U;
            s_hold[axis].state = SERVO_STARTUP_HOLD_WAITING;
            s_hold_enable_confirmed[axis] = 0U;
            s_hold_enable_poll[axis] = 0U;
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

void aimtor_set_remote_control_mask(uint8_t mask)
{
    taskENTER_CRITICAL();
    if (mask != s_remote_control_mask) s_sync_poll_phase = 0U;
    s_remote_control_mask = (uint8_t)(mask &
        app_config_servo_control_axis_mask());
    taskEXIT_CRITICAL();
}

void aimtor_set_remote_control_active(bool active)
{
    aimtor_set_remote_control_mask(active ?
        app_config_servo_control_axis_mask() : 0U);
}

uint8_t aimtor_get_remote_control_mask(void)
{
    return s_remote_control_mask;
}

bool aimtor_remote_control_active(void)
{
    return s_remote_control_mask != 0U;
}

void aimtor_set_hmi_control(bool authorized, uint8_t mask)
{
    taskENTER_CRITICAL();
    s_hmi_mask = (uint8_t)(mask & app_config_servo_control_axis_mask());
    s_hmi_authorized = authorized && aimtor_cold_clear_ready() && !aimtor_recovery_blocks_motion() && !aimtor_degraded() && s_hmi_mask != 0U ? 1U : 0U;
    taskEXIT_CRITICAL();
}

bool aimtor_hmi_control_active(void)
{
    return s_hmi_authorized != 0U;
}

void aimtor_cli_cancel_actions(void)
{
    servo_axis_t axis;
    taskENTER_CRITICAL();
    /* 已在总线上的事务不可撤回，取消后丢弃应答，不再发送后续启动步骤。 */
    (void)motion_stop(MOTION_STOP_IMMEDIATE);
    if (s_pending_id && aimtor_cli_axis_owned((servo_axis_t)s_pending_axis))
    {
        modbus_cancel_pending();
        s_pending_cancelled = 1U;
        s_pending_id = 0U;
    }
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!aimtor_cli_axis_owned(axis)) continue;
        s_axes[axis].action = ACTION_NONE;
        s_axes[axis].snapshot.busy = 0U;
        s_axes[axis].segment_latched = 0U;
        s_axes[axis].position_running = 0U;
        /* 部分配置或使能写可能已经生效，不继续信任原模式核验。 */
        s_axes[axis].snapshot.parameter_verified = 0U;
        s_axes[axis].snapshot.disable_verified = 0U;
    }
    taskEXIT_CRITICAL();
}

servo_result_t aimtor_cli_restore_hold(void)
{
    servo_axis_t axis;
    if (app_health_watchdog_test_requested()) return SERVO_NOT_READY;
    taskENTER_CRITICAL();
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!aimtor_cli_axis_owned(axis) || !s_hold[axis].eligible) continue;
        if (!aimtor_fresh(&s_axes[axis]) || s_axes[axis].snapshot.fault ||
            s_axes[axis].snapshot.busy || s_axes[axis].snapshot.enabled ||
            !s_axes[axis].snapshot.disable_verified ||
            s_axes[axis].snapshot.actual_speed_rpm != 0)
        {
            taskEXIT_CRITICAL();
            return SERVO_NOT_READY;
        }
        memset(&s_hold[axis], 0, sizeof(s_hold[axis]));
        s_hold[axis].eligible = 1U;
        s_hold[axis].state = SERVO_STARTUP_HOLD_WAITING;
        s_hold_enable_confirmed[axis] = 0U;
        s_hold_enable_poll[axis] = 0U;
    }
    s_hold_deadline = 0U;
    s_cli_restore = 1U;
    taskEXIT_CRITICAL();
    return SERVO_OK;
}

servo_result_t aimtor_disable(servo_axis_t axis)
{
    servo_result_t result;

    if (axis >= SERVO_AXIS_COUNT)
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (!app_config_servo_axis_controllable((uint8_t)axis))
    {
        return SERVO_NOT_CONFIGURED;
    }
    (void)servo_pulse_set_frequency(axis, 0U);
    if ((aimtor_startup_hold_active() || aimtor_k1_hold_active()) &&
        !(aimtor_cli_axis_owned(axis) && (s_cli_authorized || s_cli_internal)))
    {
        /* 人工失能撤销整组待机，取消迟到的使能应答，禁止自动重启。 */
        aimtor_emergency_stop();
        return SERVO_OK;
    }
    taskENTER_CRITICAL();
    if (!aimtor_watchdog_recovery_ready())
    {
        /* 恢复期间失能仍可排队，但不得覆盖正在进行的安全序列。 */
        s_axes[axis].disable_pending = 1U;
        taskEXIT_CRITICAL();
        return app_config_servo_readonly_test() ? SERVO_NOT_CONFIGURED : SERVO_OK;
    }
    result = aimtor_can_write(axis);
    if (result == SERVO_OK)
    {
        if (!s_axes[axis].snapshot.enabled &&
            s_axes[axis].snapshot.disable_verified)
        {
            s_axes[axis].snapshot.disable_verified = 1U;
            s_axes[axis].snapshot.last_result = SERVO_OK;
            s_axes[axis].disable_pending = 0U;
        }
        else
        {
            s_axes[axis].snapshot.disable_verified = 0U;
            aimtor_begin(axis, ACTION_DISABLE);
            /* 失能同样以06H写入应答完成，不追加瞬时读回。 */
            aimtor_add(axis, AIMTOR_REG_SERVO_ENABLE, 0U, 1U, 1U, 0U);
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

servo_result_t aimtor_clear_stop_latch(void)
{
    uint8_t axis;
    servo_result_t result;

    if (app_health_watchdog_test_requested() || s_controlled_safety_active || aimtor_recovery_blocks_motion()) return SERVO_NOT_READY;
    result = SERVO_OK;
    taskENTER_CRITICAL();
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        /* 只检查本次查询模式实际参与的轴，避免未选轴阻塞单轴解锁。 */
        if (!app_config_servo_axis_selected(axis))
        {
            continue;
        }
        if (!aimtor_fresh(&s_axes[axis]) || s_axes[axis].snapshot.fault ||
            s_axes[axis].snapshot.actual_speed_rpm != 0 ||
            (APP_SERVO_SOFT_ENABLE_CONFIRMED &&
             !s_axes[axis].snapshot.disable_verified) ||
            s_axes[axis].snapshot.busy || s_axes[axis].disable_pending)
        {
            result = SERVO_NOT_READY;
        }
    }
    if (result == SERVO_OK)
    {
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!app_config_servo_axis_selected(axis))
            {
                continue;
            }
            s_axes[axis].snapshot.stop_latched = 0U;
        }
        motion_clear_fault();
        s_recovery_disqualified = 0U;
        memset(&s_safety_status, 0, sizeof(s_safety_status));
    }
    taskEXIT_CRITICAL();
    return result;
}

servo_result_t aimtor_set_maintenance_enabled(bool enabled)
{
    servo_result_t result;

    if (enabled && app_sync_params_maintenance()) return SERVO_NOT_READY;
    if (enabled && app_health_watchdog_test_requested()) return SERVO_NOT_READY;
    taskENTER_CRITICAL();
    result = aimtor_both_idle() ? SERVO_OK : SERVO_BUSY;
    if (enabled && (app_config_servo_readonly_test() ||
                    !APP_SERVO_MAINTENANCE_STOP_CONFIRMED))
    {
        result = SERVO_NOT_CONFIGURED;
    }
    if (result == SERVO_OK)
    {
        (void)motion_stop(MOTION_STOP_IMMEDIATE);
        if (enabled != (s_maintenance_enabled != 0U))
        {
            uint8_t axis;
            for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
                if (s_axes[axis].snapshot.ramp_requested_ms[0] !=
                    (enabled ? APP_SERVO_DIRECT_ACCELERATION_RAMP_MS : APP_SERVO_SPEED_ACCELERATION_RAMP_MS))
                    s_axes[axis].snapshot.parameter_verified = 0U;
        }
        s_maintenance_enabled = enabled ? 1U : 0U;
    }
    taskEXIT_CRITICAL();
    return result;
}

/* 查询当前维护模式是否开启。 */
/*
*s_maintenance_enabled = 0 → 未开启维护模式
*s_maintenance_enabled = 1 → 已开启维护模式
*/
bool aimtor_maintenance_enabled(void)
{
    /* 标志非零返回true，标志为零返回false。 */
    return s_maintenance_enabled != 0U;
}

servo_result_t aimtor_set_velocity_rpm(servo_axis_t axis, int16_t rpm)
{
    servo_result_t result;
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    int16_t diagnostic_requested=rpm;
#endif

    if (rpm && !s_stop_write_internal && !modbus_motion_permitted()) return SERVO_NOT_READY;
    if ((app_sync_params_maintenance() || app_control_arbiter_arm_locked()) && rpm != 0) return SERVO_NOT_READY;
    if (app_health_watchdog_test_requested()) return SERVO_NOT_READY;
    if ((rpm > APP_DEBUG_MAX_RPM) || (rpm < -APP_DEBUG_MAX_RPM))
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (!app_config_servo_axis_controllable((uint8_t)axis))
    {
        return SERVO_NOT_CONFIGURED;
    }
    taskENTER_CRITICAL();
    if (s_controlled_safety_active || (rpm != 0 && app_control_arbiter_arm_locked()))
    {
        taskEXIT_CRITICAL();
        return SERVO_NOT_READY;
    }
    if (!aimtor_boot_zero_ready() || !aimtor_startup_hold_ready() ||
        (s_cli_internal && rpm != 0 && !s_stop_write_internal) ||
        (s_can_mask && !s_can_internal) ||
        (!aimtor_motion_allowed() && !s_stop_write_internal && !s_hmi_authorized))
    {
        taskEXIT_CRITICAL();
        return SERVO_NOT_READY;
    }
    result = aimtor_can_write(axis);
    if ((result == SERVO_OK) &&
        (!s_axes[axis].snapshot.enabled ||
         s_axes[axis].snapshot.control_mode != SERVO_CONTROL_RS485_VELOCITY))
    {
        result = SERVO_NOT_READY;
    }
    if (result == SERVO_OK && !s_stop_write_internal && aimtor_degraded())
    {
        rpm = aimtor_feedback_limit_rpm(1U, rpm, s_last_velocity[axis]);
        if (s_degraded_active) rpm = aimtor_feedback_limit_rpm(1U, rpm, s_degraded_limit[axis]);
    }
    if (result == SERVO_OK)
    {
        aimtor_begin(axis, ACTION_SPEED);
        if (++s_axes[axis].speed_write_sequence == 0U) ++s_axes[axis].speed_write_sequence;
        s_axes[axis].snapshot.speed_write_sequence =
            s_axes[axis].speed_write_sequence;
        aimtor_add(axis, AIMTOR_REG_SPEED_COMMAND, (uint16_t)rpm,
                   1U, 1U, 0U);
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
        app_link_diag_record(APP_LINK_RS485,0x20U,s_axes[axis].speed_write_sequence,
            ((uint32_t)(uint16_t)diagnostic_requested<<16U)|(uint16_t)rpm,
            ((uint32_t)app_control_arbiter_owner()<<24U)|((uint32_t)s_stop_write_internal<<16U)|axis,
            app_control_arbiter_generation(),(uint16_t)s_last_velocity[axis],
            ((uint32_t)s_can_internal<<16U)|s_degraded_active);
#endif
    }
    taskEXIT_CRITICAL();
    return result;
}

servo_result_t aimtor_set_stop_velocity_rpm(servo_axis_t axis, int16_t rpm)
{
    servo_result_t result;
    int16_t previous;
    if (axis >= SERVO_AXIS_COUNT) return SERVO_INVALID_ARGUMENT;
    taskENTER_CRITICAL();
    previous = s_last_velocity[axis];
    /* 专供撤销授权后的停车斜坡，不允许增速或换向。 */
    if (!((s_remote_control_mask | s_hmi_mask | s_can_mask | s_cli_mask) & (1U << axis)) ||
        (rpm > 0 && (previous <= 0 || rpm > previous)) ||
        (rpm < 0 && (previous >= 0 || rpm < previous)))
    {
        taskEXIT_CRITICAL();
        return SERVO_NOT_READY;
    }
    s_stop_write_internal = 1U;
    result = aimtor_set_velocity_rpm(axis, rpm);
    s_stop_write_internal = 0U;
    taskEXIT_CRITICAL();
    return result;
}

void aimtor_can_revoke(uint32_t generation)
{
    unsigned axis;
    taskENTER_CRITICAL();
    s_can_authorized = 0U; s_can_generation = generation;
    for (axis = 0U; axis < 2U; ++axis)
        if (s_can_action[axis] && s_axes[axis].snapshot.busy &&
            !s_can_action_stop[axis] &&
            !(s_pending_id && s_pending_axis == axis))
        {
            s_axes[axis].snapshot.speed_write_completed = s_axes[axis].speed_write_sequence;
            s_axes[axis].snapshot.speed_write_result = SERVO_NOT_READY;
            s_axes[axis].snapshot.busy = 0U; s_axes[axis].action = ACTION_NONE;
            s_can_action[axis] = 0U;
        }
    taskEXIT_CRITICAL();
}

servo_result_t aimtor_can_velocity(servo_axis_t axis, int16_t rpm,
                                  uint32_t generation, uint32_t batch, bool stop)
{
    servo_result_t result;
    if (rpm && !stop && !modbus_motion_permitted()) return SERVO_NOT_READY;
    if (!APP_CAN_CONTROL_ENABLED || axis >= SERVO_AXIS_COUNT) return SERVO_NOT_CONFIGURED;
    taskENTER_CRITICAL();
    if (!(s_can_mask & (1U << axis)) || (!stop && !s_can_authorized) ||
        app_health_watchdog_test_requested())
    { taskEXIT_CRITICAL(); return SERVO_NOT_READY; }
    s_can_internal = 1U;
    result = stop ? aimtor_set_stop_velocity_rpm(axis, rpm) : aimtor_set_velocity_rpm(axis, rpm);
    s_can_internal = 0U;
    if (result == SERVO_OK)
    {
        s_can_generation = generation;
        s_can_action[axis] = 1U; s_can_action_stop[axis] = stop ? 1U : 0U;
        s_can_action_generation[axis] = generation; s_can_action_batch[axis] = batch;
        s_can_action_sequence[axis] = s_axes[axis].speed_write_sequence;
    }
    taskEXIT_CRITICAL(); return result;
}

servo_result_t aimtor_run_segment(servo_axis_t axis, int32_t distance,
                                  uint16_t speed, uint16_t acceleration)
{
#if !APP_UNPROTECTED_MOTION_TEST_ENABLED
    (void)axis; (void)distance; (void)speed; (void)acceleration;
    return SERVO_NOT_CONFIGURED;
#else

    servo_result_t result;
    aimtor_step_t *step;

    if (s_cli_internal || !aimtor_motion_allowed()) return SERVO_NOT_READY;

    if ((distance == 0) ||
        (speed == 0U) || (speed > (uint16_t)APP_OPERATION_MAX_MOTOR_RPM) ||
        (acceleration == 0U))
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (!app_config_servo_axis_controllable((uint8_t)axis))
    {
        return SERVO_NOT_CONFIGURED;
    }
    taskENTER_CRITICAL();
    result = aimtor_can_write(axis);
    if ((result == SERVO_OK) &&
        (s_axes[axis].segment_latched ||
         s_axes[axis].snapshot.actual_speed_rpm != 0 ||
         s_axes[axis].snapshot.control_mode != SERVO_CONTROL_RS485_SEGMENT_POSITION))
    {
        result = SERVO_NOT_READY;
    }
    if (result == SERVO_OK)
    {
        /* 未核验到位状态位前，每次配置仅允许一次位置触发。 */
        s_axes[axis].segment_latched = 1U;
        s_axes[axis].position_running = 0U;
        aimtor_begin(axis, ACTION_SEGMENT);

        /*
         * 每次位置运行都重新执行完整启动序列，避免此前的使能状态、
         * 触发状态或驱动器内部参数缓存影响本次定位。
         * 顺序与已经验证成功的 F103 工程一致：
         * 先失能，设置位置模式和内部多段位置，再写入位置段，
         * 重新使能，最后通过 H03_05 的 0->1 上升沿触发。
         */
        aimtor_add(axis, AIMTOR_REG_SERVO_ENABLE, 0U, 1U, 1U, 0U);
        aimtor_add(axis, AIMTOR_REG_CONTROL_MODE,
                   AIMTOR_CONTROL_MODE_POSITION, 1U, 1U, 0U);
        aimtor_add(axis, AIMTOR_REG_POSITION_SOURCE,
                   AIMTOR_POSITION_SOURCE_SEGMENT, 1U, 1U, 0U);
        aimtor_add(axis, AIMTOR_REG_SEGMENT_CONTROL, 0U, 1U, 1U, 0U);
        aimtor_add(axis, AIMTOR_REG_SEGMENT_MODE, 1U, 1U, 1U, 0U);
        aimtor_add(axis, AIMTOR_REG_SEGMENT_SELECT, 0U, 1U, 1U, 0U);
        aimtor_add(axis, AIMTOR_REG_SEGMENT_DISTANCE,
                   (uint32_t)distance, 3U, 1U, 0U);
        step = &s_axes[axis].steps[s_axes[axis].count - 1U];
        step->words[2] = speed;
        aimtor_add(axis, AIMTOR_REG_SEGMENT_ACCELERATION,
                   acceleration, 1U, 1U, 0U);
        aimtor_add(axis, AIMTOR_REG_DI2_FUNCTION, 28U, 1U, 1U, 0U);
        aimtor_add(axis, AIMTOR_REG_SERVO_ENABLE, 1U, 1U, 1U, 0U);
        aimtor_add(axis, AIMTOR_REG_SEGMENT_TRIGGER, 0U, 1U, 1U, 0U);
        aimtor_add(axis, AIMTOR_REG_SEGMENT_TRIGGER, 1U, 1U, 1U, 0U);
    }
    taskEXIT_CRITICAL();
    return result;
#endif
}

servo_result_t aimtor_complete_segment(servo_axis_t axis)
{
    aimtor_axis_state_t *state;

    if (axis >= SERVO_AXIS_COUNT)
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (!app_config_servo_axis_controllable((uint8_t)axis))
    {
        return SERVO_NOT_CONFIGURED;
    }
    taskENTER_CRITICAL();
    state = &s_axes[axis];
    if (!state->segment_latched)
    {
        taskEXIT_CRITICAL();
        return SERVO_OK;
    }
    if (state->snapshot.busy)
    {
        taskEXIT_CRITICAL();
        return SERVO_BUSY;
    }
    if (!aimtor_fresh(state) || !state->snapshot.position_valid ||
        state->snapshot.actual_speed_rpm != 0U)
    {
        taskEXIT_CRITICAL();
        return SERVO_NOT_READY;
    }
    state->segment_latched = 0U;
    state->position_running = 0U;
    taskEXIT_CRITICAL();
    return SERVO_OK;
}

servo_result_t aimtor_stop(servo_axis_t axis)
{
    servo_snapshot_t snapshot;
    servo_result_t result;

    if (!aimtor_get_snapshot(axis, &snapshot))
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (snapshot.control_mode == SERVO_CONTROL_PULSE_POSITION)
    {
        /*
         * 脉冲调试停止时立即关闭定时器和AM26C31门控，避免
         * “servo left stop” 后仍按斜坡低速输出脉冲。
         */
        return motion_stop(MOTION_STOP_IMMEDIATE) == MOTION_OK ?
               SERVO_OK : SERVO_NOT_READY;
    }
    if (snapshot.control_mode == SERVO_CONTROL_RS485_VELOCITY)
    {
        result = aimtor_set_velocity_rpm(axis, 0);
        if (result == SERVO_OK)
        {
            return result;
        }
        /* 忙或通信异常时，不允许普通停止请求被静默丢弃。 */
    }
    return aimtor_disable(axis);
}

void aimtor_stop_all(void)
{
    servo_axis_t axis;
    servo_snapshot_t snapshot;

    (void)motion_stop(MOTION_STOP_CONTROLLED);
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_config_servo_axis_selected((uint8_t)axis))
        {
            continue;
        }
        (void)aimtor_get_snapshot(axis, &snapshot);
        if (snapshot.busy)
        {
            aimtor_emergency_stop();
            return;
        }
        if ((snapshot.enabled || snapshot.busy) &&
            snapshot.control_mode != SERVO_CONTROL_PULSE_POSITION)
        {
            (void)aimtor_stop(axis);
        }
    }
}

servo_result_t aimtor_zero_once(servo_axis_t axis)
{
#if !APP_SERVO_PULSE_ENABLED
    (void)axis;
    return SERVO_UNSUPPORTED;
#else
    servo_result_t result;
    servo_snapshot_t *snapshot;

    if (app_health_watchdog_test_requested()) return SERVO_NOT_READY;
    if (!aimtor_boot_zero_ready() ||
        (s_cli_gate_enabled && !aimtor_motion_allowed())) return SERVO_NOT_READY;

    if (app_config_servo_readonly_test() ||
        !app_config_servo_axis_selected((uint8_t)axis))
    {
        return SERVO_NOT_CONFIGURED;
    }
    taskENTER_CRITICAL();
    result = aimtor_can_write(axis);
    if (result == SERVO_OK)
    {
        snapshot = &s_axes[axis].snapshot;
        if (!aimtor_configuration_idle(axis) || snapshot->actual_speed_rpm != 0 ||
            !snapshot->parameter_verified || snapshot->zero_attempted ||
            snapshot->control_mode != SERVO_CONTROL_PULSE_POSITION)
        {
            snapshot->zero_attempted = 1U;
            result = SERVO_NOT_READY;
        }
        else
        {
            snapshot->zero_attempted = 1U;
            aimtor_begin(axis, ACTION_ZERO);
            /* 6只记录当前位置；4和5会触发回零运动，绝不可替代。 */
            aimtor_add(axis, AIMTOR_REG_HOME_METHOD,
                       AIMTOR_HOME_RECORD_CURRENT, 1U, 1U, 0U);
            aimtor_add(axis, AIMTOR_REG_ABSOLUTE_POSITION, 0U, 2U, 0U, 1U);
        }
    }
    taskEXIT_CRITICAL();
    return result;
#endif
}

servo_result_t aimtor_reset_fault(servo_axis_t axis)
{
    /* 未核验复位对象，不能用设零或EEPROM指令冒充报警复位。 */
    return axis < SERVO_AXIS_COUNT ? SERVO_UNSUPPORTED : SERVO_INVALID_ARGUMENT;
}

servo_result_t aimtor_save_parameters(servo_axis_t axis)
{
    /* 禁止在未核验持久化副作用时保存软使能、触发位和给定值。 */
    return axis < SERVO_AXIS_COUNT ? SERVO_UNSUPPORTED : SERVO_INVALID_ARGUMENT;
}

static void aimtor_fail(servo_axis_t axis)
{
    aimtor_action_t action;

    action = s_axes[axis].action;
    if (action == ACTION_HOLD_ZERO && aimtor_k1_hold_active())
        s_k1_hold.failure = 3U;
    if (action == ACTION_STARTUP_HOLD)
    {
        s_hold[axis].failure =
            s_hold[axis].state == SERVO_STARTUP_HOLD_WRITING_ZERO ? 2U : 3U;
    }
    s_axes[axis].snapshot.busy = 0U;
    s_axes[axis].snapshot.last_result = SERVO_NOT_READY;
    /*
     * 速度给定是独立的周期性写事务。
     * 单次写失败不代表模式、核验或当前使能状态失效，保留这些状态，
     * 由遥控服务记录失败并在下一周期重试。
     */
    if (action != ACTION_SPEED)
    {
        s_axes[axis].snapshot.parameter_verified = 0U;
    }
    else
    {
        s_axes[axis].snapshot.speed_write_completed =
            s_axes[axis].speed_write_sequence;
        s_axes[axis].snapshot.speed_write_result = SERVO_NOT_READY;
    }
    if (action == ACTION_CONFIGURE)
    {
        /* 模式配置失败只结束本次切换，不把普通配置错误升级为锁存故障。 */
        s_axes[axis].snapshot.control_mode = SERVO_CONTROL_NONE;
    }
    s_axes[axis].segment_latched = 0U;
    s_axes[axis].position_running = 0U;
    s_axes[axis].action = ACTION_NONE;
    /*
     * 只读核验和模式配置失败不触发联停锁存。
     * 使能、速度、位置等已进入控制阶段的写事务失败，仍必须执行
     * 急停和运动故障锁存，防止驱动器状态与软件状态不一致。
     */
    if ((action != ACTION_VERIFY) &&
        (action != ACTION_CONFIGURE) &&
        (action != ACTION_SPEED))
    {
        /*
         * 响应处理可能处于临界区，不能在这里嵌套执行急停和取消事务。
         * 由通信任务下一轮在临界区外完成急停。
         */
        s_emergency_pending = 1U;
    }
}

/* 先处理全部轴的失能与停稳，任何配置都不能抢在另一在线轴失能之前。 */
static void aimtor_startup_stop_service(TickType_t now)
{
    servo_axis_t axis;
    uint8_t all_stopped = 1U;
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        servo_startup_stop_status_t *startup = &s_startup_stop[axis];
        aimtor_axis_state_t *state = &s_axes[axis];
        if (!startup->eligible) continue;
        if ((startup->state == SERVO_STARTUP_STOP_CONFIGURING ||
             startup->state == SERVO_STARTUP_STOP_VERIFYING) &&
            aimtor_tick_due(now, s_startup_deadline[axis]))
        {
            aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,

                0x0104U, 0U);
            startup->state = SERVO_STARTUP_STOP_FAILED;

            startup->failure = 4U;
        }
        if (startup->state == SERVO_STARTUP_STOP_FAILED)
        {
            all_stopped = 0U;
            continue;
        }
        if (startup->state == SERVO_STARTUP_STOP_WAITING)
        {
            /* 在线仅要求有效速度通信；驱动器故障不能阻止失能。 */
            if (!state->speed_valid ||
                now - state->speed_tick >
                    pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS))
            {
                all_stopped = 0U;
                continue;
            }
            startup->state = SERVO_STARTUP_STOP_DISABLING;
            s_startup_deadline[axis] = aimtor_deadline_after(now, 10000U);
            s_startup_retry[axis] = now;
        }
        if (startup->state == SERVO_STARTUP_STOP_DISABLING ||
            startup->state == SERVO_STARTUP_STOP_WAITING_ZERO)
        {
            all_stopped = 0U;
            if (aimtor_tick_due(now, s_startup_deadline[axis]))
            {
                startup->failure = !startup->disable_ack ? 1U :
                    !startup->off_confirmed ? 2U : 3U;
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,
                    (uint16_t)(0x0100U | startup->failure), 0U);
                startup->state = SERVO_STARTUP_STOP_FAILED;
                continue;
            }
            if (state->snapshot.stop_latched)
            {
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,

                    0x0104U, 0U);
                startup->state = SERVO_STARTUP_STOP_FAILED;

                startup->failure = 4U;
                continue;
            }
            if (startup->state == SERVO_STARTUP_STOP_DISABLING &&
                !state->snapshot.busy && !state->disable_pending &&
                aimtor_tick_due(now, s_startup_retry[axis]) &&
                state->speed_valid &&
                now - state->speed_tick <=
                    pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS))
            {
                aimtor_begin(axis, ACTION_STARTUP_DISABLE);
                aimtor_add(axis, AIMTOR_REG_SERVO_ENABLE, 0U, 1U, 1U, 0U);
            }
            if (startup->state == SERVO_STARTUP_STOP_WAITING_ZERO &&
                state->speed_sequence != s_startup_speed_sequence[axis])
            {
                s_startup_speed_sequence[axis] = state->speed_sequence;
                if (!startup->off_confirmed || !aimtor_fresh(state) ||
                    state->snapshot.actual_speed_rpm != 0)
                    s_startup_zero_samples[axis] = 0U;
                else if (!s_startup_zero_samples[axis])
                {
                    s_startup_zero_samples[axis] = 1U;
                    s_startup_zero_tick[axis] = now;
                }
                else if (now - s_startup_zero_tick[axis] >= pdMS_TO_TICKS(20U))
                {
                    startup->zero_confirmed = 1U;
                }
            }
            if (startup->off_confirmed && startup->zero_confirmed)
            {
                if (state->snapshot.fault)
                {
                    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,

                        0x0105U, 0U);
                    startup->state = SERVO_STARTUP_STOP_FAILED;

                    startup->failure = 5U;
                }
                else
                {
                    startup->state = SERVO_STARTUP_STOP_CONFIGURING;
                    s_startup_deadline[axis] =
                        aimtor_deadline_after(now, 10000U);
                }
            }
        }
    }
    if (!all_stopped) return;
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        servo_startup_stop_status_t *startup = &s_startup_stop[axis];
        aimtor_axis_state_t *state = &s_axes[axis];
        servo_result_t result;
        if (!startup->eligible || startup->state == SERVO_STARTUP_STOP_DONE)
            continue;
        if ((startup->state == SERVO_STARTUP_STOP_VERIFYING ||
             startup->state == SERVO_STARTUP_STOP_CONFIGURING) &&
            aimtor_tick_due(now, s_startup_deadline[axis]))
        {
            aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,

                0x0104U, 0U);
            startup->state = SERVO_STARTUP_STOP_FAILED;

            startup->failure = 4U;
        }
        if (startup->state == SERVO_STARTUP_STOP_VERIFYING)
        {
            if (aimtor_tick_due(now, s_startup_deadline[axis]) ||
                (!state->snapshot.busy &&
                 state->snapshot.last_result != SERVO_OK))
            {
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,

                    0x0104U, 0U);
                startup->state = SERVO_STARTUP_STOP_FAILED;

                startup->failure = 4U;
            }
            else if (!state->snapshot.busy)
            {
                if (s_boot_stage[axis] == 1U)
                {
                    s_boot_zero_internal = 1U;
                    result = aimtor_request_parameter_verify(axis);
                    s_boot_zero_internal = 0U;
                    if (result == SERVO_OK)
                    {
                        aimtor_add(axis, AIMTOR_REG_SEGMENT_TRIGGER,
                                   0U, 1U, 0U, 1U);
                        s_boot_stage[axis] = 2U;
                    }
                }
                else if (aimtor_fresh(state) &&
                         state->snapshot.actual_speed_rpm == 0 &&
                         !state->snapshot.fault && !state->snapshot.stop_latched &&
                         state->snapshot.parameter_verified &&
                         state->snapshot.disable_verified)
                {
                    startup->state = SERVO_STARTUP_STOP_DONE;
                }
            }
        }
        else if (startup->state == SERVO_STARTUP_STOP_CONFIGURING &&
                 !state->snapshot.busy && aimtor_fresh(state))
        {
            s_boot_zero_internal = 1U;
            result = aimtor_set_control_mode(axis, SERVO_CONTROL_RS485_VELOCITY);
            s_boot_zero_internal = 0U;
            if (result == SERVO_OK)
            {
                /* 清除内部位置启动沿，后续使能不能重放旧位置段。 */
                aimtor_add(axis, AIMTOR_REG_SEGMENT_TRIGGER, 0U, 1U, 1U, 0U);
                startup->state = SERVO_STARTUP_STOP_VERIFYING;
                s_boot_stage[axis] = 1U;
                s_startup_deadline[axis] = aimtor_deadline_after(now, 10000U);
            }
        }
    }
}

/* 本流程只在通信任务的临界区推进，外部模式请求不能抢占准备序列。 */
static void aimtor_boot_zero_service(TickType_t now)
{
    servo_axis_t axis;
    if (!aimtor_startup_stop_ready()) return;
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        servo_boot_zero_status_t *boot = &s_boot_zero[axis];
        aimtor_axis_state_t *state = &s_axes[axis];
        servo_result_t result;
        if (!boot->eligible || boot->state == SERVO_BOOT_ZERO_DONE ||
            boot->state == SERVO_BOOT_ZERO_FAILED)
            continue;
        if (boot->state == SERVO_BOOT_ZERO_VERIFYING)
        {
            if (aimtor_fresh(state) && state->snapshot.position_valid &&
                state->position_sequence != s_boot_position_sequence[axis] &&
                state->snapshot.actual_position == 0 &&
                !state->snapshot.enabled && !state->snapshot.fault &&
                !state->snapshot.stop_latched &&
                state->snapshot.actual_speed_rpm == 0)
            {
                boot->state = SERVO_BOOT_ZERO_DONE;
                state->snapshot.zero_verified = 1U;
            }
            else if (aimtor_tick_due(now, s_boot_deadline[axis]))
            {
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,

                    0x0203U, 0U);
                boot->state = SERVO_BOOT_ZERO_FAILED;

                boot->failure = 3U;
            }
            else if (!state->snapshot.busy)
            {
                (void)aimtor_request_position(axis);
            }
            continue;
        }
        if (boot->state == SERVO_BOOT_ZERO_PREPARING &&
            aimtor_tick_due(now, s_boot_deadline[axis]))
        {
            aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,

                0x0201U, 0U);
            boot->state = SERVO_BOOT_ZERO_FAILED;

            boot->failure = 1U;
            continue;
        }
        if (state->snapshot.busy) continue;
        if (state->snapshot.stop_latched || state->snapshot.fault)
        {
            aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,

                0x0204U, 0U);
            boot->state = SERVO_BOOT_ZERO_FAILED;

            boot->failure = 4U;
            continue;
        }
        if (!aimtor_fresh(state) || state->snapshot.actual_speed_rpm != 0)
            continue;
        if (boot->state == SERVO_BOOT_ZERO_WAITING)
        {
            /* 配置序列首先写失能；不能信任启动时的软件默认enabled=0。 */
            s_boot_zero_internal = 1U;
            result = aimtor_set_control_mode(axis, (APP_SERVO_PULSE_ENABLED && APP_UNPROTECTED_MOTION_TEST_ENABLED) ?
                SERVO_CONTROL_PULSE_POSITION : SERVO_CONTROL_RS485_VELOCITY);
            s_boot_zero_internal = 0U;
            if (result == SERVO_OK)
            {
                boot->state = SERVO_BOOT_ZERO_PREPARING;
                s_boot_stage[axis] = 1U;
                s_boot_deadline[axis] = aimtor_deadline_after(now, 10000U);
            }
            continue;
        }
        if (boot->state == SERVO_BOOT_ZERO_PREPARING)
        {
            if (state->snapshot.last_result != SERVO_OK)
            {
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,

                    0x0201U, 0U);
                boot->state = SERVO_BOOT_ZERO_FAILED;

                boot->failure = 1U;
                continue;
            }
            if (s_boot_stage[axis] == 1U)
            {
                s_boot_zero_internal = 1U;
                result = aimtor_request_parameter_verify(axis);
                s_boot_zero_internal = 0U;
                if (result == SERVO_OK) s_boot_stage[axis] = 2U;
                continue;
            }
            boot->state = SERVO_BOOT_ZERO_WRITING;
            s_boot_retry[axis] = now;
        }
        if (boot->state == SERVO_BOOT_ZERO_WRITING &&
            aimtor_tick_due(now, s_boot_retry[axis]) &&
            state->snapshot.parameter_verified &&
            state->snapshot.disable_verified && !state->snapshot.enabled &&
            aimtor_both_idle())
        {
            if (boot->attempts >= 3U)
            {
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,

                    0x0202U, 0U);
                boot->state = SERVO_BOOT_ZERO_FAILED;

                boot->failure = 2U;
                continue;
            }
            aimtor_begin(axis, ACTION_BOOT_ZERO);
            aimtor_add(axis, AIMTOR_REG_HOME_METHOD,
                       AIMTOR_HOME_RECORD_CURRENT, 1U, 1U, 0U);
        }
    }
}

/* 看门狗复位后的恢复只允许执行这一条安全序列。 */
static void aimtor_watchdog_recovery_service(TickType_t now)
{
    servo_axis_t axis;
    aimtor_axis_state_t *state;
    aimtor_step_t poll;
    uint8_t offset;
    uint8_t phase;
    uint16_t tag;

    /* 两轴轮转，离线轴不能阻塞另一轴的安全停车。 */
    for (offset = 0U; offset < SERVO_AXIS_COUNT; offset++)
    {
        servo_wdg_recovery_status_t *recovery;
        axis = (servo_axis_t)((s_wdg_axis_turn + offset) % SERVO_AXIS_COUNT);
        recovery = &s_wdg_recovery[axis];
        state = &s_axes[axis];
        if (state->disable_pending && !app_config_servo_readonly_test() &&
            aimtor_tick_due(now, s_wdg_retry[axis]))
        {
            if (s_wdg_manual_attempts[axis] < APP_WDG_RECOVERY_RETRY_MAX)
            {
                memset(&poll, 0, sizeof(poll));
                poll.address = AIMTOR_REG_SERVO_ENABLE;
                poll.count = poll.write = 1U;
                if (aimtor_submit_step(axis, AIMTOR_TAG_WDG_OFF, &poll, 0U)
                    == MODBUS_OK)
                {
                    s_wdg_manual_attempts[axis]++;
                    s_wdg_axis_turn = (uint8_t)((axis + 1U) % SERVO_AXIS_COUNT);
                    return;
                }
            }
            else state->disable_pending = 0U;
        }
        if (!recovery->eligible ||
            recovery->state == SERVO_WDG_RECOVERY_DONE ||
            recovery->state == SERVO_WDG_RECOVERY_FAILED)
        {
            continue;
        }
        if (aimtor_tick_due(now, s_wdg_deadline[axis]) &&
            recovery->state != SERVO_WDG_RECOVERY_STOP_LOCAL)
        {
            aimtor_watchdog_recovery_fail(axis,
                (recovery->state == SERVO_WDG_RECOVERY_DECELERATING ||
                 recovery->state == SERVO_WDG_RECOVERY_CONFIRM_ZERO) ? 7U : 1U);
            continue;
        }
        if (recovery->state == SERVO_WDG_RECOVERY_STOP_LOCAL)
        {
            recovery->state = SERVO_WDG_RECOVERY_WAIT_ONLINE;
            s_wdg_deadline[axis] =
                aimtor_deadline_after(now, APP_WDG_RECOVERY_TIMEOUT_MS);
        }
        if (!aimtor_tick_due(now, s_wdg_retry[axis])) continue;
        if (recovery->state == SERVO_WDG_RECOVERY_WRITE_DECEL ||
            recovery->state == SERVO_WDG_RECOVERY_VERIFY_DECEL ||
            recovery->state == SERVO_WDG_RECOVERY_WRITE_ZERO)
        {
            if (!aimtor_fresh(state) || state->snapshot.fault)
            {
                aimtor_watchdog_recovery_fail(axis,
                    state->snapshot.fault ? 2U : 6U);
                continue;
            }
            memset(&poll, 0, sizeof(poll));
            poll.count = 1U;
            poll.address = recovery->state == SERVO_WDG_RECOVERY_WRITE_ZERO ?
                AIMTOR_REG_SPEED_COMMAND : AIMTOR_REG_DECELERATION_TIME;
            poll.words[0] = recovery->state == SERVO_WDG_RECOVERY_WRITE_ZERO ?
                0U : APP_WDG_RECOVERY_DECEL_MS;
            if (s_wdg_step_attempts[axis] >= APP_WDG_RECOVERY_RETRY_MAX)
            {
                aimtor_watchdog_recovery_fail(axis,
                    recovery->state == SERVO_WDG_RECOVERY_VERIFY_DECEL ? 5U : 4U);
                continue;
            }
            tag = recovery->state == SERVO_WDG_RECOVERY_WRITE_ZERO ?
                AIMTOR_TAG_WDG_ZERO : AIMTOR_TAG_WDG_RAMP;
            if (aimtor_submit_step(axis, tag, &poll,
                recovery->state == SERVO_WDG_RECOVERY_VERIFY_DECEL) == MODBUS_OK)
            {
                s_wdg_step_attempts[axis]++;
                s_wdg_axis_turn = (uint8_t)((axis + 1U) % SERVO_AXIS_COUNT);
                return;
            }
            continue;
        }
        if ((recovery->state == SERVO_WDG_RECOVERY_DECELERATING ||
             recovery->state == SERVO_WDG_RECOVERY_CONFIRM_ZERO) &&
            !aimtor_fresh(state))
        {
            aimtor_watchdog_recovery_fail(axis, 6U);
            continue;
        }
        if (recovery->state == SERVO_WDG_RECOVERY_PREPARE_SPEED)
        {
            if (!aimtor_register_map_valid() ||
                !app_config_servo_bus_ready() ||
                !APP_SERVO_SOFT_ENABLE_CONFIRMED)
            {
                aimtor_watchdog_recovery_fail(axis, 3U);
                continue;
            }
            aimtor_begin(axis, ACTION_WDG_RECOVERY);
            aimtor_add(axis, AIMTOR_REG_SERVO_ENABLE, 0U, 1U, 1U, 0U);
            aimtor_add(axis, AIMTOR_REG_CONTROL_MODE,
                       AIMTOR_CONTROL_MODE_SPEED, 1U, 1U, 0U);
            aimtor_add(axis, AIMTOR_REG_SPEED_SELECTION, 0U, 1U, 1U, 0U);
            aimtor_add(axis, AIMTOR_REG_SPEED_SOURCE, 0U, 1U, 1U, 0U);
            aimtor_add(axis, AIMTOR_REG_SPEED_COMMAND, 0U, 1U, 1U, 0U);
            aimtor_add(axis, AIMTOR_REG_ACCELERATION_TIME,
                       APP_SERVO_SPEED_ACCELERATION_RAMP_MS, 1U, 1U, 1U);
            aimtor_add(axis, AIMTOR_REG_DECELERATION_TIME,
                       APP_SERVO_SPEED_DECELERATION_RAMP_MS, 1U, 1U, 1U);
            aimtor_add(axis, AIMTOR_REG_SERVO_ENABLE, 0U, 1U, 1U, 0U);
            /* 停稳后才失能、切换来源及恢复正常斜坡，不带速重新使能。 */
            recovery->state = SERVO_WDG_RECOVERY_DISABLE;
            s_wdg_deadline[axis] =
                aimtor_deadline_after(now, APP_WDG_RECOVERY_TIMEOUT_MS);
        }
        if (recovery->state == SERVO_WDG_RECOVERY_DISABLE &&
            state->snapshot.busy)
        {
            aimtor_step_t *step = &state->steps[state->index];
            if (s_wdg_step_attempts[axis] >= APP_WDG_RECOVERY_RETRY_MAX ||
                (step->address == AIMTOR_REG_SERVO_ENABLE &&
                 recovery->attempts >= APP_WDG_RECOVERY_RETRY_MAX))
            {
                aimtor_watchdog_recovery_fail(axis, 4U);
                continue;
            }
            if (aimtor_submit_step(axis, AIMTOR_TAG_SEQUENCE,
                                   step, 0U) == MODBUS_OK)
            {
                s_wdg_step_attempts[axis]++;
                if (step->address == AIMTOR_REG_SERVO_ENABLE)
                    recovery->attempts++;
                s_wdg_axis_turn = (uint8_t)((axis + 1U) % SERVO_AXIS_COUNT);
                return;
            }
            continue;
        }
        if (recovery->state == SERVO_WDG_RECOVERY_WAIT_ONLINE ||
            recovery->state == SERVO_WDG_RECOVERY_VERIFY ||
            recovery->state == SERVO_WDG_RECOVERY_DECELERATING ||
            recovery->state == SERVO_WDG_RECOVERY_CONFIRM_ZERO)
        {
            phase = s_wdg_poll_phase[axis];
            memset(&poll, 0, sizeof(poll));
            poll.address = s_wdg_read_addresses[phase];
            poll.count = 1U;
            if (aimtor_submit_step(axis, AIMTOR_TAG_WDG_VERIFY,
                                   &poll, 1U) == MODBUS_OK)
            {
                s_wdg_axis_turn = (uint8_t)((axis + 1U) % SERVO_AXIS_COUNT);
                return;
            }
        }
    }
    /* 失败锁存后仍开放只读诊断，但绝不调度普通运动事务。 */
    if (aimtor_pick_poll(&axis, &tag, now, 0U))
    {
        memset(&poll, 0, sizeof(poll));
        poll.address = aimtor_poll_address(tag);
        poll.count = aimtor_poll_count(tag);
        (void)aimtor_submit_step(axis, tag, &poll, 1U);
    }
}

static void aimtor_finish(servo_axis_t axis)
{
    aimtor_axis_state_t *state;

    state = &s_axes[axis];
    if ((state->action == ACTION_CONFIGURE) || (state->action == ACTION_VERIFY))
    {
        state->snapshot.parameter_verified = app_config_servo_parameters_confirmed();
        state->snapshot.control_mode = state->requested_mode;
        state->snapshot.disable_verified = 1U;
        /* 配置与核验序列均确认失能寄存器为零，同步撤销本地脉冲许可。 */
        state->snapshot.enabled = 0U;
        (void)servo_pulse_set_enable(axis, false);
    }
    else if (state->action == ACTION_SPEED)
    {
        s_last_velocity[axis] = (int16_t)state->steps[0].words[0];
        state->snapshot.speed_write_rpm = s_last_velocity[axis];
        state->snapshot.speed_write_rpm_valid = 1U;
        state->snapshot.speed_write_tick = (uint32_t)((uint64_t)xTaskGetTickCount() * 1000U / configTICK_RATE_HZ);
        state->snapshot.speed_write_completed =
            state->speed_write_sequence;
        state->snapshot.speed_write_result = SERVO_OK;
    }
    else if (state->action == ACTION_ENABLE)
    {
        state->snapshot.enabled = 1U;
        if (state->snapshot.control_mode == SERVO_CONTROL_PULSE_POSITION)
        {
            (void)servo_pulse_set_enable(axis, true);
        }
    }
    else if (state->action == ACTION_SEGMENT)
    {
        /* 位置事务最后一步为重新使能，成功后才更新软件使能状态。 */
        state->snapshot.enabled = 1U;
        state->snapshot.disable_verified = 0U;
    }
    else if (state->action == ACTION_ZERO)
    {
        state->snapshot.zero_verified = 1U;
    }
    else if (state->action == ACTION_DISABLE)
    {
        state->snapshot.enabled = 0U;
        state->snapshot.disable_verified = 1U;
        state->disable_pending = 0U;
    }
    else if (state->action == ACTION_WDG_RECOVERY)
    {
        s_wdg_recovery[axis].disable_ack = 1U;
        s_wdg_recovery[axis].state = SERVO_WDG_RECOVERY_VERIFY;
        s_wdg_poll_phase[axis] = 0U;
        s_wdg_retry[axis] = xTaskGetTickCount();
        state->snapshot.control_mode = SERVO_CONTROL_RS485_VELOCITY;
    }
    else if (state->action == ACTION_STARTUP_HOLD)
    {
        if (s_hold[axis].state == SERVO_STARTUP_HOLD_WRITING_ZERO)
        {
            s_hold[axis].zero_written = 1U;
            s_hold[axis].state = SERVO_STARTUP_HOLD_ENABLING;
        }
        else
        {
            s_hold[axis].enable_ack = 1U;
            s_hold[axis].state = SERVO_STARTUP_HOLD_VERIFYING;
            s_hold_speed_sequence[axis] = state->speed_sequence;
            s_hold_verify_tick[axis] = xTaskGetTickCount();
            s_hold_enable_poll[axis] = xTaskGetTickCount();
            s_hold_enable_confirmed[axis] = 0U;
            state->snapshot.enabled = 1U;
            state->next_speed_poll = xTaskGetTickCount();
            state->next_fault_poll = xTaskGetTickCount();
        }
    }
    state->snapshot.busy = 0U;
    state->snapshot.last_result = SERVO_OK;
    state->action = ACTION_NONE;
    s_can_action[axis] = 0U;
}

/* 写事务禁自动重试，避免触发指令在丢失应答后被重复执行。 */
static modbus_status_t aimtor_submit_step(servo_axis_t axis, uint16_t tag,
                                         aimtor_step_t *step, uint8_t read)
{
    modbus_request_t request;
    uint8_t index;
    modbus_status_t result;
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    uint16_t diagnostic_raw=step->words[0];
#endif

    /* 最终提交处再次过滤未选轴，避免新增事务路径访问缺席轴。 */
    if (!app_config_servo_axis_selected((uint8_t)axis))
    {
        return MODBUS_INVALID_ARGUMENT;
    }
    /* The RS485 worker is single-transaction.  Never enqueue a second
     * request while a previous response is still pending; doing so can make
     * startup configuration and feedback polls overwrite each other's
     * transaction identity. */
    if (s_pending_id != 0U)
        return MODBUS_QUEUE_FULL;
    if (!aimtor_cold_clear_ready() && tag != AIMTOR_TAG_COLD_CLEAR)
        return MODBUS_INVALID_ARGUMENT;
    if (!read && app_control_arbiter_arm_locked()) {
        /* Recheck the immutable sequence at the final writer, including FC16.
         * Zero-speed and independent stopping remain available. */
        for(index=0U;index<step->count;++index)
            if((uint16_t)(step->address+index)==AIMTOR_REG_SPEED_COMMAND && step->words[index]!=0U)
                return MODBUS_INVALID_ARGUMENT;
        if(step->address==AIMTOR_REG_SEGMENT_TRIGGER && step->words[0]!=0U)
            return MODBUS_INVALID_ARGUMENT;
    }
    memset(&request, 0, sizeof(request));
    request.context.owner = AIMTOR_OWNER_ID;
    request.context.axis = (uint8_t)axis;
    request.context.transaction_id = ++s_transaction_id;
    request.context.tag = tag;
    request.slave = aimtor_get_slave_address(axis);
    request.address = step->address;
    request.quantity = step->count;
    request.timeout_ms = APP_MODBUS_RESPONSE_TIMEOUT_MS;
    if (tag == AIMTOR_TAG_SEQUENCE && s_can_action[axis] &&
        !s_can_action_stop[axis] && s_can_action_generation[axis] != s_can_generation)
        return MODBUS_INVALID_ARGUMENT;
    request.retries = read ?
        (tag == AIMTOR_TAG_SEQUENCE ? APP_MODBUS_MAX_RETRIES :
                                      APP_SERVO_POLL_RETRIES) : 0U;
    /* 低优先级诊断不在总线上自动重试，释放总线后再后台补采。 */
    if (aimtor_is_diagnostic_tag(tag) || tag == AIMTOR_TAG_COLD_CLEAR) request.retries = 0U;
    request.function = read ? 0x03U : step->count == 1U ? 0x06U : 0x10U;
    /* A write never creates or refreshes a feedback sample. Keep its real age. */
    if (!read && step->address == AIMTOR_REG_SERVO_ENABLE)
        s_hold_enable_poll[axis] = xTaskGetTickCount();
    if (!read && app_config_servo_readonly_test())
    {
        /* 最终组帧处再次拦截写事务，防止新增调用路径绕过只读门禁。 */
        return MODBUS_INVALID_ARGUMENT;
    }
    request.value = step->words[0];
    if (request.function == 0x10U)
    {
        request.payload[0] = (uint8_t)(request.address >> 8U);
        request.payload[1] = (uint8_t)request.address;
        request.payload[2] = 0U;
        request.payload[3] = step->count;
        request.payload[4] = (uint8_t)(step->count * 2U);
        for (index = 0U; index < step->count; index++)
        {
            request.payload[5U + index * 2U] = (uint8_t)(step->words[index] >> 8U);
            request.payload[6U + index * 2U] = (uint8_t)step->words[index];
        }
        request.payload_length = (uint16_t)(5U + step->count * 2U);
    }
    if (!read && tag == AIMTOR_TAG_SEQUENCE && s_axes[axis].action == ACTION_SPEED &&
        aimtor_degraded())
    {
        int16_t limited = aimtor_feedback_limit_rpm(1U, (int16_t)step->words[0], s_last_velocity[axis]);
        if (s_degraded_active) limited = aimtor_feedback_limit_rpm(1U, limited, s_degraded_limit[axis]);
        step->words[0] = (uint16_t)limited;
        request.value = (uint16_t)limited;
    }
    result = modbus_submit(&request, 0U);
    if (result == MODBUS_OK)
    {
        if (tag == AIMTOR_TAG_SEQUENCE &&
            s_axes[axis].action == ACTION_STARTUP_DISABLE)
            s_startup_stop[axis].attempts++;
        if (tag == AIMTOR_TAG_SEQUENCE &&
            s_axes[axis].action == ACTION_BOOT_ZERO)
        {
            s_boot_zero[axis].attempts++;
            s_axes[axis].snapshot.zero_attempted = 1U;
        }
        {
            uint8_t item = aimtor_poll_item(tag);
            if (item < SERVO_POLL_CLASSES) {
                TickType_t due = tag == AIMTOR_TAG_SPEED ? s_axes[axis].next_speed_poll :
                    tag == AIMTOR_TAG_FAULT ? s_axes[axis].next_fault_poll :
                    tag == AIMTOR_TAG_HOLD_ENABLE ? s_hold_enable_poll[axis] :
                    tag == AIMTOR_TAG_POSITION ? s_axes[axis].next_position_poll :
                    tag == AIMTOR_TAG_STATE ? s_axes[axis].next_state_poll :
                    s_axes[axis].next_diagnostic_poll[item - 5U];
                uint32_t wait = aimtor_tick_due(xTaskGetTickCount(), due) ?
                    (uint32_t)((uint64_t)(xTaskGetTickCount() - due) * 1000U / configTICK_RATE_HZ) : 0U;
                s_poll_stats[axis][item].submitted++;
                if (wait > s_poll_stats[axis][item].max_wait_ms) s_poll_stats[axis][item].max_wait_ms = wait;
                if (item < 3U) { if (s_background_critical < 255U) s_background_critical++; }
                else s_background_critical = 0U;
            }
        }
        s_pending_id = request.context.transaction_id;
        s_pending_tag = tag;
        s_pending_axis = (uint8_t)axis;
        s_pending_cancelled = 0U;
        s_can_pending.valid = tag == AIMTOR_TAG_SEQUENCE && s_can_action[axis];
        if (s_can_pending.valid)
        {
            s_can_pending.transaction = s_pending_id; s_can_pending.axis = (uint8_t)axis;
            s_can_pending.generation = s_can_action_generation[axis];
            s_can_pending.batch = s_can_action_batch[axis];
            s_can_pending.sequence = s_can_action_sequence[axis];
            s_can_pending.stop = s_can_action_stop[axis];
        }
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
        if(!read && step->address==AIMTOR_REG_SPEED_COMMAND)
            app_link_diag_record(APP_LINK_RS485,0x21U,s_pending_id,
                ((uint32_t)diagnostic_raw<<16U)|request.value,
                ((uint32_t)app_control_arbiter_owner()<<24U)|((uint32_t)s_can_pending.valid<<16U)|axis,
                app_control_arbiter_generation(),s_can_pending.valid?s_can_pending.batch:0U,
                s_axes[axis].speed_write_sequence);
#endif
    }
    return result;
}

/* Cold boot preflight owns the business request channel until completion.
 * The independent guard keeps its read-only priority channel and safety takeover.
 */
static uint32_t aimtor_cold_ms(TickType_t elapsed)
{ return (uint32_t)((uint64_t)elapsed * 1000U / configTICK_RATE_HZ); }

bool aimtor_cold_clear_ready(void)
{ return s_cold_phase == SERVO_COLD_CLEAR_SKIPPED || s_cold_phase == SERVO_COLD_CLEAR_DONE; }

void aimtor_cold_clear_init(uint32_t reset_flags, uint32_t retained_safety)
{
    uint8_t axis;
    if (s_cold_initialized) return; /* A failure cannot be restarted in this boot. */
    s_cold_initialized = 1U;
    if (!(reset_flags & (1UL << 27U)) ||
        (reset_flags & ((1UL << 28U) | (1UL << 29U) | (1UL << 30U))) ||
        retained_safety || app_config_servo_readonly_test()) return;
    s_cold_phase = SERVO_COLD_CLEAR_OBSERVING;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        if (app_config_servo_axis_selected(axis)) {
            s_cold[axis].eligible = 1U;
            s_cold[axis].state = s_cold_phase;
        }
}

bool aimtor_cold_clear_get_status(servo_axis_t axis, servo_cold_clear_status_t *status)
{
    if (axis >= SERVO_AXIS_COUNT || !status) return false;
    taskENTER_CRITICAL();
    *status = s_cold[axis];
    if (s_cold_started && s_cold_phase != SERVO_COLD_CLEAR_DONE &&
        s_cold_phase != SERVO_COLD_CLEAR_FAILED)
        status->elapsed_ms = aimtor_cold_ms(xTaskGetTickCount() - s_cold_start);
    taskEXIT_CRITICAL();
    return true;
}

static uint8_t aimtor_cold_fresh(uint8_t axis, TickType_t now)
{
    aimtor_axis_state_t *state = &s_axes[axis];
    return s_cold_seen[axis] == 7U && state->speed_valid && state->fault_valid &&
        state->snapshot.enable_feedback_valid &&
        aimtor_cold_ms(now - state->speed_tick) < APP_COLD_CLEAR_FEEDBACK_MAX_MS &&
        aimtor_cold_ms(now - state->fault_tick) < APP_COLD_CLEAR_FEEDBACK_MAX_MS &&
        aimtor_cold_ms(now - state->snapshot.enable_feedback_tick) < APP_COLD_CLEAR_FEEDBACK_MAX_MS;
}

bool aimtor_cold_clear_suppresses_fault(servo_axis_t axis)
{
    return axis < SERVO_AXIS_COUNT && s_cold[axis].eligible &&
        !aimtor_cold_clear_ready() && s_cold_phase != SERVO_COLD_CLEAR_FAILED &&
        aimtor_cold_fresh((uint8_t)axis, xTaskGetTickCount()) &&
        s_cold[axis].zero_samples >= 2U && !s_axes[axis].snapshot.enable_feedback &&
        !s_axes[axis].snapshot.actual_speed_rpm &&
        s_axes[axis].snapshot.fault_code == AIMTOR_FAULT_MODBUS_TIMEOUT;
}

static void aimtor_cold_phase(servo_cold_clear_state_t phase)
{
    uint8_t axis;
    s_cold_phase = phase;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) if (s_cold[axis].eligible) {
        s_cold[axis].state = phase;
        s_cold[axis].elapsed_ms = aimtor_cold_ms(xTaskGetTickCount() - s_cold_start);
    }
}

static void aimtor_cold_fail(servo_cold_clear_failure_t reason, uint8_t communication)
{
    uint8_t axis;
    if (s_cold_phase == SERVO_COLD_CLEAR_FAILED) return;
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, 0xFFU,
                            (uint16_t)(0x0400U | reason), communication);
    aimtor_cold_phase(SERVO_COLD_CLEAR_FAILED);
    s_motion_authorized = s_cli_authorized = s_hmi_authorized = s_can_authorized = 0U;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) if (s_cold[axis].eligible) {
        s_cold[axis].failure = reason;
        s_startup_stop[axis].eligible = 1U;
        s_startup_stop[axis].state = SERVO_STARTUP_STOP_FAILED;
        s_startup_stop[axis].failure = 6U; /* Detail is in cold_clear diagnostics. */
        s_axes[axis].snapshot.stop_latched = 1U;
    }
    if (communication) app_health_request_driver_fallback(0x100U + (uint32_t)reason);
}

static uint8_t aimtor_cold_all_stopped(TickType_t now, uint8_t off)
{
    uint8_t axis;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) if (s_cold[axis].eligible &&
        (!aimtor_cold_fresh(axis, now) || s_cold[axis].zero_samples < 2U ||
         s_axes[axis].snapshot.actual_speed_rpm || (off && s_axes[axis].snapshot.enable_feedback)))
        return 0U;
    return 1U;
}

static void aimtor_cold_clear_response(const modbus_response_t *response)
{
    uint8_t axis = response->context.axis;
    aimtor_axis_state_t *state = &s_axes[axis];
    servo_cold_clear_status_t *cold = &s_cold[axis];
    TickType_t now = xTaskGetTickCount();
    uint16_t value;
    uint8_t valid = response->status == MODBUS_OK &&
        response->slave == aimtor_get_slave_address((servo_axis_t)axis) &&
        response->address == s_cold_pending_address && response->quantity == 1U &&
        response->function == (s_cold_pending_write ? 6U : 3U) &&
        response->operation == (s_cold_pending_write ? MODBUS_OPERATION_WRITE_SINGLE : MODBUS_OPERATION_READ_HOLDING);
    cold->last_result = response->status;
    if (s_cold_phase == SERVO_COLD_CLEAR_FAILED || !modbus_business_allowed()) return;
    if (s_cold_pending_write) {
        valid = valid && response->value == s_cold_pending_value;
        if (s_cold_pending_address == AIMTOR_REG_FAULT_RESET) {
            cold->write_ack = valid;
            /* Lost ACK is ambiguous: never repeat this write; observe actual fault. */
            if (!valid && response->status != MODBUS_TIMEOUT && response->status != MODBUS_CRC_ERROR &&
                response->status != MODBUS_FRAME_ERROR && response->status != MODBUS_TRANSPORT_ERROR) {
                aimtor_cold_fail(SERVO_COLD_FAIL_CLEAR, 0U);
            }
        } else if (!valid) aimtor_cold_fail(SERVO_COLD_FAIL_DISABLE, 1U);
        return;
    }
    valid = valid && response->data_length == 3U && response->data[0] == 2U;
    if (!valid) {
        if (s_cold_online) aimtor_cold_fail(SERVO_COLD_FAIL_FEEDBACK, 1U);
        return;
    }
    value = (uint16_t)(((uint16_t)response->data[1] << 8U) | response->data[2]);
    state->snapshot.online = 1U;
    state->snapshot.update_tick = now;
    state->snapshot.poll_successes++;
    if (s_cold_pending_address == AIMTOR_REG_ACTUAL_SPEED) {
        aimtor_diag_speed(response,state);
        aimtor_feedback_valid(axis, 0U, response->context.transaction_id, now);
        state->snapshot.actual_speed_rpm = (int16_t)value;
        state->snapshot.actual_velocity_mps = (float)(int16_t)value * APP_WHEEL_CIRCUMFERENCE_M /
            (60.0f * APP_GEAR_REDUCTION_RATIO);
        state->speed_tick = now; state->speed_sequence++;
        state->speed_valid = state->snapshot.speed_valid = 1U;
        aimtor_record_speed_sample(state);
        s_cold_seen[axis] |= 1U; s_cold_post[axis] |= 1U;
        if (value) { aimtor_cold_fail(SERVO_COLD_FAIL_MOVING, 0U); return; }
        if (!cold->zero_samples) { cold->zero_samples = 1U; s_cold_zero_tick[axis] = now; }
        else if (aimtor_cold_ms(now - s_cold_zero_tick[axis]) >= 20U) cold->zero_samples = 2U;
    } else if (s_cold_pending_address == AIMTOR_REG_FAULT_CODE) {
        aimtor_feedback_valid(axis, 1U, response->context.transaction_id, now);
        if (!(s_cold_seen[axis] & 2U)) cold->original_fault = value;
        cold->readback_fault = value;
        state->snapshot.fault_code = value; state->snapshot.fault = value != 0U;
        state->fault_tick = now; state->fault_valid = state->snapshot.fault_valid = 1U;
        s_cold_seen[axis] |= 2U; s_cold_post[axis] |= 2U;
        if (value && value != AIMTOR_FAULT_MODBUS_TIMEOUT) {
            aimtor_cold_fail(SERVO_COLD_FAIL_OTHER_FAULT, 0U); return;
        }
        if (value && s_cold_phase == SERVO_COLD_CLEAR_VERIFYING) {
            aimtor_cold_fail(cold->attempted ? SERVO_COLD_FAIL_CLEAR : SERVO_COLD_FAIL_REAPPEARED, 0U);
            return;
        }
        if (value && cold->original_fault == 0U) {
            aimtor_cold_fail(SERVO_COLD_FAIL_REAPPEARED, 0U); return;
        }
    } else {
        if (value > 1U) { aimtor_cold_fail(SERVO_COLD_FAIL_FEEDBACK, 0U); return; }
        aimtor_feedback_valid(axis, 2U, response->context.transaction_id, now);
        state->snapshot.enable_feedback_valid = 1U;
        state->snapshot.enable_feedback = (uint8_t)value;
        state->snapshot.enable_feedback_tick = now;
        s_cold_seen[axis] |= 4U; s_cold_post[axis] |= 4U;
        if (!value) state->snapshot.disable_verified = 1U;
        if (value && s_cold_phase == SERVO_COLD_CLEAR_VERIFYING)
            aimtor_cold_fail(SERVO_COLD_FAIL_DISABLE, 0U);
    }
}

static void aimtor_cold_clear_service(TickType_t now)
{
    uint8_t axis, all_online = 1U, complete = 1U;
    aimtor_step_t step;
    if (s_cold_phase == SERVO_COLD_CLEAR_FAILED) return;
    if (!s_cold_started) { s_cold_started = 1U; s_cold_start = now; }
    if (!modbus_business_allowed() || app_health_watchdog_test_requested()) {
        aimtor_cold_fail(SERVO_COLD_FAIL_SAFETY, 0U); return;
    }
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) if (s_cold[axis].eligible) {
        if (s_cold_seen[axis] != 7U) all_online = 0U;
    }
    if (all_online && !s_cold_online) { s_cold_online = 1U; s_cold_online_tick = now; }
    if ((!s_cold_online && aimtor_cold_ms(now - s_cold_start) >= APP_COLD_CLEAR_ONLINE_TIMEOUT_MS) ||
        (s_cold_online && aimtor_cold_ms(now - s_cold_online_tick) >= APP_COLD_CLEAR_DEADLINE_MS)) {
        aimtor_cold_fail(s_cold_online ? SERVO_COLD_FAIL_DEADLINE : SERVO_COLD_FAIL_ONLINE_TIMEOUT, 1U); return;
    }
    if (s_cold_online) for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) if (s_cold[axis].eligible &&
        !aimtor_cold_fresh(axis, now)) { aimtor_cold_fail(SERVO_COLD_FAIL_FEEDBACK, 1U); return; }
    if (s_pending_id) return;
    if (s_cold_phase == SERVO_COLD_CLEAR_OBSERVING && aimtor_cold_all_stopped(now, 0U))
        aimtor_cold_phase(SERVO_COLD_CLEAR_DISABLING);
    if (s_cold_phase == SERVO_COLD_CLEAR_DISABLING && aimtor_cold_all_stopped(now, 1U))
        aimtor_cold_phase(SERVO_COLD_CLEAR_CLEARING);
    if (s_cold_phase == SERVO_COLD_CLEAR_CLEARING) {
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) if (s_cold[axis].eligible &&
            s_axes[axis].snapshot.fault_code && !s_cold[axis].attempted) complete = 0U;
        if (complete) {
            aimtor_cold_phase(SERVO_COLD_CLEAR_VERIFYING);
            memset(s_cold_post, 0, sizeof(s_cold_post));
            for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) s_cold[axis].zero_samples = 0U;
        }
    }
    if (s_cold_phase == SERVO_COLD_CLEAR_VERIFYING) {
        complete = aimtor_cold_all_stopped(now, 1U);
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) if (s_cold[axis].eligible &&
            (s_cold_post[axis] != 7U || s_axes[axis].snapshot.fault_code)) complete = 0U;
        if (complete) { aimtor_cold_phase(SERVO_COLD_CLEAR_DONE); return; }
    }
    axis = s_cold_turn;
    if (!s_cold[axis].eligible) axis = (uint8_t)(1U - axis);
    memset(&step, 0, sizeof(step)); step.count = 1U;
    if (s_cold_phase == SERVO_COLD_CLEAR_DISABLING && s_axes[axis].snapshot.enable_feedback &&
        !s_cold[axis].disable_attempted && aimtor_cold_all_stopped(now, 0U)) {
        step.address = AIMTOR_REG_SERVO_ENABLE; step.write = 1U;
    } else if (s_cold_phase == SERVO_COLD_CLEAR_CLEARING &&
        s_axes[axis].snapshot.fault_code == AIMTOR_FAULT_MODBUS_TIMEOUT && !s_cold[axis].attempted &&
        aimtor_cold_all_stopped(now, 1U)) {
        step.address = AIMTOR_REG_FAULT_RESET; step.words[0] = 1U; step.write = 1U;
    } else {
        static const uint16_t addresses[] = { AIMTOR_REG_ACTUAL_SPEED, AIMTOR_REG_FAULT_CODE, AIMTOR_REG_SERVO_ENABLE };
        step.address = addresses[s_cold_read_step[axis]];
    }
    if (aimtor_submit_step((servo_axis_t)axis, AIMTOR_TAG_COLD_CLEAR, &step, !step.write) == MODBUS_OK) {
        s_cold_pending_address = step.address; s_cold_pending_write = step.write;
        s_cold_pending_value = step.words[0];
        if (!step.write) s_cold_read_step[axis] = (uint8_t)((s_cold_read_step[axis] + 1U) % 3U);
        else if (step.address == AIMTOR_REG_FAULT_RESET) s_cold[axis].attempted = 1U;
        else s_cold[axis].disable_attempted = 1U;
        s_cold_turn = (uint8_t)(1U - axis);
    }
}

void aimtor_handle_modbus_response(void)
{
    modbus_response_t response;
    aimtor_axis_state_t *state;
    aimtor_step_t *step;
    uint8_t index;
    servo_axis_t axis;
    uint16_t value;

    while (modbus_get_response(&response, 0U))
    {
        uint8_t response_matched=1U;
        taskENTER_CRITICAL();
#if APP_LINK_DIAG_ACTIVE
        s_diag_response_id = 0U;
#endif
        if (!s_pending_id || response.context.transaction_id != s_pending_id)
        {
            modbus_diag_consumed(&response,0U);
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.context.owner != AIMTOR_OWNER_ID ||
            response.context.axis != s_pending_axis || response.context.tag != s_pending_tag)
        {
            modbus_diag_consumed(&response,0U);
            response_matched=0U;
            response.context.owner = AIMTOR_OWNER_ID;
            response.context.axis = (uint8_t)s_pending_axis;
            response.context.tag = s_pending_tag;
            response.status = MODBUS_FRAME_ERROR;
        }
#if APP_LINK_DIAG_ACTIVE
        s_diag_response_id = response.context.transaction_id;
#endif
        s_pending_id = 0U;
        if (s_pending_cancelled)
        {
            modbus_diag_consumed(&response,0U);
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        axis = (servo_axis_t)response.context.axis;
        state = &s_axes[axis];
        modbus_diag_consumed(&response,response_matched);
        if (response.context.tag == AIMTOR_TAG_COLD_CLEAR)
        {
            aimtor_cold_clear_response(&response);
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.context.tag == AIMTOR_TAG_SAFETY_WRITE)
        {
            if (response.status == MODBUS_OK &&
                response.operation == MODBUS_OPERATION_WRITE_SINGLE &&
                response.address == AIMTOR_REG_SPEED_COMMAND)
            {
                s_controlled_safety_command[axis] = s_safety_pending_rpm;
                s_last_velocity[axis] = s_safety_pending_rpm;
                s_safety_status.write_failures[axis] = 0U;
                if (s_safety_pending_rpm == 0 && !s_safety_zero_written[axis])
                {
                    s_safety_zero_written[axis] = 1U;
                    s_safety_zero_tick[axis] = xTaskGetTickCount();
                    s_safety_speed_sequence[axis] = state->speed_sequence;
                }
            }
            else if (s_safety_status.write_failures[axis] < 255U)
                s_safety_status.write_failures[axis]++;
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (s_can_pending.valid && s_can_pending.transaction == response.context.transaction_id)
        {
            if (s_can_pending.axis != axis || s_can_pending.batch != s_can_action_batch[axis] ||
                s_can_pending.sequence != state->speed_write_sequence)
            {
                /* 迟到应答属于历史事务，不能推进当前批次。 */
                s_can_pending.valid = 0U; AIMTOR_RESPONSE_EXIT_CRITICAL(); continue;
            }
            if (!s_can_pending.stop && s_can_pending.generation != s_can_generation)
            {
                if (response.status == MODBUS_OK && response.address == AIMTOR_REG_SPEED_COMMAND &&
                    response.operation != MODBUS_OPERATION_READ_HOLDING)
                    s_last_velocity[axis] = (int16_t)state->steps[0].words[0];
                state->snapshot.speed_write_completed = s_can_pending.sequence;
                state->snapshot.speed_write_result = response.status == MODBUS_OK ? SERVO_OK : SERVO_NOT_READY;
                state->snapshot.busy = 0U; state->action = ACTION_NONE; s_can_action[axis] = 0U;
                s_can_pending.valid = 0U; AIMTOR_RESPONSE_EXIT_CRITICAL(); continue;
            }
            s_can_pending.valid = 0U;
        }
        if (response.context.tag == AIMTOR_TAG_HOLD_ENABLE)
        {
            uint8_t runtime_stop = !aimtor_k1_hold_active() && state->snapshot.enabled &&
                (s_motion_authorized || s_hmi_authorized || s_can_authorized ||
                 s_cli_authorized || state->snapshot.actual_speed_rpm != 0 ||
                 s_last_velocity[axis] != 0);
            aimtor_poll_record((uint8_t)axis, AIMTOR_TAG_HOLD_ENABLE,
                response.status == MODBUS_OK && response.address == AIMTOR_REG_SERVO_ENABLE &&
                response.operation == MODBUS_OPERATION_READ_HOLDING && response.quantity == 1U &&
                response.data_length == 3U && response.data[0] == 2U ? MODBUS_OK : MODBUS_FRAME_ERROR);
            s_hold_enable_poll[axis] = aimtor_deadline_after(
                xTaskGetTickCount(), 100U);
            if (response.status == MODBUS_OK &&
                response.address == AIMTOR_REG_SERVO_ENABLE &&
                response.operation == MODBUS_OPERATION_READ_HOLDING &&
                response.quantity == 1U && response.data_length == 3U &&
                response.data[0] == 2U)
            {
                value = aimtor_read_u16(response.data, 0U);
                s_hold_enable_tick[axis] = xTaskGetTickCount();
                aimtor_feedback_valid(axis, 2U, response.context.transaction_id, xTaskGetTickCount());
                state->snapshot.enable_feedback_valid = 1U;
                state->snapshot.enable_feedback = value != 0U;
                state->snapshot.enable_feedback_tick = xTaskGetTickCount();
                if (value == 0U && !state->snapshot.enabled && !state->snapshot.busy &&
                    !state->disable_pending && state->snapshot.last_result == SERVO_OK)
                    state->snapshot.disable_verified = 1U;
                /* 申请前在途的旧使能读应答不能证明本轮K5保持（内部保留K1名称）。 */
                if (!aimtor_k1_hold_active() ||
                    (int32_t)(response.context.transaction_id - s_k1_enable_floor) > 0)
                    s_hold_enable_confirmed[axis] = value == 1U ? 1U : 0U;
                if ((runtime_stop || (s_hold[axis].state == SERVO_STARTUP_HOLD_READY &&
                     !aimtor_cli_axis_owned(axis)) ||
                     (aimtor_k1_hold_active() && (s_k1_hold.axis_mask & (1U << axis)))) &&
                    value != 1U)
                {
                    if (aimtor_k1_hold_active()) s_k1_hold.failure = 2U;
                    if (runtime_stop)
                    {
                        s_recovery_disqualified = 1U;
                        s_feedback_stop_pending |= (uint8_t)(1U << axis);
                    }
                    else s_emergency_pending = 1U;
                }
            }
            else
            {
                if (s_feedback_transaction[axis][2] == response.context.transaction_id)
                { AIMTOR_RESPONSE_EXIT_CRITICAL(); continue; }
                if (!aimtor_degraded())
                { memcpy(s_degraded_limit, s_last_velocity, sizeof(s_degraded_limit)); s_degraded_active = 1U; }
                s_feedback_transaction[axis][2] = response.context.transaction_id;
                s_feedback_result[axis][2] = response.status == MODBUS_OK ? MODBUS_FRAME_ERROR : response.status;
                if (!s_enable_feedback_failures[axis])
                { s_feedback_failure_tick[axis][2] = xTaskGetTickCount();
                  s_feedback_first_transaction[axis][2] = response.context.transaction_id;
                  s_feedback_first_result[axis][2] = s_feedback_result[axis][2]; }
                s_recovery_observing = 0U;
                memset(s_recovery_samples, 0, sizeof(s_recovery_samples));
                memcpy(s_recovery_floor, s_feedback_sequence, sizeof(s_recovery_floor));
                s_recovery.healthy_ms = 0U;
                if (s_recovery.state == SERVO_RECOVERY_READY) s_recovery.state = SERVO_RECOVERY_WAIT_HEALTH;
                if (s_recovery.state == SERVO_RECOVERY_PREPARING || s_recovery.state == SERVO_RECOVERY_COMMIT)
                {
                    s_recovery.state = SERVO_RECOVERY_FAILED;
                    s_recovery.eligible = 0U; s_recovery.failure = 2U;
                    s_emergency_pending = 1U;
                }
                if (s_enable_feedback_failures[axis] < 255U) s_enable_feedback_failures[axis]++;
                if (runtime_stop)
                {
                    s_hold_enable_poll[axis] = aimtor_deadline_after(xTaskGetTickCount(), 20U);
                    if (s_enable_feedback_failures[axis] >= APP_SERVO_RUNTIME_COMM_FAILURES ||
                        (state->snapshot.enable_feedback_valid &&
                         aimtor_tick_age_ms(xTaskGetTickCount(), s_hold_enable_tick[axis]) >=
                             APP_SERVO_STATUS_MAX_AGE_MS))
                        s_feedback_stop_pending |= (uint8_t)(1U << axis);
                }
                else if ((s_hold[axis].state == SERVO_STARTUP_HOLD_READY &&
                         !aimtor_cli_axis_owned(axis)) ||
                         (aimtor_k1_hold_active() && (s_k1_hold.axis_mask & (1U << axis))))
                {
                    if (aimtor_k1_hold_active()) s_k1_hold.failure = 2U;
                    state->snapshot.enable_feedback_valid = 0U;
                    s_emergency_pending = 1U;
                }
            }
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.context.tag == AIMTOR_TAG_WDG_OFF)
        {
            if (response.status == MODBUS_OK &&
                response.operation == MODBUS_OPERATION_WRITE_SINGLE &&
                response.address == AIMTOR_REG_SERVO_ENABLE && response.value == 0U)
            {
                state->disable_pending = 0U;
                s_wdg_recovery[axis].disable_ack = 1U;
                state->snapshot.enabled = 0U;
                s_wdg_manual_attempts[axis] = 0U;
            }
            s_wdg_retry[axis] = aimtor_deadline_after(
                xTaskGetTickCount(), APP_WDG_RECOVERY_RETRY_INTERVAL_MS);
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.context.tag == AIMTOR_TAG_STARTUP_OFF)
        {
            if (response.status == MODBUS_OK &&
                response.address == AIMTOR_REG_SERVO_ENABLE &&
                response.operation == MODBUS_OPERATION_READ_HOLDING &&
                response.quantity == 1U && response.data_length == 3U &&
                aimtor_read_u16(response.data, 0U) == 0U)
            {
                s_startup_stop[axis].off_confirmed = 1U;
                s_startup_speed_sequence[axis] = state->speed_sequence;
                state->snapshot.enabled = 0U;
                state->snapshot.disable_verified = 1U;
            }
            s_startup_retry[axis] =
                aimtor_deadline_after(xTaskGetTickCount(), 100U);
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.context.tag == AIMTOR_TAG_WDG_RAMP ||
            response.context.tag == AIMTOR_TAG_WDG_ZERO)
        {
            servo_wdg_recovery_status_t *recovery = &s_wdg_recovery[axis];
            TickType_t now = xTaskGetTickCount();
            uint8_t read = recovery->state == SERVO_WDG_RECOVERY_VERIFY_DECEL;
            uint16_t address = response.context.tag == AIMTOR_TAG_WDG_ZERO ?
                AIMTOR_REG_SPEED_COMMAND : AIMTOR_REG_DECELERATION_TIME;
            if (recovery->state == SERVO_WDG_RECOVERY_FAILED)
            {
                AIMTOR_RESPONSE_EXIT_CRITICAL();
                continue;
            }
            if (response.status == MODBUS_OK && response.address == address &&
                ((read && response.operation == MODBUS_OPERATION_READ_HOLDING &&
                  response.quantity == 1U && response.data_length == 3U) ||
                 (!read && response.operation == MODBUS_OPERATION_WRITE_SINGLE &&
                  response.value == (address == AIMTOR_REG_SPEED_COMMAND ?
                                     0U : APP_WDG_RECOVERY_DECEL_MS))))
            {
                s_wdg_step_attempts[axis] = 0U;
                s_wdg_retry[axis] = now;
                if (read)
                {
                    recovery->decel_readback_ms = aimtor_read_u16(response.data, 0U);
                    if (recovery->decel_readback_ms != APP_WDG_RECOVERY_DECEL_MS)
                        aimtor_watchdog_recovery_fail(axis, 5U);
                    else
                    {
                        recovery->ramp_verified = 1U;
                        recovery->state = SERVO_WDG_RECOVERY_WRITE_ZERO;
                    }
                }
                else if (address == AIMTOR_REG_DECELERATION_TIME)
                    recovery->state = SERVO_WDG_RECOVERY_VERIFY_DECEL;
                else
                {
                    /* 零目标应答只证明指令生效，实际速度仍由新反馈确认。 */
                    recovery->zero_written = 1U;
                    recovery->state = SERVO_WDG_RECOVERY_DECELERATING;
                    recovery->zero_samples = 0U;
                    recovery->stop_timeout_ms = aimtor_watchdog_stop_timeout_ms(
                        state->snapshot.actual_speed_rpm, recovery->command_readback_rpm);
                    s_wdg_stop_tick[axis] = now;
                    s_wdg_deadline[axis] = aimtor_deadline_after(
                        now, recovery->stop_timeout_ms);
                    s_wdg_poll_phase[axis] = 0U;
                    s_last_velocity[axis] = 0;
                }
            }
            else
                s_wdg_retry[axis] = aimtor_deadline_after(
                    now, APP_WDG_RECOVERY_RETRY_INTERVAL_MS);
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.context.tag == AIMTOR_TAG_WDG_VERIFY)
        {
            servo_wdg_recovery_status_t *recovery = &s_wdg_recovery[axis];
            uint8_t phase = s_wdg_poll_phase[axis];
            uint8_t initial = recovery->state == SERVO_WDG_RECOVERY_WAIT_ONLINE;
            uint16_t address = s_wdg_read_addresses[phase];
            TickType_t now = xTaskGetTickCount();
            if (recovery->state == SERVO_WDG_RECOVERY_FAILED)
            {
                AIMTOR_RESPONSE_EXIT_CRITICAL();
                continue;
            }
            if (response.status == MODBUS_OK &&
                response.address == address &&
                response.operation == MODBUS_OPERATION_READ_HOLDING &&
                response.quantity == 1U && response.data_length == 3U &&
                response.data[0] == 2U)
            {
                value = aimtor_read_u16(response.data, 0U);
                if (phase == 0U)
                {
                    aimtor_diag_speed(&response,state);
                    state->snapshot.actual_speed_rpm = (int16_t)value;
                    state->snapshot.actual_velocity_mps = (float)(int16_t)value *
                        APP_WHEEL_CIRCUMFERENCE_M / (60.0f * APP_GEAR_REDUCTION_RATIO);
                    state->speed_tick = xTaskGetTickCount();
                    state->speed_valid = state->snapshot.speed_valid = 1U;
                    state->speed_sequence++;
                    aimtor_record_speed_sample(state);
                    if (!initial && recovery->state != SERVO_WDG_RECOVERY_VERIFY)
                    {
                        recovery->stop_elapsed_ms = (uint32_t)(
                            (uint64_t)(now - s_wdg_stop_tick[axis]) * 1000U /
                            configTICK_RATE_HZ);
                        if (value != 0U)
                        {
                            recovery->zero_samples = 0U;
                            recovery->state = SERVO_WDG_RECOVERY_DECELERATING;
                        }
                        else if (!recovery->zero_samples)
                        {
                            recovery->zero_samples = 1U;
                            s_wdg_zero_tick[axis] = now;
                            recovery->state = SERVO_WDG_RECOVERY_CONFIRM_ZERO;
                        }
                        else if (now - s_wdg_zero_tick[axis] >= pdMS_TO_TICKS(20U))
                            recovery->zero_samples = 2U;
                    }
                }
                else if (phase == 1U)
                {
                    state->snapshot.fault_code = value;
                    state->snapshot.fault = value != 0U;
                    state->fault_tick = xTaskGetTickCount();
                    state->fault_valid = state->snapshot.fault_valid = 1U;
                    if (value != 0U) aimtor_watchdog_recovery_fail(axis, 2U);
                }
                else if (phase == 2U)
                {
                    state->snapshot.enabled = value != 0U;
                    state->snapshot.enable_feedback_valid = 1U;
                    state->snapshot.enable_feedback = value != 0U;
                    state->snapshot.enable_feedback_tick = xTaskGetTickCount();
                    recovery->enable_readback = value;
                    if (!initial &&
                        recovery->state == SERVO_WDG_RECOVERY_VERIFY &&
                        value == 0U && aimtor_fresh(state) &&
                        state->snapshot.actual_speed_rpm == 0)
                    {
                        state->snapshot.disable_verified = 1U;
                        recovery->stopped = 1U;
                        recovery->verified = 1U;
                        recovery->state = SERVO_WDG_RECOVERY_DONE;
                    }
                    else if (!initial &&
                        (recovery->state == SERVO_WDG_RECOVERY_DECELERATING ||
                         recovery->state == SERVO_WDG_RECOVERY_CONFIRM_ZERO))
                    {
                        if (value != 1U) aimtor_watchdog_recovery_fail(axis, 3U);
                        else if (recovery->zero_samples >= 2U && aimtor_fresh(state))
                        {
                            recovery->stopped = 1U;
                            recovery->state = SERVO_WDG_RECOVERY_PREPARE_SPEED;
                        }
                    }
                }
                else if (phase == 3U) recovery->mode_readback = value;
                else if (phase == 4U) recovery->selection_readback = value;
                else if (phase == 5U) recovery->source_readback = value;
                else
                {
                    recovery->command_readback_rpm = (int16_t)value;
                    recovery->start_speed_rpm = state->snapshot.actual_speed_rpm;
                    if (!aimtor_fresh(state))
                        aimtor_watchdog_recovery_fail(axis, 6U);
                    else if (!APP_SERVO_SOFT_ENABLE_CONFIRMED ||
                             !aimtor_register_map_valid() ||
                             !app_config_servo_bus_ready())
                        aimtor_watchdog_recovery_fail(axis, 3U);
                    else if (state->snapshot.actual_speed_rpm == 0 ||
                             recovery->enable_readback == 0U)
                    {
                        recovery->stopped = state->snapshot.actual_speed_rpm == 0;
                        recovery->state = SERVO_WDG_RECOVERY_PREPARE_SPEED;
                    }
                    else if (recovery->enable_readback == 1U &&
                             recovery->mode_readback == AIMTOR_CONTROL_MODE_SPEED &&
                             recovery->selection_readback == 0U &&
                             recovery->source_readback == 0U)
                    {
                        recovery->ramp_applicable = 1U;
                        recovery->state = SERVO_WDG_RECOVERY_WRITE_DECEL;
                        s_wdg_step_attempts[axis] = 0U;
                    }
                    else aimtor_watchdog_recovery_fail(axis, 3U);
                }
                s_wdg_poll_phase[axis] = (uint8_t)((phase + 1U) %
                    (initial ? 7U : 3U));
                state->snapshot.consecutive_failures = 0U;
            }
            else
            {
                s_wdg_poll_phase[axis] = 0U;
                s_wdg_retry[axis] = aimtor_deadline_after(
                    xTaskGetTickCount(), APP_WDG_RECOVERY_RETRY_INTERVAL_MS);
            }
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.context.tag == AIMTOR_TAG_SEQUENCE &&
            state->action == ACTION_WDG_RECOVERY)
        {
            step = &state->steps[state->index];
            if (response.status == MODBUS_OK &&
                response.operation == MODBUS_OPERATION_WRITE_SINGLE &&
                response.address == step->address &&
                response.value == step->words[0])
            {
                if (step->address == AIMTOR_REG_SPEED_COMMAND)
                    s_wdg_recovery[axis].zero_written = 1U;
                if (state->index == 0U)
                {
                    state->snapshot.enabled = 0U;
                    if (state->snapshot.fault)
                    {
                        s_wdg_recovery[axis].disable_ack = 1U;
                        aimtor_watchdog_recovery_fail(axis, 2U);
                        AIMTOR_RESPONSE_EXIT_CRITICAL();
                        continue;
                    }
                }
                state->index++;
                s_wdg_step_attempts[axis] = 0U;
                if (state->index >= state->count)
                {
                    aimtor_finish(axis);
                }
                else if (state->index == state->count - 1U)
                    s_wdg_recovery[axis].state = SERVO_WDG_RECOVERY_DISABLE;
            }
            else
            {
                state->snapshot.last_result = SERVO_NOT_READY;
                if (s_wdg_step_attempts[axis] >= APP_WDG_RECOVERY_RETRY_MAX ||
                    (step->address == AIMTOR_REG_SERVO_ENABLE &&
                     s_wdg_recovery[axis].attempts >= APP_WDG_RECOVERY_RETRY_MAX))
                {
                    aimtor_watchdog_recovery_fail(axis, 4U);
                }
                else
                {
                    s_wdg_retry[axis] = aimtor_deadline_after(
                        xTaskGetTickCount(),
                        APP_WDG_RECOVERY_RETRY_INTERVAL_MS);
                }
            }
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.context.tag == AIMTOR_TAG_SEQUENCE &&
            state->action == ACTION_STARTUP_DISABLE)
        {
            servo_startup_stop_status_t *startup = &s_startup_stop[axis];
            if (startup->attempts >= 3U &&
                !(response.status == MODBUS_OK &&
                  response.operation == MODBUS_OPERATION_WRITE_SINGLE &&
                  response.address == AIMTOR_REG_SERVO_ENABLE && response.value == 0U))
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP,
                    (uint8_t)axis, 0x0101U, response.status);
            state->snapshot.busy = 0U;
            state->action = ACTION_NONE;
            if (response.status == MODBUS_OK &&
                response.operation == MODBUS_OPERATION_WRITE_SINGLE &&
                response.address == AIMTOR_REG_SERVO_ENABLE &&
                response.value == 0U)
            {
                startup->disable_ack = 1U;
                startup->state = SERVO_STARTUP_STOP_WAITING_ZERO;
            }
            else if (startup->attempts >= 3U)
            {
                startup->state = SERVO_STARTUP_STOP_FAILED;
                startup->failure = 1U;
            }
            s_startup_retry[axis] =
                aimtor_deadline_after(xTaskGetTickCount(), 100U);
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.context.tag == AIMTOR_TAG_SEQUENCE &&
            state->action == ACTION_BOOT_ZERO)
        {
            servo_boot_zero_status_t *boot = &s_boot_zero[axis];
            if (boot->attempts >= 3U &&
                !(response.status == MODBUS_OK &&
                  response.operation != MODBUS_OPERATION_READ_HOLDING &&
                  response.address == AIMTOR_REG_HOME_METHOD))
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP,
                    (uint8_t)axis, 0x0202U,
                    ((uint32_t)AIMTOR_REG_HOME_METHOD << 16U) | (uint32_t)response.status);
            state->snapshot.busy = 0U;
            state->action = ACTION_NONE;
            /* Modbus协议层已核验功能码和值回显，此处再次匹配寄存器。 */
            if (response.status == MODBUS_OK &&
                response.operation != MODBUS_OPERATION_READ_HOLDING &&
                response.address == AIMTOR_REG_HOME_METHOD)
            {
                boot->write_ack = 1U;
                boot->state = SERVO_BOOT_ZERO_VERIFYING;
                s_boot_position_sequence[axis] = state->position_sequence;
                s_boot_deadline[axis] =
                    aimtor_deadline_after(xTaskGetTickCount(), 1000U);
                state->snapshot.last_result = SERVO_OK;
                (void)aimtor_request_position(axis);
            }
            else
            {
                state->snapshot.last_result = SERVO_NOT_READY;
                s_boot_retry[axis] =
                    aimtor_deadline_after(xTaskGetTickCount(), 100U);
                if (boot->attempts >= 3U)
                {
                    boot->state = SERVO_BOOT_ZERO_FAILED;
                    boot->failure = 2U;
                }
            }
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.status != MODBUS_OK)
        {
            if (response.context.tag == AIMTOR_TAG_SEQUENCE &&
                aimtor_startup_feedback_active())
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP,
                    (uint8_t)axis, 0x0501U, response.status);
            if (response.context.tag != AIMTOR_TAG_SEQUENCE)
            {
                aimtor_poll_failure(state, response.context.tag, response.status,
                                    response.context.transaction_id,
                                    xTaskGetTickCount());
            }
            else
            {
                if (state->action == ACTION_DISABLE)
                {
                    /* 失能失败不能无限生成新的失能序列。 */
                    state->snapshot.busy = 0U;
                    state->snapshot.last_result = SERVO_NOT_READY;
                    state->disable_pending = 0U;
                    state->action = ACTION_NONE;
                }
                else
                {
                    aimtor_fail(axis);
                }
            }
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (aimtor_is_diagnostic_tag(response.context.tag))
        {
            uint8_t diagnostic_index = aimtor_diagnostic_index(response.context.tag);
            uint8_t diagnostic_mask = (uint8_t)(1U << diagnostic_index);
            int32_t diagnostic_value;
            TickType_t diagnostic_now = xTaskGetTickCount();
            if (response.operation != MODBUS_OPERATION_READ_HOLDING ||
                response.address != aimtor_diagnostic_address(response.context.tag) ||
                response.quantity != aimtor_diagnostic_count(response.context.tag) ||
                response.data_length != (uint16_t)(1U + response.quantity * 2U))
            {
                aimtor_poll_failure(state, response.context.tag, MODBUS_FRAME_ERROR,
                                    response.context.transaction_id,
                                    diagnostic_now);
                AIMTOR_RESPONSE_EXIT_CRITICAL();
                continue;
            }
            value = aimtor_read_u16(response.data, 0U);
            diagnostic_value = response.context.tag == AIMTOR_TAG_DIAG_CURRENT ?
                aimtor_read_i32(response.data) :
                (response.context.tag == AIMTOR_TAG_DIAG_TORQUE ||
                 response.context.tag == AIMTOR_TAG_DIAG_LOAD ?
                 (value <= 32767U ? (int32_t)value : (int32_t)value - 65536L) :
                 (int32_t)value);
            if ((diagnostic_index == 0U &&
                 (diagnostic_value < -3000 || diagnostic_value > 3000)) ||
                (diagnostic_index == 1U &&
                 (diagnostic_value < 0 || diagnostic_value > 5000)) ||
                (diagnostic_index == 2U &&
                 (diagnostic_value < -120000L || diagnostic_value > 120000L)) ||
                (diagnostic_index == 3U && diagnostic_value > 9999))
            {
                aimtor_poll_failure(state, response.context.tag, MODBUS_FRAME_ERROR,
                                    response.context.transaction_id,
                                    diagnostic_now);
                AIMTOR_RESPONSE_EXIT_CRITICAL();
                continue;
            }
            if (diagnostic_index == 0U)
                state->snapshot.realtime_torque_tenths_percent = (int16_t)diagnostic_value;
            else if (diagnostic_index == 1U)
                state->snapshot.load_ratio_tenths_percent = (uint16_t)diagnostic_value;
            else if (diagnostic_index == 2U)
                state->snapshot.phase_current_deciamp = aimtor_round_deciamp(diagnostic_value);
            else
                state->snapshot.bus_voltage_decivolt = (uint16_t)diagnostic_value;
            state->snapshot.diagnostic_valid_mask |= diagnostic_mask;
            state->snapshot.diagnostic_stale_mask &= (uint8_t)~diagnostic_mask;
            state->snapshot.diagnostic_sample_tick[diagnostic_index] = diagnostic_now;
            state->snapshot.diagnostic_update_tick = diagnostic_now;
            aimtor_poll_success(state, response.context.tag, diagnostic_now);
            /* 诊断成功不能清除安全反馈的连续失败或刷新关键反馈年龄。 */
            AIMTOR_RESPONSE_EXIT_CRITICAL();
            continue;
        }
        if (response.context.tag != AIMTOR_TAG_SEQUENCE &&
            (response.operation != MODBUS_OPERATION_READ_HOLDING ||
             response.address != aimtor_poll_address(response.context.tag) ||
             response.quantity != aimtor_poll_count(response.context.tag) ||
             response.data_length != 1U + response.quantity * 2U ||
             response.data[0] != response.quantity * 2U))
        {
            aimtor_poll_failure(state, response.context.tag, MODBUS_FRAME_ERROR,
                response.context.transaction_id, xTaskGetTickCount());
            AIMTOR_RESPONSE_EXIT_CRITICAL(); continue;
        }
        state->snapshot.consecutive_failures = 0U;
        state->snapshot.update_tick = xTaskGetTickCount();
        if (response.context.tag == AIMTOR_TAG_SEQUENCE)
        {
            if (s_can_action[axis] && !s_can_action_stop[axis] &&
                s_can_action_generation[axis] != s_can_generation)
            {
                /* 在途旧写仅归档执行事实，不再发旧给定回读或后续写。 */
                if (response.operation != MODBUS_OPERATION_READ_HOLDING && response.status == MODBUS_OK)
                    s_last_velocity[axis] = (int16_t)state->steps[0].words[0];
                state->snapshot.speed_write_completed = state->speed_write_sequence;
                state->snapshot.speed_write_result = response.status == MODBUS_OK ? SERVO_OK : SERVO_NOT_READY;
                state->snapshot.busy = 0U; state->action = ACTION_NONE; s_can_action[axis] = 0U;
                AIMTOR_RESPONSE_EXIT_CRITICAL(); continue;
            }
            step = &state->steps[state->index];
            /* 除协议匹配外再核验上下文，错误响应不得推进配置序列。 */
            if (response.address != step->address ||
                (response.operation == MODBUS_OPERATION_READ_HOLDING &&
                 (response.quantity != step->count ||
                  response.data_length != 1U + step->count * 2U)))
            {
                if (aimtor_startup_feedback_active())
                    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP,
                        (uint8_t)axis, 0x0501U, MODBUS_FRAME_ERROR);
                aimtor_fail(axis);
                AIMTOR_RESPONSE_EXIT_CRITICAL();
                continue;
            }
            if ((response.operation != MODBUS_OPERATION_READ_HOLDING) &&
                step->compare)
            {
                state->reading_back = 1U;
            }
            else
            {
                if (response.operation == MODBUS_OPERATION_READ_HOLDING &&
                    step->address == AIMTOR_REG_SERVO_ENABLE && step->count == 1U)
                {
                    aimtor_feedback_valid(axis, 2U, response.context.transaction_id, xTaskGetTickCount());
                    state->snapshot.enable_feedback_valid = 1U;
                    state->snapshot.enable_feedback = aimtor_read_u16(response.data, 0U) != 0U;
                    state->snapshot.enable_feedback_tick = xTaskGetTickCount();
                }
                if (response.operation == MODBUS_OPERATION_READ_HOLDING &&
                    step->count == 1U &&
                    (step->address == AIMTOR_REG_ACCELERATION_TIME ||
                     step->address == AIMTOR_REG_DECELERATION_TIME))
                {
                    uint8_t ramp_index = step->address == AIMTOR_REG_ACCELERATION_TIME ? 0U : 1U;
                    uint8_t bit = (uint8_t)(1U << ramp_index);
                    state->snapshot.ramp_readback_ms[ramp_index] = aimtor_read_u16(response.data, 0U);
                    state->snapshot.ramp_readback_tick[ramp_index] = (uint32_t)((uint64_t)xTaskGetTickCount() * 1000U / configTICK_RATE_HZ);
                    state->snapshot.ramp_read_mask |= bit;
                    if (state->snapshot.ramp_readback_ms[ramp_index] == step->words[0])
                        state->snapshot.ramp_match_mask |= bit;
                    else state->snapshot.ramp_match_mask &= (uint8_t)~bit;
                }
                if (step->compare)
                {
                    for (index = 0U; index < step->count; index++)
                    {
                        if (aimtor_read_u16(response.data, index) != step->words[index])
                        {
                            break;
                        }
                    }
                    if (index != step->count)
                    {
                        if (aimtor_startup_feedback_active())
                            aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, (uint8_t)axis,
                                0x0502U, ((uint32_t)step->address << 16U) |
                                aimtor_read_u16(response.data, index));
                        if (state->action == ACTION_DISABLE)
                        {
                            state->snapshot.busy = 0U;
                            state->snapshot.last_result = SERVO_NOT_READY;
                            state->disable_pending = 0U;
                            state->action = ACTION_NONE;
                        }
                        else
                        {
                            aimtor_fail(axis);
                        }
                        AIMTOR_RESPONSE_EXIT_CRITICAL();
                        continue;
                    }
                }
                state->reading_back = 0U;
                state->index++;
                if (aimtor_startup_feedback_active()) s_startup_feedback_slot = 1U;
                if (state->index >= state->count)
                {
                    aimtor_finish(axis);
                }
            }
        }
        else
        {
            value = aimtor_read_u16(response.data, 0U);
            if (response.context.tag == AIMTOR_TAG_SPEED)
            {
                aimtor_diag_speed(&response,state);
                aimtor_feedback_valid(axis, 0U, response.context.transaction_id, xTaskGetTickCount());
                state->snapshot.actual_speed_rpm = (int16_t)value;
                state->snapshot.actual_velocity_mps = (float)(int16_t)value *
                    APP_WHEEL_CIRCUMFERENCE_M / (60.0f * APP_GEAR_REDUCTION_RATIO);
                state->speed_tick = xTaskGetTickCount();
                state->speed_sequence++;
                aimtor_record_speed_sample(state);
                state->speed_valid = 1U;
                state->snapshot.speed_valid = 1U;
                if (s_controlled_safety_active &&
                    state->speed_sequence != s_safety_speed_sequence[axis])
                {
                    s_safety_speed_sequence[axis] = state->speed_sequence;
                    if (state->snapshot.actual_speed_rpm == 0 &&
                        ((state->snapshot.control_mode == SERVO_CONTROL_RS485_VELOCITY &&
                          s_safety_zero_written[axis] &&
                          (int32_t)(state->speed_tick - s_safety_zero_tick[axis]) > 0) ||
                         (state->snapshot.control_mode == SERVO_CONTROL_PULSE_POSITION &&
                          motion_get_command_velocity(axis) == 0.0f)))
                    {
                        if (s_safety_zero_samples[axis] < 2U) s_safety_zero_samples[axis]++;
                    }
                    else s_safety_zero_samples[axis] = 0U;
                }
                if (state->segment_latched)
                {
                    if (state->snapshot.actual_speed_rpm != 0)
                    {
                        /* 观察到实际速度后，才允许把后续零速视为定位完成。 */
                        state->position_running = 1U;
                    }
                    else if (state->position_running)
                    {
                        /* 速度回到零，释放本次位置触发锁存，允许下一次定位。 */
                        state->segment_latched = 0U;
                        state->position_running = 0U;
                    }
                }
            }
            else if (response.context.tag == AIMTOR_TAG_FAULT)
            {
                aimtor_feedback_valid(axis, 1U, response.context.transaction_id, xTaskGetTickCount());
                state->snapshot.fault_code = value;
                state->snapshot.fault = value != 0U;
                state->fault_tick = xTaskGetTickCount();
                state->fault_valid = 1U;
                state->snapshot.fault_valid = 1U;
            }
            else if (response.context.tag == AIMTOR_TAG_POSITION)
            {
                servo_position_sample_t *sample;
                /* 配置核验前该值仅为原始诊断，不用于运动许可或定位。 */
                state->snapshot.actual_position = (int32_t)((uint32_t)value |
                    ((uint32_t)aimtor_read_u16(response.data, 1U) << 16U));
                state->position_tick = xTaskGetTickCount();
                state->position_sequence++;
                state->snapshot.position_valid = 1U;
                memmove(state->snapshot.position_samples,
                        state->snapshot.position_samples + 1,
                        (SERVO_POSITION_SAMPLE_CAPACITY - 1U) *
                            sizeof(servo_position_sample_t));
                sample = &state->snapshot.position_samples[
                    SERVO_POSITION_SAMPLE_CAPACITY - 1U];
                sample->actual_position = state->snapshot.actual_position;
                sample->position_sequence = state->position_sequence;
                sample->position_generation = state->snapshot.position_generation;
                sample->sample_tick = state->position_tick;
                sample->valid = 1U;
            }
            else
            {
                state->snapshot.status_word = value;
            }
            aimtor_poll_success(state, response.context.tag,
                                xTaskGetTickCount());
        }
        AIMTOR_RESPONSE_EXIT_CRITICAL();
    }
}

static void aimtor_startup_hold_fail_axis(uint8_t reason, uint8_t failed_axis)
{
    uint8_t axis;
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP, failed_axis,
                            (uint16_t)(0x0300U | reason), 0U);
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        if (s_hold[axis].eligible) s_hold[axis].failure = reason;
    aimtor_emergency_stop();
}

static void aimtor_startup_hold_fail(uint8_t reason)
{
    aimtor_startup_hold_fail_axis(reason, 0xFFU);
}

static void aimtor_k1_hold_service(TickType_t now)
{
    uint8_t axis;
    servo_k1_hold_status_t status;
    if (!aimtor_k1_hold_active()) return;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!(s_k1_hold.axis_mask & (1U << axis))) continue;
        if (!aimtor_k1_axis_safe((servo_axis_t)axis) ||
            app_health_watchdog_test_requested() ||
            !aimtor_boot_zero_ready() || !app_config_motion_hardware_ready())
        {
            s_k1_hold.failure = 1U;
            aimtor_emergency_stop();
            motion_report_fault(0U);
            return;
        }
    }
    (void)aimtor_k1_hold_get_status(&status);
    if (status.confirmed_mask == status.axis_mask)
        s_k1_hold.state = SERVO_K1_HOLD_READY;
    else if (s_k1_hold.state == SERVO_K1_HOLD_READY)
    {
        s_k1_hold.failure = 2U;
        aimtor_emergency_stop();
        motion_report_fault(1U);
        return;
    }
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        if ((s_k1_hold.axis_mask & (1U << axis)) &&
            s_axes[axis].snapshot.actual_speed_rpm != 0 &&
            !s_axes[axis].snapshot.busy && !s_pending_id &&
            aimtor_tick_due(now, s_hold_zero_retry[axis]))
        {
            s_hold_zero_retry[axis] = aimtor_deadline_after(now, 100U);
            aimtor_begin((servo_axis_t)axis, ACTION_HOLD_ZERO);
            aimtor_add((servo_axis_t)axis, AIMTOR_REG_SPEED_COMMAND, 0U, 1U, 1U, 0U);
            return;
        }
}

static void aimtor_startup_hold_service(TickType_t now)
{
    servo_axis_t axis;
    aimtor_axis_state_t *state;
    servo_startup_hold_status_t *hold;
    servo_result_t result;
    uint8_t preparing = 0U;

    if (aimtor_recovery_blocks_motion() && s_recovery.state != SERVO_RECOVERY_PREPARING &&
        s_recovery.state != SERVO_RECOVERY_COMMIT) return;
    if (!aimtor_startup_hold_active() || !aimtor_boot_zero_ready()) return;
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
        if (s_hold[axis].eligible && s_axes[axis].snapshot.fault)
            s_hold_k1_rearm = 0U;
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
        if (s_hold[axis].eligible &&
            s_hold[axis].state == SERVO_STARTUP_HOLD_FAILED)
        {
            /* 人工停止后任一轴新出现真实故障，都不能再重新使能。 */
            return;
        }
    if (!s_hold_deadline)
        s_hold_deadline = aimtor_deadline_after(now,
            APP_SERVO_STARTUP_HOLD_TIMEOUT_MS);
    if (!aimtor_startup_hold_ready() &&
        aimtor_tick_due(now, s_hold_deadline))
    {
        aimtor_startup_hold_fail(4U);
        return;
    }
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        hold = &s_hold[axis];
        if (!hold->eligible) continue;
        if (aimtor_cli_axis_owned(axis) && !s_cli_restore) continue;
        state = &s_axes[axis];
        if (hold->state == SERVO_STARTUP_HOLD_READY && state->snapshot.enabled &&
            !state->snapshot.fault && !aimtor_fresh(state))
        {
            s_recovery_candidate = 1U;
            aimtor_request_controlled_safety_stop(1U);
            return;
        }
        if (state->snapshot.fault || state->snapshot.stop_latched ||
            (hold->state == SERVO_STARTUP_HOLD_READY &&
             (!aimtor_fresh(state) || !state->snapshot.enabled)))
        {
            aimtor_startup_hold_fail_axis(4U, (uint8_t)axis);
            return;
        }
        if (!aimtor_k1_hold_active() && hold->state == SERVO_STARTUP_HOLD_READY &&
            !s_motion_authorized && !s_remote_control_mask && !s_can_mask && !s_hmi_mask)
        {
            hold->disturbed = state->snapshot.actual_speed_rpm != 0;
            /* 零速速度环自行抵抗扰动；补写零速不触发失能或重新使能。 */
            if (hold->disturbed && !state->snapshot.busy &&
                aimtor_tick_due(now, s_hold_zero_retry[axis]))
            {
                s_hold_zero_retry[axis] = aimtor_deadline_after(now, 100U);
                aimtor_begin(axis, ACTION_HOLD_ZERO);
                aimtor_add(axis, AIMTOR_REG_SPEED_COMMAND, 0U, 1U, 1U, 0U);
                return;
            }
        }
        if (hold->state == SERVO_STARTUP_HOLD_PREPARING &&
            !state->snapshot.busy)
        {
            if (state->snapshot.last_result != SERVO_OK)
            {
                aimtor_startup_hold_fail_axis(1U, (uint8_t)axis);
                return;
            }
            hold->state = SERVO_STARTUP_HOLD_WRITING_ZERO;
        }
        if (hold->state == SERVO_STARTUP_HOLD_WAITING ||
            hold->state == SERVO_STARTUP_HOLD_PREPARING)
        {
            preparing = 1U;
            if (state->snapshot.busy || !aimtor_fresh(state)) continue;
            if (state->snapshot.actual_speed_rpm != 0)
            {
                aimtor_startup_hold_fail_axis(4U, (uint8_t)axis);
                return;
            }
            s_boot_zero_internal = 1U;
            result = aimtor_set_control_mode(axis,
                SERVO_CONTROL_RS485_VELOCITY);
            s_boot_zero_internal = 0U;
            if (result == SERVO_OK)
                hold->state = SERVO_STARTUP_HOLD_PREPARING;
            else if (result != SERVO_BUSY && result != SERVO_NOT_READY)
                aimtor_startup_hold_fail_axis(1U, (uint8_t)axis);
            return;
        }
    }
    /* 全部选中轴准备完成后才使能，不在配置另一轴时释放保持力。 */
    if (preparing) return;
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        hold = &s_hold[axis];
        if (!hold->eligible) continue;
        if (aimtor_cli_axis_owned(axis) && !s_cli_restore) continue;
        state = &s_axes[axis];
        if (state->snapshot.busy) continue;
        if ((hold->state == SERVO_STARTUP_HOLD_WRITING_ZERO ||
             hold->state == SERVO_STARTUP_HOLD_ENABLING) &&
            state->snapshot.last_result != SERVO_OK)
        {
            aimtor_startup_hold_fail_axis(
                hold->state == SERVO_STARTUP_HOLD_WRITING_ZERO ? 2U : 3U, (uint8_t)axis);
            return;
        }
        if (hold->state == SERVO_STARTUP_HOLD_WRITING_ZERO)
        {
            aimtor_begin(axis, ACTION_STARTUP_HOLD);
            aimtor_add(axis, AIMTOR_REG_SPEED_COMMAND, 0U, 1U, 1U, 0U);
            return;
        }
        if (hold->state == SERVO_STARTUP_HOLD_ENABLING)
        {
            if (state->snapshot.enabled || (state->snapshot.enable_feedback_valid &&
                state->snapshot.enable_feedback)) {
                aimtor_startup_hold_fail_axis(4U, (uint8_t)axis);
                return;
            }
            /* Still disabled: stale cached feedback is a refresh request,
             * not proof of unsafe motion.  Keep the original deadline and
             * zero target; the ordinary startup arbiter obtains new data. */
            if (!aimtor_fresh(state) || !state->snapshot.enable_feedback_valid ||
                aimtor_tick_age_ms(now, state->snapshot.enable_feedback_tick) >=
                    APP_SERVO_STATUS_MAX_AGE_MS) continue;
            if (state->snapshot.actual_speed_rpm != 0 || state->snapshot.enabled ||
                state->snapshot.enable_feedback)
            {
                aimtor_startup_hold_fail_axis(4U, (uint8_t)axis);
                return;
            }
            if (!app_config_motion_hardware_ready() ||
                !app_config_servo_parameters_confirmed() ||
                !state->snapshot.parameter_verified || !state->snapshot.disable_verified)
            {
                aimtor_startup_hold_fail_axis(1U, (uint8_t)axis);
                return;
            }
            aimtor_begin(axis, ACTION_STARTUP_HOLD);
            aimtor_add(axis, AIMTOR_REG_SERVO_ENABLE, 1U, 1U, 1U, 0U);
            return;
        }
        if (hold->state == SERVO_STARTUP_HOLD_VERIFYING &&
            state->speed_sequence != s_hold_speed_sequence[axis] &&
            (int32_t)(state->fault_tick - s_hold_verify_tick[axis]) > 0 &&
            s_hold_enable_confirmed[axis] &&
            aimtor_tick_age_ms(now, s_hold_enable_tick[axis]) <
                APP_SERVO_STATUS_MAX_AGE_MS &&
            aimtor_fresh(state) && state->snapshot.enabled &&
            state->snapshot.actual_speed_rpm == 0 &&
            !state->snapshot.fault &&
            state->snapshot.parameter_verified &&
            state->snapshot.control_mode == SERVO_CONTROL_RS485_VELOCITY)
        {
            hold->verified = 1U;
            hold->state = SERVO_STARTUP_HOLD_READY;
        }
    }
}

void aimtor_get_recovery_status(servo_recovery_status_t *status)
{
    if (!status) return;
    taskENTER_CRITICAL(); *status = s_recovery; taskEXIT_CRITICAL();
}

bool aimtor_recovery_blocks_motion(void)
{
    return s_recovery.state != SERVO_RECOVERY_NONE && s_recovery.state != SERVO_RECOVERY_DONE;
}

servo_result_t aimtor_request_recovery(uint8_t source)
{
    servo_result_t result = SERVO_NOT_READY;
    taskENTER_CRITICAL();
    if ((source >= 1U && source <= 4U) && s_recovery.state == SERVO_RECOVERY_READY &&
        !app_health_watchdog_test_requested())
    {
        if (s_recovery_request) { taskEXIT_CRITICAL(); return SERVO_BUSY; }
        s_recovery_request = source;
        result = SERVO_OK;
    }
    taskEXIT_CRITICAL();
    return result;
}

void aimtor_recovery_isolate(void)
{
    taskENTER_CRITICAL();
    if (s_recovery.state == SERVO_RECOVERY_REQUESTED)
    {
        modbus_cancel_pending();
        s_pending_cancelled = 1U; s_pending_id = 0U;
        s_recovery_isolated = 1U;
    }
    taskEXIT_CRITICAL();
}

static void aimtor_recovery_service(TickType_t now)
{
    uint8_t axis, item, healthy = 1U, enough = 1U;
    if (s_recovery_request)
    {
        if (s_recovery.state == SERVO_RECOVERY_READY)
        {
            s_recovery.source = s_recovery_request;
            s_recovery.state = SERVO_RECOVERY_REQUESTED;
            s_recovery_isolated = 0U;
        }
        s_recovery_request = 0U;
        return;
    }
    if (s_recovery.state == SERVO_RECOVERY_REQUESTED && !s_recovery_isolated) return;
    if (!aimtor_recovery_blocks_motion() || s_recovery.state == SERVO_RECOVERY_FAILED) return;
    if (!s_recovery.eligible || aimtor_degraded() || app_health_watchdog_test_requested() ||
        !app_config_motion_hardware_ready() || app_config_servo_readonly_test() ||
        !aimtor_boot_zero_ready() || !aimtor_watchdog_recovery_ready() || !aimtor_startup_stop_ready())
        healthy = 0U;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
    {
        aimtor_axis_state_t *state = &s_axes[axis];
        if (!app_config_servo_axis_selected(axis)) continue;
        if (state->snapshot.fault || state->snapshot.fault_code)
        {
            s_recovery.eligible = 0U;
            healthy = 0U;
        }
        if (!aimtor_fresh(state) || state->snapshot.actual_speed_rpm != 0) healthy = 0U;
        if (s_recovery.state < SERVO_RECOVERY_PREPARING &&
            (state->snapshot.busy || state->disable_pending || !state->snapshot.disable_verified ||
             state->snapshot.enabled || !state->snapshot.enable_feedback_valid ||
             state->snapshot.enable_feedback ||
             aimtor_tick_age_ms(now, state->snapshot.enable_feedback_tick) >= APP_SERVO_STATUS_MAX_AGE_MS)) healthy = 0U;
        for (item = 0U; item < 3U; ++item)
        {
            if (s_recovery_floor[axis][item] != s_feedback_sequence[axis][item])
            {
                s_recovery_floor[axis][item] = s_feedback_sequence[axis][item];
                if (s_recovery_samples[axis][item] < 3U) s_recovery_samples[axis][item]++;
            }
            if (s_recovery_samples[axis][item] < 3U) enough = 0U;
        }
    }
    if (s_recovery.state == SERVO_RECOVERY_PREPARING || s_recovery.state == SERVO_RECOVERY_COMMIT)
    {
        for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
            if (s_hold[axis].eligible && s_hold[axis].state == SERVO_STARTUP_HOLD_FAILED) healthy = 0U;
        if (!healthy || aimtor_tick_age_ms(now, s_recovery_prepare_tick) >= APP_SERVO_STARTUP_HOLD_TIMEOUT_MS)
        {
            s_recovery.state = SERVO_RECOVERY_FAILED; s_recovery.eligible = 0U; s_recovery.failure = 3U;
            aimtor_emergency_stop();
        }
        else if (aimtor_startup_hold_ready()) s_recovery.state = SERVO_RECOVERY_COMMIT;
        return;
    }
    if (!healthy)
    {
        s_recovery_observing = 0U; s_recovery.healthy_ms = 0U;
        memset(s_recovery_samples, 0, sizeof(s_recovery_samples));
        memcpy(s_recovery_floor, s_feedback_sequence, sizeof(s_recovery_floor));
        if (s_recovery.state == SERVO_RECOVERY_REQUESTED)
        { s_recovery.state = SERVO_RECOVERY_FAILED; s_recovery.eligible = 0U; s_recovery.failure = 1U; }
        else s_recovery.state = s_recovery.eligible ? SERVO_RECOVERY_WAIT_HEALTH : SERVO_RECOVERY_FAILED;
        return;
    }
    if (!s_recovery_observing) { s_recovery_observing = 1U; s_recovery_healthy_tick = now; }
    s_recovery.healthy_ms = aimtor_tick_age_ms(now, s_recovery_healthy_tick);
    if (s_recovery.state == SERVO_RECOVERY_REQUESTED)
    {
        if (!enough || s_recovery.healthy_ms < 1000U || s_pending_id)
        { s_recovery.state = SERVO_RECOVERY_FAILED; s_recovery.failure = 1U; s_recovery.eligible = 0U; return; }
        s_recovery.state = SERVO_RECOVERY_PREPARING;
        s_recovery_prepare_tick = now;
        s_motion_authorized = s_cli_authorized = s_cli_mask = s_cli_internal = 0U;
        s_hmi_authorized = s_hmi_mask = s_can_authorized = s_can_mask = 0U;
        s_remote_control_mask = s_remote_stop_active = 0U;
        memset(&s_k1_hold, 0, sizeof(s_k1_hold));
        aimtor_startup_hold_init();
        for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
            if (app_config_servo_axis_selected(axis))
            {
                s_axes[axis].snapshot.stop_latched = 0U;
                s_axes[axis].snapshot.parameter_verified = 0U;
            }
        return;
    }
    if (enough && s_recovery.healthy_ms >= 1000U) s_recovery.state = SERVO_RECOVERY_READY;
}

void aimtor_recovery_commit(bool success)
{
    uint8_t axis;
    uint8_t valid = success ? 1U : 0U;
    taskENTER_CRITICAL();
    if (s_recovery.state == SERVO_RECOVERY_COMMIT)
    {
        if (s_pending_id != 0U) { taskEXIT_CRITICAL(); return; }
        if (valid && (!s_recovery.eligible || app_health_watchdog_test_requested() ||
            !app_config_motion_hardware_ready() || !app_config_servo_parameters_confirmed())) valid = 0U;
        if (valid && !aimtor_boot_zero_ready())
            valid = 0U;
        for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; ++axis)
        {
            if (!app_config_servo_axis_selected((uint8_t)axis)) continue;
            if (!aimtor_fresh(&s_axes[axis]) || !s_axes[axis].snapshot.enable_feedback_valid ||
                aimtor_tick_age_ms(xTaskGetTickCount(), s_axes[axis].snapshot.enable_feedback_tick) >= APP_SERVO_STATUS_MAX_AGE_MS ||
                s_axes[axis].snapshot.enable_feedback != (APP_SERVO_STARTUP_HOLD_ENABLED ? 1U : 0U) ||
                s_axes[axis].snapshot.busy ||
                s_axes[axis].snapshot.actual_speed_rpm != 0 ||
                s_axes[axis].snapshot.fault ||
                s_axes[axis].snapshot.fault_code)
                valid = 0U;
            if (APP_SERVO_STARTUP_HOLD_ENABLED)
            {
                if (s_hold[axis].state != SERVO_STARTUP_HOLD_READY)
                    valid = 0U;
            }
            else if (s_axes[axis].snapshot.enabled)
            {
                valid = 0U;
            }
        }
        if (valid)
        {
            s_recovery.state = SERVO_RECOVERY_DONE;
            motion_clear_fault();
            s_recovery_disqualified = 0U;
        }
        else
        {
            s_recovery.state = SERVO_RECOVERY_FAILED;
            s_recovery.eligible = 0U; s_recovery.failure = 4U;
            aimtor_emergency_stop();
        }
    }
    taskEXIT_CRITICAL();
}

void aimtor_service(void)
{
    servo_axis_t axis;
    aimtor_axis_state_t *state;
    aimtor_step_t poll;
    uint16_t tag;
    TickType_t now;
    uint8_t state_fault;
    uint8_t state_offline;
    uint8_t offset;
    uint8_t urgent_action;
    uint8_t disable_action;
    uint8_t speed_action_pending;

    if (!modbus_business_allowed())
    {
        if (!aimtor_cold_clear_ready())
        {
            taskENTER_CRITICAL();
            aimtor_cold_clear_service(xTaskGetTickCount());
            taskEXIT_CRITICAL();
        }
        return;
    }
    if (!aimtor_cold_clear_ready())
    {
        taskENTER_CRITICAL();
        aimtor_cold_clear_service(xTaskGetTickCount());
        taskEXIT_CRITICAL();
        return;
    }

    if (s_emergency_pending != 0U)
    {
        s_emergency_pending = 0U;
        motion_request_immediate_safety_stop(MOTION_FAULT_REASON_OUTPUT_FAILURE, 0U);
        return;
    }

    if (s_feedback_stop_pending != 0U)
    {
        uint8_t affected_axes;
        taskENTER_CRITICAL();
        affected_axes = s_feedback_stop_pending;
        s_feedback_stop_pending = 0U;
        s_recovery_candidate = 1U;
        aimtor_request_controlled_safety_stop(1U);
        s_safety_status.axis_mask |= affected_axes;
        taskEXIT_CRITICAL();
        return;
    }

    taskENTER_CRITICAL();
    if (s_controlled_safety_active)
    {
        aimtor_controlled_stop_service();
        taskEXIT_CRITICAL();
        return;
    }
    taskEXIT_CRITICAL();

    taskENTER_CRITICAL();
    now = xTaskGetTickCount();
    if (aimtor_degraded())
    {
        if (!s_degraded_active) memcpy(s_degraded_limit, s_last_velocity, sizeof(s_degraded_limit));
        s_degraded_active = 1U;
    }
    else s_degraded_active = 0U;
    aimtor_recovery_service(now);
    if (s_recovery.state == SERVO_RECOVERY_COMMIT)
    { taskEXIT_CRITICAL(); return; }
    if (!aimtor_watchdog_recovery_ready())
    {
        if (s_pending_id == 0U)
        {
            modbus_resume_transport();
            aimtor_watchdog_recovery_service(now);
        }
        taskEXIT_CRITICAL();
        return;
    }
    if (s_pending_id == 0U)
    {
        aimtor_startup_stop_service(now);
        aimtor_boot_zero_service(now);
        aimtor_k1_hold_service(now);
        aimtor_startup_hold_service(now);
    }
    if (s_controlled_safety_active)
    {
        taskEXIT_CRITICAL();
        return;
    }
    state_fault = 0U;
    state_offline = 0U;
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_config_servo_axis_selected((uint8_t)axis))
        {
            continue;
        }
        state = &s_axes[axis];
        if (state->snapshot.enabled &&
            state->snapshot.fault)
        {
            state_fault = 1U;
            break;
        }
        if (state->snapshot.enabled &&
            !aimtor_fresh(state))
        {
            state_offline = 1U;
            break;
        }
    }
    taskEXIT_CRITICAL();
    if (state_fault != 0U)
    {
        motion_request_immediate_safety_stop(MOTION_FAULT_REASON_SERVO_FAULT, 0U);
        return;
    }
    if (state_offline != 0U)
    {
        s_recovery_candidate = 1U;
        aimtor_request_controlled_safety_stop(1U);
        return;
    }

    taskENTER_CRITICAL();
    if (s_pending_id != 0U)
    {
        taskEXIT_CRITICAL();
        return;
    }
    /* 旧事务已返回且取消结果已丢弃，恢复仅用于新序列和只读轮询。 */
    modbus_resume_transport();
    /* Budget one background transaction after four ordinary feedback reads.
     * Safety/startup services above always retain precedence. */
    if (s_background_critical >= APP_SERVO_BACKGROUND_MAX_CRITICAL &&
        aimtor_background_safe(now) && aimtor_pick_background(&axis, &tag, now)) {
        memset(&poll, 0, sizeof(poll));
        poll.address = aimtor_poll_address(tag); poll.count = aimtor_poll_count(tag);
        (void)aimtor_submit_step(axis, tag, &poll, 1U);
        taskEXIT_CRITICAL(); return;
    }
    /* 所有选中轴定期读取使能；写应答不能代替新鲜使能反馈。 */
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!aimtor_startup_feedback_active() &&
            (!s_axes[axis].snapshot.busy || s_axes[axis].action == ACTION_SPEED) &&
            !(aimtor_sync_poll_active() && s_sync_poll_phase == 1U) &&
            app_config_servo_axis_selected((uint8_t)axis) &&
            !s_axes[axis].disable_pending &&
            (!s_axes[axis].snapshot.enable_feedback_valid ||
             aimtor_tick_due(now, s_hold_enable_poll[axis])))
        {
            memset(&poll, 0, sizeof(poll));
            poll.address = AIMTOR_REG_SERVO_ENABLE;
            poll.count = 1U;
            (void)aimtor_submit_step(axis, AIMTOR_TAG_HOLD_ENABLE, &poll, 1U);
            taskEXIT_CRITICAL();
            return;
        }
    }
    urgent_action = 0U;
    disable_action = 0U;
    speed_action_pending = 0U;
    /* 先建立所有待失能事务，不能让一轴速度写入挡住另一轴失能。 */
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_config_servo_axis_selected((uint8_t)axis))
        {
            continue;
        }
        state = &s_axes[axis];
        if (state->disable_pending)
        {
            state->disable_pending = 0U;
            if (APP_SERVO_SOFT_ENABLE_CONFIRMED)
            {
                aimtor_begin(axis, ACTION_DISABLE);
                /* 失能同样只确认写入应答，避免瞬时读回造成误判。 */
                aimtor_add(axis, AIMTOR_REG_SERVO_ENABLE, 0U, 1U, 1U, 0U);
                if (aimtor_startup_hold_active() &&
                    state->snapshot.control_mode == SERVO_CONTROL_RS485_VELOCITY)
                    aimtor_add(axis, AIMTOR_REG_SPEED_COMMAND, 0U, 1U, 1U, 0U);
            }
        }
        if (state->snapshot.busy && state->action != ACTION_SPEED)
        {
            urgent_action = 1U;
            if (state->action == ACTION_DISABLE || state->action == ACTION_STARTUP_DISABLE)
            {
                disable_action = 1U;
            }
        }
        if (state->snapshot.busy && state->action == ACTION_SPEED)
        {
            speed_action_pending = 1U;
        }
    }
    /* A critical sample is allowed at every completed parameter step.  The
     * first sample must not depend on a flag set by a previous response;
     * otherwise a fresh boot can deadlock in CONFIGURING with no feedback. */
    /* Consume at most one critical feedback read for each completed startup
     * parameter step.  Without this token, the feedback arbiter continuously
     * re-polls the three critical tags and can starve the configuration
     * sequence until its ten-second deadline. */
    if (!disable_action && aimtor_startup_feedback_active() &&
        (s_startup_feedback_slot != 0U || !urgent_action))
    {
        uint8_t step_complete = 1U;
        uint8_t startup_off_pending = 0U;
        for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; ++axis)
            if (s_startup_stop[axis].state == SERVO_STARTUP_STOP_WAITING_ZERO &&
                !s_startup_stop[axis].off_confirmed)
                startup_off_pending = 1U;
        if (startup_off_pending) step_complete = 0U;
        for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; ++axis)
            /* A sequence remains busy while it has more steps, but a
             * completed write step (reading_back==0) is an arbitration point.
             * Only an in-flight readback may not be interrupted. */
            if (!startup_off_pending && s_axes[axis].snapshot.busy && s_axes[axis].reading_back)
                step_complete = 0U;
        if (step_complete && aimtor_pick_startup_feedback(&axis, &tag, now)) {
            memset(&poll, 0, sizeof(poll));
            poll.address = aimtor_poll_address(tag); poll.count = aimtor_poll_count(tag);
            if (aimtor_submit_step(axis, tag, &poll, 1U) == MODBUS_OK)
                s_startup_feedback_slot = 0U;
            taskEXIT_CRITICAL();
            return;
        }
    }
    /*
     * 到期关键回读优先；总线饱和时每轮关键回读后留一个速度写入机会，
     * 防止四类回读始终到期而使遥控给定永久饥饿。
     */
    if (s_cli_mask && !urgent_action &&
        s_critical_poll_streak >= app_config_servo_control_axis_count() * 2U)
    {
        /* 一轮关键反馈后给会话显式位置请求一次机会，避免脉冲启动饥饿。 */
        for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!aimtor_cli_axis_owned(axis) ||
                !s_axes[axis].position_priority ||
                !aimtor_poll_due(&s_axes[axis], AIMTOR_TAG_POSITION, now))
                continue;
            memset(&poll, 0, sizeof(poll));
            poll.address = AIMTOR_REG_ABSOLUTE_POSITION;
            poll.count = 2U;
            if (aimtor_submit_step(axis, AIMTOR_TAG_POSITION, &poll, 1U) != MODBUS_OK)
                s_axes[axis].next_position_poll = aimtor_deadline_after(now, 20U);
            s_critical_poll_streak = 0U;
            taskEXIT_CRITICAL();
            return;
        }
    }
    if (!urgent_action &&
        (aimtor_boot_zero_ready() ||
         s_critical_poll_streak < SERVO_AXIS_COUNT * 2U) &&
        (!speed_action_pending ||
         s_critical_poll_streak < app_config_servo_control_axis_count() * 2U ||
         (aimtor_sync_poll_active() && s_sync_poll_phase == 1U)) &&
        aimtor_pick_poll(&axis, &tag, now, 1U))
    {
        if (s_critical_poll_streak < SERVO_AXIS_COUNT * 2U)
        {
            s_critical_poll_streak++;
        }
        memset(&poll, 0, sizeof(poll));
        poll.address = aimtor_poll_address(tag);
        poll.count = aimtor_poll_count(tag);
        if (aimtor_submit_step(axis, tag, &poll, 1U) != MODBUS_OK)
        {
            if (tag == AIMTOR_TAG_SPEED)
                s_axes[axis].next_speed_poll = aimtor_deadline_after(now, 20U);
            else if (tag == AIMTOR_TAG_FAULT)
                s_axes[axis].next_fault_poll = aimtor_deadline_after(now, 20U);
            else
                s_axes[axis].next_state_poll = aimtor_deadline_after(now, 20U);
        }
        taskEXIT_CRITICAL();
        return;
    }
    for (offset = 0U; offset < SERVO_AXIS_COUNT; offset++)
    {
        axis = (servo_axis_t)((s_action_axis_turn + offset) % SERVO_AXIS_COUNT);
        if (!app_config_servo_axis_selected((uint8_t)axis))
        {
            continue;
        }
        state = &s_axes[axis];
        if (disable_action && state->action != ACTION_DISABLE &&
            state->action != ACTION_STARTUP_DISABLE)
        {
            continue;
        }
        if (urgent_action && state->action == ACTION_SPEED)
        {
            continue;
        }
        /* 仅静止核验后设零，不在main中无条件写回零参数。 */
        if (!app_config_servo_readonly_test() &&
            APP_SERVO_BOOT_ZERO_ENABLED &&
            (state->snapshot.control_mode == SERVO_CONTROL_PULSE_POSITION) &&
            state->snapshot.parameter_verified &&
            !state->snapshot.zero_attempted && !state->snapshot.stop_latched)
        {
            (void)aimtor_zero_once(axis);
        }
        if (state->snapshot.busy)
        {
            s_critical_poll_streak = 0U;
            s_action_axis_turn = (uint8_t)((axis + 1U) % SERVO_AXIS_COUNT);
            if (aimtor_submit_step(axis, AIMTOR_TAG_SEQUENCE,
                &state->steps[state->index],
                (uint8_t)(state->reading_back ||
                          !state->steps[state->index].write)) != MODBUS_OK)
            {
                uint8_t speed_action;

                speed_action = state->action == ACTION_SPEED;
                if (state->action == ACTION_DISABLE)
                {
                    state->snapshot.busy = 0U;
                    state->action = ACTION_NONE;
                }
                else if (state->action == ACTION_BOOT_ZERO ||
                         state->action == ACTION_STARTUP_DISABLE)
                {
                    /* 队列忙未发送，不消耗次数，也不触发普通写失败联停。 */
                    state->snapshot.busy = 0U;
                    state->action = ACTION_NONE;
                }
                else
                {
                    if (aimtor_startup_feedback_active())
                        aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_STARTUP,
                            (uint8_t)axis, 0x0503U, SERVO_QUEUE_ERROR);
                    aimtor_fail(axis);
                }
                state->snapshot.last_result = SERVO_QUEUE_ERROR;
                if (speed_action != 0U)
                {
                    state->snapshot.speed_write_completed =
                        state->speed_write_sequence;
                    state->snapshot.speed_write_result =
                        SERVO_QUEUE_ERROR;
                }
            }
            taskEXIT_CRITICAL();
            return;
        }
    }
    /* 失能应答不是反馈；启动专用读取不能被后台轮询长期挤占。 */
    for (offset = 0U; offset < SERVO_AXIS_COUNT; offset++)
    {
        axis = (servo_axis_t)((s_action_axis_turn + offset) % SERVO_AXIS_COUNT);
        if (s_startup_stop[axis].state == SERVO_STARTUP_STOP_WAITING_ZERO &&
            !s_startup_stop[axis].off_confirmed &&
            aimtor_tick_due(now, s_startup_retry[axis]))
        {
            memset(&poll, 0, sizeof(poll));
            poll.address = AIMTOR_REG_SERVO_ENABLE;
            poll.count = 1U;
            if (aimtor_submit_step(axis, AIMTOR_TAG_STARTUP_OFF, &poll, 1U)
                == MODBUS_OK)
                s_action_axis_turn = (uint8_t)((axis + 1U) % SERVO_AXIS_COUNT);
            s_critical_poll_streak = 0U;
            taskEXIT_CRITICAL();
            return;
        }
    }
    if (aimtor_pick_poll(&axis, &tag, now, 0U))
    {
        if (tag == AIMTOR_TAG_POSITION || tag == AIMTOR_TAG_STATE)
            s_critical_poll_streak = 0U;
        memset(&poll, 0, sizeof(poll));
        poll.address = aimtor_poll_address(tag);
        poll.count = aimtor_poll_count(tag);
        if (aimtor_submit_step(axis, tag, &poll, 1U) != MODBUS_OK)
        {
            if (tag == AIMTOR_TAG_SPEED)
                s_axes[axis].next_speed_poll = aimtor_deadline_after(now, 20U);
            else if (tag == AIMTOR_TAG_FAULT)
                s_axes[axis].next_fault_poll = aimtor_deadline_after(now, 20U);
            else if (aimtor_is_diagnostic_tag(tag))
                s_axes[axis].next_diagnostic_poll[aimtor_diagnostic_index(tag)] =
                    aimtor_deadline_after(now, 20U);
            else
                s_axes[axis].next_state_poll = aimtor_deadline_after(now, 20U);
        }
    }
    taskEXIT_CRITICAL();
}
