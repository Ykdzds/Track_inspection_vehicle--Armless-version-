#define main baseline_health_main
#include "test_watchdog_health.c"
#undef main
#include "app_link_diag.h"
#include "HMI/app_control_arbiter.h"
#include "aimtor_servo.h"
static unsigned safety_events,safety_freezes;
static uint32_t first_safety[6];
void aimtor_diag_startup_stop(uint8_t source,uint8_t axis,uint16_t reason,uint32_t detail)
{(void)source;(void)axis;(void)reason;(void)detail;}
app_control_owner_t app_control_arbiter_owner(void){return APP_CONTROL_OWNER_REMOTE;}
void rs485_diag_begin(void){}
void rs485_diag_times(rs485_diag_times_t *out){memset(out,0,sizeof(*out));}
void rs485_get_stats(rs485_stats_t *out){memset(out,0,sizeof(*out));}
void aimtor_rs485_diag_snapshot(app_link_diag_snapshot_t *out){out->count=0U;}
void app_link_diag_record(uint8_t source,uint16_t kind,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f)
{assert(source==APP_LINK_RS485);(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;if(kind==0x23U)safety_events++;}
void app_link_diag_freeze(uint8_t source,uint16_t kind,const uint32_t *words,uint8_t count)
{assert(source==APP_LINK_RS485 && count<=16U);(void)kind;(void)words;}
void app_link_diag_freeze6(uint8_t source,uint16_t kind,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f)
{
    assert(source==APP_LINK_RS485);
    if(kind==0x23U){safety_freezes++;if(!first_safety[1]){first_safety[0]=a;first_safety[1]=b;first_safety[2]=c;first_safety[3]=d;first_safety[4]=e;first_safety[5]=f;}}
}
int main(void)
{
    unsigned events,freezes;
    assert(baseline_health_main()==0);
    safety_events=safety_freezes=0U;memset(first_safety,0,sizeof(first_safety));
    seed(0);cycle(-1,0);
    assert(safety_events==1U && safety_freezes==0U);
    cycle(-1,0);assert(safety_events==1U);
    seed(100);pump(800,APP_HEARTBEAT_SERVO_COMM,1);
    assert((first_safety[1]&0xFFU)==1U && ((first_safety[1]>>8U)&0xFFU)==APP_HEARTBEAT_SERVO_COMM);
    events=safety_events;freezes=safety_freezes;
    cycle(APP_HEARTBEAT_SERVO_COMM,1);assert(safety_events==events && safety_freezes==freezes);
    app_health_request_driver_fallback(0x12340001UL);cycle(-1,1);
    assert(status().phase==2U && safety_events>events && safety_freezes>freezes);
    assert((first_safety[1]&0xFFU)==1U); /* retain task-loss first record */
    puts("PASS safety diagnostics: baseline, phase, failed task, full failure, no repetition, first preservation");
    return 0;
}
