#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "OLED.h"
#include "can_upper_ui.h"
extern uint8_t OLED_DisplayBuf[8][128];
int hi2c1;
int HAL_I2C_Mem_Write(int *bus,unsigned address,unsigned reg,unsigned mode,uint8_t *data,unsigned length,unsigned timeout)
{ (void)bus;(void)address;(void)reg;(void)mode;(void)data;(void)length;(void)timeout;return 0; }
void HAL_Delay(uint32_t delay){(void)delay;}
static void Blank(void){ unsigned row,col; for(row=0;row<8U;++row)for(col=0;col<128U;++col)assert(OLED_DisplayBuf[row][col]==0U); }
int main(void)
{
    char long_text[1025], output[24]; uint8_t expected[8][128];
    UpperUiView_t view; unsigned character,page,line,capacity,index,cases=0U;
    memset(long_text,'8',sizeof(long_text));long_text[1024]=0;
    OLED_Clear(); OLED_ShowChar(0,0,'?',OLED_6X8);memcpy(expected,OLED_DisplayBuf,sizeof(expected));
    for(character=0;character<256U;++character){
        OLED_Clear(); OLED_ShowChar(0,0,(char)character,OLED_6X8);
        if(character<32U || character>126U)assert(memcmp(expected,OLED_DisplayBuf,sizeof(expected))==0);
        ++cases;
    }
    OLED_Clear(); OLED_ShowStringClipped(0,0,long_text,1024,128,OLED_6X8);
    for(index=126U;index<128U;++index)assert(OLED_DisplayBuf[0][index]==0U);
    memcpy(expected,OLED_DisplayBuf,sizeof(expected));OLED_Clear();OLED_ShowString(0,0,long_text,OLED_6X8);
    assert(memcmp(expected,OLED_DisplayBuf,sizeof(expected))==0);
    OLED_Clear(); OLED_ShowStringClipped(0,57,long_text,99,128,OLED_6X8);
    OLED_ShowStringClipped(-1,0,long_text,99,128,OLED_6X8);OLED_ShowStringClipped(128,0,long_text,99,128,OLED_6X8);
    OLED_ShowStringClipped(0,64,long_text,99,128,OLED_6X8);OLED_ShowStringClipped(0,-1,long_text,99,128,OLED_6X8);
    OLED_ShowStringClipped(0,0,NULL,99,128,OLED_6X8);OLED_ShowStringClipped(0,0,long_text,0,128,OLED_6X8);
    OLED_ShowStringClipped(0,0,long_text,99,128,7);OLED_ShowStringClipped(0,49,long_text,99,128,OLED_8X16);Blank();
    OLED_ShowImage(0,0,6,8,NULL);Blank();
    OLED_Printf(0,0,OLED_6X8,"%s",long_text);assert(memcmp(expected,OLED_DisplayBuf,sizeof(expected))==0);
    OLED_Clear();OLED_Printf(0,0,OLED_6X8,NULL);Blank();
    memset(&view,0xFF,sizeof(view));view.left=32767;view.right=-32768;
    for(page=0U;page<6U;++page)for(line=0U;line<8U;++line)for(capacity=0U;capacity<=22U;++capacity){
        memset(output,0x5A,sizeof(output));view.page=(uint8_t)page;
        UpperUi_Format(&view,(uint8_t)line,output+1,capacity);
        assert(output[0]==0x5A && output[capacity+1]==0x5A);
        if(capacity && !memchr(output+1,0,capacity)){printf("unterminated page=%u line=%u capacity=%u\n",page,line,capacity);fflush(stdout);abort();}
        ++cases;
    }
    UpperUi_Format(NULL,0,output,sizeof(output));assert(output[0]==0);
    UpperUi_Format(&view,255,output,sizeof(output));assert(output[0]==0);
    UpperUi_Format(&view,0,NULL,0);
    OLED_Clear();OLED_ShowString(0,0,long_text,OLED_6X8);OLED_Clear();OLED_ShowString(0,0,"1",OLED_6X8);
    for(index=6U;index<128U;++index)assert(OLED_DisplayBuf[0][index]==0U);
    for(page=0U;page<64U;++page)for(index=0U;index<128U;++index){
        OLED_Clear();OLED_ShowStringClipped((int16_t)index,(int16_t)page,long_text,1024,128,OLED_6X8);++cases;
    }
    printf("PASS OLED actual driver: %u cases; font indexing/clip/null/extremes/format capacities/long-to-short/no wrap\n",cases);
    return 0;
}
