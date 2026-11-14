#include "remote_diagnostic.h"

#include "remote_diagnostic_config.h"

#if REMOTE_DIAGNOSTIC_ENABLED

#include "NRF24L01.h"
#include "usart.h"
#include <stdio.h>

#define REMOTE_DIAGNOSTIC_PERIOD_MS 1000U
#define REMOTE_DIAGNOSTIC_BUFFER_SIZE 1536U

static uint32_t s_last_summary_ms;
static uint8_t s_summary_started;
static volatile uint8_t s_tx_busy;
static volatile uint32_t s_tx_success;
static volatile uint32_t s_tx_busy_drops;
static volatile uint32_t s_tx_errors;
static char s_tx_buffer[REMOTE_DIAGNOSTIC_BUFFER_SIZE];

void remote_diagnostic_init(void)
{
    s_last_summary_ms = 0U;
    s_summary_started = 0U;
    s_tx_busy = 0U;
    s_tx_success = 0U;
    s_tx_busy_drops = 0U;
    s_tx_errors = 0U;
}

void remote_diagnostic_service(remote_status_model_t *model, uint32_t now_ms,
                                const char *oled_title)
{
    NRF24L01_Stats radio;
    remote_status_event_t event;
    int length, added;
    uint8_t event_index;

    if (model == NULL) return;
    if (s_summary_started && now_ms - s_last_summary_ms < REMOTE_DIAGNOSTIC_PERIOD_MS)
        return;
    s_summary_started = 1U;
    s_last_summary_ms = now_ms;
    if (s_tx_busy != 0U) { s_tx_busy_drops++; return; }
    NRF24L01_GetStats(&radio);
    length = snprintf(s_tx_buffer, sizeof(s_tx_buffer),
        "RC_DIAG build=%s t=%lu txok=%lu maxrt=%lu empty=%lu ack=%lu badw=%lu "
        "qdrop=%lu spi=%lu rec=%lu capfail=%lu pay=%lu pgap=%lu agap=%lu acc=%lu dup=%lu old=%lu "
        "len=%lu crc=%lu fmt=%lu loss=%lu cand=%lu recover=%lu edrop=%lu "
        "txdone=%lu txdrop=%lu txerr=%lu session=%u seq=%u type=%02X age=%lu "
        "alarm=%u code=%u detail_age=%lu oled=%.21s\r\n",
        REMOTE_BUILD_ID, (unsigned long)now_ms, (unsigned long)radio.tx_success,
        (unsigned long)radio.max_rt, (unsigned long)radio.empty_ack,
        (unsigned long)radio.ack_payloads, (unsigned long)radio.bad_width,
        (unsigned long)radio.queue_overflows,
        (unsigned long)radio.spi_errors, (unsigned long)radio.recoveries,
        (unsigned long)radio.capability_failures,
        (unsigned long)model->payloads_received,
        (unsigned long)model->max_payload_gap_ms,
        (unsigned long)model->max_accept_gap_ms,
        (unsigned long)model->stats.accepted,
        (unsigned long)model->stats.duplicates,
        (unsigned long)model->stats.old_frames,
        (unsigned long)model->stats.length_errors,
        (unsigned long)model->stats.crc_errors,
        (unsigned long)model->stats.format_errors,
        (unsigned long)model->stats.link_losses,
        (unsigned long)model->stats.recovery_candidates,
        (unsigned long)model->stats.recoveries,
        (unsigned long)model->event_overwrites,
        (unsigned long)s_tx_success, (unsigned long)s_tx_busy_drops,
        (unsigned long)s_tx_errors, model->latest.session, model->latest.sequence,
        model->latest.type,
        (unsigned long)(model->received ? now_ms - model->last_received_ms : UINT32_MAX),
        model->latest.alarm_level, model->slot[6].frame.data.alarm.code,
        (unsigned long)(model->slot[6].received ? now_ms - model->slot[6].received_ms : UINT32_MAX),
        oled_title == NULL ? "" : oled_title);
    if (length <= 0 || (size_t)length >= sizeof(s_tx_buffer)) {
        s_tx_errors++;
        return;
    }
    for (event_index = 0U; event_index < 4U; event_index++) {
        if (!remote_status_pop_event(model, &event)) break;
        added = snprintf(s_tx_buffer + length, sizeof(s_tx_buffer) - (size_t)length,
            "RC_EVT t=%lu kind=%u type=%02X session=%u seq=%u\r\n",
            (unsigned long)event.timestamp_ms, event.kind, event.type,
            event.session, event.sequence);
        if (added <= 0 || (size_t)added >= sizeof(s_tx_buffer) - (size_t)length) {
            s_tx_errors++;
            return;
        }
        length += added;
    }
    s_tx_busy = 1U;
    if (HAL_UART_Transmit_IT(&huart1, (uint8_t *)s_tx_buffer, (uint16_t)length) != HAL_OK) {
        s_tx_busy = 0U;
        s_tx_busy_drops++;
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) {
        s_tx_busy = 0U;
        s_tx_success++;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) {
        s_tx_errors++;
        if (huart->gState == HAL_UART_STATE_READY) s_tx_busy = 0U;
    }
}

#else

void remote_diagnostic_init(void) {}
void remote_diagnostic_service(remote_status_model_t *model, uint32_t now_ms,
                                const char *oled_title)
{
    (void)model;
    (void)now_ms;
    (void)oled_title;
}

#endif
