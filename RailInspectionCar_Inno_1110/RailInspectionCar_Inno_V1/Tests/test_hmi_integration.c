#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "usart.h"
#include "app_hmi_state.h"
#include "app_boot_identity.h"
#include "app_control_arbiter.h"
#include "app_hmi_control.h"
#include "app_health.h"
#define APP_RTOS_CREATE_H
extern TaskHandle_t motion_task_handle, servo_comm_task_handle, health_task_handle, modbus_task_handle;
extern TaskHandle_t key_task_handle, status_task_handle, cli_task_handle;
extern TaskHandle_t lcd_task_handle, iwdg_task_handle, nrf_task_handle, can_task_handle, hmi_task_handle;
#include "../APP/app_system_snapshot.c"
#ifdef TEST_HMI_READ_SOURCE_HOOK
/* Dedicated fault-injection suite may publish a new Health snapshot directly
 * after the production read returns, before the HMI observes its result. */
static uint8_t test_hmi_source_read(app_system_snapshot_t *snapshot,
    uint32_t *source_ms, uint32_t *sequence, uint32_t *source_age_ms,
    uint8_t *initialized);
#define app_system_snapshot_read_with_meta test_hmi_source_read
#endif
/* 直接调用生产ISR与服务，不复制帧边界实现。 */
#define s_rx_generation bsp_rx_generation
#include "../BSP/HMI_UART/bsp_hmi_uart.c"
#undef s_rx_generation
#include "../APP/HMI/app_hmi.c"
#define s_boot_id snapshot_boot_id
#include "../APP/HMI/app_hmi_snapshot.c"
#undef s_boot_id

