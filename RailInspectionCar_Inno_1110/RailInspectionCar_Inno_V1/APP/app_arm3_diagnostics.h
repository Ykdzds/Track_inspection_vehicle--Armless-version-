#ifndef APP_ARM3_DIAGNOSTICS_H
#define APP_ARM3_DIAGNOSTICS_H

#include "app_arm3.h"

/* Local diagnostics only: no CAN, NRF or HMI wire-layout changes.
 * Every field describes captured_ms; readers must inspect age_ms. */
typedef struct app_arm3_port_status {
    app_arm3_snapshot_t arm;
    app_arm3_platform_t platform;
    uint32_t captured_ms, age_ms;
    uint8_t input_queue_depth;
    bool stop_pending;
} app_arm3_port_status_t;

#endif
