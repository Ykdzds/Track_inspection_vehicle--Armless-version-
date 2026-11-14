#include "remote_display.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint8_t sequence;
static uint8_t session = 1U;
static uint8_t axes = 3U;
static uint8_t frame[32];
static remote_debug_view_t debug = {{2000U, 2001U, 2002U, 2003U}, 65535U, 255U,
    "111111111111", "111111111111", "111111111111", "OK"};

static void put16(uint8_t *data, int32_t value)
{
    data[0] = (uint8_t)((uint16_t)value >> 8U); data[1] = (uint8_t)value;
}

static void make(uint8_t type)
{
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x5AU; frame[1] = 0xA5U; frame[2] = 1U; frame[3] = type;
    frame[4] = sequence++; frame[5] = 22U; frame[6] = session;
    frame[7] = axes; frame[8] = 0x7FU; frame[30] = 13U; frame[31] = 10U;
}

static void accept(remote_status_model_t *model, uint32_t now)
{
    put16(frame + 28U, remote_status_crc16(frame, 28U));
    assert(remote_status_accept(model, frame, sizeof(frame), now));
}

static void send_all(remote_status_model_t *model, uint32_t now)
{
    uint8_t axis, type;
    for (type = 0x81U; type <= 0x89U; type++) {
        make(type);
        switch (type) {
        case 0x81U:
            frame[12] = 7U; frame[13] = 4U; frame[14] = 127U;
            put16(frame + 16U, 1200); put16(frame + 18U, 1190);
            put16(frame + 20U, 1200); put16(frame + 22U, 1198);
            put16(frame + 24U, 1180); put16(frame + 26U, 1179); break;
        case 0x82U:
            frame[12] = frame[13] = 255U;
            put16(frame + 18U, 1198); put16(frame + 20U, 1179);
            put16(frame + 22U, 10); put16(frame + 24U, 12); break;
        case 0x83U:
            put16(frame + 14U, 100); put16(frame + 18U, 200);
            put16(frame + 20U, -10); put16(frame + 22U, -100);
            put16(frame + 24U, 12345); put16(frame + 26U, 1000); break;
        case 0x84U:
            frame[12] = 15U; frame[13] = 1U; frame[14] = 1U;
            put16(frame + 16U, 19); put16(frame + 18U, -2);
            put16(frame + 20U, 3); break;
        case 0x85U:
            frame[12] = 1U; frame[13] = 80U; frame[14] = frame[15] = 1U;
            frame[24] = 1U; frame[25] = 2U; frame[26] = 2U; break;
        case 0x86U:
            for (axis = 0U; axis < 2U; axis++) {
                put16(frame + 12U + axis * 2U, 10000);
                put16(frame + 16U + axis * 2U, 185);
                frame[20U + axis] = 1U; put16(frame + 22U + axis * 2U, 2667);
            }
            frame[26] = frame[27] = 1U; break;
        case 0x87U: frame[12] = 1U; break;
        case 0x88U:
            put16(frame + 10U, 1005); frame[23] = 0xA1U;
            put16(frame + 12U, 4359); put16(frame + 14U, 4320);
            put16(frame + 16U, 4350); put16(frame + 18U, 4280);
            put16(frame + 20U, 4315); frame[22] = 31U;
            put16(frame + 24U, 10); put16(frame + 26U, 12); break;
        case 0x89U:
            for (axis = 0U; axis < 2U; axis++) {
                put16(frame + 12U + axis * 8U, 0x8000U | 123U);
                put16(frame + 14U + axis * 8U, 0x8000U | (axis ? 402U : 456U));
                put16(frame + 16U + axis * 8U, 0x8000U | (axis ? 21U : 23U));
                put16(frame + 18U + axis * 8U, 0x8000U | (axis ? 481U : 482U));
            }
            break;
        default: break;
        }
        accept(model, now);
    }
}


