#ifndef APP_CAN_STATUS_H
#define APP_CAN_STATUS_H

#include "app_remote_servo.h"
#include "app_cli_servo_session.h"
#include "app_position_monitor.h"
#include "bsp_can.h"
#include "debug_uart.h"
#include "modbus_rtu.h"
#include "can_protocol.h"
#include "app_can_control.h"
#include "HMI/app_hmi_control.h"

typedef struct app_can_status_input
{
    servo_snapshot_t axis[2];
    remote_servo_status_t remote;
    app_can_control_status_t control;
    app_hmi_control_status_t hmi;
    app_control_stop_t stop;
    app_control_owner_t arbiter_owner;
    app_control_owner_t stop_source;
    app_control_lock_reason_t lock_reason;
    cli_servo_session_status_t cli;
    position_monitor_status_t position;
    servo_wdg_recovery_status_t watchdog[2];
    servo_recovery_status_t recovery;
    bsp_can_stats_t bus;
    debug_uart_stats_t uart;
    modbus_stats_t modbus;
    uint32_t now_ms, snapshot_ms, sample_tick, boot_id, upper_boot_id, config_revision;
    uint32_t snapshot_sequence;
    uint32_t diagnostic, reset_reason, failed_heartbeat_mask, nrf_generation;
    uint32_t rejected, telemetry_dropped, reassembly_failed;
    uint16_t heartbeat_ages[4], minimum_stack;
    uint16_t token;
    uint16_t hmi_remaining_ms;
    uint32_t arbiter_generation;
    uint32_t seen_stop_event, seen_can_stop_generation, seen_remote_stop_generation;
    uint32_t seen_boot_id;
    uint8_t arbiter_axes;
    uint8_t seen_remote_stop_state;
    uint8_t selected, owner, life, work, drive, stop_reason, gates, runtime;
    bool source_valid, can_link_ready;
} app_can_status_input_t;

void app_can_status_describe(app_can_status_input_t *in);
bool app_can_status_basic(const app_can_status_input_t *in,
    uint16_t id, uint8_t sequence, uint8_t data[8]);
bool app_can_status_record(const app_can_status_input_t *in,
    uint8_t type, can_protocol_record_t *record);

#endif
