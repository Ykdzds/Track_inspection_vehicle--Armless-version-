/* Reuse the production-frame fixture and actual framebuffer dumper. */
#define main chinese_suite_entry
#include "test_display.c"
#undef main
#include "english_reference.h"
static unsigned comparisons;
static void compare_framebuffer(const remote_display_t *d,const english_reference_output_t *old)
{
    uint8_t expected[8][128]={{0}};
    OLED_Clear();
    for(unsigned row=0;row<8;row++) {
        const remote_display_item_t *t=&d->items[row];
        OLED_ShowUTF8(t->x,t->y,t->text,(uint16_t)strlen(t->text),t->font);
        for(unsigned col=0;old->lines[row][col];col++)
            memcpy(expected[row]+col*6U,OLED_F6x8[(unsigned char)old->lines[row][col]-32U],6U);
    }
    for(unsigned i=0;i<d->region_count;i++) {
        const remote_display_region_t *r=&d->regions[i];
        OLED_ReverseArea(r->x,r->y,r->width,r->height);
    }
    for(unsigned i=0;i<old->region_count;i++)
        for(unsigned col=old->regions[i].x;col<old->regions[i].x+old->regions[i].width;col++)
            expected[old->regions[i].y/8U][col]^=0xFFU;
    assert(!memcmp(expected,OLED_DisplayBuf,sizeof(expected)));
}
static void test_product_renderer_bounds(void)
{
    static const uint8_t box[32]={255,1,1,1,1,1,1,1,1,1,1,1,1,1,1,255,
        255,128,128,128,128,128,128,128,128,128,128,128,128,128,128,255};
    const char *bad[]={"\xFF","\xE7\x94","\xED\xA0\x80","\xF4\x90\x80\x80","\xF0\x9F\x9A\x82"};
    for(unsigned i=0;i<5U;i++) {
        OLED_Clear();OLED_ShowUTF8(0,0,bad[i],(uint16_t)strlen(bad[i]),6U);
        assert(!memcmp(OLED_DisplayBuf[0],box,16U)&&!memcmp(OLED_DisplayBuf[1],box+16U,16U));
        for(unsigned row=2U;row<8U;row++)for(unsigned x=0U;x<128U;x++)assert(!OLED_DisplayBuf[row][x]);
    }
}
static void compare_english(remote_status_model_t *m,uint32_t now,uint8_t ready,uint8_t capable,const char *tag)
{
    for(uint8_t page=0;page<REMOTE_DISPLAY_EN_PAGE_COUNT;page++) {
        remote_display_t d;english_reference_output_t old;char label[64];
        remote_display_init(&d);d.requested_page=page;
        remote_display_build(&d,m,&debug,now,ready,capable);
        english_reference(m,&debug,page,now,ready,capable,&old);
        assert(d.language==REMOTE_DISPLAY_ENGLISH&&d.item_count==8U);
        for(unsigned i=0;i<8;i++) {
            if(strcmp(d.items[i].text,old.lines[i]))fprintf(stderr,"English mismatch page%u row%u new=[%s] old=[%s]\n",page+1U,i,d.items[i].text,old.lines[i]);
            assert(!strcmp(d.items[i].text,old.lines[i]));
            assert(d.items[i].x==0&&d.items[i].y==i*8U&&d.items[i].font==6U&&d.items[i].height==8U);
            assert(d.items[i].width==strlen(old.lines[i])*6U&&d.items[i].width<=126U);
            for(unsigned c=0;c<strlen(d.items[i].text);c++)assert((unsigned char)d.items[i].text[c]>=32U&&(unsigned char)d.items[i].text[c]<=126U);
        }
        assert(d.truncated==old.truncated&&d.quality_any==old.quality_any&&d.quality_old==old.quality_old&&d.radio_live==old.radio_live&&d.invert==old.invert&&d.region_count==old.region_count);
        for(unsigned i=0;i<d.region_count;i++)assert(!d.regions[i].underline&&d.regions[i].x==old.regions[i].x&&d.regions[i].y==old.regions[i].y&&d.regions[i].width==old.regions[i].width&&d.regions[i].height==old.regions[i].height);
        compare_framebuffer(&d,&old);
        assert(!strncmp(d.diagnostic_title,"EN",2U));
        if(tag){snprintf(label,sizeof(label),"en_%s_%02u",tag,page+1U);dump(label,&d);}
        comparisons++;
    }
}
static void local_keys(remote_display_t *d,uint16_t s,uint16_t l,uint16_t b)
{
    uint16_t s0=s,l0=l,b0=b;remote_display_keys(d,s,&s,&l,&b);
    assert(s==(s0&~REMOTE_LOCAL_KEY_MASK)&&l==(l0&~REMOTE_LOCAL_KEY_MASK)&&b==(b0&~REMOTE_LOCAL_KEY_MASK));
}
static void test_language(void)
{
    remote_status_model_t m;remote_display_t d;uint8_t frozen[sizeof(d.items)];
    remote_status_init(&m);remote_display_init(&d);
    assert(d.language==REMOTE_DISPLAY_ENGLISH&&d.requested_language==REMOTE_DISPLAY_ENGLISH&&d.requested_page==0U);
    assert(remote_display_page_count(REMOTE_DISPLAY_ENGLISH)==9U&&remote_display_page_count(REMOTE_DISPLAY_CHINESE)==14U);
    remote_display_build(&d,&m,&debug,0U,1U,1U);dump("switch_boot_en",&d);
    for(unsigned round=0;round<100;round++) {
        unsigned count=remote_display_page_count((remote_display_language_t)d.requested_language);
        for(unsigned i=0;i<count;i++){local_keys(&d,0x80U,0U,0U);assert(d.requested_page==(i+1U)%count);}
        local_keys(&d,0x40U,0U,0U);assert(d.requested_page==count-1U);
        local_keys(&d,0xC0U,0x80U,0xC0U);assert(d.requested_page==count-1U);
        memcpy(frozen,d.items,sizeof(frozen));uint8_t language=d.language;
        local_keys(&d,0x90U,0x45U,0xC8U); /* K5+K8 short, K7+K1+K3 long */
        assert(d.requested_page==0U&&d.requested_language!=(round%2U)&&d.language==language);
        assert(!memcmp(frozen,d.items,sizeof(frozen)));
        local_keys(&d,0U,0U,0U);assert(d.requested_page==0U); /* a consumed event cannot repeat */
        remote_display_build(&d,&m,&debug,round,1U,1U);
        assert(d.page==0U&&d.language==d.requested_language);
        if(round<2U)dump(round?"switch_back_en":"switch_to_zh",&d);
    }
    local_keys(&d,0U,0x40U,0U);local_keys(&d,0x80U,0U,0U);
    remote_display_build(&d,&m,&debug,100U,0U,0U);assert(d.language==REMOTE_DISPLAY_CHINESE&&d.page==1U);
    local_keys(&d,0U,0x40U,0U);remote_display_build(&d,&m,&debug,200U,1U,1U);assert(d.language==REMOTE_DISPLAY_ENGLISH&&d.page==0U);
    d.requested_page=255U;remote_display_build(&d,&m,&debug,300U,1U,1U);assert(d.page==0U);
    remote_display_init(&d);assert(!d.requested_language&&!d.requested_page);
}
static void english_scenarios(void)
{
    remote_status_model_t m;remote_status_init(&m);axes=3U;
    compare_english(&m,0U,1U,1U,"empty");send_all(&m,100U);
    compare_english(&m,100U,1U,1U,"normal");compare_english(&m,800U,1U,1U,"lost");
    compare_english(&m,100U,0U,0U,NULL);compare_english(&m,100U,1U,0U,NULL);
    for(unsigned f=0;f<RH_COUNT;f++) {
        uint8_t received=m.history[f].received;m.history[f].received=0U;
        compare_english(&m,100U,1U,1U,NULL);m.history[f].received=received;m.history[f].stale=1U;
        compare_english(&m,100U,1U,1U,NULL);m.history[f].stale=0U;
    }
    for(axes=0U;axes<4U;axes++) {remote_status_init(&m);send_all(&m,100U);compare_english(&m,100U,1U,1U,NULL);}
    axes=3U;remote_status_init(&m);send_all(&m,6000U);
    m.history[RH_RAW1].value=INT32_MIN;m.history[RH_RAW2].value=INT32_MAX;
    m.history[RH_CURRENT1].value=m.history[RH_CURRENT2].value=-12000;
    m.history[RH_LOAD1].value=m.history[RH_LOAD2].value=5000;
    m.history[RH_VOLTAGE1].value=m.history[RH_VOLTAGE2].value=9999;
    m.history[RH_TORQUE1].value=m.history[RH_TORQUE2].value=-3000;
    m.history[RH_BOOT].value=m.history[RH_TRIP].value=65535;m.history[RH_NET].value=-32768;
    m.history[RH_RPM1].value=m.history[RH_RPM2].value=-32766;
    m.history[RH_ERROR].value=m.history[RH_CORRECTION].value=-32768;
    for(unsigned f=RH_TARGET;f<=RH_FAST2;f++)m.history[f].value=-32768;
    m.slot[7].frame.data.vehicle_speed.common_centi_kmh=-32768;
    compare_english(&m,6000U,1U,1U,"limits");compare_english(&m,6800U,1U,1U,"stale_limits");
    compare_english(&m,UINT32_MAX-10U,1U,1U,NULL);
    for(unsigned phase=0;phase<4;phase++) {
        uint32_t now=8000U+phase*1000U;send_all(&m,now);
        make(0x8AU);frame[15]=1U;frame[20]=1U;frame[21]=2U;frame[23]=3U;frame[24]=1U;frame[26]=0xE8U;frame[27]=3U;accept(&m,now);
        compare_english(&m,now,1U,1U,phase==0?"metadata":NULL);
        m.slot[9].frame.data.metadata.recovery_state=0U;compare_english(&m,now,1U,1U,NULL);
        m.slot[9].frame.data.metadata.recovery_state=6U;compare_english(&m,now,1U,1U,NULL);
        m.slot[9].frame.data.metadata.recovery_state=2U;
        m.slot[9].frame.data.metadata.recovery_eligible=0U;
        compare_english(&m,now,1U,1U,NULL);
    }
    /* KEY exit W/R, pre-extension 0x88, unknown units and unavailable sync. */
    for(unsigned state=0U;state<3U;state++) {
        uint32_t now=12000U+state*10U;send_all(&m,now);
        make(0x88U);put16(frame+10,-2000);frame[23]=(uint8_t)(0xA1U+(state==1U?8U:state==2U?24U:0U));accept(&m,now);
        compare_english(&m,now,1U,1U,state==1U?"key_wait":state==2U?"key_ready":NULL);
    }
    send_all(&m,13000U);make(0x88U);frame[23]=0U;accept(&m,13000U);compare_english(&m,13000U,1U,1U,NULL);
    make(0x81U);put16(frame+20,5000);accept(&m,13010U);compare_english(&m,13010U,1U,1U,NULL);
    make(0x84U);frame[12]=1U;accept(&m,13020U);compare_english(&m,13020U,1U,1U,NULL);
    for(unsigned alarm=0;alarm<5;alarm++) {
        send_all(&m,15000U);make(0x81U);frame[9]=(uint8_t)alarm;accept(&m,15000U);
        compare_english(&m,15000U,1U,1U,NULL);compare_english(&m,15500U,1U,1U,NULL);
        make(0x81U);frame[9]=(uint8_t)alarm;accept(&m,15500U);
        make(0x87U);frame[9]=frame[13]=(uint8_t)alarm;frame[14]=3U;accept(&m,15500U);
        compare_english(&m,15500U,1U,1U,alarm==3?"alarm":NULL);
    }
    session++;send_all(&m,16000U);compare_english(&m,16000U,1U,1U,NULL);
}
int main(int argc,char **argv)
{
    if(argc>1){preview=fopen(argv[1],"wb");assert(preview);}
    test_product_renderer_bounds();test_language();english_scenarios();if(preview)fclose(preview);
    printf("PASS bilingual: boot EN, K7 toggle/home/filter, frozen request, both page cycles, %u English page comparisons against 3af92be\n",comparisons);
    return 0;
}