#include "OLED.h"
#include "OLED_Text.h"
#include "OLED_ChineseFont.h"
extern uint8_t OLED_DisplayBuf[8][128];
static FILE *preview;
static const char *at(const remote_display_t *d,uint8_t y)
{for(unsigned i=0;i<d->item_count;i++)if(d->items[i].y==y)return d->items[i].text;return "";}
static uint8_t has(const remote_display_t *d,const char *s)
{for(unsigned i=0;i<d->item_count;i++)if(strstr(d->items[i].text,s))return 1U;return 0U;}
static void check(remote_display_t *d,const remote_status_model_t *m,uint8_t page,uint32_t now)
{
    d->requested_language=REMOTE_DISPLAY_CHINESE;d->requested_page=page;remote_display_build(d,m,&debug,now,1U,1U);
    if(d->truncated) {fprintf(stderr,"overflow page %u:",page+1U);for(unsigned i=0;i<d->item_count;i++)fprintf(stderr," [%s]",d->items[i].text);fputc('\n',stderr);}
    assert(!d->truncated);assert(d->item_count==6U);
    for(unsigned i=0;i<d->item_count;i++) {
        const remote_display_item_t *t=&d->items[i];uint16_t off=0U;
        assert(t->x+t->width<=128U&&t->y+t->height<=64U);
        assert((t->font==OLED_UTF8_12X12 && t->height==12U && (t->y==0U || t->y==12U || t->y==25U || t->y==38U || t->y==51U)) || (t->font==6U && t->height==8U && t->x==50U && t->y==2U));
        assert(!strchr(t->text,'~'));
        assert(OLED_TextWidthUTF8(t->text,(uint16_t)strlen(t->text),t->font)==t->width);
        while(off<strlen(t->text))assert(OLED_HasGlyph(OLED_UTF8Next(t->text,(uint16_t)strlen(t->text),&off)));
        for(unsigned j=0;j<i;j++) {
            const remote_display_item_t *b=&d->items[j];
            assert(t->x>=b->x+b->width||b->x>=t->x+t->width||t->y>=b->y+b->height||b->y>=t->y+t->height);
        }
    }
    for(unsigned i=0;i<d->region_count;i++)assert(d->regions[i].x+d->regions[i].width<=128U&&d->regions[i].y+d->regions[i].height<=64U);
    for(unsigned i=0;i<strlen(d->diagnostic_title);i++)assert((unsigned char)d->diagnostic_title[i]<128U);
}
static void dump(const char *label,const remote_display_t *d)
{
    if(!preview)return;
    OLED_Clear();
    for(unsigned i=0;i<d->item_count;i++){const remote_display_item_t *t=&d->items[i];OLED_ShowUTF8(t->x,t->y,t->text,(uint16_t)strlen(t->text),t->font);}
    for(unsigned i=0;i<d->region_count;i++){
        const remote_display_region_t *r=&d->regions[i];
        if(r->underline){for(unsigned x=0;x<r->width;x+=4U){OLED_DrawPoint(r->x+x,r->y);if(x+1U<r->width)OLED_DrawPoint(r->x+x+1U,r->y);}}
        else OLED_ReverseArea(r->x,r->y,r->width,r->height);
    }
    fprintf(preview,"PAGE %s\n",label);
    for(unsigned y=0;y<8;y++){for(unsigned x=0;x<128;x++)fprintf(preview,"%02X",OLED_DisplayBuf[y][x]);fputc('\n',preview);}
}
#if OLED_ENABLE_LEGACY_FONTS
static void test_fonts(void)
{
    uint16_t i=0U;
    assert(OLED_TextWidthUTF8("电流-1200.0/-1200.0A",40U,OLED_ASCII_6X16)==128U);
    assert(OLED_TextWidthUTF8("目标-327.68km/h",40U,8U)==120U);
    assert(OLED_UTF8Next("\xE7\x94",2U,&i)==0xFFFDU&&i==1U);
    i=0U;assert(OLED_UTF8Next("\xF0\x9F\x9A\x82",4U,&i)==0x1F682U&&i==4U);
    for(unsigned n=0;n<OLED_CHINESE_COUNT;n++){
        if(n)assert(OLED_ChineseCodepoints[n]>OLED_ChineseCodepoints[n-1]);
        assert(OLED_HasGlyph(OLED_ChineseCodepoints[n]));
    }
    OLED_Clear();OLED_ShowUTF8(0,0,"电",3U,8U);
    unsigned index=0U;while(OLED_ChineseCodepoints[index]!=0x7535U)index++;
    assert(memcmp(OLED_DisplayBuf[0],OLED_ChineseData[index],16U)==0);
    assert(memcmp(OLED_DisplayBuf[1],OLED_ChineseData[index]+16U,16U)==0);
    OLED_Clear();OLED_ShowUTF8(0,0,"0",1U,OLED_ASCII_6X16);
    for(unsigned x=0;x<6;x++)for(unsigned y=0;y<8;y++)assert(OLED_GetPoint(x,2*y)==((OLED_F6x8['0'-32][x]>>y)&1U)&&OLED_GetPoint(x,2*y+1)==((OLED_F6x8['0'-32][x]>>y)&1U));
    OLED_ShowUTF8(-10,-8,"电A",4U,8U);OLED_ShowUTF8(124,60,"\xFF",1U,8U);
    assert(OLED_HasGlyph(0x7535U)&&!OLED_HasGlyph(0x1F682U));
    for(unsigned b=1;b<256;b++) {char bytes[2]={(char)b,0};i=0U;(void)OLED_UTF8Next(bytes,1U,&i);assert(i==1U);OLED_ShowUTF8(0,0,bytes,1U,8U);}
}
static void test_small_fonts(void)
{
    static const uint8_t missing[8]={0x7F,0x41,0x41,0x41,0x41,0x41,0x7F,0};
    assert(OLED_TextWidthUTF8("电流-1200.0~/-1200.0~A",80U,OLED_UTF8_SMALL)==124U);
    assert(OLED_TextWidthUTF8("电A",4U,OLED_UTF8_SMALL)==14U);
    for(unsigned i=0U;i<OLED_SMALL_COUNT;i++) {
        char utf8[4];uint32_t cp=OLED_SmallCodepoints[i];
        if(i)assert(OLED_SmallCodepoints[i-1U]<cp);
        utf8[0]=(char)(0xE0U|(cp>>12));utf8[1]=(char)(0x80U|((cp>>6)&63U));utf8[2]=(char)(0x80U|(cp&63U));utf8[3]=0;
        OLED_Clear();OLED_ShowUTF8(0,56,utf8,3U,OLED_UTF8_SMALL);
        assert(memcmp(OLED_DisplayBuf[7],OLED_SmallData[i],8U)==0);
        for(unsigned p=0;p<7;p++)for(unsigned x=0;x<128;x++)assert(OLED_DisplayBuf[p][x]==0);
    }
    OLED_Clear();OLED_ShowUTF8(0,0,"A",1U,OLED_UTF8_SMALL);
    assert(memcmp(OLED_DisplayBuf[0],OLED_F6x8['A'-32],6U)==0);
    OLED_Clear();OLED_ShowUTF8(0,0,"\xFF",1U,OLED_UTF8_SMALL);
    assert(memcmp(OLED_DisplayBuf[0],missing,8U)==0);
    OLED_Clear();OLED_ShowUTF8(-3,-2,"电",3U,OLED_UTF8_SMALL);
    unsigned index=0U;while(OLED_SmallCodepoints[index]!=0x7535U)index++;
    for(unsigned y=0;y<6;y++)for(unsigned x=0;x<5;x++)assert(OLED_GetPoint(x,y)==((OLED_SmallData[index][x+3]>>(y+2))&1U));
    OLED_Clear();OLED_ShowUTF8(124,60,"电",3U,OLED_UTF8_SMALL);
    for(unsigned y=0;y<64;y++)for(unsigned x=0;x<128;x++)if(x<124||y<60)assert(!OLED_GetPoint(x,y));
    for(unsigned b=1;b<256;b++){char bytes[2]={(char)b,0};OLED_ShowUTF8(0,0,bytes,1U,OLED_UTF8_SMALL);}
    { char invalid[4]={(char)0xE7,(char)0x94,0,0};
      assert(OLED_TextWidthUTF8(invalid,2U,OLED_UTF8_SMALL)==16U);
      OLED_ShowUTF8(0,0,invalid,2U,OLED_UTF8_SMALL);
      assert(OLED_TextWidthUTF8(NULL,20U,OLED_UTF8_SMALL)==0U);
      OLED_ShowUTF8(0,0,NULL,20U,OLED_UTF8_SMALL); }

}

