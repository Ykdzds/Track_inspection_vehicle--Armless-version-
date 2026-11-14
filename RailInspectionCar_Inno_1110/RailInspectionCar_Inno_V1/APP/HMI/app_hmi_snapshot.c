#include "app_hmi_snapshot.h"

#include <limits.h>
#include <string.h>
#include "app_hmi_state.h"
#include "app_health.h"
#include "app_control_arbiter.h"
#include "app_hmi_control.h"
#include "app_rtos_create.h"
#include "task.h"
#include "main.h"
#include "app_boot_identity.h"
#include "bsp_hmi_uart.h"
#include "app_hmi_link_diag.h"

static uint32_t s_boot_id;
/* flags: bit0 control window; bit1 encoder ran for the copied source. */
static uint8_t s_last_read_valid, s_last_read_flags;
static uint32_t s_publish_sequence, s_last_alarm_ms;
static app_hmi_snapshot_diag_t s_snapshot_diag;
/* Maximum Modbus read only: out[0] corresponds to the requested start. */
static uint16_t s_registers[HMI_STATE_READ_MAX_REGISTERS];
static uint16_t s_result[APP_HMI_RESULT_REGISTERS];
/* 大快照采用静态存储，仅HMI任务采集和编码，避免占用任务栈。 */
static hmi_state_input_t s_input;
static hmi_alarm_state_t s_alarm;

static uint32_t source_age(uint32_t *sequence)
{
    uint32_t at;
    if (!app_system_snapshot_source_meta(&at, sequence))
    {
        *sequence = 0U;
        return UINT32_MAX;
    }
    return (uint32_t)(HAL_GetTick() - at);
}

static uint8_t collect(uint32_t *age, uint32_t *sequence, uint8_t *initialized)
{
    uint32_t started = bsp_hmi_uart_cycles(), elapsed;
    uint8_t valid = app_system_snapshot_read_with_meta(&s_input, NULL, sequence, age, initialized);
    elapsed = bsp_hmi_uart_cycles() - started;
    taskENTER_CRITICAL();
    s_snapshot_diag.last_copy_cycles = elapsed;
    if (elapsed > s_snapshot_diag.max_copy_cycles)
        s_snapshot_diag.max_copy_cycles = elapsed;
    taskEXIT_CRITICAL();
    return valid;
}

void app_hmi_snapshot_diag_read(app_hmi_snapshot_diag_t *diag)
{
    if (diag == NULL) return;
    taskENTER_CRITICAL();
    memcpy(diag, &s_snapshot_diag, sizeof(*diag));
    taskEXIT_CRITICAL();
}

void app_hmi_snapshot_diag_ack_reject(uint32_t id)
{
    taskENTER_CRITICAL();
    if (s_snapshot_diag.reject.id == id) s_snapshot_diag.reject_pending = 0U;
    taskEXIT_CRITICAL();
}

static uint8_t reject_read(uint16_t start, uint16_t quantity, uint16_t reason,
    uint32_t cache_age, uint32_t cache_sequence, uint32_t source_age_ms,
    uint32_t source_sequence, uint32_t age_before, uint32_t age_after,
    uint32_t encode_cycles)
{
    app_hmi_snapshot_reject_t *r = &s_snapshot_diag.reject;
    taskENTER_CRITICAL();
    s_snapshot_diag.rejections++;
    if (s_snapshot_diag.reject_pending) s_snapshot_diag.merged++;
    else
    {
        r->at_ms = HAL_GetTick();
        r->id = s_snapshot_diag.rejections;
        r->start = start; r->quantity = quantity; r->reason = reason;
        r->exception = 2U; r->cache_age_ms = cache_age;
        r->cache_sequence = cache_sequence;
        r->source_age_ms = source_age_ms; r->source_sequence = source_sequence;
        r->age_before_ms = age_before; r->age_after_ms = age_after;
        r->copy_cycles = reason == APP_HMI_SNAPSHOT_NO_SOURCE ||
            reason == APP_HMI_SNAPSHOT_SOURCE_EXPIRED || reason == APP_HMI_SNAPSHOT_ENCODE_EXPIRED ?
            s_snapshot_diag.last_copy_cycles : 0U;
        r->encode_cycles = encode_cycles;
        r->full_sequence = s_snapshot_diag.full_sequence;
        r->critical_sequence = s_snapshot_diag.critical_sequence;
        s_snapshot_diag.reject_pending = 1U;
    }
    taskEXIT_CRITICAL();
    app_hmi_link_diag_record(APP_HMI_LINK_READ_EXCEPTION, start, quantity,
        reason, 2U, source_age_ms, cache_age);
    return 0U;
}

