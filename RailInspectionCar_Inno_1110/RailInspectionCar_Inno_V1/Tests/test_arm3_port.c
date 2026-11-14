#include "app_arm3_port.h"
#include "app_arm3.h"
#include "app_arm3_diagnostics.h"
#include "app_arm3_inputs.h"
#include "app_hmi_arm3.h"
#include "app_can_status.h"
#include "app_snapshot_ports.h"
#include "app_control_arbiter.h"
#include "aimtor_servo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Compile the actual port as another unit. This fixture only replaces its
 * platform and business dependencies: no peripheral or physical motion. */
static app_arm3_callbacks_t callbacks;
static app_arm3_platform_t sampled;
static app_can_status_input_t snapshot;
static servo_snapshot_t actual[2];
static uint32_t tick;
static uint32_t hmi_origin_delay;
static unsigned critical, begin_calls, lock_calls, global_stops, vehicle_stops;
static unsigned received, written, core_services, input_services, hmi_services;
static unsigned reads, acks, begin_result, manager_reads;
static bool snapshot_ok, actual_ok, preparing, stopping, parameter, pending, recovery;
static app_control_owner_t owner;
static uint8_t locked;
static void (*stop_callback)(void);
static uint8_t remote_marker[32];
static uint16_t hmi_marker[32];

void test_port_enter(void){++critical;}
void test_port_exit(void){assert(critical);--critical;}
uint32_t HAL_GetTick(void){return tick;}
uint8_t app_config_servo_control_axis_mask(void){return 3U;}
uint8_t app_system_snapshot_read_can(struct app_can_status_input *out){*out=snapshot;return snapshot_ok?1U:0U;}
bool aimtor_get_snapshot_pair(servo_snapshot_t *left,servo_snapshot_t *right){assert(critical);*left=actual[0];*right=actual[1];return actual_ok;}
bool aimtor_recovery_blocks_motion(void){return recovery;}
void aimtor_request_controlled_safety_stop(uint8_t offline){assert(offline==1U);++vehicle_stops;}
app_control_owner_t app_control_arbiter_owner(void){return owner;}
uint8_t app_control_arbiter_preparing(void){return preparing;}
uint8_t app_control_arbiter_stopping(void){return stopping;}
uint8_t app_control_arbiter_motion_pending(void){return pending;}
uint8_t app_control_arbiter_parameter_locked(void){return parameter;}
app_control_lock_reason_t app_control_arbiter_lock_reason(void){return recovery?APP_CONTROL_LOCK_SAFETY:APP_CONTROL_LOCK_NONE;}
void app_control_arbiter_arm_lock(uint8_t value){locked=value;++lock_calls;}
void app_control_arbiter_set_arm_stop(void (*cb)(void)){stop_callback=cb;}
uint8_t app_control_arbiter_arm_begin(void){
    assert(critical);++begin_calls;
    if(!begin_result || owner || preparing || stopping || parameter || pending || recovery)return 0U;
    locked=1U;return 1U;
}
uint8_t app_control_arbiter_submit_global_stop(app_control_owner_t who,uint16_t reason,uint16_t decel,uint32_t sequence,uint16_t command){
    assert(who==APP_CONTROL_OWNER_NONE && reason==5U && !decel && !sequence && command==0xA300U);++global_stops;return 1U;
}
void app_arm3_init(const app_arm3_callbacks_t *cb){callbacks=*cb;}
void app_arm3_input_delay(uint32_t delay){(void)delay;}
void app_arm3_set_platform(const app_arm3_platform_t *p){sampled=*p;}
void app_arm3_snapshot(app_arm3_snapshot_t *out,uint32_t now){++manager_reads;memset(out,0,sizeof(*out));out->now_ms=now;}
void app_arm3_global_stop(uint32_t now){assert(now==tick);++global_stops;}
void app_arm3_service(uint32_t now){assert(now==tick);++core_services;}
void app_arm3_inputs_init(uint32_t boot){assert(boot==123U);}
void app_hmi_arm3_init(uint32_t boot){assert(boot==123U);}
void app_arm3_inputs_service(uint32_t now){assert(now==tick);++input_services;}
void app_hmi_arm3_service(uint32_t now){assert(now==tick);++hmi_services;}
uint32_t app_hmi_arm3_snapshot_time(void){return tick-hmi_origin_delay;}
bool app_arm3_remote_receive(const uint8_t *frame,uint8_t length,uint32_t now){assert(length==32U && now==tick && received<32U);remote_marker[received++]=frame[4];return true;}
bool app_hmi_arm3_write(uint16_t start,uint16_t count,const uint16_t *words,uint32_t now){assert((start==0x500U || start==0x520U) && count==24U && now==tick && written<32U);hmi_marker[written++]=words[0];return true;}
bool app_hmi_arm3_read(uint16_t start,uint16_t count,uint16_t *words,uint32_t now){unsigned i;assert(now==tick);++reads;for(i=0;i<count;++i)words[i]=(uint16_t)(start+i+tick);return true;}
bool app_arm3_remote_ack(uint8_t type,uint8_t out[32],uint32_t now){assert((type==0x8BU || type==0x8CU) && now==tick);++acks;memset(out,(int)(tick&255U),32U);out[0]=type;return true;}

