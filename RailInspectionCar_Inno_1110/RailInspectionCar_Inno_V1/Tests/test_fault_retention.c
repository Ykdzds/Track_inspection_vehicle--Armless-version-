#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "iwdg.h"
#include "bsp_system.h"

test_rcc_t test_rcc;
test_rtc_t test_rtc;
unsigned reset_clear_count, hiwdg;
int HAL_IWDG_Refresh(unsigned *watchdog) { assert(watchdog == &hiwdg); return 0; }
static void expect_no_previous(void)
{
    bsp_system_fault_record_t r;
    assert(!bsp_system_get_previous_fault(&r));
    assert(!bsp_system_get_previous_exception(&r));
}
int main(void)
{
    bsp_system_fault_record_t r, e;
    uint32_t saved_safety[4];
    uint32_t *stack;
    assert(sizeof(r) == 128U);
    bsp_system_test_reset(1UL << 27U); /* POR */
    expect_no_previous();
    bsp_system_safety_store(2U, 0x12345678UL);
    memcpy(saved_safety, &test_rtc, sizeof(saved_safety));
    bsp_system_fault_set_boot(1524U);
    bsp_system_fault_set_context(3U, 1U);
    bsp_system_test_set_ms(138823U);
    bsp_system_store_fault(0x80U, 3U);
    assert(bsp_system_get_current_fault(&r));
    assert(r.boot == 1524U && r.ms == 138823U && r.task_id == 3U && r.safety_phase == 1U);
    assert(r.fault_code == 0x80U && r.detail == 3U);
    /* A later fallback must not erase the first diagnostic cause. */
    bsp_system_store_fault(0x400U, 14U);
    assert(bsp_system_get_current_fault(&r) && r.fault_code == 0x80U);
    assert(bsp_system_take_saved_fault() == 0x400U); /* legacy current-RAM API */
    /* Invalid stack address never gets dereferenced, exception is independently retained. */
    bsp_system_capture_exception((uint32_t *)(uintptr_t)3U, 0xFFFFFFFDU, 3U);
    assert(bsp_system_get_current_exception(&e));
    assert(e.exception_kind == 3U && e.sp == 3U && !(e.flags & BSP_SYSTEM_FAULT_STACK_VALID));
    bsp_system_capture_exception((uint32_t *)(uintptr_t)0U, 0xFFFFFFEDU, 4U);
    assert(bsp_system_get_current_exception(&e) && e.exception_kind == 3U);
    bsp_system_test_reset(1UL << 29U); /* IWDG */
    assert(bsp_system_get_previous_fault(&r) && r.boot == 1524U && r.fault_code == 0x80U);
    assert(bsp_system_get_previous_exception(&e) && e.exception_kind == 3U);
    assert(!bsp_system_get_current_fault(&r));
    assert(memcmp(saved_safety, &test_rtc, sizeof(saved_safety)) == 0);
    /* The preceding boot had no fault: do not repeatedly attribute an older one. */
    bsp_system_test_reset(1UL << 28U); /* software */
    expect_no_previous();
    /* Reject corruption, including torn commit/header/version. */
    bsp_system_store_fault(1U, 2U);
    bsp_system_test_corrupt_current(7U, 1U);
    bsp_system_test_reset(1UL << 29U);
    expect_no_previous();
    bsp_system_store_fault(1U, 2U);
    bsp_system_test_corrupt_current(31U, 1U);
    bsp_system_test_reset(1UL << 26U);
    expect_no_previous();
    bsp_system_store_fault(1U, 2U);
    bsp_system_test_corrupt_current(1U, 1U);
    bsp_system_test_reset(1UL << 30U);
    expect_no_previous();
    /* BOR/POR wins even when a stale warm-reset bit is also set. */
    bsp_system_store_fault(1U, 2U);
    bsp_system_test_reset((1UL << 25U) | (1UL << 29U));
    expect_no_previous();
    bsp_system_store_fault(1U, 2U);
    bsp_system_test_reset((1UL << 27U) | (1UL << 26U));
    expect_no_previous();
    bsp_system_store_fault(1U, 2U);
    bsp_system_test_reset(0U);
    expect_no_previous();
    /* Exercise the actual Cortex SRAM range check with readable fixed-address pages. */
    stack = VirtualAlloc((void *)(uintptr_t)0x20000000UL, 4096U,
                         MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(stack == (void *)(uintptr_t)0x20000000UL);
    memset(stack, 0, 4096U);
    stack[5] = 0x08001235U; stack[6] = 0x08002346U; stack[7] = 0x21000000U;
    bsp_system_capture_exception(stack, 0xFFFFFFFDU, 3U);
    assert(bsp_system_get_current_exception(&e));
    assert(e.pc == 0x08002346U && e.lr == 0x08001235U && e.xpsr == 0x21000000U);
    assert(e.flags == BSP_SYSTEM_FAULT_STACK_VALID);
    bsp_system_test_reset(1UL << 28U);
    assert(bsp_system_get_previous_exception(&e) && e.pc == 0x08002346U);
    stack[5] = 0x08003457U; stack[6] = 0x08004568U; stack[7] = 0x21000000U;
    stack[23] = 0x11111111U; stack[24] = 0x22222222U; stack[25] = 0x33333333U;
    bsp_system_capture_exception(stack, 0xFFFFFFEDU, 6U);
    assert(bsp_system_get_current_exception(&e) && e.pc == 0x08004568U);
    assert(e.flags == (BSP_SYSTEM_FAULT_STACK_VALID | BSP_SYSTEM_FAULT_EXTENDED_FRAME));
    assert(VirtualFree(stack, 0U, MEM_RELEASE));
    assert(!bsp_system_get_current_fault(NULL));
    assert(!bsp_system_get_previous_exception(NULL));
    assert(memcmp(saved_safety, &test_rtc, sizeof(saved_safety)) == 0);
    puts("PASS retained diagnostics: warm/cold reset, first fault + exception, CRC/torn record, invalid stack, safety BKP isolation");
    return 0;
}
