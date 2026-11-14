#include "app_hmi_arm3.h"
#include "app_arm3.h"
#include <string.h>
typedef struct {uint32_t seq,at;uint16_t command[24],result[32];bool used;} entry_t;
static struct {
 uint32_t boot,session,nonce,serial,input,submit_seq,last_input,last_business,last_op;
 uint32_t last_core_seq,edit_revision,edit_at,snapshot_seq,snapshot_at,previous_seq,previous_at;
 uint16_t command[24],edit[24],result[32],global[64],side[2][64];uint8_t last_mask;
 entry_t cache[32];uint8_t cursor;bool edit_valid,seen,released;
} h;
static uint32_t g32(const uint16_t *p){return ((uint32_t)p[0]<<16)|p[1];}
static void p32(uint16_t *p,uint32_t v){p[0]=(uint16_t)(v>>16);p[1]=(uint16_t)v;}
static bool fresh(uint32_t seq,uint32_t now){return (seq==h.snapshot_seq && now-h.snapshot_at<=200U)||(seq==h.previous_seq && now-h.previous_at<=200U);}
static uint8_t submit(app_arm3_intent_t *in,uint32_t now,uint32_t *op){in->source=3U;in->session=h.session;in->sequence=++h.submit_seq;if(!in->sequence)return 7U;return app_arm3_submit(in,now,op);}
void app_hmi_arm3_init(uint32_t boot){memset(&h,0,sizeof(h));h.boot=boot;h.snapshot_at=UINT32_MAX-100U;}
uint32_t app_hmi_arm3_snapshot_time(void){return h.snapshot_at;}
static void result(uint32_t input,uint16_t cmd,uint8_t status,uint32_t op,uint32_t now){
 memset(h.result,0,sizeof(h.result));p32(h.result,h.boot);p32(h.result+2,h.session);p32(h.result+4,input);h.result[6]=cmd;h.result[7]=status;p32(h.result+8,op);h.result[10]=status?2U:0U;h.result[11]=status;p32(h.result+18,now);if(status)p32(h.result+20,now);h.result[30]=3U;h.result[31]=1U;
}
void app_hmi_arm3_service(uint32_t now){
 app_arm3_snapshot_t s;unsigned i,j;uint32_t flags=0;uint16_t *w;
 if(h.edit_valid && now-h.edit_at>=5000U)h.edit_valid=false;
 if(h.seen && now-h.last_input>=300U){h.released=false;h.seen=false;}
 if((uint32_t)(now-h.snapshot_at)<100U)return;
 app_arm3_snapshot(&s,now);if(h.last_core_seq && h.result[6]==1U){app_arm3_input_result_t ir;if(app_arm3_get_input_result(3U,h.session,h.last_core_seq,&ir) && !ir.pending){h.result[7]=ir.result;if(!h.last_op){h.result[10]=2U;h.result[11]=ir.result;p32(h.result+20,now);}}}h.previous_seq=h.snapshot_seq;h.previous_at=h.snapshot_at;h.snapshot_at=now;if(++h.snapshot_seq==0U)++h.snapshot_seq;
 memset(h.global,0,sizeof(h.global));w=h.global;w[0]=0x4153U;w[1]=1U;p32(w+2,h.snapshot_seq);p32(w+4,h.boot);p32(w+6,h.session);p32(w+8,now);p32(w+10,s.source_age_ms);w[12]=s.owner;w[13]=s.state;w[14]=s.permit_mask;w[15]=s.active_mask;
 if(s.work_locked)flags|=1U;if(s.side[0].proof_state && s.side[1].proof_state)flags|=2U;if(s.transport_mask==3U)flags|=4U;if(s.side[0].stopping || s.side[1].stopping)flags|=8U;if(s.safety_valid)flags|=16U;if(h.seen)flags|=32U;if(s.negotiated)flags|=64U;if(s.config_match)flags|=128U;if(s.link_ready)flags|=256U;if(s.parameter_locked)flags|=512U;if(s.chassis_busy)flags|=1024U;
 p32(w+16,flags);p32(w+18,s.reason);p32(w+20,s.generation);p32(w+22,s.arm_version);p32(w+24,s.safety_version);p32(w+26,s.safety_sequence);w[28]=s.owner_remaining_ms;w[29]=s.input_remaining_ms;p32(w+30,h.input);p32(w+32,h.last_business);p32(w+34,h.last_op);w[36]=h.result[6];w[37]=h.result[7];p32(w+38,g32(h.result+12));w[56]=s.feedback_mask;w[57]=s.transport_mask;p32(w+58,h.edit_valid?h.edit_revision:0U);w[60]=3U;p32(w+62,h.snapshot_seq);
 for(i=0;i<2U;++i){app_arm3_side_snapshot_t *a=&s.side[i];w=h.side[i];memset(w,0,128);w[0]=0x4154U;w[1]=1U;p32(w+2,h.snapshot_seq);p32(w+4,h.boot);w[6]=(uint16_t)(i+1U);w[7]=a->state;p32(w+8,(a->online?1U:0U)|(a->angle_mask==63U?2U:0U)|(a->active?4U:0U)|(a->stopping?8U:0U)|(a->diagnostic_valid?16U:0U)|(a->identity_valid?32U:0U)|(a->config_valid?64U:0U)|(a->proof_state?128U:0U)|(a->proof_state==2U?256U:0U)|(a->transport_approved?512U:0U)|((a->result_flags&1U)?1024U:0U)|(a->feedback_real?2048U:0U));w[10]=a->angle_mask;w[11]=a->speed_mask;p32(w+12,a->sequence);p32(w+14,a->feedback_age_ms);p32(w+16,a->raw_fault);p32(w+18,a->version);p32(w+20,a->config_crc);p32(w+22,a->usb_epoch);p32(w+24,a->identity_crc);p32(w+26,a->preset3_revision);p32(w+28,a->operation);w[30]=a->phase;w[31]=a->result;p32(w+32,a->result_sequence);p32(w+34,a->detail);
  for(j=0;j<6U;++j){p32(w+36U+j*2U,(uint32_t)((a->angle_mask&(1U<<j))?a->angle[j]:INT32_MIN));p32(w+48U+j*2U,(uint32_t)((a->speed_mask&(1U<<j))?a->speed[j]:INT32_MIN));}
  w[60]=a->proof_state;w[61]=a->preset_mask;p32(w+62,h.snapshot_seq);p32(h.global+40U+i*2U,a->operation);p32(h.global+44U+i*2U,a->feedback_age_ms);p32(h.global+48U+i*2U,a->usb_epoch);p32(h.global+52U+i*2U,a->preset3_revision);
 }
 if(h.last_op){uint8_t phase=2U,code=0U;bool all=true;can_arm3_result_t rr;
  for(i=0;i<2U;++i)if(h.last_mask&(1U<<i)){
   if(!app_arm3_get_result(h.last_op,(uint8_t)(i+1U),now,&rr)){all=false;phase=0U;continue;}
   if(rr.phase<phase)phase=rr.phase;
   if(rr.result>1U && code<=1U){code=rr.result;p32(h.result+12,rr.detail);}
   else if(rr.result==1U && !code)code=1U;
   p32(h.result+14,rr.generation);p32(h.result+16,rr.config_version);p32(h.result+26,rr.sequence);p32(h.result+28,rr.age_ms);
  }
  h.result[10]=phase;h.result[11]=code;if(all && phase==2U && !g32(h.result+20))p32(h.result+20,now);
  if(!all)(void)app_arm3_request_result(h.last_op,h.last_mask,now);
 }
 h.result[22]=s.owner;h.result[23]=s.state;h.result[24]=s.owner_remaining_ms;for(i=0;i<32U;++i)if(h.cache[i].used && h.cache[i].seq==h.last_business && h.result[7]!=14U)memcpy(h.cache[i].result,h.result,64U);
}
bool app_hmi_arm3_write(uint16_t start,uint16_t count,const uint16_t *w,uint32_t now){
 app_arm3_intent_t in;uint32_t input,op=0,nonce;uint16_t cmd;uint8_t status=0;unsigned i;
 if(start!=0x0500U && start!=0x0520U)return false;
 if(!w || count!=24U)return false;
 app_hmi_arm3_service(now);
 if(start==0x0520U){if(w[0]!=0x4150U || w[1]!=1U || g32(w+2)!=h.boot || !h.session || g32(w+4)!=h.session || !g32(w+6) || (w[8]!=1U && w[8]!=2U) || w[9] || w[22]!=3U || w[23]!=0xA55AU)return false;
  if(h.edit_valid && g32(w+6)==h.edit_revision)return memcmp(w,h.edit,48U)==0;
  if(h.edit_valid && g32(w+6)<=h.edit_revision)return false;memcpy(h.edit,w,48U);h.edit_revision=g32(w+6);h.edit_at=now;h.edit_valid=true;return true;}
 if(w[0]!=0x4152U || w[1]!=1U || w[22]!=3U || w[23]!=0xA55AU)return false;
 input=g32(w+6);cmd=w[8];if(!input || cmd<1U || cmd>13U || (w[18]&~3U))return false;
 if(cmd==13U){nonce=g32(w+16);if(g32(w+4) || !nonce)return false;
  if(h.nonce!=nonce || !h.session){app_arm3_snapshot_t s;app_arm3_snapshot(&s,now);if(s.owner==3U)app_arm3_global_stop(now);h.nonce=nonce;if(++h.serial==0U)++h.serial;h.session=h.boot^(0xA3500000UL+h.serial);if(!h.session)h.session=h.serial;h.input=h.submit_seq=h.last_business=h.last_op=h.edit_revision=0U;h.edit_valid=h.released=false;memset(h.cache,0,sizeof(h.cache));}
  result(input,cmd,0U,0U,now);return true;}
 if(g32(w+2)!=h.boot || !h.session || g32(w+4)!=h.session){result(input,cmd,7U,0U,now);return true;}
 for(i=0;i<32U;++i)if(h.cache[i].used && h.cache[i].seq==input){if(memcmp(h.cache[i].command,w,48U))result(input,cmd,14U,0U,now);else memcpy(h.result,h.cache[i].result,64U);return true;}
 if(input<=h.input){if(cmd!=2U)result(input,cmd,7U,0U,now);return true;}
 if(cmd!=2U && cmd!=4U && h.cache[h.cursor].used && now-h.cache[h.cursor].at<1000U){result(input,cmd,5U,0U,now);return true;}
 if(cmd!=4U && !fresh(g32(w+20),now)){if(cmd!=2U)result(input,cmd,7U,0U,now);return true;}
 h.input=input;h.last_input=now;h.seen=true;if(!(w[18]&2U))h.released=true;
 memset(&in,0,sizeof(in));in.command=(uint8_t)cmd;in.mask=(uint8_t)w[9];in.joint=w[10]>=1U && w[10]<=6U?(uint8_t)(w[10]-1U):255U;in.direction=(int8_t)(int16_t)w[11];in.grade=(uint8_t)w[12];in.light_level=(uint8_t)w[12];in.preset=(uint8_t)w[13];in.expected_version=g32(w+14);in.page_active=(w[18]&1U)!=0U;in.held=(w[18]&2U)!=0U;in.budget_ms=w[19];
 if(cmd!=4U && cmd!=3U && cmd!=2U && !h.released)status=4U;
 if((cmd>=4U && cmd<=12U) && (w[9]<1U || w[9]>3U))status=6U;if(cmd==5U && (w[19]<100U || w[19]>300U || w[10]<1U || w[10]>6U || (w[11]!=1U && w[11]!=65535U) || w[12]<1U || w[12]>5U))status=6U;if((cmd==6U || cmd==10U || cmd==11U) && (w[13]<1U || w[13]>4U))status=6U;if(cmd==8U && (w[12]<1U || w[12]>255U))status=6U;
 if(cmd==7U || cmd==11U){if(!h.edit_valid || g32(w+16)!=h.edit_revision || w[9]!=h.edit[8])status=6U;else for(i=0;i<6U;++i)in.angle[i]=(int32_t)g32(h.edit+10U+i*2U);}
 if(cmd!=2U)h.last_core_seq=0U;if(!status){status=submit(&in,now,&op);if(cmd!=2U)h.last_core_seq=h.submit_seq;}if(cmd==5U && !status)h.released=false;
 if(cmd==2U)return true;
 memcpy(h.command,w,48U);h.last_business=input;h.last_op=op;h.last_mask=in.mask;result(input,cmd,status,op,now);
 h.cache[h.cursor].used=true;h.cache[h.cursor].seq=input;h.cache[h.cursor].at=now;memcpy(h.cache[h.cursor].command,w,48U);memcpy(h.cache[h.cursor].result,h.result,64U);h.cursor=(uint8_t)((h.cursor+1U)%32U);return true;
}
bool app_hmi_arm3_read(uint16_t start,uint16_t count,uint16_t *out,uint32_t now){
 const uint16_t *p=0;uint16_t base=0,size=0;
 if(!out || !count)return false;app_hmi_arm3_service(now);
 if(start>=0x0500U && start<0x0518U){p=h.command;base=0x0500U;size=24U;}
 else if(start>=0x0520U && start<0x0538U){p=h.edit;base=0x0520U;size=24U;}
 else if(start>=0x0540U && start<0x0560U){p=h.result;base=0x0540U;size=32U;}
 else if(start>=0x0580U && start<0x05C0U){p=h.global;base=0x0580U;size=64U;}
 else if(start>=0x05C0U && start<0x0600U){p=h.side[0];base=0x05C0U;size=64U;}
 else if(start>=0x0600U && start<0x0640U){p=h.side[1];base=0x0600U;size=64U;}
 if(!p || (uint32_t)(start-base)+count>size)return false;memcpy(out,p+(start-base),(size_t)count*2U);return true;
}
