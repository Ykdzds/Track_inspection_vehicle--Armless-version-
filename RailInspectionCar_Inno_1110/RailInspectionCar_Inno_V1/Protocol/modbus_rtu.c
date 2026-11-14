#include "modbus_rtu.h"

#include <string.h>

#include "queue.h"
#include "task.h"
#include "app_config.h"
#include "bsp_rs485.h"
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
#include "HMI/app_control_arbiter.h"
#include "aimtor_servo.h"
#endif
#if defined(__CC_ARM) || defined(__arm__)
#include "main.h"
#endif

#define MODBUS_REQUEST_QUEUE_LENGTH   (8U)
#define MODBUS_RESPONSE_QUEUE_LENGTH  (8U)
#define MODBUS_READ_MAX_REGISTERS     (125U)
#define MODBUS_WRITE_MAX_REGISTERS    (120U)

static QueueHandle_t s_request_queue;
static QueueHandle_t s_response_queue;
static QueueHandle_t s_safety_responses;
/* 单槽高优先级邮箱：普通队列满载不能阻塞保护核验或停车。 */
static modbus_request_t s_safety_request;
static volatile uint8_t s_safety_request_pending;
#define MODBUS_SAFETY_OWNER (0xFEU)
/* 0=普通业务，1=独立停车，2=完全静默，3=只读诊断。 */
static volatile uint8_t s_safety_mode;
static volatile uint8_t s_motion_permitted;

bool modbus_business_allowed(void) { return s_safety_mode == 0U; }
bool modbus_motion_permitted(void) { return s_motion_permitted && !s_safety_mode; }
void modbus_set_motion_permitted(bool permitted) { s_motion_permitted = permitted ? 1U : 0U; }

static modbus_stats_t s_stats;
static volatile uint32_t s_diagnostic_sequence;
static modbus_diagnostic_t s_diagnostic;
static uint32_t s_queue_overflows;

static volatile uint32_t s_cancel_epoch;

#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
typedef struct {
    uint32_t transaction, epoch, queued_ms;
    uint8_t motion_owner, valid;
} diag_enqueue_t;
typedef struct {
    uint32_t transaction, epoch, queued_ms, dequeued_ms;
    uint32_t tx_start_ms, tc_ms, rx_ms, validated_ms, consumed_ms;
    uint16_t tag, address, quantity, value;
    uint8_t axis, slave, function, motion_owner;
    uint8_t status, attempt, valid, raw_length;
    uint8_t raw[8];
    uint8_t request_owner;
} diag_transaction_t;
/* Two bounded sidecars, never payloads or extra queue descriptors. If a caller
 * queues several requests, unmatched enqueue stamps are explicitly unknown. */
static diag_enqueue_t s_enqueued[2];
static diag_transaction_t s_link_transaction;
static uint32_t s_link_unknown, s_link_late, s_link_response_drop, s_link_sample_at;
static uint32_t s_link_replaced, s_link_consume_sample_at;
typedef char rs485_diag_storage_limit[(sizeof(s_enqueued) + sizeof(s_link_transaction) +
    6U * sizeof(uint32_t) <= 120U) ? 1 : -1];
