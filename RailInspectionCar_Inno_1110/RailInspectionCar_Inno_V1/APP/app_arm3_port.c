#include "app_arm3_port.h"
#include "app_arm3.h"
#include "app_arm3_diagnostics.h"
#include "app_config.h"
#include "app_arm3_inputs.h"
#include "app_hmi_arm3.h"
#include "app_can_status.h"
#include "app_snapshot_ports.h"
#include "app_control_arbiter.h"
#include "aimtor_servo.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include <string.h>

typedef struct {uint32_t at;uint16_t start;uint8_t kind;uint8_t remote[32];uint16_t words[24];} input_t;
static input_t queue[16], current;
static uint8_t head,tail,count;
static volatile bool stop_pending;
static uint32_t zero_since;
static bool zero_seen;
static app_can_status_input_t chassis;
static app_arm3_platform_t platform;
typedef struct {uint16_t command[24],edit[24],result[32],global[64],side[2][64];uint8_t ack[2][32];uint32_t at,hmi_at;app_arm3_port_status_t status;bool valid;} display_t;
static display_t display[2];static volatile uint8_t published;
void app_arm3_port_display_service(uint32_t now, void (*progress)(void))
{
 uint8_t next=(uint8_t)(published^1U);display_t *d=&display[next];
 if(display[published].valid && (uint32_t)(now-display[published].at)<20U)return;
 /* Keep the capture origin at the start, never at publication: elapsed task
  * preemption must remain visible to readers. No partial page is published. */
 (void)app_hmi_arm3_read(0x0500U,24U,d->command,now);if(progress)progress();
 (void)app_hmi_arm3_read(0x0520U,24U,d->edit,now);if(progress)progress();
 (void)app_hmi_arm3_read(0x0540U,32U,d->result,now);if(progress)progress();
 (void)app_hmi_arm3_read(0x0580U,64U,d->global,now);if(progress)progress();
 (void)app_hmi_arm3_read(0x05C0U,64U,d->side[0],now);if(progress)progress();
 (void)app_hmi_arm3_read(0x0600U,64U,d->side[1],now);if(progress)progress();
 d->hmi_at=app_hmi_arm3_snapshot_time();
 (void)app_arm3_remote_ack(0x8BU,d->ack[0],now);if(progress)progress();
 (void)app_arm3_remote_ack(0x8CU,d->ack[1],now);if(progress)progress();d->at=now;
 /* Only the CAN business task may read the manager's mutable state. */
 app_arm3_snapshot(&d->status.arm,now);d->status.platform=platform;
 d->status.captured_ms=now;d->status.age_ms=0U;
 taskENTER_CRITICAL();d->status.input_queue_depth=count;d->status.stop_pending=stop_pending;
 d->valid=true;published=next;taskEXIT_CRITICAL();
}
#if defined(__CC_ARM)
__weak bool app_arm3_board_sample(app_arm3_platform_t *sample)
#else
__attribute__((weak)) bool app_arm3_board_sample(app_arm3_platform_t *sample)
#endif
{ (void)sample; return false; }
static bool work_lock(void *context,bool acquire)
{
 static servo_snapshot_t axis[2];unsigned i;bool ok=true;uint32_t now=HAL_GetTick();(void)context;
 if(!acquire){app_control_arbiter_arm_lock(0U);return true;}
 /* Recheck actual producer state in the same critical transaction as locking. */
 taskENTER_CRITICAL();
 if(!zero_seen || (uint32_t)(now-zero_since)<500U || !aimtor_get_snapshot_pair(axis,axis+1U))ok=false;
 for(i=0U;i<2U;++i)if(app_config_servo_control_axis_mask()&(1U<<i)){
  if(!axis[i].online || !axis[i].speed_valid || !axis[i].fault_valid || axis[i].speed_age_ms>300U ||
     axis[i].fault_age_ms>300U || axis[i].fault || axis[i].fault_code || axis[i].actual_speed_rpm || axis[i].busy ||
     axis[i].speed_write_sequence!=axis[i].speed_write_completed)ok=false;
 }
 if(ok)ok=app_control_arbiter_arm_begin()!=0U;
 taskEXIT_CRITICAL();return ok;
}
static void transport_lock(void *context)
{
 (void)context;
 /* Unknown transport blocks every new grant immediately, even while moving.
  * Existing producers keep their stop generation until their stop completes. */
 app_control_arbiter_arm_lock(1U);
}
static void vehicle_stop(void *context)
{(void)context;(void)app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_NONE,5U,0U,0U,0xA300U);
 aimtor_request_controlled_safety_stop(1U);}
