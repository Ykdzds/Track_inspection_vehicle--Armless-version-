#include "can.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "can_upper_config.h"

#define CAN_TX_QUEUE_LENGTH 8U
#define CAN_TX_RESULT_LENGTH 8U
#define CAN_NOTIFICATIONS (CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN | CAN_IT_TX_MAILBOX_EMPTY | CAN_IT_ERROR | CAN_IT_BUSOFF | CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE | CAN_IT_LAST_ERROR_CODE)

typedef CanBusTxResult_t CanTxResult_t;

static StaticQueue_t rx_control, tx_control, result_control;
static uint8_t rx_storage[CAN_BUS_RX_QUEUE_LENGTH * sizeof(CanBusFrame_t)];
static uint8_t tx_storage[CAN_TX_QUEUE_LENGTH * sizeof(CanBusFrame_t)];
static uint8_t result_storage[CAN_TX_RESULT_LENGTH * sizeof(CanTxResult_t)];
static QueueHandle_t rx_queue, tx_queue, result_queue;
static TaskHandle_t rx_task;
static volatile CanBusStats_t bus_stats;
static volatile uint8_t started, bus_off, in_flight, completed;
static volatile uint8_t aborting;
static uint8_t abort_reason, fatal_result_pending;
static uint32_t abort_at, generation;
static CanTxResult_t fatal_result;
static CanBusFrame_t flight_frame;
static uint32_t flight_mailbox, recovery_at, recovery_delay = 100U;
static uint8_t recovery_pending;
static uint8_t flight_retries;
static uint8_t retry_pending;
static CanBusFrame_t latest_speed;
static volatile uint8_t speed_pending;
static CanBusFrame_t urgent_frame;
static uint8_t urgent_pending;

static void PublishResult(const CanBusFrame_t *frame, UpperTxOutcome_t outcome, uint32_t now)
{
    CanTxResult_t result;
    result.frame = *frame;
    result.generation = generation;
    result.mailbox = in_flight ? flight_mailbox : 0U;
    result.age = now - frame->timestamp;
    result.tsr = hcan.Instance->TSR;
    result.esr = hcan.Instance->ESR;
    result.outcome = outcome;
    if (outcome == UPPER_TX_OK) bus_stats.tx_frames++;
    else if (outcome == UPPER_TX_CANCELLED) bus_stats.tx_cancelled++;
    else if (outcome == UPPER_TX_REPLACED) bus_stats.tx_replaced++;
    else
    {
        bus_stats.tx_errors++;
        if (outcome == UPPER_TX_QUEUE_EXPIRED) bus_stats.tx_expired++;
        if (outcome == UPPER_TX_TIMEOUT) bus_stats.tx_timeouts++;
    }
    if (xQueueSend(result_queue, &result, 0U) != pdPASS)
    {
        bus_stats.result_overruns++;
        if (!fatal_result_pending)
        {
            fatal_result = result;
            fatal_result.outcome = UPPER_TX_RESULT_LOST;
            fatal_result_pending = 1U;
        }
        started = 0U;
    }
}

static void ClearTerminal(void)
{
    if (flight_mailbox == CAN_TX_MAILBOX0) __HAL_CAN_CLEAR_FLAG(&hcan, CAN_FLAG_RQCP0);
    else if (flight_mailbox == CAN_TX_MAILBOX1) __HAL_CAN_CLEAR_FLAG(&hcan, CAN_FLAG_RQCP1);
    else if (flight_mailbox == CAN_TX_MAILBOX2) __HAL_CAN_CLEAR_FLAG(&hcan, CAN_FLAG_RQCP2);
}

static void AbortFlight(UpperTxOutcome_t reason, uint32_t now)
{
    if (in_flight && !aborting)
    {
        aborting = 1U;
        abort_at = now;
        abort_reason = (uint8_t)reason;
        (void)HAL_CAN_AbortTxRequest(&hcan, flight_mailbox);
    }
}

