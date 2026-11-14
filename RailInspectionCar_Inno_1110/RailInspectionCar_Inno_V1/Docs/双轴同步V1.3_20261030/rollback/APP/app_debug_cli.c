#include "app_debug_cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "app_config.h"
#include "app_health.h"
#include "app_lcd_show.h"
#include "app_nrf_remote.h"
#include "nrf_status_telemetry.h"
#include "NRF24L01.h"
#include "app_remote_servo.h"
#include "app_position_monitor.h"
#include "aimtor_servo.h"
#include "bsp_rs485.h"
#include "bsp_system.h"
#include "debug_uart.h"
#include "modbus_rtu.h"
#include "motion_control.h"
#include "servo_debug_control.h"
#include "app_cli_servo_session.h"
#include "app_can.h"
#include "app_hmi.h"
#include "app_control_arbiter.h"
#include "app_normal_ramp.h"
#include <errno.h>
#include "servo_pulse.h"
#include "usart.h"

#define CLI_LINE_SIZE       (96U)
#define CLI_QUEUE_LENGTH    (4U)

/* CLI命令行输入缓冲区，用于暂存当前正在接收的一行命令文本。 */
static char s_build_line[CLI_LINE_SIZE];

/* 当前已接收的命令行长度，单位为字节。
 * 使用volatile表示该变量可能在中断或其他异步执行上下文中被修改。
 */
static volatile uint16_t s_build_length;

/* 命令行缓冲区溢出标志：
 * 0表示当前输入未溢出；
 * 1表示接收内容超过CLI_LINE_SIZE，需要丢弃或等待当前行结束。
 */
static volatile uint8_t s_line_overflow;

/* CLI完整命令行消息队列。
 * 接收到回车换行并完成一行命令后，将命令发送到该队列，
 * 由CLI任务统一读取和解析。
 */
static QueueHandle_t s_line_queue;
#if (APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED == 1U)
static TickType_t s_sync_debug_last_tick;
static uint8_t s_sync_debug_session_active;
static uint8_t s_sync_debug_header_printed;
static char s_sync_debug_line[768];
#endif
static uint32_t s_sync_debug_skipped;
static uint32_t s_position_summary_skipped;

int fputc(int ch, FILE *file)
{
    (void)file;
    debug_uart_write_char((uint8_t)ch);
    return ch;
}


void app_cli_init(void)
{
    s_line_queue = xQueueCreate(CLI_QUEUE_LENGTH, CLI_LINE_SIZE);
    s_build_length = 0U;
    s_line_overflow = 0U;
    s_sync_debug_skipped = 0U;
    s_position_summary_skipped = 0U;
#if (APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED == 1U)
    s_sync_debug_last_tick = 0U;
    s_sync_debug_session_active = 0U;
    s_sync_debug_header_printed = 0U;
#endif
    debug_uart_init();
}

void app_cli_uart_rx_complete_isr(void)
{
    BaseType_t higher_priority_task_woken;
    uint8_t rx_byte;

    higher_priority_task_woken = pdFALSE;
    rx_byte = debug_uart_get_rx_byte();
    if ((rx_byte == '\r') || (rx_byte == '\n'))
    {
        if (!s_line_overflow && (s_build_length > 0U) && (s_line_queue != NULL))
        {
            s_build_line[s_build_length] = '\0';
            (void)xQueueSendFromISR(s_line_queue, s_build_line,
                                    &higher_priority_task_woken);
        }
        s_build_length = 0U;
        s_line_overflow = 0U;
    }
    else if (!s_line_overflow && s_build_length < (CLI_LINE_SIZE - 1U))
    {
        s_build_line[s_build_length] = (char)rx_byte;
        s_build_length++;
    }
    else
    {
        /* 超长行整体丢弃，不把后半段误当成新的运动指令。 */
        s_line_overflow = 1U;
    }
    debug_uart_restart_rx_isr();
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void app_cli_uart_error_isr(void)
{
    debug_uart_restart_rx_isr();
}

static const char *app_cli_mode_name(servo_control_mode_t mode)
{
    if (mode == SERVO_CONTROL_PULSE_POSITION)
    {
        return "pulse";
    }
    if (mode == SERVO_CONTROL_RS485_VELOCITY)
    {
        return "speed";
    }
    if (mode == SERVO_CONTROL_RS485_SEGMENT_POSITION)
    {
        return "segment";
    }
    return "none";
}

static bool app_cli_parse_axis(const char *text, servo_axis_t *axis)
{
    if ((text == NULL) || (axis == NULL))
    {
        return false;
    }
    if ((strcmp(text, "left") == 0) || (strcmp(text, "l") == 0))
    {
        *axis = SERVO_AXIS_LEFT;
        return true;
    }
    if ((strcmp(text, "right") == 0) || (strcmp(text, "r") == 0))
    {
        *axis = SERVO_AXIS_RIGHT;
        return true;
    }
    return false;
}

static bool app_cli_axis_is_controllable(servo_axis_t axis)
{
    return app_config_servo_axis_controllable((uint8_t)axis) != 0U;
}

static void app_cli_print_session(void)
{
    cli_servo_session_status_t status;
    (void)app_cli_servo_session_get_status(&status);
    printf("control owner=%s session=%lu generation=%lu axis=%s "
           "state=%s authorized=%u remaining_ms=%lu mode=%s debug=%s "
           "reason=%s result=%u pending=%u recoverable=%u\r\n",
           app_cli_servo_control_owner(), (unsigned long)status.session_id,
           (unsigned long)status.generation,
           status.axis == SERVO_AXIS_RIGHT ? "right" : "left",
           app_cli_servo_session_state_name(status.state), status.authorized,
           (unsigned long)status.remaining_ms, servo_debug_mode_name(status.mode),
           servo_debug_state_name(status.debug_state),
           app_cli_servo_session_reason_name(status.reason),
           (unsigned int)status.last_result, status.pending, status.recoverable);
}

static void app_cli_print_control_summary(void)
{
    cli_servo_session_status_t status;
    (void)app_cli_servo_session_get_status(&status);
    printf("control owner=%s session=%lu axis=%s state=%s cli_authorized=%u\r\n",
           app_cli_servo_control_owner(), (unsigned long)status.session_id,
           status.axis == SERVO_AXIS_RIGHT ? "right" : "left",
           app_cli_servo_session_state_name(status.state), status.authorized);
}

/* Cached read only snapshots: no Modbus request or periodic output. */
static void app_cli_print_ramp(void)
{
    static app_normal_ramp_status_t normal;
    static servo_snapshot_t axes[2];
    app_control_owner_t owner;
    uint32_t now;
    uint8_t axis;
    taskENTER_CRITICAL();
    owner = app_control_arbiter_owner();
    memset(&normal, 0, sizeof(normal));
    if (owner == APP_CONTROL_OWNER_HMI) app_hmi_control_ramp_status(&normal);
    else if (owner == APP_CONTROL_OWNER_REMOTE) app_remote_servo_ramp_status(&normal);
    else if (owner == APP_CONTROL_OWNER_CAN) app_can_control_ramp_status(&normal);
    (void)aimtor_get_snapshot_pair(&axes[0], &axes[1]);
    now = HAL_GetTick();
    taskEXIT_CRITICAL();
    printf("ramp owner=%s(%u) source_authorized=%u phase=%u now=%lu cli_authorized=%u\r\n",
        app_cli_servo_control_owner(), (unsigned)owner, normal.authorized, normal.phase,
        (unsigned long)now, app_cli_servo_session_active() ? 1U : 0U);
    printf("target=%d,%d internal=%d,%d batch=%d,%d accel/decel=150/300mm_s2\r\n",
        normal.target[0], normal.target[1], normal.internal[0], normal.internal[1],
        normal.batch[0], normal.batch[1]);
    printf("service_max=%lu clipped=%lu dropped=%lu integrated=%lu batches=%lu batch_max=%lu ms\r\n",
        (unsigned long)normal.clock.max_service_ms, (unsigned long)normal.clock.clipped,
        (unsigned long)normal.clock.discarded_ms, (unsigned long)normal.clock.integrated_ms,
        (unsigned long)normal.clock.batches, (unsigned long)normal.clock.max_batch_ms);
    for (axis = 0U; axis < 2U; ++axis) {
        const servo_snapshot_t *a = &axes[axis];
        printf("%c ack=%d@%lu valid=%u actual=%d@%lu valid=%u age=%lu read=%u/%u masks=%u/%u\r\n",
            axis ? 'R' : 'L', a->speed_write_rpm, (unsigned long)a->speed_write_tick,
            a->speed_write_rpm_valid, a->actual_speed_rpm,
            (unsigned long)((uint64_t)a->speed_sample_tick * 1000U / configTICK_RATE_HZ),
            a->speed_valid, (unsigned long)a->speed_age_ms,
            a->ramp_readback_ms[0], a->ramp_readback_ms[1], a->ramp_read_mask, a->ramp_match_mask);
    }
}

static bool app_cli_number(const char *text, int32_t minimum, int32_t maximum,
                           int32_t *value)
{
    char *end;
    long parsed;
    if (!text || !text[0]) return false;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno == ERANGE || *end || parsed < minimum || parsed > maximum)
        return false;
    *value = (int32_t)parsed;
    return true;
}

