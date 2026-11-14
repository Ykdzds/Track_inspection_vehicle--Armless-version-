#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "queue.h"
#include "task.h"
#include "main.h"
#include "tim.h"
#include "app_config.h"
#include "aimtor_registers.h"
#include "aimtor_servo.h"
#include "motion_control.h"
#include "servo_debug_control.h"
#include "servo_pulse.h"
#include "modbus_rtu.h"
#include "bsp_rs485.h"
#include "stm32f4xx_it.h"
#include "app_health.h"
#ifndef TEST_COLD_CLEAR
static uint8_t watchdog_test_requested;
bool app_health_watchdog_test_requested(void) { return watchdog_test_requested != 0U; }
#endif
#ifdef TEST_CLI_SESSION_ONLY
#include "app_cli_servo_session.h"
#include "app_control_arbiter.h"
#include "app_remote_servo.h"
#ifdef TEST_HMI_PRODUCTION
#include "app_hmi_control.h"
uint8_t app_remote_servo_k5_verify_pending(void) { return 0U; }
uint32_t app_hmi_snapshot_boot_id(void) { return 0x12345678UL; }
uint32_t HAL_GetTick(void);
void app_hmi_snapshot_set_result(uint32_t session, uint32_t sequence, uint16_t command, uint16_t state,
    uint16_t error, uint32_t received, uint32_t completed, uint32_t generation,
    uint16_t owner, uint16_t remaining, uint16_t feedback)
{
    (void)session; (void)sequence; (void)command; (void)state; (void)error; (void)received;
    (void)completed; (void)generation; (void)owner; (void)remaining; (void)feedback;
}
#endif
static remote_servo_status_t cli_remote;
bool app_remote_servo_get_status(remote_servo_status_t *out)
{
    *out = cli_remote;
    return true;
}
#endif

typedef struct {
    unsigned capacity, size, head, tail, count;
    unsigned char data[];
} test_queue_t;
static TickType_t ticks;
#ifdef TEST_POLL_WIRE_DELAY_MS
static TickType_t poll_wire_due;
#endif
#ifdef TEST_RAMP_DRIVER_TIMING
static uint32_t timing_wire_delay, timing_wire_due;
static uint8_t timing_control_while_waiting;
#endif
uint32_t HAL_GetTick(void) { return (uint32_t)ticks; }
static QueueHandle_t test_modbus_responses;
void aimtor_test_enqueue_response(const modbus_response_t *response)
{
    assert(xQueueSend(test_modbus_responses, response, 0U) == pdTRUE);
}
static modbus_response_t last_speed_response[SERVO_AXIS_COUNT];
static uint16_t registers[3][65536];
static uint8_t rx[512];
static uint16_t rx_length, rx_offset;
static unsigned tx_count, zero_writes, pwm_starts;
static unsigned read_count, write_count;
/* 按Modbus从站地址统计发送次数，避免与软件轴枚举值混淆。 */
static unsigned tx_by_slave[3];
static unsigned critical_reads[3][2], speed_writes[3];
static unsigned diagnostic_reads[3][4];
static uint8_t diagnostic_current_timeout;
static unsigned check_speed_pair, expect_right_speed, speed_pair_count;
static unsigned response_fault, corrupt_register;
static unsigned ramp_read_drop, ramp_write_drop;
static unsigned boot_zero_drop[3], boot_zero_sent[3];
static unsigned startup_drop[3], startup_sent[3];
static uint8_t startup_simulate, startup_keep_speed;
static uint8_t boot_zero_keep_position;
static uint8_t inhibited;
static unsigned wdg_zero_drop[3], wdg_offline_slave, wdg_enable_writes;
static unsigned wdg_write_order[3], wdg_order_check;
static uint8_t hold_order_check, hold_enable_read_zero;
static uint8_t k1_write_audit;
static unsigned hold_enable_sent[3], hold_enable_drop[3];
static TickType_t wdg_disable_tick[3];
static uint8_t wdg_ramp_simulate, wdg_ramp_stuck;
static TickType_t wdg_ramp_tick[3];
static int16_t wdg_ramp_start[3];
static unsigned wdg_ramp_active[3], wdg_ramp_order[3];
static TIM_TypeDef timer9, timer11;
TIM_HandleTypeDef htim9 = { &timer9 }, htim11 = { &timer11 };
GPIO_TypeDef test_gpio_b, test_gpio_c, test_gpio_e;
static servo_snapshot_t snapshot(servo_axis_t axis);

TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t delay) { ticks += delay; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t delay)
{
    (void)clear;
#ifdef TEST_RAMP_DRIVER_TIMING
    while (delay--) {
        ++ticks;
        if (timing_control_while_waiting) {
            app_control_arbiter_service(); app_hmi_control_service();
        }
    }
#else
    ticks += delay;
#endif
    return 0;
}
QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{
    test_queue_t *q = calloc(1, sizeof(*q) + count * size);
    assert(q != NULL);
    q->capacity = count;
    q->size = size;
    static unsigned response_queues;
    if (size == sizeof(modbus_response_t) && (response_queues++ % 2U) == 0U)
        test_modbus_responses = q;
    return q;
}
void vQueueDelete(QueueHandle_t queue) { free(queue); }
BaseType_t xQueueReset(QueueHandle_t queue)
{
    test_queue_t *q = queue;
    q->head = q->tail = q->count = 0;
    return pdTRUE;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait)
{
    test_queue_t *q = queue;
    (void)wait;
    if (q->count == q->capacity) return pdFALSE;
    if (queue == test_modbus_responses)
    {
        const modbus_response_t *response = item;
        if (response->context.axis < SERVO_AXIS_COUNT &&
            response->status == MODBUS_OK &&
            response->operation == MODBUS_OPERATION_READ_HOLDING &&
            response->address == AIMTOR_REG_ACTUAL_SPEED)
            last_speed_response[response->context.axis] = *response;
    }
    memcpy(q->data + q->head * q->size, item, q->size);
    q->head = (q->head + 1) % q->capacity;
    q->count++;
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{
    test_queue_t *q = queue;
    /* 历史单线程夹具把空总线等待作为RTOS调度时间推进，保留20 ms虚拟调度步。
     * 独立总线/健康故障注入另用真实1 ms等待桩验证生产时序。 */
    if (!q->count) { ticks +=
#if defined(TEST_COLD_CLEAR) || defined(TEST_CLI_SESSION_ONLY) || defined(TEST_BOOT_ZERO_ONLY) || defined(TEST_POLL_WIRE_DELAY_MS)
        wait;
#else
        wait == 1U ? 20U : wait;
#endif
        return pdFALSE; }
    memcpy(item, q->data + q->tail * q->size, q->size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count--;
    return pdTRUE;
}
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    for (unsigned pin = 0U; pin < 16U; pin++)
    {
        if ((init->Pin & (1U << pin)) != 0U)
        {
            port->MODER = (port->MODER & ~(3U << (pin * 2U))) |
                          (init->Mode << (pin * 2U));
        }
    }
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (state) port->ODR |= pin;
    else port->ODR &= ~pin;
}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer; (void)channel; pwm_starts++; return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer; (void)channel; return HAL_OK;
}
void rs485_inhibit_transmit(uint8_t inhibit) { inhibited = inhibit; }
uint16_t rs485_available(void) {
#ifdef TEST_POLL_WIRE_DELAY_MS
    if ((int32_t)(ticks - poll_wire_due) < 0) return 0U;
#endif
#ifdef TEST_RAMP_DRIVER_TIMING
    if ((int32_t)(ticks - timing_wire_due) < 0) return 0U;
#endif
    return rx_length - rx_offset;
}
uint16_t rs485_read(uint8_t *data, uint16_t capacity)
{
    uint16_t n = rs485_available();
    if (n > capacity) n = capacity;
    memcpy(data, rx + rx_offset, n);
    rx_offset += n;
    return n;
}
static uint16_t be16(const uint8_t *data)
{
    return (uint16_t)((data[0] << 8) | data[1]);
}
#ifdef TEST_COLD_CLEAR
static uint8_t cold_audit(const uint8_t *data);
#endif
rs485_status_t rs485_send(const uint8_t *data, uint16_t length)
{
    unsigned axis = data[0], address = be16(data + 2), count, i;
    uint16_t crc;
    if (inhibited) return RS485_BUSY;
    assert(length >= 8);
    crc = modbus_crc16(data, length - 2);
    assert(data[length - 2] == (uint8_t)crc && data[length - 1] == (uint8_t)(crc >> 8));
    tx_count++;
#ifdef TEST_POLL_WIRE_DELAY_MS
    poll_wire_due = ticks + pdMS_TO_TICKS(TEST_POLL_WIRE_DELAY_MS);
#endif
#ifdef TEST_RAMP_DRIVER_TIMING
    timing_wire_due = ticks + timing_wire_delay;
#endif
#ifdef TEST_COLD_CLEAR
    if (cold_audit(data)) return RS485_OK;
#endif
    if (check_speed_pair && APP_REMOTE_SERVO_SYNC_ENABLED)
    {
        /* 左速度应答之后下一事务必须为右速度，不能插入写入或后台读取。 */
        if (expect_right_speed)
        {
            assert(data[1] == 3U && axis == 2U &&
                   address == AIMTOR_REG_ACTUAL_SPEED);
            expect_right_speed = 0U;
            speed_pair_count++;
        }
        if (data[1] == 3U && axis == 1U &&
            address == AIMTOR_REG_ACTUAL_SPEED)
            expect_right_speed = 1U;
    }
    if (axis < 3U)
    {
        tx_by_slave[axis]++;
    }
    if (data[1] == 3U)
    {
        read_count++;
        if (axis < 3U && address == AIMTOR_REG_ACTUAL_SPEED)
            critical_reads[axis][0]++;
        if (axis < 3U && address == AIMTOR_REG_FAULT_CODE)
            critical_reads[axis][1]++;
    }
    else
    {
        write_count++;
        if (axis < 3U && address == AIMTOR_REG_SPEED_COMMAND)
            speed_writes[axis]++;
    }
    rx_length = rx_offset = 0;
    if (wdg_ramp_simulate && wdg_ramp_active[axis] && !wdg_ramp_stuck)
    {
        int32_t start = wdg_ramp_start[axis];
        uint32_t magnitude = (uint32_t)(start < 0 ? -start : start);
        uint32_t decrease = (uint32_t)((uint64_t)(ticks - wdg_ramp_tick[axis]) *
            1000U / APP_WDG_RECOVERY_DECEL_MS);
        registers[axis][AIMTOR_REG_ACTUAL_SPEED] = decrease >= magnitude ? 0U :
            (uint16_t)(start < 0 ? -(int32_t)(magnitude - decrease) :
                                  (int32_t)(magnitude - decrease));
    }
    if (wdg_order_check && data[1] == 6U)
    {
        static const uint16_t order[] = {
            AIMTOR_REG_SERVO_ENABLE, AIMTOR_REG_CONTROL_MODE,
            AIMTOR_REG_SPEED_SELECTION, AIMTOR_REG_SPEED_SOURCE,
            AIMTOR_REG_SPEED_COMMAND, AIMTOR_REG_ACCELERATION_TIME,
            AIMTOR_REG_DECELERATION_TIME, AIMTOR_REG_SERVO_ENABLE
        };
        if (wdg_ramp_order[axis] < 2U)
        {
            assert(address == (wdg_ramp_order[axis] == 0U ?
                AIMTOR_REG_DECELERATION_TIME : AIMTOR_REG_SPEED_COMMAND));
            assert(registers[axis][AIMTOR_REG_SERVO_ENABLE] == 1U);
            assert((int16_t)registers[axis][AIMTOR_REG_ACTUAL_SPEED] != 0);
            wdg_ramp_order[axis]++;
        }
        else
        {
            assert(registers[axis][AIMTOR_REG_ACTUAL_SPEED] == 0U);
            assert(wdg_write_order[axis] < sizeof(order) / sizeof(order[0]));
            assert(address == order[wdg_write_order[axis]++]);
        }
    }
    if (data[1] == 6U && address == AIMTOR_REG_SERVO_ENABLE &&
        be16(data + 4) != 0U) wdg_enable_writes++;
    if (k1_write_audit && data[1] != 3U)
    {
        /* 正常保持/重新接管的生产事务只能补写速度零，禁止模式和使能写入。 */
        assert(data[1] == 6U && address == AIMTOR_REG_SPEED_COMMAND &&
               be16(data + 4) == 0U);
    }
    if (wdg_offline_slave == axis) return RS485_OK;
    if (data[1] == 6U && address == AIMTOR_REG_SERVO_ENABLE &&
        be16(data + 4) == 1U && hold_order_check)
    {
        assert(aimtor_startup_stop_ready() && aimtor_boot_zero_ready());
        assert(registers[axis][AIMTOR_REG_SPEED_COMMAND] == 0U);
        for (unsigned selected = 0U; selected < SERVO_AXIS_COUNT; selected++)
            if (app_config_servo_axis_controllable((uint8_t)selected))
                assert(snapshot((servo_axis_t)selected).control_mode ==
                       SERVO_CONTROL_RS485_VELOCITY);
        hold_enable_sent[axis]++;
        if (hold_enable_drop[axis])
        {
            hold_enable_drop[axis]--;
            return RS485_OK;
        }
    }
    if (data[1] == 6U && address == AIMTOR_REG_SPEED_COMMAND && wdg_zero_drop[axis])
    {
        wdg_zero_drop[axis]--;
        return RS485_OK;
    }
    if (startup_simulate && data[1] == 6U &&
        address == AIMTOR_REG_SERVO_ENABLE)
    {
        if (be16(data + 4) == 0U)
        {
            startup_sent[axis]++;
            if (startup_drop[axis] && wdg_disable_tick[axis] != 0U)
                assert(ticks - wdg_disable_tick[axis] >= APP_WDG_RECOVERY_RETRY_INTERVAL_MS);
            wdg_disable_tick[axis] = ticks;
            if (startup_drop[axis])
            {
                startup_drop[axis]--;
                return RS485_OK;
            }
            if (!startup_keep_speed)
                registers[axis][AIMTOR_REG_ACTUAL_SPEED] = 0U;
        }
    }
    if (data[1] == 6U && address == AIMTOR_REG_HOME_METHOD)
    {
        boot_zero_sent[axis]++;
        if (boot_zero_drop[axis] != 0U)
        {
            boot_zero_drop[axis]--;
            return RS485_OK;
        }
    }
    if ((ramp_read_drop && data[1] == 3U && address == ramp_read_drop) ||
        (ramp_write_drop && data[1] == 6U && address == ramp_write_drop)) return RS485_OK;
    if (response_fault == 1) return RS485_OK;
    if (data[1] == 3) {
        const uint16_t diagnostic_address[4] = {
            AIMTOR_REG_REALTIME_TORQUE, AIMTOR_REG_LOAD_RATIO,
            AIMTOR_REG_PHASE_CURRENT_RMS, AIMTOR_REG_BUS_VOLTAGE
        };
        for (unsigned field = 0U; field < 4U; field++) {
            if (address == diagnostic_address[field]) {
                assert(be16(data + 4) == (field == 2U ? 2U : 1U));
                diagnostic_reads[axis][field]++;
                if (diagnostic_current_timeout && field == 2U) return RS485_OK;
            }
        }
        count = be16(data + 4);
        rx[0] = axis; rx[1] = 3; rx[2] = count * 2;
        for (i = 0; i < count; ++i) {
            uint16_t value = registers[axis][address + i];
            if (hold_enable_read_zero && address == AIMTOR_REG_SERVO_ENABLE &&
                value == 1U) value = 0U;
            if (corrupt_register && address == corrupt_register) value ^= 1;
            rx[3 + i * 2] = value >> 8;
            rx[4 + i * 2] = value;
        }
        rx_length = 3 + count * 2;
    } else {
        count = data[1] == 6 ? 1 : be16(data + 4);
        for (i = 0; i < count; ++i)
            registers[axis][address + i] = be16(data + (data[1] == 6 ? 4 : 7 + i * 2));
        if (wdg_ramp_simulate && address == AIMTOR_REG_SPEED_COMMAND &&
            registers[axis][address] == 0U && !wdg_ramp_active[axis])
        {
            wdg_ramp_active[axis] = 1U;
            wdg_ramp_tick[axis] = ticks;
            wdg_ramp_start[axis] = (int16_t)registers[axis][AIMTOR_REG_ACTUAL_SPEED];
        }
        if (address == AIMTOR_REG_HOME_METHOD) {
            assert(registers[axis][address] == 6);
            zero_writes++;
            if (!boot_zero_keep_position)
            {
                registers[axis][AIMTOR_REG_ABSOLUTE_POSITION] = 0;
                registers[axis][AIMTOR_REG_ABSOLUTE_POSITION + 1] = 0;
            }
        }
        memcpy(rx, data, 6);
        rx_length = 6;
    }
    if (response_fault == 3) { rx[1] |= 128; rx[2] = 2; rx_length = 3; }
    if (response_fault == 4) rx[0] = 9;
    crc = modbus_crc16(rx, rx_length);
    rx[rx_length++] = crc;
    rx[rx_length++] = crc >> 8;
    if (response_fault == 2) rx[rx_length - 1] ^= 1;
    if (response_fault == 5) {
        /* 两帧粘连：先错误地址，再正确响应。 */
        assert(rx_length * 2 <= sizeof(rx));
        memcpy(rx + rx_length, rx, rx_length);
        rx[0] = 9;
        crc = modbus_crc16(rx, rx_length - 2);
        rx[rx_length - 2] = crc;
        rx[rx_length - 1] = crc >> 8;
        rx_length *= 2;
    }
    if (response_fault == 6) rx_length = 4;
    return RS485_OK;
}
#ifndef TEST_PRODUCTION_GUARDS
static void pump_period(unsigned count, TickType_t period)
{
    while (count--) {
        aimtor_service();
        modbus_process();
        aimtor_handle_modbus_response();
        ticks += period;
    }
}
static void pump(unsigned count)
{
#ifdef TEST_SAFETY_STOP_ONLY
    /* Safety fixture explicitly models elapsed time in its phase assertions. */
    pump_period(count, 5U);
#else
    /* Keep legacy wall time while servicing the bus at the production cadence. */
    while (count--) pump_period(5U, pdMS_TO_TICKS(APP_SERVO_COMM_PERIOD_MS));
#endif
}
static servo_snapshot_t snapshot(servo_axis_t axis)
{
    servo_snapshot_t s;
    assert(aimtor_get_snapshot(axis, &s));
    return s;
}
static void protocol_tests(void)
{
    modbus_request_t r;
    modbus_response_t response;
    modbus_diagnostic_t diagnostic;
    unsigned sent, fault;
    TickType_t begin;
    const uint8_t vector[] = {1, 3, 0, 0, 0, 10};
    memset(&r, 0, sizeof(r));
    assert(modbus_crc16(vector, sizeof(vector)) == 0xCDC5);
    r.slave = 1; r.function = 3; r.quantity = 125; r.timeout_ms = 50;
    begin = ticks;
    assert(modbus_submit(&r, 0) == MODBUS_OK);
    modbus_process();
    assert(modbus_get_response(&response, 0) && response.status == MODBUS_OK);
    assert(response.data_length == 251 && ticks - begin < 50);
    for (fault = 1; fault <= 4; ++fault) {
        response_fault = fault;
        sent = tx_count;
        assert(modbus_submit(&r, 0) == MODBUS_OK);
        modbus_process();
        assert(modbus_get_response(&response, 0));
        assert(response.status == (fault == 1 ? MODBUS_TIMEOUT :
               fault == 2 ? MODBUS_CRC_ERROR :
               fault == 3 ? MODBUS_EXCEPTION : MODBUS_ADDRESS_ERROR));
        assert(tx_count == sent + 1);
        modbus_get_diagnostic(&diagnostic);
        assert(diagnostic.status==(uint32_t)response.status && diagnostic.slave==r.slave);
        assert(diagnostic.function==r.function && diagnostic.address==r.address);
        assert(diagnostic.attempt==0U && diagnostic.trace_total>=fault);
    }
    response_fault = 1;
    r.retries = 2;
    sent = tx_count; begin = ticks;
    assert(modbus_submit(&r, 0) == MODBUS_OK);
    modbus_process();
    assert(modbus_get_response(&response, 0));
    assert(tx_count == sent + 3 && ticks - begin <= 160);
    modbus_get_diagnostic(&diagnostic);assert(diagnostic.attempt==2U);
    response_fault = 0;
    r.quantity = 1;
    response_fault = 5;
    assert(modbus_submit(&r, 0) == MODBUS_OK);
    modbus_process();
    assert(modbus_get_response(&response, 0) && response.status == MODBUS_OK);
    response_fault = 6;
    assert(modbus_submit(&r, 0) == MODBUS_OK);
    modbus_process();
    assert(modbus_get_response(&response, 0) && response.status == MODBUS_FRAME_ERROR);
    response_fault = 0;
    r.retries = 255;
    assert(modbus_submit(&r, 0) == MODBUS_INVALID_ARGUMENT);
    r.retries = 0; r.slave = 0;
    assert(modbus_submit(&r, 0) == MODBUS_INVALID_ARGUMENT);
    r.slave = 1; r.address = 65535; r.quantity = 2;
    assert(modbus_submit(&r, 0) == MODBUS_INVALID_ARGUMENT);
    r.address = 0;
    assert(modbus_submit(&r, 0) == MODBUS_OK);
    sent = tx_count;
    modbus_cancel_pending();
    modbus_resume_transport();
    modbus_process();
    assert(tx_count == sent);
    puts("PASS protocol: CRC, long frame, timeout, retry, exception, address, cancel");
}
static void pulse_tests(void)
{
    unsigned starts;
    const unsigned test_frequency_hz = APP_PULSE_MIN_HZ;
    const servo_axis_t axis = app_config_servo_axis_selected(SERVO_AXIS_LEFT) ?
                              SERVO_AXIS_LEFT : SERVO_AXIS_RIGHT;
    TIM_TypeDef *timer = axis == SERVO_AXIS_LEFT ? &timer9 : &timer11;
    GPIO_TypeDef *port = axis == SERVO_AXIS_LEFT ? GPIOE : GPIOB;
    unsigned pin = axis == SERVO_AXIS_LEFT ? 5U : 9U;

    assert(((port->MODER >> (pin * 2U)) & 3U) == 1U);
    assert(port->BSRR == (1U << (pin + 16U)));
    assert(servo_pulse_set_enable(axis, true) == SERVO_PULSE_OK);
    assert(servo_pulse_set_frequency(axis,
                                     test_frequency_hz) == SERVO_PULSE_OK);
    assert(timer->ARR == ((APP_PULSE_TIMER_TICK_HZ / test_frequency_hz) - 1U));
    assert(timer->CCR1 == (APP_PULSE_TIMER_TICK_HZ / test_frequency_hz) / 2U);
    assert(((port->MODER >> (pin * 2U)) & 3U) == GPIO_MODE_AF_PP);
    starts = pwm_starts;
    assert(servo_pulse_set_frequency(axis,
                                     test_frequency_hz) == SERVO_PULSE_OK);
    assert(servo_pulse_set_frequency(axis,
                                     test_frequency_hz * 2U) == SERVO_PULSE_OK);
    assert(pwm_starts == starts &&
           timer->ARR == ((APP_PULSE_TIMER_TICK_HZ /
                           (test_frequency_hz * 2U)) - 1U));
    assert(servo_pulse_set_frequency(axis,
                                     APP_PULSE_MAX_HZ) == SERVO_PULSE_OK);
    assert(timer->ARR == ((APP_PULSE_TIMER_TICK_HZ /
                           APP_PULSE_MAX_HZ) - 1U));
    assert(timer->CCR1 == (APP_PULSE_TIMER_TICK_HZ /
                           APP_PULSE_MAX_HZ) / 2U);
    assert(servo_pulse_set_direction(axis, false) != SERVO_PULSE_OK);
    assert(servo_pulse_set_enable(axis, false) == SERVO_PULSE_OK);
    assert(!servo_pulse_get_frequency(axis));
    assert(test_gpio_c.BSRR == ((GPIO_PIN_8 << 16) | GPIO_PIN_9));
    assert(servo_pulse_set_frequency(axis,
                                     test_frequency_hz) ==
           SERVO_PULSE_NOT_CONFIGURED);
    /* 到数后必须关断并锁存，旧频率请求不能恢复输出。 */
    assert(servo_pulse_set_enable(axis, true) == SERVO_PULSE_OK);
    servo_pulse_set_target(axis, 3U);
    assert(servo_pulse_set_frequency(axis, test_frequency_hz) == SERVO_PULSE_OK);
    for (unsigned i = 0U; i < 3U; i++)
    {
        timer->SR |= TIM_SR_UIF;
        if (axis == SERVO_AXIS_LEFT)
            TIM1_BRK_TIM9_IRQHandler();
        else
            TIM1_TRG_COM_TIM11_IRQHandler();
        /* 无新更新标志时重复进入中断不能重复计数。 */
        if (axis == SERVO_AXIS_LEFT) TIM1_BRK_TIM9_IRQHandler();
        else TIM1_TRG_COM_TIM11_IRQHandler();
        assert(servo_pulse_get_pulse_count(axis) == i + 1U);
    }
    assert(servo_pulse_get_pulse_count(axis) == 3U);
    assert(servo_pulse_target_reached(axis));
    assert(servo_pulse_get_frequency(axis) == 0U);
    assert((timer->DIER & TIM_DIER_UIE) == 0U);
    assert((timer->CCER & TIM_CCER_CC1E) == 0U);
    assert(timer->CCR1 == 0U);
    /* 到数中断即拉低引脚，不依赖下一次运动任务调度。 */
    assert(((port->MODER >> (pin * 2U)) & 3U) == 1U);
    assert(port->BSRR == (1U << (pin + 16U)));
    assert(servo_pulse_set_frequency(axis, test_frequency_hz) != SERVO_PULSE_OK);
    servo_pulse_complete_axis(axis);
    assert(servo_pulse_get_pulse_count(axis) == 3U);
    servo_pulse_set_target(axis, 2U);
    assert(servo_pulse_set_frequency(axis, test_frequency_hz) == SERVO_PULSE_OK);
    assert(((port->MODER >> (pin * 2U)) & 3U) == GPIO_MODE_AF_PP);
    servo_pulse_stop_all();
    assert(((port->MODER >> (pin * 2U)) & 3U) == 1U);
    /* 两轴分别验证高、低方向在到数完成和全轴停止后均不改变。 */
    servo_pulse_init();
    assert((GPIOB->ODR & (GPIO_PIN_0 | GPIO_PIN_1)) ==
           (GPIO_PIN_0 | GPIO_PIN_1));
    for (unsigned a = 0U; a < SERVO_AXIS_COUNT; a++)
    {
        if (!app_config_servo_axis_selected(a)) continue;
        for (unsigned forward = 0U; forward < 2U; forward++)
        {
            unsigned direction_before;
            TIM_TypeDef *axis_timer = a == SERVO_AXIS_LEFT ? &timer9 : &timer11;
            assert(servo_pulse_set_enable((servo_axis_t)a, true) == SERVO_PULSE_OK);
            assert(servo_pulse_set_direction((servo_axis_t)a, forward != 0U) ==
                   SERVO_PULSE_OK);
            direction_before = GPIOB->ODR;
            servo_pulse_set_target((servo_axis_t)a, 1U);
            assert(servo_pulse_set_frequency((servo_axis_t)a,
                                             test_frequency_hz) == SERVO_PULSE_OK);
            axis_timer->SR |= TIM_SR_UIF;
            if (a == SERVO_AXIS_LEFT) TIM1_BRK_TIM9_IRQHandler();
            else TIM1_TRG_COM_TIM11_IRQHandler();
            assert(GPIOB->ODR == direction_before);
            servo_pulse_complete_axis((servo_axis_t)a);
            assert(GPIOB->ODR == direction_before);
            servo_pulse_stop_all();
            assert(GPIOB->ODR == direction_before);
        }
    }
    puts("PASS pulse: limits, direction, gate, exact count, completion restart latch");
}

