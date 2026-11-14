#include "app_can_status.h"
#include "app_can_config.h"
#include "app_config.h"
#include "app_health.h"
#include <limits.h>
#include <string.h>

static uint16_t age16(uint32_t age)
{
    return age > 65534U ? 65535U : (uint16_t)age;
}

static uint16_t signed16(double value)
{
    if (value != value) return 0x8000U;
    if (value >= 32767.0) return 32767U;
    if (value <= -32767.0) return (uint16_t)(int16_t)-32767;
    return (uint16_t)(int16_t)(value < 0.0 ? value - 0.5 : value + 0.5);
}

static uint32_t signed32(double value)
{
    if (value != value) return 0x80000000UL;
    if (value >= 2147483647.0) return 0x7FFFFFFFUL;
    if (value <= -2147483647.0) return 0x80000001UL;
    return (uint32_t)(int32_t)(value < 0.0 ? value - 0.5 : value + 0.5);
}

static uint32_t unsigned32(double value)
{
    if (value != value || value < 0.0) return UINT32_MAX;
    if (value >= 4294967294.0) return UINT32_MAX - 1U;
    return (uint32_t)(value + 0.5);
}

static uint64_t distance64(double value, bool is_signed, bool *saturated)
{
    if (value != value)
    {
        *saturated = true;
        return is_signed ? (UINT64_C(1) << 63U) : UINT64_MAX;
    }
    /* double在64位边界的精度有限，留出边界并报告饱和，不进行越界转换。 */
    if (is_signed)
    {
        if (value >= 9223372036854774784.0)
        {
            *saturated = true;
            return INT64_MAX;
        }
        if (value <= -9223372036854774784.0)
        {
            *saturated = true;
            return (uint64_t)(-INT64_MAX);
        }
        return (uint64_t)(int64_t)(value < 0.0 ? value - 0.5 : value + 0.5);
    }
    if (value < 0.0 || value >= 18446744073709549568.0)
    {
        *saturated = true;
        return value < 0.0 ? 0U : UINT64_MAX - 1U;
    }
    return (uint64_t)(value + 0.5);
}

static bool speed_valid(const app_can_status_input_t *in, uint8_t axis)
{
    const servo_snapshot_t *s = &in->axis[axis];
    return in->source_valid && s->online && s->speed_valid &&
           (uint64_t)s->speed_age_ms + (uint32_t)(in->now_ms - in->snapshot_ms)
               <= APP_SERVO_STATUS_MAX_AGE_MS;
}

static bool fault_valid(const app_can_status_input_t *in, uint8_t axis)
{
    const servo_snapshot_t *s = &in->axis[axis];
    return in->source_valid && s->online && s->fault_valid &&
           (uint64_t)s->fault_age_ms + (uint32_t)(in->now_ms - in->snapshot_ms)
               <= APP_SERVO_STATUS_MAX_AGE_MS;
}

static uint8_t drive_mode(servo_control_mode_t mode)
{
    switch (mode)
    {
        case SERVO_CONTROL_NONE: return 0U;
        case SERVO_CONTROL_PULSE_POSITION: return 1U;
        case SERVO_CONTROL_RS485_VELOCITY: return 2U;
        case SERVO_CONTROL_RS485_SEGMENT_POSITION: return 3U;
        default: return 255U;
    }
}

static uint32_t s_arm_safety_sequence;
static uint32_t s_arm_safety_generation;
static uint32_t s_arm_safety_signature;

static uint32_t arm_safety_signature(const app_can_status_input_t *in,
                                     uint8_t flags, uint32_t reason)
{
    return ((uint32_t)in->selected << 24U) | ((uint32_t)flags << 16U) |
        (reason << 8U) | (in->remote.estop_latched ? 1U : 0U) |
        (in->diagnostic & ~(uint32_t)APP_DIAG_DISPLAY_WARNING_MASK & 0xFFU);
}

static bool arm_stationary(const app_can_status_input_t *in)
{
    uint8_t i;
    bool any = false;
    for (i = 0U; i < 2U; ++i)
    {
        if ((in->selected & (1U << i)) == 0U) continue;
        any = true;
        if (!speed_valid(in, i) || !fault_valid(in, i) ||
            in->axis[i].fault_code != 0U || in->axis[i].actual_speed_rpm != 0)
            return false;
    }
    return any;
}

static uint8_t remote_reason(remote_stop_reason_t reason)
{
    switch (reason)
    {
        case REMOTE_STOP_NONE: return 0U;
        case REMOTE_STOP_K1: return 1U;
        case REMOTE_STOP_LINK: return 2U;
        case REMOTE_STOP_MANUAL: return 5U;
        case REMOTE_STOP_CAN: return 5U;
        case REMOTE_STOP_SERVO_STATE: return 6U;
        default: return 7U;
    }
}

static bool control_fresh(const app_can_status_input_t *in)
{
    return in->source_valid &&
        (uint32_t)(in->now_ms - in->snapshot_ms) <= APP_SERVO_STATUS_MAX_AGE_MS;
}

static bool hmi_authorized(const app_can_status_input_t *in)
{
    return control_fresh(in) && in->arbiter_owner == APP_CONTROL_OWNER_HMI &&
        in->lock_reason == APP_CONTROL_LOCK_NONE && in->hmi.session != 0U &&
        (in->hmi.flags & 2U) != 0U &&
        (in->hmi.state == 2U || in->hmi.state == 3U) &&
        !(in->stop.owner == APP_CONTROL_OWNER_HMI && in->stop.result == 2U);
}

static bool owner_authorized(const app_can_status_input_t *in, app_control_owner_t owner)
{
    if (!control_fresh(in) || in->arbiter_owner != owner ||
        in->lock_reason != APP_CONTROL_LOCK_NONE) return false;
    if (owner == APP_CONTROL_OWNER_HMI) return hmi_authorized(in);
    if (owner == APP_CONTROL_OWNER_CAN) return in->control.authorized;
    if (owner == APP_CONTROL_OWNER_REMOTE) return in->remote.authorized != 0U;
    if (owner == APP_CONTROL_OWNER_CLI) return in->cli.authorized != 0U;
    return false;
}

static bool hmi_stop_selected(const app_can_status_input_t *in)
{
    return in->stop_source == APP_CONTROL_OWNER_HMI &&
        in->stop.event != 0U && in->stop.owner == APP_CONTROL_OWNER_HMI &&
        (in->runtime & 64U) == 0U;
}