#endif
static void test_medium_fonts(void)
{
    uint8_t before[8][128];
    const int16_t positions[][2]={{0,0},{0,12},{0,25},{0,38},{0,51},{-3,-2},{124,60},{-11,63},{128,0},{0,-12},{0,32767}};
    assert(OLED_TextWidthUTF8("电流-1200.0/-1200.0A",80U,OLED_UTF8_12X12)==120U);
    assert(OLED_TextWidthUTF8("电A",4U,OLED_UTF8_12X12)==18U);
    for(unsigned i=0;i<OLED_MEDIUM_COUNT;i++) {
        char utf8[4];uint32_t cp=OLED_MediumCodepoints[i];
        assert(!i || OLED_MediumCodepoints[i-1]<cp);
        for(unsigned x=12;x<24;x++)assert(!(OLED_MediumData[i][x]&0xF0U));
        utf8[0]=(char)(0xE0U|(cp>>12));utf8[1]=(char)(0x80U|((cp>>6)&63U));utf8[2]=(char)(0x80U|(cp&63U));utf8[3]=0;
        for(unsigned n=0;n<sizeof(positions)/sizeof(positions[0]);n++) {
            int16_t left=positions[n][0],top=positions[n][1];
            memset(OLED_DisplayBuf,0xA5,sizeof(before));memcpy(before,OLED_DisplayBuf,sizeof(before));
            OLED_ShowUTF8(left,top,utf8,3U,OLED_UTF8_12X12);
            for(int y=0;y<64;y++)for(int x=0;x<128;x++) {
                int gx=x-left,gy=y-top;
                uint8_t expected=(before[y/8][x]>>(y%8))&1U;
                if(gx>=0&&gx<12&&gy>=0&&gy<12)expected=(OLED_MediumData[i][(gy/8)*12+gx]>>(gy%8))&1U;
                assert(OLED_GetPoint(x,y)==expected);
            }
        }
    }
    for(int top=-5;top<=64;top++) {
        memset(OLED_DisplayBuf,0xA5,sizeof(before));memcpy(before,OLED_DisplayBuf,sizeof(before));
        OLED_ShowUTF8(4,top,"A",1U,OLED_UTF8_12X12);
        for(int y=0;y<64;y++)for(int x=0;x<128;x++) {
            int gx=x-4,gy=y-top-2;uint8_t expected=(before[y/8][x]>>(y%8))&1U;
            if(gx>=0&&gx<6&&gy>=0&&gy<8)expected=(OLED_F6x8['A'-32][gx]>>gy)&1U;
            assert(OLED_GetPoint(x,y)==expected);
        }
    }
    { const char *invalid[]={"\xFF","\xE7\x94","\xED\xA0\x80","\xF4\x90\x80\x80","\xF0\x9F\x9A\x82"};
      for(unsigned i=0;i<5;i++){OLED_Clear();OLED_ShowUTF8(0,51,invalid[i],(uint16_t)strlen(invalid[i]),OLED_UTF8_12X12);assert(OLED_GetPoint(0,51)&&OLED_GetPoint(0,62)&&!OLED_GetPoint(0,63));}
      assert(!OLED_TextWidthUTF8(NULL,65535U,OLED_UTF8_12X12)); }
}
static void test_event_codes_and_underlines(void)
{
    remote_status_model_t m;remote_display_t d;remote_debug_view_t input=debug;
    remote_status_init(&m);remote_display_init(&d);d.requested_language=REMOTE_DISPLAY_CHINESE;axes=3U;send_all(&m,100U);
    for(unsigned mask=0;mask<4096;mask++) {
        char expected[48];
        for(unsigned b=0;b<12;b++){input.short_keys[b]=input.long_keys[b]=input.double_keys[b]=(mask&(1U<<b))?'1':'0';}
        snprintf(expected,sizeof(expected),"短%03X 长%03X 双%03X",mask,mask,mask);
        d.requested_page=REMOTE_PAGE_DEBUG;remote_display_build(&d,&m,&input,100U,1U,1U);
        assert(!d.truncated&&has(&d,expected));
    }
    m.history[RH_CURRENT1].stale=1U;check(&d,&m,0U,100U);
    assert(d.region_count==1U && d.regions[0].underline && d.regions[0].x==24U && d.regions[0].y==63U && d.regions[0].width==18U);
    m.history[RH_CURRENT2].stale=1U;check(&d,&m,0U,100U);
    assert(d.region_count==2U && d.regions[1].x==48U && d.regions[1].width==18U);
    m.history[RH_FAULT1].value=0xFFFF;m.history[RH_FAULT1].stale=1U;
    check(&d,&m,REMOTE_PAGE_SERVO,100U);
    assert(d.regions[0].x==12U&&d.regions[0].y==50U&&d.regions[0].width==24U);
    m.history[RH_TARGET_KMH1].stale=1U;check(&d,&m,REMOTE_PAGE_SPEED,100U);
    assert(d.regions[0].x==24U&&d.regions[0].y==37U); /* A1 label excluded */
}
static void test_pages(void)
{
    remote_status_model_t m;remote_display_t d;char label[48];uint16_t s,l,b;
    remote_status_init(&m);remote_display_init(&d);d.requested_language=REMOTE_DISPLAY_CHINESE;
    for(unsigned p=0;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++){check(&d,&m,p,0U);snprintf(label,sizeof(label),"empty_%02u",p+1U);dump(label,&d);}
    s=b=0xC0U;l=0U;remote_display_keys(&d,0x40U,&s,&l,&b);assert(d.requested_page==12U&&!(s|l|b));
    d.requested_page=0U;remote_display_keys(&d,0x40U,&s,&l,&b);assert(d.requested_page==13U);
    for(unsigned p=0;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++){remote_display_keys(&d,0x80U,&s,&l,&b);assert(d.requested_page==p);}
    remote_display_keys(&d,0xC0U,&s,&l,&b);assert(d.requested_page==13U);
    send_all(&m,100U);
    for(unsigned p=0;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++){check(&d,&m,p,100U);snprintf(label,sizeof(label),"normal_%02u",p+1U);dump(label,&d);}
    check(&d,&m,0U,100U);assert(has(&d,"10.05km/h")&&has(&d,"按键")&&has(&d,"48.2/48.1V")&&has(&d,"2.3/2.1A"));
    check(&d,&m,1U,100U);assert(has(&d,"15.7/15.4")&&has(&d,"1198/1179rpm")&&has(&d,"45.6/40.2%"));
    check(&d,&m,3U,100U);assert(has(&d,"1200")&&has(&d,"1198"));
    check(&d,&m,8U,100U);assert(has(&d,"1234.5")&&has(&d,"-10.0m"));
    check(&d,&m,6U,350U);assert(has(&d,"260")); /* merged axis page includes age */
    check(&d,&m,6U,350U);assert(has(&d,"260"));check(&d,&m,7U,350U);assert(has(&d,"262"));
    for(unsigned p=0;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++){check(&d,&m,p,800U);snprintf(label,sizeof(label),"lost_%02u",p+1U);dump(label,&d);}
    check(&d,&m,0U,800U);assert(has(&d,"48.2/48.1V")&&d.quality_old&&d.region_count>=3U);
    make(0x81U);frame[9]=4U;accept(&m,1000U);
    for(unsigned p=0;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++){check(&d,&m,p,1000U);assert(!d.invert);check(&d,&m,p,1500U);assert(!d.invert);} /* lost cannot blink old alarms */
    make(0x81U);frame[9]=4U;accept(&m,1500U);check(&d,&m,0U,1500U);assert(d.invert&&has(&d,"4/??"));dump("alarm_missing_detail",&d);
    make(0x87U);frame[9]=frame[13]=4U;frame[14]=3U;accept(&m,1510U);check(&d,&m,0U,1510U);assert(has(&d,"4/03"));dump("alarm_matched",&d);
    axes=1U;make(0x81U);accept(&m,1520U);check(&d,&m,0U,1520U);assert(has(&d,"未选"));dump("single_axis",&d);
    axes=3U;make(0x81U);accept(&m,1530U);check(&d,&m,0U,1530U);assert(has(&d,"48.2/--"));
    session++;make(0x81U);accept(&m,1540U);check(&d,&m,0U,1540U);assert(has(&d,"--/--"));
}
static void test_controls_metadata(void)
{
    remote_status_model_t m;remote_display_t d;remote_status_init(&m);remote_display_init(&d);d.requested_language=REMOTE_DISPLAY_CHINESE;axes=3U;
    for(unsigned state=0;state<3;state++){
        make(0x88U);put16(frame+10,-2000);frame[23]=(uint8_t)(state==0?0xA1:state==1?0xA9:0xB9);accept(&m,100U+state*10U);
        check(&d,&m,0U,100U+state*10U);assert(has(&d,"-20.00km/h")&&has(&d,"按键"));
        if(state)assert(strstr(at(&d,12U),"按键")!=NULL); /* no text rotation */
        dump(state==0?"key":state==1?"key_wait":"key_ready",&d);
    }
    make(0x88U);put16(frame+10,32767);frame[23]=0xA4U;accept(&m,140U);check(&d,&m,0U,140U);assert(has(&d,"--km/h")&&has(&d,"命令"));
    make(0x88U);accept(&m,150U);check(&d,&m,0U,150U);assert(has(&d,"未知")&&has(&d,"--km/h"));
    make(0x8AU);frame[15]=1U;frame[20]=1U;frame[21]=2U;frame[23]=3U;frame[24]=1U;frame[26]=0xE8U;frame[27]=3U;accept(&m,160U);
    check(&d,&m,2U,160U);assert(has(&d,"人机"));check(&d,&m,3U,160U);assert(has(&d,"就绪100%"));dump("recovery_ready",&d);
    check(&d,&m,11U,160U);assert(has(&d,"未接入"));dump("not_connected",&d);
    make(0x8AU);frame[15]=2U;frame[21]=1U;accept(&m,170U);check(&d,&m,11U,170U);assert(!has(&d,"未接入"));
    make(0x81U);accept(&m,3000U);check(&d,&m,2U,3000U);assert(has(&d,"来源未知"));
    send_all(&m,4000U);make(0x84U);frame[12]=1U;frame[13]=0U;accept(&m,4010U);check(&d,&m,9U,4010U);assert(has(&d,"不适用"));dump("sync_inactive",&d);
    make(0x81U);frame[14]=0U;accept(&m,4020U);check(&d,&m,8U,4020U);assert(has(&d,"不适用"));
}
static void test_extremes(void)
{
    remote_status_model_t m;remote_display_t d;char label[48];remote_status_init(&m);remote_display_init(&d);d.requested_language=REMOTE_DISPLAY_CHINESE;send_all(&m,6000U);
    m.history[RH_RAW1].value=INT32_MIN;m.history[RH_RAW2].value=INT32_MAX;
    m.history[RH_CURRENT1].value=m.history[RH_CURRENT2].value=-12000;
    m.history[RH_LOAD1].value=m.history[RH_LOAD2].value=5000;
    m.history[RH_VOLTAGE1].value=m.history[RH_VOLTAGE2].value=9999;
    m.history[RH_BOOT].value=65535;m.history[RH_INCOMPLETE].value=1;
    m.history[RH_TORQUE1].value=m.history[RH_TORQUE2].value=-3000;
    m.slot[7].frame.data.vehicle_speed.common_centi_kmh=-32768;
    for(unsigned f=RH_TARGET;f<=RH_FAST2;f++)m.history[f].value=-32768;
    m.history[RH_RPM1].value=m.history[RH_RPM2].value=-32766;
    m.history[RH_DIFF].value=-32768;m.history[RH_NET].value=-32768;m.history[RH_TRIP].value=65535;
    m.history[RH_ERROR].value=m.history[RH_CORRECTION].value=-32768;
    m.history[RH_AGE1].value=m.history[RH_AGE2].value=0xFFFF0000L;
    m.history[RH_FAIL1].value=m.history[RH_FAIL2].value=255;
    for(unsigned p=0;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++){check(&d,&m,p,6000U);snprintf(label,sizeof(label),"limits_%02u",p+1U);dump(label,&d);}
    check(&d,&m,0U,6000U);assert(has(&d,"-327.68km/h")&&has(&d,"-1200.0/-1200.0A"));
    check(&d,&m,8U,6000U);assert(has(&d,"-2147483648")&&has(&d,"2147483647"));
    m.history[RH_TARGET_KMH1].value=INT32_MAX;m.history[RH_TARGET_KMH2].value=-INT32_MAX;
    check(&d,&m,4U,6000U);assert(has(&d,"214748364.7km/h"));
    for(unsigned p=0;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++) {
        check(&d,&m,p,6800U);snprintf(label,sizeof(label),"stale_limits_%02u",p+1U);dump(label,&d);
    }
    check(&d,&m,0U,6800U);assert(has(&d,"-1200.0/-1200.0A")&&d.region_count>=3U);
    for(unsigned p=0;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++){check(&d,&m,p,UINT32_MAX-10U);}
    for(unsigned axes_mask=0;axes_mask<4;axes_mask++){
        axes=(uint8_t)axes_mask;make(0x81U);accept(&m,6010U+axes_mask);
        for(unsigned p=0;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++)check(&d,&m,p,6010U+axes_mask);
    }
}