static uint8_t TxAllowed(uint16_t id)
{
    return (uint8_t)((id == 0x100U) || (id == 0x102U) || (id == 0x104U) ||
                     (id == 0x105U) || (id == 0x110U) || (id == 0x111U) || (id == 0x080U) || (id == 0x310U) || (id == 0x311U) || (id == 0x312U));
}

static void NotifyRx(BaseType_t *woken)
{
    if (rx_task != NULL)
    {
        vTaskNotifyGiveFromISR(rx_task, woken);
    }
}

HAL_StatusTypeDef CanBus_Start(void)
{
    static const uint16_t ids[16] = {0x101U, 0x106U, 0x120U, 0x121U,
        0x180U, 0x181U, 0x182U, 0x183U, 0x184U, 0x185U, 0x186U, 0x187U,
        0x188U, 0x300U, 0x301U, 0x302U};
    CAN_FilterTypeDef filter = {0};
    uint32_t bank;
    if (rx_queue == NULL)
    {
        rx_queue = xQueueCreateStatic(CAN_BUS_RX_QUEUE_LENGTH, sizeof(CanBusFrame_t), rx_storage, &rx_control);
        tx_queue = xQueueCreateStatic(CAN_TX_QUEUE_LENGTH, sizeof(CanBusFrame_t), tx_storage, &tx_control);
        result_queue = xQueueCreateStatic(CAN_TX_RESULT_LENGTH, sizeof(CanTxResult_t), result_storage, &result_control);
    }
    filter.FilterMode = CAN_FILTERMODE_IDLIST;
    filter.FilterScale = CAN_FILTERSCALE_16BIT;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;
    for (bank = 0U; bank < 4U; bank++)
    {
        filter.FilterBank = bank;
        filter.FilterIdHigh = ids[bank * 4U] << 5;
        filter.FilterIdLow = ids[bank * 4U + 1U] << 5;
        filter.FilterMaskIdHigh = ids[bank * 4U + 2U] << 5;
        filter.FilterMaskIdLow = ids[bank * 4U + 3U] << 5;
        if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK) return HAL_ERROR;
    }
    if (HAL_CAN_Start(&hcan) != HAL_OK) return HAL_ERROR;
    if (HAL_CAN_ActivateNotification(&hcan, CAN_NOTIFICATIONS) != HAL_OK) return HAL_ERROR;
    started = 1U;
    bus_off = 0U;
    recovery_pending = 0U;
    flight_retries = 0U;
    retry_pending = 0U;
    return HAL_OK;
}

void CanBus_SetRxTask(void *task)
{
    rx_task = (TaskHandle_t)task;
}

HAL_StatusTypeDef CanBus_Send(uint16_t id, const uint8_t data[8])
{
    CanBusFrame_t frame;
    if ((started == 0U) || (bus_off != 0U) || (data == NULL) || !TxAllowed(id))
    {
        bus_stats.tx_dropped++;
        return HAL_ERROR;
    }
    frame.id = id;
    frame.dlc = 8U;
    frame.timestamp = HAL_GetTick();
    memcpy(frame.data, data, 8U);
    if (id == 0x111U)
    {
        taskENTER_CRITICAL();
        if (speed_pending) PublishResult(&latest_speed, UPPER_TX_REPLACED, frame.timestamp);
        latest_speed = frame; speed_pending = 1U;
        taskEXIT_CRITICAL();
        return HAL_OK;
    }
    if (id == 0x080U || (id == 0x110U && (data[1] == 5U || (data[1] == 4U && data[2] == 0U))))
    {
        CanBusFrame_t queued;
        unsigned remaining = CAN_TX_QUEUE_LENGTH;
        CanBus_DropMotion();
        while (remaining-- && xQueueReceive(tx_queue, &queued, 0U) == pdPASS)
        {
            if (queued.id == 0x110U) PublishResult(&queued, UPPER_TX_CANCELLED, frame.timestamp);
            else (void)xQueueSend(tx_queue, &queued, 0U);
        }
        if (in_flight && flight_frame.id == 0x110U) AbortFlight(UPPER_TX_CANCELLED, frame.timestamp);
        if (urgent_pending && urgent_frame.id == 0x080U && urgent_frame.data[4] == 1U &&
            (id != 0x080U || data[4] != 1U)) return HAL_OK;
        urgent_frame = frame; urgent_pending = 1U;
        return HAL_OK;
    }
    if (((id < 0x300U) ? xQueueSendToFront(tx_queue, &frame, 0U) : xQueueSend(tx_queue, &frame, 0U)) != pdPASS)
    {
        bus_stats.tx_dropped++;
        return HAL_BUSY;
    }
    return HAL_OK;
}

