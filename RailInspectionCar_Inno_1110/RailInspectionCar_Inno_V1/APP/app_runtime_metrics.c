#include "app_runtime_metrics.h"
#include "main.h"
#include <string.h>
static app_runtime_metrics_t metrics;
static uint32_t last_ms[APP_RUNTIME_SLOT_COUNT], last_cycle[APP_RUNTIME_SLOT_COUNT];
static uint8_t seen[APP_RUNTIME_SLOT_COUNT];
void app_runtime_init(void) {
    memset(&metrics,0,sizeof(metrics)); memset(seen,0,sizeof(seen));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
uint32_t app_runtime_clock(void) { return DWT->CYCCNT; }
void app_runtime_note(uint8_t slot) {
    uint32_t ms, cycles, gap, hz;
    if(slot>=APP_RUNTIME_SLOT_COUNT) return;
    ms=HAL_GetTick();cycles=app_runtime_clock();hz=SystemCoreClock/1000000U;
    if(seen[slot] && hz) {
        /* CYCCNT wraps in about 25s. HAL time disambiguates long suspensions. */
        gap=(uint32_t)(ms-last_ms[slot]);
        if(gap<20000U) gap=(uint32_t)(cycles-last_cycle[slot])/hz;
        else gap=gap>4294967U?0xFFFFFFFFUL:gap*1000U;
        if(gap>metrics.gap_max_us[slot]) metrics.gap_max_us[slot]=gap;
    }
    last_ms[slot]=ms;last_cycle[slot]=cycles;seen[slot]=1U;
}
void app_runtime_max(uint8_t metric,uint32_t cycles) {
    if(metric<APP_RUNTIME_METRIC_COUNT && cycles>metrics.max_cycles[metric])
        metrics.max_cycles[metric]=cycles;
}
void app_runtime_read(app_runtime_metrics_t *out) {
    unsigned i;
    /* Aligned 32-bit counters; diagnostic values may span adjacent updates. */
    for(i=0U;i<APP_RUNTIME_SLOT_COUNT;i++) out->gap_max_us[i]=metrics.gap_max_us[i];
    for(i=0U;i<APP_RUNTIME_METRIC_COUNT;i++) out->max_cycles[i]=metrics.max_cycles[i];
}
