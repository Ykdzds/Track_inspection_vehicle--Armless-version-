#ifndef NRF_STATUS_TELEMETRY_H
#define NRF_STATUS_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>
#include "app_remote_servo.h"
#include "app_position_monitor.h"
#include "app_cli_servo_session.h"
#include "motion_control.h"

/* 内部组帧输入，不直接映射无线字节。 */
typedef struct nrf_status_input
{
    remote_servo_status_t remote;
    position_monitor_status_t position;
    servo_snapshot_t axis[SERVO_AXIS_COUNT];
    uint8_t axis_flags;
    uint8_t valid_flags;
    uint8_t alarm_level;
    uint8_t alarm_code;
    uint8_t alarm_source;
    uint8_t system_state;
    uint8_t mode;
    uint8_t authorization_flags;
    uint8_t communication_state;
    uint8_t gate_ready;
    uint8_t alarm_sequence;
    uint32_t alarm_tick_ms;
    uint32_t alarm_detail;
    uint32_t snapshot_sequence, snapshot_ms;
    uint32_t recovery_generation, recovery_healthy_ms;
    uint8_t recovery_state, recovery_eligible, recovery_failure;
    uint8_t control_owner;
    int16_t can_target_mm_s;
    uint8_t control_authorized;
} nrf_status_input_t;

typedef struct nrf_snapshot_context
{
    cli_servo_session_status_t cli;
    servo_startup_stop_status_t startup[2];
    servo_startup_hold_status_t hold[2];
    servo_boot_zero_status_t zero[2];
    servo_wdg_recovery_status_t watchdog[2];
    uint32_t reset, health;
    uint8_t ready_mask, hold_active, motion_allowed, maintenance;
    uint8_t startup_valid, owner, authorized;
    int16_t target_rpm, command_rpm[2];
    motion_state_t motion;
} nrf_snapshot_context_t;

typedef struct
{
    uint32_t generated;
    uint32_t queue_failures;
    uint32_t generated_by_type[10];
    uint32_t queued_by_type[10];
    uint8_t enabled;
    uint8_t session;
    uint8_t last_sequence;
    uint8_t last_type;
    uint8_t alarm_code;
    uint32_t max_age_ms[10];
    uint32_t overdue_count[10];
    uint32_t fast_burst_limited;
    uint32_t fast_urgent_count;
    uint32_t alarm_event_priority_count;
} nrf_telemetry_stats_t;

/* 纯编码接口，严格遵循V1.1的32字节、大端、CRC和无效值定义。 */
bool nrf_status_encode(uint8_t frame[32], uint8_t type, uint8_t sequence,
                       uint8_t session, const nrf_status_input_t *input);
void nrf_status_telemetry_init(uint8_t session);
/* 只由NRF任务调用，生成和装载失败不调用任何运动控制接口。 */
void nrf_status_telemetry_service(void);
void nrf_status_telemetry_get_stats(nrf_telemetry_stats_t *stats);
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
/* Summary=8 words; phases 1/2 each return five type counters (20 words). */
uint8_t nrf_status_telemetry_diag_words(uint32_t *words, uint8_t phase);
#endif

#endif
