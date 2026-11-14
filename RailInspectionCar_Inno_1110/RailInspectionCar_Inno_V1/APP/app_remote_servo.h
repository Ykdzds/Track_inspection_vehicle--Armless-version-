#ifndef APP_REMOTE_SERVO_H
#define APP_REMOTE_SERVO_H

#include <stdbool.h>
#include <stdint.h>

#include "app_nrf_remote.h"
#include "aimtor_servo.h"
#include "app_remote_servo_sync.h"

typedef enum
{
    REMOTE_SERVO_DISABLED = 0,
    REMOTE_SERVO_ARMING,
    REMOTE_SERVO_ARMED,
    REMOTE_SERVO_RUNNING,
    REMOTE_SERVO_ESTOP,
    REMOTE_SERVO_STOPPING
} remote_servo_state_t;

typedef enum
{
    REMOTE_CONTROLLED_STOP_IDLE = 0,
    REMOTE_CONTROLLED_STOP_DECELERATING,
    REMOTE_CONTROLLED_STOP_VERIFYING,
    REMOTE_CONTROLLED_STOP_DISABLING,
    REMOTE_CONTROLLED_STOP_DONE,
    REMOTE_CONTROLLED_STOP_FAILED,
    REMOTE_CONTROLLED_STOP_HOLD_CONFIRMING
} remote_controlled_stop_state_t;

/* 保留最近急停原因，重新授权不会清除历史诊断。 */
typedef enum
{
    REMOTE_STOP_NONE = 0,
    REMOTE_STOP_K1, /* 保留历史枚举值，现由物理K5触发。 */
    REMOTE_STOP_LINK,
    REMOTE_STOP_MANUAL,
    REMOTE_STOP_ARMING,
    REMOTE_STOP_PREPARE,
    REMOTE_STOP_ENABLE,
    REMOTE_STOP_SERVO_STATE,
    REMOTE_STOP_TRANSACTION,
    REMOTE_STOP_SPEED_SUBMIT,
    REMOTE_STOP_SYNC_ERROR,
    REMOTE_STOP_REVERSE_TIMEOUT,
    REMOTE_STOP_CAN /* PB10: controlled zero/release, without physical K5 hold. */
} remote_stop_reason_t;

typedef enum
{
    REMOTE_COMM_OK = 0,
    REMOTE_COMM_DEGRADED,
    REMOTE_COMM_FAILED
} remote_comm_state_t;

typedef enum { REMOTE_SPEED_JOYSTICK = 0, REMOTE_SPEED_KEYS = 1 } remote_speed_mode_t;
typedef enum {
    REMOTE_SWITCH_OK = 0, REMOTE_SWITCH_NEUTRAL, REMOTE_SWITCH_WAIT_ZERO,
    REMOTE_SWITCH_INPUT, REMOTE_SWITCH_INACTIVE
} remote_switch_block_t;

typedef struct
{
    remote_speed_mode_t speed_mode;
    int16_t common_target_centi_kmh;
    uint8_t key_exit_pending, key_exit_ready;
    remote_switch_block_t switch_block;
    remote_servo_state_t state;
    uint8_t authorized;
    uint8_t hold_stopping; /**< 已撤销授权，正在受控减速或确认零速。 */
    uint8_t estop_latched;
    uint8_t link_valid;
    uint16_t lv;
    int16_t target_rpm;
    int16_t commanded_rpm;
    int16_t actual_rpm;
    uint8_t control_axis_mask;
    int16_t left_commanded_rpm;
    int16_t left_actual_rpm;
    int16_t right_commanded_rpm;
    int16_t right_actual_rpm;
    servo_result_t left_result;
    servo_result_t right_result;
    uint16_t left_fault;
    uint16_t right_fault;
    uint32_t left_speed_age_ms;
    uint32_t right_speed_age_ms;
    uint8_t left_verified;
    uint8_t right_verified;
    uint8_t left_enabled;
    uint8_t right_enabled;
    uint8_t left_busy;
    uint8_t right_busy;
    uint8_t left_latched;
    uint8_t right_latched;
    uint8_t batch_active; /**< 共同速度批次是否尚未完成。 */
    uint8_t batch_success_mask; /**< 已收到成功应答的轴掩码。 */
    uint8_t batch_required_mask;
    uint8_t batch_response_gap_valid;
    uint32_t batch_id;
    uint32_t authorization_generation;
    uint32_t batch_generation;
    uint32_t batch_sample_sequence[2];
    int16_t batch_base_rpm;
    int16_t batch_command[2];
    int16_t batch_correction;
    remote_sync_status_t sync;
    uint32_t batch_age_ms;
    uint32_t batch_response_gap_ms; /**< 最近完整批次两轴应答观察间隔。 */
    uint8_t stop_axis_mask; /**< 最近停车来源轴；无线或人工停车为0。 */
    uint32_t last_frame_age_ms;
    uint32_t arm_count;
    uint32_t estop_count;
    uint32_t link_loss_estop_count;
    servo_result_t last_result;
    remote_stop_reason_t stop_reason;
    uint32_t stop_link_age_ms;
    uint32_t stop_speed_age_ms;
    uint32_t stop_fault_age_ms;
    uint32_t stop_poll_result;
    uint16_t stop_poll_address;
    uint16_t stop_fault_code;
    servo_result_t stop_transaction_result;
    remote_comm_state_t comm_state;
    uint16_t comm_fail_count;
    uint16_t speed_submit_fail_count;
    uint32_t last_comm_ok_age_ms;
    remote_controlled_stop_state_t controlled_stop_state;
    remote_stop_reason_t controlled_stop_reason;
    float stop_deceleration_mps2;
    uint32_t stop_generation;
    uint32_t stop_elapsed_ms;
    uint32_t stop_timeout_ms;
    int16_t stop_batch_command[2];
    uint8_t stop_zero_mask;
    uint8_t stop_first_command_valid;
    uint8_t stop_zero_time_valid;
    uint8_t stop_complete_time_valid;
    uint32_t stop_first_command_ms;
    uint32_t stop_zero_ms;
    uint32_t stop_complete_ms;
    servo_k1_hold_status_t k1_hold;
} remote_servo_status_t;

/* Minimal read-only gate for the position observer; no axis snapshot copies. */
typedef struct
{
    uint32_t authorization_generation;
    uint8_t state, authorized, hold_stopping, estop_latched, control_axis_mask;
} remote_position_gate_t;

void app_remote_servo_init(void);
void app_remote_servo_recovery_reset(void);
void app_remote_servo_update(const nrf_remote_snapshot_t *remote);
void app_remote_servo_service(void);
void app_remote_servo_motion_service(void);
bool app_remote_servo_get_status(remote_servo_status_t *status);
bool app_remote_servo_get_position_gate(remote_position_gate_t *status);
bool app_remote_servo_is_active(void);
/* 状态灯只读取人工锁存，不在小栈任务中生成完整遥控快照。 */
bool app_remote_servo_k1_latched(void);
/* 人工停止只可撤销遥控授权，不能代替K5重新授权。 */
void app_remote_servo_request_estop(void);
uint8_t app_remote_servo_k5_verify_pending(void);
void app_remote_servo_can_k5_stop_begin(uint32_t event, uint8_t axes);
bool app_remote_servo_can_k5_stop_complete(uint32_t event, uint8_t axes);
const char *app_remote_servo_state_name(remote_servo_state_t state);
const char *app_remote_servo_stop_reason_name(remote_stop_reason_t reason);
const char *app_remote_servo_controlled_stop_state_name(remote_controlled_stop_state_t state);

void app_remote_servo_controlled_stop(uint8_t emergency);

#endif
