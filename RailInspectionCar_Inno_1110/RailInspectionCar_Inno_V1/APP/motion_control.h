#ifndef MOTION_CONTROL_H
#define MOTION_CONTROL_H

#include <stdint.h>

/**
 * 底盘运动状态。
 * 状态由运动控制和健康监控共同维护，运动命令必须在允许的状态下执行。
 */
typedef enum
{
    MOTION_BOOT = 0,       /**< 运动模块刚初始化，尚未完成安全状态确认。 */
    MOTION_SAFE,           /**< 安全状态，禁止输出有效运动脉冲。 */
    MOTION_READY,          /**< 就绪状态，允许接受正常运动命令。 */
    MOTION_RUNNING,        /**< 运行状态，正在执行速度或距离运动。 */
    MOTION_STOPPING,       /**< 停止状态，正在按设定减速度减速至零。 */
    MOTION_FAULT,          /**< 故障状态，检测到控制故障。 */
    MOTION_OFFLINE         /**< 离线状态，伺服或通信连续失败。 */
} motion_state_t;

/**
 * 运动控制接口返回结果。
 */
typedef enum
{
    MOTION_OK = 0,              /**< 操作成功或命令已接受。 */
    MOTION_INVALID_ARGUMENT,    /**< 输入参数超出范围或不合法。 */
    MOTION_NOT_CONFIGURED,      /**< 必要硬件配置或有效电平尚未确认。 */
    MOTION_NOT_READY,           /**< 当前状态不允许执行该命令。 */
    MOTION_FAULTED              /**< 系统处于故障或离线状态。 */
} motion_result_t;

/**
 * 底盘停止方式。
 */
typedef enum
{
    MOTION_STOP_CONTROLLED = 0, /**< 按加速度平滑减速，适用于正常停车。 */
    MOTION_STOP_IMMEDIATE       /**< 立即关闭PWM并清零命令速度。 */
} motion_stop_mode_t;

/* 软件安全停车原因，用于区分短暂通信异常和必须立即停机的危险故障。 */
typedef enum
{
    MOTION_FAULT_REASON_NONE = 0,
    MOTION_FAULT_REASON_FEEDBACK_STALE,
    MOTION_FAULT_REASON_SERVO_OFFLINE,
    MOTION_FAULT_REASON_SERVO_FAULT,
    MOTION_FAULT_REASON_OUTPUT_FAILURE,
    MOTION_FAULT_REASON_STOP_TIMEOUT,
    MOTION_FAULT_REASON_UNSUPPORTED_MODE,
    MOTION_FAULT_REASON_HEARTBEAT_TIMEOUT,
    MOTION_FAULT_REASON_WATCHDOG_RESET
} motion_fault_reason_t;

typedef enum
{
    MOTION_STOP_CLASS_NONE = 0,
    MOTION_STOP_CLASS_NORMAL,
    MOTION_STOP_CLASS_CONTROLLED_SAFETY,
    MOTION_STOP_CLASS_IMMEDIATE_SAFETY
} motion_stop_class_t;

/** 初始化运动控制模块，绑定脉冲输出并将状态置为安全状态。 */
void motion_init(void);
/** 设置左右轴目标速度，单位为m/s；实际输出由运动周期任务逐步跟踪。 */
motion_result_t motion_set_velocity(float left_mps, float right_mps);
/** 按指定距离运动，参数单位分别为m、m/s和m/s²。 */
motion_result_t motion_move_distance(float distance_m,
                                     float max_velocity_mps,
                                     float acceleration_mps2);
/** 请求停止运动；可选择受控减速停止或立即关闭脉冲输出。 */
motion_result_t motion_stop(motion_stop_mode_t mode);
/** 获取当前底盘运动状态。 */
motion_state_t motion_get_state(void);
/** 执行一次固定周期的加减速规划、脉冲输出和命令位置更新。 */
void motion_update(void);
/** 设置系统是否满足进入运动就绪状态的外部条件。 */
void motion_set_system_ready(uint8_t ready);
/** 上报运动故障或通信离线故障，并触发双轴联停处理。 */
void motion_report_fault(uint8_t offline);
void motion_request_controlled_safety_stop(motion_fault_reason_t reason, uint8_t offline);
void motion_request_immediate_safety_stop(motion_fault_reason_t reason, uint8_t offline);
void motion_complete_controlled_safety_stop(uint8_t offline);
motion_stop_class_t motion_get_stop_class(void);
motion_fault_reason_t motion_get_fault_reason(void);
/** 仅由设备适配层在人工确认静止、失能及通信恢复后解除锁存。 */
void motion_clear_fault(void);
/** 获取指定轴当前经过规划后的命令速度，单位为m/s。 */
float motion_get_command_velocity(uint8_t axis);
/** 启动指定轴按指定方向、频率和脉冲数运行的单轴测试。 */
motion_result_t motion_start_pulse(uint8_t axis,
                                   uint8_t forward,
                                   uint32_t frequency_hz,
                                   uint32_t pulses);
/** 启动指定轴掩码的同步定脉冲测试。 */
motion_result_t motion_start_pulse_mask(uint8_t axis_mask,
                                         uint8_t forward,
                                         uint32_t frequency_hz,
                                         uint32_t pulses);
/** 兼容旧接口：启动左轴定脉冲测试。 */
motion_result_t motion_start_left_pulse(uint8_t forward,
                                        uint32_t frequency_hz,
                                        uint32_t pulses);
/** 返回指定轴定脉冲测试是否仍处于活动状态。 */
uint8_t motion_get_pulse_active(uint8_t axis);
/** 返回指定轴掩码中是否仍有定脉冲测试活动。 */
uint8_t motion_get_pulse_active_mask(uint8_t axis_mask);
/** 返回已经数完脉冲、正在等待驱动器反馈归零的轴掩码。 */
uint8_t motion_get_pulse_completion_pending_mask(uint8_t axis_mask);
/** 完成指定轴的反馈归零确认，并释放定脉冲完成状态。 */
motion_result_t motion_complete_pulse(uint8_t axis_mask);
/** 兼容旧接口：返回左轴定脉冲测试是否仍处于活动状态。 */
uint8_t motion_get_left_pulse_active(void);

#endif
