#include "app_rtos_task.h"
#include "app_system_snapshot.h"
#include "app_runtime_metrics.h"

#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "app_debug_cli.h"
#include "app_sync_params.h"
#include "app_health.h"
#include "app_lcd_show.h"
#include "app_nrf_remote.h"
#include "app_remote_servo.h"
#include "app_position_monitor.h"
#include "NRF24L01.h"
#include "aimtor_servo.h"
#include "beep.h"
#include "bsp_system.h"
#include "bsp_rs485.h"
#include "key.h"
#include "led.h"
#include "modbus_rtu.h"
#include "motion_control.h"
#include "servo_debug_control.h"
#include "app_cli_servo_session.h"
#include "main.h"
#include "nrf_status_telemetry.h"
#include "app_hmi.h"
#include "app_hmi_control.h"
#include "app_can_control.h"
#include "app_control_arbiter.h"
#include "bsp_hmi_uart.h"

// 运动任务只调用运动控制接口，不直接访问定时器寄存器。
void motion_task(void *argument)
{
    TickType_t last_wake;

    (void)argument;
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        app_runtime_note(APP_RUNTIME_MOTION);

        if (!modbus_business_allowed())
        {
            app_health_beat(APP_HEARTBEAT_MOTION);
            vTaskDelay(pdMS_TO_TICKS(20U));
            continue;
        }
        motion_update();
        if (!app_control_arbiter_preparing() &&
            (app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE ||
             app_control_arbiter_owner() == APP_CONTROL_OWNER_CLI))
            servo_debug_motion_service();
        app_remote_servo_motion_service();
        app_position_monitor_service();
        app_health_beat(APP_HEARTBEAT_MOTION);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_MOTION_PERIOD_MS));
    }
}

// 通信任务负责RS485收发、Modbus事务和AIMtor响应处理。
void servo_comm_task(void *argument)
{
    TickType_t last_wake;

    (void)argument;
    /* 在这个上下文里，xTaskGetCurrentTaskHandle() 获取的就是servo_comm_task自己的任务句柄 */
    /* UART2通知由唯一总线执行任务接收。 */
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        app_runtime_note(APP_RUNTIME_SERVO);
        if (!modbus_business_allowed())
        {
            app_health_beat(APP_HEARTBEAT_SERVO_COMM);
            vTaskDelay(pdMS_TO_TICKS(20U));
            continue;
        }
        if (app_health_take_guard_stop()) {
            aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_HEALTH, 0xFFU, 1U, 0U);
            aimtor_request_controlled_safety_stop(1U);
        }
        app_sync_params_service();
        app_can_control_pre_service();
        app_control_arbiter_service();
        aimtor_service();
        aimtor_handle_modbus_response();
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
        aimtor_rs485_diag_observe();
#endif
        {
            servo_recovery_status_t recovery;
            static uint32_t isolated_generation;
            aimtor_get_recovery_status(&recovery);
            if (aimtor_recovery_blocks_motion()) app_control_arbiter_recovery_hold(true);
            if (recovery.state == SERVO_RECOVERY_REQUESTED && isolated_generation != recovery.generation)
            {
                app_control_arbiter_recovery_hold(true);
                app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
                app_control_arbiter_cancel_pending();
                app_hmi_control_init();
                app_can_control_recovery_reset();
                app_cli_servo_session_recovery_reset();
                app_remote_servo_recovery_reset();
                servo_debug_abort_from_fault();
                aimtor_recovery_isolate();
                isolated_generation = recovery.generation;
            }
            if (recovery.state == SERVO_RECOVERY_COMMIT)
            {
                aimtor_recovery_commit(true);
                if (!aimtor_recovery_blocks_motion())
                {
                    app_control_arbiter_recovery_hold(false);
                    (void)app_control_arbiter_clear_safety();
                }
            }
        }
        app_can_control_pre_service();
        /* 撤权、停止及代次检查先于调试状态机的自动启动。 */
        app_remote_servo_service();
        app_cli_servo_session_service();
        app_hmi_control_service();
        app_can_control_service();
        if (!app_control_arbiter_preparing() &&
            (app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE ||
             app_control_arbiter_owner() == APP_CONTROL_OWNER_CLI))
            servo_debug_service();
        app_health_beat(APP_HEARTBEAT_SERVO_COMM);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_SERVO_COMM_PERIOD_MS));
    }
}