static void select_stop_source(app_can_status_input_t *in)
{
    bool remote_active = in->remote.controlled_stop_state != REMOTE_CONTROLLED_STOP_IDLE &&
        in->remote.controlled_stop_state != REMOTE_CONTROLLED_STOP_DONE &&
        in->remote.controlled_stop_state != REMOTE_CONTROLLED_STOP_FAILED;
    bool can_active = in->control.stop_phase >= 1U && in->control.stop_phase <= 3U;
    if (!control_fresh(in)) return;
    if (in->seen_boot_id != in->boot_id)
    {
        in->stop_source = APP_CONTROL_OWNER_NONE;
        in->seen_stop_event = in->seen_can_stop_generation = in->seen_remote_stop_generation = 0U;
        in->seen_remote_stop_state = REMOTE_CONTROLLED_STOP_IDLE;
        in->seen_boot_id = in->boot_id;
    }
    if (in->remote.stop_generation != in->seen_remote_stop_generation ||
        (remote_active && in->seen_remote_stop_state != (uint8_t)in->remote.controlled_stop_state))
        in->stop_source = APP_CONTROL_OWNER_REMOTE;
    if (in->control.stop_generation != in->seen_can_stop_generation)
        in->stop_source = APP_CONTROL_OWNER_CAN;
    if (in->stop.event != in->seen_stop_event && in->stop.owner == APP_CONTROL_OWNER_HMI)
        in->stop_source = APP_CONTROL_OWNER_HMI;
    if (remote_active && in->arbiter_owner == APP_CONTROL_OWNER_REMOTE)
        in->stop_source = APP_CONTROL_OWNER_REMOTE;
    if (can_active && !in->control.stop_external && in->arbiter_owner == APP_CONTROL_OWNER_CAN)
        in->stop_source = APP_CONTROL_OWNER_CAN;
    if (in->stop.result == 2U && in->stop.owner == APP_CONTROL_OWNER_HMI &&
        in->arbiter_owner == APP_CONTROL_OWNER_HMI)
        in->stop_source = APP_CONTROL_OWNER_HMI;
    if (in->control.stop_seen && in->control.stop_external &&
        ((in->control.stop_phase >= 1U && in->control.stop_phase <= 3U) ||
         (in->stop.command == 0x080U && in->stop.request_sequence == in->control.stop_sequence)))
        in->stop_source = APP_CONTROL_OWNER_CAN;
    in->seen_remote_stop_state = (uint8_t)in->remote.controlled_stop_state;
    in->seen_remote_stop_generation = in->remote.stop_generation;
    in->seen_can_stop_generation = in->control.stop_generation;
    in->seen_stop_event = in->stop.event;
}

static uint8_t hmi_stop_reason(uint16_t reason)
{
    if (reason == 3U) return 4U;
    if (reason == 4U || reason == 7U) return 7U;
    if (reason == 6U) return 6U;
    return reason == 0U ? 0U : 5U;
}

static uint8_t can_stop_reason(uint8_t reason)
{
    if (reason == 2U) return 3U;
    if (reason == 9U) return 4U;
    return reason;
}

static uint8_t hmi_stop_phase(const app_can_status_input_t *in)
{
    uint8_t axis;
    const app_control_stop_t *stop = &in->stop;
    if (!control_fresh(in) || in->hmi.stop.event != stop->event ||
        in->hmi.stop.generation != stop->generation ||
        in->hmi.stop.owner != APP_CONTROL_OWNER_HMI ||
        in->hmi.stop.result != stop->result) return 255U;
    if (stop->result == 5U || stop->error != 0U) return 5U;
    if (stop->result == 2U)
        return in->arbiter_owner == APP_CONTROL_OWNER_HMI &&
            stop->generation == in->arbiter_generation &&
            in->hmi.phase >= 1U && in->hmi.phase <= 3U ? (uint8_t)in->hmi.phase : 255U;
    if (stop->result != 3U || in->hmi.phase != 4U || !(in->hmi.flags & 8U) ||
        in->arbiter_owner != APP_CONTROL_OWNER_NONE ||
        in->lock_reason != APP_CONTROL_LOCK_NONE || in->selected == 0U ||
        stop->zero_mask != in->selected || stop->enabled_mask != in->selected)
        return 255U;
    for (axis = 0U; axis < 2U; ++axis)
    {
        const servo_snapshot_t *feedback = &in->axis[axis];
        if (!(in->selected & (1U << axis))) continue;
        if (!speed_valid(in, axis) || !fault_valid(in, axis) || feedback->fault ||
            feedback->fault_code || feedback->actual_speed_rpm != 0 ||
            !feedback->enable_feedback_valid || !feedback->enable_feedback ||
            !feedback->enabled || feedback->stop_latched ||
            (uint64_t)feedback->enable_feedback_age_ms +
                (uint32_t)(in->now_ms - in->snapshot_ms) > APP_SERVO_STATUS_MAX_AGE_MS)
            return 255U;
    }
    return 4U;
}

void app_can_status_describe(app_can_status_input_t *in)
{
    uint8_t i;
    bool fault = app_health_motion_diagnostic_fault(in->diagnostic), latched = in->remote.estop_latched;
    bool mode_busy = false, recovery = false;
    select_stop_source(in);
    in->owner = 0U;
    in->life = (in->gates & 1U) != 0U ? 1U : 0U;
    in->work = 0U;
    in->drive = 0U;
    in->stop_reason = 0U;
    in->runtime = 0U;
    for (i = 0U; i < 2U; ++i)
    {
        if ((in->selected & (1U << i)) == 0U) continue;
        if (in->axis[i].fault) fault = true;
        if (in->axis[i].stop_latched) latched = true;
        if (in->axis[i].busy) mode_busy = true;
        if (in->axis[i].parameter_verified)
            in->drive = drive_mode(in->axis[i].control_mode);
        if (in->watchdog[i].eligible &&
            in->watchdog[i].state != SERVO_WDG_RECOVERY_DONE &&
            in->watchdog[i].state != SERVO_WDG_RECOVERY_SKIPPED)
            recovery = true;
    }
    if (in->arbiter_owner == APP_CONTROL_OWNER_REMOTE)
    {
        in->owner = 1U;
        in->stop_reason = remote_reason(in->remote.stop_reason);
        in->work = 2U;
        in->life = in->remote.state == REMOTE_SERVO_ARMING ? 2U :
                   in->remote.state == REMOTE_SERVO_RUNNING ? 3U : 1U;
    }
    if (in->arbiter_owner == APP_CONTROL_OWNER_CLI)
    {
        in->owner = 2U;
        in->work = 6U;
        in->drive = in->cli.axis < SERVO_AXIS_COUNT ?
            drive_mode(in->axis[in->cli.axis].control_mode) : 255U;
        in->life = in->cli.state == CLI_SESSION_ENTERING ? 2U :
            in->cli.state == CLI_SESSION_LOCKED ? 6U :
            in->cli.state == CLI_SESSION_STOPPING ||
            in->cli.state == CLI_SESSION_RESTORING ? 4U :
            in->cli.debug_state == SERVO_DEBUG_RUNNING ? 3U : 7U;
        if (in->cli.reason == CLI_EXIT_TIMEOUT) in->stop_reason = 10U;
        else if (in->cli.reason == CLI_EXIT_K1) in->stop_reason = 1U;
        else if (in->cli.reason != CLI_EXIT_NONE) in->stop_reason = 5U;
        if (in->cli.state == CLI_SESSION_LOCKED) latched = true;
    }
    if (in->arbiter_owner == APP_CONTROL_OWNER_CAN)
    {
        in->owner = 4U;
        in->work = 2U; in->drive = 2U;
        in->life = in->control.stop_phase ? 4U : in->control.state == CAN_CONTROL_PREPARING ? 2U :
            in->control.state == CAN_CONTROL_RUNNING ? 3U : 1U;
        in->stop_reason = can_stop_reason(in->control.stop_reason);
        if (in->control.state == CAN_CONTROL_FAULT) { fault = true; latched = true; }
    }
    if (in->arbiter_owner == APP_CONTROL_OWNER_HMI)
    {
        in->owner = 3U;
        in->work = 2U;
        in->life = in->hmi.state == 1U ? 2U : in->hmi.state == 3U ? 3U : 1U;
        if (in->stop.owner == APP_CONTROL_OWNER_HMI && in->stop.result == 2U)
        { in->life = 4U; in->stop_reason = hmi_stop_reason(in->stop.reason); }
        if (in->hmi.state == 6U) fault = true;
    }
    if (in->arbiter_owner == APP_CONTROL_OWNER_AUTO ||
        in->arbiter_owner > APP_CONTROL_OWNER_CAN)
        in->owner = in->life = in->work = 255U;
    if ((in->remote.hold_stopping &&
         (in->arbiter_owner == APP_CONTROL_OWNER_REMOTE || in->arbiter_owner == APP_CONTROL_OWNER_NONE)) || recovery)
    {
        in->owner = 5U;
        in->life = 4U;
        if (recovery) in->stop_reason = 8U;
    }
    if (in->lock_reason == APP_CONTROL_LOCK_SAFETY) fault = true;
    if (latched && (in->arbiter_owner == APP_CONTROL_OWNER_NONE ||
        in->arbiter_owner == APP_CONTROL_OWNER_REMOTE ||
        in->arbiter_owner == APP_CONTROL_OWNER_CLI) &&
        in->life != 4U && !in->remote.hold_stopping && !recovery)
    {
        in->life = 6U;
        in->work = 5U;
    }
    if (fault)
    {
        in->life = 5U;
        in->work = 4U;
    }
    in->runtime = (uint8_t)((owner_authorized(in, APP_CONTROL_OWNER_CLI) ? 1U : 0U) |
        (owner_authorized(in, APP_CONTROL_OWNER_REMOTE) ? 2U : 0U) |
        (owner_authorized(in, APP_CONTROL_OWNER_CAN) ? 4U : 0U) |
        (mode_busy ? 16U : 0U) |
        (((in->arbiter_owner == APP_CONTROL_OWNER_REMOTE && in->remote.batch_active) ||
          (in->arbiter_owner == APP_CONTROL_OWNER_CAN && in->control.pending_mask) ||
          (in->arbiter_owner == APP_CONTROL_OWNER_HMI && mode_busy) || in->life == 4U) ? 32U : 0U) |
        (recovery ? 64U : 0U) | (hmi_authorized(in) ? 128U : 0U));
    if (!control_fresh(in))
    {
        in->owner = in->life = in->work = in->drive = 255U;
        in->stop_reason = 255U;
        in->runtime = 0U;
    }
}

