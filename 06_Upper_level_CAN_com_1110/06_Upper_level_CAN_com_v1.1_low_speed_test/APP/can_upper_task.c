#include "can_upper_task.h"
#include "can_upper_protocol.h"
#include "can_upper_boot.h"
#include "can_upper_control.h"
#include "can_upper_ui.h"
#include "can.h"
#if UPPER_USART1_DEBUG_ENABLE
#include "usart.h"
#endif
#include "i2c.h"
#include "iwdg.h"
#include "OLED.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include <stdio.h>
#include <string.h>

#define TASK_PROTOCOL 0U
#define TASK_PERIODIC 1U
#define TASK_OLED 2U
#define TASK_HEALTH 3U
#define TASK_KEY 4U
#if UPPER_USART1_DEBUG_ENABLE
#define TASK_LOG 5U
#define TASK_COUNT 6U
#else
#define TASK_COUNT 5U
#endif

static UpperProtocol_t protocol;
static UpperControl_t control;
static UpperUiView_t view;
static StaticTask_t task_controls[TASK_COUNT];
static StackType_t protocol_stack[UPPER_PROTOCOL_STACK_WORDS], periodic_stack[UPPER_PERIODIC_STACK_WORDS];
static StackType_t oled_stack[UPPER_OLED_STACK_WORDS], health_stack[UPPER_HEALTH_STACK_WORDS], key_stack[UPPER_KEY_STACK_WORDS];
static TaskHandle_t protocol_task_handle;
static volatile uint32_t task_heartbeat[TASK_COUNT];
static uint8_t oled_present;
static TaskHandle_t task_handles[TASK_COUNT];
volatile UpperDiagnostics_t upper_diagnostics;
typedef char DiagnosticBudget[(sizeof(UpperDiagnostics_t) + sizeof(task_handles) <= 128U) ? 1 : -1];
extern uint8_t OLED_DisplayBuf[8][128];

#if UPPER_USART1_DEBUG_ENABLE
#if UPPER_LOG_LEVEL == UPPER_LOG_LEVEL_RAW || UPPER_LOG_RAW_RECORD
#define LOG_QUEUE_LENGTH 6U
#define LOG_RECORD_CAPACITY 104U
#else
#define LOG_QUEUE_LENGTH 8U
#define LOG_RECORD_CAPACITY 56U
#endif
#define LOG_MAXIMUM(left, right) ((left) > (right) ? (left) : (right))
#define LOG_PAYLOAD_CAPACITY LOG_MAXIMUM(LOG_RECORD_CAPACITY, LOG_MAXIMUM(sizeof(UpperAuthGate_t), sizeof(CanBusTxResult_t)))
typedef struct
{
    uint32_t timestamp, detail;
    uint16_t id, sequence;
    uint8_t kind, success, dlc, type, copied;
    UpperLinkState_t state;
    uint8_t data[LOG_PAYLOAD_CAPACITY];
} UpperLog_t;
typedef char LogGateFits[(sizeof(UpperAuthGate_t) <= LOG_PAYLOAD_CAPACITY) ? 1 : -1];
typedef char LogResultFits[(sizeof(CanBusTxResult_t) <= LOG_PAYLOAD_CAPACITY) ? 1 : -1];
typedef char LogStopVerifyFits[(sizeof(UpperStopVerify_t) <= LOG_PAYLOAD_CAPACITY) ? 1 : -1];
static StackType_t log_stack[UPPER_LOG_STACK_WORDS];
static StaticQueue_t log_control;
static uint8_t log_storage[LOG_QUEUE_LENGTH * sizeof(UpperLog_t)];
static QueueHandle_t log_queue;
static volatile uint32_t log_dropped, log_event_dropped, log_frame_suppressed, log_record_suppressed, log_uart_bytes, uart_errors;
static uint32_t log_stats_at, log_started_at, log_last_error_time, log_error_suppressed;
static uint16_t log_last_error_id = 0xFFFFU;
typedef struct { uint32_t key, at; } LogErrorGate_t;
static LogErrorGate_t log_errors[16];
static uint8_t log_error_next, log_record_seen[UPPER_RECORD_COUNT];
static uint32_t log_lower_boot;
static uint16_t log_link_token, log_motion_token;
static UpperControlState_t log_control_state;
static uint8_t log_observed, log_capability_state, log_capability_attempt;
static uint32_t log_target_at;
static uint8_t log_target_seen, log_target_data[6];
static uint32_t log_record_at[UPPER_RECORD_COUNT];
static uint32_t log_gate_at[13], log_gate_seen, log_gate_mask;
static uint16_t log_gate_detail, log_gate_query_detail;
static uint8_t log_gate_query_attempt, log_gate_query_result;
static uint32_t log_recovery_at;
static uint8_t log_recovery_reason;
static uint32_t log_stop_verify_at, log_stop_verify_mask;

static uint8_t LogPriority(const UpperLog_t *message)
{
    if (message->kind == 3U || message->kind == 10U || ((message->kind == 0U || message->kind == 4U) && !message->success) ||
        (message->kind == 1U && (message->type == UPPER_EVENT_REJECT || message->type == UPPER_EVENT_TX_ERROR))) return 0U;
    if (message->kind == 1U || message->kind == 5U || message->kind == 8U) return 1U;
    if (message->kind == 6U || message->kind == 9U || message->kind == 11U || message->kind == 12U) return 2U;
    return message->kind == 2U ? 4U : 5U;
}

static uint8_t LogPush(const UpperLog_t *message)
{
    UpperLog_t queued;
    UBaseType_t count, index;
    uint8_t priority = LogPriority(message), inserted = 0U;
    taskENTER_CRITICAL();
    count = uxQueueMessagesWaiting(log_queue);
    if (count == LOG_QUEUE_LENGTH)
    {
        for (index = 0U; index < count; ++index)
        {
            xQueueReceive(log_queue, &queued, 0U);
            if (index + 1U == count && LogPriority(&queued) > priority) break;
            xQueueSend(log_queue, &queued, 0U);
        }
        ++log_dropped;
        if (index == count)
        {
            if (priority <= 2U) ++log_event_dropped;
            taskEXIT_CRITICAL();
            return 0U;
        }
        if (LogPriority(&queued) <= 2U) ++log_event_dropped;
        --count;
    }
    for (index = 0U; index < count; ++index)
    {
        xQueueReceive(log_queue, &queued, 0U);
        if (!inserted && priority < LogPriority(&queued))
        {
            xQueueSend(log_queue, message, 0U);
            inserted = 1U;
        }
        xQueueSend(log_queue, &queued, 0U);
    }
    if (!inserted) xQueueSend(log_queue, message, 0U);
    taskEXIT_CRITICAL();
    return 1U;
}

static uint8_t LogError(uint32_t key, uint16_t id, uint32_t now)
{
    uint8_t index;
    log_last_error_id = id;
    log_last_error_time = now;
    for (index = 0U; index < 16U; ++index)
        if (log_errors[index].key == key) break;
    if (index == 16U) index = log_error_next++ % 16U;
    if (log_errors[index].key == key &&
        (uint32_t)(now - log_errors[index].at) < UPPER_LOG_ERROR_REPEAT_MS)
    {
        ++log_error_suppressed;
        return 0U;
    }
    log_errors[index].key = key;
    log_errors[index].at = now;
    return 1U;
}

