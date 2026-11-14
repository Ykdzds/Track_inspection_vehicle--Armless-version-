#include "app_health.h"

#include "FreeRTOS.h"
#include "task.h"
#include "bsp_system.h"
#include "led.h"
#include "servo_pulse.h"
#include "app_config.h"
#include "aimtor_servo.h"
#include "aimtor_registers.h"
#include "modbus_rtu.h"
#include "app_link_diag.h"
#include <string.h>

#define APP_HEARTBEAT_TIMEOUT_MS  (500U)

static volatile TickType_t s_heartbeat_ticks[APP_HEARTBEAT_COUNT];
static volatile uint32_t s_diagnostic_faults;
static uint32_t s_reset_reason;
static volatile app_heartbeat_t s_failed_heartbeat;
static volatile uint8_t s_watchdog_test_requested;
static volatile uint8_t s_watchdog_test_active;
static volatile uint16_t s_watchdog_decision;

/* phase:0正常核验,1任务停车,2静默兜底,3停稳等待复位,4静默启动,
 * 5看门狗恢复,6故障只读保持。所有状态仅由20ms安全入口推进。 */
static app_safety_status_t s_safety;
static TickType_t s_safety_start, s_progress[2], s_zero_tick[2], s_next_guard;
static TickType_t s_request_tick, s_stop_tick[2], s_decrease_tick[2];
static TickType_t s_diag_speed_tick[2], s_diag_fault_tick[2], s_guard_tick[2];
static uint8_t s_steps[2], s_zero_samples[2], s_baseline_valid[2];
static uint16_t s_fault[2], s_enable[2];
static int16_t s_start_rpm[2], s_last_decrease_rpm[2];
static uint32_t s_request_id, s_pending_id, s_stop_limit[2];
static uint8_t s_pending_axis, s_pending_step;
static volatile uint8_t s_guard_stop;
static uint8_t s_guard_was_permitted;
static volatile uint8_t s_ack_requested;
static volatile uint32_t s_requested_fallback;
static app_health_nrf_status_t s_nrf_status;
static uint8_t s_nrf_seen, s_nrf_failed, s_nrf_stabilizing;
#define APP_HEALTH_NRF_STABLE_MS (1000U)
#define APP_HEALTH_NRF_FAIL_MS   (1000U)
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
static uint32_t s_link_safety_flags,s_link_safety_failure;
__attribute__((noinline)) static void safety_diag_observe(void)
{
    uint32_t flags=0x80000000UL|s_safety.phase|((uint32_t)s_safety.failed_task<<8U)|
        ((uint32_t)s_safety.boot_locked<<16U);
    if(flags==s_link_safety_flags && s_safety.failure==s_link_safety_failure)return;
    app_link_diag_record(APP_LINK_RS485,0x23U,s_link_safety_flags,flags,s_safety.failure,
        s_safety.generation,s_pending_id,((uint32_t)s_safety.parameters_mask<<24U)|
        ((uint32_t)s_safety.zero_mask<<16U)|((uint32_t)s_pending_axis<<8U)|s_pending_step);
    if((s_safety.phase!=0U && s_safety.phase!=6U) || s_safety.failure!=s_link_safety_failure)
        app_link_diag_freeze6(APP_LINK_RS485,0x23U,s_link_safety_flags,flags,s_safety.failure,
            s_safety.generation,s_pending_id,((uint32_t)s_safety.parameters_mask<<24U)|
            ((uint32_t)s_safety.zero_mask<<16U)|((uint32_t)s_pending_axis<<8U)|s_pending_step);
    s_link_safety_flags=flags;s_link_safety_failure=s_safety.failure;
}
#else
#define safety_diag_observe() ((void)0)
#endif
void app_health_request_driver_fallback(uint32_t reason)
{
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_HEALTH, 0xFFU,
        2U, reason);
    taskENTER_CRITICAL();
    s_requested_fallback = reason ? reason : 14U;
    modbus_safety_silence();
    bsp_system_safety_store(2U, s_requested_fallback);
    taskEXIT_CRITICAL();
    bsp_system_fault_set_context((uint32_t)s_failed_heartbeat, 2U);
    bsp_system_store_fault(APP_DIAG_DRIVER_GUARD, reason ? reason : 14U);
}
bool app_health_take_guard_stop(void)
{ uint8_t stop; taskENTER_CRITICAL(); stop = s_guard_stop; s_guard_stop = 0U; taskEXIT_CRITICAL(); return stop != 0U; }
static uint32_t safety_ms(TickType_t elapsed)
{ return (uint32_t)((uint64_t)elapsed * 1000U / configTICK_RATE_HZ); }
static uint32_t safety_abs(int16_t value)
{ return value < 0 ? (uint32_t)(-(int32_t)value) : (uint32_t)value; }
static uint8_t safety_selected(uint8_t axis)
{ return app_config_servo_axis_selected(axis); }
static uint8_t safety_mask(void)
{ return app_config_servo_control_axis_mask(); }
static uint32_t safety_detail(void)
{ return ((uint32_t)s_safety.failed_task << 16U) | s_safety.failure; }
static void safety_fallback(uint32_t reason)
{
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_HEALTH, 0xFFU,
        3U, reason);
    s_safety.failure = reason;
    s_safety.phase = 2U;
    s_safety.pending = 0U;
    s_safety_start = xTaskGetTickCount();
    modbus_set_motion_permitted(false);
    modbus_safety_silence();
    bsp_system_safety_store(2U, safety_detail());
    bsp_system_fault_set_context((uint32_t)s_safety.failed_task, s_safety.phase);
    bsp_system_store_fault(APP_DIAG_DRIVER_GUARD, safety_detail());
}
static void safety_begin(uint8_t recovery)
{
    uint8_t axis;
    TickType_t now = xTaskGetTickCount();
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_HEALTH, 0xFFU,
        4U, recovery);
    s_safety.phase = recovery ? 5U : 1U;
    s_safety.generation++;
    s_safety.pending = s_safety.zero_mask = 0U;
    s_safety_start = now;
    modbus_safety_takeover();
    for (axis = 0U; axis < 2U; axis++)
    {
        s_steps[axis] = s_zero_samples[axis] = s_baseline_valid[axis] = 0U;
        s_progress[axis] = s_decrease_tick[axis] = now;
        s_stop_limit[axis] = 0U;
    }
    bsp_system_safety_store(1U, safety_detail());
}
/* 0..6:读取速度、故障、使能、模式、选择、来源、原给定；7..9:斜坡写/读/零速写。 */
static uint16_t safety_address(uint8_t step)
{
    static const uint16_t address[] = {
        AIMTOR_REG_ACTUAL_SPEED, AIMTOR_REG_FAULT_CODE, AIMTOR_REG_SERVO_ENABLE,
        AIMTOR_REG_CONTROL_MODE, AIMTOR_REG_SPEED_SELECTION, AIMTOR_REG_SPEED_SOURCE,
        AIMTOR_REG_SPEED_COMMAND, AIMTOR_REG_DECELERATION_TIME,
        AIMTOR_REG_DECELERATION_TIME, AIMTOR_REG_SPEED_COMMAND
    };
    return step >= 10U ? address[step - 10U] : address[step];
}
static uint16_t safety_guard_address(uint8_t step)
{
    static const uint16_t address[] = {
        AIMTOR_REG_FIRMWARE_VERSION, AIMTOR_REG_NO2_STOP_MODE, AIMTOR_REG_COMM_TIMEOUT,
        AIMTOR_REG_ACTUAL_SPEED, AIMTOR_REG_FAULT_CODE, AIMTOR_REG_SERVO_ENABLE
    };
    return address[step];
}
static void safety_zero_sample(uint8_t axis, int16_t rpm, TickType_t now)
{
    s_safety.actual_rpm[axis] = rpm;
    s_diag_speed_tick[axis] = now;
    if (rpm) { s_zero_samples[axis] = 0U; s_safety.zero_mask &= (uint8_t)~(1U << axis); }
    else if (!s_zero_samples[axis]) { s_zero_samples[axis] = 1U; s_zero_tick[axis] = now; }
    else if (safety_ms(now - s_zero_tick[axis]) >= 20U)
    { s_zero_samples[axis] = 2U; s_safety.zero_mask |= (uint8_t)(1U << axis); }
}
static void safety_response(void)
{
    modbus_response_t response;
    uint8_t axis = s_pending_axis, step = s_pending_step;
    uint16_t value, expected_address;
    uint8_t write_step;
    TickType_t now = xTaskGetTickCount();
    if (!s_safety.pending || !modbus_safety_get_response(&response)) return;
    if (response.context.transaction_id != s_pending_id) {
        modbus_diag_consumed(&response,0U); return;
    }
    modbus_diag_consumed(&response,1U);
    s_safety.pending = 0U;
    expected_address = (s_safety.phase == 1U || s_safety.phase == 5U) ?
        safety_address(step) : safety_guard_address(step);
    if (response.status != MODBUS_OK || response.address != expected_address ||
        response.slave != (axis ? APP_SERVO_RIGHT_SLAVE_ADDRESS : APP_SERVO_LEFT_SLAVE_ADDRESS) ||
        (step != 7U && step != 9U && (response.operation != MODBUS_OPERATION_READ_HOLDING ||
         response.quantity != 1U || response.data_length != 3U || response.data[0] != 2U)))
    {
        if (s_safety.phase == 1U || s_safety.phase == 5U) safety_fallback(1U);
        else {
            s_safety.parameters_mask &= (uint8_t)~(1U << axis);
            modbus_set_motion_permitted(false);
        }
        return;
    }
    write_step = (uint8_t)((s_safety.phase == 1U || s_safety.phase == 5U) && (step == 7U || step == 9U));
    value = write_step ? response.value :
        (uint16_t)(((uint16_t)response.data[1] << 8U) | response.data[2]);
    if(!write_step && expected_address==AIMTOR_REG_ACTUAL_SPEED)
        modbus_diag_speed(&response,s_safety.actual_rpm[axis],0,0U,s_safety.phase);
    if (s_safety.phase == 1U || s_safety.phase == 5U)
    {
        uint32_t ramp = s_safety.phase == 5U ? APP_WDG_RECOVERY_DECEL_MS : APP_TASK_STOP_RAMP_MS;
        if (step == 0U)
        {
            s_start_rpm[axis] = (int16_t)value;
            s_safety.actual_rpm[axis] = (int16_t)value;
        }
        else if (step == 1U && value)
        {
            s_fault[axis] = value;
            /* 已进入驱动器故障停车，保持DB；不能继续发读写刷新另一轴超时。 */
            safety_fallback(2U); return;
        }
        else if ((step == 2U && value > 1U) || (step >= 3U && step <= 5U && value != 0U))
        { safety_fallback(3U); return; }
        else if (step == 2U) s_enable[axis] = value;
        else if (step == 6U)
        {
            uint32_t speed = safety_abs(s_start_rpm[axis]);
            if (safety_abs((int16_t)value) > speed) speed = safety_abs((int16_t)value);
            s_stop_limit[axis] = (uint32_t)((uint64_t)speed * ramp / 1000U) + 2000U;
        }
        else if ((step == 7U || step == 9U) &&
            (response.operation != MODBUS_OPERATION_WRITE_SINGLE || value != (step == 7U ? ramp : 0U)))
        { safety_fallback(4U); return; }
        else if (step == 8U && value != ramp) { safety_fallback(5U); return; }
        if (step == 9U)
        {
            s_stop_tick[axis] = now;
            s_decrease_tick[axis] = now;
            s_last_decrease_rpm[axis] = s_start_rpm[axis];
        }
        if (step == 11U && value) { safety_fallback(12U); return; }
        if (step == 12U && s_enable[axis] && value != 1U) { safety_fallback(13U); return; }
        if (step == 10U)
        {
            uint32_t rpm = safety_abs((int16_t)value);
            safety_zero_sample(axis, (int16_t)value, now);
            if (!s_baseline_valid[axis] || rpm < safety_abs(s_last_decrease_rpm[axis]))
            {
                s_baseline_valid[axis] = 1U;
                s_last_decrease_rpm[axis] = (int16_t)value;
                s_decrease_tick[axis] = now;
            }
            if (rpm && safety_ms(now - s_decrease_tick[axis]) >= 500U)
            { safety_fallback(6U); return; }
            {
                uint32_t elapsed = safety_ms(now - s_stop_tick[axis]);
                uint32_t expected_drop = elapsed > 500U ? (elapsed - 500U) * 1000U / ramp : 0U;
                uint32_t initial = safety_abs(s_start_rpm[axis]);
                uint32_t expected = initial > expected_drop ? initial - expected_drop : 0U;
                if (elapsed > 500U && rpm > expected + 20U)
                { safety_fallback(11U); return; }
            }
            if (rpm > safety_abs(s_start_rpm[axis]) + 20U)
            { safety_fallback(7U); return; }
        }
        s_progress[axis] = now;
        s_steps[axis] = step < 12U ? (uint8_t)(step + 1U) : 10U;
    }
    else
    {
        if (step == 0U) s_safety.firmware[axis] = value;
        if (step == 1U) s_safety.no2_mode[axis] = value;
        if (step == 2U)
        {
            s_safety.comm_timeout[axis] = value;
            s_guard_tick[axis] = now;
            if (s_safety.firmware[axis] >= APP_DRIVER_MIN_FIRMWARE &&
                s_safety.no2_mode[axis] == APP_DRIVER_NO2_STOP_MODE && value == APP_DRIVER_COMM_TIMEOUT_MS)
                s_safety.parameters_mask |= (uint8_t)(1U << axis);
            else s_safety.parameters_mask &= (uint8_t)~(1U << axis);
        }
        if (step == 3U) safety_zero_sample(axis, (int16_t)value, now);
        if (step == 4U) { s_fault[axis] = value; s_diag_fault_tick[axis] = now; }
        if (step == 5U) s_enable[axis] = value;
        s_steps[axis] = (uint8_t)((step + 1U) % 6U);
    }
    s_safety.last_progress_ms = safety_ms(now);
}
static void safety_submit(void)
{
    modbus_request_t request;
    uint8_t axis, step;
    TickType_t now = xTaskGetTickCount();
    if (s_safety.pending) return;
    axis = s_safety.axis;
    if (!safety_selected(axis)) axis = (uint8_t)(1U - axis);
    step = s_steps[axis];
    if (app_config_servo_readonly_test() && (s_safety.phase == 1U || s_safety.phase == 5U))
    { safety_fallback(10U); return; }
    memset(&request, 0, sizeof(request));
    request.slave = axis ? APP_SERVO_RIGHT_SLAVE_ADDRESS : APP_SERVO_LEFT_SLAVE_ADDRESS;
    request.context.axis = axis;
    request.context.transaction_id = ++s_request_id;
    request.function = 3U; request.quantity = 1U;
    if (s_safety.phase == 1U || s_safety.phase == 5U)
    {
        request.address = safety_address(step);
        if (step == 7U || step == 9U)
        {
            request.function = 6U;
            request.value = step == 9U ? 0U : (uint16_t)(s_safety.phase == 5U ?
                APP_WDG_RECOVERY_DECEL_MS : APP_TASK_STOP_RAMP_MS);
        }
    }
    else request.address = safety_guard_address(step);
    if (modbus_safety_submit(&request) == MODBUS_OK)
    {
        s_pending_id = request.context.transaction_id;
        s_pending_axis = axis; s_pending_step = step;
        s_request_tick = now; s_safety.pending = 1U;
        s_safety.axis = (uint8_t)(1U - axis); s_safety.step = step;
    }
}
static void safety_service(void)
{
    uint8_t axis;
    TickType_t now = xTaskGetTickCount();
    if (s_safety.phase == 4U)
    {
        if (safety_ms(now - s_safety_start) < APP_DRIVER_BOOT_SILENCE_MS) return;
        modbus_safety_diagnostics();
        s_safety.phase = 6U;
        memset(s_steps, 0, sizeof(s_steps));
    }
    if (s_safety.phase == 2U || s_safety.phase == 3U) return;
    safety_response();
    if (s_safety.phase == 2U) return;
    if (s_safety.pending && safety_ms(now - s_request_tick) >= 500U)
    {
        if (s_safety.phase == 1U || s_safety.phase == 5U) { safety_fallback(8U); return; }
        s_safety.pending = 0U;
        s_safety.parameters_mask = 0U;
        modbus_set_motion_permitted(false);
    }
    if (s_safety.phase == 1U || s_safety.phase == 5U)
    {
        s_safety.elapsed_ms = safety_ms(now - s_safety_start);
        for (axis = 0U; axis < 2U; axis++) if (safety_selected(axis))
        {
            if (safety_ms(now - s_progress[axis]) >= 500U ||
                (s_steps[axis] >= 10U && safety_ms(now - s_stop_tick[axis]) > s_stop_limit[axis]))
            { safety_fallback(9U); return; }
        }
        if ((s_safety.zero_mask & safety_mask()) == safety_mask())
        {
            uint8_t reboot = s_safety.phase == 1U;
            s_safety.phase = reboot ? 3U : 6U;
            bsp_system_safety_store(3U, safety_detail());
            if (!reboot) { modbus_safety_diagnostics(); memset(s_steps, 0, sizeof(s_steps)); }
            return;
        }
    }
    else if (s_safety.phase == 0U)
    {
        for (axis = 0U; axis < 2U; axis++)
            if (safety_selected(axis) && safety_ms(now - s_guard_tick[axis]) >= 500U)
                s_safety.parameters_mask &= (uint8_t)~(1U << axis);
        {
            uint8_t permitted = (s_safety.parameters_mask & safety_mask()) == safety_mask();
            if (s_guard_was_permitted && !permitted) s_guard_stop = 1U;
            s_guard_was_permitted = permitted;
            modbus_set_motion_permitted(permitted != 0U);
        }
        if ((s_safety.parameters_mask & safety_mask()) != safety_mask())
            s_diagnostic_faults |= APP_DIAG_DRIVER_GUARD;
        else s_diagnostic_faults &= ~APP_DIAG_DRIVER_GUARD;
    }
    if ((int32_t)(now - s_next_guard) >= 0)
    { safety_submit(); s_next_guard = now + pdMS_TO_TICKS(20U); }
}
void app_health_get_safety_status(app_safety_status_t *status)
{
    if (!status) return;
    taskENTER_CRITICAL(); *status = s_safety; taskEXIT_CRITICAL();
}
bool app_health_safety_acknowledge(void)
{
    if (s_safety.phase != 6U) return false;
    s_ack_requested = 1U;
    return true; /* 请求已接收；最终结果查询safety status。 */
}
static void safety_ack_service(void)
{
    uint8_t axis;
    TickType_t now = xTaskGetTickCount();
    if (!s_ack_requested) return;
    s_ack_requested = 0U;
    if (s_safety.phase != 6U || !app_health_critical_tasks_alive() ||
        (s_safety.parameters_mask & safety_mask()) != safety_mask()) return;
    for (axis = 0U; axis < 2U; axis++) if (safety_selected(axis) &&
        (s_fault[axis] || s_zero_samples[axis] < 2U || s_safety.actual_rpm[axis] ||
         safety_ms(now - s_diag_speed_tick[axis]) >= 500U ||
         safety_ms(now - s_diag_fault_tick[axis]) >= 500U)) return;
    bsp_system_safety_clear();
    s_safety.phase = s_safety.boot_locked = s_safety.pending = 0U;
    s_safety.parameters_mask = 0U;
    modbus_safety_release();
}