bool app_arm3_port_hmi_read(uint16_t start,uint16_t n,uint16_t *v,uint32_t now)
{(void)start;(void)n;(void)v;(void)now;return false;}
bool app_arm3_port_hmi_write(uint16_t start,uint16_t n,const uint16_t *v,uint32_t now)
{(void)start;(void)n;(void)v;(void)now;return false;}
static bool guard_permitted = true;
bool modbus_motion_permitted(void) { return guard_permitted; }
UART_HandleTypeDef huart5;
static test_uart_t uart_registers;
test_core_debug_t test_core_debug;
test_dwt_t test_dwt;
uint32_t SystemCoreClock = 1000000U;
TaskHandle_t motion_task_handle, servo_comm_task_handle, health_task_handle, modbus_task_handle;
TaskHandle_t key_task_handle, status_task_handle, cli_task_handle;
TaskHandle_t lcd_task_handle, iwdg_task_handle, nrf_task_handle, can_task_handle, hmi_task_handle;
static uint32_t tick, unsafe_calls, identity_id = 1U, irq_cost;
static can_identity_t identity;
static uint8_t response[256];
static uint16_t response_size;
static bool transmit_fail;
static HAL_StatusTypeDef receive_result, abort_rx_result, abort_tx_result;
static uint8_t error_during_rx_abort, error_during_tx_abort;
static uint8_t receive_ok_unarmed, abort_rx_ok_active, abort_tx_ok_active;
static uint32_t clear_calls, abort_rx_calls, abort_tx_calls;
static uint64_t simulated_us;
static bool startup_ready = true;
static app_control_owner_t snapshot_owner;
static uint32_t snapshot_generation;
static uint8_t snapshot_race;
static unsigned notifications;
uint32_t HAL_GetTick(void) { return tick; }
TickType_t xTaskGetTickCount(void) { return tick; }
uint32_t __get_PRIMASK(void) { return 0U; }
uint32_t __get_IPSR(void) { return 0U; }
BaseType_t xTaskNotifyGive(TaskHandle_t task) { assert(task != NULL); notifications++; return pdTRUE; }
void __disable_irq(void) { }
void __set_PRIMASK(uint32_t mask) { (void)mask; test_dwt.CYCCNT += irq_cost; }
DMA_Stream_TypeDef test_rx_stream, test_tx_stream;
test_rcc_t test_rcc;
static DMA_HandleTypeDef rx_dma, tx_dma;
static test_timer_t timer_registers;
TIM_HandleTypeDef htim2 = { &timer_registers };
static uint8_t abort_async, abort_stuck, mailbox_accept;
static unsigned mailbox_calls;
static uint16_t dma_write_position;
static uint32_t mailbox_generation;
void HAL_NVIC_ClearPendingIRQ(int irq) { (void)irq; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 42000000U; }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken)
{ assert(task != NULL); notifications++; *woken = pdTRUE; }
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length)
{
    assert(uart == &huart5 && data != NULL && length == APP_HMI_DMA_RX_SIZE);
    if (receive_result != HAL_OK) return receive_result;
    if (receive_ok_unarmed) return HAL_OK;
    if (uart->RxState != HAL_UART_STATE_READY) return HAL_BUSY;
    uart->RxState = HAL_UART_STATE_BUSY_RX; uart->pRxBuffPtr = data;
    uart->RxXferCount = uart->RxXferSize = length;
    uart->ReceptionType = HAL_UART_RECEPTION_TOIDLE; uart->ErrorCode = 0U;
    uart->hdmarx->ErrorCode = 0U;
    uart->hdmarx->Instance->CR = DMA_SxCR_EN | DMA_SxCR_CIRC;
    uart->hdmarx->Instance->NDTR = length;
    uart->Instance->CR1 |= USART_CR1_IDLEIE;
    uart->Instance->CR3 |= USART_CR3_DMAR | USART_CR3_EIE;
    dma_write_position = 0U;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *uart, const uint8_t *data, uint16_t length)
{
    assert(uart == &huart5);
    if (transmit_fail) return HAL_ERROR;
    if (uart->gState != HAL_UART_STATE_READY) return HAL_BUSY;
    memcpy(response, data, length); response_size = length;
    uart->gState = HAL_UART_STATE_BUSY_TX; uart->TxXferCount = length;
    uart->hdmatx->Instance->CR = DMA_SxCR_EN;
    uart->hdmatx->Instance->NDTR = length;
    uart->Instance->CR3 |= USART_CR3_DMAT;
    return HAL_OK;
}
static void abort_rx_done(DMA_HandleTypeDef *dma)
{
    dma->Instance->CR &= ~DMA_SxCR_EN;
    huart5.RxState = HAL_UART_STATE_READY; huart5.RxXferCount = 0U;
    bsp_hmi_uart_rx_abort_isr();
}
static void abort_tx_done(DMA_HandleTypeDef *dma)
{
    dma->Instance->CR &= ~DMA_SxCR_EN;
    huart5.gState = HAL_UART_STATE_READY; huart5.TxXferCount = 0U;
    uart_registers.CR1 &= ~(USART_CR1_TXEIE | USART_CR1_TCIE);
    bsp_hmi_uart_tx_abort_isr();
}
HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *uart)
{
    abort_rx_calls++;
    if (error_during_rx_abort) { error_during_rx_abort = 0; bsp_hmi_uart_error_isr(); }
    if (abort_rx_result != HAL_OK) return abort_rx_result;
    uart->Instance->CR1 &= ~USART_CR1_IDLEIE;
    uart->Instance->CR3 &= ~(USART_CR3_DMAR | USART_CR3_EIE);
    uart->hdmarx->XferAbortCallback = abort_rx_done;
    if (!abort_stuck) uart->hdmarx->Instance->CR &= ~DMA_SxCR_EN;
    if (!abort_async && !abort_stuck && !abort_rx_ok_active) abort_rx_done(uart->hdmarx);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit_IT(UART_HandleTypeDef *uart)
{
    abort_tx_calls++;
    if (error_during_tx_abort) { error_during_tx_abort = 0; request_tx(HMI_UART_RECOVERY_STATE); }
    if (abort_tx_result != HAL_OK) return abort_tx_result;
    uart->Instance->CR3 &= ~USART_CR3_DMAT;
    uart->hdmatx->XferAbortCallback = abort_tx_done;
    if (!abort_stuck) uart->hdmatx->Instance->CR &= ~DMA_SxCR_EN;
    if (!abort_async && !abort_stuck && !abort_tx_ok_active) abort_tx_done(uart->hdmatx);
    return HAL_OK;
}
void test_uart_clear(UART_HandleTypeDef *uart)
{
    assert(uart->RxState == HAL_UART_STATE_READY);
    clear_calls++; uart->Instance->SR = 0U; uart->Instance->DR = 0U;
}
uint32_t app_boot_identity_id(void) { return identity_id; }
bool app_boot_identity_valid(void) { return identity_id != UINT32_MAX; }
const can_identity_t *app_boot_identity_get(void) { return &identity; }
void debug_uart_get_stats(debug_uart_stats_t *stats) { memset(stats, 0, sizeof(*stats)); }
void modbus_get_stats(modbus_stats_t *stats) { memset(stats, 0, sizeof(*stats)); }
bool app_nrf_remote_get_snapshot(nrf_remote_snapshot_t *snapshot)
{ memset(snapshot, 0, sizeof(*snapshot)); return true; }
void app_hmi_control_init(void) { }
uint8_t app_hmi_control_submit(const uint16_t *mailbox) { (void)mailbox; mailbox_calls++; mailbox_generation = s_rx_generation; return mailbox_accept; }
void app_hmi_control_get_status(app_hmi_control_status_t *status) { memset(status, 0, sizeof(*status)); }
bool app_can_control_get_status(app_can_control_status_t *out)
{ memset(out, 0, sizeof(*out)); out->stop_result = 255U; return true; }
uint16_t app_hmi_control_remaining_ms(void) { return 0U; }
void app_hmi_control_trace_stats(app_hmi_trace_stats_t *out) { memset(out, 0, sizeof(*out)); }
uint8_t app_hmi_control_trace_read(uint16_t index, app_hmi_trace_event_t *out)
{ (void)index; (void)out; return 0U; }
uint32_t app_hmi_control_session(void) { return 0U; }
uint32_t app_control_arbiter_generation(void) { return snapshot_generation; }
app_control_owner_t app_control_arbiter_owner(void) { return snapshot_owner; }
app_control_lock_reason_t app_control_arbiter_lock_reason(void) { return APP_CONTROL_LOCK_NONE; }
void app_control_arbiter_safety_takeover(app_control_lock_reason_t lock) { (void)lock; unsafe_calls++; }
void aimtor_emergency_stop(void) { unsafe_calls++; }
void aimtor_set_hmi_control(bool enabled, uint8_t generation) { (void)enabled; (void)generation; unsafe_calls++; }
bool app_health_watchdog_test_requested(void) { return false; }
uint32_t app_health_get_reset_reason(void) { return 1UL << 27; }
uint32_t app_health_get_diagnostic_faults(void) { return 0U; }
app_heartbeat_t app_health_get_failed_heartbeat(void) { return APP_HEARTBEAT_COUNT; }
TickType_t app_health_get_heartbeat_age(app_heartbeat_t h) { (void)h; return 1U; }
uint16_t app_health_get_watchdog_decision(void) { return 0U; }
uint8_t app_config_servo_control_axis_mask(void) { return APP_SERVO_CONTROL_AXIS; }
uint8_t app_config_servo_axis_selected(uint8_t axis) { return (APP_SERVO_CONTROL_AXIS & (1U << axis)) != 0; }
uint8_t app_config_servo_readonly_test(void) { return APP_SERVO_READONLY_TEST; }
uint8_t app_config_motion_hardware_ready(void) { return 1U; }
uint8_t app_config_servo_bus_ready(void) { return 1U; }
bool aimtor_maintenance_enabled(void) { return false; }
float motion_get_command_velocity(uint8_t axis) { (void)axis; return 0.0f; }
motion_state_t motion_get_state(void) { return MOTION_SAFE; }
motion_stop_class_t motion_get_stop_class(void) { return MOTION_STOP_CLASS_NONE; }
motion_fault_reason_t motion_get_fault_reason(void) { return MOTION_FAULT_REASON_NONE; }
bool aimtor_get_snapshot_pair(servo_snapshot_t *left, servo_snapshot_t *right)
{
    memset(left, 0, sizeof(*left)); memset(right, 0, sizeof(*right));
    left->online = right->online = 1; left->speed_valid = right->speed_valid = 1;
    left->fault_valid = right->fault_valid = 1;
    return true;
}
bool app_position_monitor_get_status(position_monitor_status_t *s) { memset(s, 0, sizeof(*s)); return true; }
bool app_remote_servo_get_status(remote_servo_status_t *s)
{
    memset(s, 0, sizeof(*s));
    if (snapshot_race) { snapshot_race = 0U; snapshot_generation++; }
    return true;
}
bool app_cli_servo_session_get_status(cli_servo_session_status_t *s) { memset(s, 0, sizeof(*s)); return true; }
bool servo_debug_get_status(servo_debug_status_t *s) { memset(s, 0, sizeof(*s)); return true; }
bool aimtor_k1_hold_get_status(servo_k1_hold_status_t *s) { memset(s, 0, sizeof(*s)); return true; }
#define STATUS_STUB(name, type) bool name(servo_axis_t axis, type *s) { (void)axis; memset(s, 0, sizeof(*s)); return true; }
STATUS_STUB(aimtor_startup_stop_get_status, servo_startup_stop_status_t)
STATUS_STUB(aimtor_startup_hold_get_status, servo_startup_hold_status_t)
STATUS_STUB(aimtor_watchdog_recovery_get_status, servo_wdg_recovery_status_t)
STATUS_STUB(aimtor_boot_zero_get_status, servo_boot_zero_status_t)
bool aimtor_watchdog_recovery_ready(void) { return startup_ready; }
bool aimtor_startup_stop_ready(void) { return startup_ready; }
bool aimtor_boot_zero_ready(void) { return startup_ready; }
bool aimtor_startup_hold_ready(void) { return startup_ready; }
bool aimtor_startup_hold_active(void) { return false; }
bool aimtor_motion_allowed(void) { return true; }
uint8_t app_control_arbiter_axis_mask(void) { return APP_SERVO_CONTROL_AXIS; }
void app_control_arbiter_get_stop(app_control_stop_t *stop) { memset(stop, 0, sizeof(*stop)); }
bool app_can_get_snapshot(app_can_status_t *s, can_upper_snapshot_t upper[7])
{ memset(s, 0, sizeof(*s)); memset(upper, 0, 7 * sizeof(*upper)); return true; }
bool app_can_get_arm_summary(uint8_t arm_id, can_arm_summary_t *summary)
{ (void)arm_id; memset(summary, 0, sizeof(*summary)); return false; }
bool app_can_get_arm_joint(uint8_t arm_id, uint8_t joint_id,
                           can_arm_joint_t *joint)
{ (void)arm_id; (void)joint_id; memset(joint, 0, sizeof(*joint)); return false; }
bool app_can_get_arm_result(can_arm_result_t *result)
{ memset(result, 0, sizeof(*result)); return false; }
void app_can_get_arm_stats(can_arm_stats_t *stats)
{ memset(stats, 0, sizeof(*stats)); }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 512U; }