static void test_mixed_quality(void)
{
    remote_status_model_t m;remote_display_t d;remote_status_init(&m);remote_display_init(&d);d.requested_language=REMOTE_DISPLAY_CHINESE;axes=3U;
    send_all(&m,100U);
    for(unsigned f=0U;f<RH_COUNT;f++) {
        uint8_t old=m.history[f].received;
        m.history[f].received=0U;
        for(unsigned p=0U;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++)check(&d,&m,p,100U);
        m.history[f].received=old;m.history[f].stale=1U;
        for(unsigned p=0U;p<REMOTE_DISPLAY_ZH_PAGE_COUNT;p++)check(&d,&m,p,100U);
        m.history[f].stale=0U;
    }
    m.history[RH_CURRENT1].stale=1U;check(&d,&m,0U,100U);dump("mixed_current_quality",&d);m.history[RH_CURRENT1].stale=0U;
    make(0x81U);frame[14]=0U;accept(&m,110U);check(&d,&m,8U,110U);assert(has(&d,"不适用"));
    check(&d,&m,8U,1000U);assert(!has(&d,"不适用"));
    send_all(&m,1005U);
    make(0x86U);frame[26]=frame[27]=2U;accept(&m,1010U);
    make(0x81U);put16(frame+20,5000);accept(&m,1020U);
    assert(m.history[RH_CMD1].value==1200);
    check(&d,&m,3U,1020U);assert(d.quality_old);dump("unsupported_units",&d);
    uint32_t updated=m.history[RH_BOOT].updated_ms;
    make(0x83U);frame[8]=0U;accept(&m,1030U);
    assert(m.history[RH_BOOT].value==12345&&m.history[RH_BOOT].updated_ms==updated);
    check(&d,&m,8U,1030U);assert(has(&d,"1234.5")&&d.region_count>0U);
    m.history[RH_ALARM_CODE].received=1U;m.history[RH_ALARM_CODE].value=255;
    check(&d,&m,13U,1030U);assert(has(&d,"事件未知"));
    for(unsigned i=0;i<2U;i++) {
        remote_display_build(&d,&m,&debug,1030U,(uint8_t)i,0U);assert(!d.radio_live);
    }
}
static void test_field_integrity(void)
{
    remote_status_model_t model;
    remote_display_t display;
    uint32_t received;
    uint8_t page;
    remote_status_init(&model); remote_display_init(&display);
    send_all(&model, 100U);
    received = model.history[RH_BOOT].updated_ms;
    make(0x83U); frame[8] = 0U; put16(frame + 24U, 50); accept(&model, 200U);
    assert(model.history[RH_BOOT].value == 12345 && model.history[RH_BOOT].updated_ms == received);
    assert(model.history[RH_BOOT].stale && !remote_history_fresh(&model, RH_BOOT, 200U));
    make(0x83U); put16(frame + 24U, 0); accept(&model, 210U);
    assert(model.history[RH_BOOT].value == 0 && model.history[RH_BOOT].updated_ms == 210U);
    put16(frame + 24U, 999); put16(frame + 28U, remote_status_crc16(frame, 28U));
    assert(!remote_status_accept(&model, frame, 32U, 220U));
    assert(model.history[RH_BOOT].value == 0 && model.history[RH_BOOT].updated_ms == 210U);
    frame[4] = (uint8_t)(frame[4] - 1U); put16(frame + 28U, remote_status_crc16(frame, 28U));
    assert(!remote_status_accept(&model, frame, 32U, 230U));
    frame[4] = sequence++; frame[28] ^= 1U;
    assert(!remote_status_accept(&model, frame, 32U, 240U));
    assert(model.history[RH_BOOT].value == 0 && model.history[RH_BOOT].updated_ms == 210U);
    make(0x82U); frame[12] = frame[13] = 1U;
    put16(frame + 14U, 65535); put16(frame + 16U, 65535);
    put16(frame + 18U, 32767); put16(frame + 20U, 32767); accept(&model, 250U);
    assert(model.history[RH_RPM1].value == 1198 && model.history[RH_RPM1].stale);
    assert(model.history[RH_ENABLE1].value == 1 && model.history[RH_ENABLE1].stale);
    assert(model.history[RH_STATE1].value == 1 && !model.history[RH_STATE1].stale);
    check(&display, &model, REMOTE_PAGE_SERVO, 250U);
    assert(has(&display,"联1能1"));
    assert(has(&display,"1198") && has(&display,"0000") && display.quality_old && display.region_count>=2U);
    make(0x89U);
    put16(frame + 12U, 0x8000U | 321U);
    put16(frame + 14U, 0U); put16(frame + 16U, 0x8000U | 30U);
    put16(frame + 18U, 0x8000U | 500U);
    frame[10] = 4U; accept(&model, 260U);
    assert(model.history[RH_TORQUE1].value == 321 && !model.history[RH_TORQUE1].stale);
    assert(model.history[RH_LOAD1].value == 456 && model.history[RH_LOAD1].stale);
    assert(model.history[RH_CURRENT1].value == 30 && model.history[RH_CURRENT1].stale);
    assert(model.history[RH_VOLTAGE1].value == 500 && !model.history[RH_VOLTAGE1].stale);
    for (page = 0U; page < REMOTE_DISPLAY_ZH_PAGE_COUNT; page++) {
        check(&display, &model, page, UINT32_MAX - 10U);
        assert(!display.invert);
    }
    remote_status_init(&model);
    make(0x82U); frame[12] = frame[13] = 1U;
    put16(frame + 14U, 65535); put16(frame + 16U, 65535); accept(&model, 100U);
    check(&display, &model, REMOTE_PAGE_SERVO, 100U);
    assert(has(&display,"联1能--"));
    assert(!model.history[RH_ENABLE1].received && !model.history[RH_RPM1].received);
    remote_status_init(&model);
    make(0x81U); put16(frame + 20U, 1000); accept(&model, 100U);
    assert(!model.history[RH_TARGET_KMH1].received);
    make(0x86U); frame[26] = frame[27] = 1U;
    put16(frame + 16U, 0); put16(frame + 22U, 0); accept(&model, 200U);
    assert(!model.history[RH_TARGET_KMH1].received);
    make(0x86U); frame[26] = frame[27] = 1U;
    put16(frame + 16U, 65535); put16(frame + 22U, 1); accept(&model, 250U);
    make(0x81U); put16(frame + 20U, 32766); accept(&model, 260U);
    assert(model.history[RH_TARGET_KMH1].stale);
    for (axes = 1U; axes <= 3U; axes++) {
        remote_status_init(&model); send_all(&model, 500U);
        for (page = 0U; page < REMOTE_DISPLAY_ZH_PAGE_COUNT; page++) {
            check(&display, &model, page, 500U);
            assert(display.radio_live);
        }
        assert(remote_history_axis(RH_VCOMMAND) == 0U);
        assert(remote_history_axis(RH_FRONT_AGE) == 1U && remote_history_axis(RH_REAR_AGE) == 2U);
        assert(remote_history_axis(RH_AVERAGE) == 0U);
    }
    axes = 3U;
}