static bool app_cli_session_number(const char *text, uint32_t *value)
{
    char *end;
    unsigned long parsed;
    if (!text || !text[0] || text[0] == '-') return false;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno == ERANGE || *end || parsed == 0UL ||
        (uint64_t)parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

/* 在旧命令分派前统一处理授权及写请求，避免别名绕过会话轴和代次。 */
static bool app_cli_session_command(const char *line)
{
    char token[7][20];
    char extra;
    int count;
    int action_index;
    int arg;
    int32_t value;
    int32_t speed;
    int32_t ramp;
    servo_axis_t axis;
    cli_servo_request_t request;
    cli_servo_session_status_t status;
    servo_result_t result;
    uint32_t session;
    uint8_t parsed = 1U;
    uint8_t stop;
    uint8_t disable;
    const char *action;
    count = sscanf(line, "%19s %19s %19s %19s %19s %19s %19s %c",
                   token[0], token[1], token[2], token[3],
                   token[4], token[5], token[6], &extra);
    if (count < 1) return false;
    if (strcmp(token[0], "servo") != 0)
    {
        if (strcmp(line, "motion stop") != 0) return false;
        if (app_cli_servo_session_active()) app_cli_servo_session_stop();
        else
        {
            app_remote_servo_request_estop();
            aimtor_stop_all();
        }
        printf("motion stop=queued\r\n");
        return true;
    }
    if (count < 2) return false;
    if (strcmp(token[1], "debug") == 0)
    {
        if (count == 3 && strcmp(token[2], "status") == 0)
            app_cli_print_session();
        else if (count == 4 && strcmp(token[2], "on") == 0 &&
                 app_cli_parse_axis(token[3], &axis))
        {
            session = 0U;
            result = app_cli_servo_session_on(axis, &session);
            printf("servo debug on=%s result=%u session_id=%lu\r\n",
                   result == SERVO_OK ? "queued" : "rejected",
                   (unsigned int)result, (unsigned long)session);
            if (result != SERVO_OK)
            {
                printf("reason=%s\r\n",
                    !APP_SERVO_CLI_DEBUG_ENABLED ? "debug disabled" :
                    app_config_servo_readonly_test() ? "readonly configuration" :
                    !app_cli_axis_is_controllable(axis) ? "axis not configured" :
                    app_remote_servo_is_active() ? "remote owns control" :
                    result == SERVO_BUSY ? "session or remote busy" :
                    !aimtor_startup_hold_ready() ? "startup hold not ready" :
                    "startup/zero/fault/idle confirmation not ready");
                app_cli_print_session();
            }
        }
        else if (count == 4 && strcmp(token[2], "keepalive") == 0 &&
                 app_cli_session_number(token[3], &session))
            printf("servo debug keepalive=%s\r\n",
                   app_cli_servo_session_keepalive(session) ?
                   "accepted" : "rejected");
        else if (count == 3 && strcmp(token[2], "off") == 0)
            printf("servo debug off=%u queued_if_ok\r\n",
                   (unsigned int)app_cli_servo_session_off());
        else printf("invalid servo debug command\r\n");
        return true;
    }
    if (strcmp(line, "servo emergency") == 0)
    {
        if (app_cli_servo_session_active())
            app_cli_servo_session_abort(CLI_EXIT_MANUAL);
        else
        {
            app_remote_servo_request_estop();
            aimtor_emergency_stop_k1();
        }
        printf("servo emergency=queued; explicit reauthorization required\r\n");
        return true;
    }
    if (strcmp(line, "servo clear") == 0 && app_cli_servo_session_active())
    {
        printf("session locked; use a new servo debug on after safe confirmation\r\n");
        return true;
    }
    /* 查询保留原行为，不延长授权；其他未识别写命令必须明确拒绝。 */
    if ((count == 3 && strcmp(token[2], "status") == 0) ||
        strcmp(line, "servo probe") == 0 || strcmp(line, "servo read") == 0 ||
        (strcmp(line, "servo clear") == 0 && !app_cli_servo_session_active()))
        return false;
    (void)app_cli_servo_session_get_status(&status);
    axis = status.axis;
    if (count >= 3 && (app_cli_parse_axis(token[1], &axis) ||
                      strcmp(token[1], "current") == 0))
        action_index = 2;
    else if (count >= 3 && app_cli_parse_axis(token[2], &axis))
        action_index = 1;
    else
    {
        printf("CLI write rejected: independent debug authorization required\r\n");
        return true;
    }
    action = token[action_index];
    arg = 3;
    stop = strcmp(action, "stop") == 0;
    disable = strcmp(action, "disable") == 0;
    if (stop || disable)
    {
        if (app_cli_servo_session_active())
        {
            /* 非会话轴停止同样不能被授权门禁吞掉，按整组急停处理。 */
            if (disable || axis != status.axis)
                app_cli_servo_session_abort(CLI_EXIT_MANUAL);
            else app_cli_servo_session_stop();
        }
        else
        {
            if (app_remote_servo_is_active()) app_remote_servo_request_estop();
            if (disable) aimtor_emergency_stop_k1();
            else (void)aimtor_stop(axis);
        }
        printf("servo %s=queued\r\n", action);
        return true;
    }
    if (status.state != CLI_SESSION_READY || !status.authorized ||
        axis != status.axis)
    {
        printf("CLI command rejected: %s\r\n",
               axis != status.axis ? "axis mismatch" :
               status.state == CLI_SESSION_STOPPING ||
               status.state == CLI_SESSION_RESTORING ? "stopping or restoring" :
               !aimtor_startup_hold_ready() ? "startup hold not ready" :
               app_remote_servo_is_active() ? "remote owns control" :
               "unauthorized; use servo debug on left|right and wait for ready");
        return true;
    }
    memset(&request, 0, sizeof(request));
    request.axis = axis;
    if (strcmp(action, "mode") == 0 && count == 4)
    {
        request.kind = CLI_REQUEST_MODE;
        request.mode = strcmp(token[arg], "speed") == 0 ?
            SERVO_DEBUG_MODE_RS485_SPEED :
            strcmp(token[arg], "position") == 0 ||
            strcmp(token[arg], "segment") == 0 ? SERVO_DEBUG_MODE_RS485_POSITION :
            strcmp(token[arg], "pulse") == 0 ? SERVO_DEBUG_MODE_PULSE :
            SERVO_DEBUG_MODE_NONE;
        parsed = request.mode != SERVO_DEBUG_MODE_NONE;
    }
    else if (strcmp(action, "enable") == 0 && count == 3)
        request.kind = CLI_REQUEST_ENABLE;
    else if (strcmp(action, "speed") == 0 && count == 4)
    {
        request.kind = CLI_REQUEST_SPEED;
        parsed = app_cli_number(token[arg], -APP_OPERATION_MAX_MOTOR_RPM,
                                APP_OPERATION_MAX_MOTOR_RPM, &request.value);
        if (parsed && request.value == 0)
        {
            app_cli_servo_session_stop();
            printf("servo speed zero=queued stop\r\n");
            return true;
        }
    }
    else if ((strcmp(action, "position") == 0 ||
              strcmp(action, "segment") == 0) && count == 6)
    {
        request.kind = CLI_REQUEST_POSITION;
        parsed = app_cli_number(token[arg], INT32_MIN, INT32_MAX, &request.value) &&
            request.value != 0 &&
            app_cli_number(token[arg + 1], 1, APP_OPERATION_MAX_MOTOR_RPM, &speed) &&
            app_cli_number(token[arg + 2], 1, 65535, &ramp);
        if (parsed) { request.speed = (uint16_t)speed; request.ramp = (uint16_t)ramp; }
    }
    else if (strcmp(action, "pulse") == 0 && count == 6)
    {
        request.kind = CLI_REQUEST_PULSE;
        parsed = (strcmp(token[arg], "forward") == 0 ||
                  strcmp(token[arg], "reverse") == 0) &&
            app_cli_number(token[arg + 1], APP_PULSE_MIN_HZ,
                           APP_DEBUG_MAX_PULSE_HZ, &value) &&
            app_cli_number(token[arg + 2], 1, INT32_MAX, &request.value);
        if (parsed)
        {
            request.frequency = (uint32_t)value;
            request.forward = strcmp(token[arg], "forward") == 0;
        }
    }
    else if (strcmp(action, "verify") == 0 && count == 3)
        request.kind = CLI_REQUEST_VERIFY;
    else if (strcmp(action, "zero") == 0 && count == 3)
        request.kind = CLI_REQUEST_ZERO;
    else if (strcmp(action, "save") == 0 && count == 3)
        request.kind = CLI_REQUEST_SAVE;
    else parsed = 0U;
    result = parsed ? app_cli_servo_session_request(&request) : SERVO_INVALID_ARGUMENT;
    printf("CLI %s=%s result=%u\r\n", action,
           result == SERVO_OK ? "queued" : "rejected", (unsigned int)result);
    return true;
}

static servo_result_t app_cli_current_enable(void)
{
    servo_result_t result = SERVO_OK;
    servo_axis_t axis;

    /*
     * 双轴调试命令需要原子化事务；当前接口没有提供跨轴配置序列，
     * 禁止出现只使能第一轴却向上层返回成功的半完成状态。
     */
    if (app_config_servo_control_axis_count() > 1U)
    {
        return SERVO_NOT_CONFIGURED;
    }
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_cli_axis_is_controllable(axis))
        {
            continue;
        }
        result = aimtor_enable(axis);
        if ((result != SERVO_OK) && (result != SERVO_BUSY))
        {
            return result;
        }
    }
    return result;
}

static servo_result_t app_cli_current_disable(void)
{
    servo_result_t result = SERVO_OK;
    servo_axis_t axis;

    /* 双轴失能由急停/联停路径统一处理，避免单轴异步失能造成状态不一致。 */
    if (app_config_servo_control_axis_count() > 1U)
    {
        return SERVO_NOT_CONFIGURED;
    }
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_cli_axis_is_controllable(axis))
        {
            continue;
        }
        result = aimtor_disable(axis);
        if ((result != SERVO_OK) && (result != SERVO_BUSY))
        {
            return result;
        }
    }
    return result;
}

static servo_result_t app_cli_current_stop(void)
{
    if (app_remote_servo_is_active())
    {
        app_remote_servo_request_estop();
    }
    aimtor_emergency_stop();
    return SERVO_OK;
}

static servo_result_t app_cli_current_mode(const char *mode_text)
{
    servo_control_mode_t mode;
    servo_axis_t axis;
    servo_result_t result;

    if (strcmp(mode_text, "speed") == 0)
    {
        mode = SERVO_CONTROL_RS485_VELOCITY;
    }
    else
    {
        return SERVO_NOT_CONFIGURED;
    }
    if (app_config_servo_control_axis_count() > 1U)
    {
        return SERVO_NOT_CONFIGURED;
    }
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_cli_axis_is_controllable(axis))
        {
            continue;
        }
        result = aimtor_set_control_mode(axis, mode);
        if ((result != SERVO_OK) && (result != SERVO_BUSY))
        {
            return result;
        }
    }
    return SERVO_OK;
}

static servo_result_t app_cli_current_speed(int rpm)
{
    servo_result_t result = SERVO_OK;
    servo_axis_t axis;

    if ((rpm < -APP_OPERATION_MAX_MOTOR_RPM) ||
        (rpm > APP_OPERATION_MAX_MOTOR_RPM))
    {
        return SERVO_INVALID_ARGUMENT;
    }
    if (app_config_servo_control_axis_count() > 1U)
    {
        return SERVO_NOT_CONFIGURED;
    }
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_cli_axis_is_controllable(axis))
        {
            continue;
        }
        result = aimtor_set_velocity_rpm(axis, (int16_t)rpm);
        if ((result != SERVO_OK) && (result != SERVO_BUSY))
        {
            return result;
        }
    }
    return result;
}

static const char *app_cli_query_mode_name(void)
{
#if (APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_LEFT_ONLY)
    return "left";
#elif (APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_RIGHT_ONLY)
    return "right";
#else
    return "both";
#endif
}

/* 统一输出数据年龄；UINT32_MAX表示从未采集到有效数据。 */
static void app_cli_print_age(uint32_t age_ms)
{
    if (age_ms == UINT32_MAX)
    {
        printf("NA");
    }
    else
    {
        printf("%lu", (unsigned long)age_ms);
    }
}

// 输出控制状态、双轴在线状态、参数核验状态和故障计数。
static void app_cli_print_hold_status(void)
{
    servo_axis_t axis;
    servo_startup_hold_status_t hold;
    servo_snapshot_t snapshot;
    printf("startup_hold enabled=%u eligible=%u ready=%u motion_allowed=%u\r\n",
           (unsigned int)APP_SERVO_STARTUP_HOLD_ENABLED,
           (unsigned int)aimtor_startup_hold_active(),
           (unsigned int)aimtor_startup_hold_ready(),
           (unsigned int)aimtor_motion_allowed());
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        (void)aimtor_startup_hold_get_status(axis, &hold);
        (void)aimtor_get_snapshot(axis, &snapshot);
        printf("%c hold_state=%s enabled=%u zero=%u enable_ack=%u verify=%u fault=0x%04X failure=%u disturbed=%u\r\n",
               axis == SERVO_AXIS_LEFT ? 'L' : 'R',
               aimtor_startup_hold_state_name(hold.state),
               (unsigned int)snapshot.enabled, (unsigned int)hold.zero_written,
               (unsigned int)hold.enable_ack, (unsigned int)hold.verified,
               snapshot.fault_code, (unsigned int)hold.failure,
               (unsigned int)hold.disturbed);
    }
}

static void app_cli_print_position_summary(void)
{
    position_monitor_status_t status;
    debug_uart_stats_t tx;
    char line[128];
    int length;
    (void)app_position_monitor_get_status(&status);
    length = snprintf(line, sizeof(line),
           "position=%s valid=%u pair=%u gen=%lu gaps=%lu\r\n",
           app_position_monitor_state_name(status.state),
           status.vehicle_valid, status.pair_valid,
           (unsigned long)status.baseline_generation,
           (unsigned long)status.lost_intervals);
    debug_uart_get_stats(&tx);
    /* 摘要不挤占原有长响应的容量，完整信息可用独立命令查询。 */
    if (length <= 0 || (size_t)length >= sizeof(line) ||
        tx.buffer_free < (uint16_t)length)
    {
        s_position_summary_skipped++;
        return;
    }
    printf("%s", line);
}

