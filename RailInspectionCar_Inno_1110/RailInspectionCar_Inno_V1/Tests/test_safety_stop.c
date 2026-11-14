#define main core_regression_main
#include "test_core.c"
#undef main

extern void aimtor_test_safety_seed(int16_t rpm);
extern void aimtor_test_safety_poll_fail(uint8_t failures, modbus_status_t status);
extern void aimtor_test_safety_age(void);
extern void aimtor_test_safety_enable_poll(servo_axis_t axis);
extern void aimtor_test_safety_pulse_mode(void);

static void safety_reset(int16_t rpm)
{
    response_fault = 0U;
    rx_length = rx_offset = 0U;
    memset(registers, 0, sizeof(registers));
    ticks = 1000U;
    motion_init();
    modbus_cancel_pending();
    modbus_resume_transport();
    aimtor_test_safety_seed(rpm);
    for (unsigned axis = 0U; axis < 2U; axis++)
    {
        if (!app_config_servo_axis_selected((uint8_t)axis)) continue;
        registers[axis + 1U][AIMTOR_REG_SERVO_ENABLE] = 1U;
        registers[axis + 1U][AIMTOR_REG_SPEED_COMMAND] = (uint16_t)rpm;
        registers[axis + 1U][AIMTOR_REG_ACTUAL_SPEED] = (uint16_t)rpm;
    }
}

static void run_stop(int16_t rpm, uint8_t stale)
{
    servo_safety_stop_status_t evidence;
    int16_t previous[2] = {rpm, rpm};
    safety_reset(rpm);
    if (stale) aimtor_test_safety_age();
else aimtor_test_safety_poll_fail(APP_SERVO_RUNTIME_COMM_FAILURES, MODBUS_TIMEOUT);
    aimtor_service();
    assert(aimtor_get_safety_stop_status(&evidence));
    assert(aimtor_safety_stop_active());
    {
        uint32_t expected = (uint32_t)((float)abs(rpm) *
            APP_WHEEL_CIRCUMFERENCE_M * 1000.0f /
            (60.0f * APP_GEAR_REDUCTION_RATIO * APP_SERVO_SAFETY_STOP_DECEL_MPS2));
        assert(evidence.timeout_ms == expected + 5000U);
    }
    assert(motion_get_state() == MOTION_STOPPING);
    assert(motion_get_stop_class() == MOTION_STOP_CLASS_CONTROLLED_SAFETY);
    assert(aimtor_clear_stop_latch() == SERVO_NOT_READY);
    assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT, rpm) == SERVO_NOT_READY ||
           !app_config_servo_axis_selected(0U));
    for (unsigned elapsed = 0U; elapsed < 30000U && aimtor_safety_stop_active(); elapsed++)
    {
        for (unsigned axis = 0U; axis < 2U; axis++)
        {
            int16_t command;
            if (!app_config_servo_axis_selected((uint8_t)axis)) continue;
            command = (int16_t)registers[axis + 1U][AIMTOR_REG_SPEED_COMMAND];
            assert(abs(command) <= abs(previous[axis]));
            assert(!command || (command > 0) == (rpm > 0));
            if (command) assert(snapshot((servo_axis_t)axis).enabled);
            if ((uint32_t)(ticks - evidence.started_ms) < 500U)
                assert(abs(command) > abs(rpm) - 100);
            previous[axis] = command;
            registers[axis + 1U][AIMTOR_REG_ACTUAL_SPEED] = (uint16_t)command;
        }
        motion_update();
        pump_period(1U, 1U);
        motion_set_system_ready(1U);
    }
    assert(!aimtor_safety_stop_active());
    assert(aimtor_get_safety_stop_status(&evidence));
    assert(evidence.completed && !evidence.escalated);
    assert(evidence.completed_ms - evidence.started_ms > 1000U);
    {
        float expected_ms = (float)abs(rpm) * APP_WHEEL_CIRCUMFERENCE_M * 1000.0f /
            (60.0f * APP_GEAR_REDUCTION_RATIO * APP_SERVO_SAFETY_STOP_DECEL_MPS2);
        assert(evidence.completed_ms - evidence.started_ms >= (uint32_t)expected_ms);
        assert(evidence.completed_ms - evidence.started_ms < (uint32_t)expected_ms + 1000U);
    }
    assert(motion_get_stop_class() == MOTION_STOP_CLASS_CONTROLLED_SAFETY);
    assert(motion_get_state() == MOTION_OFFLINE);
    for (unsigned axis = 0U; axis < 2U; axis++)
        if (app_config_servo_axis_selected((uint8_t)axis))
        {
            assert(snapshot((servo_axis_t)axis).stop_latched);
            assert(!snapshot((servo_axis_t)axis).enabled);
            assert(registers[axis + 1U][AIMTOR_REG_SPEED_COMMAND] == 0U);
        }
    pump(30U);
    assert(motion_get_state() == MOTION_OFFLINE);
}