uint32_t modbus_diag_millis(void)
{
#if defined(__CC_ARM) || defined(__arm__)
    return HAL_GetTick();
#else
    return (uint32_t)((uint64_t)xTaskGetTickCount() * 1000U / configTICK_RATE_HZ);
#endif
}
#define diag_ms modbus_diag_millis
static void diag_enqueued(const modbus_request_t *r, uint8_t safety)
{
    diag_enqueue_t *e = &s_enqueued[safety];
    uint8_t ordinary=(uint8_t)(r->function==3U || (r->function==6U && r->address==0x0603U && r->value!=0U));
    uint8_t selected=(uint8_t)(!ordinary || !s_link_sample_at || (uint32_t)(diag_ms()-s_link_sample_at)>=200U);
    e->transaction=r->context.transaction_id; e->epoch=r->transport_epoch;
    e->queued_ms=diag_ms(); e->motion_owner=(uint8_t)app_control_arbiter_owner(); e->valid=(uint8_t)(1U|(selected<<1U));
    if(selected && ordinary) s_link_sample_at=e->queued_ms?e->queued_ms:1U;
    if(selected) app_link_diag_record(APP_LINK_RS485, 1U, e->transaction,e->epoch,e->queued_ms,
        ((uint32_t)e->motion_owner<<24U)|((uint32_t)r->context.owner<<16U)|r->context.tag,
        ((uint32_t)r->slave<<24U)|((uint32_t)r->context.axis<<16U)|r->address,
        ((uint32_t)r->function<<24U)|((uint32_t)r->quantity<<16U)|r->value);
    if(selected && r->function==6U && r->address==0x0603U)
        app_link_diag_record(APP_LINK_RS485,0x11U,e->transaction,e->epoch,
            ((uint32_t)e->motion_owner<<24U)|((uint32_t)safety<<16U)|r->context.tag,
            r->context.axis,r->value,e->queued_ms);
}
static void diag_dequeued(const modbus_request_t *r, uint8_t safety)
{
    diag_enqueue_t *e=&s_enqueued[safety];
    taskENTER_CRITICAL();
    memset(&s_link_transaction,0,sizeof(s_link_transaction));
    s_link_transaction.transaction=r->context.transaction_id;s_link_transaction.epoch=r->transport_epoch;
    s_link_transaction.dequeued_ms=diag_ms();s_link_transaction.tag=r->context.tag;
    s_link_transaction.address=r->address;s_link_transaction.quantity=r->quantity;
    s_link_transaction.value=r->value;s_link_transaction.axis=r->context.axis;
    s_link_transaction.slave=r->slave;s_link_transaction.function=r->function;
    s_link_transaction.request_owner=r->context.owner;
    s_link_transaction.motion_owner=0xFFU;
    if(e->valid && e->transaction==r->context.transaction_id && e->epoch==r->transport_epoch) {
        s_link_transaction.queued_ms=e->queued_ms;s_link_transaction.motion_owner=e->motion_owner;
        s_link_transaction.valid=(uint8_t)(1U|((e->valid&2U)<<5U));e->valid=0U;
    } else ++s_link_unknown;
    taskEXIT_CRITICAL();
    rs485_diag_begin();
}
static void diag_raw(const uint8_t *raw,uint16_t length)
{
    taskENTER_CRITICAL();
    s_link_transaction.raw_length=(uint8_t)(length>8U?8U:length);
    memset(s_link_transaction.raw,0,sizeof(s_link_transaction.raw));
    memcpy(s_link_transaction.raw,raw,s_link_transaction.raw_length);
    taskEXIT_CRITICAL();
}
static void diag_pack_transaction(uint32_t *words)
{
    words[0]=s_link_transaction.transaction;words[1]=s_link_transaction.epoch;
    words[2]=s_link_transaction.queued_ms;words[3]=s_link_transaction.dequeued_ms;
    words[4]=s_link_transaction.tx_start_ms;words[5]=s_link_transaction.tc_ms;
    words[6]=s_link_transaction.rx_ms;words[7]=s_link_transaction.validated_ms;
    words[8]=s_link_transaction.consumed_ms;
    words[9]=((uint32_t)s_link_transaction.tag<<16U)|s_link_transaction.address;
    words[10]=((uint32_t)s_link_transaction.quantity<<16U)|s_link_transaction.value;
    words[11]=((uint32_t)s_link_transaction.motion_owner<<24U)|((uint32_t)s_link_transaction.request_owner<<16U)|
        ((uint32_t)s_link_transaction.axis<<8U)|s_link_transaction.slave;
    words[12]=((uint32_t)s_link_transaction.function<<24U)|((uint32_t)s_link_transaction.status<<16U)|
        ((uint32_t)s_link_transaction.attempt<<8U)|s_link_transaction.valid;
    words[13]=s_link_transaction.raw_length;
    memcpy(&words[14],s_link_transaction.raw,8U);
}
static uint32_t diag_elapsed(uint32_t now)
{
    return now-((s_link_transaction.valid&1U)?s_link_transaction.queued_ms:s_link_transaction.dequeued_ms);
}
static modbus_status_t diag_submit_rejected(const modbus_request_t *r,modbus_status_t reason)
{
    /* Rejected requests have no accepted motion source or enqueue timestamp.
     * The common immutable record header supplies the exact refusal HAL time. */
    app_link_diag_freeze6(APP_LINK_RS485,0x14U,
        r?r->context.transaction_id:UINT32_MAX,s_cancel_epoch,
        ((uint32_t)reason<<16U)|(r?r->context.tag:0xFFFFU),
        0xFF000000UL|(r?((uint32_t)r->context.owner<<16U)|((uint32_t)r->context.axis<<8U)|1U:0x00FFFF00UL),
        r?((uint32_t)r->slave<<24U)|((uint32_t)r->function<<16U)|r->address:UINT32_MAX,
        r?((uint32_t)r->quantity<<16U)|r->value:UINT32_MAX);
    return reason;
}
static void diag_finished(modbus_status_t status,uint8_t attempt)
{
    rs485_diag_times_t times;
    uint32_t words[16];
    rs485_diag_times(&times);
    taskENTER_CRITICAL();
    s_link_transaction.tx_start_ms=times.start_ms;s_link_transaction.tc_ms=times.tc_ms;
    s_link_transaction.rx_ms=times.rx_ms;s_link_transaction.valid|=(uint8_t)(times.valid<<1U);
    s_link_transaction.validated_ms=diag_ms();s_link_transaction.valid|=0x10U;
    s_link_transaction.status=(uint8_t)status;s_link_transaction.attempt=attempt;
    taskEXIT_CRITICAL();
    if(status!=MODBUS_OK || (s_link_transaction.valid&0x40U) ||
       diag_elapsed(s_link_transaction.validated_ms)>=40U) {
        app_link_diag_record(APP_LINK_RS485,3U,s_link_transaction.transaction,
            s_link_transaction.queued_ms,s_link_transaction.tx_start_ms,
            s_link_transaction.tc_ms,s_link_transaction.rx_ms,s_link_transaction.valid);
        app_link_diag_record(APP_LINK_RS485,4U,s_link_transaction.transaction,s_link_transaction.epoch,
            ((uint32_t)s_link_transaction.motion_owner<<24U)|((uint32_t)attempt<<16U)|status,
            s_link_transaction.dequeued_ms,s_link_transaction.validated_ms,s_link_transaction.valid);
    }
    if(status!=MODBUS_OK || diag_elapsed(s_link_transaction.validated_ms)>=40U) {
        taskENTER_CRITICAL();diag_pack_transaction(words);taskEXIT_CRITICAL();
        app_link_diag_freeze(APP_LINK_RS485,status==MODBUS_OK?0x13U:0x12U,words,16U);
    }
}
void modbus_diag_consumed(const modbus_response_t *r,uint8_t accepted)
{
    uint32_t now=diag_ms(),epoch;uint8_t matched,selected=0U,slow=0U;
    if(r==NULL)return;
    taskENTER_CRITICAL();
    matched=(uint8_t)(r->context.transaction_id==s_link_transaction.transaction &&
        s_link_transaction.epoch==s_cancel_epoch && r->context.axis==s_link_transaction.axis &&
        r->context.tag==s_link_transaction.tag && r->context.owner==s_link_transaction.request_owner &&
        r->slave==s_link_transaction.slave && r->function==s_link_transaction.function);
    epoch=s_cancel_epoch;
    if(matched && accepted) {
        s_link_transaction.consumed_ms=now;s_link_transaction.valid|=0x20U;
        slow=(uint8_t)(diag_elapsed(now)>=40U);
        selected=(uint8_t)((s_link_transaction.valid&0x40U)!=0U);
    } else if(!accepted)++s_link_late;
    else {
        /* The bounded transport sidecar may already describe the next normal
         * or safety request.  A consumer's accepted result remains authoritative;
         * never borrow the newer request's sample flag or elapsed time. */
        ++s_link_replaced;selected=0U;
    }
    if(accepted && r->status==MODBUS_OK && selected && !slow) {
        if(!s_link_consume_sample_at || (uint32_t)(now-s_link_consume_sample_at)>=200U)
            s_link_consume_sample_at=now?now:1U;
        else selected=0U;
    }
    taskEXIT_CRITICAL();
    if(!accepted || r->status!=MODBUS_OK || selected || slow)
        app_link_diag_record(APP_LINK_RS485,5U,r->context.transaction_id,now,
            ((uint32_t)accepted<<16U)|matched,((uint32_t)r->context.axis<<16U)|r->context.tag,
            epoch,r->status);
}
void modbus_diag_speed(const modbus_response_t *r,int16_t previous,int16_t command,
                       uint8_t command_valid,uint16_t state)
{
    uint32_t words[16];
    if(r->status!=MODBUS_OK || r->function!=3U || r->data_length!=3U || r->data[0]!=2U ||
       r->data[1]!=0U || r->data[2]!=0U || (previous==0 && (!command_valid || command==0))) return;
    memset(words,0,sizeof(words));
    words[0]=r->context.transaction_id;words[1]=diag_ms();words[2]=s_cancel_epoch;
    words[3]=((uint32_t)(uint16_t)previous<<16U)|(uint16_t)command;
    words[4]=((uint32_t)command_valid<<24U)|((uint32_t)r->context.axis<<16U)|state;
    words[5]=((uint32_t)app_control_arbiter_owner()<<24U)|((uint32_t)r->context.owner<<16U)|r->context.tag;
    taskENTER_CRITICAL();
    if(s_link_transaction.transaction==r->context.transaction_id && s_link_transaction.epoch==s_cancel_epoch) {
        words[6]=s_link_transaction.queued_ms;words[7]=s_link_transaction.dequeued_ms;
        words[8]=s_link_transaction.tx_start_ms;words[9]=s_link_transaction.tc_ms;
        words[10]=s_link_transaction.rx_ms;words[11]=s_link_transaction.validated_ms;
        words[12]=s_link_transaction.consumed_ms;
        words[13]=((uint32_t)s_link_transaction.valid<<24U)|((uint32_t)s_link_transaction.raw_length<<16U)|r->address;
        memcpy(&words[14],s_link_transaction.raw,8U);
    } else words[13]=r->address; /* Unknown timing/raw is never borrowed from another transaction. */
    taskEXIT_CRITICAL();
    app_link_diag_freeze(APP_LINK_RS485,0x10U,words,16U);
    app_link_diag_record(APP_LINK_RS485,0x10U,words[0],words[3],words[4],words[5],words[13],words[2]);
}
#endif