#ifdef TEST_READONLY_ONLY
static void readonly_tests(void)
{
    unsigned tx_before;
    servo_snapshot_t left;
    servo_snapshot_t right;
    servo_axis_t selected_axis;
    servo_axis_t unselected_axis;
    uint8_t both_selected;

    selected_axis = APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_RIGHT_ONLY ?
                    SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT;
    unselected_axis = selected_axis == SERVO_AXIS_LEFT ?
                      SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT;
    both_selected = APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_BOTH;
    /* 专项测试前清除前置协议测试留下的总线统计。 */
    tx_count = 0U;
    read_count = 0U;
    write_count = 0U;
    memset(tx_by_slave, 0, sizeof(tx_by_slave));
    rx_length = 0U;
    rx_offset = 0U;
    aimtor_init();
    pump(80);
    left = snapshot(SERVO_AXIS_LEFT);
    right = snapshot(SERVO_AXIS_RIGHT);
    assert((selected_axis == SERVO_AXIS_LEFT ? left.query_selected :
                                               right.query_selected) &&
           (selected_axis == SERVO_AXIS_LEFT ? left.online : right.online));
    assert((both_selected ||
            (selected_axis == SERVO_AXIS_LEFT ? !right.query_selected :
                                                !left.query_selected)) &&
           (both_selected ||
            (selected_axis == SERVO_AXIS_LEFT ? !right.online :
                                                !left.online)));
    assert(tx_by_slave[aimtor_get_slave_address(selected_axis)] > 0U);
    assert(both_selected ||
           tx_by_slave[aimtor_get_slave_address(unselected_axis)] == 0U);
    assert(read_count == tx_count);
    assert(write_count == 0U);
    if (!both_selected)
    {
        assert(aimtor_request_status(unselected_axis) == SERVO_NOT_CONFIGURED);
    }
    assert(aimtor_set_control_mode(selected_axis,
                                    SERVO_CONTROL_PULSE_POSITION) ==
           SERVO_NOT_CONFIGURED);
    assert(aimtor_enable(selected_axis) == SERVO_NOT_CONFIGURED);
    assert(aimtor_set_velocity_rpm(selected_axis, 1) ==
           SERVO_NOT_CONFIGURED);
    assert(aimtor_run_segment(selected_axis, 1, 1, 65535) ==
           SERVO_NOT_CONFIGURED);
    assert(aimtor_zero_once(selected_axis) == SERVO_NOT_CONFIGURED);
    assert(aimtor_save_parameters(selected_axis) == SERVO_UNSUPPORTED);
    tx_before = tx_count;
    pump(80);
    assert(tx_count > tx_before);
    assert(write_count == 0U);
    assert(both_selected ||
           tx_by_slave[aimtor_get_slave_address(unselected_axis)] == 0U);
    puts("PASS readonly: selected-axis polling, unselected-axis isolation, write gate");
}
#endif

static void boot_zero_tests(void)
{
    unsigned i;
    servo_boot_zero_status_t boot;
    servo_axis_t test_axis = APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_RIGHT_ONLY ?
                            SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT;
    unsigned test_slave = aimtor_get_slave_address(test_axis);
    const uint32_t skipped[] = {
        0x04000003UL, 0x02000000UL, 0x10000000UL, 0x20000000UL, 0x40000000UL
    };
    for (i = 0U; i < sizeof(skipped) / sizeof(skipped[0]); i++)
    {
        aimtor_init();
        aimtor_boot_zero_init(skipped[i]);
        assert(aimtor_boot_zero_ready());
        assert(aimtor_boot_zero_get_status(SERVO_AXIS_LEFT, &boot));
        assert(!boot.eligible);
    }
    memset(boot_zero_sent, 0, sizeof(boot_zero_sent));
    memset(tx_by_slave, 0, sizeof(tx_by_slave));
    aimtor_init();
    aimtor_boot_zero_init(0x0E000003UL);
    aimtor_startup_hold_init();
#ifdef TEST_READONLY_ONLY
    (void)test_slave;
    assert(aimtor_boot_zero_ready());
    pump(100);
    assert(boot_zero_sent[1] == 0U && boot_zero_sent[2] == 0U);
#else
    assert(!aimtor_boot_zero_ready());
    assert(aimtor_enable(test_axis) == SERVO_NOT_READY);
    assert(aimtor_run_segment(test_axis, 10, 50, 100) == SERVO_NOT_READY);
    assert(aimtor_set_velocity_rpm(test_axis, 50) == SERVO_NOT_READY);
    response_fault = 1U;
    pump(100);
    assert(boot_zero_sent[1] == 0U && boot_zero_sent[2] == 0U);
    response_fault = 0U;
    registers[1][AIMTOR_REG_WORD_ORDER] = 1U;
    registers[2][AIMTOR_REG_WORD_ORDER] = 1U;
    registers[1][AIMTOR_REG_ABSOLUTE_POSITION] = 50U;
    registers[2][AIMTOR_REG_ABSOLUTE_POSITION] = 60U;
    pump(1500);
    assert(aimtor_boot_zero_ready());
    assert(boot_zero_sent[1] == (unsigned)app_config_servo_axis_selected(0U));
    assert(boot_zero_sent[2] == (unsigned)app_config_servo_axis_selected(1U));
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
        if (!app_config_servo_axis_selected((uint8_t)i))
            assert(tx_by_slave[aimtor_get_slave_address((servo_axis_t)i)] == 0U);
    pump(200);
    assert(boot_zero_sent[test_slave] == 1U);
    assert(snapshot(test_axis).enabled ==
           (APP_SERVO_STARTUP_HOLD_ENABLED ? 1U : 0U));
    assert(snapshot(test_axis).zero_verified);
    /* 重试只影响失败轴，第三次成功后不得再写。 */
    aimtor_init();
    memset(boot_zero_sent, 0, sizeof(boot_zero_sent));
    boot_zero_drop[test_slave] = 2U;
    aimtor_boot_zero_init(0x08000000UL);
    pump(1800);
    assert(aimtor_boot_zero_ready());
    assert(boot_zero_sent[test_slave] == 3U);
    /* 三次失败后保持门禁；清锁不能重置计数。 */
    aimtor_init();
    memset(boot_zero_sent, 0, sizeof(boot_zero_sent));
    boot_zero_drop[test_slave] = 3U;
    aimtor_boot_zero_init(0x0E000003UL);
    pump(1800);
    assert(!aimtor_boot_zero_ready());
    assert(aimtor_boot_zero_get_status(test_axis, &boot));
    assert(boot.state == SERVO_BOOT_ZERO_FAILED && boot.failure == 2U);
    assert(boot.attempts == 3U && boot_zero_sent[test_slave] == 3U);
    (void)aimtor_clear_stop_latch();
    pump(200);
    assert(boot_zero_sent[test_slave] == 3U);
    /* 已获写应答但位置不为零，只查询，不重发触发。 */
    aimtor_init();
    memset(boot_zero_sent, 0, sizeof(boot_zero_sent));
    registers[test_slave][AIMTOR_REG_ABSOLUTE_POSITION] = 12U;
    boot_zero_keep_position = 1U;
    aimtor_boot_zero_init(0x0E000003UL);
    pump(1800);
    assert(aimtor_boot_zero_get_status(test_axis, &boot));
    assert(boot.state == SERVO_BOOT_ZERO_FAILED && boot.failure == 3U);
    assert(boot.write_ack && boot.attempts == 1U && boot_zero_sent[test_slave] == 1U);
    boot_zero_keep_position = 0U;
#endif
    aimtor_init();
    aimtor_boot_zero_init(0x04000003UL);
    pump(100);
    puts("PASS boot zero: POR gate, reset skip, offline wait, bounded retry, zero readback");
}

static void startup_stop_tests(void)
{
    unsigned i;
    servo_axis_t axis = APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_RIGHT_ONLY ?
                        SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT;
    unsigned slave = aimtor_get_slave_address(axis);
    servo_startup_stop_status_t startup;
    aimtor_init();
    aimtor_startup_stop_init();
#ifdef TEST_READONLY_ONLY
    (void)i;
    (void)slave;
    (void)startup;
    assert(aimtor_startup_stop_ready());
    pump(100);
#else
    startup_simulate = 1U;
    memset(startup_sent, 0, sizeof(startup_sent));
    memset(tx_by_slave, 0, sizeof(tx_by_slave));
    aimtor_boot_zero_init(0x04000003UL);
    assert(!aimtor_boot_zero_ready());
    assert(aimtor_enable(axis) == SERVO_NOT_READY);
    response_fault = 1U;
    pump(100);
    assert(startup_sent[1] == 0U && startup_sent[2] == 0U);
    response_fault = 0U;
    for (i = 1U; i <= 2U; i++)
    {
        registers[i][AIMTOR_REG_WORD_ORDER] = 1U;
        registers[i][AIMTOR_REG_ACTUAL_SPEED] = 100U;
        registers[i][AIMTOR_REG_SERVO_ENABLE] = 1U;
        registers[i][AIMTOR_REG_SPEED_COMMAND] = 100U;
    }
    pump(1600);
    assert(aimtor_startup_stop_ready() && aimtor_boot_zero_ready());
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
    {
        unsigned id = aimtor_get_slave_address((servo_axis_t)i);
        if (!app_config_servo_axis_selected((uint8_t)i))
        {
            assert(tx_by_slave[id] == 0U);
            continue;
        }
        assert(startup_sent[id] > 0U);
        assert(registers[id][AIMTOR_REG_SERVO_ENABLE] == 0U);
        assert(registers[id][AIMTOR_REG_SPEED_COMMAND] == 0U);
        assert(aimtor_startup_stop_get_status((servo_axis_t)i, &startup));
        assert(startup.attempts == 1U && startup.zero_confirmed);
    }
    /* 三次失能应答丢失后锁定，另一轴仍必须处理停车。 */
    aimtor_init();
    memset(startup_sent, 0, sizeof(startup_sent));
    startup_drop[slave] = 3U;
    aimtor_startup_stop_init();
    pump(1800);
    assert(aimtor_startup_stop_get_status(axis, &startup));
    assert(startup.state == SERVO_STARTUP_STOP_FAILED &&
           startup.attempts == 3U && startup.failure == 1U);
    assert(startup_sent[slave] == 3U);
    assert(!aimtor_boot_zero_ready());
    /* 回读失能值不可信时，不能仅凭写应答通过。 */
    aimtor_init();
    corrupt_register = AIMTOR_REG_SERVO_ENABLE;
    aimtor_startup_stop_init();
    pump(2400);
    assert(aimtor_startup_stop_get_status(axis, &startup));
    assert(startup.state == SERVO_STARTUP_STOP_FAILED &&
           startup.failure == 2U);
    corrupt_register = 0U;
    /* 驱动器有故障仍须先发失能，但不能继续配置或放行。 */
    aimtor_init();
    registers[slave][AIMTOR_REG_FAULT_CODE] = 1U;
    aimtor_startup_stop_init();
    pump(1800);
    assert(aimtor_startup_stop_get_status(axis, &startup));
    assert(startup.disable_ack && startup.state == SERVO_STARTUP_STOP_FAILED &&
           startup.failure == 5U);
    registers[slave][AIMTOR_REG_FAULT_CODE] = 0U;
    /* 有失能应答但反馈持续非零，不允许进入模式配置。 */
    aimtor_init();
    startup_keep_speed = 1U;
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 30U;
    aimtor_startup_stop_init();
    pump(2400);
    assert(aimtor_startup_stop_get_status(axis, &startup));
    assert(startup.state == SERVO_STARTUP_STOP_FAILED &&
           startup.failure == 3U);
    startup_keep_speed = 0U;
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 0U;
    /* POR停车、设零完成后按宏进入使能零速待机，不恢复旧运动。 */
    aimtor_init();
    memset(boot_zero_sent, 0, sizeof(boot_zero_sent));
    aimtor_startup_stop_init();
    aimtor_boot_zero_init(0x0E000003UL);
    aimtor_startup_hold_init();
    pump(1800);
    assert(aimtor_boot_zero_ready());
    assert(boot_zero_sent[slave] == 1U);
    assert(snapshot(axis).enabled ==
           (APP_SERVO_STARTUP_HOLD_ENABLED ? 1U : 0U));
    startup_simulate = 0U;
#endif
    aimtor_init();
    puts("PASS startup stop: forced disable, zero feedback, reset skip, retry and POR chain");
}

