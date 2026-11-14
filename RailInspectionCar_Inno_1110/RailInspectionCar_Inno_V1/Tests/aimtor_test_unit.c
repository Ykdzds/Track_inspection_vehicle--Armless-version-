/* 仅主机测试开启门禁，生产工程仍使用app_config.h中的默认关闭值。 */
#include "app_config.h"
#include <stdio.h>
#undef APP_SERVO_SOFT_ENABLE_CONFIRMED
#undef APP_SERVO_MAINTENANCE_STOP_CONFIRMED
#define APP_SERVO_SOFT_ENABLE_CONFIRMED 1U
#define APP_SERVO_MAINTENANCE_STOP_CONFIRMED 1U
#if !defined(TEST_READONLY_ONLY) && !defined(TEST_BOOT_ZERO_ONLY) && !defined(TEST_CLI_SESSION_ONLY) && !defined(TEST_DIAGNOSTIC_ONLY) && !defined(TEST_SAFETY_STOP_ONLY)
#undef APP_SERVO_QUERY_MODE
#undef APP_SERVO_READONLY_TEST
#define APP_SERVO_QUERY_MODE APP_SERVO_QUERY_BOTH
#define APP_SERVO_READONLY_TEST 0U
#endif
#undef APP_UNPROTECTED_MOTION_TEST_ENABLED
#define APP_UNPROTECTED_MOTION_TEST_ENABLED (1U)
/* 历史设备单元测试独立于安全任务；独立安全测试编译真实门禁。 */
#include "modbus_rtu.h"
#define modbus_motion_permitted() (true)
/* Driver-only fixtures use this weak input. Tests that link the real chassis
 * arbiter must use its strong definition and its actual ownership state. */
static uint8_t test_arm_work_locked;
__attribute__((weak)) uint8_t app_control_arbiter_arm_locked(void){return test_arm_work_locked;}
void aimtor_test_arm_work_lock(uint8_t locked){test_arm_work_locked=locked;}
#include "../Device/aimtor_servo.c"
void aimtor_test_arm_final_writer(void){
 aimtor_step_t step;memset(&step,0,sizeof(step));step.address=AIMTOR_REG_SPEED_COMMAND;step.count=1U;step.words[0]=100U;
 test_arm_work_locked=1U;
 assert(aimtor_submit_step(SERVO_AXIS_LEFT,AIMTOR_TAG_SEQUENCE,&step,0U)==MODBUS_INVALID_ARGUMENT);
 test_arm_work_locked=0U;
}
/* Legacy fixtures have no safety task; joint startup tests link the real task. */
void app_health_request_driver_fallback(uint32_t reason)
{ (void)reason; modbus_safety_silence(); }

void aimtor_test_safety_seed(int16_t rpm)
{
    uint8_t axis;
    aimtor_init();
    aimtor_cli_gate_init();
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        aimtor_axis_state_t *state = &s_axes[axis];
        if (!app_config_servo_axis_selected(axis)) continue;
        s_last_velocity[axis] = rpm;
        state->snapshot.enabled = 1U;
        state->snapshot.enable_feedback_valid = state->snapshot.enable_feedback = 1U;
        state->snapshot.enable_feedback_tick = s_hold_enable_tick[axis] = xTaskGetTickCount();
        state->snapshot.parameter_verified = 1U;
        state->snapshot.control_mode = SERVO_CONTROL_RS485_VELOCITY;
        state->snapshot.actual_speed_rpm = rpm;
        state->snapshot.speed_valid = state->snapshot.fault_valid = 1U;
        state->speed_valid = state->fault_valid = 1U;
        state->speed_tick = state->fault_tick = xTaskGetTickCount();
    }
}

void aimtor_test_safety_poll_fail(uint8_t failures, modbus_status_t status)
{
    uint8_t index;
    for (index = 0U; index < failures; index++)
    {
        ++s_transaction_id;
        aimtor_poll_failure(&s_axes[app_config_servo_axis_selected(0U) ? 0U : 1U],
            AIMTOR_TAG_SPEED, status, s_transaction_id, xTaskGetTickCount());
    }
}

void aimtor_test_safety_age(void)
{
    uint8_t axis;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        s_axes[axis].speed_tick -= pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS + 1U);
        s_axes[axis].fault_tick -= pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS + 1U);
    }
}

void aimtor_test_safety_enable_poll(servo_axis_t axis)
{
    aimtor_step_t step;
    memset(&step, 0, sizeof(step));
    s_hold[axis].state = SERVO_STARTUP_HOLD_READY;
    step.address = AIMTOR_REG_SERVO_ENABLE;
    step.count = 1U;
    assert(aimtor_submit_step(axis, AIMTOR_TAG_HOLD_ENABLE, &step, 1U) == MODBUS_OK);
}

