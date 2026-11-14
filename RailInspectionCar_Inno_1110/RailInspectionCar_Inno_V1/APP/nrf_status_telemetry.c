#include "nrf_status_telemetry.h"
#include "app_arm3_port.h"
#include "app_arm3_inputs.h"
#include "app_cli_servo_session.h"
#include "app_config.h"
#include "HMI/app_control_arbiter.h"
#include "app_snapshot_ports.h"
#include "app_health.h"
#include "bsp_system.h"
#include "motion_control.h"
#include "NRF24L01.h"
#include "nrf_remote_protocol.h"
#include "task.h"
#include <limits.h>
#include <string.h>

static nrf_telemetry_stats_t s_stats;
#if APP_NRF_TELEMETRY_ENABLED
/* 大快照放在静态区，避免侵占无线任务栈。 */
static nrf_status_input_t s_input;
static nrf_snapshot_context_t s_context;
static uint32_t s_last[10];
static uint8_t s_sent[10];
static uint8_t s_alarm_sequence;
static uint8_t s_alarm_code;
static uint8_t s_alarm_source;
static uint8_t s_alarm_level;
static uint32_t s_alarm_detail;
static uint32_t s_alarm_tick;
static uint32_t s_collect_tick;
static uint8_t s_collected;
static uint32_t s_radio_generation;
static uint8_t s_fast_burst;
static uint8_t s_schedule_cursor;
static uint8_t s_alarm_sent_sequence;
static uint8_t s_alarm_sent_valid;
static uint32_t s_schedule_start_ms;
static uint8_t s_schedule_started;
#endif

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8U);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24U);
    p[1] = (uint8_t)(value >> 16U);
    p[2] = (uint8_t)(value >> 8U);
    p[3] = (uint8_t)value;
}

static uint16_t sat_u16(uint64_t value)
{
    return (uint16_t)(value > 65535U ? 65535U : value);
}

static uint16_t diagnostic_signed(int16_t value, uint8_t valid)
{
    if (value < -16384 || value > 16383) return 0U;
    return valid ? (uint16_t)(((uint16_t)value & 0x7FFFU) | 0x8000U) : 0U;
}

static uint16_t diagnostic_unsigned(uint16_t value, uint8_t valid)
{
    if (value > 0x7FFFU) return 0U;
    return valid ? (uint16_t)(value | 0x8000U) : 0U;
}

