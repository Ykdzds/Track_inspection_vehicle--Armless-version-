#include "app_arm3_inputs.h"
#include "app_arm3.h"
#include "app_hmi_arm3.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static app_arm3_snapshot_t snap;
static app_arm3_intent_t last;
static unsigned submits,stops,motions;static bool fake_results;
uint8_t app_arm3_submit(const app_arm3_intent_t *in,uint32_t now,uint32_t *op){(void)now;last=*in;++submits;if(in->command>=5U && in->command<=11U){++motions;*op=motions;}else *op=0;return 0;}
bool app_arm3_get_input_result(uint8_t a,uint32_t b,uint32_t c,app_arm3_input_result_t *d){(void)a;(void)b;(void)c;(void)d;return false;}
void app_arm3_snapshot(app_arm3_snapshot_t *out,uint32_t now){*out=snap;out->now_ms=now;}
void app_arm3_global_stop(uint32_t now){(void)now;++stops;}
bool app_arm3_get_result(uint32_t op,uint8_t side,uint32_t now,can_arm3_result_t *r){(void)now;if(!fake_results)return false;memset(r,0,sizeof(*r));r->operation=op;r->arm_id=side;r->phase=2U;r->result=side==1U?1U:13U;r->detail=99U;return true;}
bool app_arm3_request_result(uint32_t op,uint8_t mask,uint32_t now){(void)op;(void)mask;(void)now;return false;}
static void p32(uint8_t *p,uint32_t x){p[0]=(uint8_t)(x>>24);p[1]=(uint8_t)(x>>16);p[2]=(uint8_t)(x>>8);p[3]=(uint8_t)x;}
static uint32_t g32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint16_t crc16(const uint8_t *p,unsigned n){uint16_t c=0xFFFFU;unsigned i,b;for(i=0;i<n;++i){c^=(uint16_t)p[i]<<8;for(b=0;b<8;++b)c=(uint16_t)((c&0x8000U)?((uint32_t)c<<1)^0x1021U:(uint32_t)c<<1);}return c;}
static void seal(uint8_t *p){uint16_t c=crc16(p,28);p[28]=(uint8_t)(c>>8);p[29]=(uint8_t)c;}
static void word32(uint16_t *p,uint32_t v){p[0]=(uint16_t)(v>>16);p[1]=(uint16_t)v;}
int main(void){uint8_t p[32]={0},ack[32];uint32_t session;uint16_t w[24]={0},global[64],result[32],side[64];unsigned before;
 app_arm3_inputs_init(99U);p[0]=0xA5;p[1]=0x5A;p[2]=2;p[3]=2;p[5]=22;p[23]=13;p[25]=1;p[30]=13;p[31]=10;p32(p+10,1);seal(p);assert(app_arm3_remote_receive(p,32,0));assert(app_arm3_remote_ack(0x8B,ack,0));session=g32(ack+10);assert(session);
 p32(p+6,session);p32(p+10,2);p[23]=2;p[26]=ack[24];p[27]=ack[25];seal(p);assert(app_arm3_remote_receive(p,32,10));assert(last.command==2);
 p32(p+10,3);p32(p+14,1);p[20]=1;p[21]=2;p[22]=3;p[23]=5;p[24]=1;p[19]=4;seal(p);app_arm3_remote_receive(p,32,20);assert(motions==1 && last.direction==1 && last.held && last.grade==3);
 p32(p+10,4);seal(p);app_arm3_remote_receive(p,32,30);assert(motions==1);p32(p+10,5);p[24]=255;seal(p);app_arm3_remote_receive(p,32,40);assert(motions==1);app_arm3_remote_ack(0x8B,ack,40);assert(ack[23]==14U);
 p[23]=2;p[24]=1;p32(p+10,6);seal(p);app_arm3_remote_receive(p,32,50);assert(last.command==2 && last.direction==1 && last.held);
 p[23]=4;p[19]=0;p32(p+14,2);p32(p+10,7);seal(p);app_arm3_remote_receive(p,32,60);assert(last.command==4);
 before=submits;p32(p+10,8);seal(p);app_arm3_remote_receive(p,32,70);assert(submits==before); /* no ALIVE side effect before stop replay */
 snap.owner=1;app_arm3_inputs_service(370);assert(stops==1 && !app_arm3_remote_active());
 app_hmi_arm3_init(99);app_hmi_arm3_service(0);assert(app_hmi_arm3_read(0x0580,64,global,0));assert(global[0]==0x4153 && global[2]==global[62] && global[3]==global[63]);assert(app_hmi_arm3_read(0x05C0,64,side,0));assert(side[36]==0x8000 && side[37]==0);
 w[0]=0x4152;w[1]=1;word32(w+6,1);w[8]=13;word32(w+16,77);w[22]=3;w[23]=0xA55A;assert(app_hmi_arm3_write(0x0500,24,w,0));assert(app_hmi_arm3_read(0x0540,32,result,0));session=((uint32_t)result[2]<<16)|result[3];assert(session);
 word32(w+2,99);word32(w+4,session);word32(w+6,2);w[8]=2;w[18]=1;w[20]=global[2];w[21]=global[3];assert(app_hmi_arm3_write(0x0500,24,w,10));
 word32(w+6,3);w[8]=5;w[9]=1;w[10]=2;w[11]=1;w[12]=3;w[18]=3;w[19]=300;assert(app_hmi_arm3_write(0x0500,24,w,20));assert(last.command==5 && last.budget_ms==300 && last.source==3);before=motions;
 assert(app_hmi_arm3_write(0x0500,24,w,30));assert(motions==before);w[12]=4;assert(app_hmi_arm3_write(0x0500,24,w,40));assert(motions==before);assert(app_hmi_arm3_read(0x0540,32,result,40));assert(result[7]==14);
 word32(w+6,4);w[8]=2;w[18]=1;assert(app_hmi_arm3_write(0x0500,24,w,50));assert(motions==before);assert(app_hmi_arm3_read(0x0540,32,result,50));assert(result[7]==14); /* alive preserves business result */
 word32(w+6,5);w[8]=5;w[19]=301;assert(app_hmi_arm3_write(0x0500,24,w,60));assert(motions==before);
 word32(w+6,6);w[8]=4;w[20]=w[21]=0;assert(app_hmi_arm3_write(0x0500,24,w,1000));assert(last.command==4);before=submits;assert(app_hmi_arm3_write(0x0500,24,w,1010));assert(submits==before);
 app_hmi_arm3_service(1000U);assert(app_hmi_arm3_read(0x0580,64,global,1000U));
 word32(w+6,7);w[8]=6;w[9]=3;w[13]=1;w[18]=1;w[20]=global[2];w[21]=global[3];
 assert(app_hmi_arm3_write(0x0500,24,w,1010U));fake_results=true;app_hmi_arm3_service(1100U);
 assert(app_hmi_arm3_read(0x0540,32,result,1100U));assert(result[10]==2U && result[11]==13U);
 snap.side[0].online=true;snap.side[0].angle_mask=63U;snap.side[0].active=true;snap.side[0].stopping=true;
 snap.side[0].diagnostic_valid=snap.side[0].identity_valid=snap.side[0].config_valid=true;
 snap.side[0].transport_approved=snap.side[0].feedback_real=true;snap.side[0].proof_state=2U;snap.side[0].result_flags=1U;
 snap.parameter_locked=snap.chassis_busy=true;app_hmi_arm3_service(1200U);
 assert(app_hmi_arm3_read(0x05C0,64,side,1200U));assert(side[9]==0x0FFFU);
 assert(app_hmi_arm3_read(0x0580,64,global,1200U));assert((global[17]&0x600U)==0x600U);
 puts("PASS ARM3 input sessions, CRC, stale/replay/conflict, held evidence, bounded touch, stop dedup, coherent unknown snapshots");return 0;
}
