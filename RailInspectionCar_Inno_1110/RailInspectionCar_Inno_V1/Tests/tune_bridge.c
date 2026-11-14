/* Test-only bridge to exercise production firmware from desktop tests. */
#include <stdint.h>
#include <string.h>
#include "app_sync_params.h"
#include "app_tune_cli.h"
#include "app_tune_diagnostics.h"
#include "app_dual_axis_motion.h"
static uint32_t now_ms,boot_id=1234U;
static uint8_t bytes[8192];static unsigned used;
static app_dual_axis_motion_t motion;
static remote_sync_status_t sync_state;
static servo_snapshot_t axes[2];
static int16_t command[2];
#ifdef APP_SYNC_STORAGE_TEST
static uint8_t parameter_flash[0x40000];
static const uint32_t parameter_uid[3]={1U,2U,3U};
__declspec(dllexport) void bridge_storage_service(void) { app_sync_storage_service(); }
__declspec(dllexport) void bridge_storage_fail_after(int words) { app_sync_storage_test_fail_after(words); }
__declspec(dllexport) void bridge_storage_gate(uint8_t ready) { app_sync_storage_test_gate(ready); }
__declspec(dllexport) void bridge_reboot(uint32_t boot) {
    now_ms=0U;used=0U;boot_id=boot;
    app_sync_storage_test_attach(parameter_flash,sizeof(parameter_flash),parameter_uid);
    app_sync_storage_test_gate(1U);app_sync_storage_test_fail_after(-1);
    app_sync_params_init();app_tune_init();
}
#endif
/* The bridge exercises the production command/framing path; hardware caches are unknown. */
uint32_t app_tune_diagnostics_capture(uint32_t pages[4][52],const app_tune_diag_commands_t *commands) {
    (void)commands;memset(pages,0xFF,4U*52U*sizeof(uint32_t));return 0U;
}
uint32_t HAL_GetTick(void) {return now_ms;}
TickType_t xTaskGetTickCount(void) {return now_ms;}
uint32_t app_boot_identity_id(void) {return boot_id;}
bool app_boot_identity_valid(void) {return true;}
uint16_t modbus_crc16(const uint8_t *p,uint16_t n) {
    uint16_t c=65535;unsigned i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(uint16_t)((c>>1)^((c&1)?0xA001:0));}return c;
}
uint8_t debug_uart_try_write_frame(const uint8_t *p,uint16_t n,uint16_t reserve) {
    (void)reserve;if(used+n>sizeof(bytes))return 0;memcpy(bytes+used,p,n);used+=n;return 1;
}
void app_tune_snapshot(app_tune_input_t *s) {
    unsigned i;memset(s,0,sizeof(*s));s->boot=boot_id;s->ms=now_ms;s->owner=2;s->generation=42;
    s->authorized=1;s->source_valid[0]=1;
    for(i=0;i<2;i++){s->valid[i]=1;s->actual[i]=axes[i].actual_speed_rpm;s->commanded[i]=command[i];
        s->sample_ms[i]=now_ms;s->sequence[i]=axes[i].speed_sequence;}
}
__declspec(dllexport) void bridge_init(uint32_t boot) {
    now_ms=0;used=0;boot_id=boot;memset(axes,0,sizeof(axes));memset(&sync_state,0,sizeof(sync_state));
#ifdef APP_SYNC_STORAGE_TEST
    memset(parameter_flash,0xFF,sizeof(parameter_flash));
    app_sync_storage_test_attach(parameter_flash,sizeof(parameter_flash),parameter_uid);
    app_sync_storage_test_gate(1U);app_sync_storage_test_fail_after(-1);
#endif
    app_sync_params_init();app_tune_init();
    app_dual_begin(&motion,2,42,3,0,.15f,.30f);
    app_dual_target(&motion,app_dual_nominal_rpm_to_wheel(120),1,0,0,0);
}
__declspec(dllexport) void bridge_command(const char *text) {
    char line[192];strncpy(line,text,191);line[191]=0;
    line[strcspn(line,"\r\n")]=0;app_tune_command(line);
    app_sync_params_service();app_sync_storage_service();app_tune_service();
}
__declspec(dllexport) void bridge_step(uint32_t ms) {
    unsigned i;now_ms=ms;app_sync_params_service();
    for(i=0;i<2;i++) {
        axes[i].speed_valid=axes[i].fault_valid=1;axes[i].speed_sample_tick=ms;
        ++axes[i].speed_sequence;axes[i].actual_speed_rpm=command[i]-(i?1:0);
    }
    app_dual_output(&motion,&sync_state,&axes[0],&axes[1],ms,ms,1,command);
    app_sync_target(2,120,ms,ms/50+1,42);app_sync_storage_service();app_tune_service();
}
__declspec(dllexport) unsigned bridge_read(uint8_t *p,unsigned capacity) {
    unsigned n=used<capacity?used:capacity;memcpy(p,bytes,n);memmove(bytes,bytes+n,used-n);used-=n;return n;
}