static void test_header_alarm_pixels(void)
{
    remote_status_model_t m;remote_display_t d;uint8_t plain[8][128];
    remote_status_init(&m);remote_display_init(&d);d.requested_language=REMOTE_DISPLAY_CHINESE;axes=3U;send_all(&m,1500U);
    make(0x81U);frame[9]=3U;accept(&m,1500U);
    check(&d,&m,0U,1500U);assert(d.invert&&has(&d,"3/??"));
    OLED_Clear();for(unsigned i=0;i<d.item_count;i++){remote_display_item_t *t=&d.items[i];OLED_ShowUTF8(t->x,t->y,t->text,(uint16_t)strlen(t->text),t->font);}
    memcpy(plain,OLED_DisplayBuf,sizeof(plain));
    assert(d.region_count==1U);remote_display_region_t r=d.regions[0];
    assert(r.x==92U&&r.y==2U&&r.height==8U&&r.width==30U);
    OLED_ReverseArea(r.x,r.y,r.width,r.height);
    for(unsigned p=0;p<8;p++)for(unsigned x=0;x<128;x++) {
        uint8_t expected=plain[p][x];if(x>=r.x&&x<r.x+r.width)for(unsigned bit=0;bit<8;bit++)if(p*8U+bit>=r.y&&p*8U+bit<r.y+r.height)expected^=1U<<bit;
        assert(OLED_DisplayBuf[p][x]==expected);
    }
    check(&d,&m,0U,2000U);assert(!d.invert); /* stale current alarm never blinks */
    make(0x81U);frame[9]=3U;accept(&m,2000U);check(&d,&m,0U,2000U);assert(!d.invert); /* off phase */
    d.requested_page=255U;remote_display_build(&d,&m,&debug,2000U,1U,1U);assert(d.page==0U);
}

int main(int argc,char **argv)
{
    if(argc>1){preview=fopen(argv[1],"wb");assert(preview);}

#if OLED_ENABLE_LEGACY_FONTS
    test_fonts();test_small_fonts();
#endif
    test_medium_fonts();test_event_codes_and_underlines();test_pages();test_controls_metadata();test_extremes();test_mixed_quality();test_field_integrity();test_header_alarm_pixels();if(preview)fclose(preview);
    puts("PASS Chinese OLED: actual glyphs/framebuffer, 14 four-line 12x12 pages, width/limits, UTF8 bounds, quality, alarms, axes, history, metadata and controls");return 0;
}