static void advance(uint32_t us)
{
    simulated_us += us;
    test_dwt.CYCCNT += (uint32_t)((uint64_t)us * SystemCoreClock / 1000000U);
    tick = (uint32_t)(simulated_us / 1000U);
    if (sys_initialized && (uint32_t)(tick - sys_buffer[sys_active].now_ms) >=
        APP_HEALTH_TASK_PERIOD_MS) app_system_snapshot_service();
}
static void inject(const uint8_t *data, unsigned count)
{
    unsigned index;
    for (index = 0U; index < count; index++)
    {
        advance(90U);
        if (!rx_armed()) continue;
        s_dma_rx[dma_write_position] = data[index];
        dma_write_position = (uint16_t)((dma_write_position + 1U) % APP_HMI_DMA_RX_SIZE);
        test_rx_stream.NDTR = APP_HMI_DMA_RX_SIZE - dma_write_position;
        if (dma_write_position == APP_HMI_DMA_RX_SIZE / 2U || dma_write_position == 0U)
        {
            bsp_hmi_uart_dma_irq_isr(0U);
            bsp_hmi_uart_rx_event_isr(dma_write_position ? dma_write_position : APP_HMI_DMA_RX_SIZE,
                dma_write_position ? HAL_UART_RXEVENT_HT : HAL_UART_RXEVENT_TC);
        }
    }
}
static void idle(void)
{
    advance(100U); bsp_hmi_uart_irq_entry_isr();
    bsp_hmi_uart_rx_event_isr(dma_write_position, HAL_UART_RXEVENT_IDLE);
}
static void boundary(void)
{
    idle(); advance(1750U); bsp_hmi_uart_quiet_isr();
}
static void crc(uint8_t *frame, unsigned n)
{ uint16_t c = hmi_modbus_crc16(frame, (uint16_t)n); frame[n] = (uint8_t)c; frame[n+1] = (uint8_t)(c >> 8); }
static void request(uint8_t *frame, uint16_t start, uint16_t count)
{ frame[0]=1; frame[1]=3; frame[2]=(uint8_t)(start>>8); frame[3]=(uint8_t)start; frame[4]=(uint8_t)(count>>8); frame[5]=(uint8_t)count; crc(frame,6); }
static void flush(void) { boundary(); app_hmi_service(); }
static void complete(void)
{
    test_tx_stream.flags = 16U; bsp_hmi_uart_dma_irq_isr(1U);
    test_tx_stream.flags = 0U; test_tx_stream.CR &= ~DMA_SxCR_EN;
    uart_registers.CR3 &= ~USART_CR3_DMAT;
    huart5.gState = HAL_UART_STATE_READY; huart5.TxXferCount = 0;
    uart_registers.CR1 &= ~(USART_CR1_TXEIE | USART_CR1_TCIE);
    bsp_hmi_uart_tx_complete_isr();
}
static void fresh(void)
{
    memset(&huart5, 0, sizeof(huart5)); memset(&uart_registers, 0, sizeof(uart_registers));
    memset(&rx_dma, 0, sizeof(rx_dma)); memset(&tx_dma, 0, sizeof(tx_dma));
    memset(&test_rx_stream, 0, sizeof(test_rx_stream)); memset(&test_tx_stream, 0, sizeof(test_tx_stream));
    rx_dma.Instance = &test_rx_stream; tx_dma.Instance = &test_tx_stream;
    rx_dma.Parent = tx_dma.Parent = &huart5;
    rx_dma.Init.Mode = DMA_CIRCULAR; tx_dma.Init.Mode = DMA_NORMAL;
    rx_dma.Init.Channel = tx_dma.Init.Channel = DMA_CHANNEL_4;
    rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY; tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    huart5.Instance = &uart_registers; huart5.hdmarx = &rx_dma; huart5.hdmatx = &tx_dma;
    huart5.gState = huart5.RxState = HAL_UART_STATE_READY;
    timer_registers.PSC = 83U; timer_registers.ARR = 1749U; test_rcc.CFGR = 0x1000U;
    abort_async = abort_stuck = mailbox_accept = 0U;
    mailbox_calls = notifications = 0U;
    receive_result = abort_rx_result = abort_tx_result = HAL_OK;
    error_during_rx_abort = error_during_tx_abort = 0;
    receive_ok_unarmed = abort_rx_ok_active = abort_tx_ok_active = 0;
    abort_rx_calls = abort_tx_calls = clear_calls = 0;
    response_size = 0U; transmit_fail = false;
    app_system_snapshot_init(); app_hmi_init(); advance(3000);
}
static void recover_and_read(uint8_t *frame)
{
    bsp_hmi_uart_service();
    boundary(); app_hmi_service();
    request(frame, 0, 64); inject(frame, 8); flush();
    assert(s_tx_active); complete();
}