/* RS485/DMA事务的唯一执行上下文，不运行任何业务控制状态机。 */
void modbus_bus_task(void *argument)
{
    (void)argument;
    rs485_bind_rx_task(xTaskGetCurrentTaskHandle());
    for (;;)
    {
        app_runtime_note(APP_RUNTIME_MODBUS);
        rs485_service();
        modbus_process();
        app_health_beat(APP_HEARTBEAT_MODBUS);
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

// 健康任务集中判定READY、FAULT和OFFLINE，并维护系统心跳。
void health_task(void *argument)
{
    TickType_t last_wake;
    TickType_t last_evaluation;
    uint8_t evaluation_seen = 0U;
    servo_snapshot_t left;
    servo_snapshot_t right;
    uint8_t left_selected;
    uint8_t right_selected;
    uint8_t selected_axis_bad;
    uint8_t ready;

    (void)argument;
    last_wake = xTaskGetTickCount();
    last_evaluation = last_wake;
    for (;;)
    {
        app_runtime_note(APP_RUNTIME_HEALTH);

        if (!modbus_business_allowed())
        {
            app_system_snapshot_service();
            app_health_beat(APP_HEARTBEAT_HEALTH);
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_HEALTH_TASK_PERIOD_MS));
            continue;
        }
        /* 只读快照每10ms发布；原健康决策保持100ms周期。 */
        if (!evaluation_seen || (TickType_t)(xTaskGetTickCount() - last_evaluation) >=
            pdMS_TO_TICKS(APP_HEALTH_PERIOD_MS))
        {
            last_evaluation = xTaskGetTickCount();
            evaluation_seen = 1U;
            /*
             * 快照接口内部已经保护共享状态，这里不能再包一层临界区，
             * 否则会把嵌套临界区扩展到健康判定和系统状态读取。
             */
            (void)aimtor_get_snapshot(SERVO_AXIS_LEFT, &left);
            (void)aimtor_get_snapshot(SERVO_AXIS_RIGHT, &right);
            left_selected = app_config_servo_axis_selected(SERVO_AXIS_LEFT);
            right_selected = app_config_servo_axis_selected(SERVO_AXIS_RIGHT);
            taskENTER_CRITICAL();
            selected_axis_bad = (uint8_t)(
                (left_selected &&
                 ((left.fault && !aimtor_cold_clear_suppresses_fault(SERVO_AXIS_LEFT)) ||
                  (!left.online &&
                   !(aimtor_remote_control_active() &&
                     (aimtor_get_remote_control_mask() &
                      APP_SERVO_CONTROL_AXIS_LEFT))))) ||
                (right_selected &&
                 ((right.fault && !aimtor_cold_clear_suppresses_fault(SERVO_AXIS_RIGHT)) ||
                  (!right.online &&
                   !(aimtor_remote_control_active() &&
                     (aimtor_get_remote_control_mask() &
                      APP_SERVO_CONTROL_AXIS_RIGHT))))));
            if (((left_selected && left.enabled) ||
                 (right_selected && right.enabled) ||
                 motion_get_state() == MOTION_RUNNING ||
                 motion_get_state() == MOTION_STOPPING) &&
                selected_axis_bad)
            {
                selected_axis_bad = 1U;
            }
            else
            {
                selected_axis_bad = 0U;
            }
            /*
             * 只读调试模式永远不开放运动就绪。
             * 正式控制模式要求所有参与查询的轴同时满足运动门禁；
             * 当前配置未选中的轴不参与就绪判定。
             */
            ready = (uint8_t)(
                aimtor_motion_allowed() &&
                !app_config_servo_readonly_test() &&
                aimtor_register_map_valid() &&
                app_config_motion_hardware_ready() &&
                app_config_servo_bus_ready() &&
                !aimtor_maintenance_enabled() &&
                (!left_selected ||
                 (aimtor_cli_control_active() &&
                  !aimtor_cli_axis_owned(SERVO_AXIS_LEFT) &&
                  left.online && !left.fault && left.speed_valid &&
                  left.fault_valid && left.actual_speed_rpm == 0 &&
                  left.speed_age_ms < APP_SERVO_STATUS_MAX_AGE_MS &&
                  left.fault_age_ms < APP_SERVO_STATUS_MAX_AGE_MS &&
                  !left.busy && !left.stop_latched) ||
                ((left.online || (aimtor_remote_control_active() &&
                                  (aimtor_get_remote_control_mask() &
                                   APP_SERVO_CONTROL_AXIS_LEFT) &&
                                  !left.fault)) &&
                  left.enabled && !left.busy &&
                  !left.stop_latched &&
                  (left.control_mode != SERVO_CONTROL_PULSE_POSITION ||
                   !APP_SERVO_BOOT_ZERO_ENABLED || left.zero_verified) &&
                  left.parameter_verified &&
                  (left.control_mode != SERVO_CONTROL_NONE) &&
                  !left.fault)) &&
                (!right_selected ||
                 (aimtor_cli_control_active() &&
                  !aimtor_cli_axis_owned(SERVO_AXIS_RIGHT) &&
                  right.online && !right.fault && right.speed_valid &&
                  right.fault_valid && right.actual_speed_rpm == 0 &&
                  right.speed_age_ms < APP_SERVO_STATUS_MAX_AGE_MS &&
                  right.fault_age_ms < APP_SERVO_STATUS_MAX_AGE_MS &&
                  !right.busy && !right.stop_latched) ||
                ((right.online || (aimtor_remote_control_active() &&
                                   (aimtor_get_remote_control_mask() &
                                    APP_SERVO_CONTROL_AXIS_RIGHT) &&
                                   !right.fault)) &&
                 right.enabled && !right.busy &&
                  !right.stop_latched &&
                  (right.control_mode != SERVO_CONTROL_PULSE_POSITION ||
                   !APP_SERVO_BOOT_ZERO_ENABLED || right.zero_verified) &&
                  right.parameter_verified &&
                  (right.control_mode != SERVO_CONTROL_NONE) &&
                  !right.fault)));
            taskEXIT_CRITICAL();
            if (selected_axis_bad != 0U)
            {
                if ((left_selected && left.fault) || (right_selected && right.fault))
                {
                    motion_request_immediate_safety_stop(
                        MOTION_FAULT_REASON_SERVO_FAULT, 0U);
                }
                else
                {
                    aimtor_request_controlled_safety_stop(1U);
                }
                ready = 0U;
            }
            motion_set_system_ready(ready);
        }
        /* 健康任务为快照唯一运行期写者，安全切换不转移写入所有权。 */
        app_system_snapshot_service();
        app_health_beat(APP_HEARTBEAT_HEALTH);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_HEALTH_TASK_PERIOD_MS));
    }
}