#if !(APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED)
#define diag_submit_rejected(r,reason) (reason)
#endif

void modbus_cancel_pending(void)
{
    taskENTER_CRITICAL();
    s_cancel_epoch++;
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    s_enqueued[0].valid=s_enqueued[1].valid=0U;
    app_link_diag_record(APP_LINK_RS485,6U,s_link_transaction.transaction,s_cancel_epoch,
        s_safety_mode,(uint32_t)app_control_arbiter_owner(),diag_ms(),0U);
#endif
    s_safety_request_pending = 0U;
    rs485_inhibit_transmit(1U);
    if (s_request_queue != NULL)
    {
        /* 调用 FreeRTOS 的 xQueueReset()，把队列 s_request_queue 清空，并显式忽略它的返回值 */
        (void)xQueueReset(s_request_queue);
    }
    taskEXIT_CRITICAL();
}

void modbus_resume_transport(void)
{
    if (s_safety_mode == 0U) rs485_inhibit_transmit(0U);
}

static void modbus_safety_mode(uint8_t mode)
{
    taskENTER_CRITICAL();
    s_safety_mode = mode;
    s_motion_permitted = 0U;
    modbus_cancel_pending();
    if (s_response_queue) (void)xQueueReset(s_response_queue);
    if (s_safety_responses) (void)xQueueReset(s_safety_responses);
    taskEXIT_CRITICAL();
}
void modbus_safety_takeover(void) { modbus_safety_mode(1U); }
void modbus_safety_silence(void) { modbus_safety_mode(2U); }
void modbus_safety_diagnostics(void) { modbus_safety_mode(3U); }
void modbus_safety_release(void) { modbus_safety_mode(0U); modbus_resume_transport(); }
bool modbus_safety_get_response(modbus_response_t *response)
{
    bool received=s_safety_responses && xQueueReceive(s_safety_responses,response,0U)==pdTRUE;
    return received;
}
modbus_status_t modbus_safety_submit(const modbus_request_t *request)
{
    modbus_request_t copy;
    BaseType_t sent;
    if (!request || !s_request_queue || request->slave == 0U || request->slave > 247U ||
        (request->function != 3U && request->function != 6U) ||
        (request->function == 3U && request->quantity != 1U)) return diag_submit_rejected(request,MODBUS_INVALID_ARGUMENT);
    copy = *request;
    /* 安全通道只允许H06_06斜坡与H06_03零速，不能清故障或写使能。 */
    if (copy.function == 6U &&
        !((copy.address == 0x0606U && copy.value > 0U) ||
          (copy.address == 0x0603U && copy.value == 0U))) return diag_submit_rejected(request,MODBUS_INVALID_ARGUMENT);
    copy.context.owner = MODBUS_SAFETY_OWNER;
    copy.retries = 0U;
    copy.timeout_ms = APP_MODBUS_RESPONSE_TIMEOUT_MS;
    taskENTER_CRITICAL();
    if (s_safety_mode == 2U || (s_safety_mode == 3U && copy.function != 3U))
    { taskEXIT_CRITICAL(); return diag_submit_rejected(request,MODBUS_TRANSPORT_ERROR); }
    copy.transport_epoch = s_cancel_epoch;
    sent = s_safety_request_pending ? pdFALSE : pdTRUE;
    if (sent == pdTRUE)
    {
        s_safety_request = copy; s_safety_request_pending = 1U;
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
        diag_enqueued(&copy,1U);
#endif
    }
    taskEXIT_CRITICAL();
    return sent == pdTRUE ? MODBUS_OK : diag_submit_rejected(request,MODBUS_QUEUE_FULL);
}

