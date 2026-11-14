#ifndef AIMTOR_SERVO_H
#define AIMTOR_SERVO_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "app_link_diag.h"

typedef enum
{
    SERVO_AXIS_LEFT = 0,
    SERVO_AXIS_RIGHT,
    SERVO_AXIS_COUNT
} servo_axis_t;

enum {
    AIMTOR_STOP_SOURCE_UNKNOWN = 0, AIMTOR_STOP_SOURCE_STARTUP = 1,
    AIMTOR_STOP_SOURCE_BOARD_KEY = 2, AIMTOR_STOP_SOURCE_REMOTE = 3,
    AIMTOR_STOP_SOURCE_HMI = 4, AIMTOR_STOP_SOURCE_CAN = 5,
    AIMTOR_STOP_SOURCE_HEALTH = 6, AIMTOR_STOP_SOURCE_DRIVER = 7
};
#if APP_LINK_DIAG_ACTIVE
void aimtor_diag_startup_stop(uint8_t source, uint8_t axis, uint16_t reason, uint32_t detail);
#else
static inline void aimtor_diag_startup_stop(uint8_t source, uint8_t axis, uint16_t reason, uint32_t detail)
{ (void)source; (void)axis; (void)reason; (void)detail; }
#endif

typedef enum
{
    SERVO_CONTROL_NONE = 0, /* 未配置模式 */
    SERVO_CONTROL_PULSE_POSITION, /* 脉冲位置模式 */
    SERVO_CONTROL_RS485_VELOCITY, /* RS485速度模式 */
    SERVO_CONTROL_RS485_SEGMENT_POSITION /* RS485多段位置模式 */
} servo_control_mode_t;

typedef enum
{
    SERVO_OK = 0,
    SERVO_NOT_CONFIGURED,
    SERVO_INVALID_ARGUMENT,
    SERVO_QUEUE_ERROR,
    SERVO_NOT_READY,
    SERVO_BUSY,
    SERVO_UNSUPPORTED
} servo_result_t;

/* 在有效速度应答处理时记录，供双轴配对读取最近四个样本。 */
#define SERVO_SPEED_SAMPLE_CAPACITY (4U)
#define SERVO_DIAGNOSTIC_TORQUE_VALID       (1U << 0U)
#define SERVO_DIAGNOSTIC_LOAD_VALID         (1U << 1U)
#define SERVO_DIAGNOSTIC_CURRENT_VALID      (1U << 2U)
#define SERVO_DIAGNOSTIC_BUS_VOLTAGE_VALID  (1U << 3U)
typedef struct
{
    int16_t actual_speed_rpm;
    uint32_t speed_sequence;
    TickType_t sample_tick;
    uint8_t valid;
} servo_speed_sample_t;

/* 有效H0B_07应答的原始记录，时间戳不是显示刷新时间。 */
#define SERVO_POSITION_SAMPLE_CAPACITY (4U)
typedef struct
{
    int32_t actual_position;
    uint32_t position_sequence;
    uint32_t position_generation;
    TickType_t sample_tick;
    uint8_t valid;
} servo_position_sample_t;

/* Position observer projection. No diagnostic/command payload and no I/O. */
typedef struct
{
    servo_position_sample_t position_samples[SERVO_POSITION_SAMPLE_CAPACITY];
    uint32_t position_age_ms, position_sequence, position_generation, speed_age_ms;
    int16_t actual_speed_rpm;
    uint8_t online, fault, parameter_verified, control_mode;
    uint8_t speed_valid, position_valid, busy;
} servo_position_monitor_axis_t;