static uint8_t LogRaw(uint32_t now, uint8_t record)
{
    return (uint8_t)((UPPER_LOG_LEVEL == UPPER_LOG_LEVEL_RAW ||
        (record ? UPPER_LOG_RAW_RECORD : UPPER_LOG_RAW_FRAME)) &&
        (uint32_t)(now - log_started_at) < UPPER_LOG_RAW_DURATION_MS);
}

static void LogEvent(void *user, UpperEvent_t event, uint32_t detail)
{
    UpperLog_t message;
    const UpperRecord_t *record;
    uint32_t index;
    (void)user;
    memset(&message, 0, sizeof(message));
    message.timestamp = HAL_GetTick();
    message.kind = 1U;
    message.type = (uint8_t)event;
    message.detail = detail;
    message.id = protocol.snapshot.last_rx_id;
    message.sequence = protocol.record_header[0];
    message.state = protocol.snapshot.state;
    if ((event == UPPER_EVENT_REJECT || event == UPPER_EVENT_TX_ERROR) &&
        !LogError(0x10000000UL | ((uint32_t)event << 24) | detail,
            event == UPPER_EVENT_TX_ERROR ? (uint16_t)detail : protocol.snapshot.last_rx_id, message.timestamp)) return;
    if (event == UPPER_EVENT_RECORD)
    {
        for (index = 0U; index < UPPER_RECORD_COUNT; index++)
        {
            record = &protocol.snapshot.records[index];
            if (!record->valid || record->type != detail) continue;
            if (UPPER_LOG_LEVEL == UPPER_LOG_LEVEL_QUIET ||
                (log_record_seen[index] && (uint32_t)(message.timestamp - log_record_at[index]) < UPPER_LOG_RECORD_PERIOD_MS))
            {
                ++log_record_suppressed;
                return;
            }
            log_record_seen[index] = 1U;
            log_record_at[index] = message.timestamp;
            message.kind = 2U;
            message.type = record->type;
            message.sequence = record->sequence;
            message.dlc = (uint8_t)record->length;
            message.success = Upper_RecordFresh(&protocol.snapshot, record, message.timestamp);
            message.id = protocol.snapshot.link_token;
            message.copied = (uint8_t)(record->length < sizeof(message.data) ? record->length : sizeof(message.data));
            memcpy(message.data, record->payload, message.copied);
            break;
        }
    }
    LogPush(&message);
}

#endif

static uint8_t ProtocolSend(void *user, uint16_t id, const uint8_t data[8])
{
    (void)user;
#if UPPER_USART1_DEBUG_ENABLE
    if (!UPPER_CAN_MOTION_ENABLED && (id == 0x080U || id == 0x110U || id == 0x111U))
        LogEvent(NULL, UPPER_EVENT_REJECT, 0x10000UL | id);
#endif
    return (uint8_t)(CanBus_Send(id, data) == HAL_OK);
}

#if UPPER_USART1_DEBUG_ENABLE
static void LogFrame(const CanBusFrame_t *frame, uint8_t tx, uint8_t success)
{
    UpperLog_t message;
    uint32_t now = HAL_GetTick();
    uint8_t target = 0U;
    if (!success)
    {
        if (!LogError(0x20000000UL | ((uint32_t)tx << 16) | frame->id, frame->id, now)) return;
    }
    else if (tx && frame->id == 0x111U && UPPER_LOG_LEVEL != UPPER_LOG_LEVEL_QUIET)
    {
        if (log_target_seen && memcmp(log_target_data, frame->data + 2U, 6U) == 0 &&
            (uint32_t)(now - log_target_at) < UPPER_LOG_TARGET_PERIOD_MS)
        {
            ++log_frame_suppressed;
            return;
        }
        log_target_seen = 1U;
        log_target_at = now;
        memcpy(log_target_data, frame->data + 2U, 6U);
        target = 1U;
    }
    else if (!(tx && (frame->id == 0x102U || frame->id == 0x104U || frame->id == 0x105U || frame->id == 0x110U)) &&
        !(!tx && (frame->id == 0x101U || frame->id == 0x106U || frame->id == 0x120U || frame->id == 0x121U)) &&
        !(LogRaw(now, 0U) && frame->id == UPPER_LOG_RAW_ID))
    {
        ++log_frame_suppressed;
        return;
    }
    memset(&message, 0, sizeof(message));
    message.timestamp = frame->timestamp;
    message.id = frame->id;
    message.dlc = frame->dlc;
    message.kind = target ? 7U : (tx ? 4U : 0U);
    message.success = success;
    message.state = protocol.snapshot.state;
    memcpy(message.data, frame->data, 8U);
    LogPush(&message);
}

static void LogObserve(uint32_t now)
{
    static uint32_t operation_seen, operation_logged_at;
    UpperLog_t message;
    uint8_t capability = (uint8_t)(protocol.capability_active | (protocol.capability_published << 1) | (protocol.capability_confirmed << 2));
    uint8_t preparation_end = (uint8_t)(log_observed && log_control_state == SIM_AUTH_PREPARING && control.state != SIM_AUTH_PREPARING);
    uint8_t gate_index = control.detail >= 201U && control.detail <= 212U ? (uint8_t)(control.detail - 200U) : 0U;
    memset(&message, 0, sizeof(message));
    message.timestamp = now;
    message.state = protocol.snapshot.state;
    if (!log_observed || log_lower_boot != protocol.snapshot.lower_boot_id || log_link_token != protocol.snapshot.link_token)
    {
        memset(log_record_seen, 0, sizeof(log_record_seen));
        message.kind = 5U;
        message.detail = protocol.snapshot.lower_boot_id;
        message.id = protocol.snapshot.link_token;
        if(!LogPush(&message))return;
    }
    if (!log_observed || log_control_state != control.state || log_motion_token != control.motion_token)
    {
        message.kind = 6U;
        message.type = (uint8_t)control.state;
        message.id = control.motion_token;
        message.detail = control.detail;
        if(!LogPush(&message))return;
    }
    if (!log_observed || log_capability_state != capability || log_capability_attempt != protocol.capability_attempts)
    {
        message.kind = 8U;
        message.type = capability;
        message.sequence = protocol.capability_sequence;
        message.detail = protocol.capability_attempts;
        if(!LogPush(&message))return;
    }
    if ((control.state == SIM_AUTH_PREPARING || preparation_end || (control.detail >= 201U && control.detail <= 212U)) &&
        (!log_observed || preparation_end || log_gate_detail != control.detail || log_gate_mask != control.gate.mask ||
         log_gate_query_attempt != control.query_attempts || log_gate_query_result != control.query_result || log_gate_query_detail != control.query_detail) &&
        (preparation_end || !(log_gate_seen & (1UL << gate_index)) || (uint32_t)(now - log_gate_at[gate_index]) >= UPPER_LOG_ERROR_REPEAT_MS))
    {
        message.kind = 9U; message.id = control.detail; message.sequence = control.prepare_last_reason;
        message.type = control.query_attempts; message.success = control.query_result; message.dlc = control.query_phase;
        message.detail = control.query_detail;
        memcpy(message.data, &control.gate, sizeof(control.gate));
        if(!LogPush(&message))return;
        log_gate_at[gate_index] = now; log_gate_seen |= 1UL << gate_index;
        log_gate_detail = control.detail; log_gate_mask = control.gate.mask;
        log_gate_query_attempt = control.query_attempts; log_gate_query_result = control.query_result; log_gate_query_detail = control.query_detail;
    }
    if (control.operation_detail && control.operation_count != operation_seen &&
        (!operation_seen || (uint32_t)(now - operation_logged_at) >= UPPER_LOG_ERROR_REPEAT_MS))
    {
        unsigned axis;
        message.kind = 11U; message.id = control.operation_detail;
        message.detail = (uint16_t)control.rejected_rpm; message.sequence = (uint16_t)control.target_rpm;
        for (axis = 0U; axis < 2U; ++axis)
        {
            uint32_t age = now - protocol.snapshot.basic[6U + axis].timestamp;
            memcpy(message.data + axis * 4U, &age, sizeof(age));
        }
        memcpy(message.data + 8U, &protocol.snapshot.left_rpm, sizeof(int16_t));
        memcpy(message.data + 10U, &protocol.snapshot.right_rpm, sizeof(int16_t));
        if(!LogPush(&message))return;
        operation_seen = control.operation_count; operation_logged_at = now;
    }
    if ((control.state == SIM_CROSS_SOURCE_STOP || control.state == SIM_RECOVERY_VERIFY || control.detail == 115U || control.detail == 116U) &&
        (!log_recovery_at || log_recovery_reason != control.recovery_reason || (uint32_t)(now - log_recovery_at) >= UPPER_LOG_ERROR_REPEAT_MS))
    {
        const UpperRecord_t *record = UpperControl_Record(&protocol.snapshot, 0x11U);
        uint32_t flags = record ? Upper_ReadU32(record->payload + 12U) : 0U;
        uint32_t ages[2];
        ages[0] = now - protocol.snapshot.basic[6].timestamp; ages[1] = now - protocol.snapshot.basic[7].timestamp;
        message.kind = 12U; message.type = control.recovery_reason; message.detail = control.stop_event;
        message.success = control.stop_phase; message.dlc = protocol.snapshot.control_source;
        message.sequence = protocol.snapshot.lifecycle;
        memcpy(message.data, &flags, sizeof(flags)); memcpy(message.data + 4U, ages, sizeof(ages));
        if(!LogPush(&message))return;
        log_recovery_at = now; log_recovery_reason = control.recovery_reason;
    }
    if (control.stop_verify.at &&
        (!log_stop_verify_at || log_stop_verify_mask != control.stop_verify.mask ||
         (uint32_t)(now-log_stop_verify_at) >= UPPER_LOG_ERROR_REPEAT_MS)) {
        message.kind = 13U;
        memcpy(message.data,&control.stop_verify,sizeof(control.stop_verify));
        if(!LogPush(&message))return;
        log_stop_verify_at = now; log_stop_verify_mask = control.stop_verify.mask;
    }
    log_observed = 1U;
    log_lower_boot = protocol.snapshot.lower_boot_id;
    log_link_token = protocol.snapshot.link_token;
    log_control_state = control.state;
    log_motion_token = control.motion_token;
    log_capability_state = capability;
    log_capability_attempt = protocol.capability_attempts;
}