void aimtor_test_safety_pulse_mode(void)
{
    uint8_t axis;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_config_servo_axis_selected(axis)) continue;
        s_axes[axis].snapshot.control_mode = SERVO_CONTROL_PULSE_POSITION;
        assert(servo_pulse_set_enable((servo_axis_t)axis, true) == SERVO_PULSE_OK);
    }
    aimtor_set_hmi_control(true, app_config_servo_control_axis_mask());
}

extern void aimtor_test_enqueue_response(const modbus_response_t *response);

void aimtor_test_stop_polling(void)
{
    servo_axis_t axis;
    uint16_t tag;
    TickType_t now = xTaskGetTickCount();
    aimtor_init(); aimtor_cli_gate_init();
    if (APP_SERVO_CONTROL_AXIS != APP_SERVO_CONTROL_AXIS_BOTH ||
        !APP_REMOTE_SERVO_SYNC_ENABLED) return;
    aimtor_set_hmi_control(false, APP_SERVO_CONTROL_AXIS_BOTH);
    assert(!aimtor_hmi_control_active() && aimtor_sync_poll_active());
    s_sync_poll_phase = 1U;
    s_poll_axis_turn = 1U; s_poll_tag_turn[1] = 1U;
    s_axes[1].next_speed_poll = s_axes[1].next_fault_poll = now;
    assert(aimtor_pick_poll(&axis, &tag, now, 1U));
    assert(axis == SERVO_AXIS_RIGHT && tag == AIMTOR_TAG_SPEED);
    assert(!aimtor_background_safe(now));
    aimtor_set_hmi_control(false, 0U);
    assert(!aimtor_sync_poll_active());
    puts("PASS stop polling: permission revoked, paired feedback retained, no background borrowing, release retires mask");
}

static void diagnostic_response(uint16_t tag, int32_t raw, modbus_status_t result,
                                 uint8_t malformed)
{
    modbus_response_t response;
    uint32_t bits = (uint32_t)raw;
    memset(&response, 0, sizeof(response));
    response.context.owner = AIMTOR_OWNER_ID;
    response.context.axis = (uint8_t)s_pending_axis;
    response.context.tag = tag;
    response.context.transaction_id = ++s_transaction_id;
    s_pending_id = response.context.transaction_id;
    s_pending_tag = tag;
    s_pending_cancelled = 0U;
    response.status = result;
    response.operation = MODBUS_OPERATION_READ_HOLDING;
    response.address = aimtor_diagnostic_address(tag);
    response.quantity = aimtor_diagnostic_count(tag);
    response.data_length = (uint16_t)(1U + response.quantity * 2U);
    response.data[0] = (uint8_t)(response.quantity * 2U);
    response.data[1] = (uint8_t)(bits >> 8U);
    response.data[2] = (uint8_t)bits;
    response.data[3] = (uint8_t)(bits >> 24U);
    response.data[4] = (uint8_t)(bits >> 16U);
    if (malformed) response.quantity++;
    aimtor_test_enqueue_response(&response);
    aimtor_handle_modbus_response();
}

