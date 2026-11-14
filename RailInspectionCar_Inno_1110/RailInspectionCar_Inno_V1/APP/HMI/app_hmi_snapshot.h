#ifndef APP_HMI_SNAPSHOT_H
#define APP_HMI_SNAPSHOT_H

#include <stdint.h>

/* Reasons distinguish a rejected legal read from an invalid dictionary range. */
enum {
    APP_HMI_SNAPSHOT_BAD_RANGE = 1U,
    APP_HMI_SNAPSHOT_NO_SOURCE,
    APP_HMI_SNAPSHOT_SOURCE_EXPIRED,
    APP_HMI_SNAPSHOT_ENCODE_EXPIRED,
    APP_HMI_SNAPSHOT_NO_FULL_CACHE,
    APP_HMI_SNAPSHOT_FULL_CACHE_EXPIRED,
    APP_HMI_SNAPSHOT_BAD_PARAMETER
};

typedef struct {
    uint32_t at_ms, id, cache_age_ms, source_age_ms;
    uint32_t source_sequence, cache_sequence, age_before_ms, age_after_ms;
    uint32_t copy_cycles, encode_cycles;
    uint32_t full_sequence, critical_sequence;
    uint16_t start, quantity, reason, exception;
} app_hmi_snapshot_reject_t;

typedef struct {
    uint32_t full_builds, critical_builds, rejections, merged;
    uint32_t last_copy_cycles, max_copy_cycles;
    uint32_t last_critical_cycles, max_critical_cycles;
    uint32_t last_full_cycles, max_full_cycles;
    uint32_t ordinary_builds, last_ordinary_cycles, max_ordinary_cycles;
    uint32_t last_age_before_ms, last_age_after_ms;
    uint32_t full_source_ms, full_sequence;
    uint32_t critical_source_ms, critical_sequence;
    app_hmi_snapshot_reject_t reject;
    uint8_t reject_pending;
} app_hmi_snapshot_diag_t;

typedef struct {
    uint32_t source_ms, sequence, copy_cycles, encode_cycles;
} app_hmi_snapshot_read_meta_t;

/* HMI-task transaction metadata; clear before every request. Result reads and
 * validation/source-copy failures have no copied-source metadata. An expired
 * copied source remains inspectable until clear or idle alarm maintenance. */
void app_hmi_snapshot_clear_read_meta(void);
uint8_t app_hmi_snapshot_last_read(app_hmi_snapshot_read_meta_t *meta);

/* Durations are DWT wall cycles, including task preemption. A pending first
 * rejection survives later failures until the CLI successfully outputs it. */
void app_hmi_snapshot_diag_read(app_hmi_snapshot_diag_t *diag);
void app_hmi_snapshot_diag_ack_reject(uint32_t id);

void app_hmi_snapshot_init(uint32_t boot_id);
void app_hmi_snapshot_service(void);
uint8_t app_hmi_snapshot_read(uint16_t start, uint16_t quantity,
                              uint16_t *values);
void app_hmi_snapshot_set_result(uint32_t session, uint32_t sequence,
                                 uint16_t command, uint16_t state,
                                 uint16_t error, uint32_t received_ms,
                                 uint32_t completed_ms, uint32_t generation,
                                 uint16_t owner, uint16_t remaining_ms,
                                 uint16_t feedback);
uint32_t app_hmi_snapshot_boot_id(void);
uint32_t app_hmi_snapshot_publish_sequence(void);

#endif