static void protocol_tests(void)
{
    uint8_t frame[300] = {0};
    unsigned index;
    const uint16_t starts[8] = {0, 0x50, 0x70, 0x90, 0xB0, 0x200, 0x120, 0x460};
    const uint16_t counts[8] = {64, 32, 32, 32, 32, 24, 20, 64};
    fresh();
    for (index = 0U; index < 8U; index++)
    {
        request(frame, starts[index], counts[index]); inject(frame, 8); app_hmi_service();
        assert(!s_tx_active); flush();
        assert(response_size == counts[index] * 2U + 5U);
        assert(hmi_modbus_crc16(response, response_size) == 0U);
        assert(s_stats.responses == index); complete();
    }
    assert(s_stats.requests == 8U && s_stats.responses == 8U && rx_armed());
    assert(s_diag.region_requests[6] == 2U);
    fresh(); request(frame, 0x3F, 2); inject(frame, 8); flush();
    assert(response[1] == 0x83); complete(); assert(s_stats.exceptions == 1U);
    fresh(); request(frame, 0, 0); inject(frame, 8); flush();
    assert(response[1] == 0x83 && !s_stats.requests); complete();
    fresh(); request(frame, 0, 126); inject(frame, 8); flush();
    assert(response[1] == 0x83); complete();
    fresh(); request(frame, 0, 1); frame[7] ^= 1U; inject(frame, 8); flush();
    assert(s_stats.crc_errors == 1U && !s_tx_active);
    fresh(); request(frame, 0, 1); frame[0] = 2; crc(frame, 6); inject(frame, 8); flush();
    assert(s_diag.address_ignored == 1U && !s_stats.requests);
    fresh(); request(frame, 0, 1); inject(frame, 4); flush();
    assert(s_stats.partial_timeouts == 1U && !s_tx_active);
    fresh(); memset(frame, 0xFF, sizeof(frame)); inject(frame, sizeof(frame)); flush();
    app_hmi_service(); assert(s_stats.frame_errors == 1U && !s_tx_active);
    fresh(); memset(frame, 0, 57); frame[0] = 1; frame[1] = 0x10;
    frame[2] = 1; frame[5] = 24; frame[6] = 48; crc(frame, 55);
    mailbox_accept = 1U; inject(frame, 57); flush();
    if (APP_HMI_CONTROL_ENABLED)
    {
        assert(response_size == 8U && response[1] == 0x10 && mailbox_calls == 1U);
        assert(mailbox_generation == s_rx_generation);
    }
    else assert(response_size == 5U && response[1] == 0x90 && mailbox_calls == 0U);
    assert(unsafe_calls == 0U); complete();
    puts("PASS DMA protocol: eight regions, CRC, lengths, 57-byte mailbox, 133-byte response, no ISR motion");
}