static int16_t sat_i16(double value)
{
    if (value >= 32767.0) return 32767;
    if (value <= -32768.0) return -32768;
    return (int16_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

static uint16_t distance_u16(double mm)
{
    if (mm >= 6553500.0) return 65535U;
    if (mm <= 0.0) return 0U;
    return (uint16_t)(mm / 100.0 + 0.5);
}

static uint8_t speed_valid(const servo_snapshot_t *axis)
{
    return axis->online && axis->speed_valid &&
        axis->speed_age_ms < APP_SERVO_STATUS_MAX_AGE_MS;
}

static uint8_t fault_valid(const servo_snapshot_t *axis)
{
    return axis->online && axis->fault_valid &&
        axis->fault_age_ms < APP_SERVO_STATUS_MAX_AGE_MS;
}

static uint8_t axis_selected(const nrf_status_input_t *in, uint8_t axis)
{
    return (uint8_t)((in->axis_flags >> axis) & 1U);
}

static uint16_t fault_code(const nrf_status_input_t *in, uint8_t axis)
{
    return axis_selected(in, axis) && fault_valid(&in->axis[axis]) ?
        in->axis[axis].fault_code : 0xFFFFU;
}

static uint16_t actual_speed(const nrf_status_input_t *in, uint8_t axis)
{
    return axis_selected(in, axis) && speed_valid(&in->axis[axis]) ?
        (uint16_t)in->axis[axis].actual_speed_rpm : 0x7FFFU;
}

bool nrf_status_encode(uint8_t frame[32], uint8_t type, uint8_t sequence,
                       uint8_t session, const nrf_status_input_t *in)
{
    uint8_t axis;
    uint8_t selected;
    uint8_t flags;
    uint8_t validity;
    uint16_t crc;
    const servo_snapshot_t *feedback;
    if (frame == NULL || in == NULL || type < 0x81U || type > 0x8AU)
        return false;
    memset(frame, 0, 32U);
    frame[0] = 0x5AU; frame[1] = 0xA5U; frame[2] = 1U;
    frame[3] = type; frame[4] = sequence; frame[5] = 22U;
    frame[6] = session; frame[7] = (uint8_t)(in->axis_flags & 0x3FU);
    validity = (uint8_t)(in->valid_flags & ~0x40U);
    frame[9] = in->alarm_level;
    switch (type)
    {
        case 0x8AU:
            put32(&frame[12], in->snapshot_sequence);
            put32(&frame[16], in->snapshot_ms);
            frame[20] = in->control_owner;
            frame[21] = 2U;
            frame[22] = 3U; /* legacy extension + ARM3 v2 input supported */
            frame[23] = in->recovery_state;
            frame[24] = in->recovery_eligible;
            frame[25] = in->recovery_failure;
            frame[26] = (uint8_t)in->recovery_healthy_ms;
            frame[27] = (uint8_t)(in->recovery_healthy_ms >> 8U);
            break;
        case 0x81U:
            frame[12] = in->system_state;
            frame[13] = in->mode;
            frame[14] = (uint8_t)(in->authorization_flags & 0x7FU);
            frame[15] = in->communication_state;
            put16(&frame[16], (uint16_t)in->remote.target_rpm);
            put16(&frame[18], (uint16_t)in->remote.commanded_rpm);
            put16(&frame[20], axis_selected(in, 0U) ?
                (uint16_t)in->remote.left_commanded_rpm : 0x7FFFU);
            put16(&frame[22], actual_speed(in, 0U));
            put16(&frame[24], axis_selected(in, 1U) ?
                (uint16_t)in->remote.right_commanded_rpm : 0x7FFFU);
            put16(&frame[26], actual_speed(in, 1U));
            break;
        case 0x82U:
            for (axis = 0U; axis < 2U; axis++)
            {
                feedback = &in->axis[axis];
                selected = axis_selected(in, axis);
                flags = 0U;
                if (selected)
                {
                    if (feedback->online) flags |= 1U;
                    /* 使能位必须配合有效反馈位显示，离线不能伪装保持使能。 */
                    if (fault_valid(feedback) && feedback->enabled) flags |= 2U;
                    if (fault_valid(feedback) && feedback->fault) flags |= 4U;
                    if (speed_valid(feedback)) flags |= 8U;
                    if (fault_valid(feedback)) flags |= 16U;
                    if (speed_valid(feedback) && feedback->actual_speed_rpm == 0)
                        flags |= 32U;
                    if (feedback->parameter_verified) flags |= 64U;
                    if (!in->gate_ready || feedback->stop_latched) flags |= 128U;
                }
                frame[12U + axis] = flags;
                put16(&frame[14U + axis * 2U], fault_code(in, axis));
                put16(&frame[18U + axis * 2U], actual_speed(in, axis));
                put16(&frame[22U + axis * 2U], selected ?
                    sat_u16(feedback->speed_age_ms) : 65535U);
                frame[26U + axis] = selected ?
                    (uint8_t)(feedback->poll_failures > 255U ?
                              255U : feedback->poll_failures) : 0U;
            }
            break;
        case 0x83U:
            put32(&frame[12], (uint32_t)in->position.axis[0].raw_position);
            put32(&frame[16], (uint32_t)in->position.axis[1].raw_position);
            put16(&frame[20], (uint16_t)sat_i16(in->position.difference_mm));
            put16(&frame[22], (uint16_t)sat_i16(
                in->position.vehicle_boot_net_mm / 100.0));
            put16(&frame[24], distance_u16(in->position.vehicle_boot_path_mm));
            put16(&frame[26], distance_u16(in->position.vehicle_trip_path_mm));
            if (in->position.difference_mm > 32767.0 ||
                in->position.difference_mm < -32768.0 ||
                in->position.vehicle_boot_net_mm > 3276700.0 ||
                in->position.vehicle_boot_net_mm < -3276800.0 ||
                in->position.vehicle_boot_path_mm > 6553500.0 ||
                in->position.vehicle_trip_path_mm > 6553500.0)
                validity |= 128U;
            break;
        case 0x84U:
            frame[12] = (uint8_t)((in->remote.sync.enabled ? 1U : 0U) |
                (in->remote.sync.active ? 2U : 0U) |
                (APP_REMOTE_SERVO_SYNC_TRIP_ENABLED ? 4U : 0U) |
                (in->remote.sync.state == REMOTE_SYNC_LIMITED ? 8U : 0U));
            frame[13] = (in->axis_flags & 3U) != 3U ? 6U :
                (uint8_t)in->remote.sync.state;
            frame[14] = in->remote.sync.pair_valid;
            frame[15] = (uint8_t)in->remote.sync.reject_reason;
            put16(&frame[16], speed_valid(&in->axis[0]) &&
                speed_valid(&in->axis[1]) && (in->axis_flags & 3U) == 3U ?
                (uint16_t)sat_i16((int32_t)in->axis[0].actual_speed_rpm -
                                 (int32_t)in->axis[1].actual_speed_rpm) : 0x7FFFU);
            put16(&frame[18], (uint16_t)sat_i16(in->remote.sync.effective_error));
            put16(&frame[20], (uint16_t)in->remote.sync.integer_correction);
            put16(&frame[22], sat_u16(in->remote.sync.skew_ms));
            put16(&frame[24], sat_u16(in->remote.sync.age_ms[0]));
            put16(&frame[26], sat_u16(in->remote.sync.age_ms[1]));
            break;
        case 0x85U:
            /* 未接入整机数据源：状态全部未知，电量显式无效。 */
            frame[13] = 0xFFU;
            break;
        case 0x86U:
            put16(&frame[12], sat_u16(APP_POSITION_LEFT_COUNTS_PER_REV));
            put16(&frame[14], sat_u16(APP_POSITION_RIGHT_COUNTS_PER_REV));
            put16(&frame[16], sat_u16(APP_POSITION_LEFT_WHEEL_DIAMETER_MM));
            put16(&frame[18], sat_u16(APP_POSITION_RIGHT_WHEEL_DIAMETER_MM));
            frame[20] = APP_POSITION_LEFT_SIGN > 0 ? 1U : 0xFFU;
            frame[21] = APP_POSITION_RIGHT_SIGN > 0 ? 1U : 0xFFU;
            put16(&frame[22], sat_u16(
                ((uint64_t)APP_POSITION_LEFT_GEAR_NUM * 1000U +
                 APP_POSITION_LEFT_GEAR_DEN / 2U) /
                APP_POSITION_LEFT_GEAR_DEN));
            put16(&frame[24], sat_u16(
                ((uint64_t)APP_POSITION_RIGHT_GEAR_NUM * 1000U +
                 APP_POSITION_RIGHT_GEAR_DEN / 2U) /
                APP_POSITION_RIGHT_GEAR_DEN));
            frame[26] = 1U; frame[27] = 1U;
            break;
        case 0x87U:
            frame[12] = in->alarm_sequence;
            frame[13] = in->alarm_level;
            frame[14] = in->alarm_code;
            frame[15] = in->alarm_source;
            put16(&frame[16], fault_code(in, 0U));
            put16(&frame[18], fault_code(in, 1U));
            put32(&frame[20], in->alarm_tick_ms);
            put32(&frame[24], in->alarm_detail);
            break;
        case 0x88U:
        {
            uint8_t front_valid = (uint8_t)(axis_selected(in, 0U) &&
                speed_valid(&in->axis[0]));
            uint8_t rear_valid = (uint8_t)(axis_selected(in, 1U) &&
                speed_valid(&in->axis[1]));
            uint8_t target_valid = (uint8_t)((in->axis_flags & 3U) != 0U &&
                in->remote.target_rpm != 32767);
            uint8_t command_valid = (uint8_t)((in->axis_flags & 3U) != 0U &&
                in->remote.commanded_rpm != 32767);
            uint32_t skew = in->axis[0].speed_age_ms > in->axis[1].speed_age_ms ?
                in->axis[0].speed_age_ms - in->axis[1].speed_age_ms :
                in->axis[1].speed_age_ms - in->axis[0].speed_age_ms;
            uint8_t average_valid = (uint8_t)(front_valid && rear_valid &&
                skew <= APP_REMOTE_SERVO_SYNC_MAX_SKEW_MS);
            double front = front_valid ?
                (double)in->axis[0].actual_velocity_mps * 1000.0 : 32767.0;
            double rear = rear_valid ?
                (double)in->axis[1].actual_velocity_mps * 1000.0 : 32767.0;
            double average = average_valid ?
                (front + rear) * 0.5 : 32767.0;
            double target = target_valid ? (double)in->remote.target_rpm *
                (double)APP_WHEEL_CIRCUMFERENCE_M * 1000.0 /
                (60.0 * (double)APP_GEAR_REDUCTION_RATIO) : 32767.0;
            double command = command_valid ? (double)in->remote.commanded_rpm *
                (double)APP_WHEEL_CIRCUMFERENCE_M * 1000.0 /
                (60.0 * (double)APP_GEAR_REDUCTION_RATIO) : 32767.0;
            put16(&frame[12], (uint16_t)sat_i16(target));
            put16(&frame[14], (uint16_t)sat_i16(command));
            put16(&frame[16], (uint16_t)sat_i16(front));
            put16(&frame[18], (uint16_t)sat_i16(rear));
            put16(&frame[20], (uint16_t)sat_i16(average));
            frame[22] = (uint8_t)((target_valid ? 1U : 0U) |
                (command_valid ? 2U : 0U) |
                (front_valid ? 4U : 0U) |
                (rear_valid ? 8U : 0U) |
                (average_valid ? 16U : 0U));
            /* 0x88 extension: signed 0.01 km/h and source/mode in the SAME frame.
             * low 3 bits: JOY, KEY, HMI, CAN, CLI; bit3 waiting, bit4 ready. */
            {
                int16_t common = 0;
                uint8_t source = 0U;
                if (in->control_owner == APP_CONTROL_OWNER_CLI) { source = 4U; common = 32767; }
                else if (in->control_owner == APP_CONTROL_OWNER_HMI) {
                    source = 2U;
                    common = in->remote.target_rpm == 32767 ? 32767 : sat_i16(
                        (double)in->remote.target_rpm * APP_WHEEL_CIRCUMFERENCE_M_D * 6.0 / APP_GEAR_REDUCTION_RATIO);
                } else if (in->control_owner == APP_CONTROL_OWNER_CAN) {
                    source = 3U;
                    common = in->control_authorized ? sat_i16((double)in->can_target_mm_s * 0.36) : 0;
                } else if (in->control_owner == APP_CONTROL_OWNER_REMOTE) {
                    source = in->remote.speed_mode == REMOTE_SPEED_KEYS ? 1U : 0U;
                    common = in->remote.common_target_centi_kmh;
                }
                put16(&frame[10], (uint16_t)common);
                frame[23] = (uint8_t)(0xA0U | source |
                    (source == 1U && in->remote.key_exit_pending ? 8U : 0U) |
                    (source == 1U && in->remote.key_exit_ready ? 16U : 0U));
            }
            put16(&frame[24], front_valid ? sat_u16(in->axis[0].speed_age_ms) : 65535U);
            put16(&frame[26], rear_valid ? sat_u16(in->axis[1].speed_age_ms) : 65535U);
            validity = (uint8_t)(validity & (uint8_t)~32U);
            break;
        }
        case 0x89U:
            for (axis = 0U; axis < 2U; axis++)
            {
                const servo_snapshot_t *diagnostic = &in->axis[axis];
                uint8_t valid_mask = axis_selected(in, axis) ?
                    diagnostic->diagnostic_valid_mask : 0U;
                uint8_t base = (uint8_t)(12U + axis * 8U);
                /* 仅0x89使用保留字节承载逐字段陈旧位，重发缓存不是新采样。 */
                frame[10U + axis] = (uint8_t)(diagnostic->diagnostic_stale_mask & valid_mask & 15U);
                put16(&frame[base], diagnostic_signed(
                    diagnostic->realtime_torque_tenths_percent,
                    (uint8_t)((valid_mask & SERVO_DIAGNOSTIC_TORQUE_VALID) != 0U)));
                put16(&frame[base + 2U], diagnostic_unsigned(
                    diagnostic->load_ratio_tenths_percent,
                    (uint8_t)((valid_mask & SERVO_DIAGNOSTIC_LOAD_VALID) != 0U)));
                put16(&frame[base + 4U], diagnostic_signed(
                    diagnostic->phase_current_deciamp,
                    (uint8_t)((valid_mask & SERVO_DIAGNOSTIC_CURRENT_VALID) != 0U)));
                put16(&frame[base + 6U], diagnostic_unsigned(
                    diagnostic->bus_voltage_decivolt,
                    (uint8_t)((valid_mask & SERVO_DIAGNOSTIC_BUS_VOLTAGE_VALID) != 0U)));
            }
            break;
        default: return false;
    }
    frame[8] = validity;
    crc = nrf_remote_crc16(frame, 28U);
    put16(&frame[28], crc);
    frame[30] = 13U; frame[31] = 10U;
    return true;
}

void nrf_status_telemetry_init(uint8_t session)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.session = session;
    s_stats.enabled = APP_NRF_TELEMETRY_ENABLED;
#if APP_NRF_TELEMETRY_ENABLED
    memset(s_last, 0, sizeof(s_last));
    memset(s_sent, 0, sizeof(s_sent));
    s_alarm_sequence = 0U;
    s_alarm_code = 0U;
    s_alarm_source = 0U;
    s_alarm_level = 0U;
    s_alarm_detail = 0U;
    s_alarm_tick = 0U;
    s_collected = 0U;
    s_radio_generation = 0U;
    s_fast_burst = 0U;
    s_schedule_cursor = 1U;
    s_alarm_sent_sequence = 0U;
    s_alarm_sent_valid = 0U;
    s_schedule_started = 0U;
#endif
}

#if APP_NRF_TELEMETRY_ENABLED
static void nrf_alarm(uint8_t level, uint8_t code, uint8_t source, uint32_t detail)
{
    if (level <= s_input.alarm_level) return;
    s_input.alarm_level = level;
    s_input.alarm_code = code;
    s_input.alarm_source = source;
    s_input.alarm_detail = detail;
}

static bool nrf_collect(uint32_t now)
{
    uint8_t axis;
    uint8_t all_feedback = 1U;
    uint8_t all_position = 1U;
    uint8_t incomplete = 0U;
    cli_servo_session_status_t cli;
    servo_startup_stop_status_t stop;
    servo_boot_zero_status_t zero;
    servo_startup_hold_status_t hold;
    servo_wdg_recovery_status_t watchdog;
    motion_state_t motion;
    uint32_t reset, health;
    memset(&s_input, 0, sizeof(s_input));
    if (!app_system_snapshot_read_nrf(&s_input, &s_context)) return false;
    cli = s_context.cli;
    reset = s_context.reset;
    health = s_context.health;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        uint8_t field;
        for (field = 0U; field < 4U; field++)
        {
            if ((TickType_t)(xTaskGetTickCount() -
                    s_input.axis[axis].diagnostic_sample_tick[field]) >
                    pdMS_TO_TICKS(APP_SERVO_DIAGNOSTIC_POLL_MS * 3U))
                s_input.axis[axis].diagnostic_stale_mask |= (uint8_t)(1U << field);
        }
    }
    s_input.axis_flags = app_config_servo_control_axis_mask();
    if ((s_input.axis_flags & 3U) == 3U) s_input.axis_flags |= 4U;
    if (app_config_servo_readonly_test()) s_input.axis_flags |= 8U;
    if (reset & (1UL << 27U)) s_input.axis_flags |= 16U;
    if (reset & (1UL << 29U)) s_input.axis_flags |= 32U;
    s_input.gate_ready = (uint8_t)(((s_context.ready_mask & 2U) != 0U) &&
        ((s_context.ready_mask & 1U) != 0U) && ((s_context.ready_mask & 4U) != 0U) &&
        ((s_context.ready_mask & 8U) != 0U) && app_config_motion_hardware_ready() &&
        app_config_servo_bus_ready() && s_context.motion_allowed &&
        !app_config_servo_readonly_test() && !s_context.maintenance);
    s_input.system_state = 5U;
    if (s_context.hold_active) s_input.mode = 7U;
    if (s_input.remote.state == REMOTE_SERVO_ARMING)
    {
        s_input.system_state = 6U; s_input.mode = 4U;
    }
    else if (s_input.remote.authorized)
    {
        s_input.system_state = s_input.remote.state == REMOTE_SERVO_RUNNING ? 7U : 6U;
        s_input.mode = 4U;
    }
    else if (s_input.remote.hold_stopping)
    {
        s_input.system_state = 8U; s_input.mode = 4U;
    }
    else if (s_context.hold_active && ((s_context.ready_mask & 8U) != 0U))
        s_input.system_state = 4U;
    else
    {
        for (axis = 0U; axis < 2U; axis++)
            if (axis_selected(&s_input, axis))
                s_input.mode = (uint8_t)s_input.axis[axis].control_mode;
    }
    if (!s_input.remote.hold_stopping && s_input.remote.k1_hold.valid &&
        s_input.remote.k1_hold.zero_mask == s_input.remote.k1_hold.axis_mask)
        s_input.mode = 7U;
    if (cli.state != CLI_SESSION_IDLE)
    {
        s_input.mode = (uint8_t)s_input.axis[cli.axis].control_mode;
        s_input.system_state = cli.state == CLI_SESSION_STOPPING ||
            cli.state == CLI_SESSION_RESTORING ? 8U :
            cli.state == CLI_SESSION_LOCKED ? (cli.recoverable ? 9U : 10U) :
            cli.debug_state == SERVO_DEBUG_RUNNING ? 7U : 6U;
    }
    s_input.authorization_flags = (uint8_t)(
        (s_input.remote.link_valid ? 1U : 0U) |
        (s_input.remote.authorized ? 2U : 0U) |
        (s_input.gate_ready ? 4U : 0U) |
        (cli.state == CLI_SESSION_IDLE &&
         ((s_input.remote.k1_hold.valid &&
           s_input.remote.k1_hold.zero_mask == s_input.remote.k1_hold.axis_mask) ||
          (s_input.remote.k1_hold.state == SERVO_K1_HOLD_INACTIVE &&
           !s_input.remote.hold_stopping &&
           s_context.hold_active && ((s_context.ready_mask & 8U) != 0U))) ? 8U : 0U) |
        ((s_input.remote.estop_latched && s_input.remote.stop_reason == REMOTE_STOP_K1) ||
         (cli.state == CLI_SESSION_LOCKED && cli.reason == CLI_EXIT_K1) ? 16U : 0U) |
        (s_input.remote.hold_stopping && !s_input.remote.link_valid ? 32U : 0U) |
        (!s_input.remote.authorized && !cli.authorized &&
         s_input.remote.target_rpm == 0 ? 64U : 0U));
    s_input.communication_state = (uint8_t)s_input.remote.comm_state;
    if (!s_input.remote.link_valid) s_input.communication_state = 3U;
    for (axis = 0U; axis < 2U; axis++)
    {
        if (!axis_selected(&s_input, axis)) continue;
        if (!speed_valid(&s_input.axis[axis]) || !fault_valid(&s_input.axis[axis]))
            all_feedback = 0U;
        if (!s_input.position.axis[axis].valid) all_position = 0U;
        if (s_input.position.axis[axis].incomplete) incomplete = 1U;
        if (!s_input.axis[axis].online)
        {
            nrf_alarm(2U, 5U, (uint8_t)(axis + 1U), 0U);
        }
        if (fault_valid(&s_input.axis[axis]) && s_input.axis[axis].fault)
        {
            nrf_alarm(3U, (uint8_t)(3U + axis), (uint8_t)(axis + 1U),
                      s_input.axis[axis].fault_code);
        }
    }
    if (!s_input.remote.link_valid) nrf_alarm(2U, 1U, 3U, 0U);
    if (cli.state == CLI_SESSION_LOCKED && cli.reason == CLI_EXIT_K1)
        nrf_alarm(3U, 2U, 3U, 0U);
    if (incomplete) nrf_alarm(2U, 10U, 4U, s_input.position.lost_intervals);
    if (s_input.remote.sync.over_limit)
        nrf_alarm(2U, 11U, 5U, s_input.remote.sync.over_limit_duration_ms);
    motion = s_context.motion;
    /* 启动进度只计算一次，不能被另一轴的WAITING覆盖已发现的FAILED。 */
    if (!((s_context.ready_mask & 1U) != 0U))
    { s_input.system_state = 1U; s_input.mode = 5U; }
    else if (!((s_context.ready_mask & 4U) != 0U))
    { s_input.system_state = 3U; s_input.mode = 6U; }
    else if (!((s_context.ready_mask & 8U) != 0U) && cli.state == CLI_SESSION_IDLE)
    { s_input.system_state = 4U; s_input.mode = 7U; }
    if (!((s_context.ready_mask & 2U) != 0U))
    { s_input.system_state = 2U; s_input.mode = 8U; }
    if (s_input.remote.estop_latched || motion == MOTION_FAULT ||
        motion == MOTION_OFFLINE)
    {
        /* CLI可恢复人工锁存沿用旧显示；运行期K1保持的真实故障优先。 */
        s_input.system_state = motion == MOTION_OFFLINE ||
            (motion == MOTION_FAULT &&
             !(cli.state == CLI_SESSION_LOCKED && cli.recoverable)) ?
            10U : s_input.remote.hold_stopping ? 8U :
            s_input.remote.stop_reason == REMOTE_STOP_K1 ||
            s_input.remote.stop_reason == REMOTE_STOP_MANUAL ||
            (cli.state == CLI_SESSION_LOCKED && cli.recoverable) ? 9U : 10U;
        if (s_input.remote.stop_reason == REMOTE_STOP_K1)
            nrf_alarm(3U, 2U, 3U, 0U);
        else if (s_input.remote.stop_reason == REMOTE_STOP_SYNC_ERROR ||
                 s_input.remote.stop_reason == REMOTE_STOP_REVERSE_TIMEOUT)
            nrf_alarm(3U, 11U, 5U, s_input.remote.stop_reason);
        else if (s_input.remote.comm_state == REMOTE_COMM_FAILED ||
                 motion == MOTION_OFFLINE)
            nrf_alarm(3U, 5U, 0U, s_input.remote.stop_reason);
        else
            /* 冻结协议无独立人工停车码，用系统锁存状态表达，不冒充驱动器故障。 */
            nrf_alarm(3U, 0U, 0U, s_input.remote.stop_reason);
    }
    /* 启动状态与失败码优先于普通未授权状态，不能伪装为待机就绪。 */
    for (axis = 0U; axis < 2U; axis++)
    {
        if (!axis_selected(&s_input, axis)) continue;
        if (!s_context.startup_valid) return false;
        stop = s_context.startup[axis];
        zero = s_context.zero[axis];
        hold = s_context.hold[axis];
        watchdog = s_context.watchdog[axis];
        if (stop.state == SERVO_STARTUP_STOP_FAILED ||
            zero.state == SERVO_BOOT_ZERO_FAILED ||
            (hold.state == SERVO_STARTUP_HOLD_FAILED && hold.failure != 5U) ||
            watchdog.state == SERVO_WDG_RECOVERY_FAILED)
        {
            uint8_t code = watchdog.state == SERVO_WDG_RECOVERY_FAILED ? 8U :
                stop.state == SERVO_STARTUP_STOP_FAILED ? 6U :
                zero.state == SERVO_BOOT_ZERO_FAILED ? 7U : 9U;
            nrf_alarm(code == 8U ? 4U : 3U, code, (uint8_t)(axis + 1U),
                code == 8U ? watchdog.failure_reason : code == 6U ?
                stop.failure : code == 7U ? zero.failure : hold.failure);
            s_input.system_state = 10U;
        }
    }
    if (health & (APP_DIAG_ASSERT_FAILED | APP_DIAG_STACK_OVERFLOW |
                  APP_DIAG_TASK_CREATE_ERROR | APP_DIAG_HEARTBEAT_TIMEOUT))
    {
        /* 系统健康故障优先于同等级的恢复故障。 */
        s_input.alarm_level = 0U;
        nrf_alarm(4U, 12U, 0U, health);
        s_input.system_state = 10U;
    }
    /* 安全异常可能先于保持快照更新，不能继续发布零速保持有效。 */
    if (s_input.system_state == 10U)
        s_input.authorization_flags &= (uint8_t)~8U;
    if (app_config_servo_readonly_test() || s_context.maintenance)
        s_input.system_state = 12U;
    if (!s_input.remote.authorized && !s_input.remote.hold_stopping &&
        s_input.remote.state != REMOTE_SERVO_ARMING)
    {
        /* 非遥控流程没有共同斜坡和逐轴命令的可信来源，不输出历史遥控速度。 */
        s_input.remote.target_rpm = 0;
        s_input.remote.commanded_rpm = s_input.mode == 7U ? 0 : 32767;
        s_input.remote.left_commanded_rpm = s_input.mode == 7U ? 0 : 32767;
        s_input.remote.right_commanded_rpm = s_input.mode == 7U ? 0 : 32767;
    }
    if (cli.state != CLI_SESSION_IDLE)
    {
        /* CLI没有双轴共同斜坡；未核验的目标及命令用协议无效值表示。 */
        s_input.remote.target_rpm = 32767;
        s_input.remote.commanded_rpm = 32767;
        s_input.remote.left_commanded_rpm = 32767;
        s_input.remote.right_commanded_rpm = 32767;
        s_input.remote.sync.active = s_input.remote.sync.pair_valid = 0U;
    }
    if (s_context.owner == APP_CONTROL_OWNER_HMI || s_context.owner == APP_CONTROL_OWNER_CAN)
    {
        s_input.remote.target_rpm = s_context.authorized ? s_context.target_rpm : 0;
        s_input.remote.left_commanded_rpm = s_context.command_rpm[0];
        s_input.remote.right_commanded_rpm = s_context.command_rpm[1];
        s_input.remote.commanded_rpm = s_context.command_rpm[
            app_config_servo_control_axis_mask() == 2U ? 1U : 0U];
        if (s_input.system_state != 10U && s_input.system_state != 12U)
            s_input.system_state = s_context.authorized ?
                (motion == MOTION_RUNNING ? 7U : 6U) : 8U;
        s_input.authorization_flags &= (uint8_t)~(2U | 8U | 64U);
    }
    if (s_input.alarm_code != s_alarm_code || s_input.alarm_source != s_alarm_source ||
        s_input.alarm_level != s_alarm_level || s_input.alarm_detail != s_alarm_detail)
    {
        s_alarm_sequence++;
        s_alarm_tick = now;
        s_alarm_code = s_input.alarm_code;
        s_alarm_source = s_input.alarm_source;
        s_alarm_level = s_input.alarm_level;
        s_alarm_detail = s_input.alarm_detail;
    }
    s_input.alarm_sequence = s_alarm_sequence;
    s_input.alarm_tick_ms = s_alarm_tick;
    s_input.valid_flags = (uint8_t)(1U | (all_feedback ? 2U : 0U) |
        (all_position && s_input.position.enabled ? 4U : 0U) |
        (s_input.position.pair_valid ? 8U : 0U) |
        (s_input.position.vehicle_valid ? 16U : 0U) |
        (s_input.remote.sync.pair_valid ? 32U : 0U) |
        (incomplete ? 128U : 0U));
    return true;
}
#endif

