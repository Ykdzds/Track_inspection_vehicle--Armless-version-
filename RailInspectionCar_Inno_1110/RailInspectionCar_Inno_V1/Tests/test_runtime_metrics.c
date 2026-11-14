#include <assert.h>
#include <stdio.h>
#include "main.h"
#include "app_runtime_metrics.h"
test_core_debug_t test_core_debug;
test_dwt_t test_dwt;
uint32_t SystemCoreClock=168000000U;
static uint32_t now;
uint32_t HAL_GetTick(void) {return now;}
int main(void) {
    app_runtime_metrics_t out;
    app_runtime_init();
    assert((test_core_debug.DEMCR&CoreDebug_DEMCR_TRCENA_Msk) && (test_dwt.CTRL&DWT_CTRL_CYCCNTENA_Msk));
    now=100U;test_dwt.CYCCNT=0xFFFFF000UL;app_runtime_note(APP_RUNTIME_SERVO);
    now=101U;test_dwt.CYCCNT+=168000U;app_runtime_note(APP_RUNTIME_SERVO);
    app_runtime_read(&out);assert(out.gap_max_us[APP_RUNTIME_SERVO]==1000U);
    now=30101U;test_dwt.CYCCNT+=1U;app_runtime_note(APP_RUNTIME_SERVO);
    app_runtime_read(&out);assert(out.gap_max_us[APP_RUNTIME_SERVO]==30000000U);
    now=0xFFFFFFF0UL;test_dwt.CYCCNT=1000U;app_runtime_note(APP_RUNTIME_CLI);
    now=4U;test_dwt.CYCCNT+=20U*168000U;app_runtime_note(APP_RUNTIME_CLI);
    app_runtime_max(APP_RUNTIME_TUNE_CRITICAL,123U);app_runtime_max(APP_RUNTIME_TUNE_CRITICAL,12U);
    app_runtime_note(255U);app_runtime_max(255U,100000U);
    app_runtime_read(&out);
    assert(out.gap_max_us[APP_RUNTIME_CLI]==20000U && out.max_cycles[APP_RUNTIME_TUNE_CRITICAL]==123U);
    now=5000000U;app_runtime_note(APP_RUNTIME_MOTION);
    now=10000000U;app_runtime_note(APP_RUNTIME_MOTION);
    app_runtime_read(&out);assert(out.gap_max_us[APP_RUNTIME_MOTION]==0xFFFFFFFFUL);
    puts("PASS runtime counter wrap, long suspension, saturation, maximum and invalid slots");
    return 0;
}