static void boundary_tests(void)
{
    uint8_t frame[8], noise[256] = {0};
    uint32_t saved;
    fresh(); request(frame, 0, 64);
    inject(frame, 3); idle(); advance(500U); inject(frame + 3, 5); idle();
    advance(1749U); assert(!s_stats.requests); advance(1U); bsp_hmi_uart_quiet_isr();
    app_hmi_service(); assert(s_stats.requests == 1U); complete();
    fresh(); request(frame, 0, 64); inject(frame, 4); idle();
    inject(frame + 4, 4); advance(1750U); bsp_hmi_uart_quiet_isr();
    assert(s_diag.boundaries == 0U); idle(); advance(1750U); bsp_hmi_uart_quiet_isr();
    app_hmi_service(); assert(s_stats.requests == 1U); complete();
    fresh(); request(frame, 0, 1); inject(frame, 8); boundary();
    inject(frame, 8); boundary(); assert(s_diag.boundaries == 2U);
    app_hmi_service(); assert(s_stats.requests == 2U && s_stats.tx_busy == 1U);
    assert(s_diag.tx_dropped == 1U); complete();
    fresh(); inject(noise, 256); saved = s_stats.rx_bytes;
    idle(); bsp_hmi_uart_rx_event_isr(256, HAL_UART_RXEVENT_HT);
    assert(saved == s_stats.rx_bytes); app_hmi_service();
    inject(noise, 248); idle(); app_hmi_service();
    request(frame, 0, 1); inject(frame, 8); idle();
    assert(s_stats.rx_bytes == 512U && s_diag.full_events == 1U);
    bsp_hmi_uart_rx_event_isr(512, HAL_UART_RXEVENT_TC);
    assert(s_stats.rx_bytes == 512U);
    fresh(); request(frame, 0, 64);
    inject(noise, 250); boundary(); app_hmi_service();
    inject(noise, 256); boundary(); app_hmi_service();
    inject(frame, 8); flush(); assert(s_stats.requests == 1U); complete();
    fresh(); request(frame, 0, 1); inject(frame, 8); boundary();
    saved = s_diag.boundaries; bsp_hmi_uart_quiet_isr(); assert(saved == s_diag.boundaries);
    bsp_hmi_uart_bind_task((void *)1); app_hmi_service(); complete(); assert(notifications > 0U);
    fresh(); inject(noise, 256); idle(); advance(1800U); bsp_hmi_uart_quiet_isr();
    assert(s_diag.boundaries == 1U);
    puts("PASS DMA boundaries: split IDLE, no false timer boundary, HT/TC duplicate, wrap, queued frames, task binding");
    fresh(); request(frame, 0, 64); inject(frame, 8); idle();
    advance(1749U); bsp_hmi_uart_quiet_isr();
    assert(s_diag.quiet_early_count == 1U && !s_diag.boundaries);
    assert(!s_quiet_pending && s_diag.quiet_elapsed_min_cycles > 0U);
    puts("PASS early quiet ISR evidence is counted without changing framing behavior");
}

