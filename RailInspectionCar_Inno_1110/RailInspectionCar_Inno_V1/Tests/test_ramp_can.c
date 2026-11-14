#define main can_regression_main
#include "test_can_control.c"
#undef main
int main(void)
{
    const int targets[] = {500, 1000, 1600, -500, -1000, -1600};
    unsigned i;
    for (i = 0U; i < sizeof(targets)/sizeof(targets[0]); ++i)
    {
        uint32_t began, elapsed;
        unsigned axis = APP_SERVO_CONTROL_AXIS == 2U ? 1U : 0U;
        int16_t mm, target;
        double expected;
        reset(); acquire();
        mm = (int16_t)(targets[i] * app_can_control_rpm_mm_s(1, (uint8_t)axis));
        target = (int16_t)(mm / app_can_control_rpm_mm_s(1, (uint8_t)axis));
        expected = fabs((double)target) * APP_SERVO_SPEED_ACCELERATION_RAMP_RAW_MS / 1000.0;
        began = tick;
        for (elapsed = 0U; elapsed < 60000U; ++elapsed) {
            if (elapsed % 100U == 0U) assert(speed(mm, 300U));
            advance(1U, true);
            if (state.commanded_rpm[axis] == target) break;
        }
        printf("MEASURE CAN 0->%d: %lu ms axis=%u\n", target, (unsigned long)(tick-began), APP_SERVO_CONTROL_AXIS);
        assert(elapsed < 60000U && fabs((double)(tick-began) - expected) < 500.0);
        assert(state.authorized);
    }
    return 0;
}