typedef struct
{
    servo_speed_sample_t speed_samples[SERVO_SPEED_SAMPLE_CAPACITY];
    servo_position_sample_t position_samples[SERVO_POSITION_SAMPLE_CAPACITY];
    uint8_t online;
    uint8_t enabled;
    /* 只读显示证据：与控制器软件使能状态分离，不新增查询。 */
    uint8_t enable_feedback_valid, enable_feedback;
    TickType_t enable_feedback_tick;
    uint32_t enable_feedback_age_ms;
    uint8_t fault;
    uint8_t parameter_verified;
    servo_control_mode_t control_mode;
    uint16_t status_word;
    uint16_t fault_code;
    int16_t actual_speed_rpm;
    float actual_velocity_mps;
    int32_t actual_position;
    uint32_t position_age_ms;
    uint32_t position_sequence;
    uint32_t position_generation;
    TickType_t position_sample_tick;
    uint8_t position_valid;
    uint32_t speed_sequence;
    TickType_t speed_sample_tick; /**< 实际速度应答接收时刻，单位为系统节拍。 */
    /* 历史预留字段不作为本次四项诊断的采样依据。 */
    int32_t position_deviation;
    uint16_t do_state;
    uint16_t bus_voltage;
    int16_t temperature;
    uint16_t load_ratio;
    /* 四项诊断采用定点缓存，逐字段有效位和陈旧位彼此独立。 */
    int16_t realtime_torque_tenths_percent;
    uint16_t load_ratio_tenths_percent;
    int16_t phase_current_deciamp;
    uint16_t bus_voltage_decivolt;
    uint8_t diagnostic_valid_mask;
    uint8_t diagnostic_stale_mask;
    TickType_t diagnostic_sample_tick[4];
    uint32_t diagnostic_update_tick;
    uint32_t diagnostic_poll_failures;
    uint16_t consecutive_failures;
    uint8_t feedback_failures[3];
    uint8_t feedback_degraded;
    uint32_t update_tick;
    uint32_t speed_age_ms;
    uint32_t fault_age_ms;
    uint32_t poll_successes;
    uint32_t poll_failures;
    uint32_t last_poll_result;
    uint16_t last_poll_address;
    uint8_t query_selected;
    uint8_t speed_valid;
    uint8_t fault_valid;
    uint8_t probe_pending;
    uint8_t busy;             /**< 异步配置或写后回读尚未结束。 */
    int16_t speed_write_rpm; /* actual completed value after device health limiting */
    uint8_t speed_write_rpm_valid;
    uint32_t speed_write_tick; /* RTOS uptime ms: last acknowledged speed write */
    /* Cached H06_05/H06_06 readback; read/match masks: accel=1, decel=2.
     * Values are evidence at ramp_readback_tick, not periodic live polling. */
    uint16_t ramp_requested_ms[2], ramp_readback_ms[2];
    uint8_t ramp_read_mask, ramp_match_mask;
    uint32_t ramp_readback_tick[2];
    uint32_t speed_write_sequence; /**< 速度写事务序号。 */
    uint32_t speed_write_completed; /**< 最近完成的速度写事务序号。 */
    servo_result_t speed_write_result; /**< 最近完成的速度写事务结果。 */
    uint8_t zero_attempted;   /**< 本次MCU启动已尝试设零，失败不自动重试。 */
    uint8_t zero_verified;    /**< 设零后读取指令位置确认为零。 */
    uint8_t stop_latched;     /**< 联停锁存，需人工确认后重新配置。 */
    uint8_t disable_verified; /**< 最近失能指令已回读，仍须硬件确认真实使能状态。 */
    servo_result_t last_result; /**< 最近异步操作结果，成功不等于运动到位。 */
} servo_snapshot_t;

typedef enum
{
    SERVO_HEALTH_NORMAL = 0, SERVO_HEALTH_DEGRADED, SERVO_HEALTH_STOP
} servo_health_level_t;
typedef struct
{
    uint32_t valid_tick, valid_sequence, transaction, result, first_failure_tick, age_ms;
    uint16_t address;
    uint8_t failures, valid;
    servo_health_level_t level;
} servo_feedback_health_t;
typedef struct
{
    servo_feedback_health_t item[3]; /* speed, fault, enable */
    servo_health_level_t level;
} servo_axis_health_t;
bool aimtor_get_health(servo_axis_t axis, servo_axis_health_t *health);
void aimtor_recovery_isolate(void); /* servo task: after all source queues are cleared */

typedef enum
{
    SERVO_RECOVERY_NONE = 0, SERVO_RECOVERY_WAIT_HEALTH,
    SERVO_RECOVERY_READY, SERVO_RECOVERY_REQUESTED,
    SERVO_RECOVERY_PREPARING, SERVO_RECOVERY_COMMIT,
    SERVO_RECOVERY_DONE, SERVO_RECOVERY_FAILED
} servo_recovery_state_t;

