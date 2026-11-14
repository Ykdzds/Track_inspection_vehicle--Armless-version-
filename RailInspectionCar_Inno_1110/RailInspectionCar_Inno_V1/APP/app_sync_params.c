#include "app_sync_params.h"
#include "app_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#if defined(__CC_ARM) || defined(APP_SYNC_GATE_PRODUCTION_TEST)
#include "main.h"
#include "app_boot_identity.h"
#include "app_control_arbiter.h"
#include "app_health.h"
#include "aimtor_servo.h"
#include "motion_control.h"
#include "app_cli_servo_session.h"
#endif
#define STORE_BASE 0x080A0000UL
#define STORE_SIZE 0x40000UL
#define SLOT_SIZE 128U
#define STORE_MAGIC 0x34594E53UL
#define STORE_COMMIT 0xC04D17EDUL
typedef char config_size_must_be_36[(sizeof(app_sync_params_t)==36U)?1:-1];
static app_sync_params_t current = { APP_REMOTE_SERVO_SYNC_KP, APP_DUAL_KI, APP_DUAL_KAW,
    APP_REMOTE_SERVO_SYNC_LIMIT_RPM, APP_REMOTE_SERVO_SYNC_RATE_RPM_S,
    APP_REMOTE_SERVO_SYNC_DEADBAND_RPM, APP_DUAL_PI_ENABLED, APP_DUAL_S_ENABLED, APP_DUAL_JERK_MPS3 };
