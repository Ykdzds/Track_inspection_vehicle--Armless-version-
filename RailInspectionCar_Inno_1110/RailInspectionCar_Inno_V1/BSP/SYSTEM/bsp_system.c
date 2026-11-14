#include "bsp_system.h"
#include "iwdg.h"
#include <stddef.h>

#define FAULT_COLD_RESET_MASK ((1UL << 25U) | (1UL << 27U))
#define FAULT_WARM_RESET_MASK ((1UL << 26U) | (0xFUL << 28U))
#define FAULT_UNKNOWN (0xFFFFFFFFUL)

typedef struct {
    bsp_system_fault_record_t previous_fault, previous_exception;
    bsp_system_fault_record_t current_fault, current_exception;
} fault_retention_t;
typedef char fault_record_size_must_be_128[(sizeof(bsp_system_fault_record_t) == 128U) ? 1 : -1];
typedef char fault_area_size_must_be_512[(sizeof(fault_retention_t) == 512U) ? 1 : -1];
#if defined(__CC_ARM)
/* The checked-in scatter file marks this input section UNINIT. */
__attribute__((section("FAULT_RETAIN"), zero_init)) static volatile fault_retention_t s_fault_retention;
#else
static volatile fault_retention_t s_fault_retention;
#endif
/* Fault entry switches MSP here before calling any C code. */
uint64_t bsp_system_exception_stack[BSP_SYSTEM_EXCEPTION_STACK_BYTES / 8U];