// 根据功能码统一标识事务类型，便于响应解析和上层分发。
static modbus_operation_t modbus_get_operation(uint8_t function)
{
    if (function == 0x03U)
    {
        return MODBUS_OPERATION_READ_HOLDING;
    }
    if (function == 0x06U)
    {
        return MODBUS_OPERATION_WRITE_SINGLE;
    }
    if (function == 0x10U)
    {
        return MODBUS_OPERATION_WRITE_MULTIPLE;
    }
    return MODBUS_OPERATION_NONE;
}

// 计算Modbus RTU CRC16，返回值发送时按低字节在前排列。
uint16_t modbus_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc;
    uint16_t index;
    uint8_t bit;

    crc = 0xFFFFU;
    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

// 按Modbus RTU格式组帧，并在帧尾追加CRC校验。
static uint16_t modbus_build_request(const modbus_request_t *request,
                                     uint8_t *frame)
{
    uint16_t crc;
    uint16_t length;

    frame[0] = request->slave;
    frame[1] = request->function;
    length = 2U;
    if ((request->function == 0x03U) || (request->function == 0x04U))
    {
        frame[2] = (uint8_t)(request->address >> 8U);
        frame[3] = (uint8_t)request->address;
        frame[4] = (uint8_t)(request->quantity >> 8U);
        frame[5] = (uint8_t)request->quantity;
        length = 6U;
    }
    else if (request->function == 0x06U)
    {
        frame[2] = (uint8_t)(request->address >> 8U);
        frame[3] = (uint8_t)request->address;
        frame[4] = (uint8_t)(request->value >> 8U);
        frame[5] = (uint8_t)request->value;
        length = 6U;
    }
    else if ((request->function == 0x10U) &&
             (request->payload_length > 0U))
    {
        memcpy(&frame[2], request->payload, request->payload_length);
        length = (uint16_t)(2U + request->payload_length);
    }
    else
    {
        return 0U;
    }
    crc = modbus_crc16(frame, length);
    frame[length] = (uint8_t)crc;
    frame[length + 1U] = (uint8_t)(crc >> 8U);
    return (uint16_t)(length + 2U);
}

// 根据已接收的功能码和字节数推断完整响应长度。
static uint16_t modbus_expected_length(const uint8_t *frame, uint16_t length)
{
    if (length < 2U)
    {
        return 0U;
    }
    if ((frame[1] & 0x80U) != 0U)
    {
        return 5U;
    }
    if ((frame[1] == 0x03U) || (frame[1] == 0x04U))
    {
        if (length < 3U)
        {
            return 0U;
        }
        return (uint16_t)(frame[2] + 5U);
    }
    if ((frame[1] == 0x06U) || (frame[1] == 0x10U))
    {
        return 8U;
    }
    return 0U;
}

