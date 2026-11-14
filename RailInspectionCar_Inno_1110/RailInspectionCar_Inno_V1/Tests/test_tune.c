#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app_tune_cli.h"
#include "app_tune_diagnostics.h"
#include "app_sync_params.h"
#include "app_dual_axis_motion.h"
static uint32_t tick;
static uint8_t output[256]; static uint16_t output_size; static unsigned sent;
static unsigned diagnostic_count;static uint32_t diagnostic_ids[16];static uint8_t diagnostic_page_ids[16];
uint32_t HAL_GetTick(void) { return tick; }
TickType_t xTaskGetTickCount(void) { return tick; }
uint32_t app_boot_identity_id(void) { return 1234; }
bool app_boot_identity_valid(void) { return true; }
uint16_t modbus_crc16(const uint8_t *p,uint16_t n) {
    uint16_t c=65535U;unsigned i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(uint16_t)((c>>1)^((c&1)?0xA001:0));}return c;
}
uint8_t debug_uart_try_write_frame(const uint8_t *p,uint16_t n,uint16_t reserve) {
    (void)reserve;assert(n<=256);memcpy(output,p,n);output_size=n;++sent;
    if(p[5]==18U) {assert(n==242U);diagnostic_page_ids[diagnostic_count]=p[28];
        memcpy(diagnostic_ids+diagnostic_count,p+20,4U);diagnostic_count++;}
    return 1;
}
void app_tune_snapshot(app_tune_input_t *s) {
    memset(s,0,sizeof(*s));s->boot=1234;s->ms=tick;
}
uint32_t app_tune_diagnostics_capture(uint32_t pages[4][52],const app_tune_diag_commands_t *commands) {
    unsigned i,j;assert(commands->streaming);for(i=0;i<4;i++)for(j=0;j<52;j++)pages[i][j]=i*100+j;
    return APP_TUNE_DIAG_SYSTEM;
}
static uint32_t read32(unsigned offset) {uint32_t v;memcpy(&v,output+12+offset,4);return v;}
static void request(const char *text) {
    char line[192];snprintf(line,sizeof(line),"%s %04X",text,modbus_crc16((const uint8_t *)text,(uint16_t)strlen(text)));
    assert(app_tune_command(line));app_sync_params_service();app_sync_storage_service();app_tune_service();
    assert(output_size>=14 && output[0]==0xA5);
    assert(modbus_crc16(output+4,(uint16_t)(output_size-6))==((uint16_t)output[output_size-2]|((uint16_t)output[output_size-1]<<8)));
}
int main(void) {
    app_sync_params_t p,before;uint32_t v;
#ifdef APP_SYNC_STORAGE_TEST
    static uint8_t flash[0x40000];static const uint32_t uid[3]={1,2,3};
    memset(flash,255,sizeof(flash));app_sync_storage_test_attach(flash,sizeof(flash),uid);
    app_sync_storage_test_gate(1U);app_sync_storage_test_fail_after(-1);
#endif
    { app_sync_timing_t written,read;memset(&written,0,sizeof(written));
      app_sync_params_init();assert(!app_sync_timing_read(1U,&read));
      written.generation=42U;written.batch=2U;written.applied=-1.5f;
      app_sync_timing_observe(1U,&written);written.applied=99.0f;
      assert(app_sync_timing_read(1U,&read) && read.applied==-1.5f);
      assert(!app_sync_timing_read(2U,&read) && !app_sync_timing_read(5U,&read));
      assert(!app_sync_timing_read(99U,&read));
      app_sync_params_init();assert(!app_sync_timing_read(1U,&read)); }
    app_sync_params_init();app_tune_init();request("tune hello 1");assert(output[5]==1 && output_size==162);assert((read32(120)&0x90U)==0x90U);
    /* Query must not publish parameters or occupy a save transaction. */
    app_sync_params_read(&before);
    request("tune gate 1001 1234");
    assert(output[5]==21 && output_size==126 && read32(0)==1234U && read32(4)==1001U);
    assert(read32(8)==TUNE_OK && read32(12)==1U && read32(16)==1U && read32(24)==1U && read32(28)==0U);
    app_sync_params_read(&p);assert(!memcmp(&before,&p,sizeof(p)) && !app_sync_params_pending() && !app_sync_params_maintenance());
    request("tune gate 1002 9999");assert(output[5]==3 && read32(8)==TUNE_BOOT);
    request("tune gate 1003 invalid");assert(output[5]==3 && read32(8)==TUNE_FORMAT);
    request("tune gate 1004 1234 0");assert(output[5]==3 && read32(8)==TUNE_FORMAT);
    {char corrupt_gate[]="tune gate 1005 1234 0000";assert(app_tune_command(corrupt_gate));app_sync_storage_service();app_tune_service();assert(read32(8)==TUNE_CRC);}
#ifdef APP_SYNC_STORAGE_TEST
    app_sync_storage_test_gate(0U);request("tune gate 1006 1234");
    assert(output[5]==21 && output_size==126 && read32(8)==TUNE_GATE && read32(24)==0U && read32(28)!=0U);
    app_sync_storage_test_gate(1U);
#endif
    request("tune set 2 1234 1 1 0.4 0.02 1 4 10 1");assert(read32(8)==0 && read32(12)==2);
    v=app_sync_params_read(&p);assert(v==2 && p.pi && fabsf(p.kp-.4f)<.0001f);
    request("tune set 2 1234 1 1 0.4 0.02 1 4 10 1");assert(read32(8)==0 && app_sync_params_read(NULL)==2);
    request("tune set 2 1234 1 1 0.5 0.02 1 4 10 1");assert(read32(8)==TUNE_CONFLICT);
    before=p;
    request("tune set 3 1234 1 0 0.5 0 0 4 10 1");assert(read32(8)==TUNE_VERSION);
    request("tune set 4 9999 2 0 0.5 0 0 4 10 1");assert(read32(8)==TUNE_BOOT);
    request("tune set 5 1234 2 1 0.5 0 1 4 10 1");assert(read32(8)==TUNE_INVALID);
    request("tune set 6 1234 2 0 nan 0 0 4 10 1");assert(read32(8)==TUNE_FORMAT);
    request("tune set 7 1234 2 0 0.5 0 0 6 10 1");assert(read32(8)==TUNE_INVALID);
    app_sync_params_read(&p);assert(!memcmp(&p,&before,sizeof(p)));
    {char corrupt[]="tune defaults 8 1234 2 0000";assert(app_tune_command(corrupt));app_sync_storage_service();app_tune_service();assert(read32(8)==TUNE_CRC);}
    p.ki=NAN;assert(!app_sync_params_valid(&p));p=before;p.kaw=INFINITY;assert(!app_sync_params_valid(&p));
    request("tune defaults 9 1234 2");assert(read32(8)==0 && read32(12)==3);
    request("tune set 2 1234 1 1 0.4 0.02 1 4 10 1");assert(read32(8)==0 && read32(12)==3);
    request("tune config 90");assert(output[5]==19 && output_size==146);
    request("tune setcfg 91 1234 3 0 0.6 0 0 5 20 1 1 0.4");assert(output[5]==19 && read32(8)==0 && read32(12)==4);
    app_sync_params_read(&p);assert(p.s_mode && p.jerk==.4f);
    request("tune set 92 1234 4 0 0.5 0 0 5 20 1");app_sync_params_read(&p);assert(p.s_mode && p.jerk==.4f);
    request("tune enhanced 93 1234 1");
    request("tune stream 94 1234 1");tick=50;app_sync_storage_service();app_tune_service();assert(output[5]==20 && output_size==158);
    request("tune hello 95");tick=100;app_sync_storage_service();app_tune_service();assert(output[5]==16); /* legacy reconnect resets subscription */
    tick=0;
    request("tune stream 10 1234 1");tick=50;app_sync_storage_service();app_tune_service();assert(output[5]==16 && output_size==130);
    request("tune diag 11 1234 1");tick=51;app_sync_storage_service();app_tune_service();
    assert(output[5]==18 && output_size==242 && output[28]==0 && output[29]==4);
    tick=301;app_sync_storage_service();app_tune_service();tick=551;app_sync_storage_service();app_tune_service();tick=801;app_sync_storage_service();app_tune_service();
    assert(diagnostic_count==4U);
    for(unsigned page=0U;page<4U;page++) {
        assert(diagnostic_page_ids[page]==page && diagnostic_ids[page]==1U);
    }
    {unsigned count=sent;tick=4000;app_sync_storage_service();app_tune_service();assert(count==sent);}
    request("tune stream 12 1234 1");tick=4050;app_sync_storage_service();app_tune_service();
    assert(output[5]!=18); /* lease expiry disabled diagnostics until explicitly requested */
    app_sync_params_init();assert(app_sync_params_read(&p)==1 && p.pi==0 && fabsf(p.kp-.3f)<.0001f);
    {
        servo_snapshot_t feedback[2];remote_sync_status_t sync;app_dual_axis_motion_t motion;
        app_sync_observation_t first,held;int16_t rpm[2];unsigned i;
        memset(feedback,0,sizeof(feedback));memset(&sync,0,sizeof(sync));
        app_dual_begin(&motion,2U,42U,3U,0U,.15f,.3f);
        app_dual_target(&motion,app_dual_nominal_rpm_to_wheel(100),1U,0U,0U,0U);
        for(i=0U;i<2U;i++){feedback[i].speed_valid=feedback[i].fault_valid=1U;
            feedback[i].speed_sequence=1U;feedback[i].speed_sample_tick=50U;
            feedback[i].actual_speed_rpm=(int16_t)(i?96:100);}
        tick=1050U;
#ifdef APP_SYNC_STORAGE_TEST
        assert(app_dual_output(&motion,&sync,feedback,feedback+1,50U,50U,1U,rpm));
#else
        assert(app_dual_output(&motion,&sync,feedback,feedback+1,50U,1050U,1U,rpm));
#endif
        app_sync_observation_read(2U,&first);assert(first.pair_version==1U && first.pair_generated_ms==1050U);
        app_sync_params_defaults(&p);p.kp=.4f;
        assert(app_sync_params_submit(&p,1U)==TUNE_PENDING);app_sync_params_service();
        tick=1070U;
#ifdef APP_SYNC_STORAGE_TEST
        assert(app_dual_output(&motion,&sync,feedback,feedback+1,70U,70U,1U,rpm));
#else
        assert(app_dual_output(&motion,&sync,feedback,feedback+1,70U,1070U,1U,rpm));
#endif
        app_sync_observation_read(2U,&held);assert(held.used_version==2U && !sync.new_pair);
        assert(held.pair_version==first.pair_version && held.pair_generated_ms==first.pair_generated_ms &&
            held.pair_aligned_ms==first.pair_aligned_ms && held.pair_reference==first.pair_reference);
    }
    {
        static servo_snapshot_t a[2]; static remote_sync_status_t feedback;
        static app_dual_axis_motion_t motion; app_sync_params_t tuning;
        uint8_t owners[3]={1,2,5}; unsigned owner,i,t; int16_t rpm[2];
        for(owner=0;owner<3;owner++) {
            app_sync_params_init(); memset(a,0,sizeof(a)); memset(&feedback,0,sizeof(feedback));
            app_dual_begin(&motion,owners[owner],42,3,0,.15f,.3f);
            app_dual_target(&motion,app_dual_nominal_rpm_to_wheel(100),1,0,0,0);
            for(t=50;t<=1000;t+=50) {
                for(i=0;i<2;i++){a[i].speed_valid=a[i].fault_valid=1;a[i].speed_sample_tick=t;
                    ++a[i].speed_sequence;a[i].actual_speed_rpm=i?96:100;}
                if(t==500) {
                    app_sync_params_defaults(&tuning);tuning.kp=.5f;tuning.ki=.02f;tuning.kaw=1;tuning.pi=1;
                    assert(app_sync_params_submit(&tuning,1)==TUNE_PENDING);app_sync_params_service();
                }
                assert(app_dual_output(&motion,&feedback,a,a+1,t,t,1,rpm));
                assert(motion.generation==42 && motion.target_sequence==1);
                if(t>=500)assert(motion.parameter_version==2 && motion.pi_mode);
            }
            app_sync_observation_t observed;
            app_sync_observation_read(owners[owner],&observed);assert(observed.used_version==2 && observed.generation==42);
            app_sync_params_read(&tuning);tuning.limit=.1f;tuning.pi=0;
            assert(app_sync_params_submit(&tuning,2)==TUNE_PENDING);app_sync_params_service();
            app_dual_output(&motion,&feedback,a,a+1,1050,1050,1,rpm);
            assert(motion.integral==0 && fabsf(motion.applied/app_dual_nominal_rpm_to_wheel(1))<=.6001f);
        }
    }
#ifdef APP_SYNC_STORAGE_TEST
    {
        app_sync_storage_status_t stored;unsigned i;
        app_sync_params_init();app_tune_init();tick=0U;
        request("tune savecfg 101 1234 1 1 2 3 0 0.4 0 0 5 20 1 0 0");
        assert(output[5]==19U && read32(8)==TUNE_PENDING && read32(128)==TUNE_PENDING);
        app_sync_params_read(&p);assert(p.kp==.3f); /* not applied before Flash validation */
        request("tune config 102");assert(read32(8)==TUNE_OK && read32(128)==TUNE_PENDING);
        for(i=0U;i<30U;i++){app_sync_storage_service();app_tune_service();}
        request("tune config 103");assert(read32(8)==TUNE_OK && read32(128)==TUNE_OK);
        assert(read32(100)==1U && read32(108)==1234U && read32(112)==101U);
        app_sync_params_read(&p);assert(p.kp==.4f && read32(12)==2U);
        app_sync_storage_read(&stored);assert(stored.saved_sequence==1U && stored.free_slots==2047U);
        app_sync_params_init();app_tune_init();request("tune config 104");
        app_sync_params_read(&p);assert(p.kp==.4f && read32(100)==1U && read32(124)==1U);
        request("tune defaults 105 1234 1");app_sync_params_read(&p);assert(p.kp==.3f);
        app_sync_params_init();app_sync_params_read(&p);assert(p.kp==.4f); /* defaults are RAM only */
        puts("PASS wire Flash: asynchronous receipt/query, final CRC readback, reboot restore, RAM-only defaults");
    }
#endif
    puts("PASS tune CRC, binary wire sizes, atomic commit, idempotency, conflicts, boot/version validation, finite values, lease and restart defaults");
    return 0;
}