static void watchdog_recovery_tests(void)
{
    servo_wdg_recovery_status_t recovery;
    servo_axis_t axis = APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_RIGHT_ONLY ?
                        SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT;
    unsigned slave = aimtor_get_slave_address(axis);
    unsigned i;
    aimtor_init();
    aimtor_watchdog_recovery_init(0U);
    assert(aimtor_watchdog_recovery_ready());
    assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
    assert(recovery.state == SERVO_WDG_RECOVERY_SKIPPED);
    aimtor_watchdog_recovery_init(1U);
#ifdef TEST_READONLY_ONLY
    (void)i; (void)slave;
    assert(aimtor_watchdog_recovery_ready());
    i = write_count;
    pump(100);
    assert(write_count == i);
#else
    assert(!aimtor_watchdog_recovery_ready());
    assert(aimtor_enable(axis) == SERVO_NOT_READY);
    assert(aimtor_set_control_mode(axis, SERVO_CONTROL_PULSE_POSITION) == SERVO_NOT_READY);
    assert(aimtor_set_velocity_rpm(axis, 50) == SERVO_NOT_READY);
    assert(aimtor_run_segment(axis, 100, 50, 100) == SERVO_NOT_READY);
    assert(aimtor_zero_once(axis) == SERVO_NOT_READY);
    assert(motion_start_pulse(axis, 1U, 1000U, 100U) != MOTION_OK);
    startup_simulate = 1U;
    memset(wdg_disable_tick, 0, sizeof(wdg_disable_tick));
    memset(startup_sent, 0, sizeof(startup_sent));
    memset(tx_by_slave, 0, sizeof(tx_by_slave));
    memset(wdg_write_order, 0, sizeof(wdg_write_order));
    memset(wdg_ramp_order, 0, sizeof(wdg_ramp_order));
    memset(wdg_ramp_active, 0, sizeof(wdg_ramp_active));
    wdg_ramp_simulate = 1U;
    wdg_order_check = 1U;
    wdg_enable_writes = 0U;
    for (i = 1U; i <= 2U; i++)
    {
        registers[i][AIMTOR_REG_FAULT_CODE] = 0U;
        registers[i][AIMTOR_REG_ACTUAL_SPEED] = 180U;
        registers[i][AIMTOR_REG_SPEED_COMMAND] = 180U;
        registers[i][AIMTOR_REG_SERVO_ENABLE] = 1U;
        registers[i][AIMTOR_REG_CONTROL_MODE] = AIMTOR_CONTROL_MODE_SPEED;
        registers[i][AIMTOR_REG_SPEED_SELECTION] = 0U;
        registers[i][AIMTOR_REG_SPEED_SOURCE] = 0U;
    }
    pump(700);
    assert(aimtor_watchdog_recovery_ready());
    wdg_order_check = 0U;
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
    {
        unsigned id = aimtor_get_slave_address((servo_axis_t)i);
        if (!app_config_servo_axis_selected((uint8_t)i))
        {
            assert(tx_by_slave[id] == 0U);
            continue;
        }
        assert(aimtor_watchdog_recovery_get_status((servo_axis_t)i, &recovery));
        assert(recovery.state == SERVO_WDG_RECOVERY_DONE && recovery.verified);
        assert(recovery.attempts == 2U && recovery.zero_written && recovery.disable_ack);
        assert(recovery.ramp_applicable && recovery.ramp_verified &&
               recovery.stopped && recovery.zero_samples == 2U);
        assert(recovery.stop_elapsed_ms >= (180UL * APP_WDG_RECOVERY_DECEL_MS / 1000UL));
        assert(recovery.stop_timeout_ms == (180UL * APP_WDG_RECOVERY_DECEL_MS / 1000UL + APP_WDG_RECOVERY_STOP_MARGIN_MS));
        assert(wdg_write_order[id] == 8U);
        assert(registers[id][AIMTOR_REG_SPEED_COMMAND] == 0U);
        assert(registers[id][AIMTOR_REG_SERVO_ENABLE] == 0U);
    }
    assert(wdg_enable_writes == 0U && aimtor_get_remote_control_mask() == 0U);
    i = write_count;
    pump(100);
    assert(write_count == i);
    /* 恢复完成自动衔接启动停车；非POR启动不发送设零。 */
    aimtor_startup_stop_init();
    aimtor_boot_zero_init(0x20000003UL);
    aimtor_startup_hold_init();
    memset(boot_zero_sent, 0, sizeof(boot_zero_sent));
    /* Production service is 1ms: keep 3s observation with the actual cadence.
       5ms serialized transactions can manufacture stale feedback while the peer configures. */
    pump_period(3000U, pdMS_TO_TICKS(APP_SERVO_COMM_PERIOD_MS));
    assert(aimtor_boot_zero_ready());
    assert(boot_zero_sent[1] == 0U && boot_zero_sent[2] == 0U);
    assert(aimtor_startup_hold_ready());
    assert(snapshot(axis).enabled == (APP_SERVO_STARTUP_HOLD_ENABLED ? 1U : 0U));
    wdg_ramp_simulate = 0U;
    for (i = 1U; i <= 2U; i++)
        registers[i][AIMTOR_REG_SERVO_ENABLE] = 0U;
    /* 下一次看门狗启动重新执行，单次失能丢应答最多三次发送。 */
    aimtor_init();
    memset(startup_sent, 0, sizeof(startup_sent));
    memset(wdg_disable_tick, 0, sizeof(wdg_disable_tick));
    startup_drop[slave] = 1U;
    aimtor_watchdog_recovery_init(1U);
    pump(300);
    assert(aimtor_watchdog_recovery_ready());
    assert(startup_sent[slave] == 3U);
    /* 三次失能失败锁存，通信恢复与清锁均不得绕过。 */
    aimtor_init();
    memset(startup_sent, 0, sizeof(startup_sent));
    memset(wdg_disable_tick, 0, sizeof(wdg_disable_tick));
    startup_drop[slave] = 3U;
    aimtor_watchdog_recovery_init(1U);
    pump(800);
    assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
    assert(recovery.state == SERVO_WDG_RECOVERY_FAILED && recovery.attempts == 3U);
    assert(startup_sent[slave] >= 3U && !aimtor_boot_zero_ready());
    (void)aimtor_clear_stop_latch();
    pump(100);
    assert(!aimtor_watchdog_recovery_ready());
    assert(aimtor_disable(axis) == SERVO_OK);
    pump(100);
    assert(startup_sent[slave] >= 4U);
    /* 写零失败不重复配置或使能；反馈非零只读重试直到超时。 */
    aimtor_init();
    wdg_zero_drop[slave] = 3U;
    aimtor_watchdog_recovery_init(1U);
    pump(800);
    assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
    assert(recovery.state == SERVO_WDG_RECOVERY_FAILED && !recovery.zero_written);
    aimtor_init();
    startup_keep_speed = 1U;
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 50U;
    aimtor_watchdog_recovery_init(1U);
    pump(800);
    assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
    assert(recovery.state == SERVO_WDG_RECOVERY_FAILED && recovery.zero_written);
    startup_keep_speed = 0U;
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 0U;
    /* 失能反馈不可信，不伪装成功。 */
    aimtor_init();
    corrupt_register = AIMTOR_REG_SERVO_ENABLE;
    aimtor_watchdog_recovery_init(1U);
    pump(800);
    assert(!aimtor_watchdog_recovery_ready());
    corrupt_register = 0U;
    /* 故障仍先发失能，之后锁存故障；离线轴不阻塞可通信轴。 */
    aimtor_init();
    registers[slave][AIMTOR_REG_FAULT_CODE] = 1U;
    aimtor_watchdog_recovery_init(1U);
    pump(800);
    assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
    assert(recovery.state == SERVO_WDG_RECOVERY_FAILED && recovery.disable_ack);
    registers[slave][AIMTOR_REG_FAULT_CODE] = 0U;
    aimtor_init();
    wdg_offline_slave = slave;
    aimtor_watchdog_recovery_init(1U);
    pump(800);
    assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
    assert(recovery.state == SERVO_WDG_RECOVERY_FAILED && recovery.attempts == 0U);
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
        if (i != (unsigned)axis && app_config_servo_axis_selected((uint8_t)i))
        {
            assert(aimtor_watchdog_recovery_get_status((servo_axis_t)i, &recovery));
            assert(recovery.state == SERVO_WDG_RECOVERY_DONE);
        }
    wdg_offline_slave = 0U;
    /* 正反向600rpm及Tick回绕：零目标后实际仍带速，清理必须等待停稳。 */
    for (unsigned scenario = 0U; scenario < 3U; scenario++)
    {
        int16_t speed = scenario == 1U ? -600 : 600;
        aimtor_init();
        if (scenario == 2U) ticks = UINT32_MAX - 200U;
        memset(wdg_ramp_active, 0, sizeof(wdg_ramp_active));
        wdg_ramp_simulate = 1U;
        for (i = 1U; i <= 2U; i++)
        {
            registers[i][AIMTOR_REG_ACTUAL_SPEED] = (uint16_t)speed;
            registers[i][AIMTOR_REG_SPEED_COMMAND] = (uint16_t)speed;
            registers[i][AIMTOR_REG_SERVO_ENABLE] = 1U;
        }
        aimtor_watchdog_recovery_init(1U);
        pump(80);
        assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
        assert(recovery.zero_written && !recovery.stopped);
        assert(registers[slave][AIMTOR_REG_SERVO_ENABLE] == 1U);
        assert((int16_t)registers[slave][AIMTOR_REG_ACTUAL_SPEED] != 0);
        for (i = 0U; i < 1600U; i++)
        {
            pump(1U);
            assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
            if (recovery.zero_samples == 1U) break;
        }
        assert(recovery.zero_samples == 1U && !recovery.stopped);
        /* 同一事务的重复零速应答，超过20ms也不能计为第二次反馈。 */
        {
            modbus_response_t duplicate = last_speed_response[axis];
            assert(duplicate.data[1] == 0U && duplicate.data[2] == 0U);
            assert(xQueueSend(test_modbus_responses, &duplicate, 0U) == pdTRUE);
            aimtor_handle_modbus_response();
            ticks += 25U;
            assert(xQueueSend(test_modbus_responses, &duplicate, 0U) == pdTRUE);
            aimtor_handle_modbus_response();
            assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
            assert(recovery.zero_samples == 1U && !recovery.stopped);
        }
        pump(1400);
        assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
        assert(recovery.verified && recovery.zero_samples == 2U &&
               recovery.stop_elapsed_ms >= (600UL * APP_WDG_RECOVERY_DECEL_MS / 1000UL) &&
               recovery.stop_timeout_ms == (600UL * APP_WDG_RECOVERY_DECEL_MS / 1000UL + APP_WDG_RECOVERY_STOP_MARGIN_MS));
        assert(registers[slave][AIMTOR_REG_DECELERATION_TIME] == APP_SERVO_SPEED_RAMP_MS);
    }
    /* 不支持的模式/来源、减速回读不符、带速反馈不降及人工急停均锁存。 */
    for (unsigned scenario = 0U; scenario < 7U; scenario++)
    {
        aimtor_init();
        memset(wdg_ramp_active, 0, sizeof(wdg_ramp_active));
        for (i = 1U; i <= 2U; i++)
        {
            registers[i][AIMTOR_REG_ACTUAL_SPEED] = 600U;
            registers[i][AIMTOR_REG_SPEED_COMMAND] = 600U;
            registers[i][AIMTOR_REG_SERVO_ENABLE] = 1U;
            registers[i][AIMTOR_REG_CONTROL_MODE] = AIMTOR_CONTROL_MODE_SPEED;
            registers[i][AIMTOR_REG_SPEED_SOURCE] = 0U;
        }
        if (scenario == 0U) registers[slave][AIMTOR_REG_CONTROL_MODE] = 1U;
        if (scenario == 1U) registers[slave][AIMTOR_REG_SPEED_SOURCE] = 1U;
        if (scenario == 2U) corrupt_register = AIMTOR_REG_DECELERATION_TIME;
        if (scenario == 3U) wdg_ramp_stuck = 1U;
        aimtor_watchdog_recovery_init(1U);
        pump(80);
        if (scenario == 4U) aimtor_emergency_stop();
        if (scenario == 5U) registers[slave][AIMTOR_REG_FAULT_CODE] = 1U;
        if (scenario == 6U) wdg_offline_slave = slave;
        pump(2000);
        assert(aimtor_watchdog_recovery_get_status(axis, &recovery));
        assert(recovery.state == SERVO_WDG_RECOVERY_FAILED);
        assert(recovery.failure_reason == (scenario < 2U ? 3U :
               scenario == 2U ? 5U : scenario == 3U ? 7U :
               scenario == 4U ? 8U : scenario == 5U ? 2U : 6U));
        assert(!aimtor_motion_allowed() && !aimtor_watchdog_recovery_ready());
        if (scenario != 6U)
            assert(registers[slave][AIMTOR_REG_SERVO_ENABLE] == 0U);
        /* 通信失败不能宣称失能已生效，恢复通信后才可读取真实状态。 */
        wdg_offline_slave = 0U;
        corrupt_register = 0U;
        wdg_ramp_stuck = 0U;
        registers[slave][AIMTOR_REG_FAULT_CODE] = 0U;
    }
    wdg_ramp_simulate = 0U;
    for (i = 1U; i <= 2U; i++)
        registers[i][AIMTOR_REG_ACTUAL_SPEED] = 0U;
    startup_simulate = 0U;
#endif
    aimtor_init();
    puts("PASS watchdog recovery: controlled ramp, signed speed, wrap, zero confirmation, failure latch");
}