#else
#define LogEvent(user, event, detail) ((void)0)
#define LogFrame(frame, transmit, success) ((void)0)
#define LogObserve(now) ((void)0)
#endif

static void PublishUi(uint32_t now)
{
    taskENTER_CRITICAL();
    UpperUi_Publish(&control, &protocol, now, &view);
    taskEXIT_CRITICAL();
}

static void ProtocolTask(void *argument)
{
    CanBusFrame_t frame;
    CanBusTxResult_t tx_result;
    CanBusStats_t stats, previous = {0};
#if UPPER_USART1_DEBUG_ENABLE
    UpperLog_t message;
#endif
    uint32_t now, count;
    uint8_t success;
    (void)argument;
    CanBus_SetRxTask(protocol_task_handle);
    (void)CanBus_Start();
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10U));
        now = HAL_GetTick();
        task_heartbeat[TASK_PROTOCOL] = now;
        CanBus_GetStats(&stats);
        if (stats.bus_off_count != previous.bus_off_count || stats.rx_queue_overruns != previous.rx_queue_overruns ||
            stats.recovery_count != previous.recovery_count || stats.result_overruns != previous.result_overruns)
        {
            Upper_ResetSession(&protocol, now);
            CanBus_ClearPending();
            LogEvent(NULL, UPPER_EVENT_REJECT, CanBus_IsBusOff() ? 60U : 61U);
        }
#if UPPER_USART1_DEBUG_ENABLE
        if ((stats.hal_errors != previous.hal_errors || stats.invalid_frames != previous.invalid_frames || stats.tx_errors != previous.tx_errors) &&
            LogError(0x30000000UL | stats.last_error, 0xFFFFU, now))
        {
            memset(&message, 0, sizeof(message));
            message.kind = 3U;
            message.timestamp = now;
            message.detail = stats.last_error;
            message.id = (uint16_t)stats.hal_errors;
            message.sequence = (uint16_t)stats.invalid_frames;
            message.state = protocol.snapshot.state;
            LogPush(&message);
        }
#endif
        previous = stats;
        for (count = 0U; count < 8U && CanBus_Receive(&frame); count++)
        {
            uint32_t old_boot = protocol.snapshot.lower_boot_id;
            uint16_t old_token = protocol.snapshot.link_token;
#if UPPER_USART1_DEBUG_ENABLE
            uint32_t old_rejected = protocol.snapshot.rejected + protocol.snapshot.record_errors;
#endif
            now = HAL_GetTick();
            Upper_Receive(&protocol, frame.id, frame.dlc, frame.data, frame.timestamp, now);
            LogObserve(now);
            if (frame.dlc == 8U) UpperControl_Receive(&control, &protocol, frame.id, frame.data, frame.timestamp, now);
            if (frame.id == 0x120U || frame.id == 0x121U) LogObserve(now);
            LogFrame(&frame, 0U, (uint8_t)(old_rejected == protocol.snapshot.rejected + protocol.snapshot.record_errors));
            if ((old_boot && old_boot != protocol.snapshot.lower_boot_id) || (old_token && !protocol.snapshot.link_token)) CanBus_ClearPending();
        }
        now = HAL_GetTick();
        CanBus_Service(now);
        while (CanBus_GetTxResultEx(&tx_result))
        {
            now = HAL_GetTick();
            frame = tx_result.frame;
            success = (uint8_t)(tx_result.outcome == UPPER_TX_OK);
            if (tx_result.outcome != UPPER_TX_CANCELLED && tx_result.outcome != UPPER_TX_REPLACED)
                Upper_TxResult(&protocol, frame.id, frame.data, success, now);
            UpperControl_TxOutcome(&control, frame.id, frame.data, tx_result.outcome, now);
            if (!success && tx_result.outcome != UPPER_TX_CANCELLED && tx_result.outcome != UPPER_TX_REPLACED)
            {
                if (frame.id == 0x111U && control.recovery_required) CanBus_DropMotion();
#if UPPER_USART1_DEBUG_ENABLE
                memset(&message, 0, sizeof(message));
                message.kind = 10U; message.id = frame.id; message.timestamp = now;
                memcpy(message.data, &tx_result, sizeof(tx_result));
                if (LogError(0x40000000UL | tx_result.outcome, frame.id, now)) LogPush(&message);
#endif
            }
            LogFrame(&frame, 1U, success);
        }
        {
            uint16_t old_token = protocol.snapshot.link_token;
            now = HAL_GetTick();
            Upper_Tick(&protocol, now, (uint8_t)(CanBus_IsStarted() && !CanBus_IsBusOff()));
            if (old_token && !protocol.snapshot.link_token) CanBus_ClearPending();
        }
        {
            UpperUi_NotifyState(control.state);
            UpperUi_ConsumeEventsAt(&control, &protocol, HAL_GetTick());
            now = HAL_GetTick();
            UpperControl_Tick(&control, &protocol, now);
            if (control.state != SIM_AUTHORIZED_WAIT_ZERO && control.state != SIM_AUTHORIZED_ZERO && control.state != SIM_RUNNING) CanBus_DropMotion();
        }
        now = HAL_GetTick();
        CanBus_Service(now);
        LogObserve(now);
        PublishUi(now);
        upper_diagnostics.control_progress = now;
    }
}