static uint32_t app_health_hash_string(const char *text)
{
    uint32_t hash = 2166136261UL;
    unsigned count = 0U;
    /* A corrupted TCB/name must not make the overflow hook scan arbitrary memory. */
    while (text != NULL && count++ < 128U)
    {
#if defined(__CC_ARM) || defined(__arm__) || defined(__thumb__)
        uintptr_t address = (uintptr_t)text;
        if (!((address >= 0x08000000UL && address < 0x08100000UL) ||
              (address >= 0x20000000UL && address < 0x20020000UL) ||
              (address >= 0x10000000UL && address < 0x10010000UL))) break;
#endif
        if (*text == '\0') break;
        hash ^= (uint8_t)*text++;
        hash *= 16777619UL;
    }
    return hash;
}
/* 初始化健康监控，并记录本次启动前的硬件复位原因。 */
void app_health_init(void)
{
    /* 遍历所有任务心跳记录 */
    uint8_t index;
    /* 保存当前FreeRTOS节拍值 */
    TickType_t now;

    /* 获取系统运行到当前时刻的节拍数 */
    now = xTaskGetTickCount();
    for (index = 0U; index < APP_HEARTBEAT_COUNT; index++)
    {
        /* 保存各关键任务最近一次更新心跳的时间 */
        s_heartbeat_ticks[index] = now;
    }
    /* 清除本次启动后的诊断故障 */
    s_diagnostic_faults = 0U;
    s_watchdog_test_requested = 0U;
    s_watchdog_test_active = 0U;
    s_watchdog_decision = 0xFFFFU;
    s_ack_requested = s_guard_stop = s_guard_was_permitted = 0U;
    s_requested_fallback = 0U;
    memset(&s_nrf_status, 0, sizeof(s_nrf_status));
    s_nrf_seen = s_nrf_failed = s_nrf_stabilizing = 0U;
    memset(&s_safety, 0, sizeof(s_safety));
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    s_link_safety_flags=s_link_safety_failure=0U;
#endif
    memset(s_steps, 0, sizeof(s_steps));
    memset(s_zero_samples, 0, sizeof(s_zero_samples));
    s_next_guard = s_safety_start = now;
    s_request_id = 0U;
    memset(s_diag_speed_tick, 0, sizeof(s_diag_speed_tick));
    memset(s_diag_fault_tick, 0, sizeof(s_diag_fault_tick));
    memset(s_guard_tick, 0, sizeof(s_guard_tick));
    memset(s_fault, 0, sizeof(s_fault));
    /* 读取上次复位原因，并通常清除保存值，避免重复报告 */
    s_reset_reason = bsp_system_take_reset_reason();
    /* 记录失活任务编号。初始化为 APP_HEARTBEAT_COUNT，表示当前没有任务失活 */
    s_failed_heartbeat = APP_HEARTBEAT_COUNT;
    s_safety.failed_task = APP_HEARTBEAT_COUNT;
}