static void startup_hold_tests(void)
{
    servo_startup_hold_status_t hold;
    servo_axis_t axis = APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_RIGHT_ONLY ?
                        SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT;
    unsigned slave = aimtor_get_slave_address(axis);
    unsigned i;
    const uint32_t skipped[] = {
        0x04000003UL, 0x10000000UL, 0x20000000UL, 0x40000000UL, 0x2E000003UL
    };
    for (i = 0U; i < sizeof(skipped) / sizeof(skipped[0]); i++)
    {
        aimtor_init();
        memset(boot_zero_sent, 0, sizeof(boot_zero_sent));
        for (unsigned id = 1U; id <= 2U; id++)
        {
            registers[id][AIMTOR_REG_SERVO_ENABLE] = 0U;
            registers[id][AIMTOR_REG_ACTUAL_SPEED] = 0U;
            registers[id][AIMTOR_REG_FAULT_CODE] = 0U;
            registers[id][AIMTOR_REG_WORD_ORDER] = 1U;
        }
        aimtor_startup_stop_init();
        aimtor_watchdog_recovery_init((skipped[i] &
            ((1UL << 29U) | (1UL << 30U))) != 0U);
        aimtor_boot_zero_init(skipped[i]);
        aimtor_startup_hold_init();
        assert(aimtor_startup_hold_active() ==
            (!app_config_servo_readonly_test() && APP_SERVO_STARTUP_HOLD_ENABLED));
        pump(1800);
        assert(aimtor_watchdog_recovery_ready() && aimtor_startup_stop_ready() &&
               aimtor_boot_zero_ready() && aimtor_startup_hold_ready());
        assert(snapshot(axis).enabled ==
            (!app_config_servo_readonly_test() && APP_SERVO_STARTUP_HOLD_ENABLED));
        if (APP_SERVO_STARTUP_HOLD_ENABLED && !app_config_servo_readonly_test())
            assert(!aimtor_motion_allowed());
        assert(aimtor_get_remote_control_mask() == 0U);
        assert(boot_zero_sent[1] == 0U && boot_zero_sent[2] == 0U);
    }
    aimtor_init();
    memset(tx_by_slave, 0, sizeof(tx_by_slave));
    memset(hold_enable_sent, 0, sizeof(hold_enable_sent));
    for (i = 1U; i <= 2U; i++)
    {
        registers[i][AIMTOR_REG_SERVO_ENABLE] = 0U;
        registers[i][AIMTOR_REG_ACTUAL_SPEED] = 0U;
        registers[i][AIMTOR_REG_FAULT_CODE] = 0U;
        registers[i][AIMTOR_REG_WORD_ORDER] = 1U;
    }
    aimtor_startup_stop_init();
    aimtor_boot_zero_init(0x0E000003UL);
    aimtor_startup_hold_init();
#if defined(TEST_READONLY_ONLY) || APP_SERVO_STARTUP_HOLD_ENABLED == 0U
    (void)slave;
    (void)hold;
    assert(!aimtor_startup_hold_active());
    pump(1800);
    assert(!snapshot(axis).enabled);
#else
    assert(aimtor_startup_hold_active() && !aimtor_startup_hold_ready());
    assert(!aimtor_motion_allowed());
    hold_order_check = 1U;
    pump(1800);
    assert(aimtor_startup_hold_ready() && !aimtor_motion_allowed());
    for (i = 0U; i < SERVO_AXIS_COUNT; i++)
    {
        unsigned id = aimtor_get_slave_address((servo_axis_t)i);
        if (!app_config_servo_axis_controllable((uint8_t)i))
        {
            assert(tx_by_slave[id] == 0U);
            continue;
        }
        assert(aimtor_startup_hold_get_status((servo_axis_t)i, &hold));
        assert(hold.state == SERVO_STARTUP_HOLD_READY &&
               hold.zero_written && hold.enable_ack && hold.verified);
        assert(hold_enable_sent[id] == 1U);
        assert(registers[id][AIMTOR_REG_SERVO_ENABLE] == 1U);
        assert(registers[id][AIMTOR_REG_SPEED_COMMAND] == 0U);
        assert(aimtor_enable((servo_axis_t)i) == SERVO_NOT_READY);
        assert(aimtor_set_velocity_rpm((servo_axis_t)i, 100) == SERVO_NOT_READY);
        assert(aimtor_run_segment((servo_axis_t)i, 100, 50, 100) == SERVO_NOT_READY);
        assert(aimtor_set_control_mode((servo_axis_t)i,
               SERVO_CONTROL_PULSE_POSITION) == SERVO_NOT_READY);
    }
    aimtor_set_motion_authorized(true);
    assert(aimtor_motion_allowed());
    aimtor_set_remote_control_mask(app_config_servo_control_axis_mask());
    assert(aimtor_set_velocity_rpm(axis, 100) == SERVO_OK);
    pump(100);
    aimtor_set_motion_authorized(false);
    assert(aimtor_set_velocity_rpm(axis, 80) == SERVO_NOT_READY);
    assert(aimtor_set_stop_velocity_rpm(axis, 120) == SERVO_NOT_READY);
    assert(aimtor_set_stop_velocity_rpm(axis, -1) == SERVO_NOT_READY);
    assert(aimtor_set_stop_velocity_rpm(axis, 80) == SERVO_OK);
    pump(100);
    assert(aimtor_set_stop_velocity_rpm(axis, 0) == SERVO_OK);
    pump(100);
    aimtor_set_remote_control_mask(0U);
    assert(registers[slave][AIMTOR_REG_SERVO_ENABLE] == 1U);
    assert(aimtor_disable(axis) == SERVO_OK);
    pump(100);
    assert(!aimtor_startup_hold_ready() && !aimtor_motion_allowed());
    assert(registers[slave][AIMTOR_REG_SERVO_ENABLE] == 0U);
    assert(hold_enable_sent[slave] == 1U);
    /* 成功后实际使能消失，低频回读必须撤销待机并联停。 */
    aimtor_init();
    aimtor_boot_zero_init(0x0E000003UL);
    aimtor_startup_hold_init();
    pump(1800);
    assert(aimtor_startup_hold_ready());
    registers[slave][AIMTOR_REG_SERVO_ENABLE] = 0U;
    pump(100);
    assert(!aimtor_startup_hold_ready() && !aimtor_motion_allowed());
    /* 外力导致非零速度仍保持使能零速，不能误判为急停。 */
    aimtor_init();
    aimtor_boot_zero_init(0x0E000003UL);
    aimtor_startup_hold_init();
    pump(1800);
    assert(aimtor_startup_hold_ready());
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 1U;
    pump(500);
    assert(aimtor_startup_hold_ready() && !aimtor_motion_allowed());
    assert(registers[slave][AIMTOR_REG_SERVO_ENABLE] == 1U);
    assert(registers[slave][AIMTOR_REG_SPEED_COMMAND] == 0U);
    assert(aimtor_startup_hold_get_status(axis, &hold) && hold.disturbed);
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 0U;
    pump(100);
    assert(aimtor_startup_hold_get_status(axis, &hold) && !hold.disturbed);
    /* 人工K5急停须等待失能事务完成，随后重建待机但不自动授权。 */
    aimtor_emergency_stop_k1();
    assert(aimtor_startup_hold_request_rearm() == SERVO_NOT_READY);
    pump(100);
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 1U;
    pump(100);
    assert(aimtor_startup_hold_request_rearm() == SERVO_NOT_READY);
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 0U;
    pump(100);
    assert(aimtor_startup_hold_request_rearm() == SERVO_OK);
    pump(1800);
    assert(aimtor_startup_hold_ready() && !aimtor_motion_allowed());
    assert(registers[slave][AIMTOR_REG_SERVO_ENABLE] == 1U);
    /* 连续短按K5也不得抹掉人工恢复资格。 */
    aimtor_emergency_stop_k1();
    pump(100);
    aimtor_emergency_stop_k1();
    pump(100);
    assert(aimtor_startup_hold_request_rearm() == SERVO_OK);
    pump(1800);
    assert(aimtor_startup_hold_ready());
    /* 真实故障仍必须失能，不能用K5绕过。 */
    aimtor_init();
    aimtor_boot_zero_init(0x0E000003UL);
    aimtor_startup_hold_init();
    pump(1800);
    assert(aimtor_startup_hold_ready());
    registers[slave][AIMTOR_REG_FAULT_CODE] = 1U;
    pump(100);
    assert(!aimtor_startup_hold_ready() && !aimtor_motion_allowed());
    assert(registers[slave][AIMTOR_REG_SERVO_ENABLE] == 0U);
    registers[slave][AIMTOR_REG_FAULT_CODE] = 0U;
    pump(100);
    aimtor_emergency_stop_k1();
    pump(100);
    assert(aimtor_startup_hold_request_rearm() == SERVO_NOT_READY);
    /* 待机后断开单轴，总线不能送达失能也必须保持软件门禁锁存。 */
    aimtor_init();
    aimtor_boot_zero_init(0x0E000003UL);
    aimtor_startup_hold_init();
    pump(1800);
    assert(aimtor_startup_hold_ready());
    wdg_offline_slave = slave;
    pump(300);
    assert(!aimtor_startup_hold_ready() && !aimtor_motion_allowed());
    wdg_offline_slave = 0U;
    pump(100);
    assert(!aimtor_motion_allowed());
    /* 有使能写应答但读回持续为0，不能伪装成功。 */
    aimtor_init();
    hold_enable_read_zero = 1U;
    aimtor_boot_zero_init(0x0E000003UL);
    aimtor_startup_hold_init();
    pump(2600);
    assert(aimtor_startup_hold_get_status(axis, &hold));
    assert(hold.state == SERVO_STARTUP_HOLD_FAILED && !hold.verified);
    assert(!aimtor_motion_allowed());
    hold_enable_read_zero = 0U;
    /* 使能报文丢失后锁存失败，不重复使能或恢复旧速度。 */
    aimtor_init();
    hold_enable_drop[slave] = 1U;
    aimtor_boot_zero_init(0x0E000003UL);
    aimtor_startup_hold_init();
    pump(1800);
    assert(aimtor_startup_hold_get_status(axis, &hold));
    assert(hold.state == SERVO_STARTUP_HOLD_FAILED &&
           hold.failure == 3U);
    assert(!aimtor_startup_hold_ready());
    /* 尚未使能时人工失能必须取消整个流程。 */
    aimtor_init();
    aimtor_boot_zero_init(0x0E000003UL);
    aimtor_startup_hold_init();
    assert(aimtor_disable(axis) == SERVO_OK);
    pump(1800);
    assert(!aimtor_startup_hold_ready() && !snapshot(axis).enabled);
    hold_order_check = 0U;
#endif
    aimtor_init();
    puts("PASS startup hold: all resets, POR-only zero, enable readback, gate, safe stop and failure latch");
}

static void k1_hold_setup(void)
{
    unsigned axis;
    motion_init();
    aimtor_init();
    hold_order_check = hold_enable_read_zero = 0U;
    wdg_offline_slave = response_fault = corrupt_register = 0U;
    memset(hold_enable_drop, 0, sizeof(hold_enable_drop));
    for (axis = 1U; axis <= 2U; axis++)
    {
        registers[axis][AIMTOR_REG_SERVO_ENABLE] = 0U;
        registers[axis][AIMTOR_REG_ACTUAL_SPEED] = 0U;
        registers[axis][AIMTOR_REG_FAULT_CODE] = 0U;
        registers[axis][AIMTOR_REG_WORD_ORDER] = 1U;
    }
    aimtor_startup_stop_init();
    aimtor_boot_zero_init(0x10000000UL);
    aimtor_startup_hold_init();
    pump(1800U);
    if (!APP_SERVO_STARTUP_HOLD_ENABLED && !app_config_servo_readonly_test())
    {
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
            if (app_config_servo_axis_controllable(axis))
            {
                assert(aimtor_set_control_mode((servo_axis_t)axis,
                    SERVO_CONTROL_RS485_VELOCITY) == SERVO_OK);
                pump(200U);
            }
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
            if (app_config_servo_axis_controllable(axis))
            {
                assert(aimtor_enable((servo_axis_t)axis) == SERVO_OK);
                pump(100U);
            }
    }
    assert(aimtor_startup_hold_ready());
    aimtor_set_motion_authorized(false);
    aimtor_set_remote_control_mask(0U);
}

static void k1_hold_tests(void)
{
    servo_k1_hold_status_t hold;
    servo_axis_t axis = app_config_servo_axis_controllable(SERVO_AXIS_LEFT) ?
        SERVO_AXIS_LEFT : SERVO_AXIS_RIGHT;
    unsigned slave = aimtor_get_slave_address(axis);
    unsigned id;
    k1_hold_setup();
    if (app_config_servo_readonly_test())
    {
        unsigned before = write_count;
        assert(aimtor_k1_hold_request(app_config_servo_control_axis_mask()) != SERVO_OK);
        pump(100U);
        assert(write_count == before);
        return;
    }
    assert(aimtor_k1_hold_request(0U) == SERVO_INVALID_ARGUMENT);
    k1_write_audit = 1U;
    assert(aimtor_k1_hold_request(app_config_servo_control_axis_mask()) == SERVO_OK);
    assert(aimtor_k1_hold_get_status(&hold) && !hold.valid &&
           hold.confirmed_mask == 0U && hold.state == SERVO_K1_HOLD_CONFIRMING);
    assert(aimtor_k1_hold_release() == SERVO_NOT_READY);
    aimtor_set_motion_authorized(true);
    assert(!aimtor_motion_allowed());
    assert(aimtor_set_velocity_rpm(axis, 10) == SERVO_NOT_READY);
    assert(aimtor_set_control_mode(axis, SERVO_CONTROL_PULSE_POSITION) == SERVO_NOT_READY);
    pump(150U);
    assert(aimtor_k1_hold_get_status(&hold) && hold.valid &&
           hold.confirmed_mask == app_config_servo_control_axis_mask());
    /* 外力扰动只补写零，不失能；扰动期间拒绝安全释放。 */
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 1U;
    pump(150U);
    assert(aimtor_k1_hold_get_status(&hold) && hold.valid && hold.disturbed);
    assert(aimtor_k1_hold_release() == SERVO_NOT_READY);
    assert(registers[slave][AIMTOR_REG_SERVO_ENABLE] == 1U &&
           registers[slave][AIMTOR_REG_SPEED_COMMAND] == 0U);
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 0U;
    pump(100U);
    assert(aimtor_k1_hold_release() == SERVO_OK);
    aimtor_set_motion_authorized(true);
    aimtor_set_remote_control_mask(app_config_servo_control_axis_mask());
    assert(aimtor_motion_allowed());
    pump(100U);
    for (id = 0U; id < SERVO_AXIS_COUNT; id++)
        if (app_config_servo_axis_controllable(id))
            assert(snapshot((servo_axis_t)id).enabled &&
                   snapshot((servo_axis_t)id).parameter_verified);
    k1_write_audit = 0U;
    /* 各类真实异常必须退出保持并失能，不能新长按释放。 */
    for (id = 0U; id < 6U; id++)
    {
        k1_hold_setup();
        assert(aimtor_k1_hold_request(app_config_servo_control_axis_mask()) == SERVO_OK);
        pump(150U);
        assert(aimtor_k1_hold_get_status(&hold) && hold.valid);
        if (id == 0U) registers[slave][AIMTOR_REG_SERVO_ENABLE] = 0U;
        else if (id == 1U) registers[slave][AIMTOR_REG_FAULT_CODE] = 1U;
        else if (id == 2U) wdg_offline_slave = slave;
        else if (id == 3U) assert(aimtor_disable(axis) == SERVO_OK);
        else if (id == 4U)
        {
            registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 1U;
            wdg_zero_drop[slave] = 1U;
        }
        else
        {
            hold_enable_read_zero = 1U;
        }
        pump(400U);
        assert(aimtor_k1_hold_get_status(&hold) &&
               hold.state == SERVO_K1_HOLD_FAILED && !hold.valid);
        assert(!aimtor_motion_allowed());
        assert(aimtor_k1_hold_release() == SERVO_NOT_READY);
        assert(!snapshot(axis).enabled);
        wdg_offline_slave = hold_enable_read_zero = 0U;
        memset(wdg_zero_drop, 0, sizeof(wdg_zero_drop));
    }
    aimtor_init();
    puts("PASS K5 production hold: independent startup switch, fresh enable read, disturbance, no re-enable/config writes, fault/timeout/disable/zero failure");
}

#ifdef TEST_CLI_SESSION_ONLY
/* 链接生产会话、模式准备及Modbus模块，硬件桩只模拟驱动器寄存器。 */
static void cli_pump(unsigned count)
{
    while (count--)
    {
#ifdef TEST_RAMP_DRIVER_TIMING
        pump_period(1U, pdMS_TO_TICKS(APP_SERVO_COMM_PERIOD_MS));
#else
        pump_period(5U, pdMS_TO_TICKS(APP_SERVO_COMM_PERIOD_MS));
#endif
        app_control_arbiter_service();
        app_cli_servo_session_service();
#ifdef TEST_HMI_PRODUCTION
        app_hmi_control_service();
#endif
        if (app_control_arbiter_owner() != APP_CONTROL_OWNER_HMI) servo_debug_service();
        motion_set_system_ready(aimtor_motion_allowed());
    }
}

static cli_servo_session_status_t cli_status(void)
{
    cli_servo_session_status_t out;
    assert(app_cli_servo_session_get_status(&out));
    return out;
}

static void cli_reset(uint32_t reset)
{
    memset(&cli_remote, 0, sizeof(cli_remote));
    response_fault = corrupt_register = 0U;
    hold_order_check = hold_enable_read_zero = 0U;
    motion_init();
    aimtor_init();
    app_cli_servo_session_init();
    /* Mirrors the unconditional application authorization-gate init. */
    aimtor_cli_gate_init();
    /* A simulated MCU reset also resets the independent bus safety latch. */
    modbus_cancel_pending();
    modbus_safety_release();
    app_control_arbiter_init();
#ifdef TEST_HMI_PRODUCTION
    app_hmi_control_init();
#endif
    for (unsigned slave = 1U; slave <= 2U; slave++)
    {
        registers[slave][AIMTOR_REG_SERVO_ENABLE] = 0U;
        registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 0U;
        registers[slave][AIMTOR_REG_FAULT_CODE] = 0U;
        registers[slave][AIMTOR_REG_WORD_ORDER] = 1U;
    }
    aimtor_startup_stop_init();
    aimtor_boot_zero_init(reset);
    aimtor_startup_hold_init();
    cli_pump(2000U);
}

static uint32_t cli_on(servo_axis_t axis)
{
    uint32_t id;
    assert(app_cli_servo_session_on(axis, &id) == SERVO_OK);
    assert(cli_status().state == CLI_SESSION_ENTERING);
    cli_pump(1600U);
    assert(cli_status().state == CLI_SESSION_READY && cli_status().authorized);
    assert(cli_status().session_id == id);
    return id;
}

static void cli_request(servo_axis_t axis, cli_servo_request_kind_t kind,
                        int32_t value)
{
    cli_servo_request_t request;
    memset(&request, 0, sizeof(request));
    request.axis = axis;
    request.kind = kind;
    request.value = value;
    request.speed = 50U;
    request.ramp = 500U;
    request.frequency = 1000U;
    request.forward = 1U;
    assert(app_cli_servo_session_request(&request) == SERVO_OK);
}