static void diagnostic_sink_tests(void)
{
    uint8_t frame[57] = {1U, 0x10U, 7U, 0U, 0U, 24U, 48U};
    uint16_t crc;
    fresh();
    frame[7] = 0x48U; frame[8] = 0x44U;
    frame[9] = 1U; frame[14] = 1U; /* version/page and event=1 */
    crc = hmi_modbus_crc16(frame, 55U); frame[55] = (uint8_t)crc; frame[56] = (uint8_t)(crc >> 8U);
    inject(frame, sizeof(frame)); flush();
    assert(response_size == 8U && response[1] == 0x10U);
    assert(!mailbox_calls && !unsafe_calls); complete();
    frame[8] = 0U;
    crc = hmi_modbus_crc16(frame, 55U); frame[55] = (uint8_t)crc; frame[56] = (uint8_t)(crc >> 8U);
    inject(frame, sizeof(frame)); flush();
    assert(response_size == 5U && response[1] == 0x90U && response[2] == 3U);
    assert(!mailbox_calls && !unsafe_calls); complete();
    puts("PASS diagnostic address validates pages and bypasses motion even in read-only builds");
}

static void recovery_tests(void)
{
    uint8_t frame[8], data[256] = {0};
    uint32_t saved, index;
    fresh(); receive_result = HAL_ERROR; app_hmi_init();
    assert(s_rx_recovery.pending && s_diag.arm_failures == 1U);
    bsp_hmi_uart_service(); assert(s_rx_recovery.pending);
    saved = s_diag.rx_attempts; advance(9000); bsp_hmi_uart_service(); assert(saved == s_diag.rx_attempts);
    receive_result = HAL_OK; advance(1000); bsp_hmi_uart_service(); assert(rx_armed());
    recover_and_read(frame);
    fresh(); rx_dma.Init.Mode = DMA_NORMAL; bsp_hmi_uart_service(); assert(!rx_armed());
    rx_dma.Init.Mode = DMA_CIRCULAR; advance(10000); bsp_hmi_uart_service(); assert(rx_armed());
    fresh(); rx_dma.Init.Channel = 0U; bsp_hmi_uart_service(); assert(!rx_armed());
    fresh(); tx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY; bsp_hmi_uart_service(); assert(!rx_armed());
    fresh(); test_rx_stream.CR &= ~DMA_SxCR_EN; abort_async = 1U;
    bsp_hmi_uart_service(); assert(s_rx_recovery.pending == HMI_RECOVERY_ABORTING);
    abort_rx_done(&rx_dma); bsp_hmi_uart_service(); assert(rx_armed() && !s_rx_recovery.pending);
    fresh(); test_rx_stream.CR &= ~DMA_SxCR_EN; abort_async = 1U;
    bsp_hmi_uart_service(); advance(20000); bsp_hmi_uart_service();
    assert(s_diag.rx_abort_timeouts == 1U && rx_armed());
    saved = s_diag.dma_starts; bsp_hmi_uart_rx_abort_isr(); bsp_hmi_uart_service();
    assert(saved == s_diag.dma_starts);
    fresh(); abort_stuck = 1U; uart_registers.CR1 &= ~USART_CR1_IDLEIE;
    bsp_hmi_uart_service(); advance(20000); bsp_hmi_uart_service();
    assert(s_diag.rx_abort_timeouts == 1U && s_rx_recovery.pending && s_diag.dma_starts == 1U);
    test_rx_stream.CR &= ~DMA_SxCR_EN; abort_stuck = 0U; bsp_hmi_uart_service(); assert(rx_armed());
    fresh(); abort_rx_result = HAL_ERROR; uart_registers.CR1 &= ~USART_CR1_IDLEIE;
    bsp_hmi_uart_service(); saved = abort_rx_calls; advance(9000); bsp_hmi_uart_service();
    assert(saved == abort_rx_calls); abort_rx_result = HAL_OK; advance(1000); bsp_hmi_uart_service(); assert(rx_armed());
    fresh(); huart5.ErrorCode = HAL_UART_ERROR_ORE | HAL_UART_ERROR_FE;
    bsp_hmi_uart_error_isr(); bsp_hmi_uart_service(); assert(s_diag.ore == 1U && s_diag.fe == 1U);
    recover_and_read(frame);
    fresh(); request(frame, 0, 64); inject(frame, 8); flush(); assert(s_tx_active);
    huart5.ErrorCode = HAL_UART_ERROR_NE; bsp_hmi_uart_error_isr(); bsp_hmi_uart_service();
    assert(s_tx_active && abort_tx_calls == 0U); complete(); assert(s_stats.responses == 1U);
    fresh(); request(frame, 0, 64); inject(frame, 8); flush();
    saved = s_stats.responses; test_tx_stream.CR &= ~DMA_SxCR_EN;
    uart_registers.CR3 &= ~USART_CR3_DMAT; uart_registers.CR1 |= USART_CR1_TCIE;
    assert(s_tx_active && s_stats.responses == saved); complete(); assert(s_stats.responses == saved + 1U);
    fresh(); assert(bsp_hmi_uart_send(data, 133)); saved = s_diag.dma_starts;
    assert(!bsp_hmi_uart_send(frame, 8) && !memcmp(s_tx_buffer, data, 133));
    advance(32000); bsp_hmi_uart_service();
    assert(!s_tx_active && s_diag.tx_timeouts == 1U && s_diag.dma_starts == saved && !s_stats.responses);
    bsp_hmi_uart_tx_complete_isr(); assert(!s_stats.responses);
    fresh(); transmit_fail = true; assert(!bsp_hmi_uart_send(data, 8));
    transmit_fail = false; bsp_hmi_uart_service(); assert(!s_tx_active && rx_armed());
    fresh(); for (index = 0U; index < 5U; index++) inject(data, 256);
    assert(s_stats.rx_overflow == 1U && s_rx_recovery.pending); recover_and_read(frame);
    fresh(); for (index = 0U; index < 32U; index++) { inject(data, 1); idle(); }
    assert(s_diag.event_overflow == 1U && s_rx_recovery.pending); recover_and_read(frame);
    fresh(); saved = s_diag.dma_starts;
    for (index = 0U; index < 1000U; index++) { advance(2000); bsp_hmi_uart_service(); }
    assert(saved == s_diag.dma_starts && !s_diag.rx_attempts);
    fresh(); abort_async = 1U; huart5.ErrorCode = HAL_UART_ERROR_PE; bsp_hmi_uart_error_isr();
    bsp_hmi_uart_service(); saved = s_rx_recovery.epoch;
    bsp_hmi_uart_error_isr(); assert(s_rx_recovery.epoch > saved);
    abort_rx_done(&rx_dma); bsp_hmi_uart_service(); assert(s_rx_recovery.pending);
    abort_async = 0U; advance(10000); bsp_hmi_uart_service(); assert(rx_armed());
    puts("PASS DMA recovery: startup/config, asynchronous Abort, timeout fallback, stale callback, epochs, RX/TX isolation, overflow, idle");
}