static void app_cli_print_position(void)
{
    position_monitor_status_t status;
    uint8_t axis;
    (void)app_position_monitor_get_status(&status);
    printf("position enabled=%u state=%s source_mask=%u valid=%u sampled_wheel_estimate=1\r\n",
           status.enabled, app_position_monitor_state_name(status.state),
           status.source_mask, status.vehicle_valid);
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        const position_axis_status_t *a = &status.axis[axis];
        printf("%c selected=%u state=%s valid=%u raw=%ld seq=%lu age=%lu gen=%lu incomplete=%u gaps=%lu\r\n",
               axis == 0U ? 'L' : 'R', a->selected,
               app_position_monitor_state_name(a->state), a->valid,
               (long)a->raw_position, (unsigned long)a->sequence,
               (unsigned long)a->age_ms, (unsigned long)a->generation,
               a->incomplete, (unsigned long)a->lost_intervals);
        printf("%c boot_net=%.3fm boot_path=%.3fm trip_net=%.3fm trip_path=%.3fm trip_anchor=%u\r\n",
               axis == 0U ? 'L' : 'R', a->boot_net_mm / 1000.0,
               a->boot_path_mm / 1000.0, a->trip_net_mm / 1000.0,
               a->trip_path_mm / 1000.0, a->trip_anchor_pending);
        printf("%c boot_counts=%lld path_counts=%llu trip_counts=%lld trip_path_counts=%llu\r\n",
               axis == 0U ? 'L' : 'R', (long long)a->boot_net_counts,
               (unsigned long long)a->boot_path_counts,
               (long long)a->trip_net_counts, (unsigned long long)a->trip_path_counts);
    }
    printf("vehicle boot_net=%.3fm boot_path=%.3fm trip_net=%.3fm trip_path=%.3fm valid=%u\r\n",
           status.vehicle_boot_net_mm / 1000.0, status.vehicle_boot_path_mm / 1000.0,
           status.vehicle_trip_net_mm / 1000.0, status.vehicle_trip_path_mm / 1000.0,
           status.vehicle_valid);
    printf("relative valid=%u gen=%lu auth_gen=%lu raw_diff=%lld L=%lld R=%lld diff=%lld diff_mm=%.3f "
           "seq=%lu/%lu age=%lu skew=%lu pairs=%lu\r\n",
           status.pair_valid, (unsigned long)status.baseline_generation,
           (unsigned long)status.authorization_generation,
           (long long)status.raw_difference_counts,
           (long long)status.relative_counts[0], (long long)status.relative_counts[1],
           (long long)status.difference_counts, status.difference_mm,
           (unsigned long)status.pair_sequence[0], (unsigned long)status.pair_sequence[1],
           (unsigned long)status.pair_age_ms, (unsigned long)status.sample_skew_ms,
           (unsigned long)status.pair_count);
    printf("rate_valid=%u rate_mm_s=%.3f skew_est_valid=%u skew_est_mm=%.3f "
           "reset_pending=%u resets=%lu reset_rejected=%lu gaps=%lu\r\n",
           status.rate_valid, status.difference_rate_mm_s,
           status.skew_estimate_valid, status.skew_estimate_mm,
           status.trip_reset_pending, (unsigned long)status.trip_reset_count,
           (unsigned long)status.trip_reset_rejected, (unsigned long)status.lost_intervals);
    printf("invalid fields retain last values; sampled path may miss within-sample reversals\r\n");
}

static void app_cli_print_status(void)
{
    app_cli_print_control_summary();
    servo_snapshot_t left;
    servo_snapshot_t right;
    servo_boot_zero_status_t boot;
    servo_startup_stop_status_t startup;
    servo_wdg_recovery_status_t wdg;
    servo_axis_t axis;
    servo_snapshot_t snapshot;
    servo_wdg_recovery_state_t wdg_state = SERVO_WDG_RECOVERY_SKIPPED;
    servo_safety_stop_status_t safety;
    servo_recovery_status_t recovery;

    (void)aimtor_get_safety_stop_status(&safety);
    aimtor_get_recovery_status(&recovery);
    printf("recovery state=%s eligible=%u source=%u failure=%u healthy_ms=%lu generation=%lu first=%u/%u/%u tx=%lu result=%lu age=%lu since=%lu reg=%04X\r\n",
           aimtor_recovery_state_name(recovery.state),
           (unsigned int)recovery.eligible, (unsigned int)recovery.source,
           (unsigned int)recovery.failure, (unsigned long)recovery.healthy_ms,
           (unsigned long)recovery.generation, (unsigned int)recovery.first_axis,
           (unsigned int)recovery.first_item, (unsigned int)recovery.first_failures,
           (unsigned long)recovery.first_transaction,
           (unsigned long)recovery.first_result,
           (unsigned long)recovery.first_age_ms,
           (unsigned long)recovery.first_failure_ms, recovery.first_address);
    printf("safety class=%u reason=%u initial=%u source=%u axes=%u active=%u done=%u escalated=%u start=%lu end=%lu timeout=%lu\r\n",
           (unsigned int)motion_get_stop_class(), (unsigned int)motion_get_fault_reason(),
           safety.initial_reason, safety.source, safety.axis_mask,
           safety.active, safety.completed, safety.escalated,
           (unsigned long)safety.started_ms, (unsigned long)safety.completed_ms,
           (unsigned long)safety.timeout_ms);
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
        printf("%c safety rpm=%d cmd=%d mode=%u speed_age=%lu fault_age=%lu fault=%04X failures=%u mb=%lu addr=%04X stop_write_failures=%u\r\n",
               axis == SERVO_AXIS_LEFT ? 'L' : 'R', safety.actual_rpm[axis],
               safety.command_rpm[axis], safety.mode[axis],
               (unsigned long)safety.speed_age_ms[axis], (unsigned long)safety.fault_age_ms[axis],
               safety.fault_code[axis], safety.failures[axis],
               (unsigned long)safety.last_modbus_result[axis], safety.last_address[axis],
               safety.write_failures[axis]);

    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        servo_cold_clear_status_t cold;
        (void)aimtor_cold_clear_get_status(axis, &cold);
        printf("%c cold_clear state=%u eligible=%u original=%04X attempted=%u ack=%u readback=%04X disable=%u zeros=%u failure=%u elapsed=%lu mb=%lu\r\n",
            axis == SERVO_AXIS_LEFT ? 'L' : 'R', (unsigned int)cold.state,
            cold.eligible, cold.original_fault, cold.attempted, cold.write_ack,
            cold.readback_fault, cold.disable_attempted, cold.zero_samples,
            (unsigned int)cold.failure, (unsigned long)cold.elapsed_ms, (unsigned long)cold.last_result);
    }
    printf("startup_stop ready=%u\r\n",
           (unsigned int)aimtor_startup_stop_ready());
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        (void)aimtor_watchdog_recovery_get_status(axis, &wdg);
        if (!wdg.eligible) continue;
        if (wdg.state == SERVO_WDG_RECOVERY_FAILED)
        {
            wdg_state = wdg.state;
            break;
        }
        if (wdg_state == SERVO_WDG_RECOVERY_SKIPPED ||
            (wdg_state == SERVO_WDG_RECOVERY_DONE &&
             wdg.state != SERVO_WDG_RECOVERY_DONE))
            wdg_state = wdg.state;
    }
    printf("wdg_recovery state=%s ready=%u\r\n",
           aimtor_watchdog_recovery_state_name(wdg_state),
           (unsigned int)aimtor_watchdog_recovery_ready());
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        (void)aimtor_watchdog_recovery_get_status(axis, &wdg);
        printf("%c wdg_state=%s eligible=%u attempts=%u zero=%u disable=%u verify=%u failure=%u\r\n",
               axis == SERVO_AXIS_LEFT ? 'L' : 'R',
               aimtor_watchdog_recovery_state_name(wdg.state),
               (unsigned int)wdg.eligible, (unsigned int)wdg.attempts,
               (unsigned int)wdg.zero_written,
               (unsigned int)wdg.disable_ack,
               (unsigned int)wdg.verified,
               (unsigned int)wdg.failure_reason);
        if (wdg.eligible)
            printf("  ramp=%u/%u start=%d command=%d decel=%u elapsed=%lu timeout=%lu zeros=%u stopped=%u cfg=%u/%u/%u son=%u\r\n",
                   (unsigned int)wdg.ramp_applicable, (unsigned int)wdg.ramp_verified,
                   (int)wdg.start_speed_rpm, (int)wdg.command_readback_rpm,
                   (unsigned int)wdg.decel_readback_ms,
                   (unsigned long)wdg.stop_elapsed_ms, (unsigned long)wdg.stop_timeout_ms,
                   (unsigned int)wdg.zero_samples, (unsigned int)wdg.stopped,
                   (unsigned int)wdg.mode_readback, (unsigned int)wdg.selection_readback,
                   (unsigned int)wdg.source_readback, (unsigned int)wdg.enable_readback);
    }
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        (void)aimtor_startup_stop_get_status(axis, &startup);
        printf("%c startup_state=%s attempts=%u disable_ack=%u off_confirmed=%u zero_confirmed=%u failure=%u\r\n",
               axis == SERVO_AXIS_LEFT ? 'L' : 'R',
               aimtor_startup_stop_state_name(startup.state),
               (unsigned int)startup.attempts, (unsigned int)startup.disable_ack,
               (unsigned int)startup.off_confirmed,
               (unsigned int)startup.zero_confirmed, (unsigned int)startup.failure);
    }
    app_cli_print_hold_status();
    printf("boot_zero ready=%u\r\n", (unsigned int)aimtor_boot_zero_ready());
    for (axis = SERVO_AXIS_LEFT; axis < SERVO_AXIS_COUNT; axis++)
    {
        (void)aimtor_boot_zero_get_status(axis, &boot);
        (void)aimtor_get_snapshot(axis, &snapshot);
        printf("%c zero_state=%s eligible=%u attempts=%u write_ack=%u failure=%u position=%ld\r\n",
               axis == SERVO_AXIS_LEFT ? 'L' : 'R',
               aimtor_boot_zero_state_name(boot.state),
               (unsigned int)boot.eligible, (unsigned int)boot.attempts,
               (unsigned int)boot.write_ack, (unsigned int)boot.failure,
               (long)snapshot.actual_position);
    }

    (void)aimtor_get_snapshot(SERVO_AXIS_LEFT, &left);
    (void)aimtor_get_snapshot(SERVO_AXIS_RIGHT, &right);
    printf("state=%u hw=%u bus=%u param=%u maint=%u map=%u diag=0x%08lX\r\n",
           (unsigned int)motion_get_state(),
           (unsigned int)app_config_motion_hardware_ready(),
           (unsigned int)app_config_servo_bus_ready(),
           (unsigned int)app_config_servo_parameters_confirmed(),
           (unsigned int)aimtor_maintenance_enabled(),
           (unsigned int)aimtor_register_map_valid(),
           (unsigned long)app_health_get_diagnostic_faults());
    printf("query=%s readonly=%u L_selected=%u R_selected=%u\r\n",
           app_cli_query_mode_name(),
           (unsigned int)app_config_servo_readonly_test(),
           (unsigned int)app_config_servo_axis_selected(SERVO_AXIS_LEFT),
           (unsigned int)app_config_servo_axis_selected(SERVO_AXIS_RIGHT));
     printf("L addr=%u online=%u enable=%u verify=%u mode=%s rpm=%d fault=0x%04X fail=%u fb=%u/%u/%u degraded=%u\r\n",
           (unsigned int)aimtor_get_slave_address(SERVO_AXIS_LEFT),
           left.online, left.enabled, left.parameter_verified,
           app_cli_mode_name(left.control_mode), left.actual_speed_rpm,
            left.fault_code, left.consecutive_failures,
            left.feedback_failures[0], left.feedback_failures[1],
            left.feedback_failures[2], left.feedback_degraded);
     printf("R addr=%u online=%u enable=%u verify=%u mode=%s rpm=%d fault=0x%04X fail=%u fb=%u/%u/%u degraded=%u\r\n",
           (unsigned int)aimtor_get_slave_address(SERVO_AXIS_RIGHT),
           right.online, right.enabled, right.parameter_verified,
           app_cli_mode_name(right.control_mode), right.actual_speed_rpm,
            right.fault_code, right.consecutive_failures,
            right.feedback_failures[0], right.feedback_failures[1],
            right.feedback_failures[2], right.feedback_degraded);
    printf("soft_gate=%u independent_stop=%u rpm_limit=%d accel_ramp_ms=%u decel_ramp_ms=%u\r\n",
           APP_SERVO_SOFT_ENABLE_CONFIRMED, APP_SERVO_MAINTENANCE_STOP_CONFIRMED,
            APP_OPERATION_MAX_MOTOR_RPM, APP_SERVO_SPEED_ACCELERATION_RAMP_MS,
            APP_SERVO_SPEED_DECELERATION_RAMP_MS);
    printf("ramp owner=%u software_accel_mm_s2=%u software_decel_mm_s2=%u now_ms=%lu\r\n",
           (unsigned int)app_control_arbiter_owner(),
           (unsigned int)(APP_DESIGN_ACCELERATION_MPS2 * 1000.0 + 0.5),
           (unsigned int)((double)APP_DESIGN_DECELERATION_MPS2 * 1000.0 + 0.5),
           (unsigned long)HAL_GetTick());
    printf("L ramp request=%u/%u read=%u/%u masks=%u/%u tick=%lu/%lu command=%d@%lu actual=%d\r\n",
           left.ramp_requested_ms[0], left.ramp_requested_ms[1],
           left.ramp_readback_ms[0], left.ramp_readback_ms[1], left.ramp_read_mask, left.ramp_match_mask,
           (unsigned long)left.ramp_readback_tick[0], (unsigned long)left.ramp_readback_tick[1],
           left.speed_write_rpm, (unsigned long)left.speed_write_tick, left.actual_speed_rpm);
    printf("R ramp request=%u/%u read=%u/%u masks=%u/%u tick=%lu/%lu command=%d@%lu actual=%d\r\n",
           right.ramp_requested_ms[0], right.ramp_requested_ms[1],
           right.ramp_readback_ms[0], right.ramp_readback_ms[1], right.ramp_read_mask, right.ramp_match_mask,
           (unsigned long)right.ramp_readback_tick[0], (unsigned long)right.ramp_readback_tick[1],
           right.speed_write_rpm, (unsigned long)right.speed_write_tick, right.actual_speed_rpm);
    printf("L busy=%u result=%u zero=%u/%u latch=%u off_verify=%u pos=%ld\r\n",
           left.busy, (unsigned int)left.last_result, left.zero_attempted,
           left.zero_verified, left.stop_latched, left.disable_verified,
           (long)left.actual_position);
    printf("R busy=%u result=%u zero=%u/%u latch=%u off_verify=%u pos=%ld\r\n",
           right.busy, (unsigned int)right.last_result, right.zero_attempted,
           right.zero_verified, right.stop_latched, right.disable_verified,
           (long)right.actual_position);
    printf("L diag speed_age=");
    app_cli_print_age(left.speed_age_ms);
    printf(" fault_age=");
    app_cli_print_age(left.fault_age_ms);
    printf(" probe=%u ok=%lu fail=%lu last=%lu@0x%04X\r\n",
           left.probe_pending, (unsigned long)left.poll_successes,
           (unsigned long)left.poll_failures,
           (unsigned long)left.last_poll_result,
           (unsigned int)left.last_poll_address);
    printf("R diag speed_age=");
    app_cli_print_age(right.speed_age_ms);
    printf(" fault_age=");
    app_cli_print_age(right.fault_age_ms);
    printf(" probe=%u ok=%lu fail=%lu last=%lu@0x%04X\r\n",
           right.probe_pending, (unsigned long)right.poll_successes,
           (unsigned long)right.poll_failures,
           (unsigned long)right.last_poll_result,
           (unsigned int)right.last_poll_address);
    printf("reset=0x%08lX wdg_reset=%u saved=0x%08lX detail=%lu dead=%u age=%lu/%lu/%lu lcd=%lu/%lu stage=%u spi=%lu rebuild=%lu\r\n",
           (unsigned long)app_health_get_reset_reason(),
           (unsigned int)bsp_system_was_iwdg_reset(),
           (unsigned long)bsp_system_take_saved_fault(),
           (unsigned long)bsp_system_take_saved_fault_detail(),
           (unsigned int)app_health_get_failed_heartbeat(),
           (unsigned long)app_health_get_heartbeat_age(APP_HEARTBEAT_MOTION),
           (unsigned long)app_health_get_heartbeat_age(APP_HEARTBEAT_SERVO_COMM),
           (unsigned long)app_health_get_heartbeat_age(APP_HEARTBEAT_HEALTH),
           (unsigned long)app_lcd_get_last_refresh_ticks(),
           (unsigned long)app_lcd_get_max_refresh_ticks(),
           (unsigned int)app_lcd_get_init_stage(),
           (unsigned long)app_lcd_get_spi_errors(),
           (unsigned long)app_lcd_get_rebuild_count());
    printf("wdg_test_k6_enabled=%u requested=%u active=%u\r\n",
           (unsigned int)APP_WDG_TEST_K6_SHORT_ENABLED,
           (unsigned int)app_health_watchdog_test_requested(),
           (unsigned int)app_health_watchdog_test_active());
    app_cli_print_position_summary();
}