static void run_pulse_stop(void)
{
    servo_safety_stop_status_t evidence;
    uint32_t previous[2];
    safety_reset(0);
    aimtor_test_safety_pulse_mode();
    motion_set_system_ready(1U);
    assert(motion_start_pulse_mask(app_config_servo_control_axis_mask(),
        1U, 10000U, 1000000U) == MOTION_OK);
    for (unsigned cycle = 0U; cycle < 300U; cycle++)
    {
        motion_update();
        ticks += APP_MOTION_PERIOD_MS;
    }
    for (unsigned axis = 0U; axis < 2U; axis++)
        previous[axis] = servo_pulse_get_frequency((servo_axis_t)axis);
    aimtor_request_controlled_safety_stop(1U);
    for (unsigned cycle = 0U; cycle < 5000U && aimtor_safety_stop_active(); cycle++)
    {
        motion_update();
        for (unsigned axis = 0U; axis < 2U; axis++)
        {
            uint32_t frequency;
            if (!app_config_servo_axis_selected((uint8_t)axis)) continue;
            frequency = servo_pulse_get_frequency((servo_axis_t)axis);
            assert(frequency <= previous[axis]);
            if (cycle == 0U) assert(frequency > previous[axis] / 2U);
            if (frequency) assert(snapshot((servo_axis_t)axis).enabled);
            previous[axis] = frequency;
            registers[axis + 1U][AIMTOR_REG_ACTUAL_SPEED] = (uint16_t)(
                (float)frequency / APP_PULSES_PER_METER * 60.0f *
                APP_GEAR_REDUCTION_RATIO / APP_WHEEL_CIRCUMFERENCE_M);
        }
        pump_period(1U, APP_MOTION_PERIOD_MS);
    }
    assert(aimtor_get_safety_stop_status(&evidence));
    assert(evidence.completed && !evidence.escalated);
    assert(!aimtor_safety_stop_active());
    assert(motion_get_stop_class() == MOTION_STOP_CLASS_CONTROLLED_SAFETY);
}