uint8_t CanBus_Receive(CanBusFrame_t *frame)
{
    return (uint8_t)((rx_queue != NULL) && (xQueueReceive(rx_queue, frame, 0U) == pdPASS));
}

void CanBus_GetStats(CanBusStats_t *stats)
{
    taskENTER_CRITICAL();
    memcpy(stats, (const void *)&bus_stats, sizeof(*stats));
    taskEXIT_CRITICAL();
}

uint8_t CanBus_IsStarted(void) { return started; }
uint8_t CanBus_IsBusOff(void) { return bus_off; }

void CanBus_ClearPending(void)
{
    taskENTER_CRITICAL();
    AbortFlight(UPPER_TX_CANCELLED, HAL_GetTick());
    speed_pending = 0U;
    retry_pending = 0U;
    urgent_pending = 0U;
    if (rx_queue != NULL) xQueueReset(rx_queue);
    if (tx_queue != NULL) xQueueReset(tx_queue);
    taskEXIT_CRITICAL();
}

void CanBus_DropMotion(void)
{
    taskENTER_CRITICAL();
    if (speed_pending) PublishResult(&latest_speed, UPPER_TX_CANCELLED, HAL_GetTick());
    speed_pending = 0U;
    retry_pending = 0U;
    if (in_flight && flight_frame.id == 0x111U)
    {
        AbortFlight(UPPER_TX_CANCELLED, HAL_GetTick());
    }
    taskEXIT_CRITICAL();
}

uint8_t CanBus_GetTxResult(CanBusFrame_t *frame, uint8_t *success)
{
    CanTxResult_t result;
    if ((result_queue == NULL) || (xQueueReceive(result_queue, &result, 0U) != pdPASS)) return 0U;
    *frame = result.frame;
    *success = (uint8_t)(result.outcome == UPPER_TX_OK);
    return 1U;
}

uint8_t CanBus_GetTxResultEx(CanBusTxResult_t *result)
{
    if (result_queue != NULL && xQueueReceive(result_queue, result, 0U) == pdPASS) return 1U;
    if (!fatal_result_pending) return 0U;
    *result = fatal_result;
    fatal_result_pending = 0U;
    return 1U;
}