// 输出USART2和Modbus通信统计，便于现场排查丢帧和超时。
static void app_cli_print_comm(void)
{
    rs485_stats_t rs485;
    modbus_stats_t modbus;

    rs485_get_stats(&rs485);
    modbus_get_stats(&modbus);
    printf("sync_debug_skipped=%lu\r\n", (unsigned long)s_sync_debug_skipped);
    printf("position_summary_skipped=%lu\r\n", (unsigned long)s_position_summary_skipped);
    printf("uart=%lu,8N1 rx=%lu tx=%lu overflow=%lu uart_err=%lu restart=%lu tx_to=%lu\r\n",
           (unsigned long)huart2.Init.BaudRate,
           (unsigned long)rs485.rx_bytes, (unsigned long)rs485.tx_bytes,
           (unsigned long)rs485.rx_overflows, (unsigned long)rs485.uart_errors,
           (unsigned long)rs485.rx_restarts,
           (unsigned long)rs485.tx_timeouts);
    printf("idle=%lu dma_ht=%lu dma_tc=%lu mb_to=%lu crc=%lu frame=%lu retry=%lu\r\n",
           (unsigned long)rs485.idle_interrupts,
           (unsigned long)rs485.dma_half_events,
           (unsigned long)rs485.dma_full_events,
           (unsigned long)modbus.timeouts, (unsigned long)modbus.crc_errors,
           (unsigned long)modbus.frame_errors, (unsigned long)modbus.retries);
    printf("irq=%lu dma_start_fail=%lu abort_fail=%lu dma_pos=%u/%u ndtr=%u run=%u recover=%u\r\n",
           (unsigned long)rs485.irq_entries,
           (unsigned long)rs485.dma_start_failures,
           (unsigned long)rs485.rx_abort_failures,
           (unsigned int)rs485.dma_position,
           (unsigned int)rs485.dma_last_position,
           (unsigned int)rs485.dma_remaining,
           (unsigned int)rs485.dma_running,
           (unsigned int)rs485.recovery_pending);
    printf("err_flags=0x%08lX hal_err=0x%08lX sr=0x%08lX cr1=0x%08lX cr3=0x%08lX\r\n",
           (unsigned long)rs485.uart_error_flags,
           (unsigned long)rs485.hal_error_flags,
           (unsigned long)rs485.uart_sr,
           (unsigned long)rs485.uart_cr1,
           (unsigned long)rs485.uart_cr3);
    printf("dma_cr=0x%08lX rx_state=0x%08lX pa3_mode=%u pa3_af=%u\r\n",
           (unsigned long)rs485.dma_control,
           (unsigned long)rs485.rx_state,
           (unsigned int)rs485.pa3_mode, (unsigned int)rs485.pa3_af);
    {
        debug_uart_stats_t debug_uart;
        debug_uart_get_stats(&debug_uart);
        printf("usart1_tx_dma queued=%lu sent=%lu dropped=%lu start=%lu "
               "complete=%lu error=%lu start_fail=%lu busy=%u used=%u free=%u peak=%u\r\n",
               (unsigned long)debug_uart.queued_bytes,
               (unsigned long)debug_uart.sent_bytes,
               (unsigned long)debug_uart.dropped_bytes,
               (unsigned long)debug_uart.dma_start_count,
               (unsigned long)debug_uart.dma_complete_count,
               (unsigned long)debug_uart.dma_error_count,
               (unsigned long)debug_uart.dma_start_failures,
               (unsigned int)debug_uart.tx_busy,
               (unsigned int)debug_uart.buffer_used,
               (unsigned int)debug_uart.buffer_free,
               (unsigned int)debug_uart.buffer_high_water);
    }
}

static void app_cli_print_help(void)
{
    printf("lcd status | ramp status\r\n");
    printf("safety status | safety acknowledge\r\n");
    printf("servo debug on <left|right> | status | keepalive <session_id> | off\r\n");
    printf("position status | position trip reset\r\n");
    printf("can status | can stat | can upper status\r\n");
    printf("can control status\r\n");
    printf("remote servo status | servo poll status\r\n");
    printf("status | comm stat | motion stop | servo emergency | servo clear\r\n");
    printf("servo probe | servo verify|zero|save|stop|enable|disable <left|right>\r\n");
    printf("servo left status | servo right status\r\n");
    printf("servo left mode <pulse|speed|position>\r\n");
    printf("servo left enable | disable | stop\r\n");
    printf("servo left speed <-%d..%d motor rpm>\r\n",
           APP_OPERATION_MAX_MOTOR_RPM, APP_OPERATION_MAX_MOTOR_RPM);
    printf("servo left position <pulses> <1..%d rpm> <ramp_ms>\r\n",
           APP_OPERATION_MAX_MOTOR_RPM);
    printf("servo left pulse <forward|reverse> <%u..%lu Hz> <pulses>\r\n",
           (unsigned int)APP_PULSE_MIN_HZ,
           (unsigned long)APP_DEBUG_MAX_PULSE_HZ);
    printf("servo current status | mode speed | enable | disable | speed <rpm> | stop\r\n");
    printf("servo current pulse <forward|reverse> <%u..%lu Hz> <pulses>\r\n",
           (unsigned int)APP_PULSE_MIN_HZ,
           (unsigned long)APP_DEBUG_MAX_PULSE_HZ);
    printf("current control_axis=%s\r\n",
           app_config_servo_control_axis_name());
}

static void app_cli_print_axis_debug_status(servo_axis_t axis)
{
    servo_debug_status_t status;

    if (!app_config_servo_axis_selected((uint8_t)axis))
    {
        printf("servo %s status=%u\r\n",
               axis == SERVO_AXIS_RIGHT ? "right" : "left",
               (unsigned int)SERVO_NOT_CONFIGURED);
        return;
    }
    if (!servo_debug_get_status(&status))
    {
        printf("servo %s status unavailable\r\n",
               axis == SERVO_AXIS_RIGHT ? "right" : "left");
        return;
    }
    if (axis != servo_debug_get_axis())
    {
        servo_snapshot_t feedback;
        (void)aimtor_get_snapshot(axis, &feedback);
        printf("axis=%s owner=none online=%u enabled=%u rpm=%d "
               "position=%ld fault=0x%04X\r\n",
               axis == SERVO_AXIS_RIGHT ? "right" : "left",
               feedback.online, feedback.enabled, feedback.actual_speed_rpm,
               (long)feedback.actual_position, feedback.fault_code);
        return;
    }
    printf("axis=%s mode=%s state=%s online=%u verified=%u enabled=%u "
           "rpm=%d position=%ld target=%ld busy=%u result=%u\r\n",
           axis == SERVO_AXIS_RIGHT ? "right" : "left",
           servo_debug_mode_name(status.mode),
           servo_debug_state_name(status.state),
           status.servo.online, status.servo.parameter_verified,
           status.servo.enabled, status.servo.actual_speed_rpm,
           (long)status.servo.actual_position, (long)status.target,
           status.servo.busy, (unsigned int)status.result);
    printf("pulse=%lu/%lu out=%u reached=%u stop_confirmed=%u "
           "timeout=%u\r\n",
           (unsigned long)status.pulse_count,
           (unsigned long)status.target_pulses,
           status.pulse_output_active,
           status.pulse_target_reached,
           status.pulse_stop_confirmed,
           status.pulse_completion_timeout);
    printf("pulse_pos valid=%u stable=%u start=%ld now=%ld delta=%ld "
           "target=%ld error=%ld\r\n",
           status.pulse_position_valid,
           status.pulse_position_stable,
           (long)status.pulse_position_start,
           (long)status.pulse_position_now,
           (long)status.pulse_position_delta,
           (long)status.pulse_position_target,
           (long)status.pulse_position_error);
}

