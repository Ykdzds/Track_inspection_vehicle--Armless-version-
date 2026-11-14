#include "app_nrf_remote.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "NRF24L01.h"
#include "nrf_remote_protocol.h"
#include "app_arm3_port.h"
#include "app_arm3_inputs.h"
#include "nrf_status_telemetry.h"
#include "main.h"

static nrf_remote_snapshot_t s_snapshot;
static TickType_t s_last_valid_tick;
static uint8_t s_have_sequence;
static uint8_t s_have_event_sequence;
static uint8_t s_link_loss_reported;
static uint8_t s_last_frame_sequence;
static uint16_t s_last_event_sequence;
static TickType_t s_last_valid_frame_tick;
static TickType_t s_last_service_tick;
static uint8_t s_have_valid_frame_tick;
static uint32_t s_recovery_generation;
static uint8_t s_rebaseline_event;

static uint32_t nrf_remote_age_ms(TickType_t now)
{
    return (uint32_t)(((uint64_t)(TickType_t)(now - s_last_valid_tick) * 1000UL) /
                      (uint32_t)configTICK_RATE_HZ);
}

void app_nrf_remote_init(void)
{
    (void)memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_last_valid_tick = xTaskGetTickCount();
    s_have_sequence = 0U;
    s_have_event_sequence = 0U;
    s_link_loss_reported = 0U;
    s_last_valid_frame_tick = s_last_valid_tick;
    s_last_service_tick = s_last_valid_tick;
    s_have_valid_frame_tick = 0U;
    s_snapshot.initialized = 0U;
    s_recovery_generation = 0U;
    s_rebaseline_event = 0U;
}