typedef struct
{
    servo_recovery_state_t state;
    uint32_t generation, healthy_ms;
    uint8_t source, failure, eligible;
    uint8_t first_axis, first_item, first_failures;
    uint32_t first_transaction, first_result, first_age_ms, first_failure_ms;
    uint16_t first_address;
} servo_recovery_status_t;

void aimtor_get_recovery_status(servo_recovery_status_t *status);
const char *aimtor_recovery_state_name(servo_recovery_state_t state);
servo_result_t aimtor_request_recovery(uint8_t source);
bool aimtor_recovery_blocks_motion(void);
void aimtor_recovery_commit(bool success);

static inline uint8_t aimtor_feedback_pair_degraded(const servo_snapshot_t *left,
    const servo_snapshot_t *right, uint8_t mask)
{
    return (uint8_t)(((mask & 1U) && left->feedback_degraded) ||
                     ((mask & 2U) && right->feedback_degraded));
}

static inline int16_t aimtor_feedback_limit_rpm(uint8_t degraded, int16_t requested,
                                               int16_t confirmed)
{
    if (!degraded) return requested;
    if (!confirmed || (confirmed > 0 && requested < 0) ||
        (confirmed < 0 && requested > 0)) return 0;
    if ((confirmed > 0 && requested > confirmed) ||
        (confirmed < 0 && requested < confirmed)) return confirmed;
    return requested;
}

void aimtor_init(void);
typedef enum {
    SERVO_COLD_CLEAR_SKIPPED = 0, SERVO_COLD_CLEAR_OBSERVING,
    SERVO_COLD_CLEAR_DISABLING, SERVO_COLD_CLEAR_CLEARING,
    SERVO_COLD_CLEAR_VERIFYING, SERVO_COLD_CLEAR_DONE, SERVO_COLD_CLEAR_FAILED
} servo_cold_clear_state_t;
typedef enum {
    SERVO_COLD_FAIL_NONE = 0, SERVO_COLD_FAIL_ONLINE_TIMEOUT,
    SERVO_COLD_FAIL_DEADLINE, SERVO_COLD_FAIL_MOVING, SERVO_COLD_FAIL_OTHER_FAULT,
    SERVO_COLD_FAIL_FEEDBACK, SERVO_COLD_FAIL_DISABLE, SERVO_COLD_FAIL_CLEAR,
    SERVO_COLD_FAIL_REAPPEARED, SERVO_COLD_FAIL_SAFETY
} servo_cold_clear_failure_t;
typedef struct {
    servo_cold_clear_state_t state;
    servo_cold_clear_failure_t failure;
    uint16_t original_fault, readback_fault;
    uint8_t eligible, attempted, write_ack, disable_attempted, zero_samples;
    uint32_t elapsed_ms, last_result;
} servo_cold_clear_status_t;
/* One-shot initialization, before scheduling business tasks. */
void aimtor_cold_clear_init(uint32_t reset_flags, uint32_t retained_safety);
bool aimtor_cold_clear_ready(void);
bool aimtor_cold_clear_get_status(servo_axis_t axis, servo_cold_clear_status_t *status);
bool aimtor_cold_clear_suppresses_fault(servo_axis_t axis);
typedef enum
{
    SERVO_STARTUP_STOP_SKIPPED = 0,
    SERVO_STARTUP_STOP_WAITING,
    SERVO_STARTUP_STOP_DISABLING,
    SERVO_STARTUP_STOP_WAITING_ZERO,
    SERVO_STARTUP_STOP_CONFIGURING,
    SERVO_STARTUP_STOP_VERIFYING,
    SERVO_STARTUP_STOP_DONE,
    SERVO_STARTUP_STOP_FAILED
} servo_startup_stop_state_t;

typedef struct
{
    servo_startup_stop_state_t state;
    uint8_t eligible;
    uint8_t attempts;
    uint8_t disable_ack;
    uint8_t off_confirmed;
    uint8_t zero_confirmed;
    /* 1写失能失败，2失能确认失败，3停稳超时，4配置失败，5驱动器故障。 */
    uint8_t failure;
} servo_startup_stop_status_t;

