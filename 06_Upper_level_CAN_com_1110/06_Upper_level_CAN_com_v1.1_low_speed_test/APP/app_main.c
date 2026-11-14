#include "app_main.h"
#include "can_upper_task.h"
void app_init(void)
{
    UpperApp_Init();
}
void freertos_taskstart(void)
{
    UpperApp_Start();
}