void app_arm3_port_init(uint32_t boot)
{app_arm3_callbacks_t cb;memset(&cb,0,sizeof(cb));head=tail=count=0U;stop_pending=false;
 zero_seen=false;
 if(APP_ARM3_PROFILE_ENABLED)transport_lock(NULL);
 app_control_arbiter_set_arm_stop(app_arm3_port_stop);cb.work_lock=work_lock;cb.transport_lock=transport_lock;cb.vehicle_stop=vehicle_stop;app_arm3_init(&cb);
 app_arm3_inputs_init(boot);app_hmi_arm3_init(boot);memset(display,0,sizeof(display));published=0U;}
void app_arm3_port_stop(void){stop_pending=true;}
bool app_arm3_port_get_status(app_arm3_port_status_t *status,uint32_t now)
{
 bool valid;
 if(!status)return false;
 taskENTER_CRITICAL();
 *status=display[published].status;valid=display[published].valid;
 taskEXIT_CRITICAL();
 status->age_ms=valid?now-status->captured_ms:UINT32_MAX;
 return valid;
}
uint8_t app_arm3_port_diag_words(uint32_t *w,uint8_t phase,uint32_t *sampled_ms)
{
 const app_arm3_port_status_t *v;unsigned i;uint8_t n;
 if(!w || !sampled_ms)return 0U;
 taskENTER_CRITICAL();v=&display[published].status;*sampled_ms=v->captured_ms;
 if(phase==0U){
  w[0]=display[published].valid | ((uint32_t)v->arm.link_ready<<1U) | ((uint32_t)v->arm.negotiated<<2U) |
   ((uint32_t)v->platform.profile_enabled<<3U) | ((uint32_t)v->arm.safety_valid<<4U) | ((uint32_t)v->arm.config_match<<5U);
  w[1]=v->arm.generation;w[2]=v->arm.owner | ((uint32_t)v->arm.state<<8U) | ((uint32_t)v->arm.permit_mask<<16U) | ((uint32_t)v->arm.work_locked<<24U);
  w[3]=v->arm.reason;w[4]=v->arm.arm_version;w[5]=v->arm.safety_version;
  w[6]=v->arm.owner_remaining_ms | ((uint32_t)v->arm.input_remaining_ms<<16U);
  w[7]=v->arm.source_age_ms;w[8]=v->arm.last_operation;w[9]=v->arm.rejected;w[10]=v->arm.queue_full;
  w[11]=v->input_queue_depth | ((uint32_t)v->stop_pending<<8U) | ((uint32_t)v->arm.chassis_busy<<9U) | ((uint32_t)v->arm.parameter_locked<<10U);
  w[12]=v->platform.sample_age_ms;w[13]=v->platform.chassis_zero_stable_ms | ((uint32_t)v->platform.chassis_feedback_age_ms<<16U);
  w[14]=v->platform.hardware_bitmap;w[15]=v->platform.mapped_input_mask;w[16]=v->platform.config_flags;
  w[17]=v->arm.snapshot_sequence;n=18U;
 }else{
  for(i=0U;i<2U;++i){const app_arm3_side_snapshot_t *a=&v->arm.side[i];uint32_t *p=w+12U*i;
   p[0]=a->operation;p[1]=a->generation;p[2]=a->version;p[3]=a->sequence;p[4]=a->result_sequence;
   p[5]=a->phase | ((uint32_t)a->result<<8U) | ((uint32_t)a->result_flags<<16U) | ((uint32_t)a->proof_state<<24U);
   p[6]=a->feedback_age_ms;p[7]=a->detail;p[8]=a->raw_fault;p[9]=a->usb_epoch;
   p[10]=a->online | ((uint32_t)a->active<<1U) | ((uint32_t)a->stopping<<2U) | ((uint32_t)a->feedback_real<<3U) |
    ((uint32_t)a->identity_valid<<4U) | ((uint32_t)a->config_valid<<5U) | ((uint32_t)a->transport_approved<<6U);
   p[11]=a->proof_id;
  }n=24U;
 }
 taskEXIT_CRITICAL();return n;
}
bool app_arm3_port_remote(const uint8_t frame[32],uint32_t now)
{input_t *p;bool ok=false;if(!frame || frame[0]!=0xA5U || frame[1]!=0x5AU || frame[2]!=2U || frame[3]!=2U)return false;
 taskENTER_CRITICAL();if(count<16U){p=&queue[head];memcpy(p->remote,frame,32U);p->kind=1U;p->at=now;
 head=(uint8_t)((head+1U)%16U);++count;ok=true;}else stop_pending=true;taskEXIT_CRITICAL();(void)ok;return true;}
