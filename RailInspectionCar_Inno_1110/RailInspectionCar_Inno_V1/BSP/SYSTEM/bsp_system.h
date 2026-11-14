#ifndef BSP_SYSTEM_H
#define BSP_SYSTEM_H

#include <stdint.h>

/* BKP16..19 remain exclusively owned by the existing safety latch. */
void bsp_system_safety_store(uint32_t state, uint32_t detail);
uint32_t bsp_system_safety_state(void);
uint32_t bsp_system_safety_detail(void);
void bsp_system_safety_clear(void);
uint32_t bsp_system_take_reset_reason(void);
void bsp_system_watchdog_refresh(void);
void bsp_system_store_fault(uint32_t fault_code, uint32_t detail);
uint32_t bsp_system_take_saved_fault(void);
uint32_t bsp_system_take_saved_fault_detail(void);
uint8_t bsp_system_was_iwdg_reset(void);
uint32_t bsp_system_get_reset_reason(void);
uint32_t bsp_system_get_iwdg_reset_count(void);

#define BSP_SYSTEM_FAULT_MAGIC          (0x464C5432UL)
#define BSP_SYSTEM_FAULT_VERSION        (1UL)
#define BSP_SYSTEM_FAULT_COMMIT         (0x434F4D54UL)
#define BSP_SYSTEM_FAULT_EXCEPTION       (0x80000000UL)
#define BSP_SYSTEM_FAULT_STACK_VALID     (1UL)
#define BSP_SYSTEM_FAULT_EXTENDED_FRAME  (2UL)
#define BSP_SYSTEM_EXCEPTION_STACK_BYTES (512U)

/* Exactly 128 bytes on every host; no pointers or compiler-dependent enums. */
typedef struct {
    uint32_t magic, version, length, boot, ms, reset_reason;
    uint32_t fault_code, detail, task_id, safety_phase;
    uint32_t exception_kind, exc_return, sp, pc, lr, xpsr;
    uint32_t cfsr, hfsr, dfsr, mmfar, bfar, afsr;
    uint32_t msp, psp, primask, basepri, control, flags;
    uint32_t reserved0, reserved1, crc, commit;
} bsp_system_fault_record_t;

/* Startup freezes the immediately preceding boot only. Cold/BOR resets invalidate SRAM. */
void bsp_system_fault_set_boot(uint32_t boot);
void bsp_system_fault_set_context(uint32_t task_id, uint32_t safety_phase);
uint8_t bsp_system_get_previous_fault(bsp_system_fault_record_t *record);
uint8_t bsp_system_get_current_fault(bsp_system_fault_record_t *record);
uint8_t bsp_system_get_previous_exception(bsp_system_fault_record_t *record);
uint8_t bsp_system_get_current_exception(bsp_system_fault_record_t *record);
/* Fault handlers must pass the ORIGINAL SP before switching to the emergency stack. */
void bsp_system_capture_exception(uint32_t *stack, uint32_t exc_return, uint32_t kind);
extern uint64_t bsp_system_exception_stack[BSP_SYSTEM_EXCEPTION_STACK_BYTES / 8U];

#ifdef BSP_SYSTEM_RETENTION_TEST
/* Test-only reset simulation: never present in the production image. */
void bsp_system_test_reset(uint32_t reset_reason);
void bsp_system_test_corrupt_current(uint32_t word, uint32_t mask);
void bsp_system_test_set_ms(uint32_t ms);
#endif

#endif
