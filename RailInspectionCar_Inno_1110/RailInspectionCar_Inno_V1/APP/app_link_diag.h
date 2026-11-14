#ifndef APP_LINK_DIAG_H
#define APP_LINK_DIAG_H
#include <stdint.h>
#include "app_config.h"

#define APP_LINK_DIAG_ENABLED (APP_HMI_LINK_DIAG_ENABLED || APP_NRF_DIAGNOSTIC_ENABLED || APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_SERVO_RS485_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED || APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED)
#define APP_LINK_DIAG_IMPLEMENTATION "link5-can-r3"
#if APP_LINK_DIAG_ENABLED && (defined(__CC_ARM) || defined(__arm__) || defined(APP_LINK_DIAG_TEST) || defined(APP_HMI_LINK_DIAG_TEST))
#define APP_LINK_DIAG_ACTIVE 1
#else
#define APP_LINK_DIAG_ACTIVE 0
#endif
enum { APP_LINK_HMI, APP_LINK_REMOTE, APP_LINK_CAN_SERVO, APP_LINK_CAN_ARM,
       APP_LINK_RS485, APP_LINK_CAN_RAW, APP_LINK_SYNC, APP_LINK_CHANNELS };
/* CLI-owned compact projections. All new fields are hexadecimal unsigned words;
 * signed quantities are two's-complement. phase 0=state, 1+=health/detail. */
typedef struct { uint32_t sampled_ms, words[24]; uint8_t count; } app_link_diag_snapshot_t;
#if APP_LINK_DIAG_ACTIVE
void app_link_diag_record(uint8_t source, uint16_t kind, uint32_t a, uint32_t b,
                         uint32_t c, uint32_t d, uint32_t e, uint32_t f);
/* Task context: first immutable record (<=16 words) until every page is sent. */
void app_link_diag_freeze(uint8_t source, uint16_t kind, const uint32_t *words, uint8_t count);
void app_link_diag_freeze6(uint8_t source,uint16_t kind,uint32_t a,uint32_t b,
                          uint32_t c,uint32_t d,uint32_t e,uint32_t f);
/* First startup/stop cause for this boot, retained after successful output. */
void app_link_diag_startup_first(const uint32_t *words, uint8_t count);
#else
static inline void app_link_diag_startup_first(const uint32_t *words, uint8_t count)
{ (void)words; (void)count; }
static inline void app_link_diag_freeze6(uint8_t source,uint16_t kind,uint32_t a,uint32_t b,
    uint32_t c,uint32_t d,uint32_t e,uint32_t f)
{(void)source;(void)kind;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
static inline void app_link_diag_record(uint8_t source, uint16_t kind, uint32_t a,
    uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f)
{ (void)source;(void)kind;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f; }
static inline void app_link_diag_freeze(uint8_t source,uint16_t kind,const uint32_t *words,uint8_t count)
{ (void)source;(void)kind;(void)words;(void)count; }
#endif
#if APP_LINK_DIAG_ACTIVE && (APP_NRF_DIAGNOSTIC_ENABLED || APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED)
void app_remote_diag_snapshot(app_link_diag_snapshot_t *out, uint8_t phase);
#endif
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
void app_can_diag_snapshot(app_link_diag_snapshot_t *out, uint8_t source, uint8_t phase);
#endif
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
void app_rs485_diag_snapshot(app_link_diag_snapshot_t *out, uint8_t phase);
#endif
#if APP_LINK_DIAG_ACTIVE && APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED
void app_position_diag_snapshot(app_link_diag_snapshot_t *out);
#endif
#endif