void aimtor_test_diagnostic(void)
{
    uint8_t axis;
    assert(aimtor_diagnostic_address(AIMTOR_TAG_DIAG_TORQUE) == 0x0B02U);
    assert(aimtor_diagnostic_address(AIMTOR_TAG_DIAG_LOAD) == 0x0B0CU);
    assert(aimtor_diagnostic_address(AIMTOR_TAG_DIAG_CURRENT) == 0x0B18U);
    assert(aimtor_diagnostic_address(AIMTOR_TAG_DIAG_BUS_VOLTAGE) == 0x0B1AU);
    assert(aimtor_diagnostic_count(AIMTOR_TAG_DIAG_CURRENT) == 2U);
    aimtor_init();
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) {
        aimtor_axis_state_t *state = &s_axes[axis];
        uint32_t update;
        if (!app_config_servo_axis_selected(axis)) {
            assert(state->snapshot.diagnostic_valid_mask == 0U);
            continue;
        }
        s_pending_axis = axis;
        assert(state->snapshot.diagnostic_valid_mask == 0U);
        diagnostic_response(AIMTOR_TAG_DIAG_TORQUE, 123, MODBUS_FRAME_ERROR, 0U);
        assert(state->snapshot.diagnostic_valid_mask == 0U);
        assert(state->snapshot.diagnostic_poll_failures == 1U);
        state->snapshot.consecutive_failures = 2U;
        update = state->snapshot.update_tick;
        diagnostic_response(AIMTOR_TAG_DIAG_TORQUE, -123, MODBUS_OK, 0U);
        diagnostic_response(AIMTOR_TAG_DIAG_LOAD, 456, MODBUS_OK, 0U);
        diagnostic_response(AIMTOR_TAG_DIAG_CURRENT, 1234, MODBUS_OK, 0U);
        diagnostic_response(AIMTOR_TAG_DIAG_BUS_VOLTAGE, 3250, MODBUS_OK, 0U);
        assert(state->snapshot.diagnostic_valid_mask == 15U);
        assert(state->snapshot.diagnostic_stale_mask == 0U);
        assert(state->snapshot.realtime_torque_tenths_percent == -123);
        assert(state->snapshot.load_ratio_tenths_percent == 456U);
        assert(state->snapshot.phase_current_deciamp == 123);
        assert(state->snapshot.bus_voltage_decivolt == 3250U);
        assert(state->snapshot.consecutive_failures == 2U);
        assert(state->snapshot.update_tick == update);
        assert(state->snapshot.poll_successes == 0U);
        diagnostic_response(AIMTOR_TAG_DIAG_CURRENT, -1234, MODBUS_OK, 0U);
        assert(state->snapshot.phase_current_deciamp == -123);
        diagnostic_response(AIMTOR_TAG_DIAG_CURRENT, -1235, MODBUS_OK, 0U);
        assert(state->snapshot.phase_current_deciamp == -124);
        diagnostic_response(AIMTOR_TAG_DIAG_CURRENT, INT32_MIN, MODBUS_OK, 0U);
        assert(state->snapshot.phase_current_deciamp == -124);
        assert(state->snapshot.diagnostic_stale_mask == 4U);
        diagnostic_response(AIMTOR_TAG_DIAG_CURRENT, 120000, MODBUS_OK, 0U);
        assert(state->snapshot.phase_current_deciamp == 12000);
        diagnostic_response(AIMTOR_TAG_DIAG_LOAD, -1, MODBUS_OK, 0U);
        assert(state->snapshot.load_ratio_tenths_percent == 456U);
        diagnostic_response(AIMTOR_TAG_DIAG_BUS_VOLTAGE, 3251, MODBUS_OK, 1U);
        assert(state->snapshot.bus_voltage_decivolt == 3250U);
        diagnostic_response(AIMTOR_TAG_DIAG_TORQUE, 123, MODBUS_FRAME_ERROR, 0U);
        assert(state->snapshot.realtime_torque_tenths_percent == -123);
        assert(state->snapshot.diagnostic_valid_mask == 15U);
        assert(state->snapshot.consecutive_failures == 2U);
        assert(state->snapshot.poll_failures == 0U);
    }
    aimtor_init();
    {
        servo_axis_t selected;
        uint16_t tag;
        TickType_t now = xTaskGetTickCount();
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) {
            s_axes[axis].speed_valid = s_axes[axis].fault_valid = 1U;
            s_axes[axis].speed_tick = s_axes[axis].fault_tick = now;
            s_axes[axis].next_speed_poll = s_axes[axis].next_fault_poll = now + 1000U;
            s_axes[axis].next_position_poll = s_axes[axis].next_state_poll = now + 1000U;
        }
        assert(aimtor_pick_poll(&selected, &tag, now, 0U));
        assert(app_config_servo_axis_selected((uint8_t)selected));
        assert(tag == AIMTOR_TAG_DIAG_TORQUE);
        s_remote_stop_active = 1U;
        assert(!aimtor_pick_poll(&selected, &tag, now, 0U));
        s_remote_stop_active = 0U;
        s_axes[selected].next_speed_poll = now;
        assert(aimtor_pick_poll(&selected, &tag, now, 0U));
        assert(tag == AIMTOR_TAG_SPEED);
    }
    aimtor_init();
    puts("PASS servo diagnostic: addresses, low-word-first, signed scaling, range and independent failures");
}

