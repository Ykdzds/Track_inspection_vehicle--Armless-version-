#define main remote_regression_main
#include "test_remote_servo.c"
#undef main
#include <math.h>
int main(void)
{
    const int targets[] = {500, 1000, 1600, -500, -1000, -1600};
    unsigned i, axis, n;
    nrf_remote_snapshot_t input;
    for (i = 0U; i < sizeof(targets)/sizeof(targets[0]); ++i)
    {
        TickType_t began;
        int target;
        double expected;
        memset(axes, 0, sizeof(axes)); memset(&input, 0, sizeof(input));
        memset(&debug, 0, sizeof(debug));
        startup_hold = 0U; hold_ready = boot_ready = 1U;
        hold_response = prepare_fail_mask = enable_fail_mask = 0U;
        for (axis = 0U; axis < 2U; ++axis) {
            axes[axis].online = axes[axis].enabled = axes[axis].parameter_verified = 1U;
            axes[axis].control_mode = SERVO_CONTROL_RS485_VELOCITY;
        }
        input.link_up = 1U; reset_remote(); arm(&input);
        assert(status().authorized);
        input.left_vertical = joystick_for_rpm(targets[i]);
        ticks += 10U; feed(&input); target = status().target_rpm;
        began = ticks;
        expected = abs(target) * APP_SERVO_SPEED_ACCELERATION_RAMP_RAW_MS / 1000.0;
        for (n = 0U; n < 60000U; ++n) {
            ticks += 1U; feed(&input);
            if (status().commanded_rpm == target) break;
        }
        printf("MEASURE remote 0->%d: %lu ms axis=%u sync=%u\n", target,
            (unsigned long)(ticks-began), APP_SERVO_CONTROL_AXIS, APP_REMOTE_SERVO_SYNC_ENABLED);
        assert(n < 60000U && fabs((double)(ticks-began) - expected) < 500.0);
        assert(status().authorized);
    }
    return 0;
}
