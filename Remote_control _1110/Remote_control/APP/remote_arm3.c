#include "remote_arm3.h"
#include <string.h>
#include <stdio.h>
#define KEY(n) ((uint16_t)(1U<<((n)-1U)))
static struct {uint32_t boot,session,input,event,cap_at,ack_at;uint16_t challenge,held,oldheld;uint8_t mode,cap,mask,joint,grade,preset,light,owner,state,permit,reason,pending,arg,exit,wait_release,jog,event_mask,event_joint,event_grade;uint32_t operation,version;int32_t actual;uint16_t age;} a;
static uint16_t crc16(const uint8_t *p,unsigned n){uint16_t c=0xFFFFU;unsigned i,b;for(i=0;i<n;++i){c^=(uint16_t)p[i]<<8;for(b=0;b<8;++b)c=(uint16_t)((c&0x8000U)?((uint32_t)c<<1)^0x1021U:(uint32_t)c<<1);}return c;}
static uint32_t g32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void p32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void event(uint8_t op,uint8_t arg){if(a.pending && op!=4U)return;a.pending=op;a.arg=arg;a.event_mask=a.mask;a.event_joint=a.joint;a.event_grade=a.grade;if(++a.event==0U){a.mode=0U;a.session=0U;}if(op==4U){a.jog=0U;a.wait_release=1U;}}
void remote_arm3_init(void){memset(&a,0,sizeof(a));a.mask=a.joint=a.grade=a.preset=1U;a.actual=INT32_MIN;a.age=65535U;}
uint8_t remote_arm3_active(void){return a.mode;}
void remote_arm3_keys(uint16_t s,uint16_t l,uint16_t d,uint16_t held,uint16_t k9,uint32_t now){
 uint16_t down=(uint16_t)(held&~a.oldheld);uint8_t dir; a.oldheld=held;a.held=held;
 if(l&KEY(10)){if(a.mode){event(4U,0U);a.exit=1U;}else if(a.cap && now-a.cap_at<=500U){a.mode=1U;a.session=a.input=a.event=0U;a.pending=0U;a.wait_release=1U;a.exit=0U;}}
 if(!a.mode)return;
 if(a.session && now-a.ack_at>300U){a.session=0U;a.pending=0U;a.jog=0U;a.owner=0U;a.wait_release=1U;}
 if(!held && !a.jog)a.wait_release=0U;
 if(d&KEY(10))event(3U,0U);else if(s&KEY(10))event(1U,0U);
 if(d&KEY(12)){a.light^=1U;event(a.light?8U:9U,a.light?128U:0U);}
 else if(l&KEY(12))event(6U,a.preset);else if(s&KEY(12))a.preset=(uint8_t)(a.preset%4U+1U);
 if((s&KEY(11)) || k9 || (down&(KEY(1)|KEY(2))) || (s&(KEY(7)|KEY(8)))){
  event(4U,0U);if(s&KEY(11))a.mask=(uint8_t)(a.mask%3U+1U);if(k9)a.joint=(uint8_t)((a.joint-1U+k9)%6U+1U);if((down&KEY(1)) && a.grade<5U)++a.grade;if((down&KEY(2)) && a.grade>1U)--a.grade;
 }
 if((down&KEY(5)) || (s&KEY(5))){event(4U,0U);a.event_mask=3U;}
 else if(s&KEY(6))event(4U,0U);
 dir=(held&(KEY(3)|KEY(4)))==KEY(3)?1U:(held&(KEY(3)|KEY(4)))==KEY(4)?255U:0U;
 if(a.jog && (!dir || dir!=a.jog)){event(4U,0U);}
 if((held&(KEY(3)|KEY(4)))==(KEY(3)|KEY(4)))event(4U,0U);
 if(dir && !a.jog && !a.wait_release && !a.pending && a.mask!=3U && a.owner==1U && a.session){event(5U,dir);a.jog=dir;}
}
uint8_t remote_arm3_ack(const uint8_t *p,uint8_t n,uint32_t now){uint32_t session,boot,accepted;
 if(!p || n!=32U || p[0]!=0x5AU || p[1]!=0xA5U || p[5]!=22U || p[30]!=13U || p[31]!=10U || crc16(p,28U)!=((uint16_t)p[28]<<8|p[29]))return 0U;
 if(p[2]==1U && p[3]==0x8AU){if(p[21]==2U){a.cap=(p[22]&2U)!=0U;a.cap_at=now;}return 0U;}
 if(p[2]!=2U || (p[3]!=0x8BU && p[3]!=0x8CU))return 0U;
 boot=g32(p+6);if(p[3]==0x8BU){session=g32(p+10);accepted=g32(p+14);if(a.boot!=boot){a.boot=boot;a.session=0U;a.pending=a.jog=0U;a.wait_release=1U;}
  if(!a.mode)return 1U;if(a.session && a.session!=session){a.session=0U;a.pending=a.jog=0U;a.wait_release=1U;return 1U;}
  if(session){a.session=session;a.ack_at=now;a.challenge=(uint16_t)((uint16_t)p[24]<<8|p[25]);a.owner=p[18];a.state=p[19];a.permit=p[20];a.reason=p[23];if(a.pending && accepted==a.event){uint8_t done=a.pending;a.pending=0U;if(done==4U && a.exit==1U){a.exit=2U;event(3U,0U);}else if(done==3U && a.exit){a.mode=a.exit=0U;}}}
 }else if(boot==a.boot){a.age=(uint16_t)((uint16_t)p[12]<<8|p[13]);a.actual=(int32_t)g32(p+14);a.version=g32(p+18);a.operation=g32(p+22);}
 return 1U;
}
uint8_t remote_arm3_frame(uint8_t out[32],uint8_t seq,uint32_t now){uint16_t c;uint8_t op;
 if(!a.mode)return 0U;if(a.session && now-a.ack_at>300U){a.session=0U;a.pending=a.jog=0U;a.wait_release=1U;}
 memset(out,0,32U);out[0]=0xA5U;out[1]=0x5AU;out[2]=2U;out[3]=2U;out[4]=seq;out[5]=22U;p32(out+6,a.session);if(++a.input==0U){a.session=0U;a.input=1U;}p32(out+10,a.input);p32(out+14,a.event);out[18]=(uint8_t)(a.held>>8);out[19]=(uint8_t)a.held;out[20]=a.pending?a.event_mask:a.mask;out[21]=a.pending?a.event_joint:a.joint;out[22]=a.pending?a.event_grade:a.grade;op=a.session?(a.pending?a.pending:2U):13U;out[23]=op;out[24]=a.pending?a.arg:a.jog;out[25]=1U;if(!a.session){out[18]=out[19]=0U;}out[26]=(uint8_t)(a.challenge>>8);out[27]=(uint8_t)a.challenge;c=crc16(out,28U);out[28]=(uint8_t)(c>>8);out[29]=(uint8_t)c;out[30]=13U;out[31]=10U;return 1U;
}
void remote_arm3_display(remote_display_t *d,uint32_t now){unsigned i;char lines[8][40];if(!a.mode)return;
 memset(lines,0,sizeof(lines));snprintf(lines[0],40,"ARM3 %s %s",a.session?"LINK":"HELLO",now-a.ack_at<=300U?"OK":"WAIT");snprintf(lines[1],40,"SIDE %s J%u G%u",a.mask==1U?"L":a.mask==2U?"R":"B",a.joint,a.grade);snprintf(lines[2],40,"OWNER %u STATE %u",a.owner,a.state);snprintf(lines[3],40,"PERMIT %u ERR %u",a.permit,a.reason);snprintf(lines[4],40,"PRESET %u LIGHT %u",a.preset,a.light);if(a.actual==INT32_MIN)snprintf(lines[5],40,"ACT -- AGE %u",a.age);else snprintf(lines[5],40,"ACT %ld AGE %u",(long)a.actual,a.age);snprintf(lines[6],40,"OP %lu %s",(unsigned long)a.operation,a.jog?"JOG":"IDLE");snprintf(lines[7],40,"K5 STOP K10 OWNER");d->item_count=8U;d->region_count=0U;for(i=0;i<8U;++i){d->items[i].x=0U;d->items[i].y=(uint8_t)(i*8U);d->items[i].font=6U;strcpy(d->items[i].text,lines[i]);}}