static void PeriodicTask(void *argument)
{
    TickType_t wake = xTaskGetTickCount();
    (void)argument;
    for (;;)
    {
        task_heartbeat[TASK_PERIODIC] = HAL_GetTick();
        xTaskNotifyGive(protocol_task_handle);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10U));
    }
}

#if UPPER_USART1_DEBUG_ENABLE
static uint8_t SerialWrite(const char *text)
{
    uint16_t length = (uint16_t)strlen(text);
    if (HAL_UART_Transmit(&huart1, (uint8_t *)text, length, 100U) != HAL_OK) { uart_errors++; return 0U; }
    log_uart_bytes += length;
    return 1U;
}

/* Only the producer creates this scene. It remains immutable until every page
 * was accepted by USART1; ordinary queue pressure cannot evict it. */
static uint8_t LogStopFirst(char *text, uint32_t capacity)
{
    const UpperStopFirst_t *f=UpperControl_StopFirst();
    uint8_t page;
    int length;
    if(!f->pending)return 0U;
    page=f->page;
    if(page==0U)length=snprintf(text,capacity,
        "STOP_FIRST v=1 at=%lu part=0 source=%u recovery=%u detail=%u state=%u event_at=%lu key=%u/%02X boot=%08lX/%08lX token=%u/%u target=%d/%d\r\n",
        (unsigned long)f->at,f->source,f->recovery,f->detail,f->state,(unsigned long)f->event_at,
        f->key_event,f->key_mask,(unsigned long)f->lower_boot,(unsigned long)f->upper_boot,
        f->link_token,f->motion_token,f->target_rpm,f->target_mm_s);
    else if(page==1U)length=snprintf(text,capacity,
        "STOP_FIRST v=1 at=%lu part=1 valid=%02X ctrl_age=%lu axis_age=%lu/%lu hb_age=%lu ctrl_seq=%u speed_seq=%u gen=%lu flags=%08lX owner=%u life=%u axes=%u\r\n",
        (unsigned long)f->at,f->validity,(unsigned long)f->control_age,
        (unsigned long)f->axis_age[0],(unsigned long)f->axis_age[1],(unsigned long)f->heartbeat_age,
        f->control_sequence,f->speed_sequence,(unsigned long)f->control_generation,
        (unsigned long)f->flags,f->owner,f->life,f->axes);
    else length=snprintf(text,capacity,
        "STOP_FIRST v=1 at=%lu part=2 axis_flags=%02X/%02X mode=%u/%u fault=%u/%u stop_req=%u merged=%lu\r\n",
        (unsigned long)f->at,f->axis_flags[0],f->axis_flags[1],f->axis_mode[0],f->axis_mode[1],
        f->axis_fault[0],f->axis_fault[1],f->stop_request,(unsigned long)f->merged);
    if(length>0 && (uint32_t)length<capacity && SerialWrite(text)) {
        /* Only metadata acknowledgement is atomic; formatting/UART wait stay outside. */
        taskENTER_CRITICAL(); UpperControl_StopFirstSent(page); taskEXIT_CRITICAL();
    }
    return 1U;
}

/* The producer freezes control.stop_verify, so this retry path needs no second
 * SRAM scene and cannot be evicted by the ordinary log queue. */
static uint8_t LogStopFailure(char *text,uint32_t capacity)
{
    const UpperStopVerify_t *v=&control.stop_verify;
    int length;
    if(control.stop_failure_state!=1U)return 0U;
    if(v->mask&0x80000000UL)
        length=snprintf(text,capacity,
            "STOP_FAIL v=1 at=%lu err=%u mask=%08lX seq=%u lower=%08lX/%08lX upper=%08lX/%08lX token=%u/%u phase=%u result=%u\r\n",
            (unsigned long)v->at,control.stop_failure_detail,(unsigned long)v->mask,v->token,
            (unsigned long)v->control_age,(unsigned long)v->stop_age,
            (unsigned long)v->axis_age[0],(unsigned long)v->axis_age[1],
            (unsigned)(v->diagnostic>>16U),(unsigned)(v->diagnostic&65535U),v->phase,v->result);
    else
        length=snprintf(text,capacity,
            "STOP_FAIL v=1 at=%lu err=%u mask=%08lX seq=%u event=%lu owner=%u life=%u phase=%u result=%u flags=%08lX age=%lu/%lu axis=%lu/%lu diag=%08lX\r\n",
            (unsigned long)v->at,control.stop_failure_detail,(unsigned long)v->mask,
            control.stop_failure_sequence,(unsigned long)v->event,v->owner,v->life,v->phase,v->result,(unsigned long)v->flags,
            (unsigned long)v->control_age,(unsigned long)v->stop_age,
            (unsigned long)v->axis_age[0],(unsigned long)v->axis_age[1],(unsigned long)v->diagnostic);
    if(length>0 && (uint32_t)length<capacity && SerialWrite(text)) {
        taskENTER_CRITICAL();control.stop_failure_state=2U;taskEXIT_CRITICAL();
    }
    return 1U;
}

static void WriteHex64(const uint8_t *data, char *text)
{
    snprintf(text, 19U, "0x%08lX%08lX", (unsigned long)Upper_ReadU32(data + 4), (unsigned long)Upper_ReadU32(data));
}

