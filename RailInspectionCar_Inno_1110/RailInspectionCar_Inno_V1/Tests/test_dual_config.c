#include <assert.h>
#include <stdio.h>
#include "app_config.h"
int main(void)
{
    assert((int)app_config_motion_ramp_valid()==TEST_DUAL_CONFIG_VALID);
    puts("PASS dual-axis optional-mode and mechanical configuration gate");
    return 0;
}