static void healthy(void){unsigned i;memset(&snapshot,0,sizeof(snapshot));snapshot.source_valid=true;snapshot.snapshot_ms=tick;snapshot.can_link_ready=true;for(i=0;i<2U;++i){memset(&actual[i],0,sizeof(actual[i]));actual[i].online=actual[i].speed_valid=actual[i].fault_valid=1U;actual[i].speed_sequence=5U;actual[i].speed_write_sequence=actual[i].speed_write_completed=9U;snapshot.axis[i]=actual[i];}}
static void reset(void){tick=1000U;critical=begin_calls=lock_calls=global_stops=vehicle_stops=received=written=core_services=input_services=hmi_services=reads=acks=0U;begin_result=1U;snapshot_ok=actual_ok=true;preparing=stopping=parameter=pending=recovery=false;owner=APP_CONTROL_OWNER_NONE;locked=0U;memset(&callbacks,0,sizeof(callbacks));healthy();app_arm3_port_init(123U);assert(callbacks.work_lock && callbacks.transport_lock && callbacks.vehicle_stop && stop_callback);assert(!critical);}
static void service(uint32_t at){tick=at;snapshot.snapshot_ms=tick;app_arm3_port_service(tick);app_arm3_port_display_service(tick,NULL);assert(!critical);}
static void test_lock(void){
    reset();assert(locked==(APP_ARM3_PROFILE_ENABLED?1U:0U));
    owner=APP_CONTROL_OWNER_REMOTE;actual[0].actual_speed_rpm=20;callbacks.transport_lock(NULL);assert(locked && !begin_calls && owner==APP_CONTROL_OWNER_REMOTE);
    owner=APP_CONTROL_OWNER_NONE;healthy();service(1000U);assert(!callbacks.work_lock(NULL,true));
    service(1499U);assert(!callbacks.work_lock(NULL,true));
    service(1500U);assert(callbacks.work_lock(NULL,true) && begin_calls==1U && locked);
    assert(sampled.profile_enabled==(APP_ARM3_PROFILE_ENABLED!=0U));
    assert(!sampled.safety_config_approved && !sampled.safety_version && !sampled.hardware_bitmap && !sampled.safety_flags && sampled.sample_age_ms==UINT32_MAX);
    actual[1].actual_speed_rpm=1;assert(!callbacks.work_lock(NULL,true));actual[1].actual_speed_rpm=0;
    actual[0].busy=1;assert(!callbacks.work_lock(NULL,true));actual[0].busy=0;
    actual[0].speed_write_completed=8U;assert(!callbacks.work_lock(NULL,true));actual[0].speed_write_completed=9U;
    actual[1].speed_age_ms=301U;assert(!callbacks.work_lock(NULL,true));actual[1].speed_age_ms=0U;
    actual[1].fault_code=1U;assert(!callbacks.work_lock(NULL,true));actual[1].fault_code=0U;
    actual_ok=false;assert(!callbacks.work_lock(NULL,true));actual_ok=true;
    parameter=true;assert(!callbacks.work_lock(NULL,true));parameter=false;
    owner=APP_CONTROL_OWNER_HMI;assert(!callbacks.work_lock(NULL,true));owner=APP_CONTROL_OWNER_NONE;
    snapshot_ok=false;service(1501U);assert(sampled.chassis_feedback_age_ms==65535U && !callbacks.work_lock(NULL,true));snapshot_ok=true;
    service(1502U);assert(sampled.chassis_zero_stable_ms==0U);service(2002U);assert(callbacks.work_lock(NULL,true));
    assert(callbacks.work_lock(NULL,false) && !locked);
    callbacks.vehicle_stop(NULL);assert(vehicle_stops==1U && global_stops==1U);
    puts("PASS ARM3 real port: immediate unknown transport lock; 500ms and atomic producer recheck; unconfigured safety rejects");
}
static void test_queue(void){
    uint8_t frame[32]={0xA5U,0x5AU,2U,2U};uint16_t words[24]={0U};unsigned i;
    reset();assert(!app_arm3_port_remote(NULL,tick));assert(!app_arm3_port_hmi_write(0x500U,23U,words,tick));
    for(i=0;i<9U;++i){frame[4]=(uint8_t)i;assert(app_arm3_port_remote(frame,tick));}frame[4]=200U;
    for(i=0;i<7U;++i){words[0]=(uint16_t)(20U+i);assert(app_arm3_port_hmi_write(0x520U,24U,words,tick));}words[0]=200U;
    assert(!received && !written);assert(!app_arm3_port_hmi_write(0x500U,24U,words,tick));
    service(1000U);assert(received==8U && !written && global_stops==1U);
    service(1000U);assert(received==9U && written==7U && global_stops==1U);
    for(i=0;i<9U;++i)assert(remote_marker[i]==i);for(i=0;i<7U;++i)assert(hmi_marker[i]==20U+i);
    frame[4]=42U;assert(app_arm3_port_remote(frame,tick));service(1201U);assert(received==9U && global_stops==1U);
    stop_callback();assert(global_stops==1U);service(1201U);assert(global_stops==2U);
    assert(core_services==4U && input_services==4U && hmi_services==4U);
    puts("PASS ARM3 real port: immutable 16-entry ISR-free input queue, eight inputs per service, overflow/stale/stop fanout");
}
static unsigned progress_calls;
static void display_progress(void){
    uint16_t value;
    assert(!critical);
    /* A transport opportunity must not expose the page being built. */
    assert(app_arm3_port_hmi_read(0x500U,1U,&value,tick));
    assert(value==0x500U+1000U);
    ++progress_calls;
}
static void test_display(void){
    uint16_t words[64];uint8_t ack[32];unsigned i,read_count,ack_count,services;
    reset();service(1000U);assert(reads==6U && acks==2U);read_count=reads;ack_count=acks;services=core_services;
    tick=1100U;
    assert(app_arm3_port_hmi_read(0x581U,4U,words,tick));for(i=0;i<4U;++i)assert(words[i]==0x581U+i+1000U);
    assert(app_arm3_port_hmi_read(0x600U,64U,words,tick));assert(words[63]==0x63FU+1000U);
    assert((((uint32_t)words[14]<<16U)|words[15])==(((uint32_t)(0x60EU+1000U)<<16U)|(0x60FU+1000U))+100U);
    assert(!app_arm3_port_hmi_read(0x63FU,2U,words,tick));assert(!app_arm3_port_hmi_read(0x640U,1U,words,tick));assert(!app_arm3_port_hmi_read(0x518U,1U,words,tick));
    assert(app_arm3_port_ack(0x8BU,ack,tick) && ack[0]==0x8BU && ack[1]==(1000U&255U));assert(!app_arm3_port_ack(0x8AU,ack,tick));assert((((uint16_t)ack[26]<<8U)|ack[27])==0xE8E8U+100U);
    assert(reads==read_count && acks==ack_count && core_services==services);
    service(1100U);assert(app_arm3_port_hmi_read(0x580U,1U,words,tick) && words[0]==0x580U+1100U);assert(app_arm3_port_ack(0x8CU,ack,tick) && ack[1]==(1100U&255U));
    puts("PASS ARM3 real port: consistent published double buffer, boundary checks, reads and ACK never call manager/adapters");
    reset();service(1000U);progress_calls=0U;
    tick=1019U;app_arm3_port_display_service(tick,display_progress);assert(!progress_calls && reads==6U);
    /* Safety/input services remain live while display is rate limited. */
    app_arm3_port_stop();service(1019U);assert(global_stops==1U && core_services==2U && reads==6U);
    tick=1020U;app_arm3_port_display_service(tick,display_progress);
    assert(progress_calls==8U && reads==12U && acks==4U);
    assert(app_arm3_port_hmi_read(0x500U,1U,words,tick) && words[0]==0x500U+1020U);
    /* Internal HMI cache can predate the new presentation by 80 ms. */
    hmi_origin_delay=80U;service(1040U);
    assert(app_arm3_port_hmi_read(0x60EU,2U,words,1045U));
    {uint32_t value=(((uint32_t)(0x60EU+1040U)<<16U)|(0x60FU+1040U))+85U;
     assert((((uint32_t)words[0]<<16U)|words[1])==value);
     assert(app_arm3_port_hmi_read(0x60EU,1U,words,1045U) && words[0]==(uint16_t)(value>>16U));
     assert(app_arm3_port_hmi_read(0x60FU,1U,words,1045U) && words[0]==(uint16_t)value);}
    hmi_origin_delay=0U;
    puts("PASS display 20ms throttle, eight transport opportunities and atomic publication; STOP never throttled");
}
static void test_status(void){
    app_arm3_port_status_t status;
    uint32_t compact[24],sampled;
    uint8_t frame[32]={0xA5U,0x5AU,2U,2U};unsigned before;
    reset();manager_reads=0U;
    assert(!app_arm3_port_get_status(&status,tick));
    assert(status.age_ms==UINT32_MAX && !manager_reads);
    service(tick);before=manager_reads;assert(before==1U);
    tick+=201U;
    assert(app_arm3_port_get_status(&status,tick));
    assert(status.captured_ms==1000U && status.age_ms==201U && status.arm.now_ms==1000U);
    assert(!status.arm.negotiated && !status.arm.safety_valid);
    assert(!status.platform.safety_config_approved && !status.platform.safety_version);
    assert(status.input_queue_depth==0U && !status.stop_pending);
    assert(app_arm3_port_remote(frame,tick));stop_callback();
    assert(app_arm3_port_get_status(&status,tick));
    assert(!status.input_queue_depth && !status.stop_pending); /* Same published snapshot. */
    assert(app_arm3_port_diag_words(compact,0U,&sampled)==18U && sampled==1000U);
    assert((compact[0]&1U) && !compact[11]);
    assert(app_arm3_port_diag_words(compact,1U,&sampled)==24U && sampled==1000U);
    assert(manager_reads==before); /* Readers never enter the mutable manager. */
    service(tick);assert(app_arm3_port_get_status(&status,tick) && status.age_ms==0U);
    assert(!app_arm3_port_get_status(NULL,tick));
    tick=UINT32_MAX-10U;service(tick);
    assert(app_arm3_port_get_status(&status,9U) && status.age_ms==20U);
    puts("PASS ARM3 coherent read-only diagnostic cache, unknown startup, stale age and tick wrap");
}
int main(void){test_lock();test_queue();test_display();test_status();return 0;}
