#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "iwdg.h"
#include "bsp_system.h"
test_rcc_t test_rcc;
test_rtc_t test_rtc;
unsigned reset_clear_count, hiwdg;
int HAL_IWDG_Refresh(unsigned *watchdog)
{
    assert(watchdog == &hiwdg);
    return 0;
}
int main(int argc, char **argv)
{
    uint32_t reason;
    assert(argc == 2);
    reason = (uint32_t)strtoul(argv[1], NULL, 0);
    test_rcc.CSR = reason;
    assert(bsp_system_take_reset_reason() == reason);
    assert(reset_clear_count == 1U && test_rcc.CSR == 0U);
    assert(bsp_system_was_iwdg_reset() == ((reason & RCC_CSR_IWDGRSTF) != 0U));
    assert(bsp_system_get_iwdg_reset_count() == bsp_system_was_iwdg_reset());
    assert(bsp_system_get_reset_reason() == reason);
    assert(bsp_system_take_saved_fault() == 0U);
    /* 后续健康模块再次调用，不能覆盖启动原因或再次清除硬件标志。 */
    test_rcc.CSR = ~reason;
    assert(bsp_system_take_reset_reason() == reason && reset_clear_count == 1U);
    bsp_system_store_fault(123U, 456U);
    assert(bsp_system_take_saved_fault() == 123U);
    assert(bsp_system_take_saved_fault_detail() == 456U);
    bsp_system_watchdog_refresh();
    assert(bsp_system_safety_state() == 0U);
    bsp_system_safety_store(2U, 0x12345678UL);
    assert(bsp_system_safety_state() == 2U && bsp_system_safety_detail() == 0x12345678UL);
    test_rtc.BKP18R ^= 1U;
    assert(bsp_system_safety_state() == 0U);
    bsp_system_safety_store(3U, 7U);
    assert(bsp_system_safety_state() == 3U);
    bsp_system_safety_clear();
    assert(bsp_system_safety_state() == 0U);
    puts("PASS reset: capture once, IWDG flag, RAM diagnostics, checked backup safety latch");
    return 0;
}