void app_hmi_snapshot_set_result(uint32_t session, uint32_t sequence,
                                 uint16_t command, uint16_t state,
                                 uint16_t error, uint32_t received_ms,
                                 uint32_t completed_ms, uint32_t generation,
                                 uint16_t owner, uint16_t remaining_ms,
                                 uint16_t feedback)
{
    taskENTER_CRITICAL();
    /* 结果区保留既有低字在前的控制协议，状态区按正式字典高字在前。 */
    s_result[0] = (uint16_t)s_boot_id;
    s_result[1] = (uint16_t)(s_boot_id >> 16U);
    s_result[2] = (uint16_t)session;
    s_result[3] = (uint16_t)(session >> 16U);
    s_result[4] = (uint16_t)sequence;
    s_result[5] = (uint16_t)(sequence >> 16U);
    s_result[6] = command; s_result[7] = state; s_result[8] = error;
    s_result[9] = 1U;
    s_result[10] = (uint16_t)received_ms;
    s_result[11] = (uint16_t)(received_ms >> 16U);
    s_result[12] = (uint16_t)completed_ms;
    s_result[13] = (uint16_t)(completed_ms >> 16U);
    s_result[14] = (uint16_t)generation;
    s_result[15] = (uint16_t)(generation >> 16U);
    s_result[16] = owner; s_result[17] = remaining_ms;
    s_result[18] = feedback; s_result[19] = 0U;
    taskEXIT_CRITICAL();
}

void app_hmi_snapshot_clear_read_meta(void)
{
    s_last_read_valid = 0U;
}

uint8_t app_hmi_snapshot_last_read(app_hmi_snapshot_read_meta_t *meta)
{
    if (meta == NULL || !s_last_read_valid) return 0U;
    meta->source_ms = s_input.now_ms;
    meta->sequence = s_input.sequence;
    meta->copy_cycles = s_snapshot_diag.last_copy_cycles;
    meta->encode_cycles = (s_last_read_flags & 2U) == 0U ? 0U :
        ((s_last_read_flags & 1U) ? s_snapshot_diag.last_critical_cycles :
            s_snapshot_diag.last_ordinary_cycles);
    return 1U;
}

void app_hmi_snapshot_init(uint32_t boot_id)
{
    s_boot_id = boot_id;
    s_last_read_valid = s_last_read_flags = 0U;
    s_publish_sequence = 0U;
    s_last_alarm_ms = HAL_GetTick() - APP_HMI_SNAPSHOT_PERIOD_MS;
    memset(&s_snapshot_diag, 0, sizeof(s_snapshot_diag));
    memset(s_result, 0, sizeof(s_result));
    memset(&s_alarm, 0, sizeof(s_alarm));
}

void app_hmi_snapshot_service(void)
{
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_last_alarm_ms) < APP_HMI_SNAPSHOT_PERIOD_MS) return;
    /* Idle maintenance performs one observation, never a full dictionary build
     * or catch-up loop. The shared scratch cannot retain transaction metadata. */
    s_last_alarm_ms = now;
    app_hmi_snapshot_clear_read_meta();
    if (!collect(NULL, NULL, NULL) ||
        (uint32_t)(HAL_GetTick() - s_input.now_ms) > APP_SYSTEM_SNAPSHOT_MAX_AGE_MS) return;
    app_hmi_state_alarm_observe(&s_input, &s_alarm);
}

static uint8_t in_range(uint16_t start, uint32_t end,
                         uint16_t first, uint16_t last)
{
    return start >= first && end <= last;
}

