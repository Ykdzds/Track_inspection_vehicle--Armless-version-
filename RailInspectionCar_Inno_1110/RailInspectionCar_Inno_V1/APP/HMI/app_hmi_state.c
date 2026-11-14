#include "app_hmi_state.h"

#include <limits.h>
#include <string.h>
#include "app_control_arbiter.h"
#include "app_health.h"

static void put32(uint16_t *r, uint16_t address, uint32_t value)
{
    r[address] = (uint16_t)(value >> 16U);
    r[address + 1U] = (uint16_t)value;
}

static uint16_t age16(uint32_t value)
{
    return value == UINT32_MAX ? 0xFFFFU :
        value > 65534U ? 65534U : (uint16_t)value;
}

static uint16_t signed16(double value)
{
    if (value != value || value > 32766.0 || value < -32768.0) return 0x7FFFU;
    return (uint16_t)(int16_t)value;
}

static uint32_t signed32(double value, uint8_t *valid)
{
    if (value != value || value > 2147483646.0 || value < -2147483648.0)
    {
        *valid = 0U;
        return 0x7FFFFFFFUL;
    }
    return (uint32_t)(int32_t)value;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
        ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint8_t speed_valid(const hmi_state_input_t *in, uint8_t axis)
{
    const servo_snapshot_t *a = &in->axis[axis];
    return in->axes_valid && (in->selected & (1U << axis)) &&
        a->online && a->speed_valid && a->speed_age_ms < APP_SERVO_STATUS_MAX_AGE_MS;
}

static uint8_t fault_valid(const hmi_state_input_t *in, uint8_t axis)
{
    const servo_snapshot_t *a = &in->axis[axis];
    return in->axes_valid && (in->selected & (1U << axis)) &&
        a->online && a->fault_valid && a->fault_age_ms < APP_SERVO_STATUS_MAX_AGE_MS;
}

static uint8_t position_valid(const hmi_state_input_t *in, uint8_t axis)
{
    const servo_snapshot_t *a = &in->axis[axis];
    return in->axes_valid && (in->selected & (1U << axis)) && a->online &&
        a->position_valid && a->position_age_ms <= APP_POSITION_MAX_AGE_MS;
}

static uint16_t mode_code(servo_control_mode_t mode)
{
    switch (mode)
    {
        case SERVO_CONTROL_RS485_VELOCITY: return 1U;
        case SERVO_CONTROL_RS485_SEGMENT_POSITION: return 2U;
        case SERVO_CONTROL_PULSE_POSITION: return 3U;
        default: return 0U;
    }
}

static uint16_t reset_code(uint32_t flags)
{
    if (flags & (1UL << 29U)) return 4U;
    if (flags & (1UL << 30U)) return 5U;
    if (flags & (1UL << 28U)) return 3U;
    if (flags & (1UL << 27U)) return 1U;
    if (flags & (1UL << 26U)) return 2U;
    if (flags & (1UL << 25U)) return 1U;
    return flags ? 6U : 0U;
}

static uint16_t owner_code(uint16_t owner)
{
    switch (owner)
    {
        case APP_CONTROL_OWNER_NONE: return 0U;
        case APP_CONTROL_OWNER_HMI: return 1U;
        case APP_CONTROL_OWNER_REMOTE: return 2U;
        case APP_CONTROL_OWNER_AUTO: return 3U;
        case APP_CONTROL_OWNER_CAN: return 3U;
        case APP_CONTROL_OWNER_CLI: return 4U;
        default: return 0xFFFFU;
    }
}

static uint8_t can_session(const hmi_state_input_t *in)
{
    return in->can_valid && in->can.enabled && in->can.task_created &&
        in->can.phase == CAN_LINK_READY && in->can.bus.phase == BSP_CAN_READY;
}

static uint32_t upper_sources(const hmi_state_input_t *in)
{
    const can_upper_snapshot_t *u = &in->upper[0];
    return can_session(in) && u->present && u->valid && u->length == 32U ?
        le32(u->payload + 8U) : 0U;
}

static uint8_t upper_valid(const hmi_state_input_t *in, uint8_t index)
{
    const can_upper_snapshot_t *u = &in->upper[index];
    static const uint8_t source_bits[HMI_UPPER_COUNT] = {0U, 19U, 16U, 17U, 15U, 18U, 20U};
    uint16_t length = index == 4U ? 48U : index == 6U ? 24U : 32U;
    if (index != 0U && (!(upper_sources(in) & (1UL << source_bits[index])) ||
        !(le32(in->upper[0].payload + 12U) & (1UL << source_bits[index])))) return 0U;
    return can_session(in) && u->present && u->valid && u->length == length;
}

static double axis_mmps(uint8_t axis, int32_t rpm)
{
    double diameter = axis == 0U ? APP_POSITION_LEFT_WHEEL_DIAMETER_MM : APP_POSITION_RIGHT_WHEEL_DIAMETER_MM;
    double ratio = axis == 0U ? (double)APP_POSITION_LEFT_GEAR_DEN / APP_POSITION_LEFT_GEAR_NUM :
        (double)APP_POSITION_RIGHT_GEAR_DEN / APP_POSITION_RIGHT_GEAR_NUM;
    int sign = axis == 0U ? APP_POSITION_LEFT_SIGN : APP_POSITION_RIGHT_SIGN;
    return rpm * 3.14159265358979323846 * diameter * ratio * sign / 60.0;
}

static uint16_t failed_task_code(uint16_t task)
{
    switch (task)
    {
        case APP_HEARTBEAT_MOTION: return 0U;
        case APP_HEARTBEAT_SERVO_COMM: return 1U;
        case APP_HEARTBEAT_HEALTH: return 2U;
        case APP_HEARTBEAT_NRF: return 3U;
        case APP_HEARTBEAT_MODBUS: return 4U;
        default: return 0xFFFFU;
    }
}

static uint16_t axis_command(const hmi_state_input_t *in, uint8_t axis)
{
    if (!(in->selected & (1U << axis))) return 0x7FFFU;
    if (in->owner == APP_CONTROL_OWNER_HMI && (in->hmi_control.output_mask & (1U << axis)))
        return signed16(axis_mmps(axis, in->hmi_control.output[axis]));
    if (in->owner == APP_CONTROL_OWNER_CAN)
        return signed16(axis_mmps(axis, in->can_control.commanded_rpm[axis]));
    if (in->remote_valid && (in->remote.authorized || in->remote.hold_stopping))
        return signed16(axis_mmps(axis, axis == 0U ? in->remote.left_commanded_rpm :
            in->remote.right_commanded_rpm));
    if (in->cli_valid && in->debug_valid && in->cli.authorized &&
        in->cli.axis == axis && in->debug.mode == SERVO_DEBUG_MODE_RS485_SPEED)
        return signed16(axis_mmps(axis, in->debug.target_rpm));
    if (in->k1.valid || (in->startup_valid && in->hold[axis].verified)) return 0U;
    if (in->motion == MOTION_RUNNING || in->motion == MOTION_STOPPING)
        return signed16((double)in->motion_velocity[axis] * 1000.0);
    return 0x7FFFU;
}

/* 只读样本配对不调用P补偿状态机，不消费控制器的待配对队列。 */
static uint8_t paired_speed(const hmi_state_input_t *in,
uint32_t ages[2], uint32_t *skew, int32_t *difference, double *vehicle)
{
    unsigned l, r;
    uint32_t best = UINT32_MAX, freshest = UINT32_MAX;
    TickType_t now = in->now_tick;
    if (in->selected != 3U || !speed_valid(in, 0U) || !speed_valid(in, 1U)) return 0U;
    for (l = 0U; l < SERVO_SPEED_SAMPLE_CAPACITY; l++)
        for (r = 0U; r < SERVO_SPEED_SAMPLE_CAPACITY; r++)
        {
            const servo_speed_sample_t *a = &in->axis[0].speed_samples[l];
            const servo_speed_sample_t *b = &in->axis[1].speed_samples[r];
            TickType_t d = a->sample_tick - b->sample_tick;
            TickType_t reverse = b->sample_tick - a->sample_tick;
            uint32_t sa = (uint32_t)((uint64_t)(TickType_t)(now - a->sample_tick) * 1000U / configTICK_RATE_HZ);
            uint32_t sb = (uint32_t)((uint64_t)(TickType_t)(now - b->sample_tick) * 1000U / configTICK_RATE_HZ);
            uint32_t gap = (uint32_t)((uint64_t)(d < reverse ? d : reverse) * 1000U / configTICK_RATE_HZ);
            uint32_t age = sa > sb ? sa : sb;
            if (!a->valid || !b->valid || age >= APP_REMOTE_SERVO_SYNC_MAX_AGE_MS) continue;
            if (gap < best || (gap == best && age < freshest))
            {
                best = gap;
                freshest = age;
                ages[0] = sa; ages[1] = sb;
                *difference = (int32_t)a->actual_speed_rpm - b->actual_speed_rpm;
                *vehicle = (axis_mmps(0U, a->actual_speed_rpm) + axis_mmps(1U, b->actual_speed_rpm)) / 2.0;
            }
        }
    *skew = best;
    return best <= APP_REMOTE_SERVO_SYNC_MAX_SKEW_MS;
}

static void alarm_offer(uint16_t *level, uint16_t *code, uint16_t *source,
                        int32_t *parameter, uint16_t next_level,
                        uint16_t next_code, uint16_t next_source, int32_t detail)
{
    if (next_level > *level)
    {
        *level = next_level; *code = next_code; *source = next_source;
        *parameter = detail;
    }
}

void app_hmi_state_alarm_observe(const hmi_state_input_t *in, hmi_alarm_state_t *alarm)
{
    uint16_t level = 0U, code = 0U, source = 0U, summary = 0U;
    int32_t parameter = 0;
    uint8_t axis, known;
    if (in == NULL || alarm == NULL) return;
    known = in->axes_valid && in->remote_valid && in->startup_valid &&
        in->health_valid && in->position_valid && in->k1_valid;
    if (in->safety_fault_reason != MOTION_FAULT_REASON_NONE)
    {
        summary |= 1U << 9U;
        alarm_offer(&level, &code, &source, &parameter, 4U, 0x0404U, 1U,
            (int32_t)(((uint32_t)in->safety_stop_class << 16U) | in->safety_fault_reason));
    }
    if (in->remote_valid && in->remote.estop_latched && in->remote.stop_reason != REMOTE_STOP_LINK)
    {
        summary |= 1U << 1U;
        alarm_offer(&level, &code, &source, &parameter, 4U,
            in->remote.stop_reason == REMOTE_STOP_K1 ? 0x0101U : 0x0102U,
            5U, (int32_t)in->remote.stop_reason);
    }
    for (axis = 0U; axis < 2U; axis++)
    {
        if (!(in->selected & (1U << axis))) continue;
        if (!fault_valid(in, axis)) known = 0U;
        if (fault_valid(in, axis) && in->axis[axis].fault_code)
        {
            summary |= (uint16_t)(1U << (2U + axis));
            alarm_offer(&level, &code, &source, &parameter, 3U,
                (uint16_t)(0x0201U + axis), (uint16_t)(2U + axis), in->axis[axis].fault_code);
        }
        if (in->init_complete && (!speed_valid(in, axis) || !fault_valid(in, axis)))
        {
            summary |= 1U << 4U;
            alarm_offer(&level, &code, &source, &parameter, 3U, 0x0301U, 1U, axis);
        }
        if (in->startup_valid && (in->watchdog[axis].state == SERVO_WDG_RECOVERY_FAILED ||
            in->startup[axis].state == SERVO_STARTUP_STOP_FAILED ||
            in->hold[axis].state == SERVO_STARTUP_HOLD_FAILED ||
            in->boot_zero[axis].state == SERVO_BOOT_ZERO_FAILED))
        {
            summary |= 1U << 6U;
            alarm_offer(&level, &code, &source, &parameter, 3U, 0x0402U, 1U, axis);
        }
    }
    if (!in->identity_valid)
    {
        summary |= 1U << 6U;
        alarm_offer(&level, &code, &source, &parameter, 2U, 0x0403U, 1U, in->identity_result);
    }
    if (in->remote_valid &&
        ((!in->remote.link_valid && (in->remote.authorized || in->remote.hold_stopping)) ||
         (in->remote.estop_latched && in->remote.stop_reason == REMOTE_STOP_LINK)))
    {
        summary |= 1U;
        alarm_offer(&level, &code, &source, &parameter, 3U, 0x0303U, 4U, (int32_t)in->remote.last_frame_age_ms);
    }
    for (axis = 0U; axis < 2U; axis++)
        if (in->position_valid && (in->selected & (1U << axis)) && in->position.axis[axis].incomplete)
        {
            summary |= 1U << 10U;
            alarm_offer(&level, &code, &source, &parameter, 2U, 0x0601U, 1U, axis);
        }
    if (in->diagnostic)
    {
        uint16_t severity = (in->diagnostic & (APP_DIAG_STACK_OVERFLOW |
            APP_DIAG_ASSERT_FAILED | APP_DIAG_HEARTBEAT_TIMEOUT | APP_DIAG_WDG_TEST_REQUESTED)) ? 4U : 2U;
        summary |= 1U << 9U;
        alarm_offer(&level, &code, &source, &parameter, severity, 0x0401U, 6U, (int32_t)in->diagnostic);
    }
    if (in->remote_valid && in->remote.controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED)
    {
        summary |= 1U << 8U;
        alarm_offer(&level, &code, &source, &parameter, 4U, 0x0501U, 5U, (int32_t)in->remote.controlled_stop_reason);
    }
    if (in->can_valid && in->can.enabled && in->can.task_created && !can_session(in))
    {
        summary |= 1U << 5U;
        alarm_offer(&level, &code, &source, &parameter, 2U, 0x0302U, 1U, in->can.phase);
    }
    if (((upper_sources(in) & (1UL << 19U)) && !upper_valid(in, 1U)) ||
        ((upper_sources(in) & (1UL << 17U)) && !upper_valid(in, 3U))) known = 0U;
    if (upper_valid(in, 1U) && (le32(in->upper[1].payload + 28U) & 16U) &&
        le32(in->upper[1].payload + 20U))
    {
        summary |= 1U << 13U;
        alarm_offer(&level, &code, &source, &parameter, 3U, 0x0701U, 1U, (int32_t)le32(in->upper[1].payload + 20U));
    }
    if (upper_valid(in, 3U) && (in->upper[3].payload[21] == 5U || le32(in->upper[3].payload + 24U)))
    {
        summary |= 1U << 13U;
        alarm_offer(&level, &code, &source, &parameter, 3U, 0x0702U, 1U, (int32_t)le32(in->upper[3].payload + 24U));
    }
    if (!alarm->initialized || alarm->code != code || alarm->source != source ||
        alarm->level != level || alarm->parameter != parameter || alarm->valid != known)
    {
        alarm->sequence++;
        alarm->time_ms = in->now_ms;
        alarm->initialized = 1U;
    }
    alarm->code = code; alarm->source = source; alarm->level = level;
    alarm->parameter = parameter; alarm->active = level != 0U; alarm->valid = known;
    alarm->summary = summary;
    alarm->observed_ms = in->now_ms;
    alarm->observed_sequence = in->sequence;
}

static uint8_t alarm_fresh(const hmi_alarm_state_t *alarm, uint32_t read_ms)
{
    return alarm->initialized && (uint32_t)(read_ms - alarm->observed_ms) <=
        APP_SYSTEM_SNAPSHOT_MAX_AGE_MS;
}

static uint16_t alarm_level(const hmi_alarm_state_t *alarm, uint32_t read_ms)
{
    return alarm_fresh(alarm, read_ms) && (alarm->level || alarm->valid) ?
        alarm->level : 0xFFFFU;
}

static uint16_t alarm_code(const hmi_alarm_state_t *alarm, uint32_t read_ms)
{
    return alarm_level(alarm, read_ms) != 0xFFFFU ? alarm->code : 0xFFFFU;
}

/* Event identity/time stay latched; runtime stop evidence comes from this read. */
static void project_alarm(const hmi_state_input_t *in, const hmi_alarm_state_t *alarm,
                            uint32_t read_ms, uint16_t *r)
{
    uint8_t fresh = alarm_fresh(alarm, read_ms);
    uint16_t level = alarm_level(alarm, read_ms);
    r[0x90U] = (uint16_t)alarm->sequence;
    r[0x91U] = level;
    r[0x92U] = alarm_code(alarm, read_ms);
    r[0x93U] = level != 0xFFFFU ? alarm->source : 0xFFFFU;
    r[0x94U] = level != 0xFFFFU ? alarm->active : 0xFFFFU;
    r[0x95U] = 0U;
    put32(r, 0x96U, alarm->time_ms);
    put32(r, 0x98U, (uint32_t)alarm->parameter);
    r[0x9AU] = 0xFFFFU; /* No physical DI3 feedback. */
    r[0x9BU] = in->remote_valid ?
        in->remote.hold_stopping ? 1U :
        in->remote.controlled_stop_state == REMOTE_CONTROLLED_STOP_DONE && in->remote.stop_complete_time_valid ? 2U :
        in->remote.controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED ? 3U : 0U : 0xFFFFU;
    if (in->can_control.stop_phase && !in->can_control.stop_external &&
        (in->owner == APP_CONTROL_OWNER_CAN || in->owner == APP_CONTROL_OWNER_NONE))
        r[0x9BU] = in->can_control.stop_phase == 4U ? 2U :
            in->can_control.stop_phase == 5U ? 3U : 1U;
    r[0x9CU] = fresh && (alarm->valid || alarm->summary) ? alarm->summary : 0xFFFFU;
    r[0x9DU] = fresh && alarm->valid;
    put32(r, 0x9EU, in->sequence);
}

/* Retained full encoder is the regression reference; production window reads
 * use the const projection above and never advance the event sequence. */
static void build_alarm(const hmi_state_input_t *in, hmi_alarm_state_t *alarm,
                         uint16_t *r)
{
    app_hmi_state_alarm_observe(in, alarm);
    project_alarm(in, alarm, in->now_ms, r);
    r[0xCDU] = alarm_level(alarm, in->now_ms);
}

static void build_position(const hmi_state_input_t *in, uint16_t *r)
{
    const position_monitor_status_t *p = &in->position;
    uint8_t axis, valid, complete = 1U, all = 1U, distance_valid;
    uint32_t max_age = 0U, latest = 0U, net, path, trip;
    for (axis = 0U; axis < 2U; axis++)
    {
        valid = position_valid(in, axis);
        put32(r, (uint16_t)(0x70U + 2U * axis), valid ? (uint32_t)in->axis[axis].actual_position : 0x7FFFFFFFUL);
        r[0x8CU + axis] = valid;
        if (!(in->selected & (1U << axis))) continue;
        if (!valid) all = 0U;
        if (in->axis[axis].position_age_ms > max_age) max_age = in->axis[axis].position_age_ms;
        if (in->axis[axis].position_sequence > latest) latest = in->axis[axis].position_sequence;
        if (p->axis[axis].incomplete) complete = 0U;
    }
    r[0x7CU] = all ? 1U : 0U;
    if (!complete) r[0x7CU] |= 4U;
    distance_valid = in->position_valid && p->enabled && p->vehicle_valid && all && complete &&
        p->vehicle_boot_path_mm >= 0.0 && p->vehicle_trip_path_mm >= 0.0;
    net = signed32(p->vehicle_boot_net_mm, &distance_valid);
    path = signed32(p->vehicle_boot_path_mm, &distance_valid);
    trip = signed32(p->vehicle_trip_path_mm, &distance_valid);
    if (distance_valid) r[0x7CU] |= 2U;
    put32(r, 0x74U, distance_valid ? net : 0x7FFFFFFFUL);
    put32(r, 0x76U, distance_valid ? path : 0x7FFFFFFFUL);
    put32(r, 0x78U, distance_valid ? trip : 0x7FFFFFFFUL);
    valid = in->position_valid && p->pair_valid && p->pair_age_ms <= APP_POSITION_MAX_AGE_MS;
    net = signed32(p->difference_mm, &valid);
    put32(r, 0x7AU, valid ? net : 0x7FFFFFFFUL);
    /* bit3为向后兼容补充：遥控相对位置差基准有效，不影响原bit0至2。 */
    if (valid) r[0x7CU] |= 8U;
    r[0x7DU] = age16(max_age);
    r[0x7EU] = (uint16_t)latest;
    r[0x7FU] = in->position_valid ? (uint16_t)p->baseline_generation : 0U;
}

static void build_sync(const hmi_state_input_t *in, uint16_t *r)
{
    const remote_sync_status_t *sync = in->owner == APP_CONTROL_OWNER_HMI ? &in->hmi_control.sync :
        in->owner == APP_CONTROL_OWNER_CAN ? &in->can_control.sync : &in->remote.sync;
    uint32_t ages[2] = {UINT32_MAX, UINT32_MAX}, skew = UINT32_MAX;
    int32_t difference = 0, effective;
    double vehicle = 0.0;
    uint8_t over;
    uint8_t paired = paired_speed(in, ages, &skew, &difference, &vehicle);
    uint8_t active = in->selected == 3U && speed_valid(in, 0U) && speed_valid(in, 1U) &&
        ((in->owner == APP_CONTROL_OWNER_HMI && (in->hmi_control.flags & 2U)) ||
         (in->owner == APP_CONTROL_OWNER_CAN && in->can_control.authorized) ||
         (in->remote_valid && in->remote.authorized)) &&
        sync->active && sync->pair_valid &&
        sync->age_ms[0] < APP_REMOTE_SERVO_SYNC_MAX_AGE_MS &&
        sync->age_ms[1] < APP_REMOTE_SERVO_SYNC_MAX_AGE_MS;
    if (active)
    {
        difference = sync->raw_error;
        skew = sync->skew_ms;
        ages[0] = sync->age_ms[0]; ages[1] = sync->age_ms[1];
        paired = 1U;
    }
    over = paired && (difference > APP_REMOTE_SERVO_SYNC_ERROR_RPM || difference < -APP_REMOTE_SERVO_SYNC_ERROR_RPM);
    r[0x80U] = (APP_REMOTE_SERVO_SYNC_ENABLED ? 1U : 0U) |
        (active ? 2U : 0U) | (over ? 4U : 0U) |
        (active && sync->state == REMOTE_SYNC_LIMITED ? 8U : 0U);
    r[0x81U] = !APP_REMOTE_SERVO_SYNC_ENABLED || in->selected != 3U ? 0U :
        in->remote_valid && sync->state == REMOTE_SYNC_FAULT ? 4U :
        active ? (sync->state == REMOTE_SYNC_LIMITED ? 3U : 2U) : 1U;
    r[0x82U] = paired;
    r[0x83U] = paired ? 0U : in->selected != 3U ? 4U :
        !speed_valid(in, 0U) || !speed_valid(in, 1U) ? 2U : skew != UINT32_MAX ? 3U : 5U;
    effective = difference > APP_REMOTE_SERVO_SYNC_DEADBAND_RPM ? difference - APP_REMOTE_SERVO_SYNC_DEADBAND_RPM :
        difference < -APP_REMOTE_SERVO_SYNC_DEADBAND_RPM ? difference + APP_REMOTE_SERVO_SYNC_DEADBAND_RPM : 0;
    r[0x84U] = paired ? signed16((double)difference) : 0x7FFFU;
    r[0x85U] = paired ? signed16(active ? (double)sync->effective_error : (double)effective) : 0x7FFFU;
    r[0x86U] = active ? (uint16_t)sync->integer_correction : 0x7FFFU;
    r[0x87U] = age16(skew);
    r[0x88U] = age16(paired ? ages[0] : in->axis[0].speed_age_ms);
    r[0x89U] = age16(paired ? ages[1] : in->axis[1].speed_age_ms);
    put32(r, 0x8AU, in->sequence);
    r[0x8EU] = in->axes_valid && in->remote_valid;
}

static uint16_t build_upper(const hmi_state_input_t *in, uint16_t *r)
{
    uint32_t sources = upper_sources(in), flags;
    uint16_t caps = 0U;
    const uint8_t *p;
    uint8_t i;
    r[0xA0U] = 0U; r[0xA1U] = 255U;
    r[0xA2U] = 0U; r[0xA3U] = 0xFFFFU;
    put32(r, 0xA4U, 0x7FFFFFFFUL); put32(r, 0xA6U, 0x7FFFFFFFUL);
    r[0xA8U] = 0U; r[0xA9U] = 0U; r[0xAAU] = 0U;
    r[0xABU] = 0xFFFFU; r[0xACU] = 1U;
    r[0xB0U] = 0xFFFFU; r[0xB1U] = 0xFFFFU;
    r[0xB2U] = !in->can_valid || !in->can.enabled ? 0U :
        can_session(in) ? 1U : in->can.bus.phase == BSP_CAN_BACKOFF ? 3U : 2U;
    r[0xB3U] = (sources & ((1UL << 17U) | (1UL << 18U))) != 0U &&
        (!(sources & (1UL << 17U)) || upper_valid(in, 3U)) &&
        (!(sources & (1UL << 18U)) || upper_valid(in, 5U));
    put32(r, 0xB4U, in->sequence);
    if (in->can_valid && in->can.enabled && in->can.task_created) caps |= 1U << 10U;
    if (sources & (1UL << 16U))
    {
        caps |= 1U << 7U; r[0xAAU] = 0xFFFFU;
        if (upper_valid(in, 2U))
        {
            p = in->upper[2].payload; flags = le32(p + 28U);
            if ((flags & 1U) != 0U)
                r[0xAAU] = le16(p + 6U) != 0U ? 3U : (flags & 2U) ? 2U : 1U;
        }
    }
    if (sources & (1UL << 17U))
    {
        caps |= 1U << 9U; r[0xB1U] = 0xFFFFU;
        if (upper_valid(in, 3U))
        {
            p = in->upper[3].payload;
            if (p[20] == 0U) r[0xB1U] = 0U;
            else if (p[20] != 255U && (p[23] & 1U))
                r[0xB1U] = p[21] == 5U || le32(p + 24U) ? 3U :
                    le32(p + 8U) == 0U ? 2U : 1U;
        }
    }
    if (sources & (1UL << 18U))
    {
        caps |= 1U << 8U; r[0xB0U] = 0xFFFFU;
        if (upper_valid(in, 5U))
        {
            p = in->upper[5].payload; flags = le32(p + 28U);
            if ((flags & 16U) && le32(p + 20U)) r[0xB0U] = 3U;
            else if (flags & 1U) r[0xB0U] = (flags & 6U) ? 2U : 1U;
        }
    }
    if (sources & (1UL << 15U))
    {
        caps |= 1U << 4U; r[0xA2U] = 1U;
        if (upper_valid(in, 4U))
        {
            p = in->upper[4].payload;
            r[0xA2U] = le32(p + 44U) ? 3U : p[2] && (p[3] & 3U) ? 2U : 1U;
            if (p[3] & 8U) r[0xA3U] = age16(le32(p + 24U));
            /* CAN为经纬或轨道坐标，不能冒充现有本地X/Y坐标。 */
        }
    }
    if ((sources & (1UL << 25U)) != 0U &&
        (le32(in->upper[0].payload + 12U) & (1UL << 25U)) != 0U)
    {
        uint8_t arm_valid = in->arm_valid[0] && in->arm_valid[1] &&
            in->arm[0].valid && in->arm[1].valid;
        uint8_t fault = 0U, moving = 0U;
        for (i = 0U; i < CAN_ARM_COUNT; ++i)
        {
            if (!in->arm_valid[i] || !in->arm[i].valid ||
                in->arm[i].sample_age_ms > 1000U)
                continue;
            if ((in->arm[i].flags & (1U << 4U)) != 0U ||
                in->arm[i].state == 5U) fault = 1U;
            if ((in->arm[i].flags & (1U << 2U)) != 0U ||
                in->arm[i].state == 4U) moving = 1U;
        }
        caps |= 1U << 5U;
        r[0xA8U] = !arm_valid ? 0xFFFFU : fault ? 3U : moving ? 2U : 1U;
    }
    if (((sources & (1UL << 17U)) && r[0xB1U] == 0xFFFFU) ||
        ((sources & (1UL << 18U)) && r[0xB0U] == 0xFFFFU)) r[0xB3U] = 0U;
    return caps;
}

static void build_details(const hmi_state_input_t *in, uint16_t *r)
{
    unsigned i, word;
    uint16_t a;
    r[0x300U] = 0x5249U; r[0x301U] = 1U;
    put32(r, 0x302U, in->boot_id); put32(r, 0x304U, in->sequence);
    put32(r, 0x306U, in->now_ms);
    r[0x308U] = in->selected; r[0x309U] = owner_code(in->owner);
    r[0x30AU] = in->can_valid ? (uint16_t)in->can.bus.phase : 0xFFFFU;
    r[0x30BU] = in->can_valid ? (uint16_t)in->can.phase : 0xFFFFU;
    put32(r, 0x30CU, in->can_valid ? in->can.upper_boot_id : 0U);
    put32(r, 0x30EU, in->can_valid ? in->can.generation : 0U);
    put32(r, 0x310U, in->can_valid ? in->can.bus.rx_count : 0U);
    put32(r, 0x312U, in->can_valid ? in->can.bus.tx_success : 0U);
    put32(r, 0x314U, in->can_valid ? in->can.bus.tx_failed : 0U);
    put32(r, 0x316U, in->can_valid ? in->can.bus.bus_off_count : 0U);
    put32(r, 0x318U, in->can_valid ? in->can.upper_invalid : 0U);
    r[0x31AU] = in->can_valid ? age16(in->now_ms - in->can.last_service_ms) : 0xFFFFU;
    r[0x31BU] = age16(in->heartbeat_ms[3]);
    r[0x31CU] = in->can_valid ? 1U : 0U;
    r[0x31DU] = 0U; /* 无实体DI3、STO、制动反馈能力。 */

    r[0x31EU] = in->remote_valid ? in->remote.controlled_stop_reason : 0xFFFFU;
    r[0x31FU] = in->remote_valid ? in->remote.controlled_stop_state : 0xFFFFU;
    r[0x320U] = in->remote_valid ? in->remote.stop_zero_mask : 0U;
    r[0x321U] = in->k1.state;
    r[0x322U] = in->k1.valid; r[0x323U] = in->k1.confirmed_mask;
    put32(r, 0x324U, in->remote.stop_elapsed_ms);
    put32(r, 0x326U, in->remote.stop_timeout_ms);
    put32(r, 0x328U, in->remote.stop_first_command_valid ? in->remote.stop_first_command_ms : UINT32_MAX);
    put32(r, 0x32AU, in->remote.stop_zero_time_valid ? in->remote.stop_zero_ms : UINT32_MAX);
    put32(r, 0x32CU, in->remote.stop_complete_time_valid ? in->remote.stop_complete_ms : UINT32_MAX);
    r[0x32EU] = in->cli_valid ? in->cli.state : 0xFFFFU;
    r[0x32FU] = in->cli_valid ? in->cli.reason : 0xFFFFU;
    for (i = 0U; i < 2U; i++)
    {
        a = (uint16_t)(0x330U + 8U * i);
        r[a] = in->startup_valid ? in->startup[i].state : 0xFFFFU;
        r[a + 1U] = in->startup_valid ? in->hold[i].state : 0xFFFFU;
        r[a + 2U] = in->startup_valid ? in->watchdog[i].state : 0xFFFFU;
        r[a + 3U] = in->startup_valid ? in->watchdog[i].failure_reason : 0xFFFFU;
        r[a + 4U] = in->startup_valid ? in->watchdog[i].zero_samples : 0U;
        r[a + 5U] = in->startup_valid ? in->watchdog[i].decel_readback_ms : 0xFFFFU;
        r[a + 6U] = in->startup_valid ? (uint16_t)in->watchdog[i].start_speed_rpm : 0x7FFFU;
        r[a + 7U] = in->startup_valid ? in->boot_zero[i].state : 0xFFFFU;
    }
    r[0x340U] = in->position_valid ? in->position.state : 0xFFFFU;
    r[0x341U] = in->position_valid && in->position.pair_valid;
    r[0x342U] = in->position_valid && in->position.vehicle_valid;
    r[0x343U] = in->position_valid ? in->position.source_mask : 0U;
    put32(r, 0x344U, in->position.baseline_generation);
    put32(r, 0x346U, in->position.lost_intervals);
    put32(r, 0x348U, in->position.pair_age_ms);
    r[0x34AU] = in->position_valid ? in->position.axis[0].state : 0xFFFFU;
    r[0x34BU] = in->position_valid ? in->position.axis[1].state : 0xFFFFU;
    r[0x34CU] = in->position_valid ? in->position.axis[0].incomplete : 0U;
    r[0x34DU] = in->position_valid ? in->position.axis[1].incomplete : 0U;
    put32(r, 0x34EU, in->axes_valid ? in->axis[0].position_sequence : 0U);
    put32(r, 0x350U, in->axes_valid ? in->axis[1].position_sequence : 0U);
    put32(r, 0x352U, in->axes_valid ? in->axis[0].position_age_ms : UINT32_MAX);
    put32(r, 0x354U, in->axes_valid ? in->axis[1].position_age_ms : UINT32_MAX);
    r[0x356U] = in->position_valid;
    r[0x357U] = 0U; /* 未实现告警历史，不声明历史能力。 */

    r[0x358U] = in->remote_valid ? in->remote.authorized : 0U;
    r[0x359U] = in->cli_valid ? in->cli.authorized : 0U;
    put32(r, 0x35AU, in->cli_valid ? in->cli.session_id : 0U);
    r[0x35CU] = in->axes_valid ? age16(in->axis[0].fault_age_ms) : 0xFFFFU;
    r[0x35DU] = in->axes_valid ? age16(in->axis[1].fault_age_ms) : 0xFFFFU;
    r[0x35EU] = in->remote_valid ? in->remote.estop_latched : 0xFFFFU;
    r[0x35FU] = in->init_complete;
    /* 扩展区保存规范化的原CAN记录；不以半条记录或过期载荷刷新有效性。 */
    for (i = 0U; i < HMI_UPPER_COUNT; i++)
    {
        const can_upper_snapshot_t *u = &in->upper[i];
        a = (uint16_t)(0x360U + 32U * i);
        r[a] = i == 0U ? 2U : (uint16_t)(0x3FU + i);
        r[a + 1U] = u->present;
        r[a + 2U] = upper_valid(in, (uint8_t)i);
        r[a + 3U] = u->present ? u->length : 0U;
        r[a + 4U] = u->sequence;
        r[a + 5U] = u->present ? age16(u->age_ms) : 0xFFFFU;
        put32(r, (uint16_t)(a + 6U), u->present ? u->received_ms : UINT32_MAX);
        for (word = 0U; word < 24U; word++)
            r[a + 8U + word] = u->present && word * 2U + 1U < u->length ?
                le16(u->payload + word * 2U) : 0xFFFFU;
    }
}

void app_hmi_state_build(const hmi_state_input_t *in, hmi_alarm_state_t *alarm,
                         uint16_t r[HMI_STATE_REGISTER_COUNT])
{
    uint8_t i, selected, online = 1U, healthy = 1U, moving = 0U;
    uint16_t gates = 0U, caps, target0, target1;
    uint32_t ages[2] = {UINT32_MAX, UINT32_MAX}, skew = UINT32_MAX;
    int32_t difference = 0;
    double vehicle = 0.0;
    uint8_t paired;
    if (in == NULL || alarm == NULL || r == NULL) return;
    memset(r, 0, HMI_STATE_REGISTER_COUNT * sizeof(*r));
    for (i = 0U; i < 2U; i++)
    {
        const servo_snapshot_t *a = &in->axis[i];
        uint16_t base = i == 0U ? 0x20U : 0x2AU;
        selected = (in->selected & (1U << i)) != 0U;
        if (selected && !speed_valid(in, i)) online = 0U;
        if (selected && (!fault_valid(in, i) || a->fault_code)) healthy = 0U;
        if (selected && speed_valid(in, i) && a->actual_speed_rpm) moving = 1U;
        r[8U + i] = selected && in->axes_valid ? (a->online ? 1U : 0U) : 0xFFFFU;
        r[10U + i] = fault_valid(in, i) ? a->fault_code : 0xFFFFU;
        r[base] = selected && in->axes_valid && a->online ? a->status_word : 0xFFFFU;
        r[base + 1U] = selected && in->axes_valid && a->parameter_verified ? mode_code(a->control_mode) : 0U;
        r[base + 2U] = speed_valid(in, i) ? signed16(axis_mmps(i, a->actual_speed_rpm)) : 0x7FFFU;
        r[base + 3U] = axis_command(in, i);
        put32(r, (uint16_t)(base + 4U), in->cli_valid && in->debug_valid &&
            in->cli.axis == i && in->debug.mode == SERVO_DEBUG_MODE_RS485_POSITION ?
            (uint32_t)in->debug.target : 0x7FFFFFFFUL);
        put32(r, (uint16_t)(base + 6U), position_valid(in, i) ? (uint32_t)a->actual_position : 0x7FFFFFFFUL);
        r[base + 8U] = r[10U + i];
        /* 软件使能配置来自既有驱动状态，不冒充独立硬件SON反馈。 */
        r[base + 9U] = fault_valid(in, i) && !a->busy && a->enable_feedback_valid &&
            a->enable_feedback_age_ms < APP_SERVO_STATUS_MAX_AGE_MS &&
            a->enable_feedback == a->enabled ? (a->enable_feedback ? 1U : 0U) : 0xFFFFU;
        r[0xC4U + i] = selected && in->axes_valid ?
            (speed_valid(in, i) ? 1U : 0U) | (fault_valid(in, i) ? 2U : 0U) |
            (speed_valid(in, i) && !a->actual_speed_rpm ? 4U : 0U) : 0xFFFFU;
        r[0xC6U + i] = selected && in->axes_valid ? age16(a->speed_age_ms) : 0xFFFFU;
        r[0xC8U + i] = selected && in->axes_valid ? (uint16_t)a->poll_failures : 0U;
    }
    if (APP_SERVO_OUTPUT_POLARITY_CONFIRMED) gates |= 1U;
    if (APP_SERVO_DIRECTION_CONFIRMED) gates |= 2U;
    if (APP_SERVO_ADDRESS_CONFIRMED) gates |= 4U;
    if (APP_SERVO_PULSE_FORMAT_CONFIRMED) gates |= 8U;
    if (APP_SERVO_ELECTRONIC_GEAR_CONFIRMED) gates |= 16U;
    if (APP_SERVO_SOFT_ENABLE_CONFIRMED) gates |= 32U;
    if (APP_SERVO_MAINTENANCE_STOP_CONFIRMED) gates |= 64U;
    if (online) gates |= 128U;
    if (healthy) gates |= 256U;
    if (in->safety_ready) gates |= 512U;
    r[0U] = in->init_complete ? 1U : 0U;
    if (in->init_complete && in->safety_ready) r[0U] = 2U;
    if (moving || in->motion == MOTION_RUNNING) r[0U] = 3U;
    if (in->remote_valid && in->remote.hold_stopping) r[0U] = 4U;
    if (in->hmi_control.flags & 4U) r[0U] = 4U;
    if (in->cli_valid && (in->cli.state == CLI_SESSION_STOPPING || in->cli.state == CLI_SESSION_RESTORING)) r[0U] = 4U;
    if (in->motion == MOTION_STOPPING) r[0U] = 4U;
    if (in->can_control.stop_phase >= 1U && in->can_control.stop_phase <= 3U &&
        !in->can_control.stop_external) r[0U] = 4U;
    if (in->can_control.state == CAN_CONTROL_FAULT) r[0U] = 5U;
    if (in->init_complete && !online) r[0U] = 6U;
    if (in->motion == MOTION_FAULT || (in->diagnostic & (APP_DIAG_HEARTBEAT_TIMEOUT | APP_DIAG_STACK_OVERFLOW | APP_DIAG_ASSERT_FAILED))) r[0U] = 5U;
    for (i = 0U; i < 2U; i++) if (fault_valid(in, i) && in->axis[i].fault_code) r[0U] = 5U;
    r[1U] = in->init_stage; r[2U] = in->init_progress;
    r[3U] = gates; r[0xCU] = gates;
    r[5U] = (uint16_t)in->diagnostic; r[6U] = (uint16_t)(in->diagnostic >> 16U);
    r[7U] = reset_code(in->reset_reason);
    r[0xDU] = APP_HMI_FW_VERSION; r[0xEU] = 1U;
    r[0xFU] = in->init_complete; r[0x10U] = (uint16_t)in->sequence;
    r[0x15U] = APP_HMI_CONTROL_ENABLED ? 1U : 0U;
    r[0x16U] = in->maintenance ? 2U : 0U;
    r[0x17U] = APP_HMI_CONTROL_ENABLED && (in->hmi_control.flags & 2U) ? 0U : 1U;
    put32(r, 0x18U, in->identity_valid ? in->boot_id : 0xFFFFFFFFUL); put32(r, 0x1AU, in->sequence);
    r[0x1CU] = in->identity_valid && in->health_valid && in->axes_valid &&
        in->startup_valid && in->remote_valid && in->cli_valid && in->owner_valid;
    paired = paired_speed(in, ages, &skew, &difference, &vehicle);
    r[0x34U] = 0x7FFFU;
    if (in->selected == 1U && speed_valid(in, 0U)) r[0x34U] = r[0x22U];
    if (in->selected == 2U && speed_valid(in, 1U)) r[0x34U] = r[0x2CU];
    if (paired) r[0x34U] = signed16(vehicle);
    if (!APP_SERVO_DIRECTION_CONFIRMED) r[0x34U] = 0x7FFFU;
    r[0x35U] = 0x7FFFU;
    r[0x36U] = in->maintenance ? 3U :
        in->cli_valid && in->cli.authorized && in->cli.mode != SERVO_DEBUG_MODE_RS485_SPEED &&
        in->cli.mode != SERVO_DEBUG_MODE_NONE ? 2U : moving ? 1U : 0U;
    r[0x37U] = 0xFFFFU;
    r[0x50U] = (uint16_t)in->uart.requests; r[0x51U] = (uint16_t)in->uart.responses;
    r[0x52U] = 0xFFFFU; r[0x53U] = (uint16_t)in->uart.partial_timeouts;
    r[0x54U] = (uint16_t)in->uart.crc_errors; r[0x55U] = (uint16_t)in->uart.frame_errors;
    r[0x56U] = (uint16_t)in->uart.exceptions; r[0x57U] = (uint16_t)in->uart.rx_errors;
    r[0x58U] = r[0xC6U]; r[0x59U] = r[0xC7U];
    for (i = 0U; i < 3U; i++) r[0x5AU + i] = age16(in->heartbeat_ms[i]);
    r[0x5DU] = in->watchdog_decision; r[0x5EU] = failed_task_code(in->failed_task);
    r[0x5FU] = in->minimum_stack;
    put32(r, 0x60U, in->uart.requests); put32(r, 0x62U, in->uart.responses);
    r[0x64U] = in->uart.request_seen ? age16(in->now_ms - in->uart.last_request_ms) : 0xFFFFU;
    r[0x65U] = (uint16_t)in->uart.rx_overflow; put32(r, 0x66U, in->sequence);
    build_position(in, r); build_sync(in, r); build_alarm(in, alarm, r);
    r[0xC3U] = in->position_valid ? (uint16_t)in->position.lost_intervals : 0U;
    caps = build_upper(in, r);
    if (APP_POSITION_MONITOR_ENABLED) caps |= 1U;
    if (in->selected == 3U) caps |= 2U;
    caps |= 4U | (1U << 11U) | (1U << 12U);
    r[0xC0U] = 0x5249U; r[0xC1U] = 1U; r[0xC2U] = caps;
    target0 = axis_command(in, 0U); target1 = axis_command(in, 1U);
    r[0xCAU] = in->selected == 1U ? target0 : in->selected == 2U ? target1 :
        target0 != 0x7FFFU && target1 != 0x7FFFU ? signed16(((int16_t)target0 + (int16_t)target1) / 2.0) : 0x7FFFU;
    r[0xCBU] = in->owner_valid ? owner_code(in->owner) : 0xFFFFU; r[0xCCU] = APP_HMI_CONTROL_ENABLED && (in->hmi_control.flags & 2U) ? 1U : 0U;
    r[0xCEU] = in->axes_valid && in->health_valid && in->position_valid;
    r[0xCFU] = in->owner_valid && in->remote_valid && in->cli_valid && in->debug_valid && in->k1_valid;
    r[4U] = r[0x91U] == 0xFFFFU ? 0xFFFFU : r[0x92U];
    r[0x200U] = 0x5249U; r[0x201U] = 1U;
    r[0x202U] = APP_HMI_CONTROL_ENABLED ? 255U : 1U;
    r[0x203U] = APP_HMI_DICTIONARY_REVISION;
    put32(r, 0x204U, in->identity_valid ? in->boot_id : 0xFFFFFFFFUL); put32(r, 0x206U, in->sequence);
    put32(r, 0x208U, in->now_ms); r[0x20AU] = APP_HMI_SNAPSHOT_PERIOD_MS;
    r[0x20BU] = APP_HMI_FW_PATCH; put32(r, 0x20CU, in->reset_reason);
    put32(r, 0x20EU, APP_HMI_CONTROL_ENABLED ? in->hmi_session : 0U);
    r[0x210U] = r[0xCBU];
    r[0x211U] = APP_HMI_CONTROL_ENABLED ? in->lock_reason : 1U;
    r[0x212U] = APP_HMI_CONTROL_ENABLED ? in->hmi_age : 0xFFFFU;
    r[0x213U] = APP_HMI_CONTROL_ENABLED ? in->hmi_remaining : 0U;
    r[0x214U] = APP_HMI_CONTROL_ENABLED ? in->selected : 0U;
    r[0x215U] = APP_HMI_CONTROL_ENABLED ? (uint16_t)APP_OPERATION_MAX_VELOCITY_MM_S : 0U;
    r[0x216U] = APP_HMI_CONTROL_ENABLED ? APP_HMI_SESSION_LEASE_MS : 0U;
    build_details(in, r);
    app_hmi_state_build_control(in, &r[HMI_CONTROL_REGISTER_START]);
}

/* A request-relative writer never forms pointers outside the caller's exact
 * output range. Both halves of a 32-bit field are bounded independently. */
typedef struct
{
    uint16_t *out;
    uint16_t first, quantity;
} hmi_read_window_t;

static void read_put16(hmi_read_window_t *w, uint16_t address, uint16_t value)
{
    if (address >= w->first && (uint16_t)(address - w->first) < w->quantity)
        w->out[address - w->first] = value;
}

static void read_or16(hmi_read_window_t *w, uint16_t address, uint16_t value)
{
    if (address >= w->first && (uint16_t)(address - w->first) < w->quantity)
        w->out[address - w->first] |= value;
}

static void read_put32(hmi_read_window_t *w, uint16_t address, uint32_t value)
{
    read_put16(w, address, (uint16_t)(value >> 16U));
    read_put16(w, (uint16_t)(address + 1U), (uint16_t)value);
}

static void read_project_alarm(const hmi_state_input_t *in, const hmi_alarm_state_t *alarm,
                            uint32_t read_ms, hmi_read_window_t *w)
{
    uint8_t fresh = alarm_fresh(alarm, read_ms);
    uint16_t level = alarm_level(alarm, read_ms);
    read_put16(w, 0x90U, (uint16_t)alarm->sequence);
    read_put16(w, 0x91U, level);
    read_put16(w, 0x92U, alarm_code(alarm, read_ms));
    read_put16(w, 0x93U, level != 0xFFFFU ? alarm->source : 0xFFFFU);
    read_put16(w, 0x94U, level != 0xFFFFU ? alarm->active : 0xFFFFU);
    read_put16(w, 0x95U, 0U);
    read_put32(w, 0x96U, alarm->time_ms);
    read_put32(w, 0x98U, (uint32_t)alarm->parameter);
    read_put16(w, 0x9AU, 0xFFFFU); /* No physical DI3 feedback. */
    read_put16(w, 0x9BU, in->remote_valid ?
        in->remote.hold_stopping ? 1U :
        in->remote.controlled_stop_state == REMOTE_CONTROLLED_STOP_DONE && in->remote.stop_complete_time_valid ? 2U :
        in->remote.controlled_stop_state == REMOTE_CONTROLLED_STOP_FAILED ? 3U : 0U : 0xFFFFU);
    if (in->can_control.stop_phase && !in->can_control.stop_external &&
        (in->owner == APP_CONTROL_OWNER_CAN || in->owner == APP_CONTROL_OWNER_NONE))
        read_put16(w, 0x9BU, in->can_control.stop_phase == 4U ? 2U :
            in->can_control.stop_phase == 5U ? 3U : 1U);
    read_put16(w, 0x9CU, fresh && (alarm->valid || alarm->summary) ? alarm->summary : 0xFFFFU);
    read_put16(w, 0x9DU, fresh && alarm->valid);
    read_put32(w, 0x9EU, in->sequence);
}

static void read_build_position(const hmi_state_input_t *in, hmi_read_window_t *w)
{
    const position_monitor_status_t *p = &in->position;
    uint8_t axis, valid, complete = 1U, all = 1U, distance_valid;
    uint32_t max_age = 0U, latest = 0U, net, path, trip;
    for (axis = 0U; axis < 2U; axis++)
    {
        valid = position_valid(in, axis);
        read_put32(w, (uint16_t)(0x70U + 2U * axis), valid ? (uint32_t)in->axis[axis].actual_position : 0x7FFFFFFFUL);
        read_put16(w, 0x8CU + axis, valid);
        if (!(in->selected & (1U << axis))) continue;
        if (!valid) all = 0U;
        if (in->axis[axis].position_age_ms > max_age) max_age = in->axis[axis].position_age_ms;
        if (in->axis[axis].position_sequence > latest) latest = in->axis[axis].position_sequence;
        if (p->axis[axis].incomplete) complete = 0U;
    }
    read_put16(w, 0x7CU, all ? 1U : 0U);
    if (!complete) read_or16(w, 0x7CU, 4U);
    distance_valid = in->position_valid && p->enabled && p->vehicle_valid && all && complete &&
        p->vehicle_boot_path_mm >= 0.0 && p->vehicle_trip_path_mm >= 0.0;
    net = signed32(p->vehicle_boot_net_mm, &distance_valid);
    path = signed32(p->vehicle_boot_path_mm, &distance_valid);
    trip = signed32(p->vehicle_trip_path_mm, &distance_valid);
    if (distance_valid) read_or16(w, 0x7CU, 2U);
    read_put32(w, 0x74U, distance_valid ? net : 0x7FFFFFFFUL);
    read_put32(w, 0x76U, distance_valid ? path : 0x7FFFFFFFUL);
    read_put32(w, 0x78U, distance_valid ? trip : 0x7FFFFFFFUL);
    valid = in->position_valid && p->pair_valid && p->pair_age_ms <= APP_POSITION_MAX_AGE_MS;
    net = signed32(p->difference_mm, &valid);
    read_put32(w, 0x7AU, valid ? net : 0x7FFFFFFFUL);
    /* bit3为向后兼容补充：遥控相对位置差基准有效，不影响原bit0至2。 */
    if (valid) read_or16(w, 0x7CU, 8U);
    read_put16(w, 0x7DU, age16(max_age));
    read_put16(w, 0x7EU, (uint16_t)latest);
    read_put16(w, 0x7FU, in->position_valid ? (uint16_t)p->baseline_generation : 0U);
}

static void read_build_sync(const hmi_state_input_t *in, hmi_read_window_t *w)
{
    const remote_sync_status_t *sync = in->owner == APP_CONTROL_OWNER_HMI ? &in->hmi_control.sync :
        in->owner == APP_CONTROL_OWNER_CAN ? &in->can_control.sync : &in->remote.sync;
    uint32_t ages[2] = {UINT32_MAX, UINT32_MAX}, skew = UINT32_MAX;
    int32_t difference = 0, effective;
    double vehicle = 0.0;
    uint8_t over;
    uint8_t paired = paired_speed(in, ages, &skew, &difference, &vehicle);
    uint8_t active = in->selected == 3U && speed_valid(in, 0U) && speed_valid(in, 1U) &&
        ((in->owner == APP_CONTROL_OWNER_HMI && (in->hmi_control.flags & 2U)) ||
         (in->owner == APP_CONTROL_OWNER_CAN && in->can_control.authorized) ||
         (in->remote_valid && in->remote.authorized)) &&
        sync->active && sync->pair_valid &&
        sync->age_ms[0] < APP_REMOTE_SERVO_SYNC_MAX_AGE_MS &&
        sync->age_ms[1] < APP_REMOTE_SERVO_SYNC_MAX_AGE_MS;
    if (active)
    {
        difference = sync->raw_error;
        skew = sync->skew_ms;
        ages[0] = sync->age_ms[0]; ages[1] = sync->age_ms[1];
        paired = 1U;
    }
    over = paired && (difference > APP_REMOTE_SERVO_SYNC_ERROR_RPM || difference < -APP_REMOTE_SERVO_SYNC_ERROR_RPM);
    read_put16(w, 0x80U, (APP_REMOTE_SERVO_SYNC_ENABLED ? 1U : 0U) |
        (active ? 2U : 0U) | (over ? 4U : 0U) |
        (active && sync->state == REMOTE_SYNC_LIMITED ? 8U : 0U));
    read_put16(w, 0x81U, !APP_REMOTE_SERVO_SYNC_ENABLED || in->selected != 3U ? 0U :
        in->remote_valid && sync->state == REMOTE_SYNC_FAULT ? 4U :
        active ? (sync->state == REMOTE_SYNC_LIMITED ? 3U : 2U) : 1U);
    read_put16(w, 0x82U, paired);
    read_put16(w, 0x83U, paired ? 0U : in->selected != 3U ? 4U :
        !speed_valid(in, 0U) || !speed_valid(in, 1U) ? 2U : skew != UINT32_MAX ? 3U : 5U);
    effective = difference > APP_REMOTE_SERVO_SYNC_DEADBAND_RPM ? difference - APP_REMOTE_SERVO_SYNC_DEADBAND_RPM :
        difference < -APP_REMOTE_SERVO_SYNC_DEADBAND_RPM ? difference + APP_REMOTE_SERVO_SYNC_DEADBAND_RPM : 0;
    read_put16(w, 0x84U, paired ? signed16((double)difference) : 0x7FFFU);
    read_put16(w, 0x85U, paired ? signed16(active ? (double)sync->effective_error : (double)effective) : 0x7FFFU);
    read_put16(w, 0x86U, active ? (uint16_t)sync->integer_correction : 0x7FFFU);
    read_put16(w, 0x87U, age16(skew));
    read_put16(w, 0x88U, age16(paired ? ages[0] : in->axis[0].speed_age_ms));
    read_put16(w, 0x89U, age16(paired ? ages[1] : in->axis[1].speed_age_ms));
    read_put32(w, 0x8AU, in->sequence);
    read_put16(w, 0x8EU, in->axes_valid && in->remote_valid);
}

static void read_build_control(const hmi_state_input_t *in,
                                 hmi_read_window_t *w)
{
    uint8_t i;
    read_put16(w, 0x0U, 0x534DU); read_put16(w, 0x1U, 2U);
    read_put32(w, 0x2U, in->boot_id); read_put32(w, 0x4U, in->sequence);
    read_put32(w, 0x6U, in->now_ms); read_put16(w, 0x8U, owner_code(in->owner));
    read_put16(w, 0x9U, in->selected); read_put16(w, 0xAU, in->hmi_control.state);
    read_put16(w, 0xBU, in->hmi_control.flags);
    read_put32(w, 0xCU, in->hmi_control.session);
    read_put16(w, 0xEU, in->hmi_remaining);
    read_put16(w, 0xFU, age16(in->hmi_control.target_age_ms));
    read_put32(w, 0x10U, in->hmi_control.last_sequence);
    read_put16(w, 0x12U, in->owner == APP_CONTROL_OWNER_NONE || in->owner == APP_CONTROL_OWNER_HMI ?
        (uint16_t)in->hmi_control.target : 0x7FFFU);
    for (i = 0U; i < 2U; ++i)
    {
        const servo_snapshot_t *axis = &in->axis[i];
        uint16_t bit = (uint16_t)(1U << i);
        read_put16(w, 0x13U + i, (in->selected & bit) && in->owner == APP_CONTROL_OWNER_HMI ?
            (uint16_t)in->hmi_control.target : 0x7FFFU);
        read_put16(w, 0x15U + i, in->hmi_control.output_mask & bit ?
            (uint16_t)in->hmi_control.output[i] : 0x7FFFU);
        read_put16(w, 0x17U + i, speed_valid(in, i) ? (uint16_t)axis->actual_speed_rpm : 0x7FFFU);
        if (speed_valid(in, i)) read_or16(w, 0x19U, bit);
        if (in->axes_valid && (in->selected & bit) && axis->online &&
            axis->enable_feedback_valid && axis->enable_feedback_age_ms < APP_SERVO_STATUS_MAX_AGE_MS)
        {
            read_or16(w, 0x1AU, bit);
            if (axis->enable_feedback) read_or16(w, 0x1BU, bit);
        }
        read_put16(w, 0x2EU + i, speed_valid(in, i) ? age16(axis->speed_age_ms) : 0xFFFFU);
    }
    /* Historical stop zero proof does not describe an active new session. */
    read_put16(w, 0x1CU, !in->hmi_control.session && in->hmi_control.stop.result == 3U ?
        in->hmi_control.stop.zero_mask : in->hmi_control.zero_mask);
    read_put16(w, 0x1DU, in->hmi_control.stop.reason);
    read_put16(w, 0x1EU, in->hmi_control.phase);
    read_put16(w, 0x1FU, in->hmi_control.stop.deceleration_mm_s2);
    read_put32(w, 0x20U, in->hmi_control.elapsed_ms);
    read_put32(w, 0x22U, in->hmi_control.stop.timeout_ms);
    read_put16(w, 0x24U, in->hmi_control.error); read_put16(w, 0x25U, in->lock_reason);
    read_put16(w, 0x26U, (uint16_t)-APP_OPERATION_MAX_MOTOR_RPM);
    read_put16(w, 0x27U, APP_OPERATION_MAX_MOTOR_RPM);
    read_put16(w, 0x28U, (uint16_t)((double)APP_DESIGN_ACCELERATION_MPS2 * 1000.0 + 0.5));
    read_put16(w, 0x29U, (uint16_t)(APP_DESIGN_DECELERATION_MPS2 * 1000.0f + 0.5f));
    read_put16(w, 0x2AU, APP_HMI_NORMAL_STOP_DECEL_MM_S2); read_put16(w, 0x2BU, APP_HMI_EMERGENCY_STOP_DECEL_MM_S2);
    read_put16(w, 0x2CU, APP_HMI_SPEED_HOLD_MAX_MS); read_put16(w, 0x2DU, APP_HMI_SESSION_LEASE_MS);
    read_put32(w, 0x30U, in->hmi_control.stop.event);
    read_put32(w, 0x32U, in->hmi_control.stop.request_sequence);
    read_put16(w, 0x34U, in->hmi_control.stop.command); read_put16(w, 0x35U, in->hmi_control.stop.result);
    read_put32(w, 0x36U, in->hmi_control.stop.completed_ms);
    /* 0x4B8-0x4BF is the reserved recovery mailbox/status window.  Keep the
     * layout compact and monotonic so the 26号 screen can render it without
     * polling hardware: version, state/eligibility, generation, failure,
     * healthy duration and first-fault summary. */
    read_put16(w, 0x58U, 1U);
    read_put16(w, 0x59U, (uint16_t)in->recovery.state);
    read_put16(w, 0x5AU, in->recovery.eligible ? 1U : 0U);
    read_put32(w, 0x5BU, in->recovery.generation);
    read_put16(w, 0x5DU, in->recovery.failure);
    read_put16(w, 0x5EU, in->recovery.first_axis | ((uint16_t)in->recovery.first_item << 8U));
    read_put16(w, 0x5FU, (uint16_t)(in->recovery.healthy_ms > 65534U ? 65534U : in->recovery.healthy_ms));
    read_put16(w, 0x38U, owner_code(in->hmi_control.stop.owner));
    read_put16(w, 0x39U, in->hmi_control.output_mask);
    read_put16(w, 0x3AU, in->lock_reason != APP_CONTROL_LOCK_NONE ? 0U :
        in->owner == APP_CONTROL_OWNER_CAN && in->can_control.authorized ?
        (uint16_t)in->can_control.target_mm_s : in->owner == APP_CONTROL_OWNER_HMI &&
        (in->hmi_control.flags & 2U) ? signed16(axis_mmps(in->selected == 2U ? 1U : 0U,
        in->hmi_control.target)) :
        in->owner == APP_CONTROL_OWNER_REMOTE && in->remote.authorized ?
        signed16(axis_mmps(in->selected == 2U ? 1U : 0U, in->remote.target_rpm)) : 0U);
    read_put16(w, 0x3BU, 1U);
    read_put16(w, 0x3CU, axis_command(in, 0U));
    read_put16(w, 0x3DU, axis_command(in, 1U));
    read_put16(w, 0x3EU, in->axes_valid ? 1U : 0U);
    read_put16(w, 0x3FU, APP_SYSTEM_SNAPSHOT_PERIOD_MS);
    for (i = 0U; i < 2U; ++i)
    {
        const servo_snapshot_t *axis = &in->axis[i];
        uint16_t offset = (uint16_t)(0x40U + i * 12U);
        uint8_t field;
        read_put16(w, offset, in->axes_valid && axis->online && (in->selected & (1U << i)) ?
            axis->diagnostic_valid_mask : 0U);
        read_put16(w, offset + 1U, axis->diagnostic_stale_mask);
        read_put16(w, offset + 2U, (uint16_t)axis->realtime_torque_tenths_percent);
        read_put16(w, offset + 3U, axis->load_ratio_tenths_percent);
        read_put16(w, offset + 4U, (uint16_t)axis->phase_current_deciamp);
        read_put16(w, offset + 5U, axis->bus_voltage_decivolt);
        for (field = 0U; field < 4U; ++field)
        {
            uint32_t age = (uint32_t)(in->now_tick - axis->diagnostic_sample_tick[field]) * portTICK_PERIOD_MS;
            read_put16(w, offset + 6U + field, age16(age));
            if (age > APP_SERVO_DIAGNOSTIC_POLL_MS * 3U)
                read_or16(w, offset + 1U, (uint16_t)(1U << field));
        }
        read_put16(w, offset + 10U, age16(axis->speed_age_ms));
        read_put16(w, offset + 11U, age16(axis->fault_age_ms));
    }
}

static void build_basic_window(const hmi_state_input_t *in,
    const hmi_alarm_state_t *alarm, uint32_t read_ms, hmi_read_window_t *w)
{
    uint8_t i, selected, online = 1U, healthy = 1U, moving = 0U;
    uint16_t gates = 0U;
    uint32_t ages[2] = {UINT32_MAX, UINT32_MAX}, skew = UINT32_MAX;
    int32_t difference = 0;
    double vehicle = 0.0;
    uint8_t paired;
    for (i = 0U; i < 2U; i++)
    {
        const servo_snapshot_t *a = &in->axis[i];
        uint16_t base = i == 0U ? 0x20U : 0x2AU;
        selected = (in->selected & (1U << i)) != 0U;
        if (selected && !speed_valid(in, i)) online = 0U;
        if (selected && (!fault_valid(in, i) || a->fault_code)) healthy = 0U;
        if (selected && speed_valid(in, i) && a->actual_speed_rpm) moving = 1U;
        read_put16(w, 8U + i, selected && in->axes_valid ? (a->online ? 1U : 0U) : 0xFFFFU);
        read_put16(w, 10U + i, fault_valid(in, i) ? a->fault_code : 0xFFFFU);
        read_put16(w, base, selected && in->axes_valid && a->online ? a->status_word : 0xFFFFU);
        read_put16(w, base + 1U, selected && in->axes_valid && a->parameter_verified ? mode_code(a->control_mode) : 0U);
        read_put16(w, base + 2U, speed_valid(in, i) ? signed16(axis_mmps(i, a->actual_speed_rpm)) : 0x7FFFU);
        read_put16(w, base + 3U, axis_command(in, i));
        read_put32(w, (uint16_t)(base + 4U), in->cli_valid && in->debug_valid &&
            in->cli.axis == i && in->debug.mode == SERVO_DEBUG_MODE_RS485_POSITION ?
            (uint32_t)in->debug.target : 0x7FFFFFFFUL);
        read_put32(w, (uint16_t)(base + 6U), position_valid(in, i) ? (uint32_t)a->actual_position : 0x7FFFFFFFUL);
        read_put16(w, base + 8U, fault_valid(in, i) ? a->fault_code : 0xFFFFU);
        /* 软件使能配置来自既有驱动状态，不冒充独立硬件SON反馈。 */
        read_put16(w, base + 9U, fault_valid(in, i) && !a->busy && a->enable_feedback_valid &&
            a->enable_feedback_age_ms < APP_SERVO_STATUS_MAX_AGE_MS &&
            a->enable_feedback == a->enabled ? (a->enable_feedback ? 1U : 0U) : 0xFFFFU);
    }
    if (APP_SERVO_OUTPUT_POLARITY_CONFIRMED) gates |= 1U;
    if (APP_SERVO_DIRECTION_CONFIRMED) gates |= 2U;
    if (APP_SERVO_ADDRESS_CONFIRMED) gates |= 4U;
    if (APP_SERVO_PULSE_FORMAT_CONFIRMED) gates |= 8U;
    if (APP_SERVO_ELECTRONIC_GEAR_CONFIRMED) gates |= 16U;
    if (APP_SERVO_SOFT_ENABLE_CONFIRMED) gates |= 32U;
    if (APP_SERVO_MAINTENANCE_STOP_CONFIRMED) gates |= 64U;
    if (online) gates |= 128U;
    if (healthy) gates |= 256U;
    if (in->safety_ready) gates |= 512U;
    read_put16(w, 0U, in->init_complete ? 1U : 0U);
    if (in->init_complete && in->safety_ready) read_put16(w, 0U, 2U);
    if (moving || in->motion == MOTION_RUNNING) read_put16(w, 0U, 3U);
    if (in->remote_valid && in->remote.hold_stopping) read_put16(w, 0U, 4U);
    if (in->hmi_control.flags & 4U) read_put16(w, 0U, 4U);
    if (in->cli_valid && (in->cli.state == CLI_SESSION_STOPPING || in->cli.state == CLI_SESSION_RESTORING)) read_put16(w, 0U, 4U);
    if (in->motion == MOTION_STOPPING) read_put16(w, 0U, 4U);
    if (in->can_control.stop_phase >= 1U && in->can_control.stop_phase <= 3U &&
        !in->can_control.stop_external) read_put16(w, 0U, 4U);
    if (in->can_control.state == CAN_CONTROL_FAULT) read_put16(w, 0U, 5U);
    if (in->init_complete && !online) read_put16(w, 0U, 6U);
    if (in->motion == MOTION_FAULT || (in->diagnostic & (APP_DIAG_HEARTBEAT_TIMEOUT | APP_DIAG_STACK_OVERFLOW | APP_DIAG_ASSERT_FAILED))) read_put16(w, 0U, 5U);
    for (i = 0U; i < 2U; i++) if (fault_valid(in, i) && in->axis[i].fault_code) read_put16(w, 0U, 5U);
    read_put16(w, 1U, in->init_stage); read_put16(w, 2U, in->init_progress);
    read_put16(w, 3U, gates); read_put16(w, 0xCU, gates);
    read_put16(w, 5U, (uint16_t)in->diagnostic); read_put16(w, 6U, (uint16_t)(in->diagnostic >> 16U));
    read_put16(w, 7U, reset_code(in->reset_reason));
    read_put16(w, 0xDU, APP_HMI_FW_VERSION); read_put16(w, 0xEU, 1U);
    read_put16(w, 0xFU, in->init_complete); read_put16(w, 0x10U, (uint16_t)in->sequence);
    read_put16(w, 0x15U, APP_HMI_CONTROL_ENABLED ? 1U : 0U);
    read_put16(w, 0x16U, in->maintenance ? 2U : 0U);
    read_put16(w, 0x17U, APP_HMI_CONTROL_ENABLED && (in->hmi_control.flags & 2U) ? 0U : 1U);
    read_put32(w, 0x18U, in->identity_valid ? in->boot_id : 0xFFFFFFFFUL); read_put32(w, 0x1AU, in->sequence);
    read_put16(w, 0x1CU, in->identity_valid && in->health_valid && in->axes_valid &&
        in->startup_valid && in->remote_valid && in->cli_valid && in->owner_valid);
    paired = paired_speed(in, ages, &skew, &difference, &vehicle);
    read_put16(w, 0x34U, 0x7FFFU);
    if (in->selected == 1U && speed_valid(in, 0U)) read_put16(w, 0x34U, signed16(axis_mmps(0U, in->axis[0].actual_speed_rpm)));
    if (in->selected == 2U && speed_valid(in, 1U)) read_put16(w, 0x34U, signed16(axis_mmps(1U, in->axis[1].actual_speed_rpm)));
    if (paired) read_put16(w, 0x34U, signed16(vehicle));
    if (!APP_SERVO_DIRECTION_CONFIRMED) read_put16(w, 0x34U, 0x7FFFU);
    read_put16(w, 0x35U, 0x7FFFU);
    read_put16(w, 0x36U, in->maintenance ? 3U :
        in->cli_valid && in->cli.authorized && in->cli.mode != SERVO_DEBUG_MODE_RS485_SPEED &&
        in->cli.mode != SERVO_DEBUG_MODE_NONE ? 2U : moving ? 1U : 0U);
    read_put16(w, 0x37U, 0xFFFFU);
    read_put16(w, 4U, alarm_code(alarm, read_ms));
}

static void build_communications_window(const hmi_state_input_t *in, hmi_read_window_t *w)
{
    uint8_t i;
    read_put16(w, 0x50U, (uint16_t)in->uart.requests); read_put16(w, 0x51U, (uint16_t)in->uart.responses);
    read_put16(w, 0x52U, 0xFFFFU); read_put16(w, 0x53U, (uint16_t)in->uart.partial_timeouts);
    read_put16(w, 0x54U, (uint16_t)in->uart.crc_errors); read_put16(w, 0x55U, (uint16_t)in->uart.frame_errors);
    read_put16(w, 0x56U, (uint16_t)in->uart.exceptions); read_put16(w, 0x57U, (uint16_t)in->uart.rx_errors);
    for (i = 0U; i < 2U; ++i)
        read_put16(w, 0x58U + i, in->axes_valid && (in->selected & (1U << i)) ?
            age16(in->axis[i].speed_age_ms) : 0xFFFFU);
    for (i = 0U; i < 3U; i++) read_put16(w, 0x5AU + i, age16(in->heartbeat_ms[i]));
    read_put16(w, 0x5DU, in->watchdog_decision); read_put16(w, 0x5EU, failed_task_code(in->failed_task));
    read_put16(w, 0x5FU, in->minimum_stack);
    read_put32(w, 0x60U, in->uart.requests); read_put32(w, 0x62U, in->uart.responses);
    read_put16(w, 0x64U, in->uart.request_seen ? age16(in->now_ms - in->uart.last_request_ms) : 0xFFFFU);
    read_put16(w, 0x65U, (uint16_t)in->uart.rx_overflow); read_put32(w, 0x66U, in->sequence);
}

/* Upper summary and capabilities own different dictionary windows. */
static void build_upper_summary_window(const hmi_state_input_t *in,
    const hmi_alarm_state_t *alarm, uint32_t read_ms, hmi_read_window_t *w)
{
    uint32_t sources = upper_sources(in), flags;
    const uint8_t *p;
    uint8_t i;
    read_project_alarm(in, alarm, read_ms, w);
    read_put16(w, 0xA0U, 0U); read_put16(w, 0xA1U, 255U);
    read_put16(w, 0xA2U, 0U); read_put16(w, 0xA3U, 0xFFFFU);
    read_put32(w, 0xA4U, 0x7FFFFFFFUL); read_put32(w, 0xA6U, 0x7FFFFFFFUL);
    read_put16(w, 0xA8U, 0U); read_put16(w, 0xA9U, 0U); read_put16(w, 0xAAU, 0U);
    read_put16(w, 0xABU, 0xFFFFU); read_put16(w, 0xACU, 1U);
    if (sources & (1UL << 16U))
    {
        read_put16(w, 0xAAU, 0xFFFFU);
        if (upper_valid(in, 2U))
        {
            p = in->upper[2].payload; flags = le32(p + 28U);
            if ((flags & 1U) != 0U)
                read_put16(w, 0xAAU, le16(p + 6U) != 0U ? 3U : (flags & 2U) ? 2U : 1U);
        }
    }
    if (sources & (1UL << 15U))
    {
        read_put16(w, 0xA2U, 1U);
        if (upper_valid(in, 4U))
        {
            p = in->upper[4].payload;
            read_put16(w, 0xA2U, le32(p + 44U) ? 3U : p[2] && (p[3] & 3U) ? 2U : 1U);
            if (p[3] & 8U) read_put16(w, 0xA3U, age16(le32(p + 24U)));
            /* CAN为经纬或轨道坐标，不能冒充现有本地X/Y坐标。 */
        }
    }
    if ((sources & (1UL << 25U)) != 0U &&
        (le32(in->upper[0].payload + 12U) & (1UL << 25U)) != 0U)
    {
        uint8_t arm_valid = in->arm_valid[0] && in->arm_valid[1] &&
            in->arm[0].valid && in->arm[1].valid;
        uint8_t fault = 0U, moving = 0U;
        for (i = 0U; i < CAN_ARM_COUNT; ++i)
        {
            if (!in->arm_valid[i] || !in->arm[i].valid ||
                in->arm[i].sample_age_ms > 1000U)
                continue;
            if ((in->arm[i].flags & (1U << 4U)) != 0U ||
                in->arm[i].state == 5U) fault = 1U;
            if ((in->arm[i].flags & (1U << 2U)) != 0U ||
                in->arm[i].state == 4U) moving = 1U;
        }
        read_put16(w, 0xA8U, !arm_valid ? 0xFFFFU : fault ? 3U : moving ? 2U : 1U);
    }
}

static void build_capabilities_window(const hmi_state_input_t *in,
    const hmi_alarm_state_t *alarm, uint32_t read_ms, hmi_read_window_t *w)
{
    uint32_t sources = upper_sources(in), flags;
    uint16_t caps = 0U, target0, target1, status0 = 0xFFFFU, status1 = 0xFFFFU;
    const uint8_t *p;
    uint8_t i, selected;
    status0 = 0xFFFFU; status1 = 0xFFFFU;
    read_put16(w, 0xB2U, !in->can_valid || !in->can.enabled ? 0U :
        can_session(in) ? 1U : in->can.bus.phase == BSP_CAN_BACKOFF ? 3U : 2U);
    read_put16(w, 0xB3U, (sources & ((1UL << 17U) | (1UL << 18U))) != 0U &&
        (!(sources & (1UL << 17U)) || upper_valid(in, 3U)) &&
        (!(sources & (1UL << 18U)) || upper_valid(in, 5U)));
    read_put32(w, 0xB4U, in->sequence);
    if (in->can_valid && in->can.enabled && in->can.task_created) caps |= 1U << 10U;
    if (sources & (1UL << 17U))
    {
        caps |= 1U << 9U; status1 = 0xFFFFU;
        if (upper_valid(in, 3U))
        {
            p = in->upper[3].payload;
            if (p[20] == 0U) status1 = 0U;
            else if (p[20] != 255U && (p[23] & 1U))
                status1 = p[21] == 5U || le32(p + 24U) ? 3U :
                    le32(p + 8U) == 0U ? 2U : 1U;
        }
    }
    if (sources & (1UL << 18U))
    {
        caps |= 1U << 8U; status0 = 0xFFFFU;
        if (upper_valid(in, 5U))
        {
            p = in->upper[5].payload; flags = le32(p + 28U);
            if ((flags & 16U) && le32(p + 20U)) status0 = 3U;
            else if (flags & 1U) status0 = (flags & 6U) ? 2U : 1U;
        }
    }
    if (((sources & (1UL << 17U)) && status1 == 0xFFFFU) ||
        ((sources & (1UL << 18U)) && status0 == 0xFFFFU)) read_put16(w, 0xB3U, 0U);
    read_put16(w, 0xB0U, status0); read_put16(w, 0xB1U, status1);
    if (sources & (1UL << 16U)) caps |= 1U << 7U;
    if (sources & (1UL << 15U)) caps |= 1U << 4U;
    if ((sources & (1UL << 25U)) &&
        (le32(in->upper[0].payload + 12U) & (1UL << 25U))) caps |= 1U << 5U;
    read_put16(w, 0xC3U, in->position_valid ? (uint16_t)in->position.lost_intervals : 0U);
    for (i = 0U; i < 2U; ++i)
    {
        const servo_snapshot_t *a = &in->axis[i];
        selected = (in->selected & (1U << i)) != 0U;
        read_put16(w, 0xC4U + i, selected && in->axes_valid ?
            (speed_valid(in, i) ? 1U : 0U) | (fault_valid(in, i) ? 2U : 0U) |
            (speed_valid(in, i) && !a->actual_speed_rpm ? 4U : 0U) : 0xFFFFU);
        read_put16(w, 0xC6U + i, selected && in->axes_valid ? age16(a->speed_age_ms) : 0xFFFFU);
        read_put16(w, 0xC8U + i, selected && in->axes_valid ? (uint16_t)a->poll_failures : 0U);
    }
    if (APP_POSITION_MONITOR_ENABLED) caps |= 1U;
    if (in->selected == 3U) caps |= 2U;
    caps |= 4U | (1U << 11U) | (1U << 12U);
    read_put16(w, 0xC0U, 0x5249U); read_put16(w, 0xC1U, 1U); read_put16(w, 0xC2U, caps);
    target0 = axis_command(in, 0U); target1 = axis_command(in, 1U);
    read_put16(w, 0xCAU, in->selected == 1U ? target0 : in->selected == 2U ? target1 :
        target0 != 0x7FFFU && target1 != 0x7FFFU ? signed16(((int16_t)target0 + (int16_t)target1) / 2.0) : 0x7FFFU);
    read_put16(w, 0xCBU, in->owner_valid ? owner_code(in->owner) : 0xFFFFU); read_put16(w, 0xCCU, APP_HMI_CONTROL_ENABLED && (in->hmi_control.flags & 2U) ? 1U : 0U);
    read_put16(w, 0xCEU, in->axes_valid && in->health_valid && in->position_valid);
    read_put16(w, 0xCFU, in->owner_valid && in->remote_valid && in->cli_valid && in->debug_valid && in->k1_valid);
    read_put16(w, 0xCDU, alarm_level(alarm, read_ms));
}

static void build_metadata_window(const hmi_state_input_t *in, hmi_read_window_t *w)
{
    read_put16(w, 0x200U, 0x5249U); read_put16(w, 0x201U, 1U);
    read_put16(w, 0x202U, APP_HMI_CONTROL_ENABLED ? 255U : 1U);
    read_put16(w, 0x203U, APP_HMI_DICTIONARY_REVISION);
    read_put32(w, 0x204U, in->identity_valid ? in->boot_id : 0xFFFFFFFFUL); read_put32(w, 0x206U, in->sequence);
    read_put32(w, 0x208U, in->now_ms); read_put16(w, 0x20AU, APP_HMI_SNAPSHOT_PERIOD_MS);
    read_put16(w, 0x20BU, APP_HMI_FW_PATCH); read_put32(w, 0x20CU, in->reset_reason);
    read_put32(w, 0x20EU, APP_HMI_CONTROL_ENABLED ? in->hmi_session : 0U);
    read_put16(w, 0x210U, in->owner_valid ? owner_code(in->owner) : 0xFFFFU);
    read_put16(w, 0x211U, APP_HMI_CONTROL_ENABLED ? in->lock_reason : 1U);
    read_put16(w, 0x212U, APP_HMI_CONTROL_ENABLED ? in->hmi_age : 0xFFFFU);
    read_put16(w, 0x213U, APP_HMI_CONTROL_ENABLED ? in->hmi_remaining : 0U);
    read_put16(w, 0x214U, APP_HMI_CONTROL_ENABLED ? in->selected : 0U);
    read_put16(w, 0x215U, APP_HMI_CONTROL_ENABLED ? (uint16_t)APP_OPERATION_MAX_VELOCITY_MM_S : 0U);
    read_put16(w, 0x216U, APP_HMI_CONTROL_ENABLED ? APP_HMI_SESSION_LEASE_MS : 0U);
}



static void build_details_window(const hmi_state_input_t *in, uint16_t start,
    uint16_t end, hmi_read_window_t *w)
{
    unsigned i, word;
    uint16_t a;
    if (start <= 0x32FU)
    {
        read_put16(w, 0x300U, 0x5249U); read_put16(w, 0x301U, 1U);
        read_put32(w, 0x302U, in->boot_id); read_put32(w, 0x304U, in->sequence);
        read_put32(w, 0x306U, in->now_ms);
        read_put16(w, 0x308U, in->selected); read_put16(w, 0x309U, owner_code(in->owner));
        read_put16(w, 0x30AU, in->can_valid ? (uint16_t)in->can.bus.phase : 0xFFFFU);
        read_put16(w, 0x30BU, in->can_valid ? (uint16_t)in->can.phase : 0xFFFFU);
        read_put32(w, 0x30CU, in->can_valid ? in->can.upper_boot_id : 0U);
        read_put32(w, 0x30EU, in->can_valid ? in->can.generation : 0U);
        read_put32(w, 0x310U, in->can_valid ? in->can.bus.rx_count : 0U);
        read_put32(w, 0x312U, in->can_valid ? in->can.bus.tx_success : 0U);
        read_put32(w, 0x314U, in->can_valid ? in->can.bus.tx_failed : 0U);
        read_put32(w, 0x316U, in->can_valid ? in->can.bus.bus_off_count : 0U);
        read_put32(w, 0x318U, in->can_valid ? in->can.upper_invalid : 0U);
        read_put16(w, 0x31AU, in->can_valid ? age16(in->now_ms - in->can.last_service_ms) : 0xFFFFU);
        read_put16(w, 0x31BU, age16(in->heartbeat_ms[3]));
        read_put16(w, 0x31CU, in->can_valid ? 1U : 0U);
        read_put16(w, 0x31DU, 0U); /* 无实体DI3、STO、制动反馈能力。 */

        read_put16(w, 0x31EU, in->remote_valid ? in->remote.controlled_stop_reason : 0xFFFFU);
        read_put16(w, 0x31FU, in->remote_valid ? in->remote.controlled_stop_state : 0xFFFFU);
        read_put16(w, 0x320U, in->remote_valid ? in->remote.stop_zero_mask : 0U);
        read_put16(w, 0x321U, in->k1.state);
        read_put16(w, 0x322U, in->k1.valid); read_put16(w, 0x323U, in->k1.confirmed_mask);
        read_put32(w, 0x324U, in->remote.stop_elapsed_ms);
        read_put32(w, 0x326U, in->remote.stop_timeout_ms);
        read_put32(w, 0x328U, in->remote.stop_first_command_valid ? in->remote.stop_first_command_ms : UINT32_MAX);
        read_put32(w, 0x32AU, in->remote.stop_zero_time_valid ? in->remote.stop_zero_ms : UINT32_MAX);
        read_put32(w, 0x32CU, in->remote.stop_complete_time_valid ? in->remote.stop_complete_ms : UINT32_MAX);
        read_put16(w, 0x32EU, in->cli_valid ? in->cli.state : 0xFFFFU);
        read_put16(w, 0x32FU, in->cli_valid ? in->cli.reason : 0xFFFFU);
    }
    if (start <= 0x33FU && end >= 0x330U)
    {
        for (i = 0U; i < 2U; i++)
        {
            a = (uint16_t)(0x330U + 8U * i);
            if (a > end || a + 7U < start) continue;
            read_put16(w, a, in->startup_valid ? in->startup[i].state : 0xFFFFU);
            read_put16(w, a + 1U, in->startup_valid ? in->hold[i].state : 0xFFFFU);
            read_put16(w, a + 2U, in->startup_valid ? in->watchdog[i].state : 0xFFFFU);
            read_put16(w, a + 3U, in->startup_valid ? in->watchdog[i].failure_reason : 0xFFFFU);
            read_put16(w, a + 4U, in->startup_valid ? in->watchdog[i].zero_samples : 0U);
            read_put16(w, a + 5U, in->startup_valid ? in->watchdog[i].decel_readback_ms : 0xFFFFU);
            read_put16(w, a + 6U, in->startup_valid ? (uint16_t)in->watchdog[i].start_speed_rpm : 0x7FFFU);
            read_put16(w, a + 7U, in->startup_valid ? in->boot_zero[i].state : 0xFFFFU);
        }
    }
    if (start <= 0x35FU && end >= 0x340U)
    {
        read_put16(w, 0x340U, in->position_valid ? in->position.state : 0xFFFFU);
        read_put16(w, 0x341U, in->position_valid && in->position.pair_valid);
        read_put16(w, 0x342U, in->position_valid && in->position.vehicle_valid);
        read_put16(w, 0x343U, in->position_valid ? in->position.source_mask : 0U);
        read_put32(w, 0x344U, in->position.baseline_generation);
        read_put32(w, 0x346U, in->position.lost_intervals);
        read_put32(w, 0x348U, in->position.pair_age_ms);
        read_put16(w, 0x34AU, in->position_valid ? in->position.axis[0].state : 0xFFFFU);
        read_put16(w, 0x34BU, in->position_valid ? in->position.axis[1].state : 0xFFFFU);
        read_put16(w, 0x34CU, in->position_valid ? in->position.axis[0].incomplete : 0U);
        read_put16(w, 0x34DU, in->position_valid ? in->position.axis[1].incomplete : 0U);
        read_put32(w, 0x34EU, in->axes_valid ? in->axis[0].position_sequence : 0U);
        read_put32(w, 0x350U, in->axes_valid ? in->axis[1].position_sequence : 0U);
        read_put32(w, 0x352U, in->axes_valid ? in->axis[0].position_age_ms : UINT32_MAX);
        read_put32(w, 0x354U, in->axes_valid ? in->axis[1].position_age_ms : UINT32_MAX);
        read_put16(w, 0x356U, in->position_valid);
        read_put16(w, 0x357U, 0U); /* 未实现告警历史，不声明历史能力。 */

        read_put16(w, 0x358U, in->remote_valid ? in->remote.authorized : 0U);
        read_put16(w, 0x359U, in->cli_valid ? in->cli.authorized : 0U);
        read_put32(w, 0x35AU, in->cli_valid ? in->cli.session_id : 0U);
        read_put16(w, 0x35CU, in->axes_valid ? age16(in->axis[0].fault_age_ms) : 0xFFFFU);
        read_put16(w, 0x35DU, in->axes_valid ? age16(in->axis[1].fault_age_ms) : 0xFFFFU);
        read_put16(w, 0x35EU, in->remote_valid ? in->remote.estop_latched : 0xFFFFU);
        read_put16(w, 0x35FU, in->init_complete);
    }
    /* 扩展区保存规范化的原CAN记录；不以半条记录或过期载荷刷新有效性。 */
    if (end >= 0x360U)
    {
        for (i = 0U; i < HMI_UPPER_COUNT; i++)
        {
            const can_upper_snapshot_t *u = &in->upper[i];
            a = (uint16_t)(0x360U + 32U * i);
            if (a > end || a + 31U < start) continue;
            read_put16(w, a, i == 0U ? 2U : (uint16_t)(0x3FU + i));
            read_put16(w, a + 1U, u->present);
            read_put16(w, a + 2U, upper_valid(in, (uint8_t)i));
            read_put16(w, a + 3U, u->present ? u->length : 0U);
            read_put16(w, a + 4U, u->sequence);
            read_put16(w, a + 5U, u->present ? age16(u->age_ms) : 0xFFFFU);
            read_put32(w, (uint16_t)(a + 6U), u->present ? u->received_ms : UINT32_MAX);
            for (word = 0U; word < 24U; word++)
                if (a + 8U + word >= start && a + 8U + word <= end)
                    read_put16(w, a + 8U + word, u->present && word * 2U + 1U < u->length ?
                        le16(u->payload + word * 2U) : 0xFFFFU);
        }
    }
}

/* Each valid read writes exactly quantity words. out[0] is start, including
 * subreads that split a 32-bit value or span internal CAN detail records. */
uint8_t app_hmi_state_build_read_window(const hmi_state_input_t *in,
    const hmi_alarm_state_t *alarm, uint32_t read_ms,
    uint16_t start, uint16_t quantity, uint16_t *out)
{
    uint32_t last;
    uint8_t window;
    hmi_read_window_t w;
    if (in == NULL || alarm == NULL || out == NULL || quantity == 0U ||
        quantity > HMI_STATE_READ_MAX_REGISTERS)
        return 0U;
    last = (uint32_t)start + quantity - 1U;
    if (start <= 0x003FU && last <= 0x003FU) window = 0U;
    else if (start >= 0x0050U && last <= 0x006FU) window = 1U;
    else if (start >= 0x0070U && last <= 0x008FU) window = 2U;
    else if (start >= 0x0090U && last <= 0x00AFU) window = 3U;
    else if (start >= 0x00B0U && last <= 0x00CFU) window = 4U;
    else if (start >= 0x0200U && last <= 0x0217U) window = 5U;
    else if (start >= 0x0300U && last <= 0x045FU) window = 6U;
    else if (start >= HMI_CONTROL_REGISTER_START && last < HMI_STATE_REGISTER_COUNT)
        window = 7U;
    else return 0U;
    memset(out, 0, quantity * sizeof(*out));
    w.out = out; w.first = start; w.quantity = quantity;
    switch (window)
    {
        case 0U: build_basic_window(in, alarm, read_ms, &w); break;
        case 1U: build_communications_window(in, &w); break;
        case 2U: read_build_position(in, &w); read_build_sync(in, &w); break;
        case 3U: build_upper_summary_window(in, alarm, read_ms, &w); break;
        case 4U: build_capabilities_window(in, alarm, read_ms, &w); break;
        case 5U: build_metadata_window(in, &w); break;
        case 6U: build_details_window(in, start, (uint16_t)last, &w); break;
        default:
            w.first = (uint16_t)(start - HMI_CONTROL_REGISTER_START);
            read_build_control(in, &w);
            break;
    }
    return 1U;
}

/* Legacy absolute-address adapter is retained for callers/test oracles. The
 * production snapshot uses the exact request-relative API above. */
uint8_t app_hmi_state_build_window(const hmi_state_input_t *in,
    const hmi_alarm_state_t *alarm, uint32_t read_ms,
    uint16_t start, uint16_t quantity, uint16_t r[HMI_STATE_REGISTER_COUNT])
{
    if (r == NULL || start >= HMI_STATE_REGISTER_COUNT) return 0U;
    return app_hmi_state_build_read_window(in, alarm, read_ms, start, quantity, &r[start]);
}

/* Same encoder for full publications and the critical request path. All
 * offsets are relative to 0x0460, and every call clears the OR-ed masks. */
void app_hmi_state_build_control(const hmi_state_input_t *in,
                                 uint16_t r[HMI_CONTROL_REGISTER_COUNT])
{
    uint8_t i;
    if (in == NULL || r == NULL) return;
    memset(r, 0, HMI_CONTROL_REGISTER_COUNT * sizeof(*r));
    r[0x0U] = 0x534DU; r[0x1U] = 2U;
    put32(r, 0x2U, in->boot_id); put32(r, 0x4U, in->sequence);
    put32(r, 0x6U, in->now_ms); r[0x8U] = owner_code(in->owner);
    r[0x9U] = in->selected; r[0xAU] = in->hmi_control.state;
    r[0xBU] = in->hmi_control.flags;
    put32(r, 0xCU, in->hmi_control.session);
    r[0xEU] = in->hmi_remaining;
    r[0xFU] = age16(in->hmi_control.target_age_ms);
    put32(r, 0x10U, in->hmi_control.last_sequence);
    r[0x12U] = in->owner == APP_CONTROL_OWNER_NONE || in->owner == APP_CONTROL_OWNER_HMI ?
        (uint16_t)in->hmi_control.target : 0x7FFFU;
    for (i = 0U; i < 2U; ++i)
    {
        const servo_snapshot_t *axis = &in->axis[i];
        uint16_t bit = (uint16_t)(1U << i);
        r[0x13U + i] = (in->selected & bit) && in->owner == APP_CONTROL_OWNER_HMI ?
            (uint16_t)in->hmi_control.target : 0x7FFFU;
        r[0x15U + i] = in->hmi_control.output_mask & bit ?
            (uint16_t)in->hmi_control.output[i] : 0x7FFFU;
        r[0x17U + i] = speed_valid(in, i) ? (uint16_t)axis->actual_speed_rpm : 0x7FFFU;
        if (speed_valid(in, i)) r[0x19U] |= bit;
        if (in->axes_valid && (in->selected & bit) && axis->online &&
            axis->enable_feedback_valid && axis->enable_feedback_age_ms < APP_SERVO_STATUS_MAX_AGE_MS)
        {
            r[0x1AU] |= bit;
            if (axis->enable_feedback) r[0x1BU] |= bit;
        }
        r[0x2EU + i] = speed_valid(in, i) ? age16(axis->speed_age_ms) : 0xFFFFU;
    }
    /* Historical stop zero proof does not describe an active new session. */
    r[0x1CU] = !in->hmi_control.session && in->hmi_control.stop.result == 3U ?
        in->hmi_control.stop.zero_mask : in->hmi_control.zero_mask;
    r[0x1DU] = in->hmi_control.stop.reason;
    r[0x1EU] = in->hmi_control.phase;
    r[0x1FU] = in->hmi_control.stop.deceleration_mm_s2;
    put32(r, 0x20U, in->hmi_control.elapsed_ms);
    put32(r, 0x22U, in->hmi_control.stop.timeout_ms);
    r[0x24U] = in->hmi_control.error; r[0x25U] = in->lock_reason;
    r[0x26U] = (uint16_t)-APP_OPERATION_MAX_MOTOR_RPM;
    r[0x27U] = APP_OPERATION_MAX_MOTOR_RPM;
    r[0x28U] = (uint16_t)((double)APP_DESIGN_ACCELERATION_MPS2 * 1000.0 + 0.5);
    r[0x29U] = (uint16_t)(APP_DESIGN_DECELERATION_MPS2 * 1000.0f + 0.5f);
    r[0x2AU] = APP_HMI_NORMAL_STOP_DECEL_MM_S2; r[0x2BU] = APP_HMI_EMERGENCY_STOP_DECEL_MM_S2;
    r[0x2CU] = APP_HMI_SPEED_HOLD_MAX_MS; r[0x2DU] = APP_HMI_SESSION_LEASE_MS;
    put32(r, 0x30U, in->hmi_control.stop.event);
    put32(r, 0x32U, in->hmi_control.stop.request_sequence);
    r[0x34U] = in->hmi_control.stop.command; r[0x35U] = in->hmi_control.stop.result;
    put32(r, 0x36U, in->hmi_control.stop.completed_ms);
    /* 0x4B8-0x4BF is the reserved recovery mailbox/status window.  Keep the
     * layout compact and monotonic so the 26号 screen can render it without
     * polling hardware: version, state/eligibility, generation, failure,
     * healthy duration and first-fault summary. */
    r[0x58U] = 1U;
    r[0x59U] = (uint16_t)in->recovery.state;
    r[0x5AU] = in->recovery.eligible ? 1U : 0U;
    put32(r, 0x5BU, in->recovery.generation);
    r[0x5DU] = in->recovery.failure;
    r[0x5EU] = in->recovery.first_axis | ((uint16_t)in->recovery.first_item << 8U);
    r[0x5FU] = (uint16_t)(in->recovery.healthy_ms > 65534U ? 65534U : in->recovery.healthy_ms);
    r[0x38U] = owner_code(in->hmi_control.stop.owner);
    r[0x39U] = in->hmi_control.output_mask;
    r[0x3AU] = in->lock_reason != APP_CONTROL_LOCK_NONE ? 0U :
        in->owner == APP_CONTROL_OWNER_CAN && in->can_control.authorized ?
        (uint16_t)in->can_control.target_mm_s : in->owner == APP_CONTROL_OWNER_HMI &&
        (in->hmi_control.flags & 2U) ? signed16(axis_mmps(in->selected == 2U ? 1U : 0U,
        in->hmi_control.target)) :
        in->owner == APP_CONTROL_OWNER_REMOTE && in->remote.authorized ?
        signed16(axis_mmps(in->selected == 2U ? 1U : 0U, in->remote.target_rpm)) : 0U;
    r[0x3BU] = 1U;
    r[0x3CU] = axis_command(in, 0U);
    r[0x3DU] = axis_command(in, 1U);
    r[0x3EU] = in->axes_valid ? 1U : 0U;
    r[0x3FU] = APP_SYSTEM_SNAPSHOT_PERIOD_MS;
    for (i = 0U; i < 2U; ++i)
    {
        const servo_snapshot_t *axis = &in->axis[i];
        uint16_t offset = (uint16_t)(0x40U + i * 12U);
        uint8_t field;
        r[offset] = in->axes_valid && axis->online && (in->selected & (1U << i)) ?
            axis->diagnostic_valid_mask : 0U;
        r[offset + 1U] = axis->diagnostic_stale_mask;
        r[offset + 2U] = (uint16_t)axis->realtime_torque_tenths_percent;
        r[offset + 3U] = axis->load_ratio_tenths_percent;
        r[offset + 4U] = (uint16_t)axis->phase_current_deciamp;
        r[offset + 5U] = axis->bus_voltage_decivolt;
        for (field = 0U; field < 4U; ++field)
        {
            uint32_t age = (uint32_t)(in->now_tick - axis->diagnostic_sample_tick[field]) * portTICK_PERIOD_MS;
            r[offset + 6U + field] = age16(age);
            if (age > APP_SERVO_DIAGNOSTIC_POLL_MS * 3U)
                r[offset + 1U] |= (uint16_t)(1U << field);
        }
        r[offset + 10U] = age16(axis->speed_age_ms);
        r[offset + 11U] = age16(axis->fault_age_ms);
    }
}