static app_sync_params_t pending;
static uint32_t version = 1U;
static uint8_t queued, maintenance, planning_lock;
static uint32_t last_reject_ms, last_reject_request;
static uint32_t zero_since;
static uint8_t zero_seen;
#if defined(__CC_ARM) || defined(APP_SYNC_GATE_PRODUCTION_TEST)
#define APP_SYNC_GATE_NOW() HAL_GetTick()
#else
#define APP_SYNC_GATE_NOW() 0U
#endif
static app_sync_observation_t observations[3];
static app_sync_timing_t timing[3];
static uint8_t timing_seen[3];
static app_sync_target_t targets[3];
static app_sync_storage_status_t storage;
#if defined(__CC_ARM) || defined(APP_SYNC_GATE_PRODUCTION_TEST)
static servo_snapshot_t gate_axis[2];
#endif
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
static app_sync_params_t saving;
static uint8_t record[SLOT_SIZE];
static uint32_t write_slot, write_word, fingerprint;
static uint8_t save_active;
#ifdef APP_SYNC_STORAGE_TEST
static uint8_t *store_bytes;
static uint32_t store_size, test_uid[3];
static uint8_t test_gate=1U;
static int fail_after=-1;
static app_sync_gate_input_t test_conditions;
static uint8_t test_production;
void app_sync_storage_test_conditions(const app_sync_gate_input_t *input) { test_production=(input!=NULL);if(input)test_conditions=*input; }
void app_sync_storage_test_attach(uint8_t *bytes,uint32_t length,const uint32_t uid[3]) { store_bytes=bytes;store_size=length;memcpy(test_uid,uid,12U); }
void app_sync_storage_test_gate(uint8_t ready) { test_gate=ready;test_production=0U; }
void app_sync_storage_test_fail_after(int words) { fail_after=words; }
#else
#define store_bytes ((const uint8_t *)STORE_BASE)
#define store_size STORE_SIZE
#endif
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(uint8_t *p,uint32_t v) { unsigned i;for(i=0;i<4U;i++)p[i]=(uint8_t)(v>>(8U*i)); }
static uint32_t crc32(const uint8_t *p,uint32_t n) { uint32_t c=0xFFFFFFFFUL;unsigned b;while(n--){c^=*p++;for(b=0;b<8U;b++)c=(c>>1)^((c&1U)?0xEDB88320UL:0U);}return ~c; }
static void encode(uint8_t *p,const app_sync_params_t *v) { float f[6];unsigned i;uint32_t bits;f[0]=v->kp;f[1]=v->ki;f[2]=v->kaw;f[3]=v->limit;f[4]=v->rate;f[5]=v->jerk;for(i=0;i<5U;i++){memcpy(&bits,&f[i],4U);put32(p+i*4U,bits);}put32(p+20U,v->deadband);put32(p+24U,v->pi);put32(p+28U,v->s_mode);memcpy(&bits,&f[5],4U);put32(p+32U,bits); }
static void decode(const uint8_t *p,app_sync_params_t *v) { float f[5];uint32_t bits;unsigned i;for(i=0;i<5U;i++){bits=get32(p+i*4U);memcpy(&f[i],&bits,4U);}v->kp=f[0];v->ki=f[1];v->kaw=f[2];v->limit=f[3];v->rate=f[4];v->deadband=get32(p+20U);v->pi=get32(p+24U);v->s_mode=get32(p+28U);bits=get32(p+32U);memcpy(&v->jerk,&bits,4U); }
static uint8_t blank(const uint8_t *p) { unsigned i;for(i=0;i<SLOT_SIZE;i++)if(p[i]!=0xFFU)return 0U;return 1U; }
static uint32_t config_fingerprint(void) { uint32_t values[9];float f;values[0]=0x34594E53UL;values[1]=APP_OPERATION_MAX_MOTOR_RPM;values[2]=APP_SERVO_CONTROL_AXIS;f=APP_DUAL_FRONT_GEAR;memcpy(values+3,&f,4U);f=APP_DUAL_REAR_GEAR;memcpy(values+4,&f,4U);values[5]=(uint32_t)APP_DUAL_FRONT_SIGN;values[6]=(uint32_t)APP_DUAL_REAR_SIGN;f=APP_WHEEL_DIAMETER_M;memcpy(values+7,&f,4U);f=APP_GEAR_REDUCTION_RATIO;memcpy(values+8,&f,4U);return crc32((const uint8_t *)values,sizeof(values)); }
static uint8_t valid_record(const uint8_t *p) { app_sync_params_t v;if(get32(p)!=STORE_MAGIC || get32(p+4U)!=1U || get32(p+8U)!=SLOT_SIZE || get32(p+124U)!=STORE_COMMIT || crc32(p,120U)!=get32(p+120U))return 0U;decode(p+40U,&v);return app_sync_params_valid(&v); }
static void storage_init(void) { uint32_t at;uint8_t nonblank=0U,bad=0U,incompatible=0U,unknown=0U;fingerprint=config_fingerprint();save_active=0U;write_word=0U;
#ifdef APP_SYNC_STORAGE_TEST
memcpy(storage.uid,test_uid,12U);
#else
storage.uid[0]=*(const uint32_t *)UID_BASE;storage.uid[1]=*(const uint32_t *)(UID_BASE+4U);storage.uid[2]=*(const uint32_t *)(UID_BASE+8U);zero_seen=0U;
#endif
for(at=0U;at+SLOT_SIZE<=store_size;at+=SLOT_SIZE) { const uint8_t *p=store_bytes+at;if(blank(p)){storage.free_slots++;continue;}nonblank=1U;if(get32(p)!=STORE_MAGIC){unknown=1U;continue;}if(!valid_record(p)){bad=1U;continue;}if(get32(p+12U)!=fingerprint || memcmp(p+16U,storage.uid,12U)){incompatible=1U;continue;}if(!storage.saved_sequence || (int32_t)(get32(p+28U)-storage.saved_sequence)>0){decode(p+40U,&storage.saved);storage.saved_sequence=get32(p+28U);storage.transaction_boot=get32(p+32U);storage.transaction_request=get32(p+36U);storage.params_crc32=crc32(p+40U,36U);}}
if(storage.saved_sequence){current=storage.saved;storage.load_reason=1U;storage.storage_state=storage.free_slots?1U:4U;}else{storage.load_reason=unknown?4U:incompatible?3U:bad?2U:0U;storage.storage_state=unknown?5U:storage.free_slots?0U:4U;}
if(unknown)storage.storage_state=5U;
(void)nonblank;storage.save_result=TUNE_OK;
}
#endif
static int slot(uint8_t owner) { return owner == 2U ? 0 : owner == 1U ? 1 : owner == 5U ? 2 : -1; }
void app_sync_gate_evaluate(const app_sync_gate_input_t *input,uint32_t zero_ms,
                           uint8_t transaction_busy,app_sync_gate_status_t *out) {
    *out=input->detail;
    out->reason_mask=input->base_reasons;
    out->zero_ms=input->base_reasons?0U:zero_ms;
    if(out->zero_ms<500U)out->reason_mask|=APP_SYNC_GATE_ZERO_HOLD;
    if(transaction_busy)out->reason_mask|=APP_SYNC_GATE_TRANSACTION;
    out->current_blocking=out->reason_mask;
    out->gate_ok=(uint8_t)(out->reason_mask==0U);
}
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
static void gate_collect(app_sync_gate_input_t *input) {
    memset(input,0,sizeof(*input));
#if defined(__CC_ARM) || defined(APP_SYNC_GATE_PRODUCTION_TEST)
    app_sync_gate_status_t *out=&input->detail;
    app_safety_status_t safety;app_health_nrf_status_t nrf;
    uint32_t now=HAL_GetTick();unsigned i;uint8_t pair;
    out->snapshot_ms=now;out->boot=app_boot_identity_id();
    app_health_get_safety_status(&safety);app_health_get_nrf_status(&nrf);
    out->diagnostic=app_health_get_diagnostic_faults();
    if(safety.phase)input->base_reasons|=APP_SYNC_GATE_SAFETY;
    if(app_health_motion_diagnostic_fault(out->diagnostic & ~APP_DIAG_NRF_INIT_ERROR))
        input->base_reasons|=APP_SYNC_GATE_DIAGNOSTIC;
    if(app_health_watchdog_test_requested())input->base_reasons|=APP_SYNC_GATE_WDG_TEST;
    out->owner=app_control_arbiter_owner();out->preparing=app_control_arbiter_preparing();
    out->stopping=app_control_arbiter_stopping();
    out->maintenance=maintenance || app_control_arbiter_parameter_locked();
    if(out->owner || out->preparing || out->stopping || app_control_arbiter_motion_pending() ||
       app_control_arbiter_lock_reason()!=APP_CONTROL_LOCK_NONE || app_control_arbiter_arm_locked())
        input->base_reasons|=APP_SYNC_GATE_ARB_LOCK;
    /* The lock held by this save protects it; an external lock blocks entry. */
    if(app_control_arbiter_parameter_locked() && !maintenance)input->base_reasons|=APP_SYNC_GATE_TRANSACTION;
    if(aimtor_cli_control_active() || aimtor_hmi_control_active() || aimtor_can_control_active() ||
       aimtor_remote_control_active() || aimtor_cli_restoring_hold() || app_cli_servo_session_active())
        input->base_reasons|=APP_SYNC_GATE_SOURCE;
    if(aimtor_maintenance_enabled() || aimtor_recovery_blocks_motion())input->base_reasons|=APP_SYNC_GATE_RECOVERY;
    if(motion_get_state()==MOTION_RUNNING || motion_get_state()==MOTION_STOPPING)input->base_reasons|=APP_SYNC_GATE_MOTION;
    out->nrf_current=nrf.current_fault;
    out->nrf_recovered=nrf.known && nrf.initialized && nrf.ready && nrf.recovered &&
        (uint32_t)(now-nrf.updated_ms)<100U;
    out->nrf_failure=nrf.failure;
    out->nrf_recovery_ms=nrf.known && nrf.initialized && nrf.ready ? now-nrf.recovery_since_ms:0U;
    if(out->nrf_current || !out->nrf_recovered)input->base_reasons|=APP_SYNC_GATE_NRF;
    memset(gate_axis,0,sizeof(gate_axis));pair=aimtor_get_snapshot_pair(gate_axis,gate_axis+1U);
    if(!pair)input->base_reasons|=APP_SYNC_GATE_PAIR;
    for(i=0U;i<2U;i++) {
        servo_snapshot_t *a=gate_axis+i;uint32_t flags=0U;
        out->speed[i]=pair?(uint32_t)(int32_t)a->actual_speed_rpm:0U;
        out->speed_age[i]=pair?a->speed_age_ms:0xFFFFFFFFUL;
        out->fault_age[i]=pair?a->fault_age_ms:0xFFFFFFFFUL;
        if(a->online)flags|=1U;if(a->busy)flags|=2U;if(a->speed_valid)flags|=4U;
        if(a->fault_valid)flags|=8U;if(a->fault || a->fault_code)flags|=16U;
        if(a->feedback_degraded)flags|=32U;if(pair)flags|=128U;
        out->axis_flags|=flags<<(i*8U);
        if(!a->online || a->busy)input->base_reasons|=APP_SYNC_GATE_AXIS_BUSY;
        if(!a->speed_valid || !a->fault_valid)input->base_reasons|=APP_SYNC_GATE_FEEDBACK;
        if(a->fault || a->fault_code || a->feedback_degraded)input->base_reasons|=APP_SYNC_GATE_AXIS_FAULT;
        if(a->actual_speed_rpm)input->base_reasons|=APP_SYNC_GATE_NONZERO;
        if(out->speed_age[i]>=100U || out->fault_age[i]>=100U)input->base_reasons|=APP_SYNC_GATE_STALE;
    }
#else
    if(test_production)*input=test_conditions;
    else {input->detail.nrf_recovered=1U;if(!test_gate)input->base_reasons=APP_SYNC_GATE_DIAGNOSTIC;}
#endif
}
#endif
static uint8_t safe_gate(void) {
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
    app_sync_gate_input_t input;app_sync_gate_status_t result;uint32_t now;
#if defined(APP_SYNC_STORAGE_TEST) && !defined(APP_SYNC_GATE_PRODUCTION_TEST)
    if(!test_production)return test_gate;
#endif
    gate_collect(&input);now=input.detail.snapshot_ms;
    if(input.base_reasons)zero_seen=0U;
    else if(!zero_seen){zero_seen=1U;zero_since=now;}
    app_sync_gate_evaluate(&input,zero_seen?now-zero_since:0U,0U,&result);
    return result.gate_ok;
#else
    return 1U;
#endif
}
void app_sync_gate_read(app_sync_gate_status_t *out) {
    if(!out)return;
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
    app_sync_gate_input_t input;uint32_t elapsed=0U;
    gate_collect(&input);
#if defined(APP_SYNC_STORAGE_TEST) && !defined(APP_SYNC_GATE_PRODUCTION_TEST)
    if(!test_production && test_gate)elapsed=500U;
    else
#endif
    if(zero_seen)elapsed=input.detail.snapshot_ms-zero_since;
    /* Read-only: queries never start/reset/advance the service's zero timer. */
    app_sync_gate_evaluate(&input,elapsed,(uint8_t)(maintenance || queued || save_active),out);
    out->last_reject_ms=last_reject_ms;out->last_reject_request=last_reject_request;
#else
    memset(out,0,sizeof(*out));out->gate_ok=out->nrf_recovered=1U;
#endif
}
static uint8_t lock_begin(void) {
#if defined(__CC_ARM) || defined(APP_SYNC_GATE_PRODUCTION_TEST)
    if(!app_control_arbiter_parameter_begin())return 0U;
#endif
    maintenance=1U;return 1U;
}
static void lock_end(void) {
    maintenance=0U;
#if defined(__CC_ARM) || defined(APP_SYNC_GATE_PRODUCTION_TEST)
    app_control_arbiter_parameter_end();
#endif
}
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
uint8_t app_sync_params_maintenance(void) {
#if defined(__CC_ARM)
    /* Include the arbiter latch, including the few instructions before the local flag is published. */
    return maintenance || app_control_arbiter_parameter_locked();
#else
    return maintenance;
#endif
}
#endif
void app_sync_params_defaults(app_sync_params_t *p) { p->kp=APP_REMOTE_SERVO_SYNC_KP;p->ki=APP_DUAL_KI;p->kaw=APP_DUAL_KAW;p->limit=APP_REMOTE_SERVO_SYNC_LIMIT_RPM;p->rate=APP_REMOTE_SERVO_SYNC_RATE_RPM_S;p->deadband=APP_REMOTE_SERVO_SYNC_DEADBAND_RPM;p->pi=APP_DUAL_PI_ENABLED;p->s_mode=APP_DUAL_S_ENABLED;p->jerk=APP_DUAL_JERK_MPS3; }
void app_sync_params_init(void) { app_sync_params_defaults(&current);queued=maintenance=planning_lock=zero_seen=0U;zero_since=0U;last_reject_ms=last_reject_request=0U;version=1U;memset(observations,0,sizeof(observations));memset(targets,0,sizeof(targets));memset(timing,0,sizeof(timing));memset(timing_seen,0,sizeof(timing_seen));memset(&storage,0,sizeof(storage));
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
    storage_init();
#endif
}
uint8_t app_sync_params_valid(const app_sync_params_t *p) { return p && p->kp>=0.0f && p->kp<=10.0f && p->ki>=0.0f && p->ki<=10.0f && p->kaw>=0.0f && p->kaw<=100.0f && p->limit>=0.0f && p->limit<=APP_REMOTE_SERVO_SYNC_LIMIT_RPM && p->rate>0.0f && p->rate<=APP_REMOTE_SERVO_SYNC_RATE_RPM_S && p->deadband<=10U && p->pi<=1U && (!p->pi || (p->ki>0.0f && p->kaw>0.0f)) && p->s_mode<=1U && p->jerk>=0.0f && p->jerk<=1000.0f && (!p->s_mode || p->jerk>0.0f); }
uint32_t app_sync_params_read(app_sync_params_t *p) { uint32_t v;taskENTER_CRITICAL();if(p)*p=current;v=version;taskEXIT_CRITICAL();return v; }
uint8_t app_sync_params_submit(const app_sync_params_t *p,uint32_t expected) {
    uint8_t result=TUNE_PENDING,change;
    if(!app_sync_params_valid(p))return TUNE_INVALID;
    change=p->s_mode!=current.s_mode || p->jerk!=current.jerk;
    if(change && !safe_gate())return TUNE_GATE;
    taskENTER_CRITICAL();
    if(queued || maintenance)result=TUNE_BUSY;
    else if(expected!=version)result=TUNE_VERSION;
    else if(change && !lock_begin())result=TUNE_GATE;
    taskEXIT_CRITICAL();
    if(result!=TUNE_PENDING)return result;
    /* The latch excludes new authorizations; recheck feedback after taking it. */
    if(change && !safe_gate()){lock_end();return TUNE_GATE;}
    taskENTER_CRITICAL();pending=*p;queued=1U;planning_lock=change;taskEXIT_CRITICAL();
    return TUNE_PENDING;
}
void app_sync_params_service(void) { uint8_t release=0U;taskENTER_CRITICAL();if(queued){current=pending;++version;if(!version)version=1U;queued=0U;release=planning_lock;planning_lock=0U;}taskEXIT_CRITICAL();if(release)lock_end(); }
uint8_t app_sync_params_pending(void) { return queued; }
void app_sync_storage_read(app_sync_storage_status_t *out) { taskENTER_CRITICAL();*out=storage;taskEXIT_CRITICAL(); }
uint8_t app_sync_storage_submit(const app_sync_params_t *p,uint32_t expected,const uint32_t uid[3],uint32_t boot,uint32_t request) {
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
    uint32_t at;uint8_t encoded[36];if(!app_sync_params_valid(p))return TUNE_INVALID;if(memcmp(uid,storage.uid,12U))return TUNE_BOOT;if(maintenance || queued || save_active)return TUNE_BUSY;if(expected!=version)return TUNE_VERSION;
    if(storage.transaction_boot==boot && storage.transaction_request==request && storage.saved_sequence)return !memcmp(p,&storage.saved,sizeof(*p))?TUNE_OK:TUNE_CONFLICT;
    if(storage.storage_state==5U)return TUNE_STORAGE;if(!safe_gate()){last_reject_ms=APP_SYNC_GATE_NOW();last_reject_request=request;return TUNE_GATE;}if(!lock_begin()){last_reject_ms=APP_SYNC_GATE_NOW();last_reject_request=request;return TUNE_GATE;}if(!safe_gate()){last_reject_ms=APP_SYNC_GATE_NOW();last_reject_request=request;lock_end();return TUNE_GATE;}
    encode(encoded,p);if(storage.saved_sequence && !memcmp(p,&storage.saved,sizeof(*p))){taskENTER_CRITICAL();current=*p;if(++version==0U)version=1U;storage.transaction_boot=boot;storage.transaction_request=request;storage.save_result=TUNE_OK;taskEXIT_CRITICAL();lock_end();return TUNE_OK;}
    for(at=0U;at+SLOT_SIZE<=store_size;at+=SLOT_SIZE)if(blank(store_bytes+at))break;
    if(at+SLOT_SIZE>store_size){storage.storage_state=4U;lock_end();return TUNE_FULL;}
    if(storage.saved_sequence==0xFFFFFFFFUL){lock_end();return TUNE_FULL;}
    memset(record,0xFF,sizeof(record));put32(record,STORE_MAGIC);put32(record+4U,1U);put32(record+8U,SLOT_SIZE);put32(record+12U,fingerprint);memcpy(record+16U,storage.uid,12U);put32(record+28U,storage.saved_sequence+1U);put32(record+32U,boot);put32(record+36U,request);memcpy(record+40U,encoded,36U);put32(record+120U,crc32(record,120U));put32(record+124U,STORE_COMMIT);
    saving=*p;write_slot=at;write_word=0U;save_active=1U;storage.free_slots--;
    storage.transaction_boot=boot;storage.transaction_request=request;
    storage.storage_state=2U;storage.save_result=TUNE_PENDING;return TUNE_PENDING;
#else
    (void)p;(void)expected;(void)uid;(void)boot;(void)request;return TUNE_STORAGE;
#endif
}
#if defined(__CC_ARM)
/* No calls or Flash reads while BSY; literal pool is linked into SRAM. Interrupts remain enabled;
 * one word bounds single-bank fetch stalls. Never erase at runtime. */