static void system_fault(const app_can_status_input_t *in, uint8_t p[8])
{
    uint16_t primary = 0U, secondary = 0U, bitmap = 0U;
    uint16_t faults[8];
    uint8_t count = 0U, i;
    bool known = in->source_valid;
    for (i = 0U; i < 2U; ++i)
    {
        if ((in->selected & (1U << i)) == 0U) continue;
        if (!fault_valid(in, i))
        {
            known = false;
            bitmap |= 1U << 4U;
        }
        else if (in->axis[i].fault || in->axis[i].fault_code != 0U)
        {
            bitmap |= (uint16_t)(1U << (2U + i));
            faults[count++] = (uint16_t)(0x0101U + i);
        }
    }
    if ((bitmap & (1U << 4U)) != 0U) faults[count++] = 0x0201U;
    if (in->remote.controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED)
    {
        bitmap |= 1U << 8U;
        faults[count++] = 0x0304U;
    }
    for (i = 0U; i < 2U; ++i)
        if ((in->selected & (1U << i)) != 0U &&
            in->watchdog[i].state == SERVO_WDG_RECOVERY_FAILED)
        {
            bitmap |= 1U << 6U;
            faults[count++] = 0x0302U;
            break;
        }
    if (in->remote.estop_latched ||
        (in->cli.state == CLI_SESSION_LOCKED && in->cli.recoverable))
    {
        bitmap |= 2U;
        faults[count++] = 2U;
    }
    if (app_health_motion_diagnostic_fault(in->diagnostic))
    {
        bitmap |= 1U << 9U;
        faults[count++] = 0x0401U;
    }
    if (known) bitmap |= 0x8000U;
    primary = count != 0U ? faults[0] : known ? 0U : 65535U;
    secondary = count > 1U ? faults[1] : known ? 0U : 65535U;
    can_protocol_write_u16(p, primary);
    can_protocol_write_u16(p + 2U, bitmap);
    can_protocol_write_u16(p + 4U, secondary);
}

static double rpm_mm_s(int16_t rpm, uint8_t axis)
{
    double diameter = axis == 0U ? APP_POSITION_LEFT_WHEEL_DIAMETER_MM :
                                  APP_POSITION_RIGHT_WHEEL_DIAMETER_MM;
    double numerator = axis == 0U ? APP_POSITION_LEFT_GEAR_NUM :
                                   APP_POSITION_RIGHT_GEAR_NUM;
    double denominator = axis == 0U ? APP_POSITION_LEFT_GEAR_DEN :
                                     APP_POSITION_RIGHT_GEAR_DEN;
    int sign = axis == 0U ? APP_POSITION_LEFT_SIGN : APP_POSITION_RIGHT_SIGN;
    return rpm * sign * 3.14159265358979323846 * diameter * denominator /
           (60.0 * numerator);
}

bool app_can_status_basic(const app_can_status_input_t *in,
    uint16_t id, uint8_t sequence, uint8_t p[8])
{
    uint8_t i;
    memset(p, 0, 8U);
    can_protocol_write_u16(p + 6U, in->token);
    switch (id)
    {
        case 0x180U:
        {
            bool actual_valid = in->source_valid && in->selected != 0U;
            bool enabled = actual_valid, target_valid;
            double actual = 0.0, target = 0.0;
            uint8_t axes = 0U;
            for (i = 0U; i < 2U; ++i)
            {
                if ((in->selected & (1U << i)) == 0U) continue;
                ++axes;
                actual_valid = actual_valid && speed_valid(in, i);
                enabled = enabled && fault_valid(in, i) && in->axis[i].enabled;
                actual += rpm_mm_s(in->axis[i].actual_speed_rpm, i);
                target += rpm_mm_s(in->arbiter_owner == APP_CONTROL_OWNER_HMI ?
                    in->hmi.target : i == 0U ? in->remote.left_commanded_rpm :
                    in->remote.right_commanded_rpm, i);
            }
            target_valid = axes != 0U &&
                owner_authorized(in, APP_CONTROL_OWNER_REMOTE);
            if (in->arbiter_owner == APP_CONTROL_OWNER_HMI)
                target_valid = axes != 0U && hmi_authorized(in) &&
                    (uint64_t)in->hmi.target_age_ms +
                        (uint32_t)(in->now_ms - in->snapshot_ms) < APP_HMI_SPEED_HOLD_MAX_MS;
            if (in->owner == 4U || (in->owner == 5U && !in->control.stop_external &&
                in->control.stop_phase >= 1U && in->control.stop_phase <= 3U))
            {
                actual_valid = actual_valid && in->control.actual_valid;
                target_valid = axes != 0U && owner_authorized(in, APP_CONTROL_OWNER_CAN);
                actual = in->control.actual_mm_s * axes;
                target = in->control.target_mm_s * axes;
            }
            p[0] = in->work;
            p[1] = (uint8_t)((actual_valid ? 1U : 0U) |
                (target_valid ? 2U : 0U) | (enabled ? 4U : 0U) |
                ((in->runtime & 135U) != 0U ? 8U : 0U) |
                (in->life == 4U ? 16U : 0U) |
                (in->life == 5U ? 32U : 0U) |
                (in->life == 6U ? 64U : 0U) |
                (control_fresh(in) ? 128U : 0U));
            can_protocol_write_u16(p + 2U, actual_valid ? signed16(actual / axes) : 0x8000U);
            /* CLI位置/脉冲和未授权状态没有可靠速度目标，不伪造目标0。 */
            can_protocol_write_u16(p + 4U, target_valid ? signed16(target / axes) : 0x8000U);
            break;
        }
        case 0x181U:
            can_protocol_write_u16(p, 65535U);
            can_protocol_write_u16(p + 2U, 0x8000U);
            p[4] = 255U;
            break;
        case 0x182U:
            system_fault(in, p);
            break;
        case 0x183U:
            memset(p, 255, 5U);
            break;
        case 0x185U:
            p[0] = 1U; p[1] = sequence; p[2] = in->owner; p[3] = in->life;
            can_protocol_write_u16(p + 4U, in->token);
            p[6] = in->gates & 31U; p[7] = in->selected;
            break;
        case 0x186U:
        case 0x187U:
        {
            const servo_snapshot_t *s;
            i = id == 0x186U ? 0U : 1U;
            s = &in->axis[i];
            p[0] = (uint8_t)((in->source_valid && s->online ? 1U : 0U) |
                (fault_valid(in, i) && s->enabled ? 2U : 0U) |
                (speed_valid(in, i) ? 4U : 0U) | (fault_valid(in, i) ? 8U : 0U) |
                (in->source_valid && s->parameter_verified ? 16U : 0U) |
                (in->source_valid && s->stop_latched ? 32U : 0U) |
                (in->source_valid && s->disable_verified ? 64U : 0U) |
                ((in->selected & (1U << i)) != 0U ? 128U : 0U));
            p[1] = in->source_valid && s->parameter_verified ? drive_mode(s->control_mode) : 255U;
            can_protocol_write_u16(p + 2U, speed_valid(in, i) ? (uint16_t)s->actual_speed_rpm : 0x8000U);
            can_protocol_write_u16(p + 4U, fault_valid(in, i) ? s->fault_code : 65535U);
            break;
        }
        case 0x188U:
            p[0] = in->life; p[1] = in->drive; p[2] = in->owner; p[3] = in->stop_reason;
            can_protocol_write_u16(p + 4U, in->token);
            p[6] = in->gates & 31U; p[7] = in->runtime;
            break;
        default: return false;
    }
    return true;
}

