#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_config.h"
#include "app_health.h"
#include "aimtor_registers.h"
#include "modbus_rtu.h"
#include "bsp_rs485.h"
#include "queue.h"
#include "task.h"
#include "led.h"
static TickType_t ticks;
static uint32_t reset_reason, retained, detail;
static unsigned refreshes, writes, zero_writes, tx_count;
static uint16_t regs[3][65536];
static uint8_t rx[16], rx_len, rx_pos, inhibited, offline, stuck, wrong_ramp, zero_drop;
static uint32_t last_ramp_tick[3];
static int16_t ramp_start[3];
static uint8_t ramping[3];
typedef struct { unsigned capacity,size,count,head,tail; unsigned char data[]; } queue_t;
QueueHandle_t xQueueCreate(unsigned count,unsigned size)
{ queue_t *q=calloc(1,sizeof(*q)+count*size); assert(q); q->capacity=count;q->size=size;return q; }
void vQueueDelete(QueueHandle_t q) { free(q); }
BaseType_t xQueueReset(QueueHandle_t handle)
{ queue_t *q=handle; q->count=q->head=q->tail=0;return pdTRUE; }
BaseType_t xQueueSend(QueueHandle_t handle,const void *item,TickType_t wait)
{ queue_t *q=handle;(void)wait;if(q->count==q->capacity)return pdFALSE;memcpy(q->data+q->head*q->size,item,q->size);q->head=(q->head+1)%q->capacity;q->count++;return pdTRUE; }
BaseType_t xQueueReceive(QueueHandle_t handle,void *item,TickType_t wait)
{ queue_t *q=handle;if(!q->count){ticks+=wait;return pdFALSE;}memcpy(item,q->data+q->tail*q->size,q->size);q->tail=(q->tail+1)%q->capacity;q->count--;return pdTRUE; }
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t delay) { ticks+=delay; }
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t delay) { (void)clear;ticks+=delay;return 0; }
uint32_t bsp_system_take_reset_reason(void) { return reset_reason; }
void bsp_system_watchdog_refresh(void) { refreshes++; }
void bsp_system_fault_set_context(uint32_t task, uint32_t phase) { (void)task; (void)phase; }
void bsp_system_store_fault(uint32_t fault,uint32_t d) { (void)fault;detail=d; }
void bsp_system_safety_store(uint32_t state,uint32_t d) { retained=state;detail=d; }
uint32_t bsp_system_safety_state(void) { return retained; }
uint32_t bsp_system_safety_detail(void) { return detail; }
void bsp_system_safety_clear(void) { retained=0; }
void servo_pulse_stop_all(void) {}
void set_led(uint8_t led,uint8_t level) { (void)led;(void)level; }
void rs485_inhibit_transmit(uint8_t value) { inhibited=value; }
uint16_t rs485_available(void) { return (uint16_t)(rx_len-rx_pos); }
uint32_t rs485_last_rx_tick(void) { return ticks; }
uint16_t rs485_read(uint8_t *data,uint16_t capacity)
{ unsigned count=0;while(count<capacity && rx_pos<rx_len)data[count++]=rx[rx_pos++];return (uint16_t)count; }
rs485_status_t rs485_send(const uint8_t *frame,uint16_t length)
{
    unsigned slave=frame[0],address=((unsigned)frame[2]<<8)|frame[3];
    uint16_t value=((uint16_t)frame[4]<<8)|frame[5],crc;
    (void)length;
    if(inhibited)return RS485_BUSY;
    tx_count++;rx_len=rx_pos=0;
    if(slave==offline)return RS485_OK;
    if(frame[1]==6U){
        assert(address==AIMTOR_REG_DECELERATION_TIME || address==AIMTOR_REG_SPEED_COMMAND);
        writes++;
        if(address==AIMTOR_REG_SPEED_COMMAND){assert(value==0);zero_writes++;if(zero_drop)return RS485_OK;ramping[slave]=1;ramp_start[slave]=(int16_t)regs[slave][AIMTOR_REG_ACTUAL_SPEED];last_ramp_tick[slave]=ticks;}
        regs[slave][address]=value;memcpy(rx,frame,6);rx_len=6;
    }else{
        if(address==AIMTOR_REG_ACTUAL_SPEED && ramping[slave] && !stuck){
            int32_t rpm=ramp_start[slave],change=(int32_t)((ticks-last_ramp_tick[slave])*1000U/regs[slave][AIMTOR_REG_DECELERATION_TIME]);
            rpm=rpm<0? -(int32_t)((-rpm)>change?(-rpm)-change:0):(rpm>change?rpm-change:0);
            regs[slave][address]=(uint16_t)(int16_t)rpm;
        }
        value=regs[slave][address];if(wrong_ramp && address==AIMTOR_REG_DECELERATION_TIME)value++;rx[0]=(uint8_t)slave;rx[1]=3;rx[2]=2;rx[3]=(uint8_t)(value>>8);rx[4]=(uint8_t)value;rx_len=5;
    }
    crc=modbus_crc16(rx,rx_len);rx[rx_len++]=(uint8_t)crc;rx[rx_len++]=(uint8_t)(crc>>8);return RS485_OK;
}
static void beat_except(int failed)
{ unsigned i;for(i=0;i<APP_HEARTBEAT_COUNT;i++)if((int)i!=failed)app_health_beat((app_heartbeat_t)i); }
static app_safety_status_t status(void)
{ app_safety_status_t out;app_health_get_safety_status(&out);return out; }
static void cycle(int failed,int bus)
{ beat_except(failed);app_health_watchdog_service();if(bus)modbus_process();ticks+=20; }
static void pump(unsigned duration,int failed,int bus)
{ TickType_t start=ticks;while(ticks-start<duration)cycle(failed,bus); }
static void seed(int16_t rpm)
{
    unsigned slave;
    writes=zero_writes=tx_count=0;offline=stuck=wrong_ramp=zero_drop=0;reset_reason=retained=0;
    memset(regs,0,sizeof(regs));memset(ramping,0,sizeof(ramping));
    for(slave=1;slave<=2;slave++){
        regs[slave][AIMTOR_REG_FIRMWARE_VERSION]=558;
        regs[slave][AIMTOR_REG_NO2_STOP_MODE]=2;
        regs[slave][AIMTOR_REG_COMM_TIMEOUT]=1000;
        regs[slave][AIMTOR_REG_SERVO_ENABLE]=1;
        regs[slave][AIMTOR_REG_ACTUAL_SPEED]=(uint16_t)rpm;
        regs[slave][AIMTOR_REG_SPEED_COMMAND]=(uint16_t)rpm;
    }
    assert(modbus_init()==MODBUS_OK);app_health_init();
}
static void nrf_steady(app_health_nrf_input_t *in,uint32_t from,uint32_t to)
{uint32_t at;for(at=from;at<=to;at+=5U)app_health_nrf_update(in,at);}
static void test_nrf_health(void)
{
    app_health_nrf_input_t in;app_health_nrf_status_t out;uint32_t start;
    memset(&in,0,sizeof(in));app_health_init();in.failure=3U;
    /* Failure starts at observation, even after a long healthy task lifetime. */
    app_health_nrf_update(&in,100000U);app_health_get_nrf_status(&out);
    assert(out.known && !out.current_fault && !out.recovered && out.failure_count==1U);
    app_health_nrf_update(&in,100999U);app_health_get_nrf_status(&out);assert(!out.current_fault);
    app_health_nrf_update(&in,101000U);app_health_get_nrf_status(&out);assert(out.current_fault);
    app_health_set_diagnostic_fault(APP_DIAG_NRF_INIT_ERROR);
    in.initialized=in.ready=1U;app_health_nrf_update(&in,101005U);
    nrf_steady(&in,101010U,105000U);app_health_get_nrf_status(&out);assert(!out.recovered);
    in.config_checks=1U;app_health_nrf_update(&in,105005U);app_health_get_nrf_status(&out);assert(!out.recovered);
    nrf_steady(&in,105010U,106000U);in.config_checks=2U;app_health_nrf_update(&in,106005U);app_health_get_nrf_status(&out);
    assert(out.recovered && !out.current_fault && out.recovery_count==1U);
    assert(app_health_get_diagnostic_faults()&APP_DIAG_NRF_INIT_ERROR);
    ++in.spi_errors;app_health_nrf_update(&in,106010U);app_health_get_nrf_status(&out);
    assert(!out.recovered && out.failure_count==2U);
    /* A transient loses verification immediately, but does not become a
     * current fault before the original 1000 ms deadline. */
    app_health_nrf_update(&in,106015U);app_health_get_nrf_status(&out);assert(!out.current_fault);
    nrf_steady(&in,106020U,107010U);in.config_checks=3U;app_health_nrf_update(&in,107015U);
    nrf_steady(&in,107020U,108010U);in.config_checks=4U;app_health_nrf_update(&in,108015U);app_health_get_nrf_status(&out);assert(out.recovered);
    app_health_nrf_update(&in,108115U);app_health_get_nrf_status(&out);assert(!out.recovered);
    in.ready=0U;app_health_nrf_update(&in,108120U);app_health_get_nrf_status(&out);assert(!out.current_fault);
    app_health_nrf_update(&in,201000U);app_health_get_nrf_status(&out);assert(out.current_fault);
    app_health_init();start=0xFFFFFF00UL;app_health_nrf_update(&in,start);
    app_health_nrf_update(&in,start+999U);app_health_get_nrf_status(&out);assert(!out.current_fault);
    app_health_nrf_update(&in,start+1000U);app_health_get_nrf_status(&out);assert(out.current_fault);
    /* Zero timestamps are legitimate; explicit state tracks timer validity. */
    app_health_init();app_health_nrf_update(&in,0U);app_health_nrf_update(&in,1000U);
    app_health_get_nrf_status(&out);assert(out.current_fault);
    puts("PASS NRF current/history separation, observation timers, verified recovery, transient loss and wrap");
}
int main(void)
{
    unsigned i,before;
    ticks=UINT32_MAX-300U;seed(0);pump(1200,-1,1);
    assert(modbus_motion_permitted());assert(status().parameters_mask==3);
    regs[1][AIMTOR_REG_NO2_STOP_MODE]=0;pump(700,-1,1);assert(!modbus_motion_permitted());
    /* A full ordinary queue cannot delay the independent guard request. */
    seed(0);
    { modbus_request_t request; memset(&request,0,sizeof(request)); request.slave=1;
      request.function=3; request.address=AIMTOR_REG_ACTUAL_SPEED; request.quantity=1;
      for(i=0;i<8;i++) assert(modbus_submit(&request,0)==MODBUS_OK);
      assert(modbus_submit(&request,0)==MODBUS_QUEUE_FULL);
      cycle(-1,1); cycle(-1,1); assert(status().firmware[0]==558); }
    /* Runtime controlled-stop failures immediately silence both axes and survive reset. */
    app_health_request_driver_fallback(0x101U); before=tx_count;
    pump(200,-1,1); assert(status().phase==2 && tx_count==before && retained==2);
    seed(1600);pump(800,APP_HEARTBEAT_SERVO_COMM,1);
    assert(status().phase==1 && retained==1);
    /* ordinary writers and late normal responses cannot bypass takeover. */
    {modbus_request_t request;memset(&request,0,sizeof(request));request.slave=1;request.function=6;request.address=AIMTOR_REG_SPEED_COMMAND;request.value=1600;assert(modbus_submit(&request,0)==MODBUS_TRANSPORT_ERROR);}
    pump(11000,APP_HEARTBEAT_SERVO_COMM,1);
    assert(status().phase==3 && retained==3 && status().zero_mask==3);
    assert(zero_writes==2 && writes==4);
    assert(regs[1][AIMTOR_REG_SERVO_ENABLE]==1 && regs[2][AIMTOR_REG_SERVO_ENABLE]==1);
    before=refreshes;pump(1000,-1,1);assert(refreshes==before && status().phase==3);
    /* reboot after zero stays read-only until manual acknowledge. */
    reset_reason=1UL<<29;assert(modbus_init()==MODBUS_OK);app_health_init();pump(1500,-1,1);
    assert(status().phase==6 && !modbus_motion_permitted());
    assert(app_health_safety_acknowledge());pump(1000,-1,1);assert(modbus_motion_permitted() && retained==0);
    /* every critical task, including new bus heartbeat, latches. */
    for(i=0;i<APP_HEARTBEAT_COUNT;i++){
        seed(-1600);stuck=1;pump(2200,(int)i,1);
        assert(status().phase==2 && retained==2 && inhibited);
        before=tx_count;pump(500,(int)i,1);assert(tx_count==before);
    }
    /* failed bus executor cannot feed forever; its restart cannot transmit. */
    seed(1600);pump(2000,APP_HEARTBEAT_MODBUS,0);assert(status().phase==2 && retained==2);
    before=tx_count;pump(200,-1,1);assert(tx_count==before);
    /* fallback survives reset and enforces at least1200ms silence. */
    reset_reason=1UL<<29;assert(modbus_init()==MODBUS_OK);app_health_init();
    pump(1100,-1,1);assert(tx_count==before && status().phase==4);
    pump(1400,-1,1);assert(status().phase==6 && writes==0);
    /* ordinary IWDG running recovery writes6250, leaves enable untouched. */
    seed(1600);reset_reason=1UL<<29;app_health_init();pump(13500,-1,1);
    assert(status().phase==6 && status().zero_mask==3);
    assert(regs[1][AIMTOR_REG_DECELERATION_TIME]==6250 && regs[2][AIMTOR_REG_DECELERATION_TIME]==6250);
    assert(regs[1][AIMTOR_REG_SERVO_ENABLE]==1);
    /* one offline axis causes both buses quiet; repeated polling cannot refresh it. */
    seed(1600);offline=2;pump(2200,APP_HEARTBEAT_MOTION,1);assert(status().phase==2);
    before=tx_count;pump(500,-1,1);assert(tx_count==before);
    /* Bad ramp readback, lost zero response, existing ER.994 never invoke disable or clear. */
    seed(1600);wrong_ramp=1;pump(2000,APP_HEARTBEAT_MOTION,1);
    assert(status().phase==2 && zero_writes==0);
    seed(1600);zero_drop=1;pump(2000,APP_HEARTBEAT_MOTION,1);
    assert(status().phase==2 && regs[1][AIMTOR_REG_SERVO_ENABLE]==1);
    seed(1600);regs[1][AIMTOR_REG_FAULT_CODE]=0x994;
    reset_reason=1UL<<29;app_health_init();pump(1000,-1,1);
    assert(status().phase==2 && writes==0 && regs[1][AIMTOR_REG_FAULT_CODE]==0x994);
    /* Safety allowlist cannot be used to clear faults or toggle enables. */
    {modbus_request_t request;memset(&request,0,sizeof(request));request.slave=1;request.function=6;
     request.address=AIMTOR_REG_SERVO_ENABLE;request.value=0;assert(modbus_safety_submit(&request)==MODBUS_INVALID_ARGUMENT);}
    seed(0);app_health_request_watchdog_test();before=refreshes;cycle(-1,1);
    if(APP_WDG_TEST_K6_SHORT_ENABLED)assert(refreshes==before && app_health_watchdog_test_active());
    else assert(refreshes>before);
    test_nrf_health();
    puts("PASS independent safety: task fault, monotonic stop, no disable, parameter gate, fallback silence, reboot, IWDG6250, tick wrap");
    return 0;
}