static void LogRecord(const UpperLog_t *message)
{
    const uint8_t *data = message->data;
    char text[180], value1[19], value2[19];
    uint32_t offset;
    uint8_t minimum = 0U;
    switch (message->type)
    {
        case 0x02: minimum = 23U; break;
        case 0x10: minimum = 36U; break;
        case 0x11: minimum = 28U; break;
        case 0x12: minimum = 40U; break;
        case 0x13: minimum = 32U; break;
        case 0x20: case 0x21: minimum = 48U; break;
        case 0x22: minimum = 56U; break;
        case 0x23: minimum = 20U; break;
        case 0x24: minimum = 48U; break;
        default: break;
    }
    if (message->copied > sizeof(message->data) || message->copied > message->dlc || message->copied < minimum)
    {
        SerialWrite("RECORD diagnostic prefix invalid\r\n");
        return;
    }
    snprintf(text, sizeof(text), "[%lu] RECORD type=%02X seq=%u len=%u crc=OK token=%04X match=YES fresh=%s LINK=%s\r\n",
        (unsigned long)message->timestamp, message->type, message->sequence, message->dlc,
        message->id, message->success ? "YES" : "NO", Upper_StateName(message->state));
    SerialWrite(text);
    switch (message->type)
    {
        case 0x02:
            snprintf(text, sizeof(text), "  CAP boot=%08lX peer=%08lX caps=%08lX sources=%08lX role=%u\r\n",
                (unsigned long)Upper_ReadU32(data), (unsigned long)Upper_ReadU32(data + 4),
                (unsigned long)Upper_ReadU32(data + 8), (unsigned long)Upper_ReadU32(data + 12), data[22]);
            break;
        case 0x10:
            snprintf(text, sizeof(text), "  CONFIG rev=%lu axes=%u wheel_um=%lu/%lu counts_rev=%lu/%lu speed_limit=%u\r\n",
                (unsigned long)Upper_ReadU32(data), data[4], (unsigned long)Upper_ReadU32(data + 8),
                (unsigned long)Upper_ReadU32(data + 12), (unsigned long)Upper_ReadU32(data + 16),
                (unsigned long)Upper_ReadU32(data + 20), Upper_ReadU16(data + 34));
            break;
        case 0x11:
            snprintf(text, sizeof(text), "  CONTROL gen=%lu motion_token=%04X source=%u life=%u flags=%08lX speed_seq=%u reject=%u\r\n",
                (unsigned long)Upper_ReadU32(data), Upper_ReadU16(data + 4), data[6], data[7],
                (unsigned long)Upper_ReadU32(data + 12), Upper_ReadU16(data + 20), Upper_ReadU16(data + 26));
            break;
        case 0x12:
            snprintf(text, sizeof(text), "  HEALTH uptime=%lu reset=%08lX flags=%08lX failed_hb=%08lX valid=%04X\r\n",
                (unsigned long)Upper_ReadU32(data), (unsigned long)Upper_ReadU32(data + 4),
                (unsigned long)Upper_ReadU32(data + 8), (unsigned long)Upper_ReadU32(data + 12), Upper_ReadU16(data + 38));
            break;
        case 0x13:
            snprintf(text, sizeof(text), "  STOP gen=%lu reason=%u phase=%u zero_axes=%u failed_axes=%u time_done=%lu\r\n",
                (unsigned long)Upper_ReadU32(data), data[4], data[5], data[6], data[7], (unsigned long)Upper_ReadU32(data + 28));
            break;
        case 0x20: case 0x21:
            WriteHex64(data + 24, value1); WriteHex64(data + 40, value2);
            snprintf(text, sizeof(text), "  AXIS %u raw=%ld sample=%lu gen=%lu source_age=%lu flags=%04X net_count=%s travel_count=%s\r\n",
                data[23], (long)(int32_t)Upper_ReadU32(data), (unsigned long)Upper_ReadU32(data + 4),
                (unsigned long)Upper_ReadU32(data + 8), (unsigned long)Upper_ReadU32(data + 16), Upper_ReadU16(data + 20), value1, value2);
            break;
        case 0x22:
            WriteHex64(data + 40, value1); WriteHex64(data + 48, value2);
            snprintf(text, sizeof(text), "  WHEEL_EST flags=%08lX baseline=%lu pair_age=%u skew=%u net_mm=%s sampled_travel_mm=%s\r\n",
                (unsigned long)Upper_ReadU32(data), (unsigned long)Upper_ReadU32(data + 4), Upper_ReadU16(data + 20), Upper_ReadU16(data + 22), value1, value2);
            break;
        case 0x23:
            snprintf(text, sizeof(text), "  SYNC flags=%08lX rpm=%d/%d delta=%d comp=%d skew=%u\r\n",
                (unsigned long)Upper_ReadU32(data), (int16_t)Upper_ReadU16(data + 4), (int16_t)Upper_ReadU16(data + 6),
                (int16_t)Upper_ReadU16(data + 8), (int16_t)Upper_ReadU16(data + 12), Upper_ReadU16(data + 18));
            break;
        case 0x24:
            snprintf(text, sizeof(text), "  LOWER_CAN rx=%lu tx_done=%lu reject=%lu drop=%lu busoff=%lu record_err=%lu valid=%08lX\r\n",
                (unsigned long)Upper_ReadU32(data), (unsigned long)Upper_ReadU32(data + 4), (unsigned long)Upper_ReadU32(data + 8),
                (unsigned long)Upper_ReadU32(data + 12), (unsigned long)Upper_ReadU32(data + 20),
                (unsigned long)Upper_ReadU32(data + 24), (unsigned long)Upper_ReadU32(data + 44));
            break;
        default:
            snprintf(text, sizeof(text), "  OPTIONAL_RECORD valid transport; service fields may be UNKNOWN\r\n");
            break;
    }
    SerialWrite(text);
    if (!LogRaw(HAL_GetTick(), 1U)) return;
    for (offset = 0U; offset + 8U <= message->copied; offset += 8U)
    {
        snprintf(text, sizeof(text), "  +%03lu: %02X %02X %02X %02X %02X %02X %02X %02X\r\n", (unsigned long)offset,
            data[offset],data[offset+1],data[offset+2],data[offset+3],data[offset+4],data[offset+5],data[offset+6],data[offset+7]);
        SerialWrite(text);
        task_heartbeat[TASK_LOG] = HAL_GetTick();
    }
    if (offset < message->copied)
    {
        uint32_t position, used = (uint32_t)snprintf(text, sizeof(text), "  +%03lu:", (unsigned long)offset);
        for (position = offset; position < message->copied && used + 6U < sizeof(text); ++position)
            used += (uint32_t)snprintf(text + used, sizeof(text) - used, " %02X", data[position]);
        snprintf(text + used, sizeof(text) - used, "\r\n");
        SerialWrite(text);
    }
}

