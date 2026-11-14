#include "app_tune_cli.h"
#include "app_tune_diagnostics.h"
#include "app_sync_params.h"
#include "app_config.h"
#include "app_boot_identity.h"
#include "debug_uart.h"
#include "modbus_rtu.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#if APP_TUNE_UART_ENABLED
/* All wire integers and IEEE754 floats are explicitly little endian. */
static uint8_t frame[256];
static uint16_t pos;
static uint32_t sequence, dropped, lease, main_tick, extra_tick;
static uint8_t streaming, response_ready, response_type, diagnostic_enabled;
static uint8_t diagnostic_page, enhanced, response_wait_ram;
static app_sync_storage_status_t storage_status;
static uint32_t diagnostic_tick, diagnostic_sequence, diagnostic_valid;
static uint32_t diagnostic_pages[APP_TUNE_DIAG_PAGES][APP_TUNE_DIAG_WORDS];
static app_tune_diag_commands_t command_stats;
static uint32_t response_id, response_code;
static struct { char text[192]; uint32_t id, code; } replay[8];
static uint8_t replay_next, pending_replay;
static app_tune_input_t input;
static app_sync_observation_t observation;
static app_sync_target_t target;
static app_sync_params_t params;
static void put8(uint8_t v) { frame[pos++]=v; }
static void put32(uint32_t v) { unsigned i; for(i=0;i<4U;++i) put8((uint8_t)(v>>(i*8U))); }
static void f32(float v) { uint32_t b; memcpy(&b,&v,4); put32(b); }
static void begin(uint8_t type) {
    pos=0; put8(0xA5);put8(0x5A);put8(0x54);put8(0x4E);put8(1);put8(type);put8(0);put8(0);put32(++sequence);
}
static uint8_t send(void) {
    uint16_t crc, size=(uint16_t)(pos-12U); frame[6]=(uint8_t)size; frame[7]=(uint8_t)(size>>8);
    crc=modbus_crc16(frame+4,(uint16_t)(pos-4U)); put8((uint8_t)crc);put8((uint8_t)(crc>>8));
    if(!debug_uart_try_write_frame(frame,pos,1024U)) { ++dropped; return 0; } return 1;
}
static void pack_params(const app_sync_params_t *p) {
    f32(p->kp);f32(p->ki);f32(p->kaw);f32(p->limit);f32(p->rate);put32(p->deadband);put32(p->pi);
}
static void pack_config(const app_sync_params_t *p) {pack_params(p);put32(p->s_mode);f32(p->jerk);}
static void pack_gate(void) {
    app_sync_gate_status_t g; unsigned i;
    app_sync_gate_read(&g); put32(app_boot_identity_id()); put32(response_id); put32(g.gate_ok ? TUNE_OK : TUNE_GATE); put32(app_sync_params_read(NULL));
    put32(1U); put32(g.snapshot_ms); put32(g.gate_ok); put32(g.reason_mask); put32(g.diagnostic); put32(g.current_blocking);
    put32(g.owner); put32(g.preparing); put32(g.stopping); put32(g.maintenance); put32(g.nrf_current); put32(g.nrf_recovered); put32(g.zero_ms);
    for (i=0U;i<2U;++i) put32(g.speed[i]); for (i=0U;i<2U;++i) put32(g.speed_age[i]); for (i=0U;i<2U;++i) put32(g.fault_age[i]);
    put32(g.axis_flags); put32(g.last_reject_ms); put32(g.last_reject_request); put32(g.nrf_failure); put32(g.nrf_recovery_ms);
}
static uint8_t reply(void) {
    uint32_t version=app_sync_params_read(&params);
    if(response_type==21U) { begin(response_type); pack_gate(); return send(); }
    begin(response_type); put32(app_boot_identity_id());put32(response_id);put32(response_code);put32(version);
    if(response_type==19U) {
        unsigned i;pack_config(&params);app_sync_storage_read(&storage_status);pack_config(&storage_status.saved);
        for(i=0U;i<3U;i++)put32(storage_status.uid[i]);
        put32(storage_status.saved_sequence);put32(storage_status.params_crc32);put32(storage_status.transaction_boot);
        put32(storage_status.transaction_request);put32(storage_status.free_slots);put32(storage_status.storage_state);
        put32(storage_status.load_reason);put32(storage_status.save_result);return send();
    }
    pack_params(&params);
    if(response_type==1U || response_type==2U) {
        app_sync_params_defaults(&params); pack_params(&params);
        f32(APP_GEAR_REDUCTION_RATIO);f32(APP_DUAL_FRONT_GEAR);f32(APP_DUAL_REAR_GEAR);
        f32((float)APP_DUAL_FRONT_SIGN);f32((float)APP_DUAL_REAR_SIGN);
        f32(10.0f);f32(10.0f);f32(100.0f);f32(APP_REMOTE_SERVO_SYNC_LIMIT_RPM);
        f32(APP_REMOTE_SERVO_SYNC_RATE_RPM_S);put32(10U);put32(100U);put32(0xFFU); /* existing 0x7F + read-only gate 0x80 */
        /* Fixed device signature for safe discovery. */
        put32(0x34304652UL);put32(0x31545537UL);
        { static const char build[16]="sync-tune-1.6"; unsigned i;
          for(i=0;i<sizeof(build);++i) put8((uint8_t)build[i]); }
    }
    return send();
}
static uint8_t number(const char *s,uint32_t *v) {
    unsigned long n; char *end;
    if(!s || !*s || *s=='-') return 0U;
    errno=0; n=strtoul(s,&end,10);
    if(errno || *end || n>0xFFFFFFFFUL) return 0U; *v=(uint32_t)n; return 1U;
}
static uint8_t real(const char *s,float *v) {
    char *end; double n; if(!s || !*s) return 0U;
    errno=0; n=strtod(s,&end); if(errno || *end || !(n>=0.0 && n<=1000.0)) return 0U;
    *v=(float)n; return 1U;
}
void app_tune_init(void) {
    enhanced=0U;sequence=dropped=lease=main_tick=extra_tick=0U; streaming=response_ready=0U;
    memset(replay,0,sizeof(replay)); replay_next=pending_replay=0U;
    diagnostic_enabled=0U;diagnostic_page=APP_TUNE_DIAG_PAGES;
    diagnostic_tick=diagnostic_sequence=diagnostic_valid=0U;
    memset(&command_stats,0,sizeof(command_stats));
}
uint8_t app_tune_command(char *line) {
    char original[192], *tokens[20], *p, *crc_text, *end;
    uint32_t id=0U,boot=0U,expected=0U,code=TUNE_FORMAT, enabled=0U,uid[3];
    unsigned n=0U,i; unsigned long crc; uint8_t type=3U,writing=0U;
    if(strncmp(line,"tune ",5)!=0) return 0U;
    if(response_ready) { ++command_stats.busy_drops;return 1U; } /* one bounded outstanding response; host retries reads */
    strncpy(original,line,sizeof(original)-1U);original[sizeof(original)-1U]=0;
    crc_text=strrchr(line,' ');
    if(!crc_text) goto done;
    errno=0; crc=strtoul(crc_text+1,&end,16);
    if(errno || *end || end==crc_text+1 || crc>65535UL ||
        crc!=modbus_crc16((const uint8_t *)line,(uint16_t)(crc_text-line))) { code=TUNE_CRC;goto done; }
    *crc_text=0; p=strtok(line," ");
    while(p && n<20U) {tokens[n++]=p;p=strtok(NULL," ");}
    if(p || n<3U || !number(tokens[2],&id)) goto done;
    if(!strcmp(tokens[1],"hello") && n==3U) {enhanced=0U;type=1U;code=TUNE_OK;goto done;}
    if(!strcmp(tokens[1],"get") && n==3U) {type=2U;code=TUNE_OK;goto done;}
    if(!strcmp(tokens[1],"config") && n==3U) {type=19U;code=TUNE_OK;goto done;}
    if(!strcmp(tokens[1],"gate") && n==4U) {
        if(!number(tokens[3],&boot)) { code=TUNE_FORMAT; goto done; }
        if(boot!=app_boot_identity_id() || !app_boot_identity_valid()) { code=TUNE_BOOT; goto done; }
        type=21U;code=TUNE_OK;goto done;
    }
    if(n<4U || !number(tokens[3],&boot)) goto done;
    if(boot!=app_boot_identity_id() || !app_boot_identity_valid()) {code=TUNE_BOOT;goto done;}
    if(!strcmp(tokens[1],"stream") && n==5U && number(tokens[4],&enabled) && enabled<=1U) {
        streaming=(uint8_t)enabled;lease=HAL_GetTick();
        if(!enabled) diagnostic_enabled=enhanced=0U;
        code=TUNE_OK;goto done;
    }
    if(!strcmp(tokens[1],"enhanced") && n==5U && number(tokens[4],&enabled) && enabled<=1U) {enhanced=(uint8_t)enabled;code=TUNE_OK;goto done;}
    if(!strcmp(tokens[1],"diag") && n==5U && number(tokens[4],&enabled) && enabled<=1U) {
        diagnostic_enabled=(uint8_t)enabled;
        if(enabled) { diagnostic_tick=HAL_GetTick()-1000U;diagnostic_page=APP_TUNE_DIAG_PAGES; }
        code=TUNE_OK;goto done;
    }
    writing=(!strcmp(tokens[1],"set") || !strcmp(tokens[1],"defaults") || !strcmp(tokens[1],"setcfg") || !strcmp(tokens[1],"savecfg"));
    if(!strcmp(tokens[1],"setcfg") || !strcmp(tokens[1],"savecfg"))type=19U;
    if(!writing || n<5U || !number(tokens[4],&expected)) goto done;
    for(i=0;i<8U;++i) {
        if(id==replay[i].id && replay[i].text[0]) {
            code=!strcmp(original,replay[i].text)?replay[i].code:TUNE_CONFLICT;goto done;
        }
    }
    app_sync_params_read(&params); /* legacy set preserves planning configuration */
    if(!strcmp(tokens[1],"defaults") && n==5U) app_sync_params_defaults(&params);
    else if(!strcmp(tokens[1],"set") && n==12U &&
        number(tokens[5],&params.pi) && real(tokens[6],&params.kp) && real(tokens[7],&params.ki) &&
        real(tokens[8],&params.kaw) && real(tokens[9],&params.limit) && real(tokens[10],&params.rate) &&
        number(tokens[11],&params.deadband)) { /* complete group parsed */ }
    else if(!strcmp(tokens[1],"setcfg") && n==14U &&
        number(tokens[5],&params.pi) && real(tokens[6],&params.kp) && real(tokens[7],&params.ki) &&
        real(tokens[8],&params.kaw) && real(tokens[9],&params.limit) && real(tokens[10],&params.rate) &&
        number(tokens[11],&params.deadband) && number(tokens[12],&params.s_mode) && real(tokens[13],&params.jerk)) { }
    else if(!strcmp(tokens[1],"savecfg") && n==17U && number(tokens[5],uid) && number(tokens[6],uid+1) && number(tokens[7],uid+2) &&
        number(tokens[8],&params.pi) && real(tokens[9],&params.kp) && real(tokens[10],&params.ki) &&
        real(tokens[11],&params.kaw) && real(tokens[12],&params.limit) && real(tokens[13],&params.rate) &&
        number(tokens[14],&params.deadband) && number(tokens[15],&params.s_mode) && real(tokens[16],&params.jerk)) { }
    else goto done;
    code=!strcmp(tokens[1],"savecfg")?app_sync_storage_submit(&params,expected,uid,boot,id):app_sync_params_submit(&params,expected);
    pending_replay=replay_next;replay_next=(uint8_t)((replay_next+1U)%8U);
    replay[pending_replay].id=id;replay[pending_replay].code=code;strcpy(replay[pending_replay].text,original);
done:
    if(code==TUNE_CRC) ++command_stats.crc_rejects;
    if(code==TUNE_FORMAT) ++command_stats.format_rejects;
    response_wait_ram=(uint8_t)(writing && type!=19U && code==TUNE_PENDING);
    if(writing && type==19U && strcmp(tokens[1],"savecfg"))response_wait_ram=(uint8_t)(code==TUNE_PENDING);
    response_id=id;response_code=code;response_type=type;response_ready=1U; return 1U;
}
void app_tune_service(void) {
    uint32_t now=HAL_GetTick();
    uint32_t tick_ms=(uint32_t)((uint64_t)xTaskGetTickCount()*1000U/configTICK_RATE_HZ);
    unsigned i; uint8_t coherent;
    if(response_ready) {
        if(response_code==TUNE_PENDING && response_wait_ram) {
            if(app_sync_params_pending()) return;
            response_code=TUNE_OK;replay[pending_replay].code=TUNE_OK;
        }
        if(reply()) response_ready=0U;
    }
    if(!streaming) return;
    if((uint32_t)(now-lease)>3000U) {streaming=diagnostic_enabled=enhanced=0U;return;}
    if(diagnostic_enabled) {
        if((uint32_t)(now-diagnostic_tick)>=1000U) {
            diagnostic_tick=now;++diagnostic_sequence;
            command_stats.dropped_frames=dropped;
            command_stats.lease_remaining_ms=3000U-(uint32_t)(now-lease);
            command_stats.streaming=streaming;command_stats.diag_enabled=diagnostic_enabled;
            diagnostic_valid=app_tune_diagnostics_capture(diagnostic_pages,&command_stats);
            diagnostic_page=0U;
        }
        if(diagnostic_page<APP_TUNE_DIAG_PAGES &&
            (uint32_t)(now-diagnostic_tick)>=250U*diagnostic_page) {
            begin(18U);put32(app_boot_identity_id());put32(diagnostic_tick);
            put32(diagnostic_sequence);put32(diagnostic_valid);
            put8(diagnostic_page);put8(APP_TUNE_DIAG_PAGES);put8(1U);put8(0U);
            for(i=0U;i<APP_TUNE_DIAG_WORDS;i++)put32(diagnostic_pages[diagnostic_page][i]);
            /* Congestion never delays the servo/control path or retries indefinitely. */
            send();++diagnostic_page;
        }
    }
    if((uint32_t)(now-main_tick)<50U) return; main_tick=now;
    app_tune_snapshot(&input);app_sync_observation_read(input.owner,&observation);
    coherent=observation.used_version && observation.generation==input.generation;
    begin(enhanced?20U:16U);put32(input.boot);put32(now);put32(input.generation);
    put32(app_sync_params_read(NULL));put32(coherent?observation.used_version:0U);
    put32(coherent?(input.owner==2U?now-(tick_ms-observation.used_ms):observation.used_ms):0U);put32(coherent?observation.batch:0U);
    put32(coherent?observation.constraints:0U);put32(dropped);
    put8(input.owner);put8(input.authorized);put8(input.valid[0]|(input.valid[1]<<1));
    put8(coherent && observation.pair_valid && (uint32_t)(tick_ms-observation.pair_ms)<100U ?
        (uint8_t)(1U|(observation.aligned?2U:0U)):0U);
    app_sync_target_read(input.owner,&target);
    f32(input.authorized && target.seen && target.generation==input.generation?target.rpm:0.0f);
    f32(coherent?observation.reference:0.0f);
    for(i=0;i<2U;++i) f32((float)input.commanded[i]);
    for(i=0;i<2U;++i) f32((float)input.actual[i]);
    f32(coherent?observation.error:0.0f);f32(coherent?observation.integral:0.0f);
    f32(coherent?observation.requested:0.0f);f32(coherent?observation.applied:0.0f);
    for(i=0;i<2U;++i)put32(input.sequence[i]);
    for(i=0;i<2U;++i)put32(input.sample_ms[i]);
    for(i=0;i<2U;++i)put32(input.age[i]);
    for(i=0;i<2U;++i)put32(observation.pair_sequence[i]);
    put32(now-(tick_ms-observation.pair_ms));
    if(enhanced) {
        put32(coherent?observation.pair_aligned_ms:0xFFFFFFFFUL);put32(coherent?observation.pair_generated_ms:0xFFFFFFFFUL);
        put32(coherent?observation.pair_version:0U);f32(coherent?observation.pair_reference:0.0f);
        f32(coherent?observation.pair_target:0.0f);put32(coherent?observation.pair_meta_flags:0U);
        put32(coherent?observation.s_mode:0xFFFFFFFFUL);
    }
    send();
    if((uint32_t)(now-extra_tick)>=200U) {
        static const uint8_t owners[3]={2U,1U,5U}; extra_tick=now;
        begin(17U);put32(input.boot);put32(now);
        for(i=0;i<3U;++i) {
            app_sync_target_read(owners[i],&target);f32(target.rpm);put32(owners[i]==2U?now-(tick_ms-target.ms):target.ms);put32(target.sequence);
            put8(target.seen);put8(input.source_valid[i] && target.seen && target.generation==input.generation);put8(owners[i]);put8(0U);
        }
        send();
    }
}
#else
void app_tune_init(void) { }
uint8_t app_tune_command(char *line) { (void)line; return 0U; }
void app_tune_service(void) { }
#endif /* APP_TUNE_UART_ENABLED */