typedef enum
{
    SERVO_STARTUP_HOLD_DISABLED = 0,
    SERVO_STARTUP_HOLD_WAITING,
    SERVO_STARTUP_HOLD_PREPARING,
    SERVO_STARTUP_HOLD_WRITING_ZERO,
    SERVO_STARTUP_HOLD_ENABLING,
    SERVO_STARTUP_HOLD_VERIFYING,
    SERVO_STARTUP_HOLD_READY,
    SERVO_STARTUP_HOLD_FAILED
} servo_startup_hold_state_t;

typedef struct
{
    servo_startup_hold_state_t state;
    uint8_t eligible;
    uint8_t zero_written;
    uint8_t enable_ack;
    uint8_t verified;
    /* 外部扰动只维持零速给定，不撤销使能；授权仍需实际零速。 */
    uint8_t disturbed;
    /* 1准备失败，2写零失败，3使能失败，4反馈确认失败，5安全退出。 */
    uint8_t failure;
} servo_startup_hold_status_t;

/* 历史k1接口名称保留，实际由物理K5触发。 */
/* 运行期K5保持与上电待机资格独立，不执行使能或模式配置。 */
typedef enum
{
    SERVO_K1_HOLD_INACTIVE = 0,
    SERVO_K1_HOLD_CONFIRMING,
    SERVO_K1_HOLD_READY,
    SERVO_K1_HOLD_FAILED
} servo_k1_hold_state_t;

typedef struct
{
    servo_k1_hold_state_t state;
    uint8_t axis_mask;
    uint8_t confirmed_mask;
    uint8_t enabled_mask;
    uint8_t zero_mask;
    uint8_t valid;
    uint8_t disturbed;
    /* 1反馈/模式异常，2使能回读异常，3补写零失败，4安全退出。 */
    uint8_t failure;
} servo_k1_hold_status_t;

servo_result_t aimtor_k1_hold_request(uint8_t axis_mask);
bool aimtor_k1_hold_get_status(servo_k1_hold_status_t *status);
/* 仅在新长按门禁通过后安全释放；本接口不授予运动权限。 */
servo_result_t aimtor_k1_hold_release(void);

typedef enum
{
    SERVO_WDG_RECOVERY_SKIPPED = 0,
    SERVO_WDG_RECOVERY_WAITING,
    SERVO_WDG_RECOVERY_STOP_LOCAL,
    SERVO_WDG_RECOVERY_WAIT_ONLINE,
    SERVO_WDG_RECOVERY_WRITE_DECEL,
    SERVO_WDG_RECOVERY_VERIFY_DECEL,
    SERVO_WDG_RECOVERY_WRITE_ZERO,
    SERVO_WDG_RECOVERY_DECELERATING,
    SERVO_WDG_RECOVERY_CONFIRM_ZERO,
    SERVO_WDG_RECOVERY_PREPARE_SPEED,
    SERVO_WDG_RECOVERY_DISABLE,
    SERVO_WDG_RECOVERY_VERIFY,
    SERVO_WDG_RECOVERY_DONE,
    SERVO_WDG_RECOVERY_FAILED
} servo_wdg_recovery_state_t;

typedef struct
{
    servo_wdg_recovery_state_t state;
    uint8_t eligible;
    uint8_t attempts;
    uint8_t zero_written;
    uint8_t disable_ack;
    uint8_t verified;
    uint8_t ramp_applicable;
    uint8_t ramp_verified;
    uint8_t stopped;
    uint8_t zero_samples;
    uint16_t decel_readback_ms;
    int16_t start_speed_rpm;
    int16_t command_readback_rpm;
    uint16_t enable_readback;
    uint16_t mode_readback;
    uint16_t selection_readback;
    uint16_t source_readback;
    uint32_t stop_timeout_ms;
    uint32_t stop_elapsed_ms;
    /* 1准备超时，2故障，3配置门禁，4写失败，5减速核验，6通信，7停车超时，8人工急停。 */
    uint16_t failure_reason;
} servo_wdg_recovery_status_t;

/* 启动停车自身保持失能；后续统一待机流程另行使能。 */
void aimtor_startup_stop_init(void);
bool aimtor_startup_stop_ready(void);
bool aimtor_startup_stop_get_status(servo_axis_t axis,
                                  servo_startup_stop_status_t *status);