#ifdef TEST_HMI_PRODUCTION
static uint32_t hmi_sequence;
static void hmi_send(uint16_t command, uint32_t session, int16_t rpm)
{
    uint16_t words[24] = {0};
    words[0] = 0x484DU; words[1] = 1U; words[2] = 0x1234U; words[3] = 0x5678U;
    words[4] = (uint16_t)(session >> 16U); words[5] = (uint16_t)session;
    ++hmi_sequence; words[6] = (uint16_t)(hmi_sequence >> 16U); words[7] = (uint16_t)hmi_sequence;
    words[8] = command; words[10] = rpm < 0 ? 0xFFFFU : 0U; words[11] = (uint16_t)rpm;
    words[17] = command == 0x21U ? 500U : 0U; words[22] = 2U; words[23] = 0xA55AU;
    assert(app_hmi_control_submit(words));
    cli_pump(1U);
}
static void hmi_production_tests(void)
{
    app_hmi_control_status_t status;
    servo_axis_t target = APP_SERVO_CONTROL_AXIS == 2U ? SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT;
    uint32_t session;
    unsigned index, axis;
    if (!APP_SERVO_CLI_DEBUG_ENABLED || app_config_servo_readonly_test()) return;
    cli_reset(0x0E000003UL); hmi_sequence = 0U;
    hmi_send(1U, 0U, 0); cli_pump(30U);
    session = app_hmi_control_session();
    app_hmi_control_get_status(&status);
    assert(session && aimtor_hmi_control_active());
    for (index = 0U; index < 80U; ++index)
    {
        hmi_send(0x21U, session, 120); cli_pump(19U);
        for (axis = 0U; axis < 2U; ++axis)
            registers[axis + 1U][AIMTOR_REG_ACTUAL_SPEED] = registers[axis + 1U][AIMTOR_REG_SPEED_COMMAND];
    }
    assert(registers[aimtor_get_slave_address(target)][AIMTOR_REG_SPEED_COMMAND] == 120U);
    app_hmi_control_get_status(&status); assert(status.target == 120 && (status.flags & 2U));
    hmi_send(3U, session, 0);
    app_hmi_control_get_status(&status); assert(!(status.flags & 2U) && (status.flags & 4U));
    for (index = 0U; index < 1200U && app_hmi_control_session(); ++index)
    {
        cli_pump(1U);
        for (axis = 0U; axis < 2U; ++axis)
            registers[axis + 1U][AIMTOR_REG_ACTUAL_SPEED] = registers[axis + 1U][AIMTOR_REG_SPEED_COMMAND];
    }
    app_hmi_control_get_status(&status);
    assert(!app_hmi_control_session() && status.stop.result == 3U);
    assert(status.stop.zero_mask == APP_SERVO_CONTROL_AXIS && status.stop.enabled_mask == APP_SERVO_CONTROL_AXIS);
    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    for (axis = 0U; axis < 2U; ++axis)
        if (app_config_servo_axis_controllable((uint8_t)axis)) assert(snapshot((servo_axis_t)axis).enabled);
    (void)cli_on(target);
    hmi_send(0x10U, 0U, 0); cli_pump(500U);
    app_hmi_control_get_status(&status);
    assert(cli_status().state == CLI_SESSION_IDLE && status.stop.result == 3U);
    assert(status.stop.owner == APP_CONTROL_OWNER_CLI);
    puts("PASS HMI production driver: prepare/rpm/sync/async ACK/zero-enable/release/cross-source CLI");
    cli_reset(0x0E000003UL); hmi_sequence = 0U;
    hmi_send(1U, 0U, 0); cli_pump(30U);
    session = app_hmi_control_session();
    assert(session);
    for (index = 0U; index < 120U; ++index)
    {
        hmi_send(0x21U, session, -1600); cli_pump(19U);
        for (axis = 0U; axis < 2U; ++axis)
            registers[axis + 1U][AIMTOR_REG_ACTUAL_SPEED] = registers[axis + 1U][AIMTOR_REG_SPEED_COMMAND];
    }
    {
        extern void aimtor_test_safety_age(void);
        int16_t before = (int16_t)registers[aimtor_get_slave_address(target)][AIMTOR_REG_SPEED_COMMAND];
        servo_safety_stop_status_t safety;
        assert(before != 0);
        aimtor_test_safety_age();
        aimtor_service();
        app_hmi_control_service();
        assert(aimtor_safety_stop_active());
        assert(!app_hmi_control_session());
        assert(snapshot(target).enabled);
        assert((int16_t)registers[aimtor_get_slave_address(target)][AIMTOR_REG_SPEED_COMMAND] == before);
        for (index = 0U; index < 5000U && aimtor_safety_stop_active(); ++index)
        {
            cli_pump(1U);
            for (axis = 0U; axis < 2U; ++axis)
                registers[axis + 1U][AIMTOR_REG_ACTUAL_SPEED] = registers[axis + 1U][AIMTOR_REG_SPEED_COMMAND];
        }
        assert(aimtor_get_safety_stop_status(&safety));
        assert(safety.completed && !safety.escalated);
        assert(!snapshot(target).enabled && snapshot(target).stop_latched);
        assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
        assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_SAFETY);
    }
    puts("PASS HMI production safety: stale feedback, no premature disable, no old target, fresh zero then fault latch");
}
#endif

static void cli_session_tests(void)
{
    servo_axis_t target = APP_SERVO_CONTROL_AXIS == APP_SERVO_CONTROL_AXIS_RIGHT ?
        SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT;
    servo_axis_t other = target == SERVO_AXIS_LEFT ? SERVO_AXIS_RIGHT : SERVO_AXIS_LEFT;
    uint32_t id, next;
    unsigned slave = aimtor_get_slave_address(target);
    cli_servo_request_t request;
    cli_reset(0x0E000003UL);
    assert(!aimtor_motion_allowed());
    assert(aimtor_set_velocity_rpm(target, 100) != SERVO_OK);
    if (!APP_SERVO_CLI_DEBUG_ENABLED || app_config_servo_readonly_test())
    {
        assert(app_cli_servo_session_on(target, &id) == SERVO_NOT_CONFIGURED);
        puts("PASS CLI session: disabled/readonly cannot authorize");
        return;
    }
    cli_remote.authorized = 1U;
    assert(app_cli_servo_session_on(target, &id) == SERVO_BUSY);
    cli_remote.authorized = 0U;
    /* 遥控K5人工锁存不能通过新CLI会话绕过。 */
    cli_remote.estop_latched = 1U;
    cli_remote.stop_reason = REMOTE_STOP_K1;
    assert(app_cli_servo_session_on(target, &id) == SERVO_BUSY);
    assert(cli_status().state == CLI_SESSION_IDLE);
    cli_remote.estop_latched = 0U;
    cli_remote.stop_reason = REMOTE_STOP_NONE;
    id = cli_on(target);
    assert(!app_cli_servo_session_keepalive(id + 1U));
    assert(app_cli_servo_session_keepalive(id));
    if (app_config_servo_axis_controllable(other))
    {
        assert(snapshot(other).enabled);
        assert(aimtor_set_velocity_rpm(other, 100) != SERVO_OK);
        assert(aimtor_request_parameter_verify(other) != SERVO_OK);
    }
    cli_request(target, CLI_REQUEST_SPEED, 150);
    cli_pump(600U);
    assert(registers[slave][AIMTOR_REG_SPEED_COMMAND] == 150U);
    assert(snapshot(target).ramp_readback_ms[0] == APP_SERVO_DIRECT_ACCELERATION_RAMP_MS);
    assert(snapshot(target).ramp_match_mask == 3U);
    assert(cli_status().state == CLI_SESSION_READY);
    app_cli_servo_session_stop();
    assert(!cli_status().authorized && !aimtor_motion_allowed());
    cli_pump(600U);
    assert(cli_status().state == CLI_SESSION_READY);
    assert(registers[slave][AIMTOR_REG_SPEED_COMMAND] == 0U);
    assert(app_cli_servo_session_keepalive(id));
    cli_request(target, CLI_REQUEST_POSITION, 1000);
    cli_pump(1000U);
    assert(snapshot(target).control_mode == SERVO_CONTROL_RS485_SEGMENT_POSITION);
    assert(registers[slave][AIMTOR_REG_SEGMENT_TRIGGER] == 1U);
    assert(app_cli_servo_session_off() == SERVO_OK);
    cli_pump(1600U);
    assert(cli_status().state == CLI_SESSION_IDLE);
    assert(aimtor_startup_hold_ready());
    assert(snapshot(target).enabled == aimtor_startup_hold_active());
    if (aimtor_startup_hold_active())
        assert(snapshot(target).control_mode == SERVO_CONTROL_RS485_VELOCITY);

    id = cli_on(target);
    cli_request(target, CLI_REQUEST_PULSE, 1000);
    cli_pump(500U);
    assert(snapshot(target).control_mode == SERVO_CONTROL_PULSE_POSITION);
    for (unsigned step = 0U; step < 20U; step++) motion_update();
    assert(servo_pulse_output_active(target));
    assert(!servo_pulse_output_active(other));
    if (app_config_servo_axis_controllable(other))
        assert(motion_start_pulse_mask((uint8_t)(1U << other), 1U, 1000U,
                                      1000U) == MOTION_NOT_READY);
    app_cli_servo_session_abort(CLI_EXIT_K1);
    cli_pump(500U);
    assert(cli_status().state == CLI_SESSION_LOCKED);
    assert(cli_status().recoverable);
    assert(!snapshot(target).enabled && !servo_pulse_output_active(target));
    next = cli_on(target);
    assert(next != id && !app_cli_servo_session_keepalive(id));
    assert(app_cli_servo_session_keepalive(next));
    cli_request(target, CLI_REQUEST_SPEED, 99);
    /* 停止优先标志取消未启动普通请求，不会发送99。 */
    app_cli_servo_session_stop();
    cli_pump(600U);
    assert(registers[slave][AIMTOR_REG_SPEED_COMMAND] != 99U);
    assert(app_cli_servo_session_keepalive(next));
    /* 状态查询不刷新保活，反馈持续新鲜时准确到期。 */
    cli_pump(APP_SERVO_CLI_DEBUG_TIMEOUT_MS / 5U);
    assert(cli_status().state == CLI_SESSION_LOCKED);
    assert(cli_status().reason == CLI_EXIT_TIMEOUT);
    ticks = UINT32_MAX - 100000U;
    /* 在回绕前建立新会话，持续处理真实事务跨过回绕边界。 */
    cli_reset(0x10000000UL);
    /* An artificial nonzero boot tick needs an explicit initial probe: zero
       scheduler deadlines represent the real MCU boot, not a late uptime. */
    for (unsigned a=0U; a<2U; ++a)
        if (app_config_servo_axis_selected((uint8_t)a))
            assert(aimtor_request_status((servo_axis_t)a) == SERVO_OK);
    cli_pump(2000U);
    id = cli_on(target);
    assert(ticks > UINT32_MAX / 2U);
    assert(aimtor_startup_hold_active() == (APP_SERVO_STARTUP_HOLD_ENABLED != 0U));
    assert(app_cli_servo_session_keepalive(id));
    cli_pump(APP_SERVO_CLI_DEBUG_TIMEOUT_MS / 5U);
    assert(ticks < UINT32_MAX / 2U);
    assert(cli_status().state == CLI_SESSION_LOCKED &&
           cli_status().reason == CLI_EXIT_TIMEOUT);
    assert(!snapshot(target).enabled);
    id = cli_on(target);
    cli_request(target, CLI_REQUEST_SPEED, 80);
    cli_pump(600U);
    assert(registers[slave][AIMTOR_REG_SPEED_COMMAND] == 80U);
    assert(app_cli_servo_session_off() == SERVO_OK);
    cli_pump(1600U);
    assert(cli_status().state == CLI_SESSION_IDLE &&
           snapshot(target).enabled == (APP_SERVO_STARTUP_HOLD_ENABLED != 0U));
    /* 非POR也恢复零速待机；普通速度入口仍被撤权门禁拒绝。 */
    if (APP_SERVO_STARTUP_HOLD_ENABLED != 0U)
    {
        assert(aimtor_startup_hold_ready());
        assert(snapshot(target).control_mode == SERVO_CONTROL_RS485_VELOCITY);
    }
    else
    {
        assert(aimtor_remote_prepare_mode(target) == SERVO_OK);
        cli_pump(500U);
        assert(aimtor_remote_prepare_enable(target) == SERVO_OK);
        cli_pump(100U);
    }
    assert(aimtor_set_velocity_rpm(target, 100) == SERVO_NOT_READY);
    aimtor_emergency_stop();
    cli_pump(300U);
    /* 真实故障不能通过新的on清除。 */
    cli_reset(0x0E000003UL);
    (void)cli_on(target);
    registers[slave][AIMTOR_REG_FAULT_CODE] = 7U;
    cli_pump(300U);
    assert(cli_status().state == CLI_SESSION_LOCKED && !cli_status().recoverable);
    app_cli_servo_session_abort(CLI_EXIT_MANUAL);
    cli_pump(100U);
    assert(!cli_status().recoverable && cli_status().reason == CLI_EXIT_SAFETY);
    registers[slave][AIMTOR_REG_FAULT_CODE] = 0U;
    cli_pump(300U);
    assert(app_cli_servo_session_on(target, &id) == SERVO_NOT_READY);
    memset(&request, 0, sizeof(request));
    request.axis = other;
    assert(app_cli_servo_session_request(&request) == SERVO_NOT_READY);
    if (app_config_servo_axis_controllable(other))
    {
        /* BOTH固件运行时切换到另一轴，原轴不得收到运动写入。 */
        cli_reset(0x0E000003UL);
        (void)cli_on(other);
        cli_request(other, CLI_REQUEST_SPEED, 75);
        cli_pump(600U);
        assert(registers[aimtor_get_slave_address(other)][AIMTOR_REG_SPEED_COMMAND] == 75U);
        assert(registers[slave][AIMTOR_REG_SPEED_COMMAND] == 0U);
        assert(snapshot(target).enabled);
        assert(app_cli_servo_session_off() == SERVO_OK);
        cli_pump(1600U);
        assert(cli_status().state == CLI_SESSION_IDLE);
    }
    /* entering期间退出不会短暂取得运动授权，也不会遗留进入请求。 */
    cli_reset(0x0E000003UL);
    assert(app_cli_servo_session_on(target, &id) == SERVO_OK);
    assert(app_cli_servo_session_off() == SERVO_OK);
    assert(!cli_status().authorized);
    cli_pump(1600U);
    assert(cli_status().state == CLI_SESSION_IDLE);
    assert(aimtor_startup_hold_ready());
    /* 连续停止不刷新5000ms停稳期限；非零反馈不能伪装已停稳。 */
    (void)cli_on(target);
    registers[slave][AIMTOR_REG_ACTUAL_SPEED] = 10U;
    cli_pump(50U);
    app_cli_servo_session_stop();
    for (unsigned step = 0U; step < 1200U; step++)
    {
        app_cli_servo_session_stop();
        cli_pump(1U);
    }
    assert(cli_status().state == CLI_SESSION_LOCKED &&
           !cli_status().recoverable && cli_status().reason == CLI_EXIT_SAFETY);
    /* 测试等待复位门禁独立于待机开关，不能经由显式on或内部权限绕过。 */
    cli_reset(0x0E000003UL);
    id = cli_on(target);
    cli_request(target, CLI_REQUEST_SPEED, 120);
    cli_pump(600U);
    watchdog_test_requested = 1U;
    aimtor_emergency_stop();
    /* 重放旧速度成功应答，底层必须忽略，不推进旧CLI请求。 */
    assert(xQueueSend(test_modbus_responses, &last_speed_response[target], 0U) == pdTRUE);
    assert(!aimtor_motion_allowed());
    aimtor_set_motion_authorized(true);
    aimtor_cli_authorize(true);
    aimtor_cli_internal(true);
    assert(!aimtor_motion_allowed());
    assert(!app_cli_servo_session_keepalive(id));
    assert(aimtor_set_velocity_rpm(target, 100) == SERVO_NOT_READY);
    assert(aimtor_set_stop_velocity_rpm(target, 0) == SERVO_NOT_READY);
    assert(aimtor_enable(target) == SERVO_NOT_READY);
    assert(aimtor_remote_prepare_mode(target) == SERVO_NOT_READY);
    assert(aimtor_remote_prepare_enable(target) == SERVO_NOT_READY);
    assert(aimtor_set_control_mode(target, SERVO_CONTROL_PULSE_POSITION) == SERVO_NOT_READY);
    assert(aimtor_clear_stop_latch() == SERVO_NOT_READY);
    assert(aimtor_cli_restore_hold() == SERVO_NOT_READY);
    assert(aimtor_startup_hold_request_rearm() == SERVO_NOT_READY);
    memset(&request, 0, sizeof(request));
    request.axis = target;
    request.kind = CLI_REQUEST_SPEED;
    request.value = 200;
    assert(app_cli_servo_session_request(&request) == SERVO_NOT_READY);
    cli_pump(600U);
    assert(cli_status().state == CLI_SESSION_LOCKED && !cli_status().authorized);
    assert(app_cli_servo_session_on(target, &id) == SERVO_NOT_READY);
    assert(!snapshot(target).enabled && !servo_pulse_output_active(target));
    watchdog_test_requested = 0U;
    /* 准备时间从申请计时，CLI保活不能延长准备预约。 */
    cli_reset(0x0E000003UL);
    assert(app_cli_servo_session_on(target, &id) == SERVO_OK);
    for (unsigned wait = 0U; wait < 11U; ++wait)
    {
        assert(app_cli_servo_session_keepalive(id));
        pump_period(1U, 200U);
    }
    app_cli_servo_session_service();
    assert(cli_status().state == CLI_SESSION_LOCKED && cli_status().reason == CLI_EXIT_TIMEOUT);
    for (unsigned deceleration = 500U; deceleration <= 600U; deceleration += 100U)
    {
        cli_reset(0x0E000003UL);
        (void)cli_on(target);
        cli_request(target, CLI_REQUEST_SPEED, 600);
        cli_pump(600U);
        assert(registers[slave][AIMTOR_REG_SPEED_COMMAND] == 600U);
        /* K5先提交600，CLI通用500入口不得减弱该事件。 */
        if (deceleration == 600U)
            assert(app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_REMOTE,
                5U, (uint16_t)deceleration, 1U, 0x10U));
        app_cli_servo_session_external_stop();
        cli_pump(50U);
        assert(!cli_status().authorized);
        assert(cli_status().state == CLI_SESSION_STOPPING);
        assert(registers[slave][AIMTOR_REG_SPEED_COMMAND] > 0U &&
            registers[slave][AIMTOR_REG_SPEED_COMMAND] < 600U);
        cli_pump(2200U);
        assert(cli_status().state == CLI_SESSION_IDLE);
        assert(aimtor_startup_hold_ready());
        app_control_stop_t stop;
        app_control_arbiter_get_stop(&stop);
        assert(stop.result == 3U && stop.error == 0U && stop.owner == APP_CONTROL_OWNER_CLI);
        assert(stop.deceleration_mm_s2 == deceleration);
        assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    }
    puts("PASS CLI production: runtime axis/three modes/POR restore/nonPOR/priority/K5/timeout/fault gates/external ramp");
}
#endif

