/* Production HMI + driver + Modbus timing, mocked hardware only. */
#define TEST_RAMP_DRIVER_TIMING 1
#define TEST_CLI_SESSION_ONLY 1
#define TEST_HMI_PRODUCTION 1
#define main core_regression_main
#include "test_core.c"
#undef main
#include "app_normal_ramp.h"
int main(void)
{
    const int16_t targets[] = {500, 1000, 1600, -500, -1000, -1600};
    unsigned i, axis;
    setbuf(stdout, NULL);
    motion_init(); assert(modbus_init() == MODBUS_OK);
    {
        extern void aimtor_test_stop_polling(void);
        aimtor_test_stop_polling();
    }
    for (i = 0U; i < sizeof(targets)/sizeof(targets[0]); ++i)
    {
        unsigned index;
        uint32_t session;
        TickType_t began;
        double expected = (targets[i] < 0 ? -targets[i] : targets[i]) *
            APP_SERVO_SPEED_ACCELERATION_RAMP_RAW_MS / 1000.0;
        timing_wire_delay = 0U; timing_control_while_waiting = 0U;
        cli_reset(0x0E000003UL); hmi_sequence = 0U;
        /* A legacy maintenance flag must not select the direct-CLI ramp for HMI. */
        if (!APP_SERVO_STARTUP_HOLD_ENABLED)
            assert(aimtor_set_maintenance_enabled(true) == SERVO_OK);
        hmi_send(1U, 0U, 0); cli_pump(30U);
        session = app_hmi_control_session(); assert(session);
        for (axis = 0U; axis < 2U; ++axis)
        {
            if (!app_config_servo_axis_selected((uint8_t)axis)) continue;
            assert(snapshot((servo_axis_t)axis).ramp_read_mask == 3U);
            assert(snapshot((servo_axis_t)axis).ramp_match_mask == 3U);
            assert(snapshot((servo_axis_t)axis).ramp_readback_ms[0] == 1U);
            assert(snapshot((servo_axis_t)axis).ramp_readback_ms[1] == APP_SERVO_SPEED_DECELERATION_RAMP_MS);
        }
        timing_wire_delay = 25U; /* Below the unchanged 50ms wire timeout. */
        timing_control_while_waiting = 1U;
        began = ticks;
        for (index = 0U; index < 60000U; ++index)
        {
            hmi_send(0x21U, session, targets[i]); cli_pump(1U);
            for (axis = 0U; axis < 2U; ++axis)
                registers[axis + 1U][AIMTOR_REG_ACTUAL_SPEED] = registers[axis + 1U][AIMTOR_REG_SPEED_COMMAND];
            assert(app_hmi_control_session() == session);
            {
                uint8_t reached = 1U;
                for (axis = 0U; axis < 2U; ++axis) {
                    servo_snapshot_t a = snapshot((servo_axis_t)axis);
                    if (app_config_servo_axis_selected((uint8_t)axis) &&
                        (!a.speed_write_rpm_valid || a.speed_write_rpm != targets[i])) reached = 0U;
                }
                if (reached) break;
            }
        }
        printf("MEASURE production HMI 0->%d: %lu ms axis=%u\n", targets[i], (unsigned long)(ticks-began), APP_SERVO_CONTROL_AXIS);
        assert(index < 60000U);
        {
            app_normal_ramp_status_t diagnostic;
            double tolerance;
            app_hmi_control_ramp_status(&diagnostic);
            printf("driver clock max=%lu clip=%lu drop=%lu batch_max=%lu internal=%d\n",
                (unsigned long)diagnostic.clock.max_service_ms, (unsigned long)diagnostic.clock.clipped,
                (unsigned long)diagnostic.clock.discarded_ms, (unsigned long)diagnostic.clock.max_batch_ms, diagnostic.internal[0]);
            tolerance = diagnostic.clock.max_batch_ms * 2.0;
            if (tolerance < 500.0) tolerance = 500.0;
            assert(fabs((double)(ticks - began) - expected) < tolerance);
        }
    }
    /* PB10 uses the production arbiter event, not a screen mailbox stop.
     * Keep delayed RS485 replies throughout deceleration and regrant. */
    {
        uint32_t old_session = app_hmi_control_session();
        unsigned index;
        assert(old_session);
        assert(app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_CAN, 5U,
            600U, 1U, 0x080U));
        for (index = 0U; index < 10000U && app_hmi_control_session(); ++index)
        {
            cli_pump(1U);
            for (axis = 0U; axis < 2U; ++axis)
                registers[axis + 1U][AIMTOR_REG_ACTUAL_SPEED] =
                    registers[axis + 1U][AIMTOR_REG_SPEED_COMMAND];
        }
        assert(!app_hmi_control_session() && index < 10000U);
        assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
        assert(app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_NONE);
        assert(!aimtor_safety_stop_active() && !aimtor_recovery_blocks_motion());
        hmi_send(1U, 0U, 0);
        for (index = 0U; index < 200U && !app_hmi_control_session(); ++index) cli_pump(1U);
        assert(app_hmi_control_session() && app_hmi_control_session() != old_session);
        puts("PASS production PB10 HMI stop with 25ms replies -> fresh session at zero");
    }
    timing_wire_delay = 0U; timing_control_while_waiting = 0U;
    hmi_production_tests();
    return 0;
}
