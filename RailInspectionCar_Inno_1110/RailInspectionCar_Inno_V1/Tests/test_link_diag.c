/* Real shared logger with deterministic hardware backpressure and worst-width
 * projections. Link hooks are separately exercised by production-path suites. */
#define main old_hmi_diag_main
#include "test_hmi_link_diag.c"
#undef main
#if APP_LINK_DIAG_ACTIVE
static void projection(app_link_diag_snapshot_t *out)
{
    unsigned i;out->sampled_ms=now;out->count=24U;
    for(i=0U;i<24U;++i)out->words[i]=UINT32_MAX;
}
#if APP_NRF_DIAGNOSTIC_ENABLED || APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED
void app_remote_diag_snapshot(app_link_diag_snapshot_t *out,uint8_t phase)
{(void)phase;projection(out);}
#endif
#if APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED
void app_can_diag_snapshot(app_link_diag_snapshot_t *out,uint8_t source,uint8_t phase)
{(void)source;(void)phase;projection(out);}
#endif
#if APP_SERVO_RS485_DIAG_ENABLED
void app_rs485_diag_snapshot(app_link_diag_snapshot_t *out,uint8_t phase)
{(void)phase;projection(out);}
#endif
#if APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED
void app_position_diag_snapshot(app_link_diag_snapshot_t *out){projection(out);}
#endif
static const unsigned routes[]={APP_HMI_LINK_DIAG_ENABLED,APP_NRF_DIAGNOSTIC_ENABLED,
    APP_CAN_SERVO_DIAG_ENABLED,APP_CAN_ARM_DIAG_ENABLED,APP_SERVO_RS485_DIAG_ENABLED};