// 校验CRC、从站地址、功能码及写操作回显，拒绝不匹配的响应。
static modbus_status_t modbus_validate_response(
    const modbus_request_t *request,
    const uint8_t *frame,
    uint16_t length,
    modbus_response_t *response)
{
    uint16_t crc;

    if (length < 5U)
    {
        s_stats.frame_errors++;
        return MODBUS_FRAME_ERROR;
    }
    crc = modbus_crc16(frame, (uint16_t)(length - 2U));
    if ((frame[length - 2U] != (uint8_t)crc) ||
        (frame[length - 1U] != (uint8_t)(crc >> 8U)))
    {
        s_stats.crc_errors++;
        return MODBUS_CRC_ERROR;
    }
    if (frame[0] != request->slave)
    {
        return MODBUS_ADDRESS_ERROR;
    }
    if ((frame[1] & 0x7FU) != request->function)
    {
        return MODBUS_FUNCTION_ERROR;
    }
    response->slave = frame[0];
    response->function = frame[1];
    if ((frame[1] & 0x80U) != 0U)
    {
        response->exception_code = frame[2];
        s_stats.exceptions++;
        return MODBUS_EXCEPTION;
    }
    if ((request->function == 0x03U) &&
        (frame[2] != (uint8_t)(request->quantity * 2U)))
    {
        s_stats.frame_errors++;
        return MODBUS_FRAME_ERROR;
    }
    if ((request->function == 0x06U) &&
        ((frame[2] != (uint8_t)(request->address >> 8U)) ||
         (frame[3] != (uint8_t)request->address) ||
         (frame[4] != (uint8_t)(request->value >> 8U)) ||
         (frame[5] != (uint8_t)request->value)))
    {
        s_stats.frame_errors++;
        return MODBUS_FRAME_ERROR;
    }
    if ((request->function == 0x10U) &&
        ((frame[2] != (uint8_t)(request->address >> 8U)) ||
         (frame[3] != (uint8_t)request->address) ||
         (frame[4] != (uint8_t)(request->quantity >> 8U)) ||
         (frame[5] != (uint8_t)request->quantity)))
    {
        s_stats.frame_errors++;
        return MODBUS_FRAME_ERROR;
    }
    response->data_length = (uint16_t)(length - 4U);
    memcpy(response->data, &frame[2], response->data_length);
    return MODBUS_OK;
}

// 在响应超时和帧间隔两个条件约束下接收一帧Modbus响应。
static modbus_status_t modbus_wait_frame(const modbus_request_t *request,
                                         modbus_response_t *response)
{
    uint8_t frame[APP_MODBUS_MAX_FRAME_SIZE];
    uint16_t length;
    uint16_t expected;
    TickType_t start;
    TickType_t last_byte;
    TickType_t timeout;
    modbus_status_t last_status;
    modbus_status_t status;

    length = 0U;
    expected = 0U;
    start = xTaskGetTickCount();
    last_byte = start;
    last_status = MODBUS_TIMEOUT;
    timeout = pdMS_TO_TICKS(request->timeout_ms == 0U ?
                         APP_MODBUS_RESPONSE_TIMEOUT_MS : request->timeout_ms);
    for (;;)
    {
        if ((xTaskGetTickCount() - start) >= timeout)
        {
            if (length > 0U)
            {
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
                diag_raw(frame,length);
#endif
                s_stats.frame_errors++;
                return MODBUS_FRAME_ERROR;
            }
            if (last_status == MODBUS_TIMEOUT)
            {
                s_stats.timeouts++;
            }
            return last_status;
        }
        if ((length < APP_MODBUS_MAX_FRAME_SIZE) &&
            (rs485_read(&frame[length], 1U) > 0U))
        {
            length++;
            last_byte = xTaskGetTickCount();
            expected = modbus_expected_length(frame, length);
            if ((expected > 0U) && (length >= expected))
            {
                status = modbus_validate_response(request, frame, length, response);
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
                diag_raw(frame,length);
                if(status!=MODBUS_OK && status!=MODBUS_EXCEPTION)
                    diag_finished(status,s_link_transaction.attempt);
#endif
                if ((status == MODBUS_OK) || (status == MODBUS_EXCEPTION))
                {
                    return status;
                }
                /* 粘包中的无关帧或坏帧不能遮蔽后续合法响应。 */
                last_status = status;
                length = 0U;
                expected = 0U;
                continue;
            }
            /* 缓冲中已有字节时连续取完，不能每取一字节就等待一个系统节拍。 */
            if (length < APP_MODBUS_MAX_FRAME_SIZE)
            {
                continue;
            }
        }
        if ((length > 0U) &&
            ((xTaskGetTickCount() - last_byte) >=
             pdMS_TO_TICKS(APP_MODBUS_FRAME_GAP_MS)))
        {
            s_stats.frame_errors++;
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
            diag_raw(frame,length);
#endif
            last_status = MODBUS_FRAME_ERROR;
            length = 0U;
            expected = 0U;
        }
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1U));
    }
}