void app_health_beat(app_heartbeat_t heartbeat)
{
    if (heartbeat < APP_HEARTBEAT_COUNT)
    {
        s_heartbeat_ticks[heartbeat] = xTaskGetTickCount();
    }
}

bool app_health_critical_tasks_alive(void)
{
    uint8_t index;
    TickType_t now;
    TickType_t timeout;

    now = xTaskGetTickCount();
    timeout = pdMS_TO_TICKS(APP_HEARTBEAT_TIMEOUT_MS);
    for (index = 0U; index < APP_HEARTBEAT_COUNT; index++)
    {
        if ((now - s_heartbeat_ticks[index]) > timeout)
        {
            s_failed_heartbeat = (app_heartbeat_t)index;
            return false;
        }
    }
    s_failed_heartbeat = APP_HEARTBEAT_COUNT;
    return true;
}

void app_health_nrf_update(const app_health_nrf_input_t *input, uint32_t now_ms)
{
    uint8_t healthy, changed;
    uint32_t checks;
    if (!input) return;
    taskENTER_CRITICAL();
    changed = (uint8_t)(s_nrf_seen &&
        ((uint32_t)(now_ms-s_nrf_status.updated_ms)>=100U ||
         input->spi_errors != s_nrf_status.spi_errors ||
         input->dma_timeouts != s_nrf_status.dma_timeouts ||
         input->verify_failures != s_nrf_status.verify_failures ||
         input->config_mismatches != s_nrf_status.config_mismatches ||
         input->recoveries != s_nrf_status.recoveries));
    healthy = (uint8_t)(input->initialized && input->ready && !changed);
    if (!healthy) {
        if (!s_nrf_failed) {
            s_nrf_failed = 1U;
            s_nrf_status.failure_since_ms = now_ms;
            ++s_nrf_status.failure_count;
        }
        s_nrf_stabilizing = 0U;
        s_nrf_status.stable_checks = 0U;
        s_nrf_status.recovered = 0U;
        if ((uint32_t)(now_ms - s_nrf_status.failure_since_ms) >= APP_HEALTH_NRF_FAIL_MS)
            s_nrf_status.current_fault = 1U;
    } else {
        s_nrf_failed = 0U;
        if (!s_nrf_stabilizing) {
            s_nrf_stabilizing = 1U;
            s_nrf_status.recovery_since_ms = now_ms;
            s_nrf_status.stable_checks = 0U;
        } else {
            checks = input->config_checks - s_nrf_status.config_checks;
            if (checks && checks < 0x80000000UL) {
                if (s_nrf_status.stable_checks < 2U)
                    s_nrf_status.stable_checks += checks > 2U ? 2U : checks;
            }
        }
        /* Flags alone cannot prove recovery. Require two real full register
         * checks after the latest failure, plus a continuous stable interval. */
        if ((uint32_t)(now_ms - s_nrf_status.recovery_since_ms) >= APP_HEALTH_NRF_STABLE_MS &&
            s_nrf_status.stable_checks >= 2U) {
            if (!s_nrf_status.recovered) ++s_nrf_status.recovery_count;
            s_nrf_status.current_fault = 0U;
            s_nrf_status.recovered = 1U;
        }
    }
    s_nrf_seen = s_nrf_status.known = 1U;
    s_nrf_status.initialized = input->initialized;
    s_nrf_status.ready = input->ready;
    if (input->failure) s_nrf_status.failure = input->failure;
    s_nrf_status.updated_ms = now_ms;
    s_nrf_status.config_checks = input->config_checks;
    s_nrf_status.spi_errors = input->spi_errors;
    s_nrf_status.dma_timeouts = input->dma_timeouts;
    s_nrf_status.verify_failures = input->verify_failures;
    s_nrf_status.config_mismatches = input->config_mismatches;
    s_nrf_status.recoveries = input->recoveries;
    taskEXIT_CRITICAL();
}
void app_health_get_nrf_status(app_health_nrf_status_t *status)
{
    if (!status) return;
    taskENTER_CRITICAL(); *status = s_nrf_status; taskEXIT_CRITICAL();
}
void app_health_set_diagnostic_fault(uint32_t fault_mask)
{
    taskENTER_CRITICAL();
    s_diagnostic_faults |= fault_mask;
    taskEXIT_CRITICAL();
}

