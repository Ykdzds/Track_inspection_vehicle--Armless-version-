#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "modbus_rtu.h"
#include "bsp_rs485.h"
#include "HMI/app_control_arbiter.h"
#include "aimtor_servo.h"
#include "queue.h"
#include "task.h"

typedef struct { unsigned capacity,size,head,tail,count; unsigned char bytes[]; } queue_t;
static uint32_t ticks, motion_owner=APP_CONTROL_OWNER_HMI;
static uint8_t wire[16], offset, length, bad_crc, no_reply, missing_tc;
static rs485_diag_times_t timing;
static uint32_t first[16];static uint8_t first_count;static uint16_t first_kind;
static uint32_t enqueue_owner, consumed_ok, emitted,consume_events;
TickType_t xTaskGetTickCount(void) {return ticks;}
void vTaskDelay(TickType_t n) {ticks+=n;}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t n) {(void)clear;ticks+=n;return 0U;}
QueueHandle_t xQueueCreate(unsigned capacity,unsigned size) {
    queue_t *q=calloc(1,sizeof(*q)+capacity*size);assert(q);q->capacity=capacity;q->size=size;return q;
}
void vQueueDelete(QueueHandle_t p){free(p);}
BaseType_t xQueueReset(QueueHandle_t p){queue_t *q=p;q->head=q->tail=q->count=0;return pdTRUE;}
BaseType_t xQueueSend(QueueHandle_t p,const void *v,TickType_t wait) {
    queue_t *q=p;(void)wait;if(q->count==q->capacity)return pdFALSE;
    memcpy(q->bytes+q->head*q->size,v,q->size);q->head=(q->head+1)%q->capacity;q->count++;return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t p,void *v,TickType_t wait) {
    queue_t *q=p;if(!q->count){ticks+=wait;return pdFALSE;}
    memcpy(v,q->bytes+q->tail*q->size,q->size);q->tail=(q->tail+1)%q->capacity;q->count--;return pdTRUE;
}
app_control_owner_t app_control_arbiter_owner(void){return (app_control_owner_t)motion_owner;}
void app_link_diag_record(uint8_t source,uint16_t kind,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f) {
    assert(source==APP_LINK_RS485);emitted++;(void)a;(void)b;(void)c;(void)e;(void)f;
    if(kind==1U)enqueue_owner=d>>24U;if(kind==5U){consumed_ok=c;++consume_events;}
}
void app_link_diag_freeze(uint8_t source,uint16_t kind,const uint32_t *words,uint8_t count) {
    assert(source==APP_LINK_RS485 && count<=16U);if(first_count)return;
    memcpy(first,words,count*4U);first_count=count;first_kind=kind;
}
void app_link_diag_freeze6(uint8_t source,uint16_t kind,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f) {
    uint32_t words[6]={a,b,c,d,e,f};app_link_diag_freeze(source,kind,words,6U);
}
void aimtor_rs485_diag_snapshot(app_link_diag_snapshot_t *out){out->words[0]=0xABC;out->count=1;}
void rs485_diag_begin(void){memset(&timing,0,sizeof(timing));}
void rs485_diag_times(rs485_diag_times_t *out){*out=timing;}
void rs485_inhibit_transmit(uint8_t inhibit){(void)inhibit;}
uint16_t rs485_available(void){return (uint16_t)(length-offset);}
uint16_t rs485_read(uint8_t *data,uint16_t capacity) {
    if(!capacity || offset==length)return 0;*data=wire[offset++];return 1;
}
void rs485_get_stats(rs485_stats_t *out){memset(out,0,sizeof(*out));out->uart_errors=7U;}
rs485_status_t rs485_send(const uint8_t *data,uint16_t size) {
    uint16_t crc;(void)size;timing.start_ms=++ticks;timing.valid=1U;
    if(!missing_tc){timing.tc_ms=++ticks;timing.valid|=2U;}
    offset=length=0U;if(no_reply)return RS485_OK;
    if(data[1]==3U){wire[0]=data[0];wire[1]=3U;wire[2]=2U;wire[3]=wire[4]=0U;length=7U;}
    else {memcpy(wire,data,6U);length=8U;}
    crc=modbus_crc16(wire,(uint16_t)(length-2U));
    wire[length-2U]=(uint8_t)crc;wire[length-1U]=(uint8_t)(crc>>8U);
    if(bad_crc)wire[length-1U]^=1U;
    timing.rx_ms=++ticks;timing.valid|=4U;return RS485_OK;
}
static modbus_request_t request(uint32_t id) {
    modbus_request_t r;memset(&r,0,sizeof(r));r.context.transaction_id=id;r.context.owner=1U;
    r.context.axis=0U;r.context.tag=2U;r.slave=1U;r.function=3U;r.address=0x0B00U;r.quantity=1U;
    return r;
}
static modbus_response_t run(modbus_request_t *r) {
    modbus_response_t response;assert(modbus_submit(r,0)==MODBUS_OK);ticks+=3U;modbus_process();
    assert(modbus_get_response(&response,0));return response;
}
int main(void) {
    modbus_request_t r;modbus_response_t response;app_link_diag_snapshot_t state;
    assert(modbus_init()==MODBUS_OK);ticks=100U;
    r=request(10U);response=run(&r);assert(response.status==MODBUS_OK && enqueue_owner==APP_CONTROL_OWNER_HMI);
    modbus_diag_consumed(&response,1U);assert(consumed_ok==0x10001U);
    app_rs485_diag_snapshot(&state,2U);
    assert(state.words[0]==10U && state.words[2]==100U && state.words[3]==103U);
    assert(state.words[4]<state.words[5] && state.words[5]<state.words[6]);
    assert((state.words[12]&0x3FU)==0x3FU && state.words[13]==7U);
    modbus_diag_speed(&response,700,700,1U,0U);assert(first_kind==0x10U && first_count==16U);
    assert(first[3]==((700U<<16U)|700U) && (first[13]>>16U&0xFFU)==7U);
    assert(!memcmp(&first[14],wire,7U));
    /* An invalid reply cannot invent a valid zero sample or overwrite first evidence. */
    response.status=MODBUS_TIMEOUT;modbus_diag_speed(&response,999,999,1U,0U);assert(first[0]==10U);
    first_count=0U;bad_crc=1U;r=request(11U);response=run(&r);
    assert(response.status==MODBUS_CRC_ERROR && first_kind==0x12U && first_count==16U);
    bad_crc=0U;first_count=0U;missing_tc=1U;r=request(12U);response=run(&r);
    app_rs485_diag_snapshot(&state,2U);assert((state.words[12]&4U)==0U && state.words[5]==0U);
    missing_tc=0U;
    /* Cancellation invalidates only the diagnostic correlation.  The actual
     * consumer accepted it, so this is not a business rejection/late reply. */
    modbus_cancel_pending();modbus_diag_consumed(&response,1U);
    app_rs485_diag_snapshot(&state,2U);assert(state.words[17]==0U && state.words[21]==1U);
    {
        unsigned before=emitted,i;
        for(i=0U;i<100U;++i)modbus_diag_consumed(&response,1U);
        assert(emitted==before); /* overwritten sidecar cannot flood kind=5 */
        app_rs485_diag_snapshot(&state,2U);assert(state.words[21]==101U && state.words[17]==0U);
        modbus_diag_consumed(&response,0U);assert(consumed_ok==0U);
        app_rs485_diag_snapshot(&state,2U);assert(state.words[17]==1U);
    }
    /* Two enqueues use no descriptor sidecar expansion: older stamp is unknown. */
    r=request(20U);assert(modbus_submit(&r,0)==MODBUS_OK);r=request(21U);assert(modbus_submit(&r,0)==MODBUS_OK);
    modbus_process();app_rs485_diag_snapshot(&state,2U);assert(state.words[0]==20U && !(state.words[12]&1U));
    assert(modbus_get_response(&response,0));modbus_process();assert(modbus_get_response(&response,0));
    ticks+=200U;motion_owner=APP_CONTROL_OWNER_CAN;r=request(22U);response=run(&r);assert(enqueue_owner==APP_CONTROL_OWNER_CAN);
    {
        unsigned before=emitted;
        r=request(23U);response=run(&r);modbus_diag_consumed(&response,1U);
        assert(emitted==before); /* entire normal transaction is sampled together */
        r=request(24U);r.function=6U;r.address=0x0603U;r.value=0U;
        response=run(&r);modbus_diag_consumed(&response,1U);assert(emitted>before);
    }
    /* Independent safety can finish before the ordinary consumer runs.  Its
     * sidecar must not turn the accepted ordinary response into a rejection,
     * nor may the ordinary consumer set the safety transaction's consumed bit. */
    {
        modbus_response_t ordinary,safety;
        uint32_t replaced,late;
        r=request(25U);ordinary=run(&r);
        r=request(26U);assert(modbus_safety_submit(&r)==MODBUS_OK);
        modbus_process();assert(modbus_safety_get_response(&safety));
        app_rs485_diag_snapshot(&state,2U);replaced=state.words[21];late=state.words[17];
        modbus_diag_consumed(&ordinary,1U);
        app_rs485_diag_snapshot(&state,2U);
        assert(state.words[0]==26U && state.words[8]==0U);
        assert(!(state.words[12]&0x20U) && state.words[21]==replaced+1U && state.words[17]==late);
        modbus_diag_consumed(&safety,1U);
        app_rs485_diag_snapshot(&state,2U);assert(state.words[12]&0x20U);
    }
    modbus_safety_takeover();r=request(30U);assert(modbus_safety_submit(&r)==MODBUS_OK);
    modbus_process();assert(modbus_safety_get_response(&response));modbus_diag_consumed(&response,1U);
    app_rs485_diag_snapshot(&state,2U);
    assert((state.words[12]&0x20U)!=0U && response.context.owner==0xFEU);
    app_rs485_diag_snapshot(&state,1U);assert(state.count==24U && state.words[3]==7U);
    app_rs485_diag_snapshot(&state,0U);assert(state.words[0]==0xABC);
    modbus_safety_release();first_count=0U;ticks=UINT32_MAX-20U;r=request(40U);
    assert(modbus_submit(&r,0U)==MODBUS_OK);ticks+=45U;modbus_process();
    assert(modbus_get_response(&response,0U) && response.status==MODBUS_OK);
    assert(first_kind==0x13U && first_count==16U && first[2]>first[7]);
    first_count=0U;no_reply=1U;r=request(41U);response=run(&r);
    assert(response.status==MODBUS_TIMEOUT && first_kind==0x12U && first[13]==0U);
    assert((first[12]&8U)==0U); /* no RX capture reused from the previous request */
    no_reply=0U;first_count=0U;
    {
        uint32_t start=ticks,before=consume_events,i;
        /* Configuration and zero writes are intentionally transport-visible,
         * but their accepted consumption is still routine and rate sampled. */
        for(i=0U;i<50U;++i) {
            r=request(100U+i);r.function=6U;r.address=0x0603U;r.value=0U;
            response=run(&r);modbus_diag_consumed(&response,1U);
        }
        assert(consume_events-before<=(ticks-start)/200U+1U);
    }
    {
        unsigned i;
        for(i=0U;i<8U;++i){r=request(50U+i);assert(modbus_submit(&r,0U)==MODBUS_OK);}
        r=request(99U);assert(modbus_submit(&r,0U)==MODBUS_QUEUE_FULL);
        assert(first_kind==0x14U && first_count==6U && first[0]==99U && (first[2]>>16U)==MODBUS_QUEUE_FULL);
        assert((first[3]>>24U)==0xFFU); /* rejected motion source is explicitly unknown */
        modbus_cancel_pending();first_count=0U;
        assert(modbus_submit(NULL,0U)==MODBUS_INVALID_ARGUMENT && first[0]==UINT32_MAX);
        assert((first[3]&1U)==0U); /* NULL request has no available context. */
    }
    assert(emitted>10U);puts("PASS production RS485 diagnostics: real TC validity, owner, queue identity, cancellation, safety, CRC/raw zero first evidence");
    return 0;
}
