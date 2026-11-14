#ifndef APP_SYNC_PARAMS_H
#define APP_SYNC_PARAMS_H
#include <stdint.h>
typedef struct { float kp, ki, kaw, limit, rate; uint32_t deadband, pi, s_mode; float jerk; } app_sync_params_t;
typedef struct {
    float target, reference, integral, requested, applied, error;
    uint32_t generation, batch, used_version, used_ms, constraints, pair_sequence[2], pair_ms;
    uint8_t pair_valid, aligned, authorized, phase;
    uint32_t pair_aligned_ms, pair_generated_ms, pair_version, pair_meta_flags, s_mode;
    float pair_reference, pair_target;
} app_sync_observation_t;

/* Last normal dual-axis allocation, not a live control status. Times are ms;
 * requested/applied are normalized motor rpm. Published by the control task. */
typedef struct {
    uint32_t generation, batch, observed_ms, calc_interval_ms, pair_gap_ms;
    uint32_t integral_dt_ms, slew_dt_ms, raw_age_ms[2], flags, reject_reason;
    float requested, applied;
    uint32_t new_pair_count, slew_update_count, zero_slew_fresh_count;
    uint32_t invalidation_count, long_gap_count, calc_interval_max_ms, slew_dt_max_ms;
} app_sync_timing_t;
#define APP_SYNC_TIMING_NEW_PAIR 1U
#define APP_SYNC_TIMING_FEEDBACK_VALID 2U
#define APP_SYNC_TIMING_CORRECTION_VALID 4U
#define APP_SYNC_TIMING_ALIGNED 8U
#define APP_SYNC_TIMING_COMPENSATE 16U
#define APP_SYNC_TIMING_CURRENT 32U
typedef struct { float rpm; uint32_t ms, sequence, generation; uint8_t seen; } app_sync_target_t;
enum { TUNE_OK=0, TUNE_PENDING=1, TUNE_INVALID=2, TUNE_VERSION=3, TUNE_BUSY=4,
       TUNE_BOOT=5, TUNE_FORMAT=6, TUNE_CRC=7, TUNE_CONFLICT=8, TUNE_GATE=9, TUNE_FLASH=10, TUNE_FULL=11, TUNE_STORAGE=12 };
typedef struct {
    uint32_t boot, snapshot_ms, reason_mask, diagnostic, current_blocking;
    uint32_t owner, preparing, stopping, maintenance, nrf_current, nrf_recovered;
    uint32_t zero_ms, speed[2], speed_age[2], fault_age[2], axis_flags;
    uint32_t last_reject_ms, last_reject_request, nrf_failure, nrf_recovery_ms;
    uint8_t gate_ok;
} app_sync_gate_status_t;
/* Read-only Flash-save gate reasons.  These bits mirror the production gate
 * checks and are part of the kind=21 diagnostic contract. */
enum {
    APP_SYNC_GATE_SAFETY       = (1UL << 0),
    APP_SYNC_GATE_DIAGNOSTIC   = (1UL << 1),
    APP_SYNC_GATE_WDG_TEST     = (1UL << 2),
    APP_SYNC_GATE_ARB_LOCK     = (1UL << 3),
    APP_SYNC_GATE_SOURCE       = (1UL << 4),
    APP_SYNC_GATE_RECOVERY     = (1UL << 5),
    APP_SYNC_GATE_MOTION       = (1UL << 6),
    APP_SYNC_GATE_PAIR         = (1UL << 7),
    APP_SYNC_GATE_AXIS_BUSY    = (1UL << 8),
    APP_SYNC_GATE_FEEDBACK     = (1UL << 9),
    APP_SYNC_GATE_AXIS_FAULT   = (1UL << 10),
    APP_SYNC_GATE_NONZERO      = (1UL << 11),
    APP_SYNC_GATE_STALE        = (1UL << 12),
    APP_SYNC_GATE_ZERO_HOLD    = (1UL << 13),
    APP_SYNC_GATE_NRF          = (1UL << 18),
    APP_SYNC_GATE_TRANSACTION  = (1UL << 20)
};
typedef struct {
    app_sync_params_t saved;
    uint32_t uid[3], saved_sequence, params_crc32, transaction_boot, transaction_request;
    uint32_t free_slots, storage_state, load_reason, save_result;
} app_sync_storage_status_t;
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
uint8_t app_sync_params_maintenance(void);
#else
/* Production storage is hardware-only; unrelated host driver suites stay isolated. */
static inline uint8_t app_sync_params_maintenance(void) { return 0U; }
#endif
void app_sync_storage_service(void);
void app_sync_storage_read(app_sync_storage_status_t *status);
void app_sync_gate_read(app_sync_gate_status_t *status);
/* Pure gate inputs are populated from production caches, also injectable in
 * host tests. No actuator or storage operation occurs in the evaluator. */
typedef struct {
    app_sync_gate_status_t detail;
    uint32_t base_reasons;
} app_sync_gate_input_t;
void app_sync_gate_evaluate(const app_sync_gate_input_t *input, uint32_t zero_ms,
                           uint8_t transaction_busy, app_sync_gate_status_t *status);
uint8_t app_sync_storage_submit(const app_sync_params_t *p,uint32_t expected,
    const uint32_t uid[3],uint32_t boot,uint32_t request);
#ifdef APP_SYNC_STORAGE_TEST
void app_sync_storage_test_attach(uint8_t *bytes,uint32_t length,const uint32_t uid[3]);
void app_sync_storage_test_gate(uint8_t ready);
void app_sync_storage_test_fail_after(int words);
void app_sync_storage_test_conditions(const app_sync_gate_input_t *input);
#endif
void app_sync_params_defaults(app_sync_params_t *p);
void app_sync_params_init(void);
uint8_t app_sync_params_valid(const app_sync_params_t *p);
uint32_t app_sync_params_read(app_sync_params_t *p);
uint8_t app_sync_params_submit(const app_sync_params_t *p, uint32_t expected);
void app_sync_params_service(void);
uint8_t app_sync_params_pending(void);
void app_sync_observe(uint8_t owner, const app_sync_observation_t *o);
void app_sync_observation_read(uint8_t owner, app_sync_observation_t *o);
void app_sync_timing_observe(uint8_t owner, const app_sync_timing_t *o);
uint8_t app_sync_timing_read(uint8_t owner, app_sync_timing_t *o);
void app_sync_target(uint8_t owner, float rpm, uint32_t ms, uint32_t sequence, uint32_t generation);
void app_sync_target_read(uint8_t owner, app_sync_target_t *t);
#endif
