#include <assert.h>
#include <stdio.h>
#include "bus_mocks/main.h"
#define __MAIN_H
#include "../APP/can_bus.c"
static CAN_TypeDef registers;
CAN_HandleTypeDef hcan = { &registers, 0U };
static uint32_t clock_ms, hardware_pending;
static unsigned submitted, aborted;
#ifndef F103_BUS_BRIDGE
uint32_t HAL_GetTick(void) { return clock_ms; }
#endif
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *handle, CAN_FilterTypeDef *filter) { (void)handle; (void)filter; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *handle) { (void)handle; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *handle, uint32_t flags) { (void)handle; (void)flags; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *handle) { (void)handle; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_DeInit(CAN_HandleTypeDef *handle) { (void)handle; hardware_pending = 0U; registers.TSR = 0U; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ResetError(CAN_HandleTypeDef *handle) { handle->ErrorCode = 0U; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *handle, uint32_t mailbox) { (void)handle; (void)mailbox; ++aborted; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *handle, CAN_TxHeaderTypeDef *header, uint8_t *data, uint32_t *mailbox)
{ (void)handle; (void)header; (void)data; *mailbox = 1U; hardware_pending = 1U; ++submitted; return HAL_OK; }
uint32_t HAL_CAN_IsTxMessagePending(CAN_HandleTypeDef *handle, uint32_t mailbox) { (void)handle; return hardware_pending & mailbox; }
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *handle) { (void)handle; return hardware_pending ? 2U : 3U; }
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *handle, uint32_t fifo) { (void)handle; (void)fifo; return 0U; }
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *handle, uint32_t fifo, CAN_RxHeaderTypeDef *header, uint8_t *data)
{ (void)handle; (void)fifo; (void)header; (void)data; return HAL_ERROR; }
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *handle) { return handle->ErrorCode; }
QueueHandle_t xQueueCreateStatic(unsigned length, unsigned size, uint8_t *storage, StaticQueue_t *queue)
{ queue->count = queue->head = 0U; queue->size = size; queue->length = length; queue->storage = storage; return queue; }
int xQueueSend(QueueHandle_t queue, const void *message, unsigned wait)
{ unsigned tail; (void)wait; if (queue->count == queue->length) return 0; tail = (queue->head + queue->count) % queue->length; memcpy(queue->storage + tail * queue->size, message, queue->size); ++queue->count; return pdPASS; }
int xQueueSendToFront(QueueHandle_t queue, const void *message, unsigned wait)
{ (void)wait; if (queue->count == queue->length) return 0; queue->head = (queue->head + queue->length - 1U) % queue->length; memcpy(queue->storage + queue->head * queue->size, message, queue->size); ++queue->count; return pdPASS; }
int xQueueSendFromISR(QueueHandle_t queue, const void *message, BaseType_t *woken) { (void)woken; return xQueueSend(queue, message, 0U); }
int xQueuePeek(QueueHandle_t queue, void *message, unsigned wait) { (void)wait; if (!queue->count) return 0; memcpy(message, queue->storage + queue->head * queue->size, queue->size); return pdPASS; }
int xQueueReceive(QueueHandle_t queue, void *message, unsigned wait) { if (!xQueuePeek(queue, message, wait)) return 0; queue->head = (queue->head + 1U) % queue->length; --queue->count; return pdPASS; }
void xQueueReset(QueueHandle_t queue) { queue->count = queue->head = 0U; }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken) { (void)task; (void)woken; }
static void ResetBus(void)
{
    rx_queue = tx_queue = result_queue = NULL;
    memset((void *)&bus_stats, 0, sizeof(bus_stats));
    memset(&registers, 0, sizeof(registers));
    started = bus_off = in_flight = completed = aborting = speed_pending = urgent_pending = 0U;
    fatal_result_pending = recovery_pending = 0U;
    generation = clock_ms = hardware_pending = submitted = aborted = 0U;
    recovery_delay = 100U; hcan.ErrorCode = 0U;
    assert(CanBus_Start() == HAL_OK);
}
#ifndef F103_BUS_BRIDGE
static void Complete(uint8_t success)
{
    hardware_pending = 0U;
    if (success) HAL_CAN_TxMailbox0CompleteCallback(&hcan);
    else HAL_CAN_TxMailbox0AbortCallback(&hcan);
    CanBus_Service(clock_ms);
}
static CanBusTxResult_t Result(UpperTxOutcome_t expected)
{
    CanBusTxResult_t result;
    assert(CanBus_GetTxResultEx(&result));
    assert(result.outcome == expected);
    return result;
}
int main(void)
{
    uint8_t data[8] = {1U, 0U, 0U, 0U, 9U, 0U, 44U, 1U}, success;
    CanBusFrame_t frame;
    CanBusTxResult_t result;
    unsigned index;
    ResetBus();
    clock_ms = 101U;
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(100U);
    assert(submitted == 1U && !bus_stats.tx_expired);
    Complete(1U); result = Result(UPPER_TX_OK); assert(result.age == 0U);
    ResetBus(); clock_ms = 0xFFFFFFF0U;
    assert(CanBus_Send(0x111U, data) == HAL_OK); clock_ms = 20U; CanBus_Service(clock_ms);
    assert(submitted == 1U && !aborting); Complete(1U); Result(UPPER_TX_OK);
    puts("PASS old service sample 100/new frame 101 and normal 32-bit wrap");
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U);
    hardware_pending = 0U; clock_ms = 1U; CanBus_Service(clock_ms);
    assert(!CanBus_GetTxResult(&frame, &success));
    HAL_CAN_TxMailbox0CompleteCallback(&hcan); CanBus_Service(clock_ms);
    assert(CanBus_GetTxResult(&frame, &success) && success);
    puts("PASS delayed completion IRQ does not create a false TX failure");
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U);
    HAL_CAN_TxMailbox1CompleteCallback(&hcan); CanBus_Service(1U);
    assert(!CanBus_GetTxResultEx(&result));
    hardware_pending = 0U; hcan.ErrorCode = HAL_CAN_ERROR_TX_TERR0; HAL_CAN_ErrorCallback(&hcan);
    clock_ms = 70U; CanBus_Service(clock_ms); assert(!in_flight && retry_pending && bus_stats.tx_retried == 1U);
    CanBus_Service(71U); assert(in_flight && submitted == 2U);
    Complete(1U); Result(UPPER_TX_OK);
    puts("PASS wrong mailbox rejected; transient transmission error retried before original deadline");
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U);
    hardware_pending = 0U; HAL_CAN_TxMailbox0AbortCallback(&hcan); CanBus_Service(1U);
    CanBus_Service(2U); hardware_pending = 0U; HAL_CAN_TxMailbox0AbortCallback(&hcan); CanBus_Service(3U);
    CanBus_Service(4U); hardware_pending = 0U; HAL_CAN_TxMailbox0AbortCallback(&hcan); CanBus_Service(5U);
    Result(UPPER_TX_HARDWARE);
    puts("PASS repeated target failures exhaust bounded retries and report final failure");
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U);
    clock_ms = 99U; CanBus_Service(clock_ms); assert(aborted == 0U);
    clock_ms = 100U; CanBus_Service(clock_ms); assert(aborted == 1U && aborting);
    assert(!CanBus_GetTxResultEx(&result));
    assert(CanBus_Send(0x100U, data) == HAL_OK); CanBus_Service(110U); assert(submitted == 1U);
    clock_ms = 111U; Complete(0U); result = Result(UPPER_TX_TIMEOUT); assert(result.age == 111U);
    assert(submitted == 1U); CanBus_Service(clock_ms); assert(submitted == 2U); Complete(1U); Result(UPPER_TX_OK);
    puts("PASS no ACK: original 100ms deadline, wait abort terminal before mailbox reuse");
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U);
    clock_ms = 100U; CanBus_Service(clock_ms); Complete(1U); Result(UPPER_TX_OK);
    puts("PASS completion wins abort race only on actual hardware success");
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); clock_ms = 100U; CanBus_Service(clock_ms);
    Result(UPPER_TX_QUEUE_EXPIRED); assert(submitted == 0U);
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); clock_ms = 90U; CanBus_Service(clock_ms);
    clock_ms = 100U; CanBus_Service(clock_ms); assert(aborted == 1U);
    Complete(0U); Result(UPPER_TX_TIMEOUT);
    puts("PASS queue age included in deadline; entering mailbox never renews frame lease");
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); data[0]++;
    assert(CanBus_Send(0x111U, data) == HAL_OK); Result(UPPER_TX_REPLACED);
    CanBus_Service(0U); CanBus_DropMotion(); assert(aborting);
    Complete(0U); Result(UPPER_TX_CANCELLED);
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U);
    CanBus_ClearPending(); assert(in_flight && aborting);
    data[4] = 10U; assert(CanBus_Send(0x111U, data) == HAL_OK);
    CanBus_Service(1U); assert(submitted == 1U);
    Complete(0U); Result(UPPER_TX_CANCELLED); assert(submitted == 2U);
    HAL_CAN_TxMailbox0AbortCallback(&hcan); assert(!completed);
    Complete(1U); result = Result(UPPER_TX_OK); assert(result.frame.data[4] == 10U && result.generation == 2U);
    puts("PASS cancellation/session barrier: no old terminal consumed by new pending transaction");
    ResetBus();
    assert(CanBus_Send(0x311U, data) == HAL_OK);
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U); assert(flight_frame.id == 0x111U);
    data[1] = 5U; assert(CanBus_Send(0x110U, data) == HAL_OK); assert(aborting);
    Complete(0U); Result(UPPER_TX_CANCELLED); assert(flight_frame.id == 0x110U);
    Complete(1U); Result(UPPER_TX_OK); assert(flight_frame.id == 0x311U);
    puts("PASS stop/revoke precedes capability fragments and aborts in-flight target");
    ResetBus();
    data[1] = 4U; data[2] = 1U; assert(CanBus_Send(0x110U, data) == HAL_OK);
    data[1] = 0U; data[4] = 1U; assert(CanBus_Send(0x080U, data) == HAL_OK);
    Result(UPPER_TX_CANCELLED); CanBus_Service(0U);
    assert(flight_frame.id == 0x080U && tx_queue->count == 0U);
    puts("PASS global quick stop purges queued authorization before it can grant late");
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U);
    hardware_pending = 0U; hcan.ErrorCode = HAL_CAN_ERROR_TX_ALST0; HAL_CAN_ErrorCallback(&hcan);
    CanBus_Service(1U); assert(retry_pending && !in_flight);
    CanBus_Service(2U); hardware_pending = 0U; HAL_CAN_TxMailbox0AbortCallback(&hcan); CanBus_Service(3U);
    CanBus_Service(4U); hardware_pending = 0U; HAL_CAN_TxMailbox0AbortCallback(&hcan); CanBus_Service(5U);
    Result(UPPER_TX_HARDWARE);
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U);
    hcan.ErrorCode = HAL_CAN_ERROR_BOF; HAL_CAN_ErrorCallback(&hcan); CanBus_Service(1U);
    Result(UPPER_TX_BUS_OFF); assert(!CanBus_IsStarted());
    puts("PASS final arbitration failure and Bus-off never reported as success");
    ResetBus();
    assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U);
    clock_ms = 100U; CanBus_Service(clock_ms); clock_ms = 120U; CanBus_Service(clock_ms);
    assert(!CanBus_IsStarted() && submitted == 1U); CanBus_Service(121U);
    Result(UPPER_TX_ABORT_TIMEOUT); assert(bus_stats.abort_timeouts == 1U);
    puts("PASS abort cleanup 20ms timeout stops link; mailbox never forcibly reused");
    ResetBus();
    for (index = 0U; index <= CAN_TX_RESULT_LENGTH; ++index)
    {
        assert(CanBus_Send(0x111U, data) == HAL_OK); CanBus_Service(0U); Complete(1U);
    }
    assert(bus_stats.result_overruns == 1U && !started);
    for (index = 0U; index < CAN_TX_RESULT_LENGTH; ++index) Result(UPPER_TX_OK);
    Result(UPPER_TX_RESULT_LOST);
    puts("PASS result overflow sticky diagnostic and fail-closed recovery");
    return 0;
}
#else
static uint8_t bridge_fail_target, bridge_failure_active;
void BusBridge_FailTarget(void) { bridge_fail_target = 1U; }
void BusBridge_DropMotion(void) { CanBus_DropMotion(); }
void BusBridge_Init(void) { ResetBus(); }
int BusBridge_Send(uint16_t id, const uint8_t *data) { return CanBus_Send(id, data) == HAL_OK; }
void BusBridge_Service(uint32_t now, void (*deliver)(uint16_t, const uint8_t *),
    void (*outcome)(uint16_t, const uint8_t *, unsigned))
{
    static uint32_t delivered_generation, due;
    CanBusTxResult_t result;
    clock_ms = now;
    if (!generation) { delivered_generation = due = 0U; bridge_fail_target = bridge_failure_active = 0U; }
    if (in_flight && hardware_pending && generation != delivered_generation)
    {
        delivered_generation = generation;
        if (bridge_fail_target && flight_frame.id == 0x111U)
        { bridge_fail_target = 0U; bridge_failure_active = 1U; }
        else { deliver(flight_frame.id, flight_frame.data); hardware_pending = 0U; }
        due = now + (generation % 7U == 0U ? 2U : 0U);
    }
    if (bridge_failure_active && aborting)
    { bridge_failure_active = 0U; hardware_pending = 0U; }
    if (in_flight && !hardware_pending && (int32_t)(now - due) >= 0)
    {
        if (aborting) HAL_CAN_TxMailbox0AbortCallback(&hcan);
        else HAL_CAN_TxMailbox0CompleteCallback(&hcan);
    }
    CanBus_Service(now);
    while (CanBus_GetTxResultEx(&result)) outcome(result.frame.id, result.frame.data, result.outcome);
}
#endif