static void app_cli_print_current_status(void)
{
    servo_snapshot_t left;
    servo_snapshot_t right;
    servo_debug_status_t pulse;
    uint8_t axis;

    (void)aimtor_get_snapshot(SERVO_AXIS_LEFT, &left);
    (void)aimtor_get_snapshot(SERVO_AXIS_RIGHT, &right);
    printf("control_axis=%s control_mask=0x%02X\r\n",
           app_config_servo_control_axis_name(),
           (unsigned int)app_config_servo_control_axis_mask());
    printf("L selected=%u online=%u enable=%u mode=%s rpm=%d fault=0x%04X\r\n",
           app_config_servo_axis_selected(SERVO_AXIS_LEFT),
           left.online, left.enabled, app_cli_mode_name(left.control_mode),
           left.actual_speed_rpm, left.fault_code);
    printf("R selected=%u online=%u enable=%u mode=%s rpm=%d fault=0x%04X\r\n",
           app_config_servo_axis_selected(SERVO_AXIS_RIGHT),
           right.online, right.enabled, app_cli_mode_name(right.control_mode),
           right.actual_speed_rpm, right.fault_code);
    if (servo_debug_get_status(&pulse) &&
        (pulse.mode == SERVO_DEBUG_MODE_PULSE))
    {
        printf("pulse state=%s stop_confirmed=%u timeout=%u "
               "stop=position_and_feedback\r\n",
               servo_debug_state_name(pulse.state),
               pulse.pulse_stop_confirmed, pulse.pulse_completion_timeout);
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!app_config_servo_axis_controllable(axis))
            {
                continue;
            }
            printf("%c pulse=%lu/%lu out_hz=%lu reached=%u cmd_rpm=%d "
                   "actual_rpm=%d enable=%u speed_age=%lu\r\n",
                   axis == SERVO_AXIS_LEFT ? 'L' : 'R',
                   (unsigned long)servo_pulse_get_pulse_count((servo_axis_t)axis),
                   (unsigned long)servo_pulse_get_target((servo_axis_t)axis),
                   (unsigned long)servo_pulse_get_frequency((servo_axis_t)axis),
                   (unsigned int)servo_pulse_target_reached((servo_axis_t)axis),
                   (int)(motion_get_command_velocity(axis) * 60.0f *
                         APP_GEAR_REDUCTION_RATIO / APP_WHEEL_CIRCUMFERENCE_M),
                   axis == SERVO_AXIS_LEFT ? left.actual_speed_rpm :
                                            right.actual_speed_rpm,
                   axis == SERVO_AXIS_LEFT ? left.enabled : right.enabled,
                   (unsigned long)(axis == SERVO_AXIS_LEFT ?
                       left.speed_age_ms : right.speed_age_ms));
        }
    }
}

// CLI命令统一进入已有门禁，不允许绕过状态机直接驱动硬件。
static void app_cli_print_remote_status(void)
{
    remote_servo_status_t status;
    nrf_remote_snapshot_t radio;
    NRF24L01_Stats wireless;
    nrf_telemetry_stats_t telemetry;
    const char *comm_state;

    app_cli_print_control_summary();
    app_cli_print_hold_status();
    (void)app_remote_servo_get_status(&status);
    printf("remote hold_stopping=%u\r\n", (unsigned int)status.hold_stopping);
    printf("controlled_stop state=%s reason=%s decel=%.3f generation=%lu "
           "elapsed=%lu timeout=%lu L_cmd=%d R_cmd=%d zero_mask=%u\r\n",
           app_remote_servo_controlled_stop_state_name(status.controlled_stop_state),
           app_remote_servo_stop_reason_name(status.controlled_stop_reason),
           (double)status.stop_deceleration_mps2,
           (unsigned long)status.stop_generation, (unsigned long)status.stop_elapsed_ms,
           (unsigned long)status.stop_timeout_ms, status.stop_batch_command[0],
           status.stop_batch_command[1], status.stop_zero_mask);
    printf("controlled_stop first_valid=%u first_ms=%lu zero_valid=%u zero_ms=%lu "
           "complete_valid=%u complete_ms=%lu\r\n",
           status.stop_first_command_valid, (unsigned long)status.stop_first_command_ms,
           status.stop_zero_time_valid, (unsigned long)status.stop_zero_ms,
           status.stop_complete_time_valid, (unsigned long)status.stop_complete_ms);
    printf("k5_hold state=%u valid=%u mask=0x%02X confirmed=0x%02X "
           "enabled=0x%02X zero=0x%02X disturbed=%u failure=%u authorized=%u\r\n",
           (unsigned int)status.k1_hold.state, status.k1_hold.valid,
           status.k1_hold.axis_mask, status.k1_hold.confirmed_mask,
           status.k1_hold.enabled_mask, status.k1_hold.zero_mask,
           status.k1_hold.disturbed, status.k1_hold.failure, status.authorized);
    (void)app_nrf_remote_get_snapshot(&radio);
    comm_state = status.comm_state == REMOTE_COMM_OK ? "ok" :
                 status.comm_state == REMOTE_COMM_DEGRADED ?
                 "degraded" : "failed";
    printf("remote state=%s authorized=%u latch=%u link=%u LV=%u "
           "target=%d command=%d actual=%d age=%lu arm=%lu "
           "estop=%lu loss_stop=%lu result=%u\r\n",
           app_remote_servo_state_name(status.state), status.authorized,
           status.estop_latched, status.link_valid, status.lv,
           status.target_rpm, status.commanded_rpm, status.actual_rpm,
           (unsigned long)status.last_frame_age_ms,
           (unsigned long)status.arm_count, (unsigned long)status.estop_count,
           (unsigned long)status.link_loss_estop_count,
           (unsigned int)status.last_result);
    printf("control_axis=%s mask=0x%02X "
           "L(cmd=%d act=%d fault=0x%04X age=%lu result=%u) "
           "R(cmd=%d act=%d fault=0x%04X age=%lu result=%u)\r\n",
           app_config_servo_control_axis_name(),
           (unsigned int)status.control_axis_mask,
           status.left_commanded_rpm, status.left_actual_rpm,
           status.left_fault, (unsigned long)status.left_speed_age_ms,
           (unsigned int)status.left_result,
           status.right_commanded_rpm, status.right_actual_rpm,
           status.right_fault, (unsigned long)status.right_speed_age_ms,
           (unsigned int)status.right_result);
    if (status.control_axis_mask == APP_SERVO_CONTROL_AXIS_BOTH)
    {
        printf("batch active=%u success_mask=0x%02X age=%lu response_gap=%lu stop_axis_mask=0x%02X\r\n",
               status.batch_active, status.batch_success_mask,
               (unsigned long)status.batch_age_ms,
               (unsigned long)status.batch_response_gap_ms,
               status.stop_axis_mask);
        printf("L verify=%u enabled=%u busy=%u latch=%u "
               "R verify=%u enabled=%u busy=%u latch=%u\r\n",
               status.left_verified, status.left_enabled,
               status.left_busy, status.left_latched,
               status.right_verified, status.right_enabled,
               status.right_busy, status.right_latched);
        printf("sync_target=%d sync_policy=%s target_diff=0 "
               "command_diff=%d actual_diff=%ld\r\n",
               status.target_rpm,
               APP_REMOTE_SERVO_SYNC_ENABLED ? "p_feedback" : "target_only",
               status.left_commanded_rpm - status.right_commanded_rpm,
               (long)status.left_actual_rpm - (long)status.right_actual_rpm);
        printf("sync_enabled=%u sync_active=%u sync_state=%s base_rpm=%d "
               "raw_error=%ld effective_error=%ld\r\n",
               status.sync.enabled, status.sync.active,
               app_remote_sync_state_name(status.sync.state), status.commanded_rpm,
               (long)status.sync.raw_error, (long)status.sync.effective_error);
        printf("requested_correction=%.3f applied_correction=%.3f effective_limit=%.3f "
               "batch_correction=%d\r\n",
               (double)status.sync.requested_correction,
               (double)status.sync.applied_correction,
               (double)status.sync.effective_limit, status.batch_correction);
        printf("sample_sequence_L/R=%lu/%lu sample_age_L/R=%lu/%lu "
               "sample_skew_ms=%lu pair_valid=%u pair_reject_reason=%s sync_dt_ms=%lu\r\n",
               (unsigned long)status.sync.sequence[0], (unsigned long)status.sync.sequence[1],
               (unsigned long)status.sync.age_ms[0], (unsigned long)status.sync.age_ms[1],
               (unsigned long)status.sync.skew_ms, status.sync.pair_valid,
               app_remote_sync_reject_name(status.sync.reject_reason),
               (unsigned long)status.sync.dt_ms);
        printf("pair_attempts=%lu pair_valid_count=%lu pair_reject_count=%lu "
               "pair_valid_ratio=%.1f%% pair_reject_repeat_count=%lu "
               "pair_reject_skew_count=%lu pair_reject_age_count=%lu "
               "pair_max_skew_ms=%lu pair_last_skew_ms=%lu pair_pending_L/R=%u/%u\r\n",
               (unsigned long)status.sync.pair_attempts,
               (unsigned long)status.sync.pair_valid_count,
               (unsigned long)status.sync.pair_reject_count,
               status.sync.pair_attempts ?
                   100.0 * status.sync.pair_valid_count / status.sync.pair_attempts : 0.0,
               (unsigned long)status.sync.pair_reject_repeat_count,
               (unsigned long)status.sync.pair_reject_skew_count,
               (unsigned long)status.sync.pair_reject_age_count,
               (unsigned long)status.sync.pair_max_skew_ms,
               (unsigned long)status.sync.pair_last_skew_ms,
               status.sync.pending_count[0], status.sync.pending_count[1]);
        printf("batch_id=%lu generation=%lu required_mask=0x%02X success_mask=0x%02X "
               "response_gap_valid=%u over_limit_duration_ms=%lu over_limit_alarm=%u "
               "over_limit_trip_enabled=%u\r\n",
               (unsigned long)status.batch_id, (unsigned long)status.authorization_generation,
               status.batch_required_mask, status.batch_success_mask,
               status.batch_response_gap_valid,
               (unsigned long)status.sync.over_limit_duration_ms,
               status.sync.over_limit_duration_ms >= APP_REMOTE_SERVO_SYNC_ERROR_MS,
               APP_REMOTE_SERVO_SYNC_TRIP_ENABLED);
    }
    printf("comm_state=%s comm_fail=%u speed_submit_fail=%u "
           "last_comm_ok_age=%lu\r\n",
           comm_state, status.comm_fail_count,
           status.speed_submit_fail_count,
           (unsigned long)status.last_comm_ok_age_ms);
    printf("stop_reason=%s link_age=%lu speed_age=%lu fault_age=%lu "
           "poll=%lu@0x%04X fault=0x%04X transaction=%u\r\n",
           app_remote_servo_stop_reason_name(status.stop_reason),
           (unsigned long)status.stop_link_age_ms,
           (unsigned long)status.stop_speed_age_ms,
           (unsigned long)status.stop_fault_age_ms,
           (unsigned long)status.stop_poll_result,
           status.stop_poll_address, status.stop_fault_code,
           (unsigned int)status.stop_transaction_result);
    printf("radio init=%u link=%u age=%lu max_gap=%lu service=%lu "
           "service_overrun=%lu task_age=%lu\r\n",
           radio.initialized, radio.link_up, (unsigned long)radio.age_ms,
           (unsigned long)radio.max_frame_gap_ms,
           (unsigned long)radio.service_count,
           (unsigned long)radio.service_overrun_count,
           (unsigned long)radio.task_age_ms);
    printf("radio valid=%lu drop=%lu crc=%lu format=%lu loss=%lu "
           "spi_err=%lu fifo_err=%lu recover=%lu\r\n",
           (unsigned long)radio.valid_frames,
           (unsigned long)radio.dropped_frames,
           (unsigned long)radio.crc_errors,
           (unsigned long)radio.format_errors,
           (unsigned long)radio.link_loss_count,
           (unsigned long)radio.nrf_spi_errors,
           (unsigned long)radio.nrf_fifo_errors,
           (unsigned long)radio.nrf_recoveries);
    printf("nrf_task_heartbeat_age=%lu\r\n",
           (unsigned long)(((uint64_t)app_health_get_heartbeat_age(
               APP_HEARTBEAT_NRF) * 1000ULL) / configTICK_RATE_HZ));
    NRF24L01_GetStats(&wireless);
    nrf_status_telemetry_get_stats(&telemetry);
    printf("ack enabled=%u supported=%u generated=%lu queued=%lu loaded=%lu "
           "replace=%lu flush=%lu fail=%lu session=%u seq=%u type=0x%02X\r\n",
           telemetry.enabled, wireless.ack_supported,
           (unsigned long)telemetry.generated,
           (unsigned long)wireless.ack_queued,
           (unsigned long)wireless.ack_loaded,
           (unsigned long)wireless.ack_replaced,
           (unsigned long)wireless.ack_flushed,
           (unsigned long)(wireless.ack_failures + telemetry.queue_failures),
           telemetry.session, telemetry.last_sequence, telemetry.last_type);
    printf("ack_age pending=%u/%lums fifo=%u/%lums clear=%lu reason=%u "
           "alarm_priority=%lu\r\n",
           wireless.ack_pending, (unsigned long)wireless.ack_pending_age_ms,
           wireless.ack_in_fifo, (unsigned long)wireless.ack_fifo_age_ms,
           (unsigned long)wireless.ack_clear_count,
           (unsigned int)wireless.ack_clear_reason,
           (unsigned long)telemetry.alarm_event_priority_count);
    printf("build=%s t=%lu telemetry_age", APP_NRF_BUILD_ID, (unsigned long)HAL_GetTick());
    for (unsigned int type_index = 0U; type_index < 10U; type_index++)
        printf(" %02X:%lu/%lu", 0x81U + type_index,
               (unsigned long)telemetry.max_age_ms[type_index],
               (unsigned long)telemetry.overdue_count[type_index]);
    printf(" urgent=%lu limited=%lu\r\n",
           (unsigned long)telemetry.fast_urgent_count,
           (unsigned long)telemetry.fast_burst_limited);
    printf("nrf_dma busy=%u started=%lu completed=%lu timeout=%lu "
           "rx_drop=%lu width_err=%lu\r\n", wireless.dma_busy,
           (unsigned long)wireless.dma_started,
           (unsigned long)wireless.dma_completed,
           (unsigned long)wireless.dma_timeouts,
           (unsigned long)wireless.rx_dropped,
           (unsigned long)wireless.invalid_widths);
    printf("nrf_recovery config_ready=%u radio_ready=%u generation=%lu "
           "failure=%lu feature=0x%02X dynpd=0x%02X verify_fail=%lu "
           "ack_timeout=%lu ack_max_wait=%lu check=%lu mismatch=%lu\r\n",
           wireless.initialized, wireless.radio_ready,
           (unsigned long)wireless.recovery_generation,
           (unsigned long)wireless.last_failure, wireless.feature, wireless.dynpd,
           (unsigned long)wireless.verify_failures,
           (unsigned long)wireless.ack_load_timeouts,
           (unsigned long)wireless.ack_max_wait_ms,
           (unsigned long)wireless.config_check_count,
           (unsigned long)wireless.config_check_mismatches);
    printf("nrf_failure valid=%u t=%lu state=%u cmd=0x%02X len=%u active=%u done=%u error=%u elapsed=%lu\r\n",
           wireless.failure_snapshot.valid, (unsigned long)wireless.failure_snapshot.tick_ms,
           wireless.failure_snapshot.state, wireless.failure_snapshot.command,
           wireless.failure_snapshot.length, wireless.failure_snapshot.active,
           wireless.failure_snapshot.done, wireless.failure_snapshot.error,
           (unsigned long)wireless.failure_snapshot.elapsed_ms);
    printf("nrf_failure_spi cr1=0x%08lX cr2=0x%08lX sr=0x%08lX\r\n",
           (unsigned long)wireless.failure_snapshot.spi_cr1,
           (unsigned long)wireless.failure_snapshot.spi_cr2,
           (unsigned long)wireless.failure_snapshot.spi_sr);
    printf("nrf_failure_dma rx_left=%lu tx_left=%lu rx_cr=0x%08lX tx_cr=0x%08lX\r\n",
           (unsigned long)wireless.failure_snapshot.rx_remaining,
           (unsigned long)wireless.failure_snapshot.tx_remaining,
           (unsigned long)wireless.failure_snapshot.rx_cr,
           (unsigned long)wireless.failure_snapshot.tx_cr);
    printf("nrf_failure_flags rx=0x%08lX tx=0x%08lX rx_err=0x%08lX tx_err=0x%08lX\r\n",
           (unsigned long)wireless.failure_snapshot.rx_flags,
           (unsigned long)wireless.failure_snapshot.tx_flags,
           (unsigned long)wireless.failure_snapshot.rx_error,
           (unsigned long)wireless.failure_snapshot.tx_error);
    app_cli_print_position_summary();
}

