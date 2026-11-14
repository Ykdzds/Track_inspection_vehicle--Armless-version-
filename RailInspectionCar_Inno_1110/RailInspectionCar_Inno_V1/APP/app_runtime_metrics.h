#ifndef APP_RUNTIME_METRICS_H
#define APP_RUNTIME_METRICS_H
#include <stdint.h>
#define APP_RUNTIME_SLOT_COUNT 6U
#define APP_RUNTIME_METRIC_COUNT 4U
enum { APP_RUNTIME_MOTION, APP_RUNTIME_SERVO, APP_RUNTIME_HEALTH,
       APP_RUNTIME_MODBUS, APP_RUNTIME_CLI, APP_RUNTIME_HMI };
enum { APP_RUNTIME_SNAPSHOT_CRITICAL, APP_RUNTIME_TUNE_CRITICAL,
       APP_RUNTIME_CLI_PARSE, APP_RUNTIME_UART_CRITICAL };
typedef struct {
    uint32_t gap_max_us[APP_RUNTIME_SLOT_COUNT];
    uint32_t max_cycles[APP_RUNTIME_METRIC_COUNT];
} app_runtime_metrics_t;
#if defined(__CC_ARM) || defined(__arm__) || defined(APP_RUNTIME_METRICS_TEST)
void app_runtime_init(void);
uint32_t app_runtime_clock(void);
void app_runtime_note(uint8_t slot);
void app_runtime_max(uint8_t metric, uint32_t cycles);
void app_runtime_read(app_runtime_metrics_t *out);
#else
/* Host modules without a Cortex cycle counter use explicit no-op instrumentation. */
static inline void app_runtime_init(void) { }
static inline uint32_t app_runtime_clock(void) { return 0U; }
static inline void app_runtime_note(uint8_t slot) { (void)slot; }
static inline void app_runtime_max(uint8_t metric, uint32_t cycles) { (void)metric; (void)cycles; }
static inline void app_runtime_read(app_runtime_metrics_t *out) {
    unsigned i; for(i=0U;i<APP_RUNTIME_SLOT_COUNT;i++) out->gap_max_us[i]=0xFFFFFFFFUL;
    for(i=0U;i<APP_RUNTIME_METRIC_COUNT;i++) out->max_cycles[i]=0xFFFFFFFFUL;
}
#endif
#endif