void key_task(void *argument)
{
    TickType_t last_wake;
    key_event_t event;

    (void)argument;
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        key_scan_5ms();
        while (key_get_event(&event, 0U))
        {
            if ((event.key == KEY_ID_1) && (event.type == KEY_EVENT_SHORT))
            {
                /* PC13短按承接原KEY2短按：普通停车。 */
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_BOARD_KEY, 0xFFU,
                    (uint16_t)event.type, ((uint32_t)event.key << 16U) |
                    (uint32_t)HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13));
                if (app_cli_servo_session_active())
                {
                    app_cli_servo_session_stop();
                    continue;
                }
                if (app_remote_servo_is_active())
                {
                    app_remote_servo_request_estop();
                }
                (void)servo_debug_stop();
            }
            else if ((event.key == KEY_ID_1) &&
                     (event.type == KEY_EVENT_LONG))
            {
                /* PC13长按承接原KEY2长按：急停。 */
                aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_BOARD_KEY, 0xFFU,
                    (uint16_t)event.type, ((uint32_t)event.key << 16U) |
                    (uint32_t)HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13));
                if (app_cli_servo_session_active())
                {
                    app_cli_servo_session_abort(CLI_EXIT_MANUAL);
                    continue;
                }
                if (app_remote_servo_is_active())
                {
                    app_remote_servo_request_estop();
                }
                (void)servo_debug_emergency_stop();
            }
            else if ((event.key == KEY_ID_1) &&
                     (event.type == KEY_EVENT_DOUBLE))
            {
                /* 原PC13长按的LCD翻页功能改由双击触发，避免与急停长按冲突。 */
#if APP_CHASSIS_LCD_ENABLED
                app_lcd_toggle_page();
#endif
            }
            else if ((event.key == KEY_ID_3) &&
                     (event.type == KEY_EVENT_LONG))
            {
                servo_axis_t axis;

                /* 只复位当前编译配置选中的轴，未选中轴完全不参与。 */
                for (axis = SERVO_AXIS_LEFT;
                     axis < SERVO_AXIS_COUNT;
                     axis++)
                {
                    if (app_config_servo_axis_selected((uint8_t)axis))
                    {
                        (void)aimtor_reset_fault(axis);
                    }
                }
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_KEY_PERIOD_MS));
    }
}