void CanBus_Service(uint32_t now)
{
    CAN_TxHeaderTypeDef header = {0};
    CanBusFrame_t frame;
    uint8_t done, from_speed = 0U, from_urgent = 0U, from_retry = 0U;
    /* Re-sample after producers and ISR activity; preserve injected forward time. */
    { uint32_t sampled = HAL_GetTick(); if ((int32_t)(sampled - now) > 0) now = sampled; }
    if ((started == 0U) || (bus_off != 0U))
    {
        if (recovery_pending == 0U)
        {
            if (in_flight)
            {
                PublishResult(&flight_frame, bus_off ? UPPER_TX_BUS_OFF : UPPER_TX_ABORT_TIMEOUT, now);
            }
            CanBus_ClearPending();
            HAL_CAN_DeInit(&hcan);
            in_flight = completed = aborting = 0U;
            bus_stats.recovery_count++;
            started = 0U;
            recovery_at = now + recovery_delay;
            recovery_pending = 1U;
        }
        if ((int32_t)(now - recovery_at) >= 0)
        {
            if (HAL_CAN_Init(&hcan) == HAL_OK && CanBus_Start() == HAL_OK)
            {
                recovery_delay = 100U;
            }
            else
            {
                recovery_delay = (recovery_delay < 500U) ? recovery_delay * 2U : 1000U;
                recovery_at = now + recovery_delay;
                recovery_pending = 1U;
                bus_stats.hal_errors++;
            }
        }
        return;
    }
    if (in_flight != 0U)
    {
        taskENTER_CRITICAL();
        done = completed;
        if (done == 0U && (int32_t)(now - flight_frame.timestamp) >= 100)
        {
            AbortFlight(UPPER_TX_TIMEOUT, now);
        }
        if (done == 0U && aborting && (int32_t)(now - abort_at) >= 20)
        {
            bus_stats.abort_timeouts++;
            started = 0U;
        }
        if (done == 0U) { taskEXIT_CRITICAL(); return; }
        if (HAL_CAN_IsTxMessagePending(&hcan, flight_mailbox)) { taskEXIT_CRITICAL(); return; }
        if (done != 1U && !aborting && flight_frame.id == 0x111U && flight_retries < UPPER_TX_RETRY_LIMIT &&
            (uint32_t)(now - flight_frame.timestamp) < 100U && started && !bus_off)
        {
            ++flight_retries;
            ++bus_stats.tx_retried;
            retry_pending = 1U;
            ClearTerminal();
            completed = 0U;
            in_flight = 0U;
            taskEXIT_CRITICAL();
            return;
        }
        PublishResult(&flight_frame, done == 1U ? UPPER_TX_OK :
            (done == 3U && aborting ? (UpperTxOutcome_t)abort_reason : UPPER_TX_HARDWARE), now);
        ClearTerminal();
        completed = aborting = 0U;
        in_flight = 0U;
        taskEXIT_CRITICAL();
        if (!started) return;
        if (done == 2U || (done == 3U && abort_reason != UPPER_TX_CANCELLED)) return;
    }
    if (urgent_pending) { frame = urgent_frame; from_urgent = 1U; }
    else if (retry_pending) { frame = flight_frame; from_retry = 1U; }
    else if (xQueuePeek(tx_queue, &frame, 0U) != pdPASS || frame.id >= 0x300U)
    {
        taskENTER_CRITICAL();
        if (speed_pending) { frame = latest_speed; from_speed = 1U; }
        taskEXIT_CRITICAL();
        if (!from_speed && xQueuePeek(tx_queue, &frame, 0U) != pdPASS) return;
    }
    { uint32_t sampled = HAL_GetTick(); if ((int32_t)(sampled - now) > 0) now = sampled; }
    if ((int32_t)(now - frame.timestamp) >= 100)
    {
        if (from_retry) retry_pending = 0U;
        else if (from_urgent) urgent_pending = 0U;
        else if (from_speed) speed_pending = 0U;
        else xQueueReceive(tx_queue, &frame, 0U);
        bus_stats.tx_dropped++;
        PublishResult(&frame, UPPER_TX_QUEUE_EXPIRED, now);
        return;
    }
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) != 3U)
    {
        bus_stats.mailbox_full++;
        return;
    }
    header.StdId = frame.id;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = 8U;
    taskENTER_CRITICAL();
    completed = 0U;
    (void)HAL_CAN_ResetError(&hcan);
    if (HAL_CAN_AddTxMessage(&hcan, &header, frame.data, &flight_mailbox) == HAL_OK)
    {
        if (!retry_pending) flight_retries = 0U;
        flight_frame = frame;
        retry_pending = 0U;
        generation++;
        in_flight = 1U;
        if (from_retry) retry_pending = 0U;
        else if (from_urgent) urgent_pending = 0U;
        else if (from_speed) speed_pending = 0U;
        else xQueueReceive(tx_queue, &frame, 0U);
    }
    else bus_stats.tx_errors++;
    taskEXIT_CRITICAL();
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *handle)
{
    CAN_RxHeaderTypeDef header;
    CanBusFrame_t frame;
    BaseType_t woken = pdFALSE;
    uint8_t count;
    if (handle != &hcan || rx_queue == NULL) return;
    for (count = 0U; count < 3U && HAL_CAN_GetRxFifoFillLevel(handle, CAN_RX_FIFO0); count++)
    {
        if (HAL_CAN_GetRxMessage(handle, CAN_RX_FIFO0, &header, frame.data) != HAL_OK) break;
        bus_stats.rx_frames++;
        if (header.IDE != CAN_ID_STD || header.RTR != CAN_RTR_DATA || header.DLC != 8U)
        {
            bus_stats.invalid_frames++;
            continue;
        }
        frame.id = (uint16_t)header.StdId;
        frame.dlc = 8U;
        frame.timestamp = HAL_GetTick();
        if (xQueueSendFromISR(rx_queue, &frame, &woken) != pdPASS) bus_stats.rx_queue_overruns++;
    }
    NotifyRx(&woken);
    portYIELD_FROM_ISR(woken);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *handle)
{
    BaseType_t woken = pdFALSE;
    uint32_t error;
    if (handle != &hcan) return;
    error = HAL_CAN_GetError(handle);
    if (in_flight && !completed && !HAL_CAN_IsTxMessagePending(handle, flight_mailbox))
    {
        uint32_t terminal_errors = flight_mailbox == CAN_TX_MAILBOX0 ? (HAL_CAN_ERROR_TX_ALST0 | HAL_CAN_ERROR_TX_TERR0) :
            (flight_mailbox == CAN_TX_MAILBOX1 ? (HAL_CAN_ERROR_TX_ALST1 | HAL_CAN_ERROR_TX_TERR1) : (HAL_CAN_ERROR_TX_ALST2 | HAL_CAN_ERROR_TX_TERR2));
        if (error & terminal_errors) completed = 2U;
    }
    bus_stats.last_error = error;
    bus_stats.hal_errors++;
    if ((error & HAL_CAN_ERROR_RX_FOV0) != 0U) bus_stats.rx_queue_overruns++;
    if ((error & HAL_CAN_ERROR_BOF) != 0U && bus_off == 0U)
    {
        bus_off = 1U;
        bus_stats.bus_off_count++;
    }
    NotifyRx(&woken);
    portYIELD_FROM_ISR(woken);
}