int main(void)
{
    servo_snapshot_t s;
    unsigned zeros;
    const float test_velocity_mps =
        (float)(APP_PULSE_MIN_HZ + 50U) / APP_PULSES_PER_METER;
    motion_init();
    assert(modbus_init() == MODBUS_OK);
    {
        extern void aimtor_test_diagnostic(void);
        aimtor_test_diagnostic();
    }
#ifdef TEST_DIAGNOSTIC_ONLY
    {
        for (unsigned slave = 1U; slave <= 2U; slave++) {
            registers[slave][AIMTOR_REG_REALTIME_TORQUE] = (uint16_t)-123;
            registers[slave][AIMTOR_REG_LOAD_RATIO] = 456U;
            registers[slave][AIMTOR_REG_PHASE_CURRENT_RMS] = (uint16_t)-1234;
            registers[slave][AIMTOR_REG_PHASE_CURRENT_RMS + 1U] = 0xFFFFU;
            registers[slave][AIMTOR_REG_BUS_VOLTAGE] = 3250U;
        }
        aimtor_init();
        memset(tx_by_slave, 0, sizeof(tx_by_slave));
        {
            TickType_t start = ticks;
            while (ticks - start < pdMS_TO_TICKS(5000U))
                pump_period(1U, pdMS_TO_TICKS(APP_SERVO_COMM_PERIOD_MS));
        }
        for (unsigned axis = 0U; axis < 2U; axis++) {
            unsigned slave = axis + 1U;
            s = snapshot((servo_axis_t)axis);
            if (app_config_servo_axis_selected((uint8_t)axis)) {
                assert(s.diagnostic_valid_mask == 15U);
                assert(s.realtime_torque_tenths_percent == -123);
                assert(s.load_ratio_tenths_percent == 456U);
                assert(s.phase_current_deciamp == -123);
                assert(s.bus_voltage_decivolt == 3250U);
                assert(s.diagnostic_poll_failures == 0U);
                assert(s.position_valid && s.position_sequence > 0U);
                assert(s.position_age_ms < APP_POSITION_MAX_AGE_MS);
                for (unsigned item = 0U; item < SERVO_POLL_CLASSES; item++) {
                    servo_poll_status_t poll;
                    assert(aimtor_get_poll_status((servo_axis_t)axis, (uint8_t)item, &poll));
                    assert(poll.submitted >= poll.succeeded && poll.succeeded > 0U);
                    assert(poll.failed == 0U && poll.age_ms != UINT32_MAX);
                }
                for (unsigned field = 0U; field < 4U; field++) {
                    assert(diagnostic_reads[slave][field] >= 3U);
                    assert(diagnostic_reads[slave][field] <= 5U);
                }
                assert(s.speed_valid && s.fault_valid);
                assert(s.speed_age_ms <= APP_SERVO_STATUS_MAX_AGE_MS);
            } else {
                assert(tx_by_slave[slave] == 0U);
                assert(s.diagnostic_valid_mask == 0U);
            }
        }
        puts("PASS servo diagnostic scheduler: selected axes, one second cycles, safety polling retained");
        diagnostic_current_timeout = 1U;
        {
            TickType_t start = ticks;
            while (ticks - start < pdMS_TO_TICKS(1500U))
                pump_period(1U, pdMS_TO_TICKS(APP_SERVO_COMM_PERIOD_MS));
        }
        for (unsigned axis = 0U; axis < 2U; axis++) {
            if (!app_config_servo_axis_selected((uint8_t)axis)) continue;
            s = snapshot((servo_axis_t)axis);
            assert(s.diagnostic_valid_mask == 15U);
            assert(s.diagnostic_stale_mask & 4U);
            assert(s.phase_current_deciamp == -123);
            assert(s.diagnostic_poll_failures > 0U);
            assert(s.speed_valid && s.fault_valid && !s.stop_latched);
            assert(s.consecutive_failures == 0U);
        }
        diagnostic_current_timeout = 0U;
        {
            TickType_t start = ticks;
            while (ticks - start < pdMS_TO_TICKS(1000U))
                pump_period(1U, pdMS_TO_TICKS(APP_SERVO_COMM_PERIOD_MS));
        }
        for (unsigned axis = 0U; axis < 2U; axis++) {
            if (app_config_servo_axis_selected((uint8_t)axis))
                assert(!(snapshot((servo_axis_t)axis).diagnostic_stale_mask & 4U));
        }
        puts("PASS servo diagnostic timeout: cached values retained, safety reads unaffected, retry recovery");
    }
    return 0;
#endif
#ifdef TEST_CLI_SESSION_ONLY
#ifdef TEST_HMI_PRODUCTION
    (void)cli_session_tests;
    hmi_production_tests();
#else
    cli_session_tests();
#endif
    return 0;
#endif
    protocol_tests();
    pulse_tests();
    boot_zero_tests();
    startup_stop_tests();
    watchdog_recovery_tests();
    startup_hold_tests();
    k1_hold_tests();
    /* 专项故障测试会锁存运动故障，后续用例按独立启动清理测试状态。 */
    motion_init();
#ifdef TEST_BOOT_ZERO_ONLY
    return 0;
#endif
#ifdef TEST_READONLY_ONLY
    readonly_tests();
    puts("ALL READONLY TESTS PASSED (host mocks; not hardware validation)");
    return 0;
#endif
    registers[1][AIMTOR_REG_WORD_ORDER] = 1;
    registers[2][AIMTOR_REG_WORD_ORDER] = 1;
    aimtor_init();
    pump(50);
    assert(snapshot(SERVO_AXIS_LEFT).online && snapshot(SERVO_AXIS_RIGHT).online);
    assert(aimtor_set_control_mode(SERVO_AXIS_LEFT, SERVO_CONTROL_PULSE_POSITION) == SERVO_OK);
    assert(aimtor_set_control_mode(SERVO_AXIS_RIGHT,
                                   SERVO_CONTROL_PULSE_POSITION) ==
           (app_config_servo_axis_controllable(SERVO_AXIS_RIGHT) ?
            SERVO_BUSY : SERVO_NOT_CONFIGURED));
    pump(100);
    s = snapshot(SERVO_AXIS_LEFT);
    assert(s.parameter_verified && !s.zero_verified && !s.busy);
    zeros = zero_writes;
    {
        uint32_t generation = snapshot(SERVO_AXIS_LEFT).position_generation;
        assert(aimtor_zero_once(SERVO_AXIS_LEFT) == SERVO_OK);
        s = snapshot(SERVO_AXIS_LEFT);
        assert(s.position_generation == generation + 1U && !s.position_valid);
        for (unsigned sample = 0; sample < SERVO_POSITION_SAMPLE_CAPACITY; sample++)
            assert(!s.position_samples[sample].valid);
    }
    pump(100);
    assert(zero_writes == (zeros + 1U));
    assert(snapshot(SERVO_AXIS_LEFT).zero_verified);
    if (app_config_servo_axis_controllable(SERVO_AXIS_RIGHT))
    {
        assert(aimtor_enable(SERVO_AXIS_RIGHT) == SERVO_NOT_READY);
        assert(!snapshot(SERVO_AXIS_RIGHT).parameter_verified);
        assert(!snapshot(SERVO_AXIS_RIGHT).zero_verified);
    }
    else
    {
        assert(aimtor_enable(SERVO_AXIS_RIGHT) == SERVO_NOT_CONFIGURED);
        assert(!snapshot(SERVO_AXIS_RIGHT).parameter_verified);
        assert(!snapshot(SERVO_AXIS_RIGHT).zero_verified);
    }
    /* 双轴配置的运动门禁要求两轴均已配置、核验并使能。 */
    assert(aimtor_set_control_mode(SERVO_AXIS_RIGHT,
                                   SERVO_CONTROL_PULSE_POSITION) == SERVO_OK);
    pump(100);
    assert(aimtor_enable(SERVO_AXIS_RIGHT) == SERVO_OK);
    pump(30);
    assert(aimtor_enable(SERVO_AXIS_LEFT) == SERVO_OK);
    pump(30);
    assert(snapshot(SERVO_AXIS_LEFT).enabled);
    motion_set_system_ready(1);
#if APP_CLI_SERVO_ACTIVE && APP_SERVO_PULSE_ENABLED
    /* 使用生产调试状态机，验证新反馈确认和双轴完成等待。 */
    servo_debug_init();
    assert(servo_debug_run_pulse(true, 30000U, 3U) == SERVO_OK);
    for (unsigned i = 0U; i < 1500U; i++)
    {
        /* 与生产通信任务一致，按1ms推进事务，不回拨测试时钟。 */
        pump_period(1U, 1U);
        servo_debug_service();
        motion_set_system_ready(1);
    }
    {
        servo_debug_status_t debug;
        assert(servo_debug_get_status(&debug));
        assert(debug.state == SERVO_DEBUG_RUNNING);
        motion_update();
        for (unsigned i = 0U; i < 3U; i++)
        {
            timer9.SR |= TIM_SR_UIF;
            timer11.SR |= TIM_SR_UIF;
            TIM1_BRK_TIM9_IRQHandler();
            TIM1_TRG_COM_TIM11_IRQHandler();
        }
        motion_update();
        servo_debug_motion_service();
        servo_debug_service();
        assert(servo_debug_get_status(&debug));
        assert(!debug.pulse_stop_confirmed);
        /* 即使速度寄存器为零，新的位置反馈持续变化也不得报告完成。 */
        for (unsigned i = 0U; i < 150U; i++)
        {
            registers[1][AIMTOR_REG_ABSOLUTE_POSITION] = (uint16_t)(3U + i);
            registers[2][AIMTOR_REG_ABSOLUTE_POSITION] = (uint16_t)(3U + i);
            pump_period(1U, 1U);
            servo_debug_service();
            motion_update();
            servo_debug_motion_service();
            assert(servo_debug_get_status(&debug));
            assert(!debug.pulse_stop_confirmed);
            assert(!servo_pulse_get_frequency(SERVO_AXIS_LEFT));
            assert(!servo_pulse_get_frequency(SERVO_AXIS_RIGHT));
        }
        /* 桩中的反馈位置与本次命令脉冲使用相同单位。 */
        registers[1][AIMTOR_REG_ABSOLUTE_POSITION] = 3U;
        registers[1][AIMTOR_REG_ABSOLUTE_POSITION + 1U] = 0U;
        registers[2][AIMTOR_REG_ABSOLUTE_POSITION] = 3U;
        registers[2][AIMTOR_REG_ABSOLUTE_POSITION + 1U] = 0U;
        for (unsigned i = 0U; i < 150U; i++)
        {
            pump_period(1U, 1U);
            servo_debug_service();
            motion_update();
            servo_debug_motion_service();
        }
        assert(servo_debug_get_status(&debug));
        assert(debug.pulse_stop_confirmed && !debug.pulse_completion_timeout);
        assert(debug.state == SERVO_DEBUG_READY);
        assert(snapshot(SERVO_AXIS_LEFT).enabled);
        assert(snapshot(SERVO_AXIS_RIGHT).enabled);
        assert(!servo_pulse_get_frequency(SERVO_AXIS_LEFT));
        assert(!servo_pulse_get_frequency(SERVO_AXIS_RIGHT));
        puts("PASS pulse completion: fresh feedback, stable position, enabled hold");
    }
#endif
    assert(motion_set_velocity(NAN, 0) == MOTION_INVALID_ARGUMENT);
    assert(motion_set_velocity(INFINITY, 0) == MOTION_INVALID_ARGUMENT);
    assert(motion_move_distance(1, .05f, .1f) == MOTION_NOT_CONFIGURED);
    assert(motion_set_velocity(test_velocity_mps, 0.0f) == MOTION_OK);
    for (unsigned i = 0; i < 800; ++i) motion_update();
    assert(servo_pulse_get_frequency(SERVO_AXIS_LEFT) > 0);
    assert(motion_set_velocity(-test_velocity_mps, 0.0f) == MOTION_OK);
    for (unsigned i = 0; i < 140; ++i) motion_update();
    assert(motion_stop(MOTION_STOP_IMMEDIATE) == MOTION_OK);
    assert(servo_pulse_get_frequency(SERVO_AXIS_LEFT) == 0U);
    motion_report_fault(1);
    motion_set_system_ready(1);
    assert(motion_get_state() == MOTION_OFFLINE);
    aimtor_emergency_stop();
    pump(60);
    assert(snapshot(SERVO_AXIS_LEFT).disable_verified);
    assert(aimtor_clear_stop_latch() == SERVO_OK);
    assert(motion_get_state() == MOTION_SAFE);
    assert(aimtor_set_maintenance_enabled(true) == SERVO_OK);
    assert(aimtor_set_control_mode(SERVO_AXIS_LEFT, SERVO_CONTROL_RS485_VELOCITY) == SERVO_OK);
    pump(100);
    s = snapshot(SERVO_AXIS_LEFT);
    assert(s.ramp_read_mask == 3U && s.ramp_match_mask == 3U);
    assert(s.ramp_requested_ms[0] == APP_SERVO_DIRECT_ACCELERATION_RAMP_MS);
    assert(s.ramp_readback_ms[0] == APP_SERVO_DIRECT_ACCELERATION_RAMP_MS);
    assert(s.ramp_readback_ms[1] == APP_SERVO_SPEED_DECELERATION_RAMP_MS);
    for (unsigned scenario = 0U; scenario < 6U; ++scenario)
    {
        unsigned address = scenario % 2U ? AIMTOR_REG_DECELERATION_TIME : AIMTOR_REG_ACCELERATION_TIME;
        if (scenario < 2U) corrupt_register = address;
        else if (scenario < 4U) ramp_read_drop = address;
        else ramp_write_drop = address;
        assert(aimtor_set_control_mode(SERVO_AXIS_LEFT, SERVO_CONTROL_RS485_VELOCITY) == SERVO_OK);
        pump(100);
        s = snapshot(SERVO_AXIS_LEFT);
        assert(!s.busy && !s.parameter_verified && !s.enabled && !s.stop_latched);
        assert(s.last_result != SERVO_OK && s.ramp_match_mask != 3U);
        assert(aimtor_enable(SERVO_AXIS_LEFT) != SERVO_OK);
        corrupt_register = ramp_read_drop = ramp_write_drop = 0U;
        assert(aimtor_set_control_mode(SERVO_AXIS_LEFT, SERVO_CONTROL_RS485_VELOCITY) == SERVO_OK);
        pump(100);
        s = snapshot(SERVO_AXIS_LEFT);
        assert(s.parameter_verified && s.ramp_read_mask == 3U && s.ramp_match_mask == 3U);
    }
    puts("PASS driver ramp: guard=1, deceleration retained, both readbacks, mismatches/read/write failures deny enable");
    assert(aimtor_enable(SERVO_AXIS_LEFT) == SERVO_OK);
    pump(30);
    assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT, APP_OPERATION_MAX_MOTOR_RPM + 1) == SERVO_INVALID_ARGUMENT);
    assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT, 10) == SERVO_OK);
    pump(30);
    assert(registers[1][AIMTOR_REG_SPEED_COMMAND] == 10);
    aimtor_stop_all();
    pump(30);
    assert(registers[1][AIMTOR_REG_SPEED_COMMAND] == 0);
    aimtor_emergency_stop();
    pump(60);
    assert(aimtor_clear_stop_latch() == SERVO_OK);
    assert(aimtor_set_control_mode(SERVO_AXIS_LEFT, SERVO_CONTROL_RS485_SEGMENT_POSITION) == SERVO_OK);
    pump(100);
    assert(aimtor_enable(SERVO_AXIS_LEFT) == SERVO_OK);
    pump(30);
    assert(aimtor_run_segment(SERVO_AXIS_LEFT, 200, APP_OPERATION_MAX_MOTOR_RPM + 1, 65535) == SERVO_INVALID_ARGUMENT);
    assert(aimtor_run_segment(SERVO_AXIS_LEFT, -200, 5, 65535) == SERVO_OK);
    pump(30);
    assert(registers[1][AIMTOR_REG_SEGMENT_DISTANCE] == (uint16_t)-200);
    assert(registers[1][AIMTOR_REG_SEGMENT_DISTANCE + 1] == 0xFFFF);
    assert(registers[1][AIMTOR_REG_SEGMENT_TRIGGER] == 1);
    assert(aimtor_run_segment(SERVO_AXIS_LEFT, 50, 5, 65535) != SERVO_OK);
    response_fault = 1;
    pump(30);
    assert(snapshot(SERVO_AXIS_LEFT).stop_latched);
    assert(motion_get_state() == MOTION_OFFLINE);
    response_fault = 0;
    aimtor_emergency_stop();
    pump(60);
    /* 独立总线空队列等待缩短为1 ms；按新鲜反馈等待，不能依赖固定循环次数。 */
    for (unsigned wait=0;wait<400;wait++) {
        servo_snapshot_t l=snapshot(SERVO_AXIS_LEFT), r=snapshot(SERVO_AXIS_RIGHT);
        if (l.online && r.online && l.speed_valid && r.speed_valid && l.fault_valid && r.fault_valid) break;
        pump(1);
    }
    assert(aimtor_clear_stop_latch() == SERVO_OK);
    corrupt_register = AIMTOR_REG_PULSE_FORMAT;
    assert(aimtor_request_parameter_verify(SERVO_AXIS_LEFT) == SERVO_OK);
    pump(100);
    s = snapshot(SERVO_AXIS_LEFT);
    /* 模式配置失败只结束本次配置，不应把尚未进入运动阶段的轴锁存为故障。 */
    assert(!s.stop_latched && !s.parameter_verified && !s.enabled);
    assert(!servo_pulse_get_frequency(SERVO_AXIS_LEFT));
    puts("PASS maintenance: segment word order, one trigger, offline latch");
    puts("PASS device/motion: readback, zero once, mode lock, limits, reverse, stop latch");
    corrupt_register = 0;
    aimtor_emergency_stop();
    pump(60);
    assert(aimtor_clear_stop_latch() == SERVO_OK);
    for (unsigned axis = 0; axis < SERVO_AXIS_COUNT; axis++)
    {
        assert(aimtor_set_control_mode((servo_axis_t)axis,
                                       SERVO_CONTROL_RS485_VELOCITY) == SERVO_OK);
        pump(100);
    }
    for (unsigned axis = 0; axis < SERVO_AXIS_COUNT; axis++)
    {
        assert(aimtor_enable((servo_axis_t)axis) == SERVO_OK);
        pump(30);
    }
    aimtor_set_remote_control_mask(3);
    check_speed_pair = 1U;
    memset(critical_reads, 0, sizeof(critical_reads));
    memset(speed_writes, 0, sizeof(speed_writes));
    /* 持续速度提交压力下，两轴两类关键回读不得饥饿。 */
    for (unsigned i = 0; i < 1000; i++)
    {
        for (unsigned axis = 0; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (!snapshot((servo_axis_t)axis).busy)
                assert(aimtor_set_velocity_rpm((servo_axis_t)axis,
                                               (int16_t)(i % 200)) == SERVO_OK);
        }
        pump(1);
        for (unsigned axis = 0; axis < SERVO_AXIS_COUNT; axis++)
        {
            s = snapshot((servo_axis_t)axis);
            assert(s.speed_age_ms < 100 && s.fault_age_ms < 100);
        }
    }
    for (unsigned axis = 1; axis <= 2; axis++)
        assert(critical_reads[axis][0] > 50 && critical_reads[axis][1] > 50 &&
               speed_writes[axis] > 10);
    if (APP_REMOTE_SERVO_SYNC_ENABLED) assert(speed_pair_count > 50);
    check_speed_pair = 0U;
    /* 非POR撤权停车也封闭普通运动入口，专用停车仍可递减。 */
    aimtor_set_remote_stop_active(true);
    assert(!aimtor_motion_allowed());
    assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT, 600) == SERVO_NOT_READY);
    pump(30);
    assert(aimtor_set_stop_velocity_rpm(SERVO_AXIS_LEFT, 0) == SERVO_OK);
    pump(30);
    assert(aimtor_set_stop_velocity_rpm(SERVO_AXIS_LEFT, 1) == SERVO_NOT_READY);
    aimtor_set_remote_stop_active(false);
    /* 快照历史保留生产速度应答的序号及接收时刻。 */
    s = snapshot(SERVO_AXIS_LEFT);
    assert(s.speed_samples[SERVO_SPEED_SAMPLE_CAPACITY - 1U].valid);
    assert(s.speed_samples[SERVO_SPEED_SAMPLE_CAPACITY - 1U].speed_sequence ==
           s.speed_sequence);
    assert(s.speed_samples[SERVO_SPEED_SAMPLE_CAPACITY - 1U].sample_tick ==
           s.speed_sample_tick);
    assert(aimtor_request_position(SERVO_AXIS_LEFT) == SERVO_OK);
    pump(60);
    s = snapshot(SERVO_AXIS_LEFT);
    assert(s.position_samples[SERVO_POSITION_SAMPLE_CAPACITY - 1U].valid);
    assert(s.position_samples[SERVO_POSITION_SAMPLE_CAPACITY - 1U].position_sequence ==
           s.position_sequence);
    assert(s.position_samples[SERVO_POSITION_SAMPLE_CAPACITY - 1U].sample_tick ==
           s.position_sample_tick);
    assert(s.position_samples[SERVO_POSITION_SAMPLE_CAPACITY - 1U].position_generation ==
           s.position_generation);