int main(void)
{
    extern void communication_recovery_tests(void);

    servo_safety_stop_status_t evidence;
    unsigned selected = app_config_servo_axis_selected(0U) ? 0U : 1U;
    motion_init();
    assert(modbus_init() == MODBUS_OK);
    communication_recovery_tests();
for (unsigned failures = 1U; failures < APP_SERVO_RUNTIME_COMM_FAILURES; failures++)
    {
        safety_reset(-1600);
        aimtor_test_safety_poll_fail((uint8_t)failures, MODBUS_TIMEOUT);
        aimtor_service();
        assert(!aimtor_safety_stop_active());
        assert(snapshot((servo_axis_t)selected).enabled);
    }
    safety_reset(-1600);
    aimtor_test_safety_poll_fail(1U, MODBUS_CRC_ERROR);
    aimtor_service();
    assert(!aimtor_safety_stop_active());
    for (unsigned failure = 0U; failure < 2U; failure++)
    {
        safety_reset(-1600);
        if (failure) response_fault = 1U;
        else registers[selected + 1U][AIMTOR_REG_SERVO_ENABLE] = 0U;
        aimtor_test_safety_enable_poll((servo_axis_t)selected);
        modbus_process();
        aimtor_handle_modbus_response();
        response_fault = 0U;
        aimtor_service();
        if (failure)
        {
            assert(!aimtor_safety_stop_active());
            continue;
        }
        assert(aimtor_safety_stop_active());
        assert(snapshot((servo_axis_t)selected).enabled);
        assert(motion_get_stop_class() == MOTION_STOP_CLASS_CONTROLLED_SAFETY);
        assert(aimtor_get_safety_stop_status(&evidence));
        assert(evidence.axis_mask & (1U << selected));
    }
    run_stop(-1600, 1U);
    run_stop(1600, 0U);
    run_pulse_stop();
    safety_reset(-1600);
    aimtor_request_controlled_safety_stop(1U);
    response_fault = 1U;
    for (unsigned elapsed = 0U; elapsed < 3000U && aimtor_safety_stop_active(); elapsed++)
        pump_period(1U, 1U);
    assert(!aimtor_safety_stop_active());
    assert(aimtor_get_safety_stop_status(&evidence));
    assert(evidence.escalated && !evidence.completed);
    assert(motion_get_fault_reason() == MOTION_FAULT_REASON_OUTPUT_FAILURE);
    safety_reset(-1600);
    aimtor_request_controlled_safety_stop(1U);
    registers[selected + 1U][AIMTOR_REG_FAULT_CODE] = 0x1234U;
    for (unsigned elapsed = 0U; elapsed < 500U && aimtor_safety_stop_active(); elapsed++)
        pump_period(1U, 1U);
    assert(!aimtor_safety_stop_active());
    assert(motion_get_fault_reason() == MOTION_FAULT_REASON_SERVO_FAULT);
    safety_reset(-1600);
    aimtor_request_controlled_safety_stop(1U);
    for (unsigned elapsed = 0U; elapsed < 30000U && aimtor_safety_stop_active(); elapsed++)
        pump_period(1U, 1U);
    assert(!aimtor_safety_stop_active());
    assert(motion_get_fault_reason() == MOTION_FAULT_REASON_STOP_TIMEOUT);
    safety_reset(-1600);
    ticks = UINT32_MAX - 50U;
    aimtor_test_safety_seed(-1600);
    aimtor_request_controlled_safety_stop(1U);
    pump_period(200U, 1U);
    assert(aimtor_safety_stop_active());
    assert((int16_t)registers[selected + 1U][AIMTOR_REG_SPEED_COMMAND] < -1400);
    safety_reset(-1600);
    aimtor_request_controlled_safety_stop(1U);
    aimtor_emergency_stop();
    assert(!aimtor_safety_stop_active());
    assert(!snapshot((servo_axis_t)selected).enabled);
    safety_reset(0);
    motion_request_immediate_safety_stop(MOTION_FAULT_REASON_OUTPUT_FAILURE, 0U);
    pump(100U);
    assert(aimtor_clear_stop_latch() == SERVO_OK);
    assert(aimtor_get_safety_stop_status(&evidence));
    assert(evidence.initial_reason == MOTION_FAULT_REASON_NONE);
    assert(motion_get_fault_reason() == MOTION_FAULT_REASON_NONE);
    motion_request_immediate_safety_stop(MOTION_FAULT_REASON_SERVO_FAULT, 0U);
    assert(aimtor_get_safety_stop_status(&evidence));
    assert(evidence.initial_reason == MOTION_FAULT_REASON_SERVO_FAULT);
    safety_reset(-1600);
    registers[selected + 1U][AIMTOR_REG_FAULT_CODE] = 1U;
    for (unsigned elapsed = 0U; elapsed < 500U && !snapshot((servo_axis_t)selected).stop_latched; elapsed++)
        pump_period(1U, 1U);
    assert(snapshot((servo_axis_t)selected).stop_latched);
    assert(motion_get_fault_reason() == MOTION_FAULT_REASON_SERVO_FAULT);
    puts("PASS safety stop: time ramp, pulse ramp, stale/timeout/CRC, enable feedback failure, fresh zero confirmation, true fault, deadline, no replay, manual preemption");
    return 0;
}

extern void aimtor_test_key_response(uint8_t axis, uint8_t item, modbus_status_t status, uint8_t malformed, uint32_t transaction);
extern void aimtor_test_key_age(uint8_t axis, uint8_t item, uint32_t age);
extern void aimtor_test_key_queue_busy(uint8_t axis);
extern void aimtor_test_real_fault(uint8_t axis);
extern void aimtor_test_cancelled_key(uint8_t axis);

