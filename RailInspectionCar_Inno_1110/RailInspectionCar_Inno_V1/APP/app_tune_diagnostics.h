#ifndef APP_TUNE_DIAGNOSTICS_H
#define APP_TUNE_DIAGNOSTICS_H
#include <stdint.h>
#define APP_TUNE_DIAG_WORDS 52U
#define APP_TUNE_DIAG_PAGES 4U
#define APP_TUNE_DIAG_SYSTEM 1U
#define APP_TUNE_DIAG_PREVIOUS 2U
#define APP_TUNE_DIAG_CURRENT 4U
#define APP_TUNE_DIAG_UART 8U
#define APP_TUNE_DIAG_RS485 16U
#define APP_TUNE_DIAG_RUNTIME 32U
#define APP_TUNE_DIAG_MODBUS 64U
#define APP_TUNE_DIAG_SYNC_TIMING 128U
#define APP_TUNE_DIAG_NRF 256U
typedef struct {
    uint32_t now_ms, reset_flags, diagnostic, watchdog_decision, failed_task;
    uint32_t owner, lock_reason, motion_allowed, ready_mask, arbiter_generation;
    uint32_t heartbeat_ms[5], stack_words[12];
    uint8_t snapshot_fresh, control_authorized;
} app_tune_diag_system_t;
typedef struct {
    uint32_t crc_rejects, format_rejects, busy_drops, dropped_frames;
    uint32_t lease_remaining_ms, streaming, diag_enabled;
} app_tune_diag_commands_t;
uint8_t app_system_snapshot_read_diagnostic(app_tune_diag_system_t *out);
/* Only called by CLI, once/second; all inputs are cached, no bus queries. */
uint32_t app_tune_diagnostics_capture(uint32_t pages[APP_TUNE_DIAG_PAGES][APP_TUNE_DIAG_WORDS],
                                    const app_tune_diag_commands_t *commands);
#endif