bool app_arm3_port_hmi_write(uint16_t start,uint16_t n,const uint16_t *v,uint32_t now)
{input_t *p;bool ok=false;if(!v || n!=24U || (start!=0x0500U && start!=0x0520U))return false;
 taskENTER_CRITICAL();if(count<16U){p=&queue[head];memcpy(p->words,v,48U);p->start=start;p->kind=2U;p->at=now;
 head=(uint8_t)((head+1U)%16U);++count;ok=true;}else stop_pending=true;taskEXIT_CRITICAL();return ok;}
static uint32_t aged(uint32_t value,uint32_t elapsed)
{return value==UINT32_MAX || value>UINT32_MAX-elapsed?UINT32_MAX:value+elapsed;}
static void age_words(uint16_t *values,const uint16_t *source,uint16_t base,uint16_t start,uint16_t n,uint16_t address,uint32_t elapsed)
{
 uint32_t value;
 if((uint32_t)address+1U<start || address>=(uint32_t)start+n)return;
 value=((uint32_t)source[address-base]<<16U)|source[address-base+1U];value=aged(value,elapsed);
 if(address>=start)values[address-start]=(uint16_t)(value>>16U);
 if((uint32_t)address+1U<(uint32_t)start+n)values[address-start+1U]=(uint16_t)value;
}
bool app_arm3_port_hmi_read(uint16_t start,uint16_t n,uint16_t *v,uint32_t now)
{
 const uint16_t *src=NULL;uint16_t base=0U,len=0U;display_t *d;uint32_t elapsed;
 if(!v || !n)return false;taskENTER_CRITICAL();d=&display[published];
 if(start>=0x0500U && start<0x0518U){src=d->command;base=0x0500U;len=24U;}
 else if(start>=0x0520U && start<0x0538U){src=d->edit;base=0x0520U;len=24U;}
 else if(start>=0x0540U && start<0x0560U){src=d->result;base=0x0540U;len=32U;}
 else if(start>=0x0580U && start<0x05C0U){src=d->global;base=0x0580U;len=64U;}
 else if(start>=0x05C0U && start<0x0600U){src=d->side[0];base=0x05C0U;len=64U;}
 else if(start>=0x0600U && start<0x0640U){src=d->side[1];base=0x0600U;len=64U;}
 if(!src || (uint32_t)(start-base)+n>len){taskEXIT_CRITICAL();return false;}
 memcpy(v,src+(start-base),n*2U);elapsed=now-d->hmi_at;
 /* Half-word reads use the full cached age for carry and saturation. */
 if(base==0x0580U){age_words(v,src,base,start,n,0x058AU,elapsed);age_words(v,src,base,start,n,0x05ACU,elapsed);age_words(v,src,base,start,n,0x05AEU,elapsed);}
 else if(base==0x05C0U || base==0x0600U)age_words(v,src,base,start,n,(uint16_t)(base+14U),elapsed);
 else if(base==0x0540U)age_words(v,src,base,start,n,0x055CU,elapsed);
 taskEXIT_CRITICAL();
 return true;
}
bool app_arm3_port_ack(uint8_t type,uint8_t frame[32],uint32_t now)
{
 uint32_t elapsed,value;unsigned i,b;uint16_t crc=0xFFFFU,offset=type==0x8BU?26U:12U;
 if(!frame || (type!=0x8BU && type!=0x8CU))return false;
 taskENTER_CRITICAL();memcpy(frame,display[published].ack[type==0x8BU?0U:1U],32U);elapsed=now-display[published].at;taskEXIT_CRITICAL();
 value=((uint16_t)frame[offset]<<8U)|frame[offset+1U];value=aged(value,elapsed);if(value>65535U)value=65535U;
 frame[offset]=(uint8_t)(value>>8U);frame[offset+1U]=(uint8_t)value;
 for(i=0U;i<28U;++i){crc^=(uint16_t)frame[i]<<8U;for(b=0U;b<8U;++b)crc=(uint16_t)((crc&0x8000U)?((uint32_t)crc<<1U)^0x1021U:(uint32_t)crc<<1U);}
 frame[28]=(uint8_t)(crc>>8U);frame[29]=(uint8_t)crc;return true;
}
void app_arm3_port_service(uint32_t now)
{unsigned k,a;bool fresh,zero,idle;uint32_t max_age=0U;
 memset(&platform,0,sizeof(platform));platform.profile_enabled=APP_ARM3_PROFILE_ENABLED!=0U;
 platform.sample_age_ms=UINT32_MAX;platform.sample_tick_ms=now;
 if(!app_arm3_board_sample(&platform)){platform.safety_config_approved=false;platform.safety_version=0U;platform.safety_flags=0U;platform.hardware_bitmap=0U;}
 /* A runtime sample cannot turn off a deployment-latched transport policy. */
 platform.profile_enabled=APP_ARM3_PROFILE_ENABLED!=0U;
 fresh=app_system_snapshot_read_can(&chassis) && chassis.source_valid && (uint32_t)(now-chassis.snapshot_ms)<=100U;
 zero=fresh;idle=app_control_arbiter_owner()==APP_CONTROL_OWNER_NONE && !app_control_arbiter_preparing() && !app_control_arbiter_stopping();
 platform.no_pending_writes=!app_control_arbiter_motion_pending();
 for(a=0U;a<2U;++a){servo_snapshot_t *v=&chassis.axis[a];
  if(!(app_config_servo_control_axis_mask()&(1U<<a)))continue;
  if(!v->online || !v->speed_valid || !v->fault_valid || v->speed_age_ms>300U || v->fault_age_ms>300U || v->fault || v->fault_code || v->actual_speed_rpm!=0)zero=false;
  if(v->busy || v->speed_write_completed!=v->speed_write_sequence)platform.no_pending_writes=false;
  if(v->speed_age_ms>max_age)max_age=v->speed_age_ms;
  if(v->actual_speed_rpm!=0 && v->speed_valid)platform.chassis_moving=true;
 }
 if(zero && idle && platform.no_pending_writes){if(!zero_seen){zero_since=now;zero_seen=true;}}
 else zero_seen=false;
 platform.chassis_idle=idle;platform.chassis_feedback_age_ms=fresh?(max_age>65535U?65535U:(uint16_t)max_age):65535U;
 platform.chassis_zero_stable_ms=zero_seen?((uint32_t)(now-zero_since)>65535U?65535U:(uint16_t)(now-zero_since)):0U;
 platform.parameter_locked=app_control_arbiter_parameter_locked()!=0U;
 platform.recovery_locked=aimtor_recovery_blocks_motion()!=0U || app_control_arbiter_lock_reason()==APP_CONTROL_LOCK_SAFETY;
 /* RK validates the 45/46 conservative clock solution before motion. */
 platform.clock_valid=chassis.can_link_ready;
 app_arm3_set_platform(&platform);
 if(stop_pending){taskENTER_CRITICAL();stop_pending=false;taskEXIT_CRITICAL();app_arm3_global_stop(now);}
 for(k=0U;k<8U;++k){taskENTER_CRITICAL();if(!count){taskEXIT_CRITICAL();break;}
 current=queue[tail];tail=(uint8_t)((tail+1U)%16U);--count;taskEXIT_CRITICAL();
 if((uint32_t)(now-current.at)>200U){
  /* Stale intent cannot become a new global stop and interrupt a later action.
   * A real STOP still passes the adapters' session/content/replay checks. */
  if(!((current.kind==1U && current.remote[23]==APP_ARM3_STOP) ||
       (current.kind==2U && current.start==0x0500U && current.words[8]==APP_ARM3_STOP)))continue;
 }
 app_arm3_input_delay(now-current.at);
 if(current.kind==1U)(void)app_arm3_remote_receive(current.remote,32U,now);
 else (void)app_hmi_arm3_write(current.start,24U,current.words,now);
 app_arm3_input_delay(0U);
 }
 app_arm3_inputs_service(now);app_hmi_arm3_service(now);app_arm3_service(now);
}