modbus_status_t modbus_init(void)
{
    s_safety_mode = s_motion_permitted = 0U;
    s_cancel_epoch = 0U;
    s_safety_request_pending = 0U;
    memset(&s_stats, 0, sizeof(s_stats));
    memset(&s_diagnostic,0,sizeof(s_diagnostic));s_diagnostic_sequence=s_queue_overflows=0U;
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    memset(s_enqueued,0,sizeof(s_enqueued));
    memset(&s_link_transaction,0,sizeof(s_link_transaction));
    s_link_unknown=s_link_late=s_link_response_drop=s_link_sample_at=0U;
    s_link_replaced=s_link_consume_sample_at=0U;
#endif
    s_request_queue = xQueueCreate(MODBUS_REQUEST_QUEUE_LENGTH,
                                   sizeof(modbus_request_t));
    s_response_queue = xQueueCreate(MODBUS_RESPONSE_QUEUE_LENGTH,
                                    sizeof(modbus_response_t));
    s_safety_responses = xQueueCreate(MODBUS_RESPONSE_QUEUE_LENGTH, sizeof(modbus_response_t));
    if ((s_request_queue == NULL) || (s_response_queue == NULL) || (s_safety_responses == NULL))
    {
        if (s_request_queue != NULL)
        {
            vQueueDelete(s_request_queue);
            s_request_queue = NULL;
        }
        if (s_response_queue != NULL)
        {
            vQueueDelete(s_response_queue);
            s_response_queue = NULL;
        }
        if (s_safety_responses) { vQueueDelete(s_safety_responses); s_safety_responses = NULL; }
        return MODBUS_NOT_INITIALIZED;
    }
    return MODBUS_OK;
}

modbus_status_t modbus_submit(const modbus_request_t *request,
                              TickType_t wait_ticks)
{
    if ((request == NULL) || (request->slave > 247U) ||
        (request->payload_length > sizeof(request->payload)) ||
        (modbus_get_operation(request->function) == MODBUS_OPERATION_NONE) ||
        (request->retries > APP_MODBUS_MAX_RETRIES) ||
        (request->timeout_ms > APP_MODBUS_RESPONSE_TIMEOUT_MS))
    {
        return diag_submit_rejected(request,MODBUS_INVALID_ARGUMENT);
    }
    if ((request->function == 0x03U) &&
        ((request->slave == 0U) || (request->quantity == 0U) ||
         (request->quantity > MODBUS_READ_MAX_REGISTERS)))
    {
        return diag_submit_rejected(request,MODBUS_INVALID_ARGUMENT);
    }
    if ((request->function != 0x06U) &&
        ((uint32_t)request->address + request->quantity > 65536UL))
    {
        return diag_submit_rejected(request,MODBUS_INVALID_ARGUMENT);
    }
    if ((request->function == 0x10U) &&
        ((request->quantity == 0U) ||
         (request->quantity > MODBUS_WRITE_MAX_REGISTERS) ||
         (request->payload_length != 5U + request->quantity * 2U) ||
         (request->payload[0] != (uint8_t)(request->address >> 8U)) ||
         (request->payload[1] != (uint8_t)request->address) ||
         (request->payload[2] != (uint8_t)(request->quantity >> 8U)) ||
         (request->payload[3] != (uint8_t)request->quantity) ||
         (request->payload[4] != (uint8_t)(request->quantity * 2U))))
    {
        return diag_submit_rejected(request,MODBUS_INVALID_ARGUMENT);
    }
    if (s_request_queue == NULL)
    {
        return diag_submit_rejected(request,MODBUS_NOT_INITIALIZED);
    }
    /* 入队不能阻塞：检查与盖章必须与安全接管原子互斥。 */
    {
        modbus_request_t copy = *request;
        BaseType_t sent;
        (void)wait_ticks;
        taskENTER_CRITICAL();
        if (s_safety_mode || request->context.owner == MODBUS_SAFETY_OWNER)
        { taskEXIT_CRITICAL(); return diag_submit_rejected(request,MODBUS_TRANSPORT_ERROR); }
        copy.transport_epoch = s_cancel_epoch;
        sent = xQueueSend(s_request_queue, &copy, 0U);
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
        if(sent==pdTRUE) diag_enqueued(&copy,0U);
#endif
        taskEXIT_CRITICAL();
        if (sent != pdTRUE) { ++s_queue_overflows; return diag_submit_rejected(request,MODBUS_QUEUE_FULL); }
    }
    return MODBUS_OK;
}

static void modbus_prepare_request(modbus_request_t *request,
                                   const modbus_request_context_t *context,
                                   uint8_t slave,
                                   uint8_t function,
                                   uint16_t address)
{
    memset(request, 0, sizeof(*request));
    if (context != NULL)
    {
        request->context = *context;
    }
    request->slave = slave;
    request->function = function;
    request->address = address;
    request->retries = APP_MODBUS_MAX_RETRIES;
    if (function != 0x03U)
    {
        /* 写入可能触发动作，丢失应答时禁止自动重复执行。 */
        request->retries = 0U;
    }
    request->timeout_ms = APP_MODBUS_RESPONSE_TIMEOUT_MS;
}

modbus_status_t modbus_read_holding(
    const modbus_request_context_t *context,
    uint8_t slave,
    uint16_t address,
    uint16_t count,
    TickType_t wait_ticks)
{
    modbus_request_t request;

    if ((slave == 0U) || (slave > 247U) || (count == 0U) ||
        (count > MODBUS_READ_MAX_REGISTERS))
    {
        return MODBUS_INVALID_ARGUMENT;
    }
    modbus_prepare_request(&request, context, slave, 0x03U, address);
    request.quantity = count;
    return modbus_submit(&request, wait_ticks);
}

modbus_status_t modbus_write_single(
    const modbus_request_context_t *context,
    uint8_t slave,
    uint16_t address,
    uint16_t value,
    TickType_t wait_ticks)
{
    modbus_request_t request;

    if (slave > 247U)
    {
        return MODBUS_INVALID_ARGUMENT;
    }
    modbus_prepare_request(&request, context, slave, 0x06U, address);
    request.value = value;
    return modbus_submit(&request, wait_ticks);
}