static uint32_t s_saved_fault, s_saved_fault_detail, s_reset_reason;
static uint32_t s_boot, s_task_id = FAULT_UNKNOWN, s_safety_phase;
static uint8_t s_was_iwdg_reset, s_reset_captured;
static volatile uint8_t s_fault_claim, s_exception_claim;
#ifdef BSP_SYSTEM_RETENTION_TEST
static uint32_t s_test_ms;
#endif
static uint32_t fault_ms(void)
{
#ifdef BSP_SYSTEM_RETENTION_TEST
    return s_test_ms;
#elif defined(__CC_ARM) || defined(__arm__) || defined(__thumb__)
    return HAL_GetTick();
#else
    return 0U;
#endif
}
static void fault_barrier(void)
{ __DSB(); }
/* Only the claim word is protected; CRC and record copying remain preemptible. */
static uint8_t claim(volatile uint8_t *claimed)
{
    uint8_t result = 0U;
#if defined(__CC_ARM) || defined(__arm__) || defined(__thumb__)
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
#endif
    if (*claimed == 0U) { *claimed = 1U; result = 1U; }
#if defined(__CC_ARM) || defined(__arm__) || defined(__thumb__)
    __set_PRIMASK(mask);
#endif
    return result;
}
static uint32_t fault_crc(const volatile bsp_system_fault_record_t *record)
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)record;
    uint32_t crc = 0xFFFFFFFFUL;
    unsigned i, bit;
    for (i = 0U; i < offsetof(bsp_system_fault_record_t, crc); ++i) {
        crc ^= bytes[i];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return ~crc;
}
static uint8_t valid(const volatile bsp_system_fault_record_t *record)
{
    return record->commit == BSP_SYSTEM_FAULT_COMMIT &&
        record->magic == BSP_SYSTEM_FAULT_MAGIC &&
        record->version == BSP_SYSTEM_FAULT_VERSION &&
        record->length == sizeof(*record) && record->crc == fault_crc(record);
}
static void clear_record(volatile bsp_system_fault_record_t *record)
{
    volatile uint32_t *words = (volatile uint32_t *)record;
    unsigned i;
    record->commit = 0U;
    fault_barrier();
    for (i = 0U; i < sizeof(*record) / 4U; ++i) words[i] = 0U;
}
static void copy_record(volatile bsp_system_fault_record_t *to,
                        const volatile bsp_system_fault_record_t *from)
{
    const volatile uint32_t *source = (const volatile uint32_t *)from;
    volatile uint32_t *target = (volatile uint32_t *)to;
    unsigned i;
    to->commit = 0U;
    fault_barrier();
    for (i = 0U; i < offsetof(bsp_system_fault_record_t, commit) / 4U; ++i)
        target[i] = source[i];
    fault_barrier();
    to->commit = BSP_SYSTEM_FAULT_COMMIT;
    fault_barrier();
}
static void begin_record(volatile bsp_system_fault_record_t *record,
                         uint32_t code, uint32_t detail)
{
    clear_record(record);
    record->magic = BSP_SYSTEM_FAULT_MAGIC;
    record->version = BSP_SYSTEM_FAULT_VERSION;
    record->length = sizeof(*record);
    record->boot = s_boot;
    record->ms = fault_ms();
    record->reset_reason = s_reset_reason;
    record->fault_code = code;
    record->detail = detail;
    record->task_id = s_task_id;
    record->safety_phase = s_safety_phase;
}
static void commit_record(volatile bsp_system_fault_record_t *record)
{
    record->crc = fault_crc(record);
    fault_barrier();
    record->commit = BSP_SYSTEM_FAULT_COMMIT;
    fault_barrier();
}
static void retention_startup(uint32_t reset_reason)
{
    uint8_t warm = (reset_reason & FAULT_COLD_RESET_MASK) == 0U &&
                   (reset_reason & FAULT_WARM_RESET_MASK) != 0U;
    if (warm && valid(&s_fault_retention.current_fault))
        copy_record(&s_fault_retention.previous_fault, &s_fault_retention.current_fault);
    else clear_record(&s_fault_retention.previous_fault);
    if (warm && valid(&s_fault_retention.current_exception))
        copy_record(&s_fault_retention.previous_exception, &s_fault_retention.current_exception);
    else clear_record(&s_fault_retention.previous_exception);
    clear_record(&s_fault_retention.current_fault);
    clear_record(&s_fault_retention.current_exception);
    s_fault_claim = s_exception_claim = 0U;
    s_boot = 0U;
    s_task_id = FAULT_UNKNOWN;
    s_safety_phase = 0U;
}
uint32_t bsp_system_take_reset_reason(void)
{
    if (s_reset_captured != 0U) return s_reset_reason;
    s_reset_reason = RCC->CSR;
    s_reset_captured = 1U;
    s_was_iwdg_reset = (s_reset_reason & RCC_CSR_IWDGRSTF) != 0U ? 1U : 0U;
    retention_startup(s_reset_reason);
    s_saved_fault = s_saved_fault_detail = 0U;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    return s_reset_reason;
}
void bsp_system_watchdog_refresh(void)
{ (void)HAL_IWDG_Refresh(&hiwdg); }
void bsp_system_fault_set_boot(uint32_t boot)
{ s_boot = boot; }
void bsp_system_fault_set_context(uint32_t task_id, uint32_t safety_phase)
{ s_task_id = task_id; s_safety_phase = safety_phase; }
void bsp_system_store_fault(uint32_t fault_code, uint32_t detail)
{
    s_saved_fault = fault_code;
    s_saved_fault_detail = detail;
    if (fault_code != 0U && claim(&s_fault_claim)) {
        begin_record(&s_fault_retention.current_fault, fault_code, detail);
        commit_record(&s_fault_retention.current_fault);
    }
}
uint32_t bsp_system_take_saved_fault(void) { return s_saved_fault; }
uint32_t bsp_system_take_saved_fault_detail(void) { return s_saved_fault_detail; }
uint32_t bsp_system_get_iwdg_reset_count(void) { return (uint32_t)s_was_iwdg_reset; }
uint8_t bsp_system_was_iwdg_reset(void) { return s_was_iwdg_reset; }
uint32_t bsp_system_get_reset_reason(void) { return s_reset_reason; }
static uint8_t get_record(bsp_system_fault_record_t *out,
                          const volatile bsp_system_fault_record_t *source)
{
    unsigned i;
    uint32_t *target;
    const volatile uint32_t *from;
    if (out == NULL) return 0U;
    target = (uint32_t *)out;
    if (!valid(source)) {
        for (i = 0U; i < sizeof(*out) / 4U; ++i) target[i] = 0U;
        return 0U;
    }
    from = (const volatile uint32_t *)source;
    for (i = 0U; i < sizeof(*out) / 4U; ++i) target[i] = from[i];
    /* A writer publishes only once, but verify the copied record as well. */
    return valid(out);
}
uint8_t bsp_system_get_previous_fault(bsp_system_fault_record_t *out)
{
    if (get_record(out, &s_fault_retention.previous_fault)) return 1U;
    return get_record(out, &s_fault_retention.previous_exception);
}
uint8_t bsp_system_get_current_fault(bsp_system_fault_record_t *out)
{
    if (get_record(out, &s_fault_retention.current_fault)) return 1U;
    return get_record(out, &s_fault_retention.current_exception);
}
uint8_t bsp_system_get_previous_exception(bsp_system_fault_record_t *out)
{ return get_record(out, &s_fault_retention.previous_exception); }
uint8_t bsp_system_get_current_exception(bsp_system_fault_record_t *out)
{ return get_record(out, &s_fault_retention.current_exception); }
/* Includes all SRAM and CCM RAM, excludes the no-init record itself. */
static uint8_t stack_range(uintptr_t address, uint32_t bytes)
{
    if ((address & 3U) != 0U) return 0U;
    if (address >= 0x20000000UL && address <= 0x2001FE00UL - bytes) return 1U;
    if (address >= 0x10000000UL && address <= 0x10010000UL - bytes) return 1U;
    return 0U;
}
void bsp_system_capture_exception(uint32_t *stack, uint32_t exc_return, uint32_t kind)
{
    volatile bsp_system_fault_record_t *record = &s_fault_retention.current_exception;
    uintptr_t address = (uintptr_t)stack;
    uint32_t frame_words = (exc_return & 0x10U) ? 8U : 26U;
    if (!claim(&s_exception_claim)) return;
    begin_record(record, BSP_SYSTEM_FAULT_EXCEPTION | kind, kind);
    record->exception_kind = kind;
    record->exc_return = exc_return;
    record->sp = (uint32_t)address;
#if defined(__CC_ARM) || defined(__arm__) || defined(__thumb__)
    record->cfsr = SCB->CFSR; record->hfsr = SCB->HFSR; record->dfsr = SCB->DFSR;
    record->mmfar = SCB->MMFAR; record->bfar = SCB->BFAR; record->afsr = SCB->AFSR;
    record->msp = (exc_return & 4U) ? __get_MSP() : (uint32_t)address;
    record->psp = (exc_return & 4U) ? (uint32_t)address : __get_PSP();
    record->primask = __get_PRIMASK(); record->basepri = __get_BASEPRI();
    record->control = __get_CONTROL();
#endif
    if (frame_words == 26U) record->flags |= BSP_SYSTEM_FAULT_EXTENDED_FRAME;
    /* Stacking errors mean even an in-range address may not contain a frame. */
    if ((record->cfsr & ((1UL << 3U) | (1UL << 4U) | (1UL << 5U) |
                        (1UL << 11U) | (1UL << 12U) | (1UL << 13U))) == 0U &&
        stack_range(address, frame_words * 4U)) {
        /* Cortex-M4F basic registers are first for BOTH frame types.  The
         * additional S0..S15/FPSCR/reserved words follow xPSR, not precede r0. */
        const volatile uint32_t *core = (const volatile uint32_t *)address;
        record->lr = core[5]; record->pc = core[6]; record->xpsr = core[7];
        record->flags |= BSP_SYSTEM_FAULT_STACK_VALID;
    }
    commit_record(record);
}
#ifdef BSP_SYSTEM_RETENTION_TEST
void bsp_system_test_reset(uint32_t reset_reason)
{
    s_reset_captured = 0U;
    s_saved_fault = s_saved_fault_detail = 0U;
    RCC->CSR = reset_reason;
    (void)bsp_system_take_reset_reason();
}
void bsp_system_test_corrupt_current(uint32_t word, uint32_t mask)
{
    volatile uint32_t *words = (volatile uint32_t *)&s_fault_retention.current_fault;
    if (word < sizeof(bsp_system_fault_record_t) / 4U) words[word] ^= mask;
}
void bsp_system_test_set_ms(uint32_t ms) { s_test_ms = ms; }
#endif

/* 先撤销签名、最后提交；断电或中途复位不能得到半条有效记录。 */
static void bsp_system_backup_access(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_ENABLE();
}
void bsp_system_safety_store(uint32_t state, uint32_t detail)
{
    bsp_system_backup_access();
    RTC->BKP16R = 0U;
    RTC->BKP17R = state;
    RTC->BKP18R = detail;
    RTC->BKP19R = ~(state ^ detail ^ 0x53414645UL);
    __DSB();
    RTC->BKP16R = 0x53414645UL;
    __DSB();
}
uint32_t bsp_system_safety_state(void)
{
    bsp_system_backup_access();
    if (RTC->BKP16R != 0x53414645UL || RTC->BKP17R < 1U || RTC->BKP17R > 3U ||
        RTC->BKP19R != ~(RTC->BKP17R ^ RTC->BKP18R ^ 0x53414645UL)) return 0U;
    return RTC->BKP17R;
}
uint32_t bsp_system_safety_detail(void)
{ return bsp_system_safety_state() ? RTC->BKP18R : 0U; }
void bsp_system_safety_clear(void)
{ bsp_system_backup_access(); RTC->BKP16R = 0U; __DSB(); }