static void nrf_remote_receive_one(void)
{
    uint8_t receive_result;
    nrf_remote_frame_t frame;
    nrf_remote_parse_result_t parse_result;
    uint8_t sequence_delta;
    uint16_t event_delta;
    uint8_t new_event;
    uint8_t key;
    TickType_t now;

    receive_result = NRF24L01_Receive();
    if (receive_result == 1U)
    {
        if(app_arm3_port_remote(NRF24L01_RxPacket,HAL_GetTick())){NRF24L01_ReportControlFrame();return;}
        parse_result = nrf_remote_parse(NRF24L01_RxPacket, &frame);
        if (parse_result != NRF_REMOTE_PARSE_OK)
        {
            if (parse_result == NRF_REMOTE_PARSE_CRC)
            {
                s_snapshot.crc_errors++;
            }
            else
            {
                s_snapshot.format_errors++;
            }
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
            app_link_diag_record(APP_LINK_REMOTE, APP_REMOTE_DIAG_REJECT,
                (uint32_t)parse_result, s_snapshot.crc_errors, s_snapshot.format_errors,
                s_snapshot.valid_frames, s_snapshot.recovery_generation,
                nrf_remote_age_ms(xTaskGetTickCount()));
#endif
        }
        else
        {
            NRF24L01_ReportControlFrame();
            now = xTaskGetTickCount();
            if (s_have_valid_frame_tick != 0U)
            {
                uint32_t frame_gap_ms;

                frame_gap_ms = (uint32_t)(((uint64_t)(TickType_t)
                    (now - s_last_valid_frame_tick) * 1000ULL) /
                    (uint32_t)configTICK_RATE_HZ);
                if (frame_gap_ms > s_snapshot.max_frame_gap_ms)
                {
                    s_snapshot.max_frame_gap_ms = frame_gap_ms;
                }
            }
            s_last_valid_frame_tick = now;
            s_have_valid_frame_tick = 1U;
            if (s_have_sequence != 0U)
            {
                sequence_delta = (uint8_t)(frame.frame_sequence -
                                           s_last_frame_sequence);
                if ((sequence_delta > 1U) && (sequence_delta < 128U))
                {
                    s_snapshot.dropped_frames += (uint32_t)(sequence_delta - 1U);
                }
            }
            s_last_frame_sequence = frame.frame_sequence;
            s_have_sequence = 1U;

            new_event = 0U;
            if (s_rebaseline_event)
            {
                /* 恢复首帧只建立按键批次基准，重复旧长按不能恢复授权。 */
                s_last_event_sequence = frame.event_sequence;
                s_have_event_sequence = 1U;
                s_rebaseline_event = 0U;
            }
            else if ((frame.event_valid != 0U) &&
                (s_have_event_sequence != 0U) &&
                (frame.event_sequence == s_last_event_sequence))
            {
                s_snapshot.duplicate_events++;
            }
            else if (frame.event_valid != 0U)
            {
                event_delta = (uint16_t)(frame.event_sequence - s_last_event_sequence);
                if ((s_have_event_sequence == 0U) || (event_delta < 0x8000U))
                {
                    if ((s_have_event_sequence != 0U) && (event_delta > 1U))
                    {
                        /* 事件批次缺失与无线帧缺失分别统计。 */
                        s_snapshot.missed_event_batches +=
                            (uint32_t)(event_delta - 1U);
                    }
                    s_last_event_sequence = frame.event_sequence;
                    s_have_event_sequence = 1U;
                    new_event = 1U;
                }
                else
                {
                    s_snapshot.duplicate_events++;
                }
            }

            if(new_event && (frame.short_press & (1U<<4U)))app_arm3_port_stop();
            if(app_arm3_remote_active()){
                /* Keep the independent legacy K5 stop, suppress every other
                 * chassis gesture while the remote ARM session is active. */
                frame.short_press&=(1U<<4U);frame.long_press=frame.double_click=0U;
                frame.left_horizontal=frame.left_vertical=frame.right_horizontal=frame.right_vertical=1000U;
                frame.speed_keys_valid=frame.speed_keys_held=0U;
                frame.speed_steps[0]=s_snapshot.speed_steps[0];frame.speed_steps[1]=s_snapshot.speed_steps[1];
            }
            s_snapshot.speed_keys_valid = frame.speed_keys_valid;
            s_snapshot.speed_keys_held = frame.speed_keys_held;
            s_snapshot.speed_steps[0] = frame.speed_steps[0];
            s_snapshot.speed_steps[1] = frame.speed_steps[1];
            s_snapshot.left_horizontal = frame.left_horizontal;
            s_snapshot.left_vertical = frame.left_vertical;
            s_snapshot.right_horizontal = frame.right_horizontal;
            s_snapshot.right_vertical = frame.right_vertical;
            if (new_event != 0U)
            {
                s_snapshot.short_press = frame.short_press;
                s_snapshot.long_press = frame.long_press;
                s_snapshot.double_click = frame.double_click;
                s_snapshot.event_sequence = frame.event_sequence;
                s_snapshot.event_batches++;
                for (key = 0U; key < 12U; key++)
                {
                    s_snapshot.short_counts[key] += (frame.short_press >> key) & 1U;
                    s_snapshot.long_counts[key] += (frame.long_press >> key) & 1U;
                    s_snapshot.double_counts[key] += (frame.double_click >> key) & 1U;
                }
            }
            s_snapshot.frame_sequence = frame.frame_sequence;
            s_snapshot.valid_frames++;
            s_last_valid_tick = now;
            s_snapshot.age_ms = 0U;
            s_snapshot.link_up = 1U;
            s_link_loss_reported = 0U;
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
            /* Sample ordinary frames; every accepted new key batch is retained.
             * valid_frames, rather than the wrapping 8-bit RF seq, correlates
             * RX -> publication -> consumption without another packet buffer. */
            if (new_event || (s_snapshot.valid_frames & 15U) == 0U)
                app_link_diag_record(APP_LINK_REMOTE, APP_REMOTE_DIAG_RX,
                    s_snapshot.valid_frames,
                    ((uint32_t)frame.event_sequence << 8U) | frame.frame_sequence,
                    s_snapshot.recovery_generation, frame.left_vertical,
                    ((uint32_t)frame.long_press << 16U) | frame.short_press,
                    ((uint32_t)new_event << 16U) | frame.double_click);
#endif
        }
    }

    now = xTaskGetTickCount();
    s_snapshot.age_ms = nrf_remote_age_ms(now);
    if ((s_snapshot.link_up != 0U) &&
        (s_snapshot.age_ms >= APP_REMOTE_SERVO_LINK_TIMEOUT_MS))
    {
        s_snapshot.link_up = 0U;
        if (s_link_loss_reported == 0U)
        {
            s_snapshot.link_loss_count++;
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
            app_link_diag_record(APP_LINK_REMOTE, APP_REMOTE_DIAG_LINK_DOWN,
                s_snapshot.age_ms, s_snapshot.link_loss_count, s_snapshot.valid_frames,
                s_snapshot.recovery_generation, 0U, s_snapshot.frame_sequence);
#endif
            s_link_loss_reported = 1U;
            /* 失联后重建序号基准，兼容遥控器重启。 */
            s_have_sequence = 0U;
            s_have_event_sequence = 0U;
            s_rebaseline_event = 1U;
        }
    }
}

