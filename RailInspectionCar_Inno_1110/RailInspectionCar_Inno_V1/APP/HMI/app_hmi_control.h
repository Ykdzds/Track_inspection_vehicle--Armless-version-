#ifndef APP_HMI_CONTROL_H
#define APP_HMI_CONTROL_H

#include <stdint.h>
#include "app_control_arbiter.h"
#include "app_remote_servo_sync.h"

typedef struct
{
    uint32_t session, last_sequence, target_age_ms, timeout_ms, elapsed_ms;
    int16_t target, output[2];
    uint16_t state, flags, zero_mask, output_mask, error, phase;
    remote_sync_status_t sync;
    app_control_stop_t stop;
} app_hmi_control_status_t;

/* Fixed-memory diagnostic history. No printing or allocation in control paths. */
#define APP_HMI_TRACE_CAPACITY 64U
enum { APP_HMI_TRACE_ACQUIRE = 1U, APP_HMI_TRACE_TARGET, APP_HMI_TRACE_STOP,
       APP_HMI_TRACE_COMPLETE, APP_HMI_TRACE_REJECT };
typedef struct {
    uint32_t at_ms, session, sequence, target_age_ms, stop_event;
    uint16_t kind, remaining_ms, reason, error;
} app_hmi_trace_event_t;
typedef struct {
    uint32_t total, max_target_gap_ms, first_anomaly_ms, first_anomaly_gap_ms;
    uint16_t count;
} app_hmi_trace_stats_t;
void app_hmi_control_trace_stats(app_hmi_trace_stats_t *out);
uint8_t app_hmi_control_trace_read(uint16_t index, app_hmi_trace_event_t *out);

void app_hmi_control_get_status(app_hmi_control_status_t *status);
void app_hmi_control_init(void);
void app_hmi_control_service(void);
void app_hmi_control_request_stop(void);
uint8_t app_hmi_control_submit(const uint16_t *mailbox);
uint32_t app_hmi_control_session(void);
uint16_t app_hmi_control_remaining_ms(void);

#endif