void app_health_request_watchdog_test(void)
{
#if (APP_WDG_TEST_K6_SHORT_ENABLED == 1U)
    taskENTER_CRITICAL();
    if (s_watchdog_test_requested == 0U)
    {
        s_watchdog_test_requested = 1U;
        s_diagnostic_faults |= APP_DIAG_WDG_TEST_REQUESTED;
    }
    taskEXIT_CRITICAL();
#endif
}

bool app_health_watchdog_test_requested(void)
{
    return s_watchdog_test_requested != 0U;
}

bool app_health_watchdog_test_active(void)
{
    return s_watchdog_test_active != 0U;
}

void app_health_watchdog_service(void)
{
    static uint8_t boot_checked;
    uint8_t alive;
    bsp_system_fault_set_context((uint32_t)s_safety.failed_task, s_safety.phase);
    if (s_watchdog_decision == 0xFFFFU) boot_checked = 0U;
    if (!boot_checked)
    {
        uint32_t retained = bsp_system_safety_state();
        boot_checked = 1U;
        if (retained || (s_reset_reason & ((1UL << 29U) | (1UL << 30U))))
        {
            s_safety.boot_locked = 1U;
            if (retained == 1U || retained == 2U)
            {
                s_safety.phase = 4U;
                modbus_safety_silence();
            }
            else if (retained == 3U)
            { s_safety.phase = 6U; modbus_safety_diagnostics(); }
            else safety_begin(1U);
        }
    }
    if (s_requested_fallback && s_safety.phase != 2U)
        safety_fallback(s_requested_fallback);
    if (s_watchdog_test_requested)
    {
        s_watchdog_decision = 0U;
        if (!s_watchdog_test_active)
        { s_watchdog_test_active = 1U; bsp_system_store_fault(APP_DIAG_WDG_TEST_REQUESTED, 2U); }
        safety_diag_observe();
        return;
    }
    alive = app_health_critical_tasks_alive() ? 1U : 0U;
    if (!alive && s_safety.phase != 1U && s_safety.phase != 2U && s_safety.phase != 3U && s_safety.phase != 4U)
    {
        s_safety.failed_task = (uint8_t)s_failed_heartbeat;
        s_diagnostic_faults |= APP_DIAG_HEARTBEAT_TIMEOUT;
        bsp_system_fault_set_context((uint32_t)s_failed_heartbeat, 1U);
        bsp_system_store_fault(APP_DIAG_HEARTBEAT_TIMEOUT, s_failed_heartbeat);
        safety_begin(0U);
    }
    if (s_safety.phase == 1U || s_safety.phase == 2U || s_safety.phase == 3U) s_failed_heartbeat = (app_heartbeat_t)s_safety.failed_task;
    safety_service();
    safety_ack_service();
    safety_diag_observe();
    s_watchdog_decision = 0U;
    if (s_safety.phase != 2U && s_safety.phase != 3U)
    {
        s_watchdog_decision = 1U;
        bsp_system_watchdog_refresh();
    }
}