bool app_can_status_record(const app_can_status_input_t *in,
    uint8_t type, can_protocol_record_t *record)
{
    uint8_t *p = record->payload, i;
    const remote_servo_status_t *r = &in->remote;
    const position_monitor_status_t *v = &in->position;
    memset(record, 0, sizeof(*record));
    record->type = type;
    switch (type)
    {
        case 0x02U:
            record->length = 32U;
            can_protocol_write_u32(p, in->boot_id);
            can_protocol_write_u32(p + 4U, in->upper_boot_id);
            can_protocol_write_u32(p + 8U, app_can_capabilities());
            can_protocol_write_u32(p + 12U, app_can_data_sources());
            can_protocol_write_u32(p + 16U, in->config_revision);
            p[20] = 1U; p[23] = in->selected;
            break;
        case 0x10U:
            record->length = 80U;
            app_can_config_encode(p, in->config_revision);
            break;
        case 0x11U:
            record->length = 40U;
            can_protocol_write_u32(p, in->arbiter_generation);
            p[6] = in->owner; p[7] = in->life; p[8] = in->work; p[9] = in->drive;
            p[10] = (in->runtime & 135U) != 0U ? in->arbiter_axes : 0U;
            p[11] = in->gates & 31U;
            can_protocol_write_u32(p + 12U, (in->can_link_ready ? 4U : 0U) |
                (owner_authorized(in, APP_CONTROL_OWNER_CLI) ? 32U : 0U) |
                (owner_authorized(in, APP_CONTROL_OWNER_REMOTE) ? 64U : 0U) |
                (hmi_authorized(in) ? 128U : 0U) |
                ((in->runtime & 16U) != 0U ? 256U : 0U) |
                (control_fresh(in) && (in->lock_reason != APP_CONTROL_LOCK_NONE ||
                    r->estop_latched || in->cli.state == CLI_SESSION_LOCKED) ? 512U : 0U));
            can_protocol_write_u32(p + 16U, in->owner == 2U ? in->cli.remaining_ms :
                owner_authorized(in, APP_CONTROL_OWNER_REMOTE) ? UINT32_MAX - 1U : 0U);
            can_protocol_write_u16(p + 20U, 65535U);
            can_protocol_write_u16(p + 22U, 65535U);
            can_protocol_write_u32(p + 28U, in->owner == 2U ? in->cli.session_id : 0U);
            can_protocol_write_u16(p + 32U, 65535U);
            p[35] = 255U;
            can_protocol_write_u32(p + 36U, in->now_ms);
            if (control_fresh(in) && (in->arbiter_owner == APP_CONTROL_OWNER_CAN ||
                (in->arbiter_owner == APP_CONTROL_OWNER_NONE &&
                 in->stop_source == APP_CONTROL_OWNER_CAN && !in->control.stop_external)))
            {
                const app_can_control_status_t *c = &in->control;
                can_protocol_write_u32(p, c->generation);
                can_protocol_write_u16(p + 4U, c->motion_token);
                p[10] = owner_authorized(in, APP_CONTROL_OWNER_CAN) ? c->axis_mask : 0U;
                can_protocol_write_u32(p + 16U, c->remaining_ms);
                can_protocol_write_u16(p + 20U, c->speed_seen ? c->speed_sequence : 65535U);
                can_protocol_write_u16(p + 22U, c->speed_seen ? age16(c->speed_age_ms) : 65535U);
                can_protocol_write_u16(p + 24U, c->limit_mm_s);
                can_protocol_write_u16(p + 26U, c->detail);
                can_protocol_write_u16(p + 32U, c->stop_seen ? c->stop_sequence : 65535U);
                p[34] = c->stop_phase; p[35] = c->stop_result;
            }
            if (control_fresh(in) && in->can_link_ready && in->control.strategy_ready)
                can_protocol_write_u32(p + 12U, can_protocol_read_u32(p + 12U) | (1UL << 10U));
            if (owner_authorized(in, APP_CONTROL_OWNER_CAN))
                can_protocol_write_u32(p + 12U, can_protocol_read_u32(p + 12U) | 8U);
            if (in->control.stop_seen && control_fresh(in))
            {
                can_protocol_write_u16(p + 32U, in->control.stop_sequence);
                if (in->stop_source == APP_CONTROL_OWNER_CAN)
                { p[34] = in->control.stop_phase; p[35] = in->control.stop_result; }
            }
            if (in->arbiter_owner == APP_CONTROL_OWNER_HMI && hmi_authorized(in))
            {
                uint32_t age = in->now_ms - in->snapshot_ms;
                can_protocol_write_u32(p + 16U,
                    in->hmi_remaining_ms > age ? in->hmi_remaining_ms - age : 0U);
            }
            if (in->stop_source != APP_CONTROL_OWNER_CAN && hmi_stop_selected(in) &&
                (in->arbiter_owner == APP_CONTROL_OWNER_NONE ||
                 (in->arbiter_owner == APP_CONTROL_OWNER_HMI &&
                  (in->stop.result == 2U || in->stop.result == 5U) &&
                  in->stop.generation == in->arbiter_generation)))
            {
                p[34] = hmi_stop_phase(in);
                p[35] = p[34] >= 1U && p[34] <= 4U ? 0U : p[34] == 5U ? 12U : 255U;
            }
            if (!control_fresh(in))
            {
                memset(p, 0, 6U);
                p[10] = 0U;
                can_protocol_write_u32(p + 16U, UINT32_MAX);
                p[34] = p[35] = 255U;
            }
            break;
        case 0x12U:
            record->length = 40U;
            can_protocol_write_u32(p, in->now_ms);
            can_protocol_write_u32(p + 4U, in->reset_reason);
            can_protocol_write_u32(p + 8U, in->diagnostic);
            can_protocol_write_u32(p + 12U, in->failed_heartbeat_mask);
            can_protocol_write_u32(p + 16U, in->nrf_generation);
            can_protocol_write_u32(p + 20U, in->bus.generation);
            can_protocol_write_u32(p + 24U, in->bus.bus_off_count);
            for (i = 0U; i < 4U; ++i) can_protocol_write_u16(p + 28U + i * 2U, in->heartbeat_ages[i]);
            can_protocol_write_u16(p + 36U, in->minimum_stack);
            can_protocol_write_u16(p + 38U, in->minimum_stack == 65535U ? 13U : 15U);
            break;
        case 0x13U:
            record->length = 40U;
            if (hmi_stop_selected(in))
            {
                uint8_t phase = hmi_stop_phase(in);
                uint32_t elapsed = in->stop.result == 2U ? in->now_ms - in->stop.started_ms :
                    in->stop.completed_ms - in->stop.started_ms;
                can_protocol_write_u32(p, in->stop.event);
                p[4] = control_fresh(in) ? hmi_stop_reason(in->stop.reason) : 255U;
                p[5] = phase;
                p[6] = phase == 255U ? 0U : (uint8_t)in->stop.zero_mask;
                p[7] = phase == 5U ? in->selected : 0U;
                for (i = 0U; i < 2U; ++i)
                    can_protocol_write_u16(p + 8U + i * 2U,
                        control_fresh(in) && in->arbiter_owner == APP_CONTROL_OWNER_HMI &&
                        (in->hmi.output_mask & (1U << i)) ? (uint16_t)in->hmi.output[i] :
                        phase == 4U && (in->selected & (1U << i)) ? 0U : 0x8000U);
                can_protocol_write_u16(p + 12U, 0x8000U);
                can_protocol_write_u16(p + 14U, 0x8000U);
                can_protocol_write_u32(p + 16U,
                    control_fresh(in) ? in->stop.deceleration_mm_s2 : UINT32_MAX);
                can_protocol_write_u32(p + 20U, UINT32_MAX);
                can_protocol_write_u32(p + 24U, UINT32_MAX);
                can_protocol_write_u32(p + 28U,
                    phase == 4U || phase == 5U ? elapsed : UINT32_MAX);
                can_protocol_write_u32(p + 32U, phase == 255U ? UINT32_MAX :
                    in->stop.result == 2U && in->stop.timeout_ms > elapsed ?
                    in->stop.timeout_ms - elapsed : 0U);
                can_protocol_write_u16(p + 36U, 65535U);
                break;
            }
            if (in->stop_source == APP_CONTROL_OWNER_CAN && in->control.stop_generation &&
                control_fresh(in) &&
                !in->remote.hold_stopping && (in->runtime & 64U) == 0U)
            {
                const app_can_control_status_t *c = &in->control;
                can_protocol_write_u32(p, c->stop_generation);
                p[4] = can_stop_reason(c->stop_reason); p[5] = c->stop_phase;
                p[6] = c->zero_mask; p[7] = c->failed_mask;
                can_protocol_write_u16(p + 8U, (uint16_t)c->commanded_rpm[0]);
                can_protocol_write_u16(p + 10U, (uint16_t)c->commanded_rpm[1]);
                can_protocol_write_u16(p + 12U, 0x8000U); can_protocol_write_u16(p + 14U, 0x8000U);
                can_protocol_write_u32(p + 16U, (uint32_t)(c->stop_deceleration * 1000.0f));
                can_protocol_write_u32(p + 20U, c->first_command_ms);
                can_protocol_write_u32(p + 24U, c->zero_ms);
                can_protocol_write_u32(p + 28U, c->complete_ms);
                can_protocol_write_u32(p + 32U, c->stop_timeout_ms > c->stop_elapsed_ms ? c->stop_timeout_ms - c->stop_elapsed_ms : 0U);
                can_protocol_write_u16(p + 36U, 65535U);
                break;
            }
            can_protocol_write_u32(p, r->stop_generation);
            p[4] = remote_reason(r->controlled_stop_reason);
            p[5] = r->controlled_stop_state == REMOTE_CONTROLLED_STOP_DECELERATING ? 1U :
                r->controlled_stop_state == REMOTE_CONTROLLED_STOP_VERIFYING ||
                r->controlled_stop_state == REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING ? 2U :
                r->controlled_stop_state == REMOTE_CONTROLLED_STOP_DISABLING ? 3U :
                r->controlled_stop_state == REMOTE_CONTROLLED_STOP_DONE ? 4U :
                r->controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED ? 5U : 0U;
            p[6] = r->stop_zero_mask;
            p[7] = p[5] == 5U ? r->stop_axis_mask : 0U;
            can_protocol_write_u16(p + 8U, in->source_valid ? (uint16_t)r->left_commanded_rpm : 0x8000U);
            can_protocol_write_u16(p + 10U, in->source_valid ? (uint16_t)r->right_commanded_rpm : 0x8000U);
            can_protocol_write_u16(p + 12U, 0x8000U);
            can_protocol_write_u16(p + 14U, 0x8000U);
            can_protocol_write_u32(p + 16U, in->source_valid && p[5] != 0U ?
                unsigned32((double)r->stop_deceleration_mps2 * 1000.0) : UINT32_MAX);
            can_protocol_write_u32(p + 20U, r->stop_first_command_valid ? r->stop_first_command_ms : UINT32_MAX);
            can_protocol_write_u32(p + 24U, r->stop_zero_time_valid ? r->stop_zero_ms : UINT32_MAX);
            can_protocol_write_u32(p + 28U, r->stop_complete_time_valid ? r->stop_complete_ms : UINT32_MAX);
            can_protocol_write_u32(p + 32U, r->stop_timeout_ms > r->stop_elapsed_ms ? r->stop_timeout_ms - r->stop_elapsed_ms : 0U);
            can_protocol_write_u16(p + 36U, 65535U);
            /* 看门狗恢复只覆盖当前活动流程，不能把旧恢复起点混入后来的遥控停车。 */
            if (in->source_valid && (in->runtime & 64U) != 0U)
            {
                uint8_t phase = 4U, zero_mask = 0U, failed_mask = 0U;
                uint16_t ramp = 65535U;
                uint32_t remaining = UINT32_MAX;
                bool ramp_seen = false, ramp_equal = true;
                p[4] = 8U;
                for (i = 0U; i < 2U; ++i)
                {
                    const servo_wdg_recovery_status_t *w = &in->watchdog[i];
                    uint8_t axis_phase;
                    uint32_t axis_remaining;
                    if ((in->selected & (1U << i)) == 0U || !w->eligible ||
                        w->state == SERVO_WDG_RECOVERY_SKIPPED) continue;
                    can_protocol_write_u16(p + 12U + i * 2U, (uint16_t)w->start_speed_rpm);
                    p[38U + i] = w->zero_samples;
                    if (w->stopped) zero_mask |= (uint8_t)(1U << i);
                    if (w->state == SERVO_WDG_RECOVERY_FAILED)
                        failed_mask |= (uint8_t)(1U << i);
                    axis_phase = w->state == SERVO_WDG_RECOVERY_DONE ? 4U :
                        w->state >= SERVO_WDG_RECOVERY_PREPARE_SPEED ? 3U :
                        w->state == SERVO_WDG_RECOVERY_CONFIRM_ZERO ? 2U :
                        w->state == SERVO_WDG_RECOVERY_DECELERATING ? 1U : 2U;
                    if (axis_phase < phase) phase = axis_phase;
                    if (w->ramp_verified)
                    {
                        if (ramp_seen && ramp != w->decel_readback_ms) ramp_equal = false;
                        ramp = w->decel_readback_ms;
                        ramp_seen = true;
                    }
                    else if (w->state != SERVO_WDG_RECOVERY_DONE) ramp_equal = false;
                    if (w->state != SERVO_WDG_RECOVERY_DONE && w->stop_timeout_ms != 0U)
                    {
                        axis_remaining = w->stop_timeout_ms > w->stop_elapsed_ms ?
                            w->stop_timeout_ms - w->stop_elapsed_ms : 0U;
                        if (axis_remaining < remaining) remaining = axis_remaining;
                    }
                }
                p[5] = failed_mask != 0U ? 5U : phase;
                p[6] = zero_mask;
                p[7] = failed_mask;
                can_protocol_write_u32(p + 16U, UINT32_MAX);
                can_protocol_write_u32(p + 20U, UINT32_MAX);
                can_protocol_write_u32(p + 24U, UINT32_MAX);
                can_protocol_write_u32(p + 28U, UINT32_MAX);
                can_protocol_write_u32(p + 32U, remaining);
                can_protocol_write_u16(p + 36U,
                    ramp_seen && ramp_equal ? ramp : 65535U);
            }
            if (!control_fresh(in))
            {
                p[4] = p[5] = 255U;
                p[6] = p[7] = 0U;
                can_protocol_write_u16(p + 8U, 0x8000U);
                can_protocol_write_u16(p + 10U, 0x8000U);
                memset(p + 12U, 255, 28U);
                can_protocol_write_u16(p + 12U, 0x8000U);
                can_protocol_write_u16(p + 14U, 0x8000U);
            }
            break;
        case 0x20U:
        case 0x21U:
        {
            const position_axis_status_t *a;
            const servo_position_sample_t *sample = NULL;
            uint16_t flags;
            uint8_t j;
            i = type == 0x20U ? 0U : 1U;
            a = &v->axis[i];
            record->length = 56U;
            can_protocol_write_u32(p, (uint32_t)a->raw_position);
            can_protocol_write_u32(p + 4U, a->sequence);
            can_protocol_write_u32(p + 8U, a->generation);
            /* 仅上传序号及代次完全匹配的应答时间戳，不反推显示更新时间。 */
            for (j = 0U; j < SERVO_POSITION_SAMPLE_CAPACITY; ++j)
            {
                const servo_position_sample_t *s = &in->axis[i].position_samples[j];
                if (s->valid && s->position_sequence == a->sequence &&
                    s->position_generation == a->generation &&
                    s->actual_position == a->raw_position) sample = s;
            }
            can_protocol_write_u32(p + 12U, sample != NULL ?
                (uint32_t)sample->sample_tick * portTICK_PERIOD_MS : 0U);
            can_protocol_write_u32(p + 16U, sample != NULL ?
                (uint32_t)((TickType_t)(in->sample_tick - sample->sample_tick)) *
                portTICK_PERIOD_MS : UINT32_MAX);
            flags = (uint16_t)((a->selected ? 1U : 0U) |
                (in->source_valid && sample != NULL && a->valid &&
                    (uint32_t)((TickType_t)(in->sample_tick - sample->sample_tick)) *
                        portTICK_PERIOD_MS <= APP_POSITION_MAX_AGE_MS ? 2U : 0U) |
                (a->incomplete ? 4U : 0U) | (a->trip_anchor_pending ? 8U : 0U) |
                (in->source_valid && in->axis[i].online ? 16U : 0U));
            can_protocol_write_u16(p + 20U, flags);
            p[22] = in->source_valid ? (uint8_t)a->state : 255U; p[23] = i;
            can_protocol_write_u64(p + 24U, (uint64_t)a->boot_net_counts);
            can_protocol_write_u64(p + 32U, (uint64_t)a->trip_net_counts);
            can_protocol_write_u64(p + 40U, a->boot_path_counts);
            can_protocol_write_u64(p + 48U, a->trip_path_counts);
            break;
        }
        case 0x22U:
        {
            bool saturated = false;
            uint32_t flags = (in->source_valid && v->vehicle_valid ? 1U : 0U) |
                (in->source_valid && v->pair_valid ? 2U : 0U) |
                (in->source_valid && v->rate_valid ? 4U : 0U) |
                (in->source_valid && v->skew_estimate_valid ? 8U : 0U) |
                (v->axis[0].incomplete || v->axis[1].incomplete ? 16U : 0U) |
                (v->enabled ? 32U : 0U) | (v->trip_reset_pending ? 64U : 0U) |
                ((uint32_t)v->source_mask << 8U);
            record->length = 104U;
            can_protocol_write_u32(p + 4U, v->baseline_generation);
            can_protocol_write_u32(p + 8U, v->authorization_generation);
            can_protocol_write_u32(p + 12U, v->pair_sequence[0]);
            can_protocol_write_u32(p + 16U, v->pair_sequence[1]);
            can_protocol_write_u16(p + 20U, age16(v->pair_age_ms));
            can_protocol_write_u16(p + 22U, age16(v->sample_skew_ms));
            can_protocol_write_u64(p + 24U, (uint64_t)v->difference_counts);
            can_protocol_write_u64(p + 32U, distance64(v->difference_mm, true, &saturated));
            can_protocol_write_u64(p + 40U, distance64(v->vehicle_boot_net_mm, true, &saturated));
            can_protocol_write_u64(p + 48U, distance64(v->vehicle_boot_path_mm, false, &saturated));
            can_protocol_write_u64(p + 56U, distance64(v->vehicle_trip_net_mm, true, &saturated));
            can_protocol_write_u64(p + 64U, distance64(v->vehicle_trip_path_mm, false, &saturated));
            can_protocol_write_u32(p + 72U, in->source_valid && v->rate_valid ?
                signed32(v->difference_rate_mm_s) : 0x80000000UL);
            can_protocol_write_u32(p + 76U, in->source_valid && v->skew_estimate_valid ?
                unsigned32(v->skew_estimate_mm) : UINT32_MAX);
            can_protocol_write_u32(p + 80U, v->lost_intervals);
            can_protocol_write_u32(p + 84U, v->trip_reset_count);
            can_protocol_write_u64(p + 88U, (uint64_t)v->relative_counts[0]);
            can_protocol_write_u64(p + 96U, (uint64_t)v->relative_counts[1]);
            can_protocol_write_u32(p, flags | (saturated ? 128U : 0U));
            break;
        }
        case 0x23U:
        {
            static const remote_sync_status_t empty_sync = {0};
            const remote_sync_status_t *sync = in->arbiter_owner == APP_CONTROL_OWNER_HMI ? &in->hmi.sync :
                in->arbiter_owner == APP_CONTROL_OWNER_CAN ? &in->control.sync :
                in->arbiter_owner == APP_CONTROL_OWNER_REMOTE ? &r->sync : &empty_sync;
            bool authorized = owner_authorized(in, in->arbiter_owner);
            bool valid = control_fresh(in) &&
                (in->arbiter_owner == APP_CONTROL_OWNER_HMI ||
                 in->arbiter_owner == APP_CONTROL_OWNER_CAN ||
                 in->arbiter_owner == APP_CONTROL_OWNER_REMOTE);
            bool compensation = valid && authorized && in->life != 4U;
            const servo_speed_sample_t *samples[2] = {NULL, NULL};
            bool pair = valid && sync->pair_valid &&
                sync->skew_ms <= APP_REMOTE_SERVO_SYNC_MAX_SKEW_MS;
            uint8_t j;
            for (i = 0U; i < 2U; ++i)
            {
                for (j = 0U; j < SERVO_SPEED_SAMPLE_CAPACITY; ++j)
                {
                    const servo_speed_sample_t *s = &in->axis[i].speed_samples[j];
                    if (s->valid && s->speed_sequence == sync->sequence[i])
                        samples[i] = s;
                }
                if (samples[i] == NULL || !in->axis[i].online ||
                    (uint32_t)((TickType_t)(in->sample_tick - samples[i]->sample_tick)) *
                    portTICK_PERIOD_MS > APP_REMOTE_SERVO_SYNC_MAX_AGE_MS) pair = false;
            }
            record->length = 32U;
            can_protocol_write_u32(p, (sync->enabled ? 1U : 0U) |
                (pair && compensation && sync->active ? 2U : 0U) | (pair ? 4U : 0U) |
                (pair && sync->effective_error == 0 ? 8U : 0U) |
                (pair && compensation && sync->state == REMOTE_SYNC_LIMITED ? 16U : 0U) |
                (pair ? 32U : 0U) |
                (valid && in->arbiter_owner == APP_CONTROL_OWNER_REMOTE ? 64U : 0U) |
                (valid && in->arbiter_owner == APP_CONTROL_OWNER_CAN &&
                    (app_can_capabilities() & (1UL << 22U)) != 0U ? 128U : 0U));
            for (i = 0U; i < 2U; ++i)
                can_protocol_write_u16(p + 4U + i * 2U,
                    pair ? (uint16_t)samples[i]->actual_speed_rpm : 0x8000U);
            can_protocol_write_u16(p + 8U, pair ? signed16(sync->raw_error) : 0x8000U);
            can_protocol_write_u16(p + 10U, pair ? signed16(sync->effective_error) : 0x8000U);
            can_protocol_write_u16(p + 12U, pair && compensation && sync->active ?
                (uint16_t)sync->integer_correction : 0x8000U);
            for (i = 0U; i < 2U; ++i)
                can_protocol_write_u16(p + 14U + i * 2U, valid ?
                    in->arbiter_owner == APP_CONTROL_OWNER_HMI ?
                        (in->hmi.output_mask & (1U << i) ? (uint16_t)in->hmi.output[i] : 0x8000U) :
                    in->arbiter_owner == APP_CONTROL_OWNER_CAN ? (uint16_t)in->control.commanded_rpm[i] :
                    (uint16_t)(i == 0U ? r->left_commanded_rpm : r->right_commanded_rpm) : 0x8000U);
            can_protocol_write_u16(p + 18U, pair ? age16(sync->skew_ms) : 65535U);
            can_protocol_write_u32(p + 20U, sync->sequence[0]);
            can_protocol_write_u32(p + 24U, sync->sequence[1]);
            can_protocol_write_u32(p + 28U, in->arbiter_owner == APP_CONTROL_OWNER_HMI ? in->hmi.last_sequence :
                in->arbiter_owner == APP_CONTROL_OWNER_CAN ? in->control.batch :
                in->arbiter_owner == APP_CONTROL_OWNER_REMOTE ? r->batch_id : 0U);
            break;
        }
        case 0x24U:
            record->length = 48U;
            can_protocol_write_u32(p, in->bus.rx_count);
            can_protocol_write_u32(p + 4U, in->bus.tx_success);
            can_protocol_write_u32(p + 8U, in->rejected + in->bus.rx_invalid);
            can_protocol_write_u32(p + 12U, in->bus.rx_dropped);
            can_protocol_write_u32(p + 16U, in->telemetry_dropped);
            can_protocol_write_u32(p + 20U, in->bus.bus_off_count);
            can_protocol_write_u32(p + 24U, in->reassembly_failed);
            can_protocol_write_u32(p + 28U, in->uart.dropped_bytes);
            can_protocol_write_u32(p + 32U, in->uart.dma_error_count);
            can_protocol_write_u32(p + 36U, in->modbus.timeouts);
            can_protocol_write_u32(p + 40U, in->modbus.crc_errors);
            can_protocol_write_u32(p + 44U, 7U);
            break;
        case 0x25U:
            record->length = 104U;
            can_protocol_write_u32(p, in->snapshot_sequence);
            can_protocol_write_u32(p + 4U, in->snapshot_ms);
            can_protocol_write_u32(p + 8U, in->boot_id);
            can_protocol_write_u32(p + 12U, control_fresh(in) ? 1U : 0U);
            p[16] = in->owner; p[17] = in->life; p[18] = in->selected;
            p[19] = in->runtime;
            can_protocol_write_u32(p + 20U, in->arbiter_generation);
            can_protocol_write_u16(p + 24U, !owner_authorized(in, in->arbiter_owner) ? 0U :
                in->arbiter_owner == APP_CONTROL_OWNER_CAN ?
                (uint16_t)in->control.target_mm_s : in->arbiter_owner == APP_CONTROL_OWNER_HMI ?
                signed16(rpm_mm_s(in->hmi.target, in->selected == 2U ? 1U : 0U)) :
                in->arbiter_owner == APP_CONTROL_OWNER_REMOTE &&
                in->remote.authorized ? signed16(in->remote.target_rpm *
                (double)APP_WHEEL_CIRCUMFERENCE_M * 1000.0 /
                (60.0 * (double)APP_GEAR_REDUCTION_RATIO)) : 0U);
            for (i = 0U; i < 2U; ++i)
            {
                can_protocol_write_u16(p + 26U + 2U * i,
                    speed_valid(in, i) ? signed16(in->axis[i].actual_speed_rpm) : 0x8000U);
                can_protocol_write_u32(p + 32U + 4U * i,
                    control_fresh(in) && in->axis[i].online && in->axis[i].position_valid &&
                    (in->selected & (1U << i)) &&
                    (uint64_t)(uint32_t)(in->sample_tick - in->axis[i].position_sample_tick) * portTICK_PERIOD_MS +
                    (uint32_t)(in->now_ms - in->snapshot_ms) <= APP_POSITION_MAX_AGE_MS ?
                    (uint32_t)in->axis[i].actual_position : 0x80000000UL);
            }
            {
                bool saturated = false;
                bool valid = control_fresh(in) && v->vehicle_valid &&
                    (uint64_t)v->pair_age_ms + (uint32_t)(in->now_ms - in->snapshot_ms) <= APP_POSITION_MAX_AGE_MS;
                can_protocol_write_u64(p + 44U, distance64(v->vehicle_boot_net_mm, true, &saturated));
                can_protocol_write_u64(p + 52U, distance64(v->vehicle_boot_path_mm, false, &saturated));
                can_protocol_write_u64(p + 60U, distance64(v->vehicle_trip_path_mm, false, &saturated));
                can_protocol_write_u32(p + 40U, valid && !saturated ? 1U : 0U);
            }
            {
                const remote_sync_status_t *sync = in->arbiter_owner == APP_CONTROL_OWNER_HMI ?
                    &in->hmi.sync : in->arbiter_owner == APP_CONTROL_OWNER_CAN ?
                    &in->control.sync : &in->remote.sync;
                bool valid = owner_authorized(in, in->arbiter_owner) && sync->pair_valid &&
                    sync->skew_ms <= APP_REMOTE_SERVO_SYNC_MAX_SKEW_MS &&
                    (in->arbiter_owner == APP_CONTROL_OWNER_REMOTE || in->arbiter_owner == APP_CONTROL_OWNER_HMI ||
                     in->arbiter_owner == APP_CONTROL_OWNER_CAN) &&
                    (uint64_t)sync->age_ms[0] + (uint32_t)(in->now_ms - in->snapshot_ms) <= APP_REMOTE_SERVO_SYNC_MAX_AGE_MS &&
                    (uint64_t)sync->age_ms[1] + (uint32_t)(in->now_ms - in->snapshot_ms) <= APP_REMOTE_SERVO_SYNC_MAX_AGE_MS;
                for (i = 0U; i < 2U; ++i)
                {
                    uint8_t j;
                    bool matched = false;
                    for (j = 0U; j < SERVO_SPEED_SAMPLE_CAPACITY; ++j)
                    {
                        const servo_speed_sample_t *sample = &in->axis[i].speed_samples[j];
                        if (sample->valid && sample->speed_sequence == sync->sequence[i] &&
                            (uint64_t)(TickType_t)(in->sample_tick - sample->sample_tick) * portTICK_PERIOD_MS +
                            (uint32_t)(in->now_ms - in->snapshot_ms) <= APP_REMOTE_SERVO_SYNC_MAX_AGE_MS)
                            matched = true;
                    }
                    if (!matched || !in->axis[i].online) valid = false;
                }
                can_protocol_write_u32(p + 68U, valid ? 1U : 0U);
                can_protocol_write_u16(p + 72U, valid ? signed16(sync->effective_error) : 0U);
                can_protocol_write_u16(p + 74U, valid && sync->active ? (uint16_t)sync->integer_correction : 0U);
                can_protocol_write_u16(p + 76U, age16(sync->skew_ms));
                can_protocol_write_u16(p + 78U, age16(sync->age_ms[0]));
                can_protocol_write_u16(p + 80U, age16(sync->age_ms[1]));
            }
            can_protocol_write_u32(p + 84U, in->diagnostic);
            can_protocol_write_u32(p + 88U, in->stop.event);
            can_protocol_write_u16(p + 92U, in->stop.result);
            can_protocol_write_u16(p + 94U, in->stop.error);
            can_protocol_write_u32(p + 96U, v->lost_intervals);
            can_protocol_write_u32(p + 100U, in->now_ms - in->snapshot_ms);
            break;
        case 0x26U:
            record->length = 64U;
            can_protocol_write_u32(p, in->snapshot_sequence);
            can_protocol_write_u32(p + 4U, in->snapshot_ms);
            for (i = 0U; i < 2U; ++i)
            {
                const servo_snapshot_t *axis = &in->axis[i];
                uint8_t field;
                uint8_t *output = p + 8U + i * 24U;
                output[0] = control_fresh(in) && axis->online && (in->selected & (1U << i)) ?
                    axis->diagnostic_valid_mask : 0U;
                output[1] = axis->diagnostic_stale_mask;
                output[2] = axis->online;
                can_protocol_write_u16(output + 4U, (uint16_t)axis->realtime_torque_tenths_percent);
                can_protocol_write_u16(output + 6U, axis->load_ratio_tenths_percent);
                can_protocol_write_u16(output + 8U, (uint16_t)axis->phase_current_deciamp);
                can_protocol_write_u16(output + 10U, axis->bus_voltage_decivolt);
                for (field = 0U; field < 4U; ++field)
                {
                    uint32_t age = (uint32_t)(in->sample_tick - axis->diagnostic_sample_tick[field]) *
                        portTICK_PERIOD_MS + (uint32_t)(in->now_ms - in->snapshot_ms);
                    if (age > APP_SERVO_DIAGNOSTIC_POLL_MS * 3U) output[1] |= (uint8_t)(1U << field);
                    can_protocol_write_u16(output + 12U + field * 2U, age16(
                        age));
                }
            }
            can_protocol_write_u32(p + 56U, in->boot_id);
            can_protocol_write_u32(p + 60U, in->now_ms - in->snapshot_ms);
            break;
        case 0x27U:
            /* Recovery diagnostics mirror the HMI 0x4B8 window and retain
             * the first-fault identity until a new boot. */
            record->length = 32U;
            can_protocol_write_u32(p, in->recovery.generation);
            p[4] = (uint8_t)in->recovery.state;
            p[5] = in->recovery.eligible;
            p[6] = in->recovery.failure;
            p[7] = in->selected;
            can_protocol_write_u32(p + 8U, in->recovery.healthy_ms);
            p[12] = in->recovery.first_axis;
            p[13] = in->recovery.first_item;
            can_protocol_write_u16(p + 14U, in->recovery.first_failures);
            can_protocol_write_u32(p + 16U, in->remote.stop_generation);
            p[20] = in->stop_source;
            p[21] = in->stop_reason;
            p[22] = in->control.stop_phase;
            p[23] = in->control.stop_result;
            can_protocol_write_u32(p + 24U, in->stop.event);
            can_protocol_write_u16(p + 28U, in->control.stop_timeout_ms > in->control.stop_elapsed_ms ?
                (uint16_t)(in->control.stop_timeout_ms - in->control.stop_elapsed_ms) : 0U);
            can_protocol_write_u16(p + 30U, in->hmi.error);
            break;
        case 0x50U:
            record->length = 32U;
            memset(p + 4U, 255, 24U);
            break;
        case 0x51U:
            return false;
        case 0x53U:
        {
            uint8_t flags = 0U;
            uint32_t reason = 1U;
            bool stationary = arm_stationary(in);
            if (in->remote.estop_latched || app_health_motion_diagnostic_fault(in->diagnostic))
                flags |= 2U;
            if (in->remote.estop_latched) { flags |= 4U; reason = 2U; }
            else if (app_health_motion_diagnostic_fault(in->diagnostic)) { reason = 3U; }
            else if (!in->source_valid) { reason = 5U; }
            else if (stationary) { reason = 1U; }
            if (in->source_valid && in->selected != 0U) flags |= 16U;
            if (stationary) flags |= 32U;
            {
                uint32_t signature = arm_safety_signature(in, flags, reason);
                if (s_arm_safety_sequence == 0U) s_arm_safety_sequence = 1U;
                else ++s_arm_safety_sequence;
                if (s_arm_safety_signature != signature)
                {
                    ++s_arm_safety_generation;
                    if (s_arm_safety_generation == 0U) s_arm_safety_generation = 1U;
                    s_arm_safety_signature = signature;
                }
                record->length = 32U;
                can_protocol_write_u32(p, s_arm_safety_sequence);
                can_protocol_write_u32(p + 4U, s_arm_safety_generation);
                p[8] = 0U; p[9] = 0U;
                can_protocol_write_u16(p + 10U, flags);
                can_protocol_write_u32(p + 12U, 0U);
                can_protocol_write_u32(p + 16U, reason);
                can_protocol_write_u32(p + 20U, in->source_valid ? 0U : UINT32_MAX);
                can_protocol_write_u16(p + 24U, 0U);
                can_protocol_write_u16(p + 26U, 0U);
                can_protocol_write_u32(p + 28U, 0U);
            }
            break;
        }
        case 0x52U:
            record->length = 24U;
            can_protocol_write_u16(p + 4U, 0x8000U);
            memset(p + 6U, 255, 18U);
            break;
        default: return false;
    }
    return true;
}
