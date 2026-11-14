#ifndef APP_HMI_LINK_DIAG_H
#define APP_HMI_LINK_DIAG_H

#include <stdint.h>
#include "app_config.h"
#include "app_link_diag.h"
#include "app_control_arbiter.h"

#define APP_HMI_LINK_DIAG_ADDRESS 0x0700U
#define APP_HMI_LINK_DIAG_WORDS 24U
#define APP_HMI_LINK_DIAG_FIFO_SIZE 32U
#define APP_HMI_LINK_IMPLEMENTATION "window-r1"

/* One accepted UART submission. All cycle stamps use the same DWT clock.
 * Source fields are meaningful only when flags bit 0 is set. */
typedef struct {
    uint32_t id, at_ms, rx_generation;
    uint32_t capture, boundary, service, parsed, submitted, completed;
    uint32_t source_ms, source_sequence, copy_cycles, encode_cycles;
    uint16_t start, quantity, function, length, flags;
} app_hmi_link_transaction_t;
enum {
    APP_HMI_LINK_RX_REQUEST = 1U, APP_HMI_LINK_MAILBOX,
    APP_HMI_LINK_CONSUME, APP_HMI_LINK_REJECT, APP_HMI_LINK_TX_SUBMIT,
    APP_HMI_LINK_STOP_FIRST, APP_HMI_LINK_STOP_UPGRADE,
    APP_HMI_LINK_STOP_COMPLETE, APP_HMI_LINK_SCREEN, APP_HMI_LINK_TX_COMPLETE,
    APP_HMI_LINK_READ_EXCEPTION, APP_HMI_LINK_SLOW_WARNING
};

/* Host legacy suites intentionally omit the logger. Its dedicated suite opts in. */
#if APP_LINK_DIAG_ACTIVE
void app_hmi_link_diag_init(void);
/* CLI context only: bounded one-line output, never waits for UART capacity. */
void app_hmi_link_diag_service(void);
/* Task context only. Fields are fixed-width and formatted later by CLI. */
/* Keep the established API without an extra 48-byte forwarding frame on
 * the deeply nested servo stop path. Host fixtures retain an overridable hook. */
#if defined(__CC_ARM)
static __forceinline void app_hmi_link_diag_record(uint16_t kind,uint32_t a,uint32_t b,
    uint32_t c,uint32_t d,uint32_t e,uint32_t f)
{app_link_diag_record(APP_LINK_HMI,kind,a,b,c,d,e,f);}
#elif defined(__arm__)
static inline __attribute__((always_inline)) void app_hmi_link_diag_record(uint16_t kind,uint32_t a,uint32_t b,
    uint32_t c,uint32_t d,uint32_t e,uint32_t f)
{app_link_diag_record(APP_LINK_HMI,kind,a,b,c,d,e,f);}
#else
void app_hmi_link_diag_record(uint16_t kind, uint32_t a, uint32_t b,
    uint32_t c, uint32_t d, uint32_t e, uint32_t f);
#endif
/* HMI task only, after an actual UART TC; never called for a failed submit. */
void app_hmi_link_diag_complete(const app_hmi_link_transaction_t *transaction);
void app_hmi_link_diag_stop(const app_control_stop_t *stop, uint16_t kind);
void app_hmi_link_diag_upgrade(const app_control_stop_t *stop,uint16_t requester,
    uint32_t sequence,uint16_t command);
/* Diagnostic mailbox only; never touches control ownership, target or lease. */
uint8_t app_hmi_link_diag_screen(const uint16_t *words, uint16_t count);
#if defined(APP_HMI_LINK_DIAG_TEST) || defined(APP_LINK_DIAG_TEST)
/* Host pressure tests start with the event width of a continuously running unit. */
void app_hmi_link_diag_test_seed_serial(uint32_t value);
#endif
#else
static inline void app_hmi_link_diag_init(void) { }
static inline void app_hmi_link_diag_service(void) { }
static inline void app_hmi_link_diag_record(uint16_t kind, uint32_t a, uint32_t b,
    uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{ (void)kind; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
static inline void app_hmi_link_diag_complete(const app_hmi_link_transaction_t *transaction)
{ (void)transaction; }
static inline void app_hmi_link_diag_upgrade(const app_control_stop_t *stop,uint16_t requester,
    uint32_t sequence,uint16_t command)
{(void)stop;(void)requester;(void)sequence;(void)command;}
static inline void app_hmi_link_diag_stop(const app_control_stop_t *stop, uint16_t kind)
{ (void)stop; (void)kind; }
static inline uint8_t app_hmi_link_diag_screen(const uint16_t *words, uint16_t count)
{ (void)words; (void)count; return 0U; }
#endif

#endif
