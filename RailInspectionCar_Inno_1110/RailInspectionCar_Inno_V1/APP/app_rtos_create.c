#include "app_hmi_link_diag.h"
#include "app_rtos_create.h"

#include "app_debug_cli.h"
#include "app_sync_params.h"
#include "app_control_arbiter.h"
#include "app_can_control.h"
#include "app_health.h"
#include "app_lcd_show.h"
#include "app_rtos_task.h"
#include "aimtor_servo.h"
#include "bsp_rs485.h"
#include "bsp_system.h"
#include "key.h"
#include "modbus_rtu.h"
#include "motion_control.h"
#include "app_nrf_remote.h"
#include "app_remote_servo.h"
#include "app_position_monitor.h"
#include "servo_debug_control.h"
#include "app_cli_servo_session.h"
#include "app_can.h"
#include "app_boot_identity.h"
#include "app_hmi.h"
#include "app_system_snapshot.h"

TaskHandle_t motion_task_handle;
TaskHandle_t servo_comm_task_handle;
TaskHandle_t modbus_task_handle;
TaskHandle_t health_task_handle;
TaskHandle_t key_task_handle;
TaskHandle_t status_task_handle;
TaskHandle_t cli_task_handle;
TaskHandle_t lcd_task_handle;
TaskHandle_t iwdg_task_handle;
TaskHandle_t nrf_task_handle;
TaskHandle_t can_task_handle;
TaskHandle_t hmi_task_handle;

/* 创建任务失败时在当前启动RAM中保存任务编号，不跨复位保留。 */
static void app_create_task_checked(TaskFunction_t task_function,
                                    const char *task_name,
                                    uint16_t stack_depth,
                                    UBaseType_t priority,
                                    TaskHandle_t *task_handle,
                                    uint32_t task_id)
{
    BaseType_t result;

    result = xTaskCreate(task_function, task_name, stack_depth, NULL,
                         priority, task_handle);
    if (result != pdPASS)
    {
        app_health_set_diagnostic_fault(APP_DIAG_TASK_CREATE_ERROR);
        bsp_system_store_fault(APP_DIAG_TASK_CREATE_ERROR, task_id);
    }
    configASSERT(result == pdPASS);
    bsp_system_watchdog_refresh();
}

void app_init(void)
{
    rs485_status_t rs485_result;
    modbus_status_t modbus_result;
    motion_init();
    app_health_init();
#if APP_CHASSIS_LCD_ENABLED
    lcd_init();
#endif
    key_init();
    app_sync_params_init();
    app_cli_init();

#if APP_CHASSIS_LCD_ENABLED
    if (!app_lcd_is_ready())
    {
        app_health_set_diagnostic_fault(APP_DIAG_LCD_NOT_READY);
    }
#endif

    /* 通信初始化失败时记录诊断，系统继续以SAFE状态启动。 */
    rs485_result=rs485_init();
    if (rs485_result != RS485_OK)
    {
        app_health_set_diagnostic_fault(APP_DIAG_RS485_INIT_ERROR);
    }
    modbus_result=modbus_init();
    if (modbus_result != MODBUS_OK)
    {
        app_health_set_diagnostic_fault(APP_DIAG_MODBUS_INIT_ERROR);
    }
    /* IWDG任务优先级最高，首次调度先安装安全启动策略。 */
    aimtor_init();
    /* This is the system authorization gate, not an interactive CLI feature. */
    aimtor_cli_gate_init();
    /* 看门狗或跨复位安全事件由独立安全入口接管；禁止旧启动流程失能/改模式。 */
    aimtor_watchdog_recovery_init(0U);
    if (!(app_health_get_reset_reason() & ((1UL << 29U) | (1UL << 30U))) &&
        !bsp_system_safety_state())
    {
        aimtor_startup_stop_init();
        aimtor_boot_zero_init(app_health_get_reset_reason());
        aimtor_startup_hold_init();
    }
    aimtor_cold_clear_init(app_health_get_reset_reason(), bsp_system_safety_state());
    servo_debug_init();
    app_control_arbiter_init();
    app_cli_servo_session_init();
    app_nrf_remote_init();
    app_remote_servo_init();
    app_position_monitor_init();
    app_boot_identity_init();
    bsp_system_fault_set_boot(app_boot_identity_id());
    app_hmi_link_diag_init();
    /* Initialization happened before boot identity/logger existed: replay its
     * actual results once, without running either initialization again. */
    app_link_diag_record(APP_LINK_HMI,0x3F0U,rs485_result,modbus_result,0U,0U,0U,0U);
    app_link_diag_record(APP_LINK_REMOTE,0x3F0U,rs485_result,modbus_result,0U,0U,0U,0U);
    app_link_diag_record(APP_LINK_CAN_SERVO,0x3F0U,rs485_result,modbus_result,0U,0U,0U,0U);
    app_link_diag_record(APP_LINK_CAN_ARM,0x3F0U,rs485_result,modbus_result,0U,0U,0U,0U);
    app_link_diag_record(APP_LINK_RS485,0x3F0U,rs485_result,modbus_result,0U,0U,0U,0U);
    app_can_control_init(app_boot_identity_get());
    app_can_init();
    app_system_snapshot_init();
    /* HMI仲裁和UART5接收必须在创建任务前完成，避免启动竞态。 */
    app_hmi_init();
}

void freertos_taskstart(void)
{
    /* 扩展双轴快照与位置诊断后保留栈余量，单位为32位字。 */
    app_create_task_checked(motion_task, "motion", 768U, 6U,
                            &motion_task_handle, 1U);
    /* K1停车及重新授权含嵌套双轴快照，为上下文保存和后续调用保留栈余量。 */
    app_create_task_checked(servo_comm_task, "servo_comm", 768U, 5U,
                            &servo_comm_task_handle, 2U);
    app_create_task_checked(health_task, "health", 512U, 4U,
                            &health_task_handle, 3U);
    app_create_task_checked(key_task, "key", 224U, 3U,
                            &key_task_handle, 4U);
    app_create_task_checked(status_task, "status", 192U, 2U,
                            &status_task_handle, 5U);
    /* Ramp readback diagnostics enlarge nested servo snapshots: keep >=256B margin. */
#if APP_UART_RX_ACTIVE
    app_create_task_checked(cli_task, "cli", 832U, 2U,
#else
    app_create_task_checked(cli_task, "diag", 256U, 2U,
#endif
                            &cli_task_handle, 6U);
#if APP_CHASSIS_LCD_ENABLED
    /* Field LCD watermark reached 244 B: add 64 words without a new task. */
    app_create_task_checked(lcd_task, "lcd", 960U, 1U,
                            &lcd_task_handle, 7U);
#endif
    app_create_task_checked(iwdg_task, "iwdg", 768U, 7U,
                            &iwdg_task_handle, 8U);
    /* 回传采集会读取双轴大快照，增加栈余量；不提高无线任务优先级。 */
    app_create_task_checked(nrf_task, "nrf", 768U, 6U,
                            &nrf_task_handle, 9U);
    app_create_task_checked(modbus_bus_task, "modbus_bus", 768U, 6U,
                            &modbus_task_handle, 11U);
    app_can_task_create();
    app_create_task_checked(hmi_task, "hmi", 768U, 3U,
                            &hmi_task_handle, 10U);
}
