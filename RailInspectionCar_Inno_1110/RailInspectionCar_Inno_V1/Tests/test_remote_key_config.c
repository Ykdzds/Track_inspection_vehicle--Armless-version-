#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "app_config.h"

#ifndef TEST_EXPECT_K5_DECEL
#define TEST_EXPECT_K5_DECEL (0.60f)
#endif
#ifndef TEST_EXPECT_K6_ENABLED
#define TEST_EXPECT_K6_ENABLED (0U)
#endif
#ifndef TEST_EXPECT_RAMP_VALID
#define TEST_EXPECT_RAMP_VALID (1U)
#endif

int main(void)
{
    assert(APP_REMOTE_STOP_AUTH_KEY_INDEX == 4U);
    assert(APP_REMOTE_WATCHDOG_KEY_INDEX == 5U);
    assert(fabsf(APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 - TEST_EXPECT_K5_DECEL) < 0.00001f);
    assert(fabsf(APP_REMOTE_SERVO_K1_STOP_DECEL_MPS2 - TEST_EXPECT_K5_DECEL) < 0.00001f);
    assert(APP_WDG_TEST_K6_SHORT_ENABLED == TEST_EXPECT_K6_ENABLED);
    assert(APP_WDG_TEST_K2_SHORT_ENABLED == TEST_EXPECT_K6_ENABLED);
    assert(app_config_motion_ramp_valid() == TEST_EXPECT_RAMP_VALID);
    puts("PASS remote key configuration: fixed mapping, aliases and ramp validation");
    return 0;
}