static void CompleteMailbox(CAN_HandleTypeDef *handle, uint32_t mailbox, uint8_t terminal)
{
    BaseType_t woken = pdFALSE;
    if (handle == &hcan && in_flight && flight_mailbox == mailbox && !completed &&
        !HAL_CAN_IsTxMessagePending(handle, mailbox))
    {
        completed = terminal;
        NotifyRx(&woken);
        portYIELD_FROM_ISR(woken);
    }
}
void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *handle) { CompleteMailbox(handle, CAN_TX_MAILBOX0, 1U); }
void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *handle) { CompleteMailbox(handle, CAN_TX_MAILBOX1, 1U); }
void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *handle) { CompleteMailbox(handle, CAN_TX_MAILBOX2, 1U); }
void HAL_CAN_TxMailbox0AbortCallback(CAN_HandleTypeDef *handle) { CompleteMailbox(handle, CAN_TX_MAILBOX0, 3U); }
void HAL_CAN_TxMailbox1AbortCallback(CAN_HandleTypeDef *handle) { CompleteMailbox(handle, CAN_TX_MAILBOX1, 3U); }
void HAL_CAN_TxMailbox2AbortCallback(CAN_HandleTypeDef *handle) { CompleteMailbox(handle, CAN_TX_MAILBOX2, 3U); }