// 状态任务只负责LED和蜂鸣器等诊断输出，不参与运动决策。
void status_task(void *argument)
{
    TickType_t last_wake;
    motion_state_t state;
    servo_wdg_recovery_status_t recovery;
    servo_startup_hold_status_t hold;
    uint8_t axis;
    uint8_t recovery_failed;
    uint8_t motion_authorized;

    (void)argument;
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        state = motion_get_state();
        recovery_failed = 0U;
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        {
            if (aimtor_watchdog_recovery_get_status((servo_axis_t)axis, &recovery) &&
                recovery.state == SERVO_WDG_RECOVERY_FAILED)
                recovery_failed = 1U;
            if (aimtor_startup_hold_get_status((servo_axis_t)axis, &hold) &&
                hold.state == SERVO_STARTUP_HOLD_FAILED)
                recovery_failed = 1U;
        }
        motion_authorized = app_status_motion_authorized(
            (uint8_t)aimtor_remote_control_active(),
            (uint8_t)aimtor_hmi_control_active(),
            (uint8_t)aimtor_can_control_active());
        if (!modbus_business_allowed()) motion_authorized = 0U;
        set_led(RGB_RED, ((state == MOTION_FAULT) ||
                          (state == MOTION_OFFLINE) || recovery_failed ||
                          app_remote_servo_k1_latched()) ?
                          LED_ON : LED_OFF);
        set_led(RGB_GREEN, motion_authorized ? LED_ON : LED_OFF);
        set_led(RGB_BLUE, (state == MOTION_RUNNING) ? LED_ON : LED_OFF);
        set_beep(((state == MOTION_FAULT) ||
                  (state == MOTION_OFFLINE) || recovery_failed) ? BEEP_ON : BEEP_OFF);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_STATUS_PERIOD_MS));
    }
}

void cli_task(void *argument)
{
    (void)argument;
    for (;;)
    {
        app_runtime_note(APP_RUNTIME_CLI);
        app_cli_process();
    }
}

#if APP_CHASSIS_LCD_ENABLED
void lcd_task(void *argument)
{
    TickType_t last_wake;
    TickType_t delay_ticks;

    (void)argument;
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        if (!app_lcd_is_ready())
        {
            /* 每秒检查，实际重建由LCD模块的2/4/8/10秒退避控制。 */
            lcd_init();
        }
        app_lcd_refresh();
        delay_ticks = app_lcd_is_ready() ?
                      pdMS_TO_TICKS(APP_LCD_PERIOD_MS) :
                      pdMS_TO_TICKS(1000U);
        vTaskDelayUntil(&last_wake, delay_ticks);
    }
}

#endif /* APP_CHASSIS_LCD_ENABLED */

void iwdg_task(void *argument)
{
    TickType_t last_wake;

    (void)argument;
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        app_health_watchdog_service();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_IWDG_PERIOD_MS));
    }
}

/* NRF任务接收解析并发布遥控快照，控制事务由通信任务执行。 */
void nrf_task(void *argument)
{
    (void)argument;
    NRF24L01_BindTask(xTaskGetCurrentTaskHandle());
    NRF24L01_Init();
    nrf_status_telemetry_init((uint8_t)(HAL_GetUIDw0() ^ HAL_GetUIDw1() ^
        HAL_GetUIDw2() ^ bsp_system_get_reset_reason() ^
        (bsp_system_get_reset_reason() >> 24U) ^ HAL_GetTick()));
    for (;;)
    {
        app_nrf_remote_service();
        {
            nrf_remote_snapshot_t remote;

            if (app_nrf_remote_get_snapshot(&remote))
            {
                app_remote_servo_update(&remote);
            }
        }
        /* 控制帧解析和授权事件发布优先，状态回传仅使用已有快照。 */
        nrf_status_telemetry_service();
        app_health_beat(APP_HEARTBEAT_NRF);
        {
            NRF24L01_Stats nrf_stats;
            app_health_nrf_input_t input;
            NRF24L01_GetStats(&nrf_stats);
            input.initialized=nrf_stats.initialized;input.ready=nrf_stats.radio_ready;
            input.failure=nrf_stats.last_failure;input.config_checks=nrf_stats.config_check_count;
            input.spi_errors=nrf_stats.spi_errors;input.dma_timeouts=nrf_stats.dma_timeouts;
            input.verify_failures=nrf_stats.verify_failures;
            input.config_mismatches=nrf_stats.config_check_mismatches;input.recoveries=nrf_stats.recoveries;
            app_health_nrf_update(&input, HAL_GetTick());
            if ((!nrf_stats.initialized || !nrf_stats.radio_ready) && nrf_stats.last_failure != 0U)
                app_health_set_diagnostic_fault(APP_DIAG_NRF_INIT_ERROR);
        }
        /* DMA完成可立即唤醒；无数据时最多等待一个原有5ms轮询周期。 */
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_NRF_TASK_PERIOD_MS));
    }
}

/* 车载屏服务不加入关键任务心跳，屏幕断线不能触发看门狗复位。 */
void hmi_task(void *argument)
{
    (void)argument;
    bsp_hmi_uart_bind_task(xTaskGetCurrentTaskHandle());
    for (;;)
    {
        app_runtime_note(APP_RUNTIME_HMI);
        app_hmi_service();
        if (app_hmi_work_pending())
        {
            taskYIELD();
        }
        else
        {
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_HMI_TASK_PERIOD_MS));
        }
    }
}
