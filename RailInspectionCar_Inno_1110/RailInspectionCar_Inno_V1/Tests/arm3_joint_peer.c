#include "app_arm3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Deterministic host platform only. No target peripheral functions are linked. */
static uint32_t now_ms = 10000U;
static app_arm3_platform_t platform;
static bool lock_cb(void *context,bool acquire){(void)context;(void)acquire;return true;}
static void stop_cb(void *context){(void)context;puts("VEHICLE_STOP");}
static void emit(const can_protocol_record_t *r){unsigned i;printf("TX %u ",r->type);for(i=0U;i<r->length;++i)printf("%02x",r->payload[i]);puts("");}
static void flush(void){can_protocol_record_t r;uint8_t p;uint32_t built;unsigned n=0U;while(n++<32U && app_arm3_take_record(&r,&p,&built)){emit(&r);app_arm3_record_transmitted(&r,now_ms);}}
static void snap(void){app_arm3_snapshot_t s;app_arm3_snapshot(&s,now_ms);printf("SNAP %u %u %u %u %u %u %u %lu %lu %lu\n",s.owner,s.state,s.permit_mask,s.work_locked,s.negotiated,s.config_match,s.active_mask,(unsigned long)s.generation,(unsigned long)s.reason,(unsigned long)s.arm_version);}
int main(void){char line[4096],hex[1024];unsigned t;app_arm3_callbacks_t cb;memset(&cb,0,sizeof(cb));cb.work_lock=lock_cb;cb.vehicle_stop=stop_cb;app_arm3_init(&cb);
 memset(&platform,0,sizeof(platform));platform.profile_enabled=platform.safety_config_approved=platform.chassis_idle=platform.no_pending_writes=platform.clock_valid=true;
 platform.safety_version=1U;platform.hardware_bitmap=63U;platform.safety_flags=0x39U;platform.chassis_zero_stable_ms=500U;platform.config_flags=15U;platform.mapped_input_mask=63U;platform.wiring_crc=1U;platform.policy_crc=2U;platform.chassis_zero_threshold_mm_s=1U;platform.debounce_ms=1U;
 app_arm3_set_platform(&platform);app_arm3_link(100U,200U,66U,true,(1UL<<20U)|(1UL<<25U)|(1UL<<28U)|(1UL<<31U),now_ms);
 while(fgets(line,sizeof(line),stdin)){
  if(sscanf(line,"STEP %u",&t)==1){now_ms=t;platform.sample_tick_ms=t;app_arm3_set_platform(&platform);app_arm3_service(now_ms);}
  else if(sscanf(line,"RX %u %1023s",&t,hex)==2){can_protocol_record_t r;size_t n=strlen(hex);unsigned i,v;if((n&1U) || n>512U || t>255U)return 2;memset(&r,0,sizeof(r));r.type=(uint8_t)t;r.length=(uint16_t)(n/2U);for(i=0U;i<r.length;++i){if(sscanf(hex+i*2U,"%2x",&v)!=1)return 2;r.payload[i]=(uint8_t)v;}
   if(r.type==0x45U && r.length==24U){can_protocol_record_t reply;memset(&reply,0,sizeof(reply));reply.type=0x46U;reply.length=32U;memcpy(reply.payload,r.payload,4U);can_protocol_write_u32(reply.payload+4U,1U);memcpy(reply.payload+8U,r.payload+8U,8U);can_protocol_write_u64(reply.payload+16U,(uint64_t)now_ms*1000U);can_protocol_write_u64(reply.payload+24U,(uint64_t)now_ms*1000U);emit(&reply);}
   else printf("RXOK %u\n",app_arm3_receive(&r,now_ms,now_ms,now_ms));
  }else if(!strncmp(line,"I ",2)){app_arm3_intent_t in;unsigned src,cmd,mask,joint,grade,preset,budget,flags,seq;int direction;uint32_t operation;memset(&in,0,sizeof(in));if(sscanf(line,"I %u %u %u %u %d %u %u %u %u %u",&src,&cmd,&mask,&joint,&direction,&grade,&preset,&budget,&flags,&seq)!=10)return 3;
   in.source=(uint8_t)src;in.command=(uint8_t)cmd;in.mask=(uint8_t)mask;in.joint=(uint8_t)joint;in.direction=(int8_t)direction;in.grade=(uint8_t)grade;in.preset=(uint8_t)preset;in.light_level=128U;in.budget_ms=(uint16_t)budget;in.held=(flags&2U)!=0U;in.page_active=(flags&1U)!=0U;in.session=src*100U;in.sequence=seq;in.expected_version=1U;in.angle[2]=9000;in.angle[5]=12000;
   t=app_arm3_submit(&in,now_ms,&operation);printf("RESULT %u %lu\n",t,(unsigned long)operation);
  }else if(!strncmp(line,"GET ",4)){unsigned op,side;can_arm3_result_t result;if(sscanf(line,"GET %u %u",&op,&side)!=2)return 3;if(app_arm3_get_result(op,(uint8_t)side,now_ms,&result))printf("CACHED %lu %u %u %u %lu\n",(unsigned long)result.operation,result.arm_id,result.phase,result.result,(unsigned long)result.sequence);else puts("CACHED NONE");
  }else if(!strncmp(line,"QUIT",4))return 0;
  flush();snap();puts("END");fflush(stdout);
 }
 return 0;
}
