#ifndef APP_HMI_STATE_H
#define APP_HMI_STATE_H

#include "app_system_snapshot.h"

#define HMI_STATE_REGISTER_COUNT (0x04C0U)
#define HMI_CONTROL_REGISTER_START (0x0460U)
#define HMI_CONTROL_REGISTER_COUNT (96U)
#define HMI_STATE_READ_MAX_REGISTERS (125U)
#define HMI_UPPER_COUNT APP_SYSTEM_UPPER_COUNT

typedef app_system_snapshot_t hmi_state_input_t;

typedef struct
{
    uint16_t code, source, level, active;
    uint32_t sequence, time_ms;
    int32_t parameter;
    uint8_t initialized, valid;
    /* Classification observation is separate from the latched event time. */
    uint16_t summary;
    uint32_t observed_ms, observed_sequence;
} hmi_alarm_state_t;

/* 纯只读编码接口，不访问驱动器、不授予控制权、不修改原业务状态。 */
void app_hmi_state_build(const hmi_state_input_t *input,
                         hmi_alarm_state_t *alarm,
                         uint16_t registers[HMI_STATE_REGISTER_COUNT]);

/* HMI task only, once per maintenance period. Reads never call this. */
void app_hmi_state_alarm_observe(const hmi_state_input_t *input,
                                  hmi_alarm_state_t *alarm);
/* Pure request-relative encoder: out[0] corresponds to start. A valid read
 * writes exactly quantity words (at most HMI_STATE_READ_MAX_REGISTERS).
 * Invalid ranges leave out unchanged; result mailbox is handled by snapshot.
 * No full-window scratch is required, including critical/detail subreads.
 * read_ms is observation age, never a source stamp. */
uint8_t app_hmi_state_build_read_window(const hmi_state_input_t *input,
    const hmi_alarm_state_t *alarm, uint32_t read_ms,
    uint16_t start, uint16_t quantity, uint16_t *out);

/* Legacy absolute-address adapter, writing only the requested intersection. */
uint8_t app_hmi_state_build_window(const hmi_state_input_t *input,
    const hmi_alarm_state_t *alarm, uint32_t read_ms,
    uint16_t start, uint16_t quantity,
    uint16_t registers[HMI_STATE_REGISTER_COUNT]);

/* Pure critical encoder: no alarm mutation or bus/control operations. */
void app_hmi_state_build_control(const hmi_state_input_t *input,
                                 uint16_t registers[HMI_CONTROL_REGISTER_COUNT]);

#endif
