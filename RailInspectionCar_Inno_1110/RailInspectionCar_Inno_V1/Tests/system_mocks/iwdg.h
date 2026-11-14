#ifndef TEST_SYSTEM_IWDG_H
#define TEST_SYSTEM_IWDG_H
#include <stdint.h>
typedef struct { uint32_t CSR; } test_rcc_t;
extern test_rcc_t test_rcc;
extern unsigned reset_clear_count;
extern unsigned hiwdg;
#define RCC (&test_rcc)
#define RCC_CSR_IWDGRSTF (1UL << 29U)
#define __HAL_RCC_CLEAR_RESET_FLAGS() \
    ((void)(reset_clear_count++, test_rcc.CSR = 0U))
int HAL_IWDG_Refresh(unsigned *watchdog);
typedef struct { uint32_t BKP16R, BKP17R, BKP18R, BKP19R; } test_rtc_t;
extern test_rtc_t test_rtc;
#define RTC (&test_rtc)
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define HAL_PWR_EnableBkUpAccess() ((void)0)
#define __HAL_RCC_RTC_ENABLE() ((void)0)
#define __DSB() ((void)0)
#endif