void app_nrf_remote_service(void)
{
    uint8_t index;
    TickType_t now;
    uint32_t service_gap_ms;
    NRF24L01_Stats stats;

    now = xTaskGetTickCount();
    if (s_snapshot.service_count != 0U)
    {
        service_gap_ms = (uint32_t)(((uint64_t)(TickType_t)
            (now - s_last_service_tick) * 1000ULL) /
            (uint32_t)configTICK_RATE_HZ);
        if (service_gap_ms > (APP_NRF_TASK_PERIOD_MS * 2U))
        {
            s_snapshot.service_overrun_count++;
        }
    }
    s_last_service_tick = now;
    s_snapshot.service_count++;
    NRF24L01_Service();
    NRF24L01_GetStats(&stats);
    if (stats.recovery_generation != s_recovery_generation)
    {
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
        app_link_diag_record(APP_LINK_REMOTE, APP_REMOTE_DIAG_RECOVERY,
            s_recovery_generation, stats.recovery_generation, stats.last_failure,
            stats.spi_errors, stats.dma_timeouts, stats.radio_ready);
#endif
        s_recovery_generation = stats.recovery_generation;
        s_have_sequence = 0U;
        s_have_event_sequence = 0U;
        s_rebaseline_event = 1U;
        s_have_valid_frame_tick = 0U;
    }
    s_snapshot.recovery_generation = stats.recovery_generation;
    s_snapshot.initialized = stats.initialized;
    s_snapshot.radio_ready = stats.radio_ready;
    if (!stats.radio_ready)
    {
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
        if (s_snapshot.link_up)
            app_link_diag_record(APP_LINK_REMOTE, APP_REMOTE_DIAG_LINK_DOWN,
                nrf_remote_age_ms(now), s_snapshot.link_loss_count + 1U,
                s_snapshot.valid_frames, stats.recovery_generation, 1U, stats.last_failure);
#endif
        if (s_snapshot.link_up) s_snapshot.link_loss_count++;
        s_snapshot.link_up = 0U;
        s_link_loss_reported = 1U;
        s_rebaseline_event = 1U;
        s_snapshot.age_ms = nrf_remote_age_ms(now);
        return;
    }
    /* 每轮最多搬运已完成的三包；接收接口不再访问SPI或等待应答。 */
    for (index = 0U; index < 3U; index++)
    {
        nrf_remote_receive_one();
        if (!NRF24L01_IsRadioReady())
        {
            break;
        }
    }
}

bool app_nrf_remote_get_snapshot(nrf_remote_snapshot_t *snapshot)
{
    NRF24L01_Stats nrf_stats;

    if (snapshot == NULL)
    {
        return false;
    }
    taskENTER_CRITICAL();
    *snapshot = s_snapshot;
    {
        TickType_t now;

        now = xTaskGetTickCount();
        snapshot->age_ms = nrf_remote_age_ms(now);
        snapshot->task_age_ms = (uint32_t)(((uint64_t)(TickType_t)
            (now - s_last_service_tick) * 1000ULL) /
            (uint32_t)configTICK_RATE_HZ);
    }
    NRF24L01_GetStats(&nrf_stats);
    snapshot->nrf_spi_errors = nrf_stats.spi_errors;
    snapshot->nrf_fifo_errors = nrf_stats.fifo_errors;
    snapshot->nrf_recoveries = nrf_stats.recoveries;
    snapshot->initialized =
        (uint8_t)(nrf_stats.initialized != 0U);
    snapshot->radio_ready = nrf_stats.radio_ready;
    snapshot->recovery_generation = nrf_stats.recovery_generation;
    if (!nrf_stats.radio_ready) snapshot->link_up = 0U;
    taskEXIT_CRITICAL();
    return true;
}

#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
void app_nrf_remote_diag_snapshot(app_link_diag_snapshot_t *out, uint8_t phase)
{
    if (out == NULL) return;
    taskENTER_CRITICAL();
    out->sampled_ms = HAL_GetTick();
    out->count = 0U;
    if (phase == 1U) {
        out->words[0] = s_snapshot.valid_frames; out->words[1] = s_snapshot.dropped_frames;
        out->words[2] = s_snapshot.crc_errors; out->words[3] = s_snapshot.format_errors;
        out->words[4] = s_snapshot.duplicate_events; out->words[5] = s_snapshot.missed_event_batches;
        out->words[6] = s_snapshot.link_loss_count; out->words[7] = s_snapshot.recovery_generation;
        out->words[8] = s_snapshot.max_frame_gap_ms; out->words[9] = s_snapshot.service_overrun_count;
        out->words[10] = nrf_remote_age_ms(xTaskGetTickCount());
        out->words[11] = (uint32_t)((uint64_t)(TickType_t)(xTaskGetTickCount() - s_last_service_tick) *
            1000U / configTICK_RATE_HZ);
        out->count = (uint8_t)(12U + NRF24L01_DiagnosticWords(&out->words[12], 0U));
    } else if (phase == 2U) {
        out->count = NRF24L01_DiagnosticWords(out->words, 1U);
        out->count = (uint8_t)(out->count + nrf_status_telemetry_diag_words(&out->words[16], 0U));
    } else if (phase == 3U) {
        out->count = NRF24L01_DiagnosticWords(out->words, 2U);
    } else if (phase == 4U || phase == 5U) {
        out->count = nrf_status_telemetry_diag_words(out->words, (uint8_t)(phase - 3U));
    }
    taskEXIT_CRITICAL();
}
#endif