static void LogMessage(const UpperLog_t *entry)
{
    UpperLog_t message = *entry;
    char text[320];
    if (message.kind == 0U || message.kind == 4U)
    {
        snprintf(text, sizeof(text), "[%lu] %s ID=%03X DLC=%u %02X %02X %02X %02X %02X %02X %02X %02X LINK=%s %s\r\n",
            (unsigned long)message.timestamp, message.kind == 4U ? "TX" : "RX", message.id, message.dlc,
            message.data[0],message.data[1],message.data[2],message.data[3],message.data[4],message.data[5],message.data[6],message.data[7],
            Upper_StateName(message.state), message.kind == 4U ? (message.success ? "TX_DONE" : "TX_FAILED") : (message.success ? "OK" : "REJECT"));
        SerialWrite(text);
        if (message.kind == 0U && message.id == 0x181U && message.data[5] == 0U) SerialWrite("  BMS UNKNOWN / NOT CONNECTED; not 0V or 0%\r\n");
    }
    else if (message.kind == 2U) LogRecord(&message);
    else if (message.kind == 5U)
    {
        snprintf(text, sizeof(text), "[%lu] SESSION boot=%08lX token=%04X LINK=%s\r\n",
            (unsigned long)message.timestamp, (unsigned long)message.detail, message.id, Upper_StateName(message.state));
        SerialWrite(text);
    }
    else if (message.kind == 6U)
    {
        snprintf(text, sizeof(text), "[%lu] CONTROL state=%s motion_token=%04X detail=%lu old_target_cleared=%s\r\n",
            (unsigned long)message.timestamp, UpperControl_StateName((UpperControlState_t)message.type), message.id,
            (unsigned long)message.detail, message.type == SIM_FAULT || message.type == SIM_LINK_NOT_READY ||
                        message.type == SIM_STOPPED_RELEASED ? "YES" : "NO");
        SerialWrite(text);
    }
    else if (message.kind == 13U)
    {
        UpperStopVerify_t verify;
        memcpy(&verify,message.data,sizeof(verify));
        snprintf(text,sizeof(text),"STOP_VERIFY t=%lu mask=%03lX owner=%u life=%u token=%u event=%lu phase=%u result=%u flags=%08lX age=%lu/%lu axis=%lu/%lu diag=%08lX\r\n",
            (unsigned long)verify.at,(unsigned long)verify.mask,verify.owner,verify.life,verify.token,
            (unsigned long)verify.event,verify.phase,verify.result,(unsigned long)verify.flags,
            (unsigned long)verify.control_age,(unsigned long)verify.stop_age,
            (unsigned long)verify.axis_age[0],(unsigned long)verify.axis_age[1],(unsigned long)verify.diagnostic);
        SerialWrite(text);
    }
    else if (message.kind == 12U)
    {
        uint32_t flags, ages[2];
        memcpy(&flags, message.data, sizeof(flags)); memcpy(ages, message.data + 4U, sizeof(ages));
        snprintf(text, sizeof(text), "RECOVERY %s event=%lu phase=%u owner=%u life=%u flags=%08lX axis_age=%lu/%lu\r\n",
            UpperControl_RecoveryName(message.type), (unsigned long)message.detail, message.success, message.dlc,
            message.sequence, (unsigned long)flags, (unsigned long)ages[0], (unsigned long)ages[1]);
        SerialWrite(text);
    }
    else if (message.kind == 7U)
    {
        snprintf(text, sizeof(text), "TARGET seq=%u value=%d token=%u lease=%u TX_DONE\r\n",
            Upper_ReadU16(message.data), (int16_t)Upper_ReadU16(message.data + 2),
            Upper_ReadU16(message.data + 4), Upper_ReadU16(message.data + 6));
        SerialWrite(text);
    }
    else if (message.kind == 8U)
    {
        snprintf(text, sizeof(text), "CAPABILITY seq=%u attempts=%lu active=%u tx_complete=%u confirmed=%u\r\n",
            message.sequence, (unsigned long)message.detail, message.type & 1U, (message.type >> 1) & 1U, (message.type >> 2) & 1U);
        SerialWrite(text);
    }
    else if (message.kind == 10U)
    {
        CanBusTxResult_t result;
        memcpy(&result, message.data, sizeof(result));
        snprintf(text, sizeof(text), "TX_FINAL id=%03X seq=%u token=%u cause=%u gen=%lu age=%lu mailbox=%lu TSR=%08lX ESR=%08lX K3:CONFIRM STOP\r\n",
            result.frame.id, Upper_ReadU16(result.frame.data), Upper_ReadU16(result.frame.data + 4U), (unsigned)result.outcome,
            (unsigned long)result.generation, (unsigned long)result.age, (unsigned long)result.mailbox,
            (unsigned long)result.tsr, (unsigned long)result.esr);
        SerialWrite(text);
    }
    else if (message.kind == 11U)
    {
        snprintf(text, sizeof(text), "OP_REJECT err=%u requested=%d target=%d L=%d/%lums R=%d/%lums %s\r\n",
            message.id, (int16_t)message.detail, (int16_t)message.sequence,
            (int16_t)Upper_ReadU16(message.data + 8U), (unsigned long)Upper_ReadU32(message.data),
            (int16_t)Upper_ReadU16(message.data + 10U), (unsigned long)Upper_ReadU32(message.data + 4U),
            message.id == 103U ? "ZERO FIRST: apply 0 then wait fresh zero" : "WAIT ZERO / CHECK FEEDBACK");
        SerialWrite(text);
    }
    else if (message.kind == 9U)
    {
        UpperAuthGate_t gate;
        unsigned axis;
        memcpy(&gate, message.data, sizeof(gate));
        snprintf(text, sizeof(text), "AUTH_GATE err=%u reason=%s last=%u mask=%08lX ctrl_age=%lu hb_age=%lu owner=%u life=%u gates=%02X axes=%u cap_tx=%u cap_ack=%u query=%u/%u/%u detail=%lu\r\n",
            message.id, UpperControl_DetailName(message.id), message.sequence, (unsigned long)gate.mask,
            (unsigned long)gate.control_age, (unsigned long)gate.heartbeat_age, gate.owner, gate.life, gate.gates, gate.axes,
            gate.capability_sent, gate.capability_confirmed, message.type, message.dlc, message.success, (unsigned long)message.detail);
        SerialWrite(text);
        for (axis = 0U; axis < 2U; ++axis)
        {
            const UpperAxisGate_t *check = &gate.axis[axis];
            snprintf(text, sizeof(text), "AUTH_AXIS axis=%u age=%lu fresh=%u flags=%02X online=%u enable=%u param=%u stop=%u mode=%u rpm=%d fault=%04X\r\n",
                axis, (unsigned long)check->age, check->fresh, check->flags, check->flags & 1U,
                (check->flags >> 1) & 1U, (check->flags >> 4) & 1U, (check->flags >> 5) & 1U, check->mode, check->rpm, check->fault);
            SerialWrite(text);
        }
    }
    else
    {
        snprintf(text, sizeof(text), "[%lu] %s event=%u detail=%08lX id=%03X record=%02X LINK=%s\r\n", (unsigned long)message.timestamp,
            message.kind == 3U ? "CAN_ERROR" : "PROTOCOL", message.type, (unsigned long)message.detail,
            message.id, message.kind == 3U ? 0U : message.sequence, Upper_StateName(message.state));
        SerialWrite(text);
    }
}

static void LogTask(void *argument)
{
    UpperLog_t message;
    CanBusStats_t stats;
    char text[320];
    uint32_t boot_id = protocol.snapshot.upper_boot_id;
    (void)argument;
    snprintf(text, sizeof(text), "\r\nUPPER CAN SIM V1.1 MOTION TEST " UPPER_BUILD_ID " boot=%08lX USART1=115200 CAN=500000 PA11/PA12\r\n", (unsigned long)boot_id);
    SerialWrite(text);
    SerialWrite(boot_id ? "TB6612/PWM DISABLED; keys select pages; profile controls motion\r\n" : "BOOT PERSISTENCE ERROR: CAN protocol disabled; no fixed boot fallback\r\n");
    snprintf(text, sizeof(text), "LOG mode=%u raw_frame=%u raw_record=%u period=%u record_period=%u raw_window=%lu\r\n",
        UPPER_LOG_LEVEL, UPPER_LOG_RAW_FRAME, UPPER_LOG_RAW_RECORD, UPPER_LOG_PERIOD_MS,
        UPPER_LOG_RECORD_PERIOD_MS, (unsigned long)UPPER_LOG_RAW_DURATION_MS);
    SerialWrite(text);
    for (;;)
    {
        task_heartbeat[TASK_LOG] = HAL_GetTick();
        (void)LogStopFirst(text,sizeof(text));
        (void)LogStopFailure(text,sizeof(text));
        if (xQueueReceive(log_queue, &message, pdMS_TO_TICKS(50U)) == pdPASS)
        {
            LogMessage(&message);
            task_heartbeat[TASK_LOG] = HAL_GetTick();
        }
        if ((uint32_t)(HAL_GetTick() - log_stats_at) >= UPPER_LOG_PERIOD_MS)
        {
            log_stats_at = HAL_GetTick();
            CanBus_GetStats(&stats);
            snprintf(text, sizeof(text), "STAT rx=%lu tx_done=%lu tx_err=%lu mailbox=%lu rx_drop=%lu tx_drop=%lu busoff=%lu log_drop=%lu event_drop=%lu frame_sup=%lu record_sup=%lu uart_bytes=%lu uart_err=%lu\r\n",
                (unsigned long)stats.rx_frames,(unsigned long)stats.tx_frames,(unsigned long)stats.tx_errors,
                (unsigned long)stats.mailbox_full,(unsigned long)stats.rx_queue_overruns,(unsigned long)stats.tx_dropped,
                (unsigned long)stats.bus_off_count,(unsigned long)log_dropped,(unsigned long)log_event_dropped,
                (unsigned long)log_frame_suppressed,(unsigned long)log_record_suppressed,
                (unsigned long)log_uart_bytes,(unsigned long)uart_errors);
            SerialWrite(text);
            snprintf(text, sizeof(text), "UI oled=%u ok=%lu fail=%lu recover=%lu page=%u hal=%u i2c=%08lX key=%lu control=%lu stack=%u/%u/%u/%u/%u/%u idle=%u timer=%u reset=%08lX fault=%lu\r\n",
                upper_diagnostics.oled_state, (unsigned long)upper_diagnostics.oled_last_ok,
                (unsigned long)upper_diagnostics.oled_fail_count, (unsigned long)upper_diagnostics.oled_recovery_count,
                upper_diagnostics.oled_page, upper_diagnostics.oled_hal_status, (unsigned long)upper_diagnostics.oled_last_error,
                (unsigned long)upper_diagnostics.key_progress, (unsigned long)upper_diagnostics.control_progress,
                upper_diagnostics.stack_min_words[0],upper_diagnostics.stack_min_words[1],upper_diagnostics.stack_min_words[2],
                upper_diagnostics.stack_min_words[3],upper_diagnostics.stack_min_words[4],upper_diagnostics.stack_min_words[5],
                upper_diagnostics.stack_min_words[6],upper_diagnostics.stack_min_words[7],
                (unsigned long)upper_diagnostics.reset_flags,(unsigned long)upper_diagnostics.last_fault);
            SerialWrite(text);
            snprintf(text, sizeof(text), "LOG error_sup=%lu last_error_id=%03X last_error_time=%lu\r\n",
                (unsigned long)log_error_suppressed, log_last_error_id, (unsigned long)log_last_error_time);
            SerialWrite(text);
            snprintf(text, sizeof(text), "TXSTAT cancel=%lu replaced=%lu expired=%lu timeout=%lu result_drop=%lu abort_timeout=%lu recovery=%lu\r\n",
                (unsigned long)stats.tx_cancelled, (unsigned long)stats.tx_replaced, (unsigned long)stats.tx_expired,
                (unsigned long)stats.tx_timeouts, (unsigned long)stats.result_overruns, (unsigned long)stats.abort_timeouts,
                (unsigned long)stats.recovery_count);
            SerialWrite(text);
        }
    }
}


