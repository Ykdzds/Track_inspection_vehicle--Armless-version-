#include "app_arm3_inputs.h"
#include "app_arm3.h"
#include <string.h>
typedef struct {uint32_t event;uint8_t body[8];uint32_t operation;uint8_t result;bool used;} event_t;
static struct {
 uint32_t boot,session,serial,input,event,at,challenge_at,previous_at,submit_seq;
 uint16_t challenge,previous,previous2,held;uint8_t mask,joint,result,ack_seq;
 bool active,released,seen;event_t cache[32];uint8_t cursor;
} r;
static uint16_t crc16(const uint8_t *p,unsigned n){uint16_t c=0xFFFFU;unsigned i,b;for(i=0;i<n;++i){c^=(uint16_t)p[i]<<8;for(b=0;b<8;++b)c=(uint16_t)((c&0x8000U)?((uint32_t)c<<1)^0x1021U:(uint32_t)c<<1);}return c;}
static uint32_t get32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static uint32_t submit(app_arm3_intent_t *in,uint32_t now){uint32_t op=0;in->source=1U;in->session=r.session;in->sequence=++r.submit_seq;if(!in->sequence){r.active=false;return 0;}{uint8_t v=app_arm3_submit(in,now,&op);if(in->command!=APP_ARM3_ALIVE)r.result=v;}return op;}
void app_arm3_inputs_init(uint32_t boot){memset(&r,0,sizeof(r));r.boot=boot;r.challenge=1U;}
void app_arm3_inputs_service(uint32_t now){
 if((uint32_t)(now-r.challenge_at)>=100U){uint32_t n=(now-r.challenge_at)/100U;r.previous=n==1U?r.challenge:0U;r.previous_at=r.challenge_at;r.challenge=(uint16_t)(r.challenge+n);if(!r.challenge)++r.challenge;r.challenge_at=now;}
 if(r.active && (uint32_t)(now-r.at)>=300U){app_arm3_snapshot_t s;app_arm3_snapshot(&s,now);if(s.owner==1U)app_arm3_global_stop(now);r.active=false;r.released=false;r.session=0U;}
}
bool app_arm3_remote_active(void){return r.active;}
bool app_arm3_remote_receive(const uint8_t *p,uint8_t length,uint32_t now){
 app_arm3_intent_t in;uint32_t session,input,event,operation=0;uint16_t held,challenge;unsigned i;uint8_t opcode;
 if(!p || length<4U || p[0]!=0xA5U || p[1]!=0x5AU || p[2]!=2U || p[3]!=2U)return false;
 app_arm3_inputs_service(now);
 if(length!=32U || p[5]!=22U || p[30]!=13U || p[31]!=10U || crc16(p,28U)!=((uint16_t)p[28]<<8|p[29]))return true;
 session=get32(p+6);input=get32(p+10);event=get32(p+14);held=(uint16_t)((uint16_t)p[18]<<8|p[19]);opcode=p[23];challenge=(uint16_t)((uint16_t)p[26]<<8|p[27]);
 if((held&0xF000U) || (p[25]&0xFEU) || !input || opcode>13U || !opcode){r.result=6U;return true;}
 if(opcode==13U){
  if(session || held || !(p[25]&1U)){r.result=6U;return true;}
  if(!r.session || (r.seen && input<=r.input)){app_arm3_snapshot_t snap;app_arm3_snapshot(&snap,now);if(snap.owner==1U)app_arm3_global_stop(now);r.seen=false;if(++r.serial==0U)++r.serial;r.session=r.boot^(0xA3000000UL+r.serial);if(!r.session)r.session=r.serial;r.input=r.event=r.submit_seq=0U;r.released=false;memset(r.cache,0,sizeof(r.cache));}
  r.active=true;r.at=now;r.result=0U;return true;
 }
 if(!session || session!=r.session){r.result=7U;return true;}
 if(opcode!=APP_ARM3_STOP && !((challenge==r.challenge && now-r.challenge_at<=200U)||(challenge==r.previous && r.previous && now-r.previous_at<=200U))){r.result=7U;return true;}
 if(input<=r.input){r.result=7U;return true;}
 r.input=input;r.seen=true;r.at=now;r.active=(p[25]&1U)!=0U;r.held=held;r.mask=p[20];r.joint=p[21];
 if(!held)r.released=true;
 memset(&in,0,sizeof(in));in.page_active=r.active;in.mask=p[20];in.joint=p[21]>=1U && p[21]<=6U?(uint8_t)(p[21]-1U):255U;in.grade=p[22];in.held=(held&0x000CU)!=0U;
 /* Every fresh physical sample updates owner activity; it never starts another motion. */
 in.command=APP_ARM3_ALIVE;in.direction=(opcode==APP_ARM3_JOG || opcode==APP_ARM3_ALIVE)?(int8_t)p[24]:0;
 if(opcode!=APP_ARM3_STOP)(void)submit(&in,now);
 if(opcode==APP_ARM3_ALIVE)return true;
 if(!event){r.result=6U;return true;}
 for(i=0;i<32U;++i)if(r.cache[i].used && r.cache[i].event==event){
  r.result=memcmp(r.cache[i].body,p+20,6U)==0?r.cache[i].result:14U;return true;}
 if(event<=r.event){r.result=7U;return true;}r.event=event;
 if(opcode!=APP_ARM3_STOP && opcode!=APP_ARM3_RELEASE && (!r.released || !r.active)){r.result=4U;return true;}
 if(opcode==APP_ARM3_APPLY && held){r.result=6U;return true;}
 in.command=opcode;in.direction=(int8_t)p[24];in.preset=p[24];in.light_level=p[24];in.budget_ms=opcode==APP_ARM3_JOG?30000U:10000U;
 if(opcode==APP_ARM3_JOG && (in.mask==3U || (in.direction!=1 && in.direction!=-1) || !in.held)){r.result=6U;return true;}
 operation=submit(&in,now);
 r.cache[r.cursor].used=true;r.cache[r.cursor].event=event;memcpy(r.cache[r.cursor].body,p+20,6U);r.cache[r.cursor].result=r.result;r.cache[r.cursor].operation=operation;r.cursor=(uint8_t)((r.cursor+1U)%32U);
 return true;
}
bool app_arm3_remote_ack(uint8_t type,uint8_t out[32],uint32_t now){
 app_arm3_snapshot_t s;app_arm3_side_snapshot_t *a;uint16_t crc;unsigned side;
 if(!out || (type!=0x8BU && type!=0x8CU))return false;
 app_arm3_inputs_service(now);app_arm3_snapshot(&s,now);memset(out,0,32);out[0]=0x5AU;out[1]=0xA5U;out[2]=2U;out[3]=type;out[4]=++r.ack_seq;out[5]=22U;put32(out+6,r.boot);
 if(type==0x8BU){put32(out+10,r.session);put32(out+14,r.event);out[18]=s.owner;out[19]=s.state;out[20]=s.permit_mask;out[21]=(uint8_t)((r.active?1U:0U)|(r.released?2U:0U));put16(out+22,r.result);put16(out+24,r.challenge);put16(out+26,s.source_age_ms>65535U?65535U:(uint16_t)s.source_age_ms);}
 else {side=r.mask==2U?1U:0U;a=&s.side[side];out[10]=(uint8_t)(side+1U);out[11]=(uint8_t)((a->online?1U:0U)|((r.joint>=1U && r.joint<=6U && (a->angle_mask&(1U<<(r.joint-1U))))?2U:0U));put16(out+12,a->feedback_age_ms>65535U?65535U:(uint16_t)a->feedback_age_ms);put32(out+14,(uint32_t)((r.joint>=1U && r.joint<=6U && (a->angle_mask&(1U<<(r.joint-1U))))?a->angle[r.joint-1U]:INT32_MIN));put32(out+18,s.arm_version);put32(out+22,a->operation);out[26]=a->phase;out[27]=a->result;}
 crc=crc16(out,28U);put16(out+28,crc);out[30]=13U;out[31]=10U;return true;
}