static servo_recovery_status_t recovery_status(void)
{
    servo_recovery_status_t r; aimtor_get_recovery_status(&r); return r;
}
static void recovery_pump(unsigned duration, uint8_t commit)
{
    TickType_t started = ticks;
    while ((TickType_t)(ticks - started) < duration)
    {
        for (unsigned a=0U; a<2U; ++a)
            if (app_config_servo_axis_selected((uint8_t)a))
                registers[a+1U][AIMTOR_REG_ACTUAL_SPEED] = registers[a+1U][AIMTOR_REG_SPEED_COMMAND];
        pump_period(1U, 1U);
        if (recovery_status().state == SERVO_RECOVERY_REQUESTED) aimtor_recovery_isolate();
        if (commit && recovery_status().state == SERVO_RECOVERY_COMMIT) aimtor_recovery_commit(true);
    }
}
void communication_recovery_tests(void)
{
    uint8_t a = app_config_servo_axis_selected(0U) ? 0U : 1U;
    servo_axis_health_t h;
    servo_recovery_status_t first;
    for (unsigned result=0U; result<3U; ++result)
        for (uint8_t item=0U; item<3U; ++item)
        {
            safety_reset(200);
            for (unsigned n=1U;n<=5U;++n)
            {
                aimtor_test_key_response(a,item,result==0U?MODBUS_TIMEOUT:result==1U?MODBUS_CRC_ERROR:MODBUS_OK,result==2U,0U);
                assert(aimtor_get_health((servo_axis_t)a,&h));
                assert(h.item[item].failures==n);
                if(n<5U) {aimtor_service();assert(!aimtor_safety_stop_active());}
            }
            aimtor_service(); assert(aimtor_safety_stop_active());
        }
    for(uint8_t malformed=2U;malformed<=4U;++malformed)
    {
        safety_reset(200);
        aimtor_test_key_response(a,0U,MODBUS_OK,malformed,0U);
        assert(aimtor_get_health((servo_axis_t)a,&h) && h.item[0].failures==1U);
        assert(h.item[0].result==MODBUS_FRAME_ERROR);
    }
    safety_reset(200);
    aimtor_test_key_response(a,0U,MODBUS_TIMEOUT,0U,100000U);
    aimtor_test_key_response(a,0U,MODBUS_TIMEOUT,0U,100000U);
    aimtor_test_key_response(a,1U,MODBUS_OK,0U,0U);
    if(app_config_servo_axis_selected((uint8_t)(1U-a)))
        aimtor_test_key_response((uint8_t)(1U-a),0U,MODBUS_OK,0U,0U);
    aimtor_test_key_queue_busy(a); aimtor_test_cancelled_key(a);
    assert(aimtor_get_health((servo_axis_t)a,&h) && h.item[0].failures==1U);
    assert(h.level==SERVO_HEALTH_DEGRADED);
    aimtor_test_key_response(a,0U,MODBUS_OK,0U,0U);
    assert(aimtor_get_health((servo_axis_t)a,&h) && h.item[0].failures==0U);
    for (uint8_t item=0U;item<3U;++item)
    {
        safety_reset(200); ticks=UINT32_MAX-100U;
        aimtor_test_safety_seed(200);
        aimtor_test_key_age(a,item,499U);
        assert(aimtor_get_health((servo_axis_t)a,&h) && h.item[item].level!=SERVO_HEALTH_STOP);
        ++ticks;
        assert(aimtor_get_health((servo_axis_t)a,&h) && h.item[item].level==SERVO_HEALTH_STOP);
        aimtor_service(); assert(aimtor_safety_stop_active());
    }
    safety_reset(200);
    aimtor_test_key_age(a, 0U, 499U);
    aimtor_test_key_queue_busy(a);
    assert(aimtor_get_health((servo_axis_t)a, &h) && h.item[0].failures == 0U && h.item[0].age_ms == 499U);
    ++ticks;
    aimtor_service(); assert(aimtor_safety_stop_active());
    if (app_config_servo_control_axis_mask() == 3U)
    {
        safety_reset(200);
        for (unsigned n = 0U; n < 4U; ++n)
        {
            aimtor_test_key_response(0U, 0U, MODBUS_TIMEOUT, 0U, 0U);
            aimtor_test_key_response(1U, 2U, MODBUS_CRC_ERROR, 0U, 0U);
        }
        aimtor_service(); assert(!aimtor_safety_stop_active());
        aimtor_test_key_response(0U, 0U, MODBUS_OK, 0U, 0U);
        assert(aimtor_get_health(SERVO_AXIS_RIGHT, &h) && h.item[2].failures == 4U);
        aimtor_test_key_response(1U, 2U, MODBUS_CRC_ERROR, 0U, 0U);
        aimtor_service(); assert(aimtor_safety_stop_active());
    }
    safety_reset(200);
    aimtor_set_hmi_control(true, app_config_servo_control_axis_mask());
    assert(aimtor_set_velocity_rpm((servo_axis_t)a,300)==SERVO_OK);
    aimtor_test_key_response(a,1U,MODBUS_CRC_ERROR,0U,0U);
    pump_period(40U,1U);
    assert((int16_t)registers[a+1U][AIMTOR_REG_SPEED_COMMAND]<=200);
    assert(snapshot((servo_axis_t)a).speed_write_rpm_valid);
    assert(snapshot((servo_axis_t)a).speed_write_rpm<=200);
    safety_reset(200);
    aimtor_set_hmi_control(true, app_config_servo_control_axis_mask());
    aimtor_test_key_response(a,1U,MODBUS_TIMEOUT,0U,0U);
    assert(aimtor_set_velocity_rpm((servo_axis_t)a,-300)==SERVO_OK);
    pump_period(40U,1U);
    assert((int16_t)registers[a+1U][AIMTOR_REG_SPEED_COMMAND]==0);
    assert(aimtor_feedback_limit_rpm(1U,300,200)==200);
    assert(aimtor_feedback_limit_rpm(1U,-200,200)==0);
    assert(aimtor_feedback_limit_rpm(1U,100,200)==100);
    run_stop(1600,1U);
    assert(recovery_status().state==SERVO_RECOVERY_WAIT_HEALTH);
    assert(aimtor_request_recovery(2U)==SERVO_NOT_READY);
    motion_clear_fault(); assert(motion_get_state()==MOTION_OFFLINE);
    recovery_pump(700U,0U);
    assert(recovery_status().state!=SERVO_RECOVERY_READY);
    aimtor_test_key_response(a,1U,MODBUS_CRC_ERROR,0U,0U);
    assert(recovery_status().healthy_ms==0U);
    recovery_pump(900U,0U);
    assert(recovery_status().state!=SERVO_RECOVERY_READY);
    recovery_pump(900U,0U);
    assert(recovery_status().state==SERVO_RECOVERY_READY);
    first=recovery_status();
    aimtor_test_key_response(a,1U,MODBUS_CRC_ERROR,0U,0U);
    assert(aimtor_request_recovery(2U)==SERVO_NOT_READY);
    for(unsigned n=0U;n<60U;++n) aimtor_service();
    assert(recovery_status().state!=SERVO_RECOVERY_READY);
    recovery_pump(1800U,0U);
    assert(recovery_status().state==SERVO_RECOVERY_READY);
    assert(aimtor_clear_stop_latch()==SERVO_NOT_READY);
    assert(aimtor_request_recovery(2U)==SERVO_OK);
    assert(aimtor_request_recovery(2U)==SERVO_BUSY);
    for(unsigned axis=0U;axis<2U;++axis) registers[axis+1U][AIMTOR_REG_WORD_ORDER]=1U;
    recovery_pump(3000U,1U);
    if(recovery_status().state!=SERVO_RECOVERY_DONE)
        fprintf(stderr,"RECOVERY state=%u failure=%u eligible=%u healthy=%u\n",recovery_status().state,recovery_status().failure,recovery_status().eligible,(unsigned)recovery_status().healthy_ms);
    assert(recovery_status().state==SERVO_RECOVERY_DONE);
    assert(recovery_status().first_transaction==first.first_transaction);
    assert(recovery_status().first_address==first.first_address);
    assert(!aimtor_motion_allowed());
    assert(motion_get_fault_reason()==MOTION_FAULT_REASON_NONE);
    for(unsigned axis=0U;axis<2U;++axis)
        if(app_config_servo_axis_selected((uint8_t)axis))
        {
            assert(registers[axis+1U][AIMTOR_REG_SPEED_COMMAND]==0U);
            assert(snapshot((servo_axis_t)axis).enabled==(APP_SERVO_STARTUP_HOLD_ENABLED?1U:0U));
        }
    assert(aimtor_request_recovery(2U)==SERVO_NOT_READY);
    run_stop(1600,1U); recovery_pump(2000U,0U);
    assert(recovery_status().state==SERVO_RECOVERY_READY);
    assert(aimtor_request_recovery(2U)==SERVO_OK);
    aimtor_service(); aimtor_recovery_isolate(); aimtor_service();
    aimtor_test_key_response(a,1U,MODBUS_TIMEOUT,0U,0U);
    recovery_pump(100U,1U);
    assert(recovery_status().state==SERVO_RECOVERY_FAILED);
    assert(!aimtor_motion_allowed());
    run_stop(1600,1U); recovery_pump(2000U,0U);
    assert(recovery_status().state==SERVO_RECOVERY_READY);
    assert(aimtor_request_recovery(2U)==SERVO_OK);
    aimtor_service(); aimtor_recovery_isolate(); aimtor_service();
    aimtor_test_real_fault(a);
    recovery_pump(300U,1U);
    assert(recovery_status().state==SERVO_RECOVERY_FAILED && !recovery_status().eligible);
    puts("PASS communication/recovery: per-axis/item 1-5 timeout/CRC/context, dedup/cancel/busy, exact age/wrap, 1000ms reset, two-phase recovery/no replay, preparation failure");
}