uint32_t app_health_get_diagnostic_faults(void)
{
    return s_diagnostic_faults;
}

uint16_t app_health_get_watchdog_decision(void)
{
    return s_watchdog_decision;
}

uint32_t app_health_get_reset_reason(void)
{
    return s_reset_reason;
}

app_heartbeat_t app_health_get_failed_heartbeat(void)
{
    return s_failed_heartbeat;
}

TickType_t app_health_get_heartbeat_age(app_heartbeat_t heartbeat)
{
    if (heartbeat >= APP_HEARTBEAT_COUNT)
    {
        return 0U;
    }
    return xTaskGetTickCount() - s_heartbeat_ticks[heartbeat];
}

/* 首次故障保存在独立UNINIT区，下次温复位可读，不能在这里打印。 */
void app_health_assert_failed(const char *file, uint32_t line)
{
    uint32_t file_hash;

    /* 此处不能依赖任务调度或总线，先切断本地脉冲输出。 */
    servo_pulse_stop_all();
    file_hash = app_health_hash_string(file);
    s_diagnostic_faults |= APP_DIAG_ASSERT_FAILED;
    /* 高16位保存文件哈希摘要，低16位保存断言行号。 */
    bsp_system_store_fault(APP_DIAG_ASSERT_FAILED,
                           ((file_hash & 0xFFFFUL) << 16U) |
                           (line & 0xFFFFUL));
    set_led(RGB_RED, LED_ON);
}

/* 栈溢出属于不可恢复故障，记录任务相关故障后交给IWDG复位。 */
void app_health_stack_overflow(const char *task_name)
{
    servo_pulse_stop_all();
    s_diagnostic_faults |= APP_DIAG_STACK_OVERFLOW;
    bsp_system_store_fault(APP_DIAG_STACK_OVERFLOW,
                           app_health_hash_string(task_name));
    set_led(RGB_RED, LED_ON);
}

#if defined(configCHECK_FOR_STACK_OVERFLOW) && (configCHECK_FOR_STACK_OVERFLOW > 0)
/* Strong definition replaces the CMSIS default assertion-only hook. */
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    app_health_stack_overflow(task_name);
    taskDISABLE_INTERRUPTS();
    for (;;) { /* IWDG owns the eventual reset; never feed it from a fault hook. */ }
}
#endif
