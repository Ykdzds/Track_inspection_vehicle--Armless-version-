#include <stdint.h>
#include <math.h>
#define NORMAL_TIMING_TEST 1
static uint32_t timing_delay, timing_due[2];
static int16_t timing_rpm[2];
#define main existing_remote_regression_main
#include "test_remote_servo.c"
#undef main
#include "app_motion_ramp.h"

static void setup(nrf_remote_snapshot_t *input)
{
    unsigned axis;
    memset(axes, 0, sizeof(axes)); memset(input, 0, sizeof(*input)); memset(&debug, 0, sizeof(debug));
    timing_delay = 0U; hold_response = prepare_fail_mask = enable_fail_mask = 0U;
    startup_hold = 0U; hold_ready = boot_ready = 1U;
    for (axis = 0U; axis < 2U; ++axis) {
        axes[axis].online = axes[axis].enabled = axes[axis].parameter_verified = 1U;
        axes[axis].control_mode = SERVO_CONTROL_RS485_VELOCITY;
    }
    input->link_up = 1U; reset_remote(); arm(input); assert(status().authorized);
}

static int actual_zero(void)
{
    unsigned axis;
    for (axis = 0U; axis < 2U; ++axis)
        if ((APP_SERVO_CONTROL_AXIS & (1U << axis)) && axes[axis].actual_speed_rpm != 0) return 0;
    return 1;
}

static void reversal_interruptions(void)
{
    unsigned scenario, direction, step, axis;
    for(direction=0U;direction<2U;++direction) for(scenario=0U;scenario<6U;++scenario) {
        nrf_remote_snapshot_t input;
        int sign=direction?1:-1, target, saw_zero=0;
        setup(&input); timing_delay=60U;
        input.left_vertical=joystick_for_rpm(sign*700);
        for(step=0U;step<20000U;++step) {++ticks;feed(&input);}
        input.left_vertical=joystick_for_rpm(-sign*1550);
        for(step=0U;step<3000U;++step) {++ticks;feed(&input);}
        assert(axes[TEST_AXIS].actual_speed_rpm*sign>0);
        target=scenario==0U?0:scenario==1U?sign*400:-sign*400;
        input.left_vertical=target?joystick_for_rpm(target):1000U;
        if(scenario==3U) ++input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX];
        if(scenario==4U) input.link_up=0U;
        if(scenario==5U) app_remote_servo_request_estop();
        for(step=0U;step<25000U;++step) {
            uint32_t seq[2]={axes[0].speed_write_sequence,axes[1].speed_write_sequence};
            ++ticks;feed(&input);if(actual_zero())saw_zero=1;
            for(axis=0U;axis<2U;++axis) {
                if(!(APP_SERVO_CONTROL_AXIS&(1U<<axis)))continue;
                if(seq[axis]!=axes[axis].speed_write_sequence && timing_rpm[axis]*sign<0)assert(saw_zero);
            }
        }
        if(scenario<3U) {assert(status().authorized);assert(abs(axes[TEST_AXIS].actual_speed_rpm-target)<=2);}
        else {
            assert(!status().authorized);
            if(scenario==5U) {for(axis=0U;axis<2U;++axis) if(APP_SERVO_CONTROL_AXIS&(1U<<axis))assert(!axes[axis].enabled);}
            else assert(actual_zero());
        }
    }
    puts("PASS reversal interruptions: both directions; neutral, restored direction, changed opposite target, K5, link loss and estop supersede ramp");
}

int main(int argc, char **argv)
{
    static const unsigned delays[] = {30U,60U,100U,60U};
    unsigned d, direction, failures = 0U, axis;
    (void)argv; setbuf(stdout,NULL);
    for (d = 0U; d < 4U; ++d) for (direction = 0U; direction < 2U; ++direction)
    {
        nrf_remote_snapshot_t input;
        uint32_t step, began, zero = 0U, reached = 0U;
        int sign = direction ? 1 : -1, initial, saw_zero = 0;
        double theory;
        setup(&input); timing_delay = delays[d];
        input.left_vertical = joystick_for_rpm(sign * 700);
        for (step = 0U; step < 20000U; ++step) { ++ticks; feed(&input); }
        initial = axes[TEST_AXIS].actual_speed_rpm;
        assert(abs(initial - sign * 700) <= 2);
        input.left_vertical = joystick_for_rpm(-sign * 1550); began = ticks;
        for (step = 0U; step < 65000U; ++step)
        {
            uint32_t sequences[2] = {axes[0].speed_write_sequence,axes[1].speed_write_sequence};
            if (d == 3U) timing_delay = (step / 17U % 2U) ? 30U : 100U;
            ++ticks; feed(&input); assert(status().authorized);
            if (!zero && actual_zero()) { zero = ticks - began; saw_zero = 1; }
            for (axis = 0U; axis < 2U; ++axis) {
                if (!(APP_SERVO_CONTROL_AXIS & (1U << axis))) continue;
                if (axes[axis].speed_write_sequence != sequences[axis] && timing_rpm[axis] * sign < 0)
                    assert(saw_zero); /* New direction cannot precede real feedback zero. */
            }
            if (zero && axes[TEST_AXIS].actual_speed_rpm * -sign >= 700)
            { reached = ticks - began - zero; break; }
        }
        theory = abs(initial) * 1000.0 / (APP_DESIGN_DECELERATION_MPS2 * app_motion_ramp_motor_rpm_per_mps());
        printf("REVERSAL axis=%u sync=%u delay=%u sign=%d zero=%lu theory=%.0f zero_to_700=%lu\n",
            APP_SERVO_CONTROL_AXIS,APP_REMOTE_SERVO_SYNC_ENABLED,delays[d],sign,(unsigned long)zero,theory,(unsigned long)reached);
        if (!zero || fabs((double)zero-theory)>500.0 || !reached || fabs((double)reached-16952.0)>500.0) ++failures;
    }
    if(argc<=1)reversal_interruptions();
    return argc > 1 ? 0 : failures != 0U;
}
