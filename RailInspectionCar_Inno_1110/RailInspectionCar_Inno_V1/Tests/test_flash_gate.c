#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_sync_params.h"
#include "app_health.h"
#include "app_control_arbiter.h"
#include "aimtor_servo.h"
#include "motion_control.h"
static uint32_t now,diagnostic;
static uint8_t owner,preparing,stopping,pending,arm,lock,locked,deny_lock,source,recovery,wdg,pair=1U;
static motion_state_t motion;
static app_health_nrf_status_t nrf;
static app_safety_status_t safety;
static servo_snapshot_t axes[2];
static uint8_t bytes[0x40000];
static const uint32_t uid[3]={1,2,3};
uint32_t HAL_GetTick(void){return now;}
uint32_t app_boot_identity_id(void){return 1234U;}
uint32_t app_health_get_diagnostic_faults(void){return diagnostic;}
void app_health_get_safety_status(app_safety_status_t *p){*p=safety;}
void app_health_get_nrf_status(app_health_nrf_status_t *p){*p=nrf;}
bool app_health_watchdog_test_requested(void){return wdg!=0U;}
app_control_owner_t app_control_arbiter_owner(void){return (app_control_owner_t)owner;}
uint8_t app_control_arbiter_preparing(void){return preparing;}
uint8_t app_control_arbiter_stopping(void){return stopping;}
uint8_t app_control_arbiter_motion_pending(void){return pending;}
uint8_t app_control_arbiter_arm_locked(void){return arm;}
app_control_lock_reason_t app_control_arbiter_lock_reason(void){return (app_control_lock_reason_t)lock;}
uint8_t app_control_arbiter_parameter_locked(void){return locked;}
uint8_t app_control_arbiter_parameter_begin(void){if(locked || deny_lock)return 0;locked=1U;return 1U;}
void app_control_arbiter_parameter_end(void){locked=0U;}
bool aimtor_cli_control_active(void){return source==1U;}
bool aimtor_hmi_control_active(void){return source==2U;}
bool aimtor_can_control_active(void){return source==3U;}
bool aimtor_remote_control_active(void){return source==4U;}
bool aimtor_cli_restoring_hold(void){return source==5U;}
bool app_cli_servo_session_active(void){return source==6U;}
bool aimtor_maintenance_enabled(void){return recovery==1U;}
bool aimtor_recovery_blocks_motion(void){return recovery==2U;}
motion_state_t motion_get_state(void){return motion;}
bool aimtor_get_snapshot_pair(servo_snapshot_t *a,servo_snapshot_t *b){*a=axes[0];*b=axes[1];return pair!=0U;}
static void tick(uint32_t ms){now=ms;nrf.updated_ms=ms;app_sync_storage_service();}
static app_sync_gate_status_t read_gate(void){app_sync_gate_status_t s;app_sync_gate_read(&s);return s;}
static void expect(uint32_t bit){app_sync_gate_status_t s=read_gate();assert(!s.gate_ok && (s.reason_mask&bit));}
static void reset(void){unsigned i;memset(bytes,255,sizeof(bytes));memset(&safety,0,sizeof(safety));
 memset(&nrf,0,sizeof(nrf));nrf.known=nrf.initialized=nrf.ready=nrf.recovered=1U;
 memset(axes,0,sizeof(axes));for(i=0;i<2;i++)axes[i].online=axes[i].speed_valid=axes[i].fault_valid=1U;
 diagnostic=0;owner=preparing=stopping=pending=arm=lock=locked=deny_lock=source=recovery=wdg=0;pair=1;motion=MOTION_READY;
 app_sync_storage_test_attach(bytes,sizeof(bytes),uid);app_sync_params_init();tick(0);tick(500);assert(read_gate().gate_ok);}