modbus_status_t modbus_write_multiple(
    const modbus_request_context_t *context,
    uint8_t slave,
    uint16_t address,
    const uint16_t *values,
    uint16_t count,
    TickType_t wait_ticks)
{
    modbus_request_t request;
    uint16_t index;

    if ((slave > 247U) || (values == NULL) ||
        (count == 0U) || (count > MODBUS_WRITE_MAX_REGISTERS))
    {
        return MODBUS_INVALID_ARGUMENT;
    }
    modbus_prepare_request(&request, context, slave, 0x10U, address);
    request.quantity = count;
    request.payload[0] = (uint8_t)(address >> 8U);
    request.payload[1] = (uint8_t)address;
    request.payload[2] = (uint8_t)(count >> 8U);
    request.payload[3] = (uint8_t)count;
    request.payload[4] = (uint8_t)(count * 2U);
    for (index = 0U; index < count; index++)
    {
        request.payload[5U + index * 2U] = (uint8_t)(values[index] >> 8U);
        request.payload[6U + index * 2U] = (uint8_t)values[index];
    }
    request.payload_length = (uint16_t)(5U + count * 2U);
    return modbus_submit(&request, wait_ticks);
}

bool modbus_get_response(modbus_response_t *response, TickType_t wait_ticks)
{
    if ((response == NULL) || (s_response_queue == NULL))
    {
        return false;
    }
    return xQueueReceive(s_response_queue, response, wait_ticks) == pdTRUE;
}

// 通信任务串行执行事务，保证RS485总线上同一时刻只有一个请求。
void modbus_process(void)
{
    modbus_request_t request;
    modbus_response_t response;
    uint8_t frame[APP_MODBUS_MAX_FRAME_SIZE];
    uint16_t length;
    uint8_t attempt = 0U;
    uint8_t retries;
    uint8_t discard[32];
    uint16_t remaining;
    uint16_t count;
    uint32_t epoch;
    uint8_t safety_request;

    /* 初始化失败时保持通信任务可运行，由健康监控维持系统SAFE。 */
    if ((s_request_queue == NULL) || (s_response_queue == NULL))
    {
        vTaskDelay(pdMS_TO_TICKS(20U));
        return;
    }
    taskENTER_CRITICAL();
    safety_request = s_safety_request_pending;
    if (safety_request)
    { request = s_safety_request; s_safety_request_pending = 0U; }
    taskEXIT_CRITICAL();
    if (!safety_request && xQueueReceive(s_request_queue, &request,
                      pdMS_TO_TICKS(1U)) != pdTRUE) return;
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    diag_dequeued(&request,safety_request);
#endif
    if (request.transport_epoch != s_cancel_epoch || s_safety_mode == 2U ||
        (s_safety_mode && request.context.owner != MODBUS_SAFETY_OWNER) ||
        (s_safety_mode == 3U && request.function != 3U)) {
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
        app_link_diag_record(APP_LINK_RS485,7U,request.context.transaction_id,
            request.transport_epoch,s_cancel_epoch,diag_ms(),s_safety_mode,0U);
#endif
        return;
    }
    epoch = request.transport_epoch;
    memset(&response, 0, sizeof(response));
    response.context = request.context;
    response.operation = modbus_get_operation(request.function);
    response.slave = request.slave;
    response.function = request.function;
    response.address = request.address;
    response.quantity = request.quantity;
    response.value = request.value;
    s_stats.requests++;
    length = modbus_build_request(&request, frame);
    if (length == 0U)
    {
        response.status = MODBUS_INVALID_ARGUMENT;
    }
    else
    {
        retries = request.retries;
        for (attempt = 0U; attempt <= retries; attempt++)
        {
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
            rs485_diag_begin();
            taskENTER_CRITICAL();
            s_link_transaction.raw_length=0U;
            memset(s_link_transaction.raw,0,sizeof(s_link_transaction.raw));
            s_link_transaction.valid&=0x41U;
            s_link_transaction.attempt=attempt;
            taskEXIT_CRITICAL();
#endif
            /* 只清除当前积存字节，持续噪声不能让通信任务永久停在这里。 */
            remaining = rs485_available();
            while (remaining > 0U)
            {
                count = rs485_read(discard, remaining > sizeof(discard) ?
                                  sizeof(discard) : remaining);
                if (count == 0U)
                {
                    break;
                }
                remaining = (uint16_t)(remaining - count);
            }
            vTaskDelay(pdMS_TO_TICKS(APP_MODBUS_FRAME_GAP_MS));
            if (epoch != s_cancel_epoch)
            {
                response.status = MODBUS_TRANSPORT_ERROR;
                break;
            }
            /* 只有唯一总线任务可以在代次核验后打开TX；安全接管只关闭TX。 */
            taskENTER_CRITICAL();
            if (epoch == s_cancel_epoch && s_safety_mode != 2U)
                rs485_inhibit_transmit(0U);
            taskEXIT_CRITICAL();
            if (rs485_send(frame, length) != RS485_OK)
            {
                response.status = MODBUS_TRANSPORT_ERROR;
            }
            else if (request.slave == 0U)
            {
                response.status = MODBUS_OK;
                break;
            }
            else
            {
                response.status = modbus_wait_frame(&request, &response);
                if ((response.status == MODBUS_OK) ||
                    (response.status == MODBUS_EXCEPTION))
                {
                    break;
                }
            }
            if (attempt < retries)
            {
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
                diag_finished(response.status,attempt);
#endif
                if (epoch != s_cancel_epoch)
                {
                    break;
                }
                s_stats.retries++;
            }
        }
    }
    if ((response.status == MODBUS_OK) && (request.slave != 0U))
    {
        s_stats.responses++;
    }
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    diag_finished(response.status,(uint8_t)(attempt>request.retries?request.retries:attempt));
#endif
    if(response.status!=MODBUS_OK) {
        ++s_diagnostic_sequence;
#if defined(__CC_ARM) || defined(__arm__)
        __DMB();
#endif
        /* Wire diagnostics use HAL milliseconds like telemetry and retained faults. */
#if defined(__CC_ARM) || defined(__arm__)
        s_diagnostic.ms=HAL_GetTick();
#else
        s_diagnostic.ms=(uint32_t)((uint64_t)xTaskGetTickCount()*1000U/configTICK_RATE_HZ);
#endif
        s_diagnostic.status=response.status;s_diagnostic.transaction=request.context.transaction_id;
        s_diagnostic.tag=request.context.tag;s_diagnostic.owner=request.context.owner;
        s_diagnostic.axis=request.context.axis;s_diagnostic.slave=request.slave;
        s_diagnostic.function=request.function;s_diagnostic.address=request.address;
        s_diagnostic.transport_epoch=epoch;
        s_diagnostic.attempt=(uint32_t)(attempt>request.retries?request.retries:attempt);
        ++s_diagnostic.trace_total;
        if(s_diagnostic.trace_total>1U)++s_diagnostic.trace_overwritten;
#if defined(__CC_ARM) || defined(__arm__)
        __DMB();
#endif
        ++s_diagnostic_sequence;
    }
    if (epoch == s_cancel_epoch) {
        BaseType_t sent=xQueueSend(request.context.owner == MODBUS_SAFETY_OWNER ?
            s_safety_responses : s_response_queue, &response, 0U);
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
        if(sent!=pdTRUE) {
            ++s_link_response_drop;
            app_link_diag_record(APP_LINK_RS485,7U,request.context.transaction_id,
                epoch,s_cancel_epoch,diag_ms(),s_safety_mode,1U);
        }
#else
        (void)sent;
#endif
    }
}