const char *aimtor_startup_stop_state_name(servo_startup_stop_state_t state);
bool aimtor_startup_hold_ready(void);
bool aimtor_startup_hold_active(void);
/* 各类复位安全流程成功后均可待机；与仅POR设零独立初始化。 */
void aimtor_startup_hold_init(void);
/* 仅人工K5急停允许长按后重建待机，真实故障不可绕过。 */
void aimtor_emergency_stop_k1(void);
servo_result_t aimtor_startup_hold_request_rearm(void);
bool aimtor_startup_hold_get_status(
    servo_axis_t axis, servo_startup_hold_status_t *status);
const char *aimtor_startup_hold_state_name(servo_startup_hold_state_t state);
/* 独立于伺服使能状态的遥控授权；CLI使用独立的单轴会话权限。 */
void aimtor_set_motion_authorized(bool authorized);
bool aimtor_motion_allowed(void);
/* CLI会话仅由通信任务推进，内部权限不得跨服务调用保留。 */
void aimtor_cli_gate_init(void);
servo_result_t aimtor_remote_prepare_mode(servo_axis_t axis);
servo_result_t aimtor_remote_prepare_enable(servo_axis_t axis);
void aimtor_cli_take_axis(servo_axis_t axis);
void aimtor_cli_release_axis(void);
void aimtor_cli_authorize(bool authorized);
void aimtor_cli_internal(bool active);
bool aimtor_cli_control_active(void);
bool aimtor_cli_axis_owned(servo_axis_t axis);
void aimtor_cli_cancel_actions(void);
void aimtor_cli_request_abort(uint8_t reason);
uint8_t aimtor_cli_take_abort(void);
servo_result_t aimtor_cli_restore_hold(void);
bool aimtor_cli_restoring_hold(void);
/* 撤权受控停车期间，仅允许专用停车接口写入递减速度。 */
void aimtor_set_remote_stop_active(bool active);
void aimtor_watchdog_recovery_init(uint8_t is_watchdog_reset);
bool aimtor_watchdog_recovery_ready(void);
bool aimtor_watchdog_recovery_get_status(
    servo_axis_t axis, servo_wdg_recovery_status_t *status);
const char *aimtor_watchdog_recovery_state_name(
    servo_wdg_recovery_state_t state);
typedef enum
{
    SERVO_BOOT_ZERO_SKIPPED = 0,
    SERVO_BOOT_ZERO_WAITING,
    SERVO_BOOT_ZERO_PREPARING,
    SERVO_BOOT_ZERO_WRITING,
    SERVO_BOOT_ZERO_VERIFYING,
    SERVO_BOOT_ZERO_DONE,
    SERVO_BOOT_ZERO_FAILED
} servo_boot_zero_state_t;

typedef struct
{
    servo_boot_zero_state_t state;
    uint8_t eligible;
    uint8_t attempts;
    uint8_t write_ack;
    /* 1为准备失败，2为写入失败，3为位置确认失败，4为安全状态异常。 */
    uint8_t failure;
} servo_boot_zero_status_t;

/* 使用已保存的启动原因，仅POR启动建立设零资格。 */
void aimtor_boot_zero_init(uint32_t reset_reason);
bool aimtor_boot_zero_ready(void);
bool aimtor_boot_zero_get_status(servo_axis_t axis,
                                servo_boot_zero_status_t *status);
const char *aimtor_boot_zero_state_name(servo_boot_zero_state_t state);
void aimtor_service(void);
servo_result_t aimtor_enable(servo_axis_t axis);
servo_result_t aimtor_disable(servo_axis_t axis);
servo_result_t aimtor_stop(servo_axis_t axis);
/** 普通双轴停止，根据当前模式分派，任何停止失败升级为联停。 */
void aimtor_stop_all(void);
servo_result_t aimtor_reset_fault(servo_axis_t axis);
servo_result_t aimtor_request_status(servo_axis_t axis);
/* 提前安排一次编码器绝对位置读取。 */
servo_result_t aimtor_request_position(servo_axis_t axis);
servo_result_t aimtor_request_parameter_verify(servo_axis_t axis);
servo_result_t aimtor_set_control_mode(servo_axis_t axis,
                                       servo_control_mode_t mode);
