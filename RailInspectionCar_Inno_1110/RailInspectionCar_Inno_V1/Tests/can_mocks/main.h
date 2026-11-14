#ifndef TEST_CAN_MAIN_H
#define TEST_CAN_MAIN_H
#include <stdint.h>
typedef struct { uint32_t DEMCR; } test_core_debug_t;
typedef struct { uint32_t CTRL, CYCCNT; } test_dwt_t;
extern test_core_debug_t test_core_debug;
extern test_dwt_t test_dwt;
extern uint32_t SystemCoreClock;
#define CoreDebug (&test_core_debug)
#define DWT (&test_dwt)
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24U)
#define DWT_CTRL_CYCCNTENA_Msk 1U
uint32_t HAL_GetTick(void);
#endif