#endif

static void OledFailure(HAL_StatusTypeDef status, uint8_t page)
{
    upper_diagnostics.oled_fail_count++;
    upper_diagnostics.oled_failure_at = HAL_GetTick();
    upper_diagnostics.oled_last_error = HAL_I2C_GetError(&hi2c1);
    upper_diagnostics.oled_last_state = HAL_I2C_GetState(&hi2c1);
    upper_diagnostics.oled_hal_status = (uint8_t)status;
    upper_diagnostics.oled_failed_page = page;
}

static uint32_t OledBudget(uint32_t start)
{
    uint32_t elapsed = HAL_GetTick() - start;
    return elapsed < UPPER_OLED_RECOVERY_BUDGET_MS ? UPPER_OLED_RECOVERY_BUDGET_MS - elapsed : 0U;
}

static uint8_t OledHardwareInit(uint32_t timeout)
{
    HAL_StatusTypeDef status;
    uint8_t commands[] = {0xAE,0x20,0x02,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,0xA1,0xC8,0xDA,0x12,
        0x81,0xCF,0xD9,0xF1,0xDB,0x30,0xA4,0xA6,0x8D,0x14,0xB0,0x10,0x00,0xAF};
    if (!timeout) return 0U;
    if (timeout > 10U) timeout = 10U;
    status = HAL_I2C_Mem_Write(&hi2c1,0x78U,0x00U,I2C_MEMADD_SIZE_8BIT,commands,sizeof(commands),timeout);
    if (status != HAL_OK) OledFailure(status, 0xFFU);
    return (uint8_t)(status == HAL_OK);
}

static uint8_t OledHardwareRefreshPage(uint8_t page)
{
    HAL_StatusTypeDef status;
    uint8_t commands[3];
    if (page >= 8U) return 0U;
    commands[0] = (uint8_t)(0xB0U + page); commands[1] = 0x10U; commands[2] = 0x00U;
    status = HAL_I2C_Mem_Write(&hi2c1,0x78U,0x00U,I2C_MEMADD_SIZE_8BIT,commands,3U,10U);
    if (status == HAL_OK)
        status = HAL_I2C_Mem_Write(&hi2c1,0x78U,0x40U,I2C_MEMADD_SIZE_8BIT,OLED_DisplayBuf[page],128U,10U);
    if (status != HAL_OK) OledFailure(status, page);
    return (uint8_t)(status == HAL_OK);
}

static uint8_t OledRecover(void)
{
    GPIO_InitTypeDef pins = {0};
    HAL_StatusTypeDef status;
    uint32_t start = HAL_GetTick(), budget;
    uint8_t pulse, released = 1U;
    upper_diagnostics.oled_state = 3U;
    upper_diagnostics.oled_recovery_count++;
    status = HAL_I2C_DeInit(&hi2c1);
    if (status != HAL_OK) { OledFailure(status, 0xFFU); return 0U; }
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
    pins.Pin = GPIO_PIN_8 | GPIO_PIN_9; pins.Mode = GPIO_MODE_OUTPUT_OD; pins.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &pins);
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_RESET) released = 0U;
    for (pulse = 0U; released && HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET && pulse < 9U; ++pulse)
    {
        if (OledBudget(start) <= 4U) { released = 0U; break; }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
        vTaskDelay(pdMS_TO_TICKS(1U));
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(1U));
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_RESET) released = 0U;
    }
    if (released && OledBudget(start) > 4U)
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
        vTaskDelay(pdMS_TO_TICKS(1U));
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(1U));
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_RESET) released = 0U;
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(1U));
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET) released = 0U;
    }
    else released = 0U;
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();
    status = HAL_I2C_Init(&hi2c1);
    if (status != HAL_OK) { OledFailure(status, 0xFFU); return 0U; }
    budget = OledBudget(start);
    if (!released || budget <= 25U) { OledFailure(HAL_BUSY, 0xFFU); return 0U; }
    status = HAL_I2C_IsDeviceReady(&hi2c1,0x78U,1U,budget > 10U ? 10U : budget);
    if (status != HAL_OK) { OledFailure(status, 0xFFU); return 0U; }
    budget = OledBudget(start);
    return budget > 25U ? OledHardwareInit(budget - 25U) : 0U;
}