servo_result_t aimtor_set_maintenance_enabled(bool enabled);
bool aimtor_maintenance_enabled(void);
servo_result_t aimtor_set_velocity_rpm(servo_axis_t axis, int16_t rpm);
/* 撤销遥控授权后的专用减速通道，只允许向零收敛。 */
servo_result_t aimtor_set_stop_velocity_rpm(servo_axis_t axis, int16_t rpm);
servo_result_t aimtor_run_segment(servo_axis_t axis,
                                  int32_t distance,
                                  uint16_t speed,
                                  uint16_t acceleration);
/** 位置反馈确认到达且实际速度为零后，释放本次内部位置触发锁存。 */
servo_result_t aimtor_complete_segment(servo_axis_t axis);
servo_result_t aimtor_save_parameters(servo_axis_t axis);
/** 已配置位置模式、失能且速度为零时，每次MCU启动最多尝试一次设零。 */
servo_result_t aimtor_zero_once(servo_axis_t axis);
typedef struct
{
    uint32_t started_ms, completed_ms, timeout_ms;
    uint32_t speed_age_ms[2], fault_age_ms[2];
    uint32_t last_modbus_result[2];
    uint16_t last_address[2], failures[2], fault_code[2];
    int16_t actual_rpm[2], command_rpm[2];
    uint8_t mode[2], source, axis_mask;
    uint8_t active, completed, escalated, write_failures[2];
    uint8_t initial_reason, final_reason;
} servo_safety_stop_status_t;
void aimtor_request_controlled_safety_stop(uint8_t offline);
void aimtor_controlled_stop_service(void);
bool aimtor_get_safety_stop_status(servo_safety_stop_status_t *status);
bool aimtor_safety_stop_active(void);
void aimtor_record_immediate_safety_stop(uint8_t reason);
/** 双轴撤销许可并尽力发送失能，链路断开时不保证驱动器收到。 */
void aimtor_emergency_stop(void);
/** 静止且通信恢复后人工解锁，不自动使能或恢复旧目标。 */
servo_result_t aimtor_clear_stop_latch(void);
/** 标记哪些轴由遥控速度控制接管，供底层区分瞬时通信抖动。 */
void aimtor_set_remote_control_mask(uint8_t mask);
/* 兼容旧调用，true表示接管当前配置的全部控制轴。 */
void aimtor_set_remote_control_active(bool active);
uint8_t aimtor_get_remote_control_mask(void);
bool aimtor_remote_control_active(void);
/* 车载屏控制只通过会话模块调用，底层不提供越权接口。 */
void aimtor_set_hmi_control(bool authorized, uint8_t mask);
bool aimtor_hmi_control_active(void);
/* CAN运动授权与HMI、遥控器权限独立。 */
void aimtor_set_can_control(bool authorized, uint8_t mask);
bool aimtor_can_control_active(void);
void aimtor_can_revoke(uint32_t generation);
servo_result_t aimtor_can_velocity(servo_axis_t axis, int16_t rpm,
                                  uint32_t generation, uint32_t batch, bool stop);
bool aimtor_get_snapshot(servo_axis_t axis, servo_snapshot_t *snapshot);
bool aimtor_get_snapshot_pair(servo_snapshot_t *left, servo_snapshot_t *right);
bool aimtor_get_position_monitor_pair(servo_position_monitor_axis_t *left,
                                     servo_position_monitor_axis_t *right);
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
/* Compact read-only projection; no hardware query or full snapshot copies. */
void aimtor_rs485_diag_snapshot(app_link_diag_snapshot_t *out);
/* Called once per existing servo task iteration after response consumption. */
void aimtor_rs485_diag_observe(void);
#endif
#define SERVO_POLL_CLASSES 9U
/* speed, fault, enable, position, state, torque, load, current, voltage. */
typedef struct {
    uint32_t submitted, succeeded, failed, last_result;
    uint32_t max_wait_ms, age_ms;
} servo_poll_status_t;
bool aimtor_get_poll_status(servo_axis_t axis, uint8_t item, servo_poll_status_t *out);
bool aimtor_register_map_valid(void);
void aimtor_handle_modbus_response(void);
uint8_t aimtor_get_slave_address(servo_axis_t axis);

#endif