/* 直接核验生产后台选择器，关键事务始终允许插入位置对之间。 */
void aimtor_test_position_scheduler(void)
{
    TickType_t now = xTaskGetTickCount();
    servo_axis_t axis;
    uint16_t tag;
    uint8_t saved_remote = s_remote_control_mask;
    s_remote_control_mask = 0U;
    s_background_turn = 0U;
#if (APP_POSITION_MONITOR_ENABLED == 1U)
    s_position_pair_pending = 0U;
#endif
    for (uint8_t a = 0U; a < SERVO_AXIS_COUNT; a++)
    {
        s_axes[a].speed_valid = 1U;
        s_axes[a].fault_valid = 1U;
        s_axes[a].speed_tick = now;
        s_axes[a].fault_tick = now;
        s_axes[a].snapshot.consecutive_failures = 0U;
        s_axes[a].next_speed_poll = now + 100U;
        s_axes[a].next_fault_poll = now + 100U;
        s_axes[a].next_position_poll = now;
        s_axes[a].next_state_poll = now;
        s_axes[a].position_priority = 1U;
        for (uint8_t d = 0U; d < 4U; d++) s_axes[a].next_diagnostic_poll[d] = now + 1000U;
    }
    s_poll_axis_turn = 0U;
    assert(aimtor_pick_poll(&axis, &tag, now, 0U));
    assert(axis == SERVO_AXIS_LEFT && tag == AIMTOR_TAG_POSITION);
    s_axes[0].next_position_poll = now + 1000U;
    /* 左轴状态也到期，不能挤占已经预约的右轴位置。 */
    s_poll_axis_turn = 0U;
#if (APP_POSITION_MONITOR_ENABLED == 1U)
    s_axes[0].next_fault_poll = now;
    assert(aimtor_pick_poll(&axis, &tag, now, 1U));
    assert(tag == AIMTOR_TAG_FAULT);
    s_axes[0].next_fault_poll = now + 100U;
    assert(aimtor_pick_poll(&axis, &tag, now, 0U));
    assert(axis == SERVO_AXIS_RIGHT && tag == AIMTOR_TAG_POSITION);
#else
    /* With pairing disabled, ordinary background round-robin selects the
     * next due right-axis position before left-axis state. */
    assert(aimtor_pick_poll(&axis, &tag, now, 0U));
    assert(axis == SERVO_AXIS_RIGHT && tag == AIMTOR_TAG_POSITION);
#endif
    s_remote_control_mask = saved_remote;
}

/* Inject completed transport responses through the production dispatcher. */
static uint16_t test_key_value;
void aimtor_test_key_response(uint8_t axis, uint8_t item, modbus_status_t status,
                              uint8_t malformed, uint32_t transaction)
{
    modbus_response_t r;
    memset(&r, 0, sizeof(r));
    if (!transaction) transaction = ++s_transaction_id;
    r.context.owner = AIMTOR_OWNER_ID; r.context.axis = axis;
    r.context.transaction_id = transaction;
    r.context.tag = item == 0U ? AIMTOR_TAG_SPEED : item == 1U ? AIMTOR_TAG_FAULT : AIMTOR_TAG_HOLD_ENABLE;
    s_pending_axis = axis; s_pending_id = transaction; s_pending_tag = r.context.tag;
    s_pending_cancelled = 0U;
    r.operation = MODBUS_OPERATION_READ_HOLDING;
    r.address = item == 0U ? AIMTOR_REG_ACTUAL_SPEED : item == 1U ? AIMTOR_REG_FAULT_CODE : AIMTOR_REG_SERVO_ENABLE;
    r.quantity = 1U; r.data_length = 3U; r.data[0] = 2U;
    r.data[1] = (uint8_t)(test_key_value >> 8U); r.data[2] = (uint8_t)test_key_value;
    if (item == 2U) r.data[2] = s_axes[axis].snapshot.enabled;
    r.status = status;
    if (malformed == 1U) r.address++;
    else if (malformed == 2U) r.context.axis = (uint8_t)(1U-axis);
    else if (malformed == 3U) r.context.tag++;
    else if (malformed == 4U) r.context.owner++;
    aimtor_test_enqueue_response(&r); aimtor_handle_modbus_response();
}
void aimtor_test_key_age(uint8_t axis, uint8_t item, uint32_t age)
{
    TickType_t tick = xTaskGetTickCount() - pdMS_TO_TICKS(age);
    if (item == 0U) s_axes[axis].speed_tick = tick;
    else if (item == 1U) s_axes[axis].fault_tick = tick;
    else s_axes[axis].snapshot.enable_feedback_tick = s_hold_enable_tick[axis] = tick;
}
void aimtor_test_key_queue_busy(uint8_t axis)
{
    aimtor_poll_failure(&s_axes[axis], AIMTOR_TAG_SPEED, MODBUS_QUEUE_FULL, 0U, xTaskGetTickCount());
}
void aimtor_test_cancelled_key(uint8_t axis)
{
    modbus_response_t r;
    memset(&r, 0, sizeof(r));
    r.context.owner = AIMTOR_OWNER_ID; r.context.axis = axis;
    r.context.transaction_id = ++s_transaction_id; r.context.tag = AIMTOR_TAG_SPEED;
    s_pending_axis = axis; s_pending_id = r.context.transaction_id; s_pending_tag = r.context.tag;
    s_pending_cancelled = 1U; r.status = MODBUS_TIMEOUT;
    aimtor_test_enqueue_response(&r); aimtor_handle_modbus_response();
}

void aimtor_test_real_fault(uint8_t axis)
{
    test_key_value = 0x1234U;
    aimtor_test_key_response(axis, 1U, MODBUS_OK, 0U, 0U);
    test_key_value = 0U;
}