int main(void){app_sync_params_t p;app_sync_storage_status_t saved;app_sync_gate_status_t s;unsigned i;
 reset();diagnostic=APP_DIAG_NRF_INIT_ERROR;assert(read_gate().gate_ok); /* historical evidence stays visible */
 assert(read_gate().diagnostic==APP_DIAG_NRF_INIT_ERROR);
 nrf.current_fault=1;expect(APP_SYNC_GATE_NRF);nrf.current_fault=0;nrf.recovered=0;expect(APP_SYNC_GATE_NRF);
 nrf.recovered=1;nrf.known=0;expect(APP_SYNC_GATE_NRF);nrf.known=1;nrf.updated_ms=400;expect(APP_SYNC_GATE_NRF);
 nrf.updated_ms=500;diagnostic|=APP_DIAG_RS485_INIT_ERROR;expect(APP_SYNC_GATE_DIAGNOSTIC);diagnostic=0;
 owner=1;expect(APP_SYNC_GATE_ARB_LOCK);owner=0;preparing=1;expect(APP_SYNC_GATE_ARB_LOCK);preparing=0;
 stopping=1;expect(APP_SYNC_GATE_ARB_LOCK);stopping=0;pending=1;expect(APP_SYNC_GATE_ARB_LOCK);pending=0;
 arm=1;expect(APP_SYNC_GATE_ARB_LOCK);arm=0;lock=4;expect(APP_SYNC_GATE_ARB_LOCK);lock=0;
 for(source=1;source<=6;source++)expect(APP_SYNC_GATE_SOURCE);source=0;
 for(recovery=1;recovery<=2;recovery++)expect(APP_SYNC_GATE_RECOVERY);recovery=0;
 safety.phase=1;expect(APP_SYNC_GATE_SAFETY);safety.phase=0;wdg=1;expect(APP_SYNC_GATE_WDG_TEST);wdg=0;
 motion=MOTION_RUNNING;expect(APP_SYNC_GATE_MOTION);motion=MOTION_STOPPING;expect(APP_SYNC_GATE_MOTION);motion=MOTION_READY;
 pair=0;expect(APP_SYNC_GATE_PAIR);assert(read_gate().speed_age[0]==0xFFFFFFFFUL);pair=1;
 axes[0].online=0;expect(APP_SYNC_GATE_AXIS_BUSY);axes[0].online=1;
 axes[1].busy=1;expect(APP_SYNC_GATE_AXIS_BUSY);axes[1].busy=0;
 axes[0].speed_valid=0;expect(APP_SYNC_GATE_FEEDBACK);axes[0].speed_valid=1;
 axes[1].fault_valid=0;expect(APP_SYNC_GATE_FEEDBACK);axes[1].fault_valid=1;
 axes[0].fault=1;expect(APP_SYNC_GATE_AXIS_FAULT);axes[0].fault=0;
 axes[1].feedback_degraded=1;expect(APP_SYNC_GATE_AXIS_FAULT);axes[1].feedback_degraded=0;
 axes[0].actual_speed_rpm=-1;expect(APP_SYNC_GATE_NONZERO);axes[0].actual_speed_rpm=0;
 axes[0].speed_age_ms=99;assert(read_gate().gate_ok);axes[0].speed_age_ms=100;expect(APP_SYNC_GATE_STALE);
 axes[0].speed_age_ms=0;axes[1].fault_age_ms=100;expect(APP_SYNC_GATE_STALE);axes[1].fault_age_ms=0;
 locked=1;expect(APP_SYNC_GATE_TRANSACTION);locked=0;
 /* Queries neither establish zero hold nor alter the service's state. */
 tick(600);owner=1;tick(601);owner=0;now=1200;nrf.updated_ms=now;expect(APP_SYNC_GATE_ZERO_HOLD);
 tick(1200);tick(1699);expect(APP_SYNC_GATE_ZERO_HOLD);tick(1700);assert(read_gate().gate_ok);
 app_sync_params_read(&p);p.kp=.4f;deny_lock=1;
 assert(app_sync_storage_submit(&p,1,uid,1234,55)==TUNE_GATE);s=read_gate();assert(s.last_reject_request==55);
 deny_lock=0;assert(app_sync_storage_submit(&p,1,uid,1234,56)==TUNE_PENDING);
 assert(locked && app_sync_params_maintenance());expect(APP_SYNC_GATE_TRANSACTION);
 for(i=0;i<32;i++)tick(1701+i);assert(!locked);app_sync_storage_read(&saved);assert(saved.save_result==TUNE_OK);
 app_sync_params_read(&p);assert(p.kp==.4f);
 /* Gate loss mid-save leaves old committed configuration and RAM. */
 p.kp=.5f;assert(app_sync_storage_submit(&p,2,uid,1234,57)==TUNE_PENDING);tick(1740);owner=1;tick(1741);
 assert(!locked);app_sync_params_read(&p);assert(p.kp==.4f);app_sync_storage_read(&saved);assert(saved.save_result==TUNE_FLASH);
 reset();now=0xFFFFFF00UL;nrf.updated_ms=now;owner=1;app_sync_storage_service();owner=0;tick(now);
 tick(0xFFFFFF00UL+499U);expect(APP_SYNC_GATE_ZERO_HOLD);tick(0xFFFFFF00UL+500U);assert(read_gate().gate_ok);
 puts("PASS production Flash gate: all conditions, read-only query, age/zero/wrap boundaries, lock race and held-save safety");return 0;
}
