/* Real F407 fast-service + link segmentation and F103 parser/control. */
#define main previous_can_app_main
#include "test_can_app.c"
#undef main
#include "can_upper_control.h"
static UpperProtocol_t peer;
static UpperControl_t peer_control;
static unsigned stop_commands;
static uint32_t control_gap,axis_gap[2],last_control,last_axis[2],last_records;
static uint8_t peer_send(void *context,uint16_t id,const uint8_t data[8])
{(void)context;if(id==0x110U && data[1]==5U)++stop_commands;return 1U;}
static bool joint_record(void *context,uint8_t type,can_protocol_record_t *record,uint32_t now)
{
    (void)context;s_input.now_ms=now;
    if(!app_can_status_record(&s_input,type,record))return false;
    if(type==0x11U){
        can_protocol_write_u32(record->payload,1U);can_protocol_write_u16(record->payload+4U,17U);
        record->payload[6]=4U;record->payload[7]=3U;record->payload[10]=3U;
        can_protocol_write_u32(record->payload+12U,12U);record->payload[34]=0U;
    }
    return true;
}
static void peer_receive(const bsp_can_frame_t *frame)
{
    const UpperRecord_t *record;
    Upper_Receive(&peer,frame->id,8U,frame->data,frame->timestamp_ms,tick);
    record=UpperControl_Record(&peer.snapshot,0x11U);
    if(record && record->timestamp!=last_control){
        uint32_t gap=record->timestamp-last_control;
        if(last_control && gap>control_gap)control_gap=gap;
        last_control=record->timestamp;
    }
    if(frame->id==0x186U || frame->id==0x187U){
        unsigned i=frame->id-0x186U;uint32_t gap=frame->timestamp_ms-last_axis[i];
        if(last_axis[i] && gap>axis_gap[i])axis_gap[i]=gap;
        last_axis[i]=frame->timestamp_ms;
    }
}
static void async_initialize(uint32_t now)
{
    can_link_callbacks_t cb;unsigned i;
    memset(&cb,0,sizeof(cb));cb.send=send_frame;cb.cancel=cancel_frame;cb.build_record=joint_record;
    cb.record_complete=arm_record_complete;cb.record_failure=record_failed;
    tick=now;can_link_init(&s_link,&cb,1U,42U,100U,1U,7U,1000U,tick);
    s_link.phase=CAN_LINK_READY;s_link.token=42U;s_link.heartbeat_seen=true;s_link.last_heartbeat_ms=tick;
    s_link.next_boot_ms=tick+500U;memset(&s_input,0,sizeof(s_input));
    s_input.source_valid=true;s_input.selected=3U;s_input.owner=4U;s_input.life=3U;s_input.runtime=4U;
    s_input.arbiter_owner=APP_CONTROL_OWNER_CAN;s_input.arbiter_axes=3U;s_input.can_link_ready=true;s_input.token=42U;
    for(i=0U;i<2U;++i){s_input.axis[i].online=true;s_input.axis[i].enabled=true;
        s_input.axis[i].speed_valid=s_input.axis[i].fault_valid=s_input.axis[i].parameter_verified=true;
        s_input.axis[i].control_mode=SERVO_CONTROL_RS485_VELOCITY;s_input.axis[i].actual_speed_rpm=100;}
    memset(s_basic_due,0,sizeof(s_basic_due));memset(s_record_due,0,sizeof(s_record_due));
    memset(async_boxes,0,sizeof(async_boxes));async_mode=true;async_deliver=peer_receive;
    async_submitted=async_completed=async_record_pending=async_record_high=0U;
    s_snapshot_supported=true;s_ready_generation=0U;
    Upper_Init(&peer,7U,peer_send,NULL,NULL);peer.snapshot.state=UPPER_READY;
    peer.snapshot.lower_boot_id=1U;peer.snapshot.link_token=42U;peer.snapshot.axes=3U;
    peer.snapshot.heartbeat_seen=1U;peer.snapshot.heartbeat_at=tick;
    UpperControl_Init(&peer_control,peer_send,NULL);
    stop_commands=0U;control_gap=axis_gap[0]=axis_gap[1]=0U;
    last_control=last_axis[0]=last_axis[1]=last_records=0U;
}
static void timing_run(uint32_t duration,bool maintain,bool control_enabled)
{
    uint32_t started=tick,round=0U;
    while((uint32_t)(tick-started)<duration){
        can_fast_budget_t quota={0U,0U,0U};uint32_t before=s_link.submitted_count;
        s_link.last_heartbeat_ms=tick;
        fast_service(&quota,SystemCoreClock/5000U);
        if(maintain){
            s_input.snapshot_ms=s_input.now_ms=s_input.sample_tick=tick;
            schedule(tick);fast_service(&quota,SystemCoreClock/5000U);
            /* Simulated maintenance/preemption occurs after TX progression. */
            ++tick;fast_service(&quota,SystemCoreClock/5000U);
        }
        assert(s_link.submitted_count-before<=3U && quota.results<=3U);
        if(control_enabled){UpperControl_Tick(&peer_control,&peer,tick);
            assert(peer_control.state==SIM_RUNNING || peer_control.state==SIM_STOP_REQUESTED || peer_control.state==SIM_STOPPING);}
        tick+=round++%31U==30U?4U:1U;
    }
}
int main(void)
{
    const UpperRecord_t *r;
    async_initialize(10000U);timing_run(2000U,true,false);
    r=UpperControl_Record(&peer.snapshot,0x11U);assert(r && tick-r->timestamp<200U);
    peer_control.state=SIM_RUNNING;peer_control.link_token=42U;peer_control.motion_token=17U;
    peer_control.lower_boot=1U;peer_control.upper_boot=7U;peer_control.axes=3U;
    peer_control.motion_generation=1U;peer_control.target_rpm=100;peer_control.target_mm_s=100;
    peer_control.speed_at=tick;peer_control.tx_seen=0U;
    timing_run(10000U,true,true);
    assert(!stop_commands && peer_control.state==SIM_RUNNING && !s_link.record_failed);
    assert(async_record_high==1U && async_submitted>1000U);
    assert(control_gap<=200U && axis_gap[0]<=200U && axis_gap[1]<=200U);
    printf("PASS async production fast path: frames=%u ctrl_gap=%lu axis_gap=%lu/%lu single_record_inflight=%u\n",
        async_submitted,(unsigned long)control_gap,(unsigned long)axis_gap[0],(unsigned long)axis_gap[1],async_record_high);
    /* Actual source silence must still revoke motion, not refresh cached data. */
    async_deliver=NULL;timing_run(600U,false,true);
    assert(stop_commands && !peer_control.motion_token && peer_control.detail==116U);
    puts("PASS F103 real stale feedback still requests ordinary STOP after asynchronous transport silence");
    return 0;
}