void nrf_status_telemetry_service(void)
{
#if APP_NRF_TELEMETRY_ENABLED
    uint32_t now;
    int32_t selected_age = INT32_MIN;
    uint8_t selected_index = 0U;
    uint8_t selected_distance = 0U;
    uint32_t age;
    uint8_t distance;
    uint8_t index;
    uint8_t slot;
    uint8_t type = 0U;
    uint8_t frame[32];
    NRF24L01_Stats radio;
    static const uint32_t period[10] = {
        APP_NRF_FAST_STATUS_PERIOD_MS, APP_NRF_DIAGNOSTIC_PERIOD_MS,
        APP_NRF_POSITION_PERIOD_MS, 250U, 2000U, APP_NRF_PARAMETER_PERIOD_MS,
        APP_NRF_ALARM_PERIOD_MS, APP_NRF_FAST_STATUS_PERIOD_MS,
        APP_NRF_SERVO_DIAGNOSTIC_PERIOD_MS, 1000U
    };
    NRF24L01_GetStats(&radio);
    if (radio.recovery_generation != s_radio_generation)
    {
        s_radio_generation = radio.recovery_generation;
        /* 只重建调度与快照；本次启动会话和状态序号继续递增。 */
        memset(s_last, 0, sizeof(s_last));
        memset(s_sent, 0, sizeof(s_sent));
        s_collected = 0U;
        s_fast_burst = 0U;
        s_schedule_cursor = 1U;
        s_alarm_sent_sequence = 0U;
        s_alarm_sent_valid = 0U;
        s_schedule_started = 0U;
    }
    if (!NRF24L01_CanQueueAck()) return;
    {static uint32_t arm_due;static uint8_t arm_type=0x8BU;uint32_t t=(uint32_t)((uint64_t)xTaskGetTickCount()*1000U/configTICK_RATE_HZ);
     if(app_arm3_remote_active() && (uint32_t)(t-arm_due)>=50U && app_arm3_port_ack(arm_type,frame,t)){
       if(NRF24L01_QueueAckPayload(frame)){arm_due=t;arm_type=arm_type==0x8BU?0x8CU:0x8BU;}return;}}
    now = (uint32_t)((uint64_t)xTaskGetTickCount() * 1000U / configTICK_RATE_HZ);
    if (!s_schedule_started) {
        s_schedule_start_ms = now;
        s_schedule_started = 1U;
    }
    /* 只在新一轮50ms采集窗口读取大快照，DMA唤醒不会重复格式化状态。 */
    if (!s_collected || now - s_collect_tick >= APP_NRF_FAST_STATUS_PERIOD_MS)
    {
        if (!nrf_collect(now)) return;
        s_collect_tick = now;
        s_collected = 1U;
    }
    /* Earliest deadline, with rotating ties. Reserve capacity for events rather
     * than consuming every ACK with the fast frame. Ten real wire types. */
    for (index = 0U; index < 10U; index++) {
        uint32_t deadline;
        if (index == 9U && s_input.snapshot_sequence == 0U) continue;
#if !APP_NRF_VEHICLE_SPEED_STATUS_ENABLED
        if (index == 7U) continue;
#endif
        deadline = s_sent[index] ? s_last[index] + period[index] : s_schedule_start_ms;
        if ((int32_t)(now - deadline) < 0) continue;
        age = now - deadline;
        distance = (uint8_t)((index + 10U - s_schedule_cursor) % 10U);
        {
            /* Fast status and vehicle speed have the tightest freshness
             * contract.  Give them an immediate-deadline score once their
             * nominal 100 ms period expires; this reserves two slots in each
             * 150 ms window even when every ACK opportunity is busy. */
            static const uint32_t budget[10] = {0U,278U,378U,378U,2200U,5200U,1200U,0U,778U,1200U};
            int32_t urgency = s_sent[index] ? (int32_t)(now - s_last[index]) - (int32_t)budget[index] : (int32_t)(now - s_schedule_start_ms) - (int32_t)budget[index];
            if (type == 0U || urgency > selected_age || (urgency == selected_age && distance < selected_distance)) {
                type = (uint8_t)(0x81U + index); selected_index = index;
                selected_age = urgency; selected_distance = distance;
            }
        }
    }
    /* First state then units: neither depends on a completed optional sample. */
    if (!s_sent[0]) type = 0x81U;
    else if (!s_sent[5]) type = 0x86U;
    else if (((!s_alarm_sent_valid && s_input.alarm_level != 0U) ||
              (s_alarm_sent_valid && s_input.alarm_sequence != s_alarm_sent_sequence)) &&
             (!s_sent[6] || now - s_last[6] >= 100U)) type = 0x87U;
    (void)selected_index;
    if (type == 0U) return;
    slot = (uint8_t)(type - 0x81U);
    if (!nrf_status_encode(frame, type, (uint8_t)(s_stats.last_sequence + 1U),
                           s_stats.session, &s_input)) return;
    taskENTER_CRITICAL();
    s_stats.generated++;
    s_stats.last_sequence++;
    s_stats.last_type = type;
    s_stats.alarm_code = s_input.alarm_code;
    s_stats.generated_by_type[slot]++;
    taskEXIT_CRITICAL();
    if (NRF24L01_QueueAckPayload(frame))
    {
        taskENTER_CRITICAL();
        age = s_sent[slot] ? now - s_last[slot] : 0U;
        s_last[slot] = now;
        s_sent[slot] = 1U;
        if (age > s_stats.max_age_ms[slot]) s_stats.max_age_ms[slot] = age;
        s_stats.queued_by_type[slot]++;
        if (age > period[slot]) s_stats.overdue_count[slot]++;
        if (type == 0x81U)
        {
            if (age >= APP_NRF_FAST_STATUS_URGENT_AGE_MS)
                s_stats.fast_urgent_count++;
            if (s_fast_burst < APP_NRF_FAST_STATUS_MAX_BURST) s_fast_burst++;
        }
        else
        {
            if (s_fast_burst >= APP_NRF_FAST_STATUS_MAX_BURST &&
                now - s_last[0] >= period[0]) s_stats.fast_burst_limited++;
            s_fast_burst = 0U;
            s_schedule_cursor = (uint8_t)(slot + 1U);
            if (s_schedule_cursor >= 10U) s_schedule_cursor = 0U;
            if (type == 0x87U)
            {
                if (!s_alarm_sent_valid || s_input.alarm_sequence != s_alarm_sent_sequence)
                    s_stats.alarm_event_priority_count++;
                s_alarm_sent_sequence = s_input.alarm_sequence;
                s_alarm_sent_valid = 1U;
            }
        }
        taskEXIT_CRITICAL();
    }
    else
    {
        taskENTER_CRITICAL();
        s_stats.queue_failures++;
        taskEXIT_CRITICAL();
    }
#endif
}

