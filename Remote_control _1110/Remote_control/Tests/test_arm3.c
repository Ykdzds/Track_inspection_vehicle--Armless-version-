#include "remote_arm3.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t crc16(const uint8_t *p,unsigned n){uint16_t c=0xFFFFU;unsigned i,b;for(i=0;i<n;++i){c^=(uint16_t)p[i]<<8;for(b=0;b<8;++b)c=(uint16_t)((c&0x8000U)?((uint32_t)c<<1)^0x1021U:(uint32_t)c<<1);}return c;}
static void seal(uint8_t *p){uint16_t c=crc16(p,28);p[28]=(uint8_t)(c>>8);p[29]=(uint8_t)c;}
int main(void){uint8_t p[32]={0},out[32];remote_display_t display;
 remote_arm3_init();remote_arm3_keys(0,0x0200,0,0,0,0);assert(!remote_arm3_active());p[0]=0x5A;p[1]=0xA5;p[2]=1;p[3]=0x8A;p[5]=22;p[21]=2;p[22]=2;p[30]=13;p[31]=10;seal(p);remote_arm3_ack(p,32,0);remote_arm3_keys(0,0x0200,0,0,0,1);assert(remote_arm3_active());assert(remote_arm3_frame(out,1,1));assert(out[23]==13 && out[2]==2 && out[5]==22);
 memset(p+6,0,22);p[2]=2;p[3]=0x8B;p[9]=9;p[13]=8;p[18]=1;p[20]=3;p[25]=7;seal(p);assert(remote_arm3_ack(p,32,2));remote_arm3_keys(0,0,0,0,0,3);remote_arm3_keys(0,0,0,4,0,4);remote_arm3_frame(out,2,4);assert(out[23]==5 && out[24]==1 && out[19]==4);memcpy(p+14,out+14,4);seal(p);remote_arm3_ack(p,32,5);remote_arm3_frame(out,3,6);assert(out[23]==2 && out[24]==1);remote_arm3_keys(0,0,0,0,0,7);remote_arm3_frame(out,4,7);assert(out[23]==4);
 remote_arm3_keys(0,0x0010,0,0,0,8);remote_arm3_frame(out,5,8);assert(out[23]==4); /* K5 long never chassis apply */
 remote_arm3_display(&display,8);assert(display.item_count==8 && strstr(display.items[0].text,"ARM3"));remote_arm3_frame(out,6,400);assert(out[23]==13);assert(out[18]==0 && out[19]==0);
 puts("PASS remote ARM3 capability, hello, held jog/alive/release, K5 isolation, loss handshake, ARM OLED");return 0;}
