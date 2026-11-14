#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_tune_diagnostics.h"
#include "app_sync_params.h"
#include "app_health.h"
#include "bsp_system.h"
#include "bsp_rs485.h"
#include "debug_uart.h"
#include "modbus_rtu.h"
#include "main.h"
test_core_debug_t test_core_debug;
test_dwt_t test_dwt;
uint32_t SystemCoreClock=168000000U;
static uint8_t available=1U;
static unsigned reads;
static uint8_t timing_available=1U, authorized=1U, snapshot_fresh=1U;
static uint32_t timing_generation=42U,timing_ms=1450U;
uint8_t app_sync_timing_read(uint8_t owner,app_sync_timing_t *out) {
    memset(out,0,sizeof(*out));assert(owner==1U);
    out->generation=timing_generation;out->batch=12U;out->observed_ms=timing_ms;
    out->calc_interval_ms=out->pair_gap_ms=120U;out->slew_dt_ms=50U;
    out->flags=APP_SYNC_TIMING_NEW_PAIR|APP_SYNC_TIMING_FEEDBACK_VALID|
        APP_SYNC_TIMING_CORRECTION_VALID|APP_SYNC_TIMING_COMPENSATE;
    out->requested=-1.5f;out->applied=-1.0f;out->new_pair_count=12U;
    out->slew_update_count=12U;out->long_gap_count=11U;return timing_available;
}
uint32_t HAL_GetTick(void) {return 1500U;}
uint32_t HAL_GetUIDw0(void) {return 11U;}
uint32_t HAL_GetUIDw1(void) {return 22U;}
uint32_t HAL_GetUIDw2(void) {return 33U;}
size_t xPortGetFreeHeapSize(void) {return 1024U;}
size_t xPortGetMinimumEverFreeHeapSize(void) {return 512U;}
uint32_t app_can_config_fingerprint(void) {return 123456U;}
uint8_t app_system_snapshot_read_diagnostic(app_tune_diag_system_t *out) {
    unsigned i;memset(out,0,sizeof(*out));out->now_ms=1490U;out->reset_flags=1UL<<29;
    out->diagnostic=128U;out->owner=1U;out->failed_task=2U;
    out->arbiter_generation=42U;out->snapshot_fresh=snapshot_fresh;
    out->control_authorized=authorized;
    for(i=0;i<5;i++)out->heartbeat_ms[i]=i;
    for(i=0;i<12;i++)out->stack_words[i]=32U+i;
    return available;
}
void app_health_get_safety_status(app_safety_status_t *out) {
    memset(out,0,sizeof(*out));out->phase=4U;out->failure=99U;
}
void app_health_get_nrf_status(app_health_nrf_status_t *out) {
    memset(out,0,sizeof(*out));out->known=1U;out->initialized=out->ready=out->recovered=1U;
    out->updated_ms=1495U;out->config_checks=9U;out->stable_checks=2U;
    out->failure_count=3U;out->recovery_count=2U;out->spi_errors=4U;
}
uint8_t bsp_system_get_previous_exception(bsp_system_fault_record_t *out) {
    memset(out,0,sizeof(*out));out->boot=1524U;out->ms=145000U;out->exception_kind=1U;
    out->fault_code=0x80000001UL;out->pc=0x08012345UL;out->cfsr=0x200U;
    out->flags=BSP_SYSTEM_FAULT_STACK_VALID;return 1U;
}
uint8_t bsp_system_get_previous_fault(bsp_system_fault_record_t *out) {
    memset(out,0,sizeof(*out));out->boot=1524U;out->fault_code=128U;
    out->task_id=2U;out->ms=144900U;return 1U;
}
uint8_t bsp_system_get_current_exception(bsp_system_fault_record_t *out) {(void)out;return 0U;}
uint8_t bsp_system_get_current_fault(bsp_system_fault_record_t *out) {(void)out;return 0U;}
void debug_uart_get_stats(debug_uart_stats_t *out) {
    memset(out,0,sizeof(*out));out->rx_ore=7U;out->rx_queue_drops=8U;out->rx_running=1U;
    out->rx_parse_max_cycles=123U;out->tx_critical_max_cycles=456U;reads++;
}
void rs485_get_stats(rs485_stats_t *out) {
    memset(out,0,sizeof(*out));out->uart_errors=9U;out->tx_timeouts=10U;reads++;
}
void modbus_get_stats(modbus_stats_t *out) {
    memset(out,0,sizeof(*out));out->timeouts=11U;out->crc_errors=12U;reads++;
}
void modbus_get_diagnostic(modbus_diagnostic_t *out) {
    memset(out,0,sizeof(*out));out->trace_total=2U;out->status=MODBUS_TIMEOUT;
    out->transaction=123U;out->address=0x0603U;out->axis=1U;
}
int main(void) {
    uint32_t pages[4][52], mask;
    app_tune_diag_commands_t commands={15U,2U,3U,4U,2500U,1U,1U};
    mask=app_tune_diagnostics_capture(pages,&commands);
    assert((mask&APP_TUNE_DIAG_PREVIOUS) && !(mask&APP_TUNE_DIAG_CURRENT));
    assert(pages[0][0]==(1UL<<29) && pages[0][12]==10U);
    assert(pages[0][14]==1524U && pages[0][19]==0x08012345UL);
    assert(pages[0][40]==128U && pages[0][42]==2U && pages[0][48]==1U);
    assert(pages[0][26]==0xFFFFFFFFUL && pages[0][51]==0xFFFFFFFFUL);
    assert(pages[1][5]==7U && pages[1][18]==8U && pages[1][40]==9U);
    assert(pages[2][16]==43U && pages[2][27]==1024U);
    assert(pages[2][25]==123U && pages[2][26]==456U);
    assert(pages[3][3]==11U && pages[3][9]==123U && pages[3][15]==0x0603U);
    assert(pages[3][20]==15U && pages[3][23]==2500U && reads==3U);
    assert((mask&APP_TUNE_DIAG_NRF) && pages[3][28]==1U && pages[3][31]==1U);
    assert(pages[3][36]==9U && pages[3][38]==3U && pages[3][40]==4U && pages[3][45]==5U);
    assert((mask&APP_TUNE_DIAG_SYNC_TIMING) && pages[2][29]==42U && pages[2][30]==12U);
    assert(pages[2][32]==120U && pages[2][34]==0U && pages[2][35]==50U);
    assert(pages[2][38]&APP_SYNC_TIMING_CURRENT);
    {float requested,applied;memcpy(&requested,&pages[2][40],4U);memcpy(&applied,&pages[2][41],4U);
     assert(requested==-1.5f && applied==-1.0f);}
    assert(pages[2][42]==12U && pages[2][46]==11U && pages[2][49]==0xFFFFFFFFUL);
    timing_ms=1400U;mask=app_tune_diagnostics_capture(pages,&commands);
    assert((mask&APP_TUNE_DIAG_SYNC_TIMING) && !(pages[2][38]&APP_SYNC_TIMING_CURRENT));
    timing_ms=1450U;authorized=0U;mask=app_tune_diagnostics_capture(pages,&commands);
    assert(!(pages[2][38]&APP_SYNC_TIMING_CURRENT));
    authorized=1U;timing_generation=41U;mask=app_tune_diagnostics_capture(pages,&commands);
    assert(!(mask&APP_TUNE_DIAG_SYNC_TIMING) && pages[2][29]==0xFFFFFFFFUL);
    timing_generation=42U;snapshot_fresh=0U;mask=app_tune_diagnostics_capture(pages,&commands);
    assert(!(mask&APP_TUNE_DIAG_SYNC_TIMING));
    snapshot_fresh=1U;timing_available=0U;mask=app_tune_diagnostics_capture(pages,&commands);
    assert(!(mask&APP_TUNE_DIAG_SYNC_TIMING));
    available=0U;mask=app_tune_diagnostics_capture(pages,&commands);
    assert(!(mask&APP_TUNE_DIAG_SYSTEM));assert(pages[0][0]==0xFFFFFFFFUL);
    puts("PASS diagnostic page mapping, fault validity, cached UART/Modbus inputs and unknown fields");
    return 0;
}