__attribute__((section("FLASH_PROGRAM_RAM"),noinline)) static uint8_t program_word(uint32_t address,uint32_t value) {
    uint32_t count;
    if(FLASH->SR & FLASH_SR_BSY)return 0U;
    FLASH->SR=FLASH_SR_OPERR|FLASH_SR_WRPERR|FLASH_SR_PGAERR|FLASH_SR_PGPERR|FLASH_SR_PGSERR|FLASH_SR_EOP;
    FLASH->CR=(FLASH->CR & ~(FLASH_CR_PSIZE|FLASH_CR_SER|FLASH_CR_MER|FLASH_CR_SNB))|FLASH_CR_PSIZE_1|FLASH_CR_PG;
    *(volatile uint32_t *)address=value;
    for(count=0U;count<100000U && (FLASH->SR & FLASH_SR_BSY);count++)__NOP();
    if(FLASH->SR & FLASH_SR_BSY)return 0U;
    FLASH->CR &= ~FLASH_CR_PG;
    return (FLASH->SR & (FLASH_SR_OPERR|FLASH_SR_WRPERR|FLASH_SR_PGAERR|FLASH_SR_PGPERR|FLASH_SR_PGSERR))==0U;
}
#endif
void app_sync_storage_service(void) {
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
    uint8_t ok=safe_gate();uint32_t value;if(!save_active)return;
    if(ok && write_word==31U && (memcmp(store_bytes+write_slot,record,124U) || crc32(store_bytes+write_slot,120U)!=get32(record+120U)))ok=0U;
    if(ok){value=get32(record+write_word*4U);
#ifdef APP_SYNC_STORAGE_TEST
        if(fail_after==0)ok=0U;else{if(fail_after>0)fail_after--;put32(store_bytes+write_slot+write_word*4U,value);}
#else
        if(FLASH->CR & FLASH_CR_LOCK){FLASH->KEYR=0x45670123UL;FLASH->KEYR=0xCDEF89ABUL;}
        if(FLASH->CR & FLASH_CR_LOCK)ok=0U;else ok=program_word(STORE_BASE+write_slot+write_word*4U,value);
        if(!(FLASH->SR & FLASH_SR_BSY)){
            uint32_t cache=FLASH->ACR & FLASH_ACR_DCEN;
            FLASH->CR &= ~FLASH_CR_PG;FLASH->CR |= FLASH_CR_LOCK;
            FLASH->ACR &= ~FLASH_ACR_DCEN;FLASH->ACR |= FLASH_ACR_DCRST;FLASH->ACR &= ~FLASH_ACR_DCRST;FLASH->ACR |= cache;
        }
#endif
        if(ok && get32(store_bytes+write_slot+write_word*4U)!=value)ok=0U;
    }
    if(!ok){save_active=0U;storage.storage_state=3U;storage.save_result=TUNE_FLASH;lock_end();return;}
    if(++write_word==32U){save_active=0U;if(!valid_record(store_bytes+write_slot)){storage.storage_state=3U;storage.save_result=TUNE_FLASH;}else{taskENTER_CRITICAL();storage.saved=saving;storage.saved_sequence=get32(record+28U);storage.params_crc32=crc32(record+40U,36U);storage.transaction_boot=get32(record+32U);storage.transaction_request=get32(record+36U);current=saving;if(++version==0U)version=1U;storage.storage_state=storage.free_slots?1U:4U;storage.save_result=TUNE_OK;taskEXIT_CRITICAL();}lock_end();}
#else
    (void)safe_gate();
#endif
}
void app_sync_observe(uint8_t owner,const app_sync_observation_t *o) { int i=slot(owner);if(i<0)return;taskENTER_CRITICAL();observations[i]=*o;taskEXIT_CRITICAL(); }
void app_sync_observation_read(uint8_t owner,app_sync_observation_t *o) { int i=slot(owner);taskENTER_CRITICAL();if(i>=0)*o=observations[i];else memset(o,0,sizeof(*o));taskEXIT_CRITICAL(); }
void app_sync_timing_observe(uint8_t owner,const app_sync_timing_t *o) { int i=slot(owner);if(i<0 || !o)return;taskENTER_CRITICAL();timing[i]=*o;timing_seen[i]=1U;taskEXIT_CRITICAL(); }
uint8_t app_sync_timing_read(uint8_t owner,app_sync_timing_t *o) { int i=slot(owner);uint8_t seen=0U;if(!o)return 0U;taskENTER_CRITICAL();if(i>=0 && timing_seen[i]){*o=timing[i];seen=1U;}else memset(o,0,sizeof(*o));taskEXIT_CRITICAL();return seen; }
void app_sync_target(uint8_t owner,float rpm,uint32_t ms,uint32_t sequence,uint32_t generation) { int i=slot(owner);if(i<0)return;taskENTER_CRITICAL();targets[i].rpm=rpm;targets[i].ms=ms;targets[i].sequence=sequence;targets[i].generation=generation;targets[i].seen=1U;taskEXIT_CRITICAL(); }
void app_sync_target_read(uint8_t owner,app_sync_target_t *t) { int i=slot(owner);taskENTER_CRITICAL();if(i>=0)*t=targets[i];else memset(t,0,sizeof(*t));taskEXIT_CRITICAL(); }
