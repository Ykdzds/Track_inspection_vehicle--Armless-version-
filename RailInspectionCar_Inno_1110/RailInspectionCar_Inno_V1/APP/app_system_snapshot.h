#ifndef APP_SYSTEM_SNAPSHOT_H
#define APP_SYSTEM_SNAPSHOT_H

#include "app_config.h"
#include "app_health.h"
#include "HMI/app_hmi_control.h"
#include "app_can_control.h"
#include "FreeRTOS.h"
#include "app_position_monitor.h"
#include "app_remote_servo.h"
#include "app_cli_servo_session.h"
#include "app_can.h"
#include "bsp_hmi_uart.h"
#include "motion_control.h"
#include "debug_uart.h"
#include "modbus_rtu.h"

#define APP_SYSTEM_UPPER_COUNT (7U)

/* 同一次采集的只读输入；数据源失败时只关闭对应有效标志。 */
typedef struct
{
    servo_snapshot_t axis[2];
    position_monitor_status_t position;
    remote_servo_status_t remote;
    app_can_control_status_t can_control;
    app_hmi_control_status_t hmi_control;
    cli_servo_session_status_t cli;
    servo_debug_status_t debug;
    servo_startup_stop_status_t startup[2];
    servo_startup_hold_status_t hold[2];
    servo_wdg_recovery_status_t watchdog[2];
    servo_recovery_status_t recovery;
    servo_boot_zero_status_t boot_zero[2];
    servo_k1_hold_status_t k1;
    app_can_status_t can;
    can_upper_snapshot_t upper[APP_SYSTEM_UPPER_COUNT];
    can_arm_summary_t arm[CAN_ARM_COUNT];
    uint8_t arm_valid[CAN_ARM_COUNT];
    hmi_uart_stats_t uart;
    debug_uart_stats_t debug_uart;
    modbus_stats_t modbus;
    uint32_t nrf_generation;
    uint32_t now_ms, boot_id, sequence, reset_reason, diagnostic;
    TickType_t now_tick;
    uint32_t heartbeat_ms[APP_HEARTBEAT_COUNT];
    uint16_t watchdog_decision, failed_task, minimum_stack;
    uint16_t init_stage, init_progress, init_complete;
    uint16_t owner, lock_reason, hmi_remaining, hmi_age;
    uint32_t hmi_session;
    motion_state_t motion;
    uint16_t safety_stop_class, safety_fault_reason;
    float motion_velocity[2];
    uint8_t selected, axes_valid, position_valid, remote_valid;
    uint8_t cli_valid, debug_valid, can_valid, startup_valid;
    uint8_t safety_ready, maintenance;
    uint8_t identity_valid, health_valid, owner_valid, k1_valid;
    uint16_t identity_result;
    uint8_t ready_mask, hold_active, motion_allowed, can_control_valid;
    uint32_t arbiter_generation;
    uint8_t arbiter_axes;
    app_control_stop_t stop;
} app_system_snapshot_t;

#define APP_SYSTEM_SNAPSHOT_PERIOD_MS APP_HEALTH_TASK_PERIOD_MS
#if APP_HEALTH_TASK_PERIOD_MS + APP_HMI_SNAPSHOT_PERIOD_MS >= 100U
#error "健康快照与HMI缓存周期之和必须小于100ms有效期"
#endif
#define APP_SYSTEM_SNAPSHOT_MAX_AGE_MS 100U

void app_system_snapshot_init(void);
void app_system_snapshot_service(void);
uint8_t app_system_snapshot_read(app_system_snapshot_t *snapshot);
/* Metadata describes the same atomic read decision, even when that source is
 * expired. Optional outputs never trigger a collection or a retry. */
uint8_t app_system_snapshot_read_with_meta(app_system_snapshot_t *snapshot,
    uint32_t *source_ms, uint32_t *sequence, uint32_t *source_age_ms,
    uint8_t *initialized);
/* Timestamp/sequence remain observable even when the source is stale. */
uint8_t app_system_snapshot_source_meta(uint32_t *source_ms, uint32_t *sequence);
struct nrf_status_input;
struct nrf_snapshot_context;
struct app_can_status_input;
uint8_t app_system_snapshot_read_nrf(struct nrf_status_input *input,
    struct nrf_snapshot_context *context);
uint8_t app_system_snapshot_read_can(struct app_can_status_input *input);

#endif
