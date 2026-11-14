/* Run the full existing production driver fixture with diagnostic hooks active. */
#define main baseline_driver_main
#include "test_core.c"
#undef main
#include "HMI/app_control_arbiter.h"
static unsigned diagnostic_zero_events, diagnostic_freezes, requested_events, submitted_events, limited_events;
static unsigned health_events,health_freezes;
static uint32_t health_first[6];
void app_link_diag_startup_first(const uint32_t *words,uint8_t count)
{assert(words!=NULL && count>0U && count<=24U);}
app_control_owner_t app_control_arbiter_owner(void){return APP_CONTROL_OWNER_REMOTE;}
uint32_t app_control_arbiter_generation(void){return 0x456U;}
void rs485_diag_begin(void){}
void rs485_diag_times(rs485_diag_times_t *out){memset(out,0,sizeof(*out));}
void rs485_get_stats(rs485_stats_t *out){memset(out,0,sizeof(*out));}
void app_link_diag_record(uint8_t source,uint16_t kind,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f)
{
    assert(source==APP_LINK_RS485);(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;
    if(kind==0x10U)diagnostic_zero_events++;
    if(kind==0x20U){assert(d==0x456U);requested_events++;if((uint16_t)(b>>16U)!=(uint16_t)b)limited_events++;}
    if(kind==0x21U){assert(d==0x456U);submitted_events++;}
    if(kind==0x22U)health_events++;
}
void app_link_diag_freeze(uint8_t source,uint16_t kind,const uint32_t *words,uint8_t count)
{
    assert(source==APP_LINK_RS485 && (count==16U || count==6U));(void)kind;(void)words;diagnostic_freezes++;
}
void app_link_diag_freeze6(uint8_t source,uint16_t kind,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f)
{uint32_t words[6]={a,b,c,d,e,f};if(kind==0x22U){health_freezes++;if(!health_first[2])memcpy(health_first,words,sizeof(words));}app_link_diag_freeze(source,kind,words,6U);}
int main(void)
{
    app_link_diag_snapshot_t out;
    unsigned transactions;
    assert(baseline_driver_main()==0);
    transactions=tx_count;
    aimtor_rs485_diag_snapshot(&out);
    assert(out.count==24U && out.words[0]==APP_CONTROL_OWNER_REMOTE && tx_count==transactions);
    assert(diagnostic_zero_events>0U && diagnostic_freezes>0U);
    assert(requested_events>0U && submitted_events>0U);
    {
        extern void aimtor_test_safety_poll_fail(uint8_t failures,modbus_status_t status);
        k1_hold_setup();
        aimtor_set_motion_authorized(true);
        aimtor_set_remote_control_mask(3U);
        assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT,100)==SERVO_OK);
        pump(50U);
        aimtor_test_safety_poll_fail(1U,MODBUS_TIMEOUT);
        assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT,200)==SERVO_OK);
        assert(limited_events>0U);
    }
    {
        extern void aimtor_test_safety_seed(int16_t rpm);
        extern void aimtor_test_safety_poll_fail(uint8_t failures,modbus_status_t status);
        extern void aimtor_test_diag_change(uint8_t item,uint16_t value);
        unsigned before,first_before;
        aimtor_test_safety_seed(100);
        aimtor_rs485_diag_observe();
        assert(health_events==2U && health_freezes==0U);
        aimtor_rs485_diag_observe();assert(health_events==2U);
        aimtor_test_diag_change(0U,0x994U);aimtor_rs485_diag_observe();
        assert(health_events==3U && health_freezes==1U && (health_first[2]&0xFFFFU)==0x994U);
        aimtor_rs485_diag_observe();assert(health_events==3U && health_freezes==1U);
        aimtor_test_diag_change(0U,0U);aimtor_rs485_diag_observe();
        assert(health_events==4U && health_freezes==1U);
        aimtor_test_diag_change(1U,0U);aimtor_rs485_diag_observe();
        assert(health_events==5U && health_freezes==2U);
        aimtor_test_diag_change(1U,1U);aimtor_rs485_diag_observe();
        aimtor_test_diag_change(2U,0U);aimtor_rs485_diag_observe();
        assert(health_events==7U && health_freezes==3U);
        aimtor_test_diag_change(2U,1U);aimtor_rs485_diag_observe();
        before=health_events;first_before=health_freezes;
        aimtor_test_safety_poll_fail(1U,MODBUS_TIMEOUT);aimtor_rs485_diag_observe();
        assert(health_events==before+1U && health_freezes==first_before+1U);
        aimtor_rs485_diag_observe();assert(health_events==before+1U);
        aimtor_test_diag_change(3U,0U);aimtor_rs485_diag_observe();
        aimtor_test_diag_change(4U,APP_SERVO_STATUS_MAX_AGE_MS);aimtor_rs485_diag_observe();
        assert(health_events==before+3U && health_freezes==first_before+2U);
        assert((health_first[2]&0xFFFFU)==0x994U); /* later changes did not replace pending first */
    }
    puts("PASS production AIMtor diagnostic hooks: normal/cold/watchdog zero samples, cached narrow projection, no extra query");
    return 0;
}