uint8_t app_hmi_snapshot_read(uint16_t start, uint16_t quantity,
                              uint16_t *values)
{
    uint32_t end = (uint32_t)start + quantity;
    uint32_t source_sequence, age_before, age_after, started, elapsed;
    uint8_t initialized;
    uint8_t critical = in_range(start, end, HMI_CONTROL_REGISTER_START,
        HMI_CONTROL_REGISTER_START + HMI_CONTROL_REGISTER_COUNT);
    app_hmi_snapshot_clear_read_meta();
    age_before = source_age(&source_sequence);
    if (values == NULL || quantity == 0U || quantity > HMI_STATE_READ_MAX_REGISTERS)
        return reject_read(start, quantity, APP_HMI_SNAPSHOT_BAD_PARAMETER,
            UINT32_MAX, 0U, age_before, source_sequence, age_before, age_before, 0U);
    if (in_range(start, end, APP_HMI_RESULT_START,
                   APP_HMI_RESULT_START + APP_HMI_RESULT_REGISTERS))
    {
        taskENTER_CRITICAL();
        memcpy(values, &s_result[start - APP_HMI_RESULT_START], quantity * sizeof(*values));
        taskEXIT_CRITICAL();
        return 1U;
    }
    if (end > HMI_STATE_REGISTER_COUNT || !(in_range(start, end, 0x0000U, 0x0040U) ||
          in_range(start, end, 0x0050U, 0x0070U) ||
          in_range(start, end, 0x0070U, 0x0090U) ||
          in_range(start, end, 0x0090U, 0x00B0U) ||
          in_range(start, end, 0x00B0U, 0x00D0U) ||
          in_range(start, end, APP_HMI_METADATA_START,
              APP_HMI_METADATA_START + APP_HMI_METADATA_REGISTERS) ||
          in_range(start, end, APP_HMI_DETAIL_START,
              APP_HMI_DETAIL_START + APP_HMI_DETAIL_REGISTERS) || critical))
        return reject_read(start, quantity, APP_HMI_SNAPSHOT_BAD_RANGE,
            UINT32_MAX, 0U, age_before, source_sequence, age_before, age_before, 0U);

    /* Every legal state window copies exactly one published source, then
     * encodes that requested window. Never substitute a newer sample/retry. */
    if (!collect(&age_after, &source_sequence, &initialized))
        return reject_read(start, quantity,
            initialized ? APP_HMI_SNAPSHOT_SOURCE_EXPIRED : APP_HMI_SNAPSHOT_NO_SOURCE,
            UINT32_MAX, 0U, age_after, source_sequence, age_before, age_after, 0U);
    s_last_read_flags = critical;
    s_last_read_valid = 1U;
    age_before = HAL_GetTick() - s_input.now_ms;
    if (age_before > APP_SYSTEM_SNAPSHOT_MAX_AGE_MS)
        return reject_read(start, quantity, APP_HMI_SNAPSHOT_SOURCE_EXPIRED,
            UINT32_MAX, 0U, age_before, s_input.sequence, age_before, age_before, 0U);
    started = bsp_hmi_uart_cycles();
    if (!app_hmi_state_build_read_window(&s_input, &s_alarm, HAL_GetTick(),
            start, quantity, s_registers))
        return reject_read(start, quantity, APP_HMI_SNAPSHOT_BAD_RANGE,
            UINT32_MAX, 0U, age_before, s_input.sequence, age_before, age_before, 0U);
    elapsed = bsp_hmi_uart_cycles() - started;
    s_last_read_flags |= 2U;
    taskENTER_CRITICAL();
    age_after = HAL_GetTick() - s_input.now_ms;
    if (age_after <= APP_SYSTEM_SNAPSHOT_MAX_AGE_MS)
        memcpy(values, s_registers, quantity * sizeof(*values));
    if (critical)
    {
        s_snapshot_diag.critical_builds++;
        s_snapshot_diag.last_critical_cycles = elapsed;
        if (elapsed > s_snapshot_diag.max_critical_cycles)
            s_snapshot_diag.max_critical_cycles = elapsed;
        s_snapshot_diag.critical_source_ms = s_input.now_ms;
        s_snapshot_diag.critical_sequence = s_input.sequence;
    }
    else
    {
        s_snapshot_diag.ordinary_builds++;
        s_snapshot_diag.last_ordinary_cycles = elapsed;
        if (elapsed > s_snapshot_diag.max_ordinary_cycles)
            s_snapshot_diag.max_ordinary_cycles = elapsed;
    }
    s_snapshot_diag.last_age_before_ms = age_before;
    s_snapshot_diag.last_age_after_ms = age_after;
    taskEXIT_CRITICAL();
    if (age_after > APP_SYSTEM_SNAPSHOT_MAX_AGE_MS)
        return reject_read(start, quantity, APP_HMI_SNAPSHOT_ENCODE_EXPIRED,
            UINT32_MAX, 0U, age_after, s_input.sequence, age_before, age_after, elapsed);
    s_publish_sequence = s_input.sequence;
    return 1U;
}

uint32_t app_hmi_snapshot_boot_id(void) { return s_boot_id; }
uint32_t app_hmi_snapshot_publish_sequence(void) { return s_publish_sequence; }