void modbus_get_stats(modbus_stats_t *stats)
{
    if (stats != NULL)
    {
        taskENTER_CRITICAL();
        *stats = s_stats;
        taskEXIT_CRITICAL();
    }
}

void modbus_get_diagnostic(modbus_diagnostic_t *out)
{
    unsigned retry;uint32_t before,after;
    for(retry=0U;retry<3U;retry++) {
        before=s_diagnostic_sequence;
        if(before&1U) continue;
#if defined(__CC_ARM) || defined(__arm__)
        __DMB();
#endif
        *out=s_diagnostic;
#if defined(__CC_ARM) || defined(__arm__)
        __DMB();
#endif
        after=s_diagnostic_sequence;
        if(before==after) {out->queue_overflows=s_queue_overflows;return;}
    }
    memset(out,0xFF,sizeof(*out));out->queue_overflows=s_queue_overflows;
}

#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
void app_rs485_diag_snapshot(app_link_diag_snapshot_t *out,uint8_t phase)
{
    out->sampled_ms=diag_ms();out->count=0U;
    if(phase==0U) {aimtor_rs485_diag_snapshot(out);return;}
    if(phase==1U) {
        rs485_stats_t bus;
        rs485_get_stats(&bus);
        out->words[0]=bus.rx_bytes;out->words[1]=bus.tx_bytes;
        out->words[2]=bus.rx_overflows;out->words[3]=bus.uart_errors;
        out->words[4]=bus.tx_timeouts;out->words[5]=bus.rx_restarts;
        out->words[6]=bus.dma_start_failures;out->words[7]=bus.rx_abort_failures;
        out->words[8]=bus.uart_error_flags;out->words[9]=bus.hal_error_flags;
        out->words[10]=bus.uart_sr;out->words[11]=bus.uart_cr1;out->words[12]=bus.uart_cr3;
        out->words[13]=bus.dma_control;
        out->words[14]=((uint32_t)bus.dma_remaining<<16U)|bus.dma_position;
        out->words[15]=((uint32_t)bus.dma_running<<24U)|((uint32_t)bus.recovery_pending<<16U)|bus.dma_last_position;
        taskENTER_CRITICAL();
        out->words[16]=s_stats.requests;out->words[17]=s_stats.responses;
        out->words[18]=s_stats.timeouts;out->words[19]=s_stats.retries;
        out->words[20]=s_stats.crc_errors;out->words[21]=s_stats.frame_errors;
        out->words[22]=s_stats.exceptions;out->words[23]=s_queue_overflows;
        taskEXIT_CRITICAL();out->count=24U;
    } else {
        taskENTER_CRITICAL();
        diag_pack_transaction(out->words);
        out->words[16]=s_link_unknown;out->words[17]=s_link_late;out->words[18]=s_link_response_drop;
        out->words[19]=s_cancel_epoch;out->words[20]=s_safety_mode;
        out->words[21]=s_link_replaced;
        taskEXIT_CRITICAL();out->count=22U;
    }
}
#endif
