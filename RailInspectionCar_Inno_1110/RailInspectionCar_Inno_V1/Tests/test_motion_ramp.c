#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "app_config.h"
#include "app_motion_ramp.h"
#include "app_normal_ramp.h"

static void test_acceleration_and_deceleration(void)
{
    app_motion_ramp_t ramp = {0.0f, 0U};
    int16_t value;
    value = app_motion_ramp_step(&ramp, 600, 50U, 0.2f, 0.3f);
    assert(value == 3);
    value = app_motion_ramp_step(&ramp, 600, 50U, 0.2f, 0.3f);
    assert(value == 6);
    value = app_motion_ramp_step(&ramp, 0, 50U, 0.2f, 0.3f);
    assert(value == 1);
}

static void test_fractional_accumulation_and_target_bound(void)
{
    app_motion_ramp_t ramp = {0.0f, 0U};
    int16_t value = 0;
    unsigned index;
    for (index = 0U; index < 10U; index++)
        value = app_motion_ramp_step(&ramp, 1, 10U, 0.2f, 0.3f);
    assert(value == 1);
    value = app_motion_ramp_step(&ramp, 0, 1000U, 0.2f, 0.3f);
    assert(value == 0);
    assert(fabsf(ramp.current_rpm) < 0.001f);
}

static void test_negative_symmetry_and_reverse_zero(void)
{
    app_motion_ramp_t ramp = {0.0f, 0U};
    int16_t value;
    value = app_motion_ramp_step(&ramp, -600, 50U, 0.2f, 0.3f);
    assert(value == -3);
    value = app_motion_ramp_step(&ramp, -600, 50U, 0.2f, 0.3f);
    assert(value == -6);
    value = app_motion_ramp_step(&ramp, 600, 50U, 100.0f, 0.3f);
    assert(value == -1);
    value = app_motion_ramp_step(&ramp, 600, 50U, 100.0f, 0.3f);
    assert(value == 0);
    value = app_motion_ramp_step(&ramp, 600, 50U, 0.2f, 0.3f);
    assert(value == 3);
}

static void test_delayed_period_limit_and_reset(void)
{
    app_motion_ramp_t ramp = {0.0f, 0U};
    int16_t value;
    value = app_motion_ramp_step(&ramp, 600, 50U, 0.2f, 0.3f);
    assert(value == 3);
    value = app_motion_ramp_step(&ramp, 600, 50U, 0.2f, 0.3f);
    assert(value == 6);
    value = app_motion_ramp_step(&ramp, 600, 5000U, 0.2f, 0.3f);
    assert(value == 8);
    app_motion_ramp_clear(&ramp);
    value = app_motion_ramp_step(&ramp, -1, 0U, 0.2f, 0.3f);
    assert(value == 0);
    assert(ramp.initialized == 1U);
}

static void test_axis_specific_conversion(void)
{
    app_motion_ramp_t ramp = {0.0f, 0U};
    int16_t value = app_motion_ramp_step_rate(
        &ramp, 100, 1000U, 0.2f, 0.3f, 200.0f);
    assert(value == 2);
}

static void test_vehicle_speed_conversion_and_limit(void)
{
    assert(app_motion_ramp_vehicle_mm_s_to_motor_rpm(APP_OPERATION_MAX_VELOCITY_MM_S) ==
           APP_OPERATION_MAX_MOTOR_RPM);
    assert(app_motion_ramp_vehicle_mm_s_to_motor_rpm(-APP_OPERATION_MAX_VELOCITY_MM_S) ==
           -APP_OPERATION_MAX_MOTOR_RPM);
    assert(app_motion_ramp_vehicle_mm_s_to_motor_rpm(6000) ==
           APP_OPERATION_MAX_MOTOR_RPM);
    assert(app_motion_ramp_motor_rpm_to_vehicle_mm_s(200) >= 726 &&
           app_motion_ramp_motor_rpm_to_vehicle_mm_s(200) <= 727);
    assert(app_motion_ramp_motor_rpm_to_vehicle_mm_s(APP_OPERATION_MAX_MOTOR_RPM) >= 5811 &&
           app_motion_ramp_motor_rpm_to_vehicle_mm_s(APP_OPERATION_MAX_MOTOR_RPM) <= 5813);
    assert(app_motion_ramp_vehicle_mm_s_to_motor_rpm(APP_OPERATION_MAX_VELOCITY_MM_S + 1) ==
           APP_OPERATION_MAX_MOTOR_RPM);
}

static void test_production_duration(void)
{
    const int16_t targets[] = {500, 1000, 1600, -500, -1000, -1600};
    const uint32_t periods[] = {1U, 10U, 20U, 50U};
    unsigned i, j;
    assert(APP_SERVO_SPEED_ACCELERATION_RAMP_MS == APP_SERVO_DRIVER_ACCEL_GUARD_MS);
    assert(APP_SERVO_DRIVER_ACCEL_GUARD_MS == 1U);
    for (i = 0U; i < sizeof(targets)/sizeof(targets[0]); ++i)
        for (j = 0U; j < sizeof(periods)/sizeof(periods[0]); ++j)
        {
            app_motion_ramp_t ramp = {0.0f, 0U};
            uint32_t elapsed = 0U;
            double expected = fabs((double)targets[i]) / (APP_DESIGN_ACCELERATION_MPS2 *
                app_motion_ramp_motor_rpm_per_mps()) * 1000.0;
            int16_t output = 0;
            while (output != targets[i] && elapsed < 100000U)
            {
                output = app_motion_ramp_step(&ramp, targets[i], periods[j],
                    APP_DESIGN_ACCELERATION_MPS2, APP_DESIGN_DECELERATION_MPS2);
                elapsed += periods[j];
            }
            assert(fabs((double)elapsed - expected) < 80.0);
        }
    puts("PASS production duration: +/-500/1000/1600 rpm, 1/10/20/50ms, single software ramp");
}

static void test_normal_clock(void)
{
    app_normal_ramp_clock_t clock;
    app_normal_ramp_begin(&clock, 0xFFFFFFF0U);
    assert(app_normal_ramp_elapsed(&clock, 0xFFFFFFF0U) == 0U);
    assert(app_normal_ramp_elapsed(&clock, 4U) == 20U);
    assert(app_normal_ramp_elapsed(&clock, 1004U) == 50U);
    assert(clock.clipped == 1U && clock.discarded_ms == 950U);
    assert(app_normal_ramp_elapsed(&clock, 1005U) == 1U);
    assert(clock.integrated_ms == 71U); /* no debt or catch-up */
    app_normal_ramp_boundary(&clock, 2000U);
    assert(app_normal_ramp_elapsed(&clock, 2001U) == 0U);
    assert(app_normal_ramp_elapsed(&clock, 2002U) == 1U);
}

int main(void)
{
    assert(app_config_motion_ramp_valid());
    test_normal_clock();
    test_production_duration();
    test_acceleration_and_deceleration();
    test_fractional_accumulation_and_target_bound();
    test_negative_symmetry_and_reverse_zero();
    test_delayed_period_limit_and_reset();
    test_axis_specific_conversion();
    test_vehicle_speed_conversion_and_limit();
    puts("PASS motion ramp: physical acceleration, accumulation, symmetry, reverse and dt limit");
    return 0;
}