void nrf_status_telemetry_get_stats(nrf_telemetry_stats_t *stats)
{
    if (stats == NULL) return;
    taskENTER_CRITICAL();
    *stats = s_stats;
    taskEXIT_CRITICAL();
}

#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
uint8_t nrf_status_telemetry_diag_words(uint32_t *words, uint8_t phase)
{
    uint8_t i, index, count = 0U;
    if (words == NULL) return 0U;
    taskENTER_CRITICAL();
    if (phase == 0U) {
        words[0] = s_stats.generated; words[1] = s_stats.queue_failures;
        words[2] = s_stats.session | ((uint32_t)s_stats.last_sequence << 8U) |
            ((uint32_t)s_stats.last_type << 16U) | ((uint32_t)s_stats.alarm_code << 24U);
        words[3] = s_stats.enabled; words[4] = s_stats.alarm_event_priority_count;
        words[5] = s_stats.fast_burst_limited; words[6] = s_stats.fast_urgent_count;
        words[7] = APP_NRF_TELEMETRY_ENABLED;
        count = 8U;
    } else if (phase <= 2U) {
        for (i = 0U; i < 5U; ++i) {
            index = (uint8_t)((phase - 1U) * 5U + i);
            words[i * 4U] = s_stats.generated_by_type[index];
            words[i * 4U + 1U] = s_stats.queued_by_type[index];
            words[i * 4U + 2U] = s_stats.max_age_ms[index];
            words[i * 4U + 3U] = s_stats.overdue_count[index];
        }
        count = 20U;
    }
    taskEXIT_CRITICAL();
    return count;
}
#endif