static void OledTask(void *argument)
{
    UpperUiView_t snapshot;
    CanBusStats_t stats;
    char text[UPPER_OLED_LINE_CHARS + 1U];
    uint8_t row;
    uint32_t now, retry = 0U;
    uint8_t page = 0U, rendered = 0U, failures = 0U;
    (void)argument;
    for (;;)
    {
        now = HAL_GetTick();
        task_heartbeat[TASK_OLED] = now;
        if (!oled_present && (int32_t)(now - retry) >= 0)
        {
            oled_present = OledRecover();
            if (oled_present) { page = 0U; rendered = 0U; failures = 0U; }
            retry = HAL_GetTick() + UPPER_OLED_BACKOFF_MS;
            upper_diagnostics.oled_state = oled_present ? 0U : 2U;
        }
        if (oled_present)
        {
            taskENTER_CRITICAL(); snapshot = view; taskEXIT_CRITICAL();
            CanBus_GetStats(&stats);
            if (!rendered)
            {
                OLED_Clear();
                for (row = 0U; row < 8U; ++row)
                {
                    UpperUi_Format(&snapshot, row, text, sizeof(text));
                    if (snapshot.page == 5U && row == 3U) snprintf(text, sizeof(text), "RX%lu TX%lu", (unsigned long)(stats.rx_frames > 999999U ? 999999U : stats.rx_frames), (unsigned long)(stats.tx_frames > 999999U ? 999999U : stats.tx_frames));
                    if (snapshot.page == 5U && row == 4U) snprintf(text, sizeof(text), "D%lu/%lu B%lu", (unsigned long)(stats.rx_queue_overruns > 99999U ? 99999U : stats.rx_queue_overruns), (unsigned long)(stats.tx_dropped > 99999U ? 99999U : stats.tx_dropped), (unsigned long)(stats.bus_off_count > 99999U ? 99999U : stats.bus_off_count));
                    if (snapshot.page == 5U && row == 6U)
                        snprintf(text, sizeof(text), "OLED:%u FAIL:%u", upper_diagnostics.oled_state, upper_diagnostics.oled_failures);
                    text[UPPER_OLED_LINE_CHARS] = '\0';
                    OLED_ShowStringClipped(0, row * 8U, text, UPPER_OLED_LINE_CHARS, 128U, OLED_6X8);
                }
                rendered = 1U;
                upper_diagnostics.oled_frame_generation++;
            }
            upper_diagnostics.oled_page = page;
            if (OledHardwareRefreshPage(page))
            {
                failures = 0U;
                upper_diagnostics.oled_refresh_count++;
                upper_diagnostics.oled_last_ok = HAL_GetTick();
                upper_diagnostics.oled_state = 0U;
                page = (uint8_t)((page + 1U) & 7U);
                if (page == 0U) rendered = 0U;
            }
            else
            {
                upper_diagnostics.oled_state = 1U;
                if (++failures >= UPPER_OLED_FAILURE_LIMIT)
                {
                    oled_present = 0U; rendered = 0U;
                    retry = HAL_GetTick() + UPPER_OLED_BACKOFF_MS;
                    upper_diagnostics.oled_state = 2U;
                }
            }
            upper_diagnostics.oled_failures = failures;
        }
        task_heartbeat[TASK_OLED] = HAL_GetTick();
        vTaskDelay(pdMS_TO_TICKS(UPPER_OLED_PAGE_PERIOD_MS));
    }
}

static void KeyTask(void *argument)
{
    uint32_t now;
    uint8_t pressed;
    (void)argument;
    for (;;)
    {
        now = HAL_GetTick();
        task_heartbeat[TASK_KEY] = now;
        pressed = (uint8_t)((HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_RESET ? 1U : 0U) |
            (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET ? 2U : 0U) |
            (HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin) == GPIO_PIN_RESET ? 4U : 0U));
        UpperUi_ScanKeysOnly(now, pressed);
        upper_diagnostics.key_progress = now;
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

static void HealthTask(void *argument)
{
    uint32_t now, index;
    uint8_t healthy;
    (void)argument;
    for (;;)
    {
        now = HAL_GetTick();
        task_heartbeat[TASK_HEALTH] = now;
        healthy = 1U;
        for (index = 0U; index < TASK_COUNT; index++) if ((uint32_t)(now - task_heartbeat[index]) >= 1000U) healthy = 0U;
        for (index = 0U; index < TASK_COUNT; ++index)
        {
            UBaseType_t words = uxTaskGetStackHighWaterMark(task_handles[index]);
            upper_diagnostics.stack_min_words[index] = (uint16_t)(words > 65535U ? 65535U : words);
        }
        upper_diagnostics.stack_min_words[6] = (uint16_t)uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandle());
        upper_diagnostics.stack_min_words[7] = (uint16_t)uxTaskGetStackHighWaterMark(xTimerGetTimerDaemonTaskHandle());
        if (healthy) HAL_IWDG_Refresh(&hiwdg);
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}

void UpperApp_Fault(uint32_t reason)
{
    taskDISABLE_INTERRUPTS();
    upper_diagnostics.last_fault = reason;
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    PWR->CR |= PWR_CR_DBP;
    BKP->DR2 = reason & 0xFFFFU;
    BKP->DR3 = (~reason) & 0xFFFFU;
    BKP->DR1 = 0x4F46U;
    for (;;) { }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    uint32_t index;
    (void)name;
    for (index = 0U; index < TASK_COUNT; ++index) if (task_handles[index] == task) break;
    UpperApp_Fault(0x100U | index);
}

void UpperApp_Init(void)
{
    uint32_t boot_id = UpperBoot_Allocate();
    upper_diagnostics.reset_flags = RCC->CSR;
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    PWR->CR |= PWR_CR_DBP;
    if ((RCC->CSR & (RCC_CSR_IWDGRSTF | RCC_CSR_SFTRSTF)) && BKP->DR1 == 0x4F46U &&
        (((BKP->DR2 ^ BKP->DR3) & 0xFFFFU) == 0xFFFFU)) upper_diagnostics.last_fault = BKP->DR2;
    BKP->DR1 = 0U;
    __HAL_RCC_CLEAR_RESET_FLAGS();
#if UPPER_USART1_DEBUG_ENABLE
    log_started_at = HAL_GetTick();
    log_queue = xQueueCreateStatic(LOG_QUEUE_LENGTH, sizeof(UpperLog_t), log_storage, &log_control);
    Upper_Init(&protocol, boot_id, ProtocolSend, LogEvent, NULL);
#else
    Upper_Init(&protocol, boot_id, ProtocolSend, NULL, NULL);
#endif
    UpperControl_Init(&control, ProtocolSend, NULL);
    UpperUi_Init();
    PublishUi(HAL_GetTick());
}

void UpperApp_Start(void)
{
    uint32_t index, now = HAL_GetTick();
    for (index = 0U; index < TASK_COUNT; index++) task_heartbeat[index] = now;
    task_handles[TASK_PROTOCOL] = protocol_task_handle = xTaskCreateStatic(ProtocolTask, "can_protocol", UPPER_PROTOCOL_STACK_WORDS, NULL, 4U, protocol_stack, &task_controls[TASK_PROTOCOL]);
    if (protocol_task_handle == NULL ||
        (task_handles[TASK_PERIODIC] = xTaskCreateStatic(PeriodicTask,"can_periodic",UPPER_PERIODIC_STACK_WORDS,NULL,3U,periodic_stack,&task_controls[TASK_PERIODIC])) == NULL ||
#if UPPER_USART1_DEBUG_ENABLE
        (task_handles[TASK_LOG] = xTaskCreateStatic(LogTask,"uart_log",UPPER_LOG_STACK_WORDS,NULL,1U,log_stack,&task_controls[TASK_LOG])) == NULL ||
#endif
        (task_handles[TASK_OLED] = xTaskCreateStatic(OledTask,"can_oled",UPPER_OLED_STACK_WORDS,NULL,1U,oled_stack,&task_controls[TASK_OLED])) == NULL ||
        (task_handles[TASK_HEALTH] = xTaskCreateStatic(HealthTask,"health",UPPER_HEALTH_STACK_WORDS,NULL,5U,health_stack,&task_controls[TASK_HEALTH])) == NULL ||
        (task_handles[TASK_KEY] = xTaskCreateStatic(KeyTask,"key_scan",UPPER_KEY_STACK_WORDS,NULL,2U,key_stack,&task_controls[TASK_KEY])) == NULL) Error_Handler();
}