static void test_startup_first_all_routes(void)
{
    uint32_t words[24];unsigned i;
    const char *first,*second,*third,*config;
    reset();blocked=1U;
    for(i=0U;i<24U;++i)words[i]=UINT32_MAX;
    app_link_diag_startup_first(words,24U);
    words[0]=0xDEADBEEFU;app_link_diag_startup_first(words,24U);
    tick(20U);assert(output_bytes==0U);
    blocked=0U;reject_line="STARTUP part=1";reject_remaining=7U;tick(100U);
    first=strstr(output,"STARTUP part=0 parts=3 count=18 merged=1 w=FFFFFFFF");
    second=strstr(output,"STARTUP part=1 parts=3 count=18 merged=1 w=FFFFFFFF");
    third=strstr(output,"STARTUP part=2 parts=3 count=18 merged=1 w=FFFFFFFF");
    config=strstr(output,"CONFIG memory=");
    assert(first && second && third && config && config<first && first<second && second<third);
    assert(!strstr(output,"DEADBEEF") && !reject_remaining);
    /* Output acknowledgement retains the first boot record permanently. */
    app_link_diag_startup_first(words,24U);tick(1200U);
    assert(!strstr(third+1," STARTUP ") && !strstr(output,"DEADBEEF"));
    reset();app_link_diag_startup_first(words,24U);tick(50U);
    assert(strstr(output,"STARTUP part=0 parts=3 count=18 merged=0 w=DEADBEEF"));
}
int main(void)
{
    unsigned i,r;uint32_t words[16];app_control_stop_t stop=make_stop(71U,3U);
    /* Referenced even in HMI-only build, avoiding a host-only unused helper. */
    app_link_diag_snapshot_t probe;projection(&probe);assert(probe.count==24U);
    test_startup_first_all_routes();
    reset();blocked=1U;
    for(i=0U;i<16U;++i)words[i]=UINT32_MAX;
    for(r=1U;r<5U;++r) {
        words[0]=0x12340000U+r;
        app_link_diag_freeze((uint8_t)r,0xAAU,words,16U);
        words[0]=0xDEADBEEFU;
        app_link_diag_freeze((uint8_t)r,0xBBU,words,16U);
    }
    app_hmi_link_diag_stop(&stop,APP_HMI_LINK_STOP_FIRST);
    /* A later stop cannot replace the pending original even across rearm. */
    stop.event=72U;stop.reason=9U;
    app_hmi_link_diag_stop(&stop,APP_HMI_LINK_STOP_FIRST);
    tick(25U);assert(output_bytes==0U);
    blocked=0U;tick(150U);
    assert(strstr(output,"FIRST at=123 owner=1 requester=5 reason=3"));
    assert(!strstr(output,"DEADBEEF"));
    for(r=1U;r<5U;++r) {
        char search[64];
        snprintf(search,sizeof(search),"kind=AA p=0 merged=1 w=1234000%u",r);
        assert((strstr(output,search)!=NULL)==(routes[r]!=0U));
    }
    /* Event kinds retain all 12 bits; a per-source flood cannot starve peers. */
    for(r=1U;r<5U;++r)app_link_diag_record((uint8_t)r,0x300U+r,1,2,3,4,5,6);
    tick(100U);
    for(r=1U;r<5U;++r)if(routes[r]) {
        char search[32];snprintf(search,sizeof(search),"kind=30%u w=",r);assert(strstr(output,search));
    }
    stop.reason=5U;stop.deceleration_mm_s2=600U;
    app_hmi_link_diag_upgrade(&stop,2U,90U,0U);
    tick(100U);
    assert(strstr(output,"10002,5A,258,"));
    if(!APP_HMI_LINK_DIAG_ENABLED)assert(strstr(output,"kind=3E7 w="));
    app_link_diag_record(APP_LINK_RS485,0x20U,2U,0x12341234U,0,0,0,0);
    app_link_diag_record(APP_LINK_RS485,0x21U,3U,0x12341234U,0,0,0,2U);
    app_link_diag_record(APP_LINK_RS485,0x20U,2U,0x12340000U,0,0,0,0);
    app_link_diag_record(APP_LINK_RS485,0x21U,3U,0x12340001U,0,0,0,2U);
    tick(100U);
    assert(!strstr(output,"12341234"));
    if(routes[4]) {assert(strstr(output,"12340000"));assert(strstr(output,"12340001"));}
    /* Flood normal queue at pressure while independent first lane still works.
     * Recheck bandwidth through real emitter and byte-wide assertions above. */
    for(i=0U;i<6000U;++i) {
        for(r=0U;r<5U;++r)app_link_diag_record((uint8_t)r,0x30U,UINT32_MAX,i,r,0,0,0);
        if(i==2500U) {
            words[0]=0xFACEU;
            for(r=1U;r<5U;++r)app_link_diag_freeze((uint8_t)r,0xCCU,words,16U);
        }
        tick(1U);
    }
    /* Recover from backpressure repeatedly crossing STATE deadlines while a
     * HEALTH part is pending. Each final health part must eventually appear. */
    blocked=1U;tick(2500U);blocked=0U;tick(11000U);
    assert(max_second<=6000U && second_bytes<=6000U);
    assert(strstr(output,"BOOT build="));
    {
        char config[128];
        snprintf(config,sizeof(config),"CONFIG memory=%s lcd=%u cli=%u tune=%u servo_cli=%u pulse=%u",
            APP_MEMORY_PROFILE_ID,APP_CHASSIS_LCD_ENABLED,APP_DEBUG_CLI_ENABLED,
            APP_TUNE_UART_ENABLED,APP_SERVO_CLI_DEBUG_ENABLED,APP_SERVO_PULSE_ENABLED);
        assert(strstr(output,config));
    }
#if !APP_CHASSIS_LCD_ENABLED
    assert(!strstr(output," LCD "));
#endif
    assert(strstr(output,"MEM free=777 min=666"));
    for(r=1U;r<5U;++r)if(routes[r]) {
        const char *name=r==1U?"RF":r==2U?"CS":r==3U?"CA":"S485";
        char search[64];snprintf(search,sizeof(search),"STATE src=%s",name);
        assert(strstr(output,search));
        snprintf(search,sizeof(search),"HEALTH src=%s p=%X part=1",name,r==1U?5U:((r==3U && routes[2])?1U:2U));
        assert(strstr(output,search));
        snprintf(search,sizeof(search),"FIRST src=%s kind=CC",name);
        assert(strstr(output,search));
    }
    if(routes[2] || routes[3]) {
        const char *name=routes[2]?"CS":"CA";char search[64];
        snprintf(search,sizeof(search),"HEALTH src=%s p=3 part=1",name);
        assert(strstr(output,search));
        if(routes[2] && routes[3])assert(!strstr(output,"HEALTH src=CA p=3"));
    }
    if(!APP_HMI_LINK_DIAG_ENABLED)assert(!strstr(output," UART "));
    puts("PASS shared logger: disabled routes, standalone BOOT/FIRST/MEM, worst widths, immutable first, backpressure, 6000B/s and periodic fairness");
    return 0;
}
#else
int main(void){app_hmi_link_diag_init();app_hmi_link_diag_service();puts("PASS all diagnostics off");return 0;}
#endif