#if (APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED == 1U)
static bool app_cli_sync_debug_is_eligible(
    const remote_servo_status_t *status)
{
    if ((status == NULL) ||
        (status->authorized == 0U) ||
        ((status->state != REMOTE_SERVO_ARMED) &&
         (status->state != REMOTE_SERVO_RUNNING)) ||
        (status->control_axis_mask != APP_SERVO_CONTROL_AXIS_BOTH))
    {
        return false;
    }
    return true;
}
#endif

static void app_cli_sync_debug_service(void)
{
#if (APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED == 1U)
    remote_servo_status_t status;
    TickType_t now;
    TickType_t period_ticks;
    int32_t actual_diff;
    position_monitor_status_t position;
    debug_uart_stats_t tx;
    int length;
    static const char header[] =
        "P_SYNC_DEBUG\tTARGET_RPM\tBASE_CMD_RPM\t"
        "L_CMD_RPM\tL_ACT_RPM\tR_CMD_RPM\tR_ACT_RPM\t"
        "ACTUAL_DIFF\tSYNC_ENABLED\tSYNC_ACTIVE\tSYNC_STATE\t"
        "POS_GEN\tPOS_VALID\tL_POS\tR_POS\tL_REL\tR_REL\tDIFF_MM\t"
        "TRIP_M\tPOS_SKEW_MS\tL_POS_SEQ\tR_POS_SEQ\r\n";

    if (!app_remote_servo_get_status(&status) ||
        !app_cli_sync_debug_is_eligible(&status))
    {
        s_sync_debug_session_active = 0U;
        s_sync_debug_header_printed = 0U;
        return;
    }

    now = xTaskGetTickCount();
    period_ticks = pdMS_TO_TICKS(APP_REMOTE_SERVO_SYNC_DEBUG_PERIOD_MS);
    if (period_ticks == 0U)
    {
        period_ticks = 1U;
    }

    if (s_sync_debug_session_active == 0U)
    {
        s_sync_debug_session_active = 1U;
        s_sync_debug_header_printed = 0U;
        s_sync_debug_last_tick = now - period_ticks;
    }

    if (s_sync_debug_header_printed == 0U)
    {
        debug_uart_get_stats(&tx);
        if (tx.buffer_free < sizeof(header) - 1U)
        {
            s_sync_debug_skipped++;
            s_sync_debug_last_tick = now;
            return;
        }
        printf("%s", header);
        s_sync_debug_header_printed = 1U;
    }

    if ((TickType_t)(now - s_sync_debug_last_tick) < period_ticks)
    {
        return;
    }
    s_sync_debug_last_tick = now;
    actual_diff = (int32_t)status.left_actual_rpm -
                  (int32_t)status.right_actual_rpm;
    (void)app_position_monitor_get_status(&position);
    length = snprintf(s_sync_debug_line, sizeof(s_sync_debug_line),
           "P_SYNC_DATA\t%+6d\t%+6d\t%+6d\t%+6d\t%+6d\t%+6d\t"
           "%+6ld\t%u\t%u\t%s\t%lu\t%u\t%ld\t%ld\t%lld\t%lld\t"
           "%.3f\t%.3f\t%lu\t%lu\t%lu\r\n",
           (int)status.target_rpm,
           (int)status.commanded_rpm,
           (int)status.left_commanded_rpm,
           (int)status.left_actual_rpm,
           (int)status.right_commanded_rpm,
           (int)status.right_actual_rpm,
           (long)actual_diff,
           (unsigned int)status.sync.enabled,
           (unsigned int)status.sync.active,
           app_remote_sync_state_name(status.sync.state),
           (unsigned long)position.baseline_generation, position.pair_valid,
           (long)position.axis[0].raw_position, (long)position.axis[1].raw_position,
           (long long)position.relative_counts[0], (long long)position.relative_counts[1],
           position.difference_mm, position.vehicle_trip_path_mm / 1000.0,
           (unsigned long)position.sample_skew_ms,
           (unsigned long)position.axis[0].sequence, (unsigned long)position.axis[1].sequence);
    debug_uart_get_stats(&tx);
    /* CLI是唯一发送生产者；DMA只释放空间，不会抢占预留容量。 */
    if (length < 0 || (size_t)length >= sizeof(s_sync_debug_line) ||
        (unsigned int)length > tx.buffer_free)
    {
        s_sync_debug_skipped++;
        return;
    }
    printf("%s", s_sync_debug_line);
#endif
}

