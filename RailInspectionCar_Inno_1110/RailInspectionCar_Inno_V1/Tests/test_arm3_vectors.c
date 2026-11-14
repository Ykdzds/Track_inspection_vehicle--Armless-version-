#include "can_arm3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Read external Python golden payload, use the production C encoder/reassembler. */
int main(int argc,char **argv){can_protocol_record_t r,m;can_protocol_reassembly_t rx;unsigned i,v;uint16_t id;uint8_t data[8];
 if(argc!=5)return 2;memset(&r,0,sizeof(r));r.type=(uint8_t)strtoul(argv[1],NULL,0);r.sequence=(uint16_t)strtoul(argv[2],NULL,0);r.token=(uint16_t)strtoul(argv[3],NULL,0);
 if(strlen(argv[4])%2U || strlen(argv[4])>512U)return 2;r.length=(uint16_t)(strlen(argv[4])/2U);
 for(i=0U;i<r.length;++i){if(sscanf(argv[4]+i*2U,"%2x",&v)!=1)return 2;r.payload[i]=(uint8_t)v;}
 if(r.type==0x5FU && r.length==64U && r.payload[8]==0U){can_arm3_manifest(&m,r.payload[34]);if(m.length!=r.length || memcmp(m.payload+32U,r.payload+32U,32U) || !can_arm3_manifest_matches(&r,r.payload[34]))return 3;}
 can_protocol_reassembly_reset(&rx);
 for(i=0U;i<(r.length+3U)/4U+2U;++i){unsigned j;can_protocol_result_t result;if(!can_protocol_encode_record_frame(&r,(uint16_t)i,&id,data))return 4;
  result=can_protocol_receive_record(&rx,(uint16_t)(id+0x10U),data,r.token,i);if(result!=(i==(r.length+3U)/4U+1U?CAN_PROTOCOL_COMPLETE:CAN_PROTOCOL_PROGRESS))return 5;
  printf("%u ",id);for(j=0U;j<8U;++j)printf("%02x",data[j]);puts("");}
 return rx.record.length!=r.length || memcmp(rx.record.payload,r.payload,r.length)?6:0;
}