/* Exercise independent source/maintenance/read phases; all reads copy one source. */
static uint32_t cadence_rejections(uint32_t producer_period, uint32_t producer_phase,
                                    uint32_t cache_phase, uint32_t origin)
{
    uint16_t values[96];
    uint32_t elapsed_ms, rejected = 0U;
    fresh();
    tick = origin; simulated_us = (uint64_t)tick * 1000U;
    app_system_snapshot_init(); app_hmi_snapshot_init(identity_id);
    for (elapsed_ms = 0U; elapsed_ms < 500U; elapsed_ms++)
    {
        tick = origin + elapsed_ms;
        simulated_us = ((uint64_t)origin + elapsed_ms) * 1000U;
        if (elapsed_ms % producer_period == producer_phase) app_system_snapshot_service();
        if (elapsed_ms % APP_HMI_SNAPSHOT_PERIOD_MS == cache_phase) app_hmi_snapshot_service();
        if (elapsed_ms > 150U && !app_hmi_snapshot_read(0x460U, 96U, values)) rejected++;
    }
    return rejected;
}
static void cadence_tests(void)
{
    uint32_t producer_phase, cache_phase, legacy_rejected = 0U;
    uint16_t values[96];
    for (producer_phase = 0U; producer_phase < APP_HEALTH_TASK_PERIOD_MS; producer_phase++)
        for (cache_phase = 0U; cache_phase < APP_HMI_SNAPSHOT_PERIOD_MS; cache_phase++)
        {
            assert(cadence_rejections(APP_HEALTH_TASK_PERIOD_MS, producer_phase, cache_phase, 1000U) == 0U);
            assert(cadence_rejections(APP_HEALTH_TASK_PERIOD_MS, producer_phase, cache_phase, UINT32_MAX - 200U) == 0U);
        }
    /* A producer at the 100ms validity boundary remains readable while current. */
    for (producer_phase = 0U; producer_phase < APP_HEALTH_PERIOD_MS; producer_phase += 10U)
        legacy_rejected += cadence_rejections(APP_HEALTH_PERIOD_MS, producer_phase, 0U, 1000U);
    assert(legacy_rejected == 0U);
    /* A real stopped producer still expires, including repeated window reads. */
    fresh(); tick += APP_SYSTEM_SNAPSHOT_MAX_AGE_MS + 1U;
    assert(!app_hmi_snapshot_read(0x460U, 96U, values));
    app_hmi_snapshot_service();
    assert(!app_hmi_snapshot_read(0x460U, 96U, values));
    assert(unsafe_calls == 0U);
    printf("PASS production cadence: 500 phase pairs x normal/wrap, zero rejected reads; legacy rejected=%lu; stopped producer rejected\n", (unsigned long)legacy_rejected);
}

