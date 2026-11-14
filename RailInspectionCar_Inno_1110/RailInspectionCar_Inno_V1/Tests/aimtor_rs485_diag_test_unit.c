/* Test-only narrow access; production observer and driver remain unchanged. */
#include "aimtor_test_unit.c"
void aimtor_test_diag_change(uint8_t item,uint16_t value)
{
    switch(item) {
    case 0U:s_axes[0].snapshot.fault_code=value;break;
    case 1U:s_axes[0].snapshot.enable_feedback=(uint8_t)value;break;
    case 2U:s_axes[0].speed_valid=(uint8_t)value;break;
    case 3U:aimtor_feedback_valid(0U,0U,++s_transaction_id,xTaskGetTickCount());break;
    case 4U:s_axes[0].speed_tick=xTaskGetTickCount()-pdMS_TO_TICKS(value);break;
    default:assert(0);break;
    }
}
