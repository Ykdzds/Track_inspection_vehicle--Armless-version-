/* Compile the deployed protection gates, without the legacy motion-test override. */
/* Reuse hardware mocks only; exclude unrelated historical tests. */
#define TEST_PRODUCTION_GUARDS 1
#include "test_core.c"
/* This driver-only fixture models the arbiter input; the joint suites link
 * the real arbiter and exercise grant/stop/Flash races. */
static uint8_t arm_work_locked;
uint8_t app_control_arbiter_arm_locked(void) { return arm_work_locked; }
#include "../Device/aimtor_servo.c"
static servo_snapshot_t snapshot(servo_axis_t axis)
{ servo_snapshot_t value; assert(aimtor_get_snapshot(axis,&value)); return value; }
static unsigned fallback_calls;
void app_health_request_driver_fallback(uint32_t reason)
{ assert(reason); fallback_calls++; modbus_safety_silence(); }
int main(void)
{
    assert(APP_UNPROTECTED_MOTION_TEST_ENABLED == 0U);
    assert(modbus_init() == MODBUS_OK);
    motion_init(); aimtor_init();
    arm_work_locked=1U;
    assert(!aimtor_motion_allowed());
    assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT,100)==SERVO_NOT_READY);
    assert(aimtor_set_velocity_rpm(SERVO_AXIS_RIGHT,-100)==SERVO_NOT_READY);
    arm_work_locked=0U;
#if APP_SERVO_PULSE_ENABLED
    assert(aimtor_set_control_mode(SERVO_AXIS_LEFT, SERVO_CONTROL_PULSE_POSITION) == SERVO_NOT_CONFIGURED);
#else
    assert(aimtor_set_control_mode(SERVO_AXIS_LEFT, SERVO_CONTROL_PULSE_POSITION) == SERVO_UNSUPPORTED);
#endif
    assert(aimtor_set_control_mode(SERVO_AXIS_LEFT, SERVO_CONTROL_RS485_SEGMENT_POSITION) == SERVO_NOT_CONFIGURED);
    assert(servo_pulse_set_frequency(SERVO_AXIS_LEFT, 100U) == SERVO_PULSE_NOT_CONFIGURED);
    assert(aimtor_set_velocity_rpm(SERVO_AXIS_LEFT, 100) == SERVO_NOT_READY);
    aimtor_set_motion_authorized(true);
    assert(!aimtor_motion_allowed());
    /* Real task-independent safety takeover rejects late queued normal writes. */
    { modbus_request_t request; memset(&request,0,sizeof(request)); request.slave=1;
      request.function=6; request.address=AIMTOR_REG_SPEED_COMMAND; request.value=100;
      assert(modbus_submit(&request,0)==MODBUS_OK); modbus_safety_takeover();
      modbus_process(); assert(registers[1][AIMTOR_REG_SPEED_COMMAND]==0U);
      assert(modbus_submit(&request,0)==MODBUS_TRANSPORT_ERROR); }
    modbus_safety_release(); modbus_set_motion_permitted(true);
    /* Communication-stop escalation uses driver timeout, with no disable writes. */
    motion_request_immediate_safety_stop(MOTION_FAULT_REASON_OUTPUT_FAILURE, 1U);
    assert(fallback_calls==1 && !modbus_business_allowed());
    assert(!s_emergency_pending);
    aimtor_watchdog_recovery_init(1U);
    assert(aimtor_watchdog_recovery_ready());
    puts("PASS production guards: pulse/position blocked, motion gate, stale queue revoked, failure silence, legacy recovery excluded");
    return 0;
}