static void app_cli_execute(char *line)
{
    char axis_text[8];
    char value_text[12];
    servo_axis_t axis;
    servo_control_mode_t mode;
    servo_result_t servo_result;
    long distance;
    int rpm;
    unsigned int speed;
    unsigned int acceleration;
    unsigned int frequency;
    unsigned long pulses;
    float left_mps;
    float right_mps;
    float acceleration_mps2;

    if (strcmp(line, "ramp status") == 0) { app_cli_print_ramp(); return; }
    if (strcmp(line, "lcd status") == 0) { app_lcd_print_status(); return; }
    if (app_can_cli_command(line)) return;
    if (app_hmi_cli_command(line)) return;
    if (app_cli_session_command(line)) return;
    if (strcmp(line, "position status") == 0)
    {
        app_cli_print_position();
        return;
    }
    if (strcmp(line, "position trip reset") == 0)
    {
        printf("position trip reset=%s\r\n",
               app_position_monitor_request_trip_reset() ? "queued" : "rejected");
        return;
    }
    if (strcmp(line, "servo poll status") == 0) {
        static const char *const names[SERVO_POLL_CLASSES] = {
            "speed", "fault", "enable", "position", "state", "torque", "load", "current", "voltage" };
        servo_poll_status_t poll;
        unsigned int axis, item;
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
            for (item = 0U; item < SERVO_POLL_CLASSES; item++) {
                (void)aimtor_get_poll_status((servo_axis_t)axis, (uint8_t)item, &poll);
                printf("%c poll=%s submit=%lu ok=%lu fail=%lu result=%lu wait_max=%lu age=%lu\r\n",
                    axis ? 'R' : 'L', names[item], (unsigned long)poll.submitted,
                    (unsigned long)poll.succeeded, (unsigned long)poll.failed,
                    (unsigned long)poll.last_result, (unsigned long)poll.max_wait_ms, (unsigned long)poll.age_ms);
            }
        return;
    }
    if (strcmp(line, "remote servo status") == 0)
    {
        app_cli_print_remote_status();
        return;
    }
    if (strcmp(line, "servo current status") == 0)
    {
        app_cli_print_current_status();
        return;
    }
    if (aimtor_startup_hold_active() && !aimtor_motion_allowed() &&
        (strstr(line, " speed ") || strstr(line, " pulse ") ||
         strstr(line, " position ") || strstr(line, " mode ") ||
         strstr(line, "enable") || strstr(line, "motion run")))
    {
        printf("motion unauthorized or startup hold not ready; command rejected\r\n");
        return;
    }
    if (app_remote_servo_is_active())
    {
        if ((strcmp(line, "servo emergency") == 0) ||
            (strcmp(line, "motion stop") == 0) ||
            (strcmp(line, "servo left stop") == 0) ||
            (strcmp(line, "servo stop left") == 0) ||
            (strcmp(line, "servo left disable") == 0) ||
            (strcmp(line, "servo disable left") == 0) ||
            (strcmp(line, "servo right stop") == 0) ||
            (strcmp(line, "servo stop right") == 0) ||
            (strcmp(line, "servo right disable") == 0) ||
            (strcmp(line, "servo disable right") == 0) ||
            (strcmp(line, "servo current stop") == 0) ||
            (strcmp(line, "servo current disable") == 0))
        {
            /* 停止命令始终有效，但不能留下遥控器自动恢复的授权。 */
            app_remote_servo_request_estop();
            (void)servo_debug_emergency_stop();
            printf("remote emergency requested; rearm with K5 at neutral\r\n");
            return;
        }
        if ((strcmp(line, "status") != 0) &&
            (strcmp(line, "comm stat") != 0) &&
            (strcmp(line, "help") != 0) &&
            (strcmp(line, "servo current status") != 0) &&
            (strcmp(line, "servo current stop") != 0) &&
            (strcmp(line, "servo left status") != 0) &&
            (strcmp(line, "servo right status") != 0) &&
            (strcmp(line, "servo probe") != 0) &&
            (strcmp(line, "servo read") != 0))
        {
            printf("remote owns configured control axis; command rejected\r\n");
            return;
        }
    }
    if (strcmp(line, "status") == 0)
    {
        app_cli_print_status();
    }
    else if (strcmp(line, "comm stat") == 0)
    {
        app_cli_print_comm();
    }
    else if (strcmp(line, "help") == 0)
    {
        app_cli_print_help();
    }
    else if ((strcmp(line, "servo left status") == 0) ||
             (strcmp(line, "servo right status") == 0))
    {
        app_cli_print_axis_debug_status(
            strstr(line, "right") != NULL ? SERVO_AXIS_RIGHT :
                                             SERVO_AXIS_LEFT);
    }
    else if (sscanf(line, "servo current mode %11s", value_text) == 1)
    {
        servo_result = app_cli_current_mode(value_text);
        printf("servo current mode=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "servo current enable") == 0)
    {
        servo_result = app_cli_current_enable();
        printf("servo current enable=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "servo current disable") == 0)
    {
        servo_result = app_cli_current_disable();
        printf("servo current disable=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "servo current stop") == 0)
    {
        servo_result = app_cli_current_stop();
        printf("servo current stop=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo current speed %d", &rpm) == 1)
    {
        servo_result = app_cli_current_speed(rpm);
        printf("servo current speed=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo current pulse %11s %u %lu", value_text,
                    &frequency, &pulses) == 3)
    {
        if (strcmp(value_text, "forward") == 0)
        {
            servo_result = servo_debug_run_pulse(
                true, frequency, (uint32_t)pulses);
        }
        else if (strcmp(value_text, "reverse") == 0)
        {
            servo_result = servo_debug_run_pulse(
                false, frequency, (uint32_t)pulses);
        }
        else
        {
            servo_result = SERVO_INVALID_ARGUMENT;
        }
        printf("servo current pulse=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo left mode %11s", value_text) == 1)
    {
        servo_debug_mode_t debug_mode;

        if (!app_cli_axis_is_controllable(SERVO_AXIS_LEFT))
        {
            printf("servo left mode=%u\r\n",
                   (unsigned int)SERVO_NOT_CONFIGURED);
            return;
        }
        if (strcmp(value_text, "pulse") == 0)
        {
            debug_mode = SERVO_DEBUG_MODE_PULSE;
        }
        else if (strcmp(value_text, "speed") == 0)
        {
            debug_mode = SERVO_DEBUG_MODE_RS485_SPEED;
        }
        else if (strcmp(value_text, "position") == 0)
        {
            debug_mode = SERVO_DEBUG_MODE_RS485_POSITION;
        }
        else
        {
            debug_mode = SERVO_DEBUG_MODE_NONE;
        }
        servo_result = servo_debug_prepare(debug_mode);
        printf("servo left mode=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo right mode %11s", value_text) == 1)
    {
        servo_debug_mode_t debug_mode;

        if (!app_cli_axis_is_controllable(SERVO_AXIS_RIGHT) ||
            (app_config_servo_control_axis_count() != 1U))
        {
            printf("servo right mode=%u\r\n",
                   (unsigned int)SERVO_NOT_CONFIGURED);
            return;
        }
        if (strcmp(value_text, "pulse") == 0)
        {
            debug_mode = SERVO_DEBUG_MODE_PULSE;
        }
        else if (strcmp(value_text, "speed") == 0)
        {
            debug_mode = SERVO_DEBUG_MODE_RS485_SPEED;
        }
        else if (strcmp(value_text, "position") == 0)
        {
            debug_mode = SERVO_DEBUG_MODE_RS485_POSITION;
        }
        else
        {
            debug_mode = SERVO_DEBUG_MODE_NONE;
        }
        if (debug_mode == SERVO_DEBUG_MODE_PULSE)
        {
            servo_result = servo_debug_prepare(debug_mode);
        }
        else
        {
            servo_result = servo_debug_prepare(debug_mode);
        }
        printf("servo right mode=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "servo left enable") == 0)
    {
        servo_result = app_cli_axis_is_controllable(SERVO_AXIS_LEFT) ?
                       servo_debug_enable() : SERVO_NOT_CONFIGURED;
        printf("servo left enable=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "servo right enable") == 0)
    {
        servo_result =
            (app_cli_axis_is_controllable(SERVO_AXIS_RIGHT) &&
             (app_config_servo_control_axis_count() == 1U)) ?
            servo_debug_enable() : SERVO_NOT_CONFIGURED;
        printf("servo right enable=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "servo left disable") == 0)
    {
        servo_result = app_cli_axis_is_controllable(SERVO_AXIS_LEFT) ?
                       servo_debug_disable() : SERVO_NOT_CONFIGURED;
        printf("servo left disable=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "servo right disable") == 0)
    {
        servo_result =
            (app_cli_axis_is_controllable(SERVO_AXIS_RIGHT) &&
             (app_config_servo_control_axis_count() == 1U)) ?
            servo_debug_disable() : SERVO_NOT_CONFIGURED;
        printf("servo right disable=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "servo left stop") == 0)
    {
        servo_result = app_cli_axis_is_controllable(SERVO_AXIS_LEFT) ?
                       servo_debug_stop() : SERVO_NOT_CONFIGURED;
        printf("servo left stop=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "servo right stop") == 0)
    {
        servo_result =
            (app_cli_axis_is_controllable(SERVO_AXIS_RIGHT) &&
             (app_config_servo_control_axis_count() == 1U)) ?
            servo_debug_stop() : SERVO_NOT_CONFIGURED;
        printf("servo right stop=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo left speed %d", &rpm) == 1)
    {
        if (!app_cli_axis_is_controllable(SERVO_AXIS_LEFT))
        {
            servo_result = SERVO_NOT_CONFIGURED;
        }
        else if ((rpm < -32768) || (rpm > 32767))
        {
            servo_result = SERVO_INVALID_ARGUMENT;
        }
        else
        {
            servo_result = servo_debug_set_speed((int16_t)rpm);
        }
        printf("servo left speed=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo right speed %d", &rpm) == 1)
    {
        if (!app_cli_axis_is_controllable(SERVO_AXIS_RIGHT) ||
            (app_config_servo_control_axis_count() != 1U))
        {
            servo_result = SERVO_NOT_CONFIGURED;
        }
        else if ((rpm < -32768) || (rpm > 32767))
        {
            servo_result = SERVO_INVALID_ARGUMENT;
        }
        else
        {
            servo_result = servo_debug_set_speed((int16_t)rpm);
        }
        printf("servo right speed=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo left position %ld %u %u", &distance,
                    &speed, &acceleration) == 3)
    {
        if (!app_cli_axis_is_controllable(SERVO_AXIS_LEFT))
        {
            servo_result = SERVO_NOT_CONFIGURED;
        }
        else if ((speed > 65535U) || (acceleration > 65535U))
        {
            servo_result = SERVO_INVALID_ARGUMENT;
        }
        else
        {
            servo_result = servo_debug_run_position((int32_t)distance,
                                                    (uint16_t)speed,
                                                    (uint16_t)acceleration);
        }
        printf("servo left position=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo right position %ld %u %u", &distance,
                    &speed, &acceleration) == 3)
    {
        if (!app_cli_axis_is_controllable(SERVO_AXIS_RIGHT) ||
            (app_config_servo_control_axis_count() != 1U))
        {
            servo_result = SERVO_NOT_CONFIGURED;
        }
        else if ((speed > 65535U) || (acceleration > 65535U))
        {
            servo_result = SERVO_INVALID_ARGUMENT;
        }
        else
        {
            servo_result = servo_debug_run_position((int32_t)distance,
                                                    (uint16_t)speed,
                                                    (uint16_t)acceleration);
        }
        printf("servo right position=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo left pulse %11s %u %lu", value_text,
                    &frequency, &pulses) == 3)
    {
        if (!app_cli_axis_is_controllable(SERVO_AXIS_LEFT))
        {
            servo_result = SERVO_NOT_CONFIGURED;
        }
        else if (strcmp(value_text, "forward") == 0)
        {
            servo_result = servo_debug_run_pulse(true, frequency,
                                                 (uint32_t)pulses);
        }
        else if (strcmp(value_text, "reverse") == 0)
        {
            servo_result = servo_debug_run_pulse(false, frequency,
                                                 (uint32_t)pulses);
        }
        else
        {
            servo_result = SERVO_INVALID_ARGUMENT;
        }
        printf("servo left pulse=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo right pulse %11s %u %lu", value_text,
                    &frequency, &pulses) == 3)
    {
        if (!app_cli_axis_is_controllable(SERVO_AXIS_RIGHT) ||
            (app_config_servo_control_axis_count() != 1U))
        {
            servo_result = SERVO_NOT_CONFIGURED;
        }
        else if (strcmp(value_text, "forward") == 0)
        {
            servo_result = servo_debug_run_pulse(
                true, frequency, (uint32_t)pulses);
        }
        else if (strcmp(value_text, "reverse") == 0)
        {
            servo_result = servo_debug_run_pulse(
                false, frequency, (uint32_t)pulses);
        }
        else
        {
            servo_result = SERVO_INVALID_ARGUMENT;
        }
        printf("servo right pulse=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "motion stop") == 0)
    {
        servo_result = servo_debug_stop();
        printf("motion stop=%u\r\n", (unsigned int)servo_result);
    }
    else if (strcmp(line, "servo emergency") == 0)
    {
        servo_result = servo_debug_emergency_stop();
        printf("local output inhibited; remote disable best effort\r\n");
    }
    else if (strcmp(line, "safety status") == 0)
    {
        app_safety_status_t safety;
        app_health_get_safety_status(&safety);
        printf("safety phase=%u task=%u step=%u generation=%lu failure=%lu params=%u zero=%u rpm=%d/%d fw=%u/%u H02_06=%u/%u H0C_30=%u/%u elapsed=%lu progress=%lu\r\n",
            safety.phase, safety.failed_task, safety.step, (unsigned long)safety.generation,
            (unsigned long)safety.failure, safety.parameters_mask, safety.zero_mask,
            safety.actual_rpm[0], safety.actual_rpm[1], safety.firmware[0], safety.firmware[1],
            safety.no2_mode[0], safety.no2_mode[1], safety.comm_timeout[0], safety.comm_timeout[1],
            (unsigned long)safety.elapsed_ms, (unsigned long)safety.last_progress_ms);
    }
    else if (strcmp(line, "safety acknowledge") == 0)
    {
        printf("safety acknowledge queued=%u (check safety status; manual authorization required)\r\n",
            app_health_safety_acknowledge() ? 1U : 0U);
    }
    else if (strcmp(line, "servo recover") == 0)
    {
        printf("servo recover request=%u\r\n", (unsigned int)aimtor_request_recovery(2U));
    }
    else if (strcmp(line, "servo clear") == 0)
    {
        printf("servo clear=%u\r\n", (unsigned int)aimtor_clear_stop_latch());
    }
    else if (sscanf(line, "motion velocity %f %f", &left_mps, &right_mps) == 2)
    {
        (void)left_mps;
        (void)right_mps;
        printf("motion velocity unsupported; use servo left speed <rpm>\r\n");
    }
    else if (sscanf(line, "motion distance %f %f %f", &left_mps, &right_mps,
                    &acceleration_mps2) == 3)
    {
        (void)left_mps;
        (void)right_mps;
        (void)acceleration_mps2;
        printf("motion distance unsupported; use servo left pulse or position\r\n");
    }
    else if (strcmp(line, "servo reset") == 0)
    {
        servo_result_t reset_result = SERVO_NOT_CONFIGURED;
        servo_axis_t reset_axis;

        /*
         * 只对当前配置选中的轴处理复位请求；未选中的轴不能被访问，
         * 也不能把未选中轴的结果混入当前诊断输出。
         */
        for (reset_axis = SERVO_AXIS_LEFT;
             reset_axis < SERVO_AXIS_COUNT;
             reset_axis++)
        {
            if (app_config_servo_axis_selected((uint8_t)reset_axis))
            {
                reset_result = aimtor_reset_fault(reset_axis);
                break;
            }
        }
        printf("servo reset=%u axis=%s\r\n",
               (unsigned int)reset_result,
               app_config_servo_control_axis_name());
    }
    else if ((strcmp(line, "servo probe") == 0) ||
             (strcmp(line, "servo read") == 0))
    {
        servo_result_t left_probe;
        servo_result_t right_probe;

        left_probe = SERVO_NOT_CONFIGURED;
        right_probe = SERVO_NOT_CONFIGURED;
        if (app_config_servo_axis_selected(SERVO_AXIS_LEFT))
        {
            left_probe = aimtor_request_status(SERVO_AXIS_LEFT);
        }
        if (app_config_servo_axis_selected(SERVO_AXIS_RIGHT))
        {
            right_probe = aimtor_request_status(SERVO_AXIS_RIGHT);
        }
        printf("servo probe L=%u R=%u\r\n",
               (unsigned int)left_probe, (unsigned int)right_probe);
    }
    else if (sscanf(line, "servo verify %7s", axis_text) == 1)
    {
        servo_result = app_cli_parse_axis(axis_text, &axis) ?
                       (app_config_servo_axis_selected((uint8_t)axis) ?
                        aimtor_request_parameter_verify(axis) :
                        SERVO_NOT_CONFIGURED) :
                       SERVO_INVALID_ARGUMENT;
        printf("servo verify=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo zero %7s", axis_text) == 1)
    {
        servo_result = app_cli_parse_axis(axis_text, &axis) ?
                       (app_cli_axis_is_controllable(axis) ?
                        aimtor_zero_once(axis) : SERVO_NOT_CONFIGURED) :
                       SERVO_INVALID_ARGUMENT;
        printf("servo zero=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo stop %7s", axis_text) == 1)
    {
        servo_result = app_cli_parse_axis(axis_text, &axis) ?
                       (app_cli_axis_is_controllable(axis) ?
                         ((app_config_servo_control_axis_count() == 1U) ?
                          servo_debug_stop() : app_cli_current_stop()) :
                        SERVO_NOT_CONFIGURED) :
                       SERVO_INVALID_ARGUMENT;
        printf("servo stop=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo maintenance %11s", value_text) == 1)
    {
        if (strcmp(value_text, "on") == 0)
        {
            servo_result = aimtor_set_maintenance_enabled(true);
        }
        else if (strcmp(value_text, "off") == 0)
        {
            servo_result = aimtor_set_maintenance_enabled(false);
        }
        else
        {
            servo_result = SERVO_INVALID_ARGUMENT;
        }
        printf("servo maintenance=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo enable %7s", axis_text) == 1)
    {
        servo_result = app_cli_parse_axis(axis_text, &axis) ?
                       (app_cli_axis_is_controllable(axis) ?
                        aimtor_enable(axis) : SERVO_NOT_CONFIGURED) :
                       SERVO_INVALID_ARGUMENT;
        printf("servo enable=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo disable %7s", axis_text) == 1)
    {
        servo_result = app_cli_parse_axis(axis_text, &axis) ?
                       (app_cli_axis_is_controllable(axis) ?
                        aimtor_disable(axis) : SERVO_NOT_CONFIGURED) :
                       SERVO_INVALID_ARGUMENT;
        printf("servo disable=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo mode %7s %11s", axis_text, value_text) == 2)
    {
        mode = SERVO_CONTROL_NONE;
        if (strcmp(value_text, "pulse") == 0)
        {
            mode = SERVO_CONTROL_PULSE_POSITION;
        }
        else if (strcmp(value_text, "speed") == 0)
        {
            mode = SERVO_CONTROL_RS485_VELOCITY;
        }
        else if (strcmp(value_text, "segment") == 0)
        {
            mode = SERVO_CONTROL_RS485_SEGMENT_POSITION;
        }
        servo_result = app_cli_parse_axis(axis_text, &axis) ?
                       (!app_cli_axis_is_controllable(axis) ?
                        SERVO_NOT_CONFIGURED :
                         ((app_config_servo_control_axis_count() == 1U) ?
                          (mode == SERVO_CONTROL_PULSE_POSITION ?
                           servo_debug_prepare(SERVO_DEBUG_MODE_PULSE) :
                           mode == SERVO_CONTROL_RS485_VELOCITY ?
                          servo_debug_prepare(SERVO_DEBUG_MODE_RS485_SPEED) :
                          mode == SERVO_CONTROL_RS485_SEGMENT_POSITION ?
                          servo_debug_prepare(
                              SERVO_DEBUG_MODE_RS485_POSITION) :
                          SERVO_INVALID_ARGUMENT) :
                          SERVO_NOT_CONFIGURED)) :
                       SERVO_INVALID_ARGUMENT;
        printf("servo mode=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo speed %7s %d", axis_text, &rpm) == 2)
    {
        if ((rpm < -32768) || (rpm > 32767))
        {
            servo_result = SERVO_INVALID_ARGUMENT;
        }
        else
        {
            servo_result = app_cli_parse_axis(axis_text, &axis) ?
                           (app_cli_axis_is_controllable(axis) ?
                             ((app_config_servo_control_axis_count() == 1U) ?
                              servo_debug_set_speed((int16_t)rpm) :
                              SERVO_NOT_CONFIGURED) :
                            SERVO_NOT_CONFIGURED) :
                           SERVO_INVALID_ARGUMENT;
        }
        printf("servo speed=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo segment %7s %ld %u %u", axis_text,
                    &distance, &speed, &acceleration) == 4)
    {
        if ((speed > 65535U) || (acceleration > 65535U))
        {
            servo_result = SERVO_INVALID_ARGUMENT;
        }
        else
        {
            servo_result = app_cli_parse_axis(axis_text, &axis) ?
                           ((app_config_servo_control_axis_count() == 1U) ?
                            servo_debug_run_position((int32_t)distance,
                                                     (uint16_t)speed,
                                                     (uint16_t)acceleration) :
                            SERVO_NOT_CONFIGURED) :
                           SERVO_INVALID_ARGUMENT;
        }
        printf("servo segment=%u\r\n", (unsigned int)servo_result);
    }
    else if (sscanf(line, "servo save %7s", axis_text) == 1)
    {
        servo_result = app_cli_parse_axis(axis_text, &axis) ?
                       (app_config_servo_axis_selected((uint8_t)axis) ?
                        aimtor_save_parameters(axis) :
                        SERVO_NOT_CONFIGURED) :
                       SERVO_INVALID_ARGUMENT;
        printf("servo save=%u\r\n", (unsigned int)servo_result);
    }
    else
    {
        printf("unknown command, use help\r\n");
    }
}
static void app_cli_radio_diagnostic_service(void)
{
#if APP_NRF_DIAGNOSTIC_ENABLED
    static NRF24L01_Stats wireless;
    static nrf_telemetry_stats_t telemetry;
    static nrf_remote_snapshot_t remote;
    static NRF24L01_Event event;
    static char output[128];
    static uint32_t last_ms, dropped;
    static uint8_t started, phase, event_pending;
    uint32_t now = HAL_GetTick();
    int length = 0;
    uint8_t index;

    if (phase == 0U) {
        if (started && now - last_ms < 1000U) return;
        started = 1U;
        last_ms = now;
        NRF24L01_GetStats(&wireless);
        nrf_status_telemetry_get_stats(&telemetry);
        (void)app_nrf_remote_get_snapshot(&remote);
    }
    if (phase == 0U)
        length = snprintf(output, sizeof(output), "RF_BUILD t=%lu id=%s drop=%lu\r\n",
            (unsigned long)last_ms, APP_NRF_BUILD_ID, (unsigned long)dropped);
    else if (phase == 1U)
        length = snprintf(output, sizeof(output), "RF_RX t=%lu valid=%lu age=%lu gap=%lu generation=%lu\r\n",
            (unsigned long)last_ms, (unsigned long)remote.valid_frames,
            (unsigned long)remote.age_ms, (unsigned long)remote.max_frame_gap_ms,
            (unsigned long)wireless.recovery_generation);
    else if (phase == 2U)
        length = snprintf(output, sizeof(output), "RF_ACK t=%lu gen=%lu queued=%lu loaded=%lu pending=%u/%lu fifo=%u/%lu\r\n",
            (unsigned long)last_ms, (unsigned long)telemetry.generated,
            (unsigned long)wireless.ack_queued, (unsigned long)wireless.ack_loaded,
            wireless.ack_pending, (unsigned long)wireless.ack_pending_age_ms,
            wireless.ack_in_fifo, (unsigned long)wireless.ack_fifo_age_ms);
    else if (phase == 3U)
        length = snprintf(output, sizeof(output), "RF_ERR t=%lu dma=%lu spi=%lu mismatch=%lu fail=%lu flush=%lu reason=%u wait=%lu\r\n",
            (unsigned long)last_ms, (unsigned long)wireless.dma_timeouts,
            (unsigned long)wireless.spi_errors, (unsigned long)wireless.config_check_mismatches,
            (unsigned long)wireless.last_failure, (unsigned long)wireless.ack_flushed,
            wireless.ack_clear_reason, (unsigned long)wireless.ack_max_wait_ms);
    else if (phase == 4U)
        length = snprintf(output, sizeof(output), "RF_STATE t=%lu session=%u seq=%u type=%02X code=%u priority=%lu limited=%lu urgent=%lu\r\n",
            (unsigned long)last_ms, telemetry.session, telemetry.last_sequence, telemetry.last_type,
            telemetry.alarm_code, (unsigned long)telemetry.alarm_event_priority_count,
            (unsigned long)telemetry.fast_burst_limited, (unsigned long)telemetry.fast_urgent_count);
    else if (phase < 15U) {
        index = (uint8_t)(phase - 5U);
        length = snprintf(output, sizeof(output), "RF_TYPE t=%lu type=%02X gen=%lu queued=%lu gap=%lu overdue=%lu\r\n",
            (unsigned long)last_ms, 0x81U + index,
            (unsigned long)telemetry.generated_by_type[index],
            (unsigned long)telemetry.queued_by_type[index],
            (unsigned long)telemetry.max_age_ms[index],
            (unsigned long)telemetry.overdue_count[index]);
    } else if (phase == 15U) {
        length = snprintf(output, sizeof(output), "RF_CLEAR t=%lu maxrt=%lu timeout=%lu full=%lu recovery=%lu edrop=%lu\r\n",
            (unsigned long)last_ms,
            (unsigned long)wireless.ack_clear_by_reason[NRF24L01_ACK_CLEAR_MAX_RT],
            (unsigned long)wireless.ack_clear_by_reason[NRF24L01_ACK_CLEAR_TIMEOUT],
            (unsigned long)wireless.ack_clear_by_reason[NRF24L01_ACK_CLEAR_TX_FULL],
            (unsigned long)wireless.ack_clear_by_reason[NRF24L01_ACK_CLEAR_RECOVERY],
            (unsigned long)wireless.event_overwrites);
    } else {
        if (!event_pending) event_pending = (uint8_t)NRF24L01_PopEvent(&event);
        if (event_pending)
            length = snprintf(output, sizeof(output), "RF_EVT t=%lu kind=%u type=%02X session=%u seq=%u\r\n",
                (unsigned long)event.timestamp_ms, event.kind, event.type, event.session, event.sequence);
    }
    if (length > 0 && (size_t)length < sizeof(output) &&
        debug_uart_try_write((const uint8_t *)output, (uint16_t)length, 512U)) {
        if (phase == 16U) event_pending = 0U;
    } else if (length != 0) dropped++;
    phase = (uint8_t)((phase + 1U) % 17U);
#endif
}

void app_cli_process(void)
{
    char line[CLI_LINE_SIZE];
    BaseType_t command_received;

    command_received = ((s_line_queue != NULL) &&
                        (xQueueReceive(s_line_queue, line, 0U) == pdTRUE));
    if (command_received)
    {
        app_cli_execute(line);
    }
    debug_uart_service();
    if (!command_received)
    {
        app_cli_sync_debug_service();
        app_can_cli_service();
        app_cli_radio_diagnostic_service();
    }
    debug_uart_service();
    vTaskDelay(pdMS_TO_TICKS(1U));
}
