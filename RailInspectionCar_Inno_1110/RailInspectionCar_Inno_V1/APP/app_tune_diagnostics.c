#include "app_tune_diagnostics.h"
#include "app_runtime_metrics.h"
#include "app_health.h"
#include "app_can_config.h"
#include "app_boot_identity.h"
#include "app_sync_params.h"
#include "app_config.h"
#include "bsp_system.h"
#include "bsp_rs485.h"
#include "debug_uart.h"
#include "modbus_rtu.h"
#include "main.h"
#include "FreeRTOS.h"
#include <string.h>
#if !defined(__CC_ARM) && !defined(__arm__)
uint32_t HAL_GetUIDw0(void);uint32_t HAL_GetUIDw1(void);uint32_t HAL_GetUIDw2(void);
size_t xPortGetFreeHeapSize(void);size_t xPortGetMinimumEverFreeHeapSize(void);
#endif
#if APP_TUNE_UART_ENABLED
static app_tune_diag_system_t system_state;
static app_safety_status_t safety;
static bsp_system_fault_record_t previous, current_fault, first_previous;
static debug_uart_stats_t uart;
static rs485_stats_t rs485;
static modbus_stats_t modbus;
static modbus_diagnostic_t bus;
static app_runtime_metrics_t runtime;
static app_sync_timing_t sync_timing;
static app_health_nrf_status_t nrf;
static uint32_t float_word(float value) {
    uint32_t word;memcpy(&word,&value,sizeof(word));return word;
}
static uint32_t text_hash(const char *s) {
    uint32_t hash=2166136261UL;
    while(*s) {hash^=(uint8_t)*s++;hash*=16777619UL;}return hash;
}
uint32_t app_tune_diagnostics_capture(uint32_t pages[4][52],const app_tune_diag_commands_t *commands)
{
    uint32_t mask=0U,*p;unsigned i;
    memset(pages,0xFF,4U*52U*sizeof(uint32_t));
    if(app_system_snapshot_read_diagnostic(&system_state)) mask|=APP_TUNE_DIAG_SYSTEM;
    app_health_get_safety_status(&safety);
    /* Exceptions are additional evidence; the first safety trigger is retained separately. */
    if(bsp_system_get_previous_exception(&previous) || bsp_system_get_previous_fault(&previous)) mask|=APP_TUNE_DIAG_PREVIOUS;
    if(bsp_system_get_current_exception(&current_fault) || bsp_system_get_current_fault(&current_fault)) mask|=APP_TUNE_DIAG_CURRENT;
    debug_uart_get_stats(&uart);rs485_get_stats(&rs485);modbus_get_stats(&modbus);
    modbus_get_diagnostic(&bus);app_runtime_read(&runtime);
    mask|=APP_TUNE_DIAG_UART|APP_TUNE_DIAG_RS485|APP_TUNE_DIAG_RUNTIME|APP_TUNE_DIAG_MODBUS;
    p=pages[0];
    if(mask&APP_TUNE_DIAG_SYSTEM) {
        p[0]=system_state.reset_flags;p[1]=system_state.diagnostic;
        p[2]=system_state.watchdog_decision;p[3]=system_state.failed_task;
        p[8]=system_state.owner;p[9]=system_state.lock_reason;
        p[10]=system_state.motion_allowed;p[11]=system_state.ready_mask;
        p[12]=HAL_GetTick()-system_state.now_ms;
    }
    p[4]=safety.phase;p[5]=safety.failure;p[6]=safety.generation;p[7]=safety.boot_locked;
    p[13]=(mask&APP_TUNE_DIAG_PREVIOUS)!=0U;
    if(p[13]) {
        p[14]=previous.boot;p[15]=previous.ms;p[16]=previous.fault_code;p[17]=previous.detail;
        p[18]=previous.exception_kind;p[19]=previous.pc;p[20]=previous.lr;p[21]=previous.cfsr;
        p[22]=previous.hfsr;p[23]=previous.bfar;p[24]=previous.mmfar;
        p[37]=previous.xpsr;p[38]=previous.exc_return;p[45]=previous.flags;
        p[47]=previous.exception_kind!=0U;
    }
    p[25]=(mask&APP_TUNE_DIAG_CURRENT)!=0U;
    if(p[25]) {
        p[26]=current_fault.fault_code;p[27]=current_fault.detail;p[28]=current_fault.exception_kind;
        p[29]=current_fault.pc;p[30]=current_fault.lr;p[39]=current_fault.ms;p[46]=current_fault.flags;
    }
    p[31]=text_hash("sync-tune-1.6 " __DATE__ " " __TIME__);
    p[32]=app_can_config_fingerprint();p[33]=HAL_GetUIDw0();p[34]=HAL_GetUIDw1();p[35]=HAL_GetUIDw2();
    p[36]=SystemCoreClock;
    p[48]=0U;
    if(bsp_system_get_previous_fault(&first_previous) && !first_previous.exception_kind) {
        p[40]=first_previous.fault_code;p[41]=first_previous.detail;
        p[42]=first_previous.task_id;p[43]=first_previous.safety_phase;p[44]=first_previous.ms;p[48]=1U;
    }
    p=pages[1];
    p[0]=uart.rx_bytes;p[1]=uart.rx_consumed;p[2]=uart.rx_overflows;p[3]=uart.rx_dropped_bytes;
    p[4]=uart.rx_uart_errors;p[5]=uart.rx_ore;p[6]=uart.rx_fe;p[7]=uart.rx_ne;p[8]=uart.rx_pe;
    p[9]=uart.rx_dma_errors;p[10]=uart.rx_restarts;p[11]=uart.rx_start_failures;p[12]=uart.rx_abort_failures;
    p[13]=uart.rx_idle_events;p[14]=uart.rx_half_events;p[15]=uart.rx_full_events;p[16]=uart.rx_epoch;
    p[17]=uart.rx_line_overflows;p[18]=uart.rx_queue_drops;p[19]=uart.rx_buffer_used;p[20]=uart.rx_high_water;
    p[21]=uart.rx_running;p[22]=uart.rx_recovery_pending;p[23]=uart.queued_bytes;p[24]=uart.sent_bytes;
    p[25]=uart.dropped_bytes;p[26]=uart.dma_start_count;p[27]=uart.dma_complete_count;
    p[28]=uart.dma_error_count;p[29]=uart.dma_start_failures;p[30]=uart.tx_lock_drops;p[31]=uart.tx_isr_drops;
    p[32]=uart.buffer_used;p[33]=uart.buffer_high_water;p[34]=uart.tx_busy;
    p[35]=uart.tx_critical_max_cycles;p[36]=uart.rx_parse_max_cycles;
    p[37]=rs485.rx_bytes;p[38]=rs485.tx_bytes;p[39]=rs485.rx_overflows;p[40]=rs485.uart_errors;
    p[41]=rs485.tx_timeouts;p[42]=rs485.rx_restarts;p[43]=rs485.dma_start_failures;
    p[44]=rs485.rx_abort_failures;p[45]=rs485.uart_error_flags;p[46]=rs485.hal_error_flags;
    p[47]=rs485.rx_state;p[48]=rs485.dma_remaining;p[49]=rs485.dma_running;
    p[50]=rs485.recovery_pending;p[51]=rs485.uart_sr;
    p=pages[2];
    if(mask&APP_TUNE_DIAG_SYSTEM) {
        for(i=0U;i<5U;i++)p[i]=system_state.heartbeat_ms[i];
        for(i=0U;i<12U;i++)p[i+5U]=system_state.stack_words[i];
    }
    for(i=0U;i<6U;i++)p[i+17U]=runtime.gap_max_us[i];
    for(i=0U;i<4U;i++)p[i+23U]=runtime.max_cycles[i];
    /* UART owns these measurements; unused runtime slots must not look measured. */
    p[25]=uart.rx_parse_max_cycles;p[26]=uart.tx_critical_max_cycles;
    p[27]=xPortGetFreeHeapSize();p[28]=xPortGetMinimumEverFreeHeapSize();
    /* Keep the last allocation explicitly timestamped. Identity is checked
     * against the cached arbiter generation; never read mutable controller state. */
    if((mask&APP_TUNE_DIAG_SYSTEM) &&
       system_state.snapshot_fresh &&
       app_sync_timing_read((uint8_t)system_state.owner,&sync_timing) &&
       sync_timing.generation==system_state.arbiter_generation) {
        uint32_t flags=sync_timing.flags&~APP_SYNC_TIMING_CURRENT;
        if(system_state.control_authorized &&
           (uint32_t)(HAL_GetTick()-sync_timing.observed_ms)<APP_REMOTE_SERVO_SYNC_MAX_AGE_MS)
            flags|=APP_SYNC_TIMING_CURRENT;
        mask|=APP_TUNE_DIAG_SYNC_TIMING;
        p[29]=sync_timing.generation;p[30]=sync_timing.batch;p[31]=sync_timing.observed_ms;
        p[32]=sync_timing.calc_interval_ms;p[33]=sync_timing.pair_gap_ms;
        p[34]=sync_timing.integral_dt_ms;p[35]=sync_timing.slew_dt_ms;
        p[36]=sync_timing.raw_age_ms[0];p[37]=sync_timing.raw_age_ms[1];
        p[38]=flags;p[39]=sync_timing.reject_reason;
        p[40]=float_word(sync_timing.requested);p[41]=float_word(sync_timing.applied);
        p[42]=sync_timing.new_pair_count;p[43]=sync_timing.slew_update_count;
        p[44]=sync_timing.zero_slew_fresh_count;p[45]=sync_timing.invalidation_count;
        p[46]=sync_timing.long_gap_count;p[47]=sync_timing.calc_interval_max_ms;
        p[48]=sync_timing.slew_dt_max_ms;
    }
    p=pages[3];p[0]=modbus.requests;p[1]=modbus.responses;p[2]=modbus.retries;
    p[3]=modbus.timeouts;p[4]=modbus.crc_errors;p[5]=modbus.frame_errors;p[6]=modbus.exceptions;
    if(bus.trace_total && bus.trace_total!=0xFFFFFFFFUL) {
        p[7]=bus.ms;p[8]=bus.status;p[9]=bus.transaction;p[10]=bus.tag;p[11]=bus.owner;
        p[12]=bus.axis;p[13]=bus.slave;p[14]=bus.function;p[15]=bus.address;
        p[16]=bus.transport_epoch;p[17]=bus.attempt;
    }
    p[18]=bus.queue_overflows;p[19]=commands->dropped_frames;p[20]=commands->crc_rejects;
    p[21]=commands->format_rejects;p[22]=commands->busy_drops;p[23]=commands->lease_remaining_ms;
    p[24]=commands->streaming;p[25]=commands->diag_enabled;p[26]=bus.trace_total;p[27]=bus.trace_overwritten;
    app_health_get_nrf_status(&nrf);
    if(nrf.known) {
        mask|=APP_TUNE_DIAG_NRF;
        p[28]=nrf.initialized;p[29]=nrf.ready;p[30]=nrf.current_fault;p[31]=nrf.recovered;
        p[32]=nrf.failure;p[33]=nrf.failure_since_ms;p[34]=nrf.recovery_since_ms;
        p[35]=nrf.updated_ms;p[36]=nrf.config_checks;p[37]=nrf.stable_checks;
        p[38]=nrf.failure_count;p[39]=nrf.recovery_count;p[40]=nrf.spi_errors;
        p[41]=nrf.dma_timeouts;p[42]=nrf.verify_failures;p[43]=nrf.config_mismatches;
        p[44]=nrf.recoveries;p[45]=HAL_GetTick()-nrf.updated_ms;
        p[46]=1U; /* NRF diagnostic layout version. Remaining words stay unknown. */
    }
    return mask;
}
#else
uint32_t app_tune_diagnostics_capture(uint32_t pages[APP_TUNE_DIAG_PAGES][APP_TUNE_DIAG_WORDS],
                                    const app_tune_diag_commands_t *commands)
{
    (void)pages;
    (void)commands;
    return 0U;
}
#endif /* APP_TUNE_UART_ENABLED */