#if APP_CAN_CONTROL_ENABLED
    {
        unsigned before, tries; uint32_t write_sequence;
        aimtor_set_can_control(true, APP_SERVO_CONTROL_AXIS);
        assert(!aimtor_motion_allowed());
        assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT, 20) == SERVO_NOT_READY);
        assert(aimtor_set_control_mode(SERVO_AXIS_LEFT, SERVO_CONTROL_RS485_SEGMENT_POSITION) == SERVO_NOT_READY);
        before = speed_writes[1];
        assert(aimtor_can_velocity(SERVO_AXIS_LEFT, 20, 10U, 1U, false) == SERVO_OK);
        write_sequence = snapshot(SERVO_AXIS_LEFT).speed_write_sequence;
        aimtor_can_revoke(11U); pump(10);
        assert(speed_writes[1] == before);
        assert(snapshot(SERVO_AXIS_LEFT).speed_write_completed == write_sequence);
        assert(snapshot(SERVO_AXIS_LEFT).speed_write_result == SERVO_NOT_READY);
        aimtor_set_can_control(true, APP_SERVO_CONTROL_AXIS);
        assert(aimtor_can_velocity(SERVO_AXIS_LEFT, 40, 12U, 2U, false) == SERVO_OK);
        for (tries = 0U; tries < 100U && speed_writes[1] == before; ++tries)
        {
            aimtor_service(); modbus_process();
            if (speed_writes[1] == before) aimtor_handle_modbus_response();
            ticks += 5U;
        }
        assert(speed_writes[1] == before + 1U);
        aimtor_can_revoke(13U); aimtor_handle_modbus_response(); pump(10);
        assert(speed_writes[1] == before + 1U);
        assert(!snapshot(SERVO_AXIS_LEFT).busy);
        assert(aimtor_can_velocity(SERVO_AXIS_LEFT, 41, 13U, 3U, true) == SERVO_NOT_READY);
        assert(aimtor_can_velocity(SERVO_AXIS_LEFT, 0, 13U, 3U, true) == SERVO_OK);
        pump(30); assert(registers[1][AIMTOR_REG_SPEED_COMMAND] == 0U);
        aimtor_set_can_control(false, 0U);
        puts("PASS CAN production AIMtor: own permission, queued cancellation, late ACK, no old retry, monotonic stop");
    }
#endif
    {
        extern void aimtor_test_arm_work_lock(uint8_t locked);
        extern void aimtor_test_arm_final_writer(void);
        aimtor_test_arm_final_writer();
        aimtor_test_arm_work_lock(1U);
        assert(!aimtor_motion_allowed());
        assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT,40)==SERVO_NOT_READY);
        aimtor_test_arm_work_lock(0U);
        puts("PASS ARM3 production driver: final nonzero write denied under work lock");
    }
    /* 急停取消已排队的速度请求，迟到旧应答不得推进旧速度序列。 */
    assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT, 99) == SERVO_OK ||
           snapshot(SERVO_AXIS_LEFT).busy);
    aimtor_service();
    aimtor_emergency_stop();
    pump(60);
    assert(snapshot(SERVO_AXIS_LEFT).stop_latched);
    assert(snapshot(SERVO_AXIS_RIGHT).stop_latched);
    assert(!snapshot(SERVO_AXIS_LEFT).enabled && !snapshot(SERVO_AXIS_RIGHT).enabled);
    puts("PASS dual bus: critical polling fairness, speed writes, cancellation and linked stop");
    {
        extern void aimtor_test_position_scheduler(void);
        aimtor_test_position_scheduler();
        puts("PASS production position: response timestamps, zero generations, paired background polls");
    }
    puts("ALL CORE TESTS PASSED (host mocks; not hardware validation)");
    return 0;
}

#endif /* TEST_PRODUCTION_GUARDS */
