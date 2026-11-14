#include "app_rtos_task.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    unsigned remote;
    unsigned hmi;
    unsigned can;

    for (remote = 0U; remote <= 1U; ++remote)
        for (hmi = 0U; hmi <= 1U; ++hmi)
            for (can = 0U; can <= 1U; ++can)
                assert(app_status_motion_authorized((uint8_t)remote,
                    (uint8_t)hmi, (uint8_t)can) ==
                    (uint8_t)(remote || hmi || can));
    puts("PASS status LED unified authorization truth table");
    return 0;
}
