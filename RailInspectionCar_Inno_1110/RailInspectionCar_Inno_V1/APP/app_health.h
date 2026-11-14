#ifndef APP_HEALTH_H
#define APP_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

/* 关键任务心跳，用于监测任务是否在规定时间内持续运行。 */
typedef enum
{
    /* 运动控制任务心跳。 */
    APP_HEARTBEAT_MOTION = 0,

    /* 伺服通信任务心跳。 */
    APP_HEARTBEAT_SERVO_COMM,

    /* 系统健康监控任务心跳。 */
    APP_HEARTBEAT_HEALTH,

    /* NRF24L01接收任务心跳。 */
    APP_HEARTBEAT_NRF,

    APP_HEARTBEAT_MODBUS,

    /* 心跳数量。 */
    APP_HEARTBEAT_COUNT

} app_heartbeat_t;

/* 诊断故障标志。 */
#define APP_DIAG_ASSERT_FAILED       (1UL << 0)
#define APP_DIAG_STACK_OVERFLOW      (1UL << 1)
#define APP_DIAG_LCD_NOT_READY       (1UL << 2)
#define APP_DIAG_LCD_SPI_ERROR       (1UL << 3)
#define APP_DIAG_RS485_INIT_ERROR    (1UL << 4)
#define APP_DIAG_MODBUS_INIT_ERROR   (1UL << 5)
#define APP_DIAG_TASK_CREATE_ERROR   (1UL << 6)
#define APP_DIAG_HEARTBEAT_TIMEOUT   (1UL << 7)
#define APP_DIAG_NRF_INIT_ERROR      (1UL << 8)
#define APP_DIAG_WDG_TEST_REQUESTED  (1UL << 9)
#define APP_DIAG_DRIVER_GUARD (1UL << 10)

/* Display-only warnings remain latched in telemetry, without blocking motion release. */
#define APP_DIAG_DISPLAY_WARNING_MASK (APP_DIAG_LCD_NOT_READY | APP_DIAG_LCD_SPI_ERROR)
static inline bool app_health_motion_diagnostic_fault(uint32_t diagnostic)
{
    /* Unknown/new diagnostic bits retain the existing conservative behavior. */
    return (diagnostic & ~(uint32_t)APP_DIAG_DISPLAY_WARNING_MASK) != 0U;
}

typedef struct {
    uint8_t phase, axis, step, pending, zero_mask, failed_task, parameters_mask, boot_locked;
    uint16_t firmware[2], no2_mode[2], comm_timeout[2];
    int16_t actual_rpm[2];
    uint32_t generation, last_progress_ms, elapsed_ms, failure;
} app_safety_status_t;
void app_health_get_safety_status(app_safety_status_t *status);
/* 人工确认仅在参数已核验、双轴新鲜零速、无驱动器故障时解除启动锁。 */
bool app_health_safety_acknowledge(void);
bool app_health_take_guard_stop(void);
/* 通信保护失败：立即禁止全部发送，由安全任务锁存并等待复位。 */
void app_health_request_driver_fallback(uint32_t reason);

/* 初始化系统健康监控模块。 */
void app_health_init(void);

/* 更新指定关键任务的心跳时间。 */
void app_health_beat(app_heartbeat_t heartbeat);

/* 判断所有关键任务是否仍在规定时间内运行。 */
bool app_health_critical_tasks_alive(void);

/* 设置系统诊断故障标志。 */
void app_health_set_diagnostic_fault(uint32_t fault_mask);

/* 读取当前累计的系统诊断故障标志。 */
uint32_t app_health_get_diagnostic_faults(void);
/* Cached driver evidence only; no SPI access in health/gate queries. */
typedef struct {
    uint32_t failure, config_checks, spi_errors, dma_timeouts, verify_failures;
    uint32_t config_mismatches, recoveries;
    uint8_t initialized, ready;
} app_health_nrf_input_t;
typedef struct {
    uint8_t initialized, ready, current_fault, recovered, known;
    uint32_t failure, failure_since_ms, recovery_since_ms;
    uint32_t updated_ms, config_checks, stable_checks, failure_count, recovery_count;
    uint32_t spi_errors, dma_timeouts, verify_failures, config_mismatches, recoveries;
} app_health_nrf_status_t;
void app_health_nrf_update(const app_health_nrf_input_t *input, uint32_t now_ms);
void app_health_get_nrf_status(app_health_nrf_status_t *status);

/* 请求停止喂狗，由IWDG任务等待真实硬件看门狗复位。 */
void app_health_request_watchdog_test(void);

/* 查询K2看门狗测试是否已请求。 */
bool app_health_watchdog_test_requested(void);

/* 查询K2看门狗测试是否已经进入停止喂狗阶段。 */
bool app_health_watchdog_test_active(void);

/* 执行一次心跳检查和喂狗决策，仅由IWDG任务调用。 */
void app_health_watchdog_service(void);

/* 读取最近一次硬件复位原因。 */
uint32_t app_health_get_reset_reason(void);

/* 读取最近一次失活的关键任务编号。 */
app_heartbeat_t app_health_get_failed_heartbeat(void);

/* 读取指定关键任务距离上次心跳的时间，单位为FreeRTOS Tick。 */
TickType_t app_health_get_heartbeat_age(app_heartbeat_t heartbeat);
/* 返回最近一次IWDG任务实际喂狗决策，65535表示尚未执行决策。 */
uint16_t app_health_get_watchdog_decision(void);

/* 记录configASSERT失败现场。 */
void app_health_assert_failed(const char *file, uint32_t line);

/* 记录FreeRTOS栈溢出现场。 */
void app_health_stack_overflow(const char *task_name);

#endif