int main(void)
{
    uint8_t frame[8];
    uint16_t values[64];
    uint32_t index;
    protocol_tests(); boundary_tests(); diagnostic_sink_tests(); recovery_tests(); cadence_tests();
    fresh(); startup_ready = false; app_system_snapshot_init(); app_hmi_snapshot_init(identity_id);
    assert(app_hmi_snapshot_read(0, 32, values) && !values[15]);
    request(frame, 0x200, 24); inject(frame, 8); flush(); assert(s_stats.requests == 1U); complete();
    startup_ready = true;
    SystemCoreClock = 168000000U; fresh();
    for (index = 0U; index < 600U; index++)
    {
        request(frame, index % 2U ? 0x200 : 0x460, index % 2U ? 24 : 64);
        inject(frame, 8); flush(); complete(); advance(190000U); bsp_hmi_uart_service(); app_hmi_service();
    }
    assert(s_stats.requests == 600U && s_stats.responses == 600U && !s_stats.rx_overflow);
    assert(!s_diag.rx_attempts && !s_diag.tx_attempts);
    assert(app_hmi_cli_command("hmi status") == 1U);
    assert(app_hmi_cli_command("invalid") == 0U && unsafe_calls == 0U);
    simulated_us = (uint64_t)UINT32_MAX * 1000U; advance(900U); fresh();
    request(frame, 0, 64); inject(frame, 8); flush(); complete();
    assert(s_stats.responses == 1U);
    puts("PASS DMA integration: no servo, 168MHz 120s, Tick/CYCCNT wrap, immutable protocol");
    return 0;
}

void aimtor_get_recovery_status(servo_recovery_status_t *out) { memset(out, 0, sizeof(*out)); }
