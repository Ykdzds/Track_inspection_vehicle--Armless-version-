#include "app_hmi.h"
#include "app_hmi_link_diag.h"

#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "app_hmi_control.h"
#include "app_hmi_snapshot.h"
#include "bsp_hmi_uart.h"
#include "hmi_modbus_slave.h"
#include "bsp_system.h"
#include "main.h"
#include "app_health.h"
#include "app_remote_servo.h"
#include "aimtor_servo.h"
#include "app_control_arbiter.h"
#include "app_boot_identity.h"
#include "app_arm3_port.h"

static uint8_t s_frame[APP_HMI_MAX_FRAME_SIZE];
static uint8_t s_response[HMI_MODBUS_MAX_PDU + 3U];
static uint16_t s_values[125U];
static hmi_modbus_request_t s_request;
static uint16_t s_length;
static uint32_t s_last_byte_cycles, s_rx_generation;
static uint8_t s_discarding;
static uint32_t s_boot_id;
static hmi_uart_diag_t s_cli_diag;
#if APP_HMI_LINK_DIAG_ENABLED
static uint32_t s_diag_completed;
static uint32_t s_diag_serial, s_boundary_cycles, s_service_cycles;
static app_hmi_link_transaction_t s_diag_request, s_diag_active;
static void observe_tx_completion(void)
{
    uint32_t count, at, cycles, info;
    bsp_hmi_uart_get_completion(&count, &at, &cycles, &info);
    if (count != s_diag_completed)
    {
        app_hmi_link_diag_record(APP_HMI_LINK_TX_COMPLETE, info,
            count, at, cycles, count - s_diag_completed, 0U);
        if (s_diag_active.id && count - s_diag_completed == 1U &&
            s_diag_active.length == (uint16_t)(info & 0xFFU))
        {
            s_diag_active.at_ms = at;
            s_diag_active.completed = cycles;
            app_hmi_link_diag_complete(&s_diag_active);
        }
        s_diag_active.id = 0U;
        s_diag_completed = count;
    }
}
#endif

static void send_response(uint16_t length, uint8_t region)
{
    uint8_t queued;
#if APP_HMI_LINK_DIAG_ENABLED
    /* Fixed-cost submission/metadata handoff only. Mask UART TC so a new
     * transfer cannot overwrite the previous completion's transaction.
     * No encoding, waits or text formatting are performed here. */
    taskENTER_CRITICAL();
    observe_tx_completion();
#endif
    queued = bsp_hmi_uart_send_response(s_response, length, region, s_last_byte_cycles);
#if APP_HMI_LINK_DIAG_ENABLED
    if (queued)
    {
        s_diag_request.length = length;
        s_diag_request.submitted = bsp_hmi_uart_cycles();
        s_diag_active = s_diag_request;
    }
    taskEXIT_CRITICAL();
#endif
    app_hmi_link_diag_record(APP_HMI_LINK_TX_SUBMIT, s_request.start,
        s_request.function, length, queued, s_last_byte_cycles, bsp_hmi_uart_cycles());
}

static uint16_t expected_frame_length(void)
{
    if (s_length < 2U) return 0U;
    if (s_frame[1] == 0x03U) return 8U;
    if (s_frame[1] == 0x10U && s_length >= 7U)
        return (uint16_t)(7U + s_frame[6] + 2U);
    return 0U;
}

void app_hmi_init(void)
{
#if defined(APP_HMI_LINK_DIAG_TEST)
    app_hmi_link_diag_init(); /* host fixture; production initializes once after boot ID */
#endif
    s_length = 0U;

    s_discarding = 0U;
    s_rx_generation = 0U;
    s_boot_id = app_boot_identity_id();
    bsp_hmi_uart_init();
    app_hmi_snapshot_init(s_boot_id);
    app_hmi_control_init();
#if APP_HMI_LINK_DIAG_ENABLED
    s_diag_completed = 0U;
    s_diag_serial = s_boundary_cycles = s_service_cycles = 0U;
    memset(&s_diag_request, 0, sizeof(s_diag_request));
    memset(&s_diag_active, 0, sizeof(s_diag_active));
#endif
}

static void handle_frame(void)
{
    uint16_t response_length = 0U;
    uint8_t parsed, region = HMI_UART_OTHER_REGION;
    app_hmi_snapshot_clear_read_meta();
#if APP_HMI_LINK_DIAG_ENABLED
    app_hmi_snapshot_read_meta_t meta;
    memset(&s_diag_request, 0, sizeof(s_diag_request));
    if (++s_diag_serial == 0U) ++s_diag_serial;
    s_diag_request.id = s_diag_serial;
    s_diag_request.rx_generation = s_rx_generation;
    s_diag_request.capture = s_last_byte_cycles;
    s_diag_request.boundary = s_boundary_cycles;
    s_diag_request.service = s_service_cycles;
    s_diag_request.function = s_length >= 2U ? s_frame[1] : 0U;
#endif

    parsed = hmi_modbus_parse(s_frame, s_length, &s_request, s_response,
                              &response_length);
#if APP_HMI_LINK_DIAG_ENABLED
    s_diag_request.parsed = bsp_hmi_uart_cycles();
#endif
    if (parsed == HMI_MODBUS_PARSE_IGNORE)
    {
        if (s_length && s_frame[0] != APP_HMI_ADDRESS)
            bsp_hmi_uart_record(HMI_UART_ADDRESS_IGNORE);
        return;
    }
    if (parsed == HMI_MODBUS_PARSE_CRC_ERROR)
    {
        bsp_hmi_uart_record(HMI_UART_CRC_ERROR);
        return;
    }
    if (parsed == HMI_MODBUS_PARSE_FRAME_ERROR)
    {
        bsp_hmi_uart_record(HMI_UART_FRAME_ERROR);
        send_response(response_length, region);
        return;
    }
    if (parsed == HMI_MODBUS_PARSE_EXCEPTION)
    {
        send_response(response_length, region);
        return;
    }
#if APP_HMI_LINK_DIAG_ENABLED
    s_diag_request.start = s_request.start;
    s_diag_request.quantity = s_request.quantity;
#endif
    app_hmi_link_diag_record(APP_HMI_LINK_RX_REQUEST, s_request.start,
        s_request.function, s_request.quantity, s_last_byte_cycles,
        bsp_hmi_uart_cycles(), HAL_GetTick());
    if (s_request.function == 0x03U)
    {
        if (s_request.quantity > 125U ||
            !(s_request.start>=0x0500U ? app_arm3_port_hmi_read(s_request.start,s_request.quantity,s_values,HAL_GetTick()) : app_hmi_snapshot_read(s_request.start, s_request.quantity, s_values)))
        {
            (void)hmi_modbus_exception(s_frame[0], s_request.function, 2U,
                                        s_response, &response_length);
        }
        else
        {
            bsp_hmi_uart_record(HMI_UART_REQUEST);
            region = bsp_hmi_uart_request_region(s_request.start, s_request.quantity);
            (void)hmi_modbus_build_read_response(s_frame[0], s_request.function,
                s_values, s_request.quantity, s_response, &response_length);
        }
#if APP_HMI_LINK_DIAG_ENABLED
        if (s_request.start < 0x0500U && app_hmi_snapshot_last_read(&meta))
        {
            s_diag_request.flags |= 1U;
            s_diag_request.source_ms = meta.source_ms;
            s_diag_request.source_sequence = meta.sequence;
            s_diag_request.copy_cycles = meta.copy_cycles;
            s_diag_request.encode_cycles = meta.encode_cycles;
        }
#endif
        send_response(response_length, region);
    }
    else if (s_request.function == 0x10U)
    {
        /* This diagnostic sink has no path to ownership or motion. It remains
         * harmlessly ACK-able with logging disabled and in read-only builds. */
        if (s_request.start == APP_HMI_LINK_DIAG_ADDRESS &&
            s_request.quantity == APP_HMI_LINK_DIAG_WORDS)
        {
            uint8_t stored;
#if APP_HMI_LINK_DIAG_ENABLED && (defined(__CC_ARM) || defined(__arm__) || defined(APP_HMI_LINK_DIAG_TEST))
            stored = app_hmi_link_diag_screen(s_request.values, s_request.quantity);
#else
            stored = s_request.values[0] == 0x4844U &&
                (s_request.values[1] == 0x0100U || s_request.values[1] == 0x0101U) &&
                (s_request.values[2] || s_request.values[3]);
#endif
            bsp_hmi_uart_record(HMI_UART_REQUEST);
            if (stored)
                (void)hmi_modbus_build_write_response(s_frame[0], s_request.start,
                    s_request.quantity, s_response, &response_length);
            else
                (void)hmi_modbus_exception(s_frame[0], s_request.function, 3U,
                    s_response, &response_length);
            send_response(response_length, region);
            return;
        }
#if !APP_HMI_CONTROL_ENABLED
        (void)hmi_modbus_exception(s_frame[0], s_request.function, 1U,
                                    s_response, &response_length);
        send_response(response_length, region);
#else
        uint16_t i;
        if ((s_request.start != APP_HMI_MAILBOX_START && s_request.start != 0x0500U && s_request.start != 0x0520U) ||
            s_request.quantity != APP_HMI_MAILBOX_REGISTERS)
        {
            (void)hmi_modbus_exception(s_frame[0], s_request.function, 2U,
                                        s_response, &response_length);
            send_response(response_length, region);
            return;
        }
        bsp_hmi_uart_record(HMI_UART_REQUEST);
        region = bsp_hmi_uart_request_region(s_request.start, s_request.quantity);
        for (i = 0U; i < 24U; i++) s_values[i] = s_request.values[i];
        if (!(s_request.start>=0x0500U ? app_arm3_port_hmi_write(s_request.start,24U,s_values,HAL_GetTick()) : app_hmi_control_submit(s_values)))
        {
            (void)hmi_modbus_exception(s_frame[0], s_request.function, 6U,
                                        s_response, &response_length);
        }
        else
        {
            (void)hmi_modbus_build_write_response(s_frame[0], s_request.start,
                s_request.quantity, s_response, &response_length);
        }
        send_response(response_length, region);
#endif
    }
}

uint8_t app_hmi_cli_command(const char *line)
{
    static app_hmi_control_status_t control;
    hmi_uart_stats_t stats;
    uint8_t i;
    uint32_t now, age;
    if (strcmp(line, "hmi trace") == 0)
    {
        app_hmi_trace_stats_t trace;
        app_hmi_trace_event_t event;
        app_hmi_control_trace_stats(&trace);
        printf("hmi trace total=%lu count=%u max_target_gap_ms=%lu first_anomaly_ms=%lu gap_ms=%lu\r\n",
            (unsigned long)trace.total, trace.count, (unsigned long)trace.max_target_gap_ms,
            (unsigned long)trace.first_anomaly_ms, (unsigned long)trace.first_anomaly_gap_ms);
        for (i = 0U; i < trace.count; ++i)
            if (app_hmi_control_trace_read(i, &event))
                printf("hmi event at=%lu kind=%u session=%lu seq=%lu age=%lu lease=%u stop=%lu reason=%u error=%u\r\n",
                    (unsigned long)event.at_ms, event.kind, (unsigned long)event.session,
                    (unsigned long)event.sequence, (unsigned long)event.target_age_ms,
                    event.remaining_ms, (unsigned long)event.stop_event, event.reason, event.error);
        return 1U;
    }
    if (strcmp(line, "hmi status") != 0) return 0U;
    app_hmi_control_get_status(&control);
    printf("hmi control owner=%u generation=%lu session=%lu state=%u flags=%04X age=%lu seq=%lu target=%d output=%d,%d sync=%u correction=%d error=%u\r\n",
        (unsigned)app_control_arbiter_owner(), (unsigned long)app_control_arbiter_generation(),
        (unsigned long)control.session, (unsigned)control.state, (unsigned)control.flags,
        (unsigned long)control.target_age_ms, (unsigned long)control.last_sequence,
        control.target, control.output[0], control.output[1], (unsigned)control.sync.state,
        control.sync.integer_correction, (unsigned)control.error);
    printf("hmi stop event=%lu owner=%u reason=%u phase=%u decel=%u elapsed=%lu timeout=%lu zero=%u enabled=%u result=%u error=%u\r\n",
        (unsigned long)control.stop.event, (unsigned)control.stop.owner, (unsigned)control.stop.reason,
        (unsigned)control.phase, (unsigned)control.stop.deceleration_mm_s2,
        (unsigned long)control.elapsed_ms, (unsigned long)control.stop.timeout_ms,
        (unsigned)control.stop.zero_mask, (unsigned)control.stop.enabled_mask,
        (unsigned)control.stop.result, (unsigned)control.stop.error);
    bsp_hmi_uart_get_stats(&stats);
    bsp_hmi_uart_get_diag(&s_cli_diag);
    printf("hmi transport=uart5-dma-r1 build=%s_%s rx=DMA_CIRCULAR tx=DMA_NORMAL\r\n", __DATE__, __TIME__);
    now = HAL_GetTick();
    age = stats.request_seen ? now - stats.last_request_ms : UINT32_MAX;
    printf("hmi readonly=%u boot=%lu identity_valid=%u identity_result=%u last_read=%lu\r\n",
        (unsigned)!APP_HMI_CONTROL_ENABLED, (unsigned long)app_boot_identity_id(),
        (unsigned)app_boot_identity_valid(), (unsigned)app_boot_identity_get()->result,
        (unsigned long)app_hmi_snapshot_publish_sequence());
    printf("hmi requests=%lu responses=%lu exceptions=%lu crc=%lu frame=%lu partial=%lu overflow=%lu tx_busy=%lu tx_errors=%lu\r\n",
        (unsigned long)stats.requests, (unsigned long)stats.responses,
        (unsigned long)stats.exceptions, (unsigned long)stats.crc_errors,
        (unsigned long)stats.frame_errors, (unsigned long)stats.partial_timeouts,
        (unsigned long)stats.rx_overflow, (unsigned long)stats.tx_busy,
        (unsigned long)stats.tx_errors);
    printf("hmi uptime_ms=%lu request_age_ms=%lu service_max_ms=%lu delay_max_cycles=%lu clock_hz=%lu\r\n",
        (unsigned long)now, (unsigned long)age, (unsigned long)s_cli_diag.service_max_ms,
        (unsigned long)s_cli_diag.response_delay_max_cycles, (unsigned long)bsp_hmi_uart_clock_hz());
    printf("hmi rx_bytes=%lu callbacks=%lu dequeued=%lu queue=%u ignored=%lu armed=%u arm_fail=%lu arm_result=%lu\r\n",
        (unsigned long)stats.rx_bytes, (unsigned long)s_cli_diag.rx_callbacks,
        (unsigned long)s_cli_diag.rx_dequeued, (unsigned)s_cli_diag.queue_depth,
        (unsigned long)s_cli_diag.address_ignored, (unsigned)s_cli_diag.rx_armed,
        (unsigned long)s_cli_diag.arm_failures, (unsigned long)s_cli_diag.arm_result);
    printf("hmi rx_errors=%lu last_error=%08lX ore=%lu fe=%lu ne=%lu pe=%lu\r\n",
        (unsigned long)stats.rx_errors, (unsigned long)s_cli_diag.last_error,
        (unsigned long)s_cli_diag.ore, (unsigned long)s_cli_diag.fe,
        (unsigned long)s_cli_diag.ne, (unsigned long)s_cli_diag.pe);
    printf("hmi tx_frames=%lu callbacks=%lu active=%u timeout=%lu max_ms=%lu\r\n",
        (unsigned long)stats.tx_frames, (unsigned long)s_cli_diag.tx_callbacks,
        (unsigned)stats.tx_active, (unsigned long)s_cli_diag.tx_timeouts,
        (unsigned long)s_cli_diag.tx_max_ms);
    printf("hmi rx_recovery=%u reason=%lu epoch=%lu attempt=%lu ok=%lu fail=%lu abort=%lu\r\n",
        (unsigned)s_cli_diag.rx_pending, (unsigned long)s_cli_diag.rx_reason,
        (unsigned long)s_cli_diag.rx_epoch, (unsigned long)s_cli_diag.rx_attempts,
        (unsigned long)s_cli_diag.rx_success, (unsigned long)s_cli_diag.rx_failures,
        (unsigned long)s_cli_diag.rx_abort_result);
    printf("hmi tx_recovery=%u reason=%lu epoch=%lu attempt=%lu ok=%lu fail=%lu abort=%lu\r\n",
        (unsigned)s_cli_diag.tx_pending, (unsigned long)s_cli_diag.tx_reason,
        (unsigned long)s_cli_diag.tx_epoch, (unsigned long)s_cli_diag.tx_attempts,
        (unsigned long)s_cli_diag.tx_success, (unsigned long)s_cli_diag.tx_failures,
        (unsigned long)s_cli_diag.tx_abort_result);
    printf("hmi gState=%08lX RxState=%08lX ErrorCode=%08lX rx_count=%u tx_count=%u\r\n",
        (unsigned long)s_cli_diag.gstate, (unsigned long)s_cli_diag.rxstate,
        (unsigned long)s_cli_diag.error_code, (unsigned)s_cli_diag.rx_count,
        (unsigned)s_cli_diag.tx_count);
    printf("hmi irq=%lu SR_irq_history=%08lX SR_error_history=%08lX CR1=%08lX CR3=%08lX\r\n",
        (unsigned long)s_cli_diag.irq_entries, (unsigned long)s_cli_diag.irq_sr,
        (unsigned long)s_cli_diag.error_sr, (unsigned long)s_cli_diag.cr1,
        (unsigned long)s_cli_diag.cr3);
    for (i = 0U; i < HMI_UART_REGION_COUNT; i++)
        printf("hmi region=%u requests=%lu responses=%lu\r\n", (unsigned)i,
            (unsigned long)s_cli_diag.region_requests[i],
            (unsigned long)s_cli_diag.region_responses[i]);
    printf("hmi dma starts=%lu result=%lu rx_irq=%lu tx_irq=%lu idle=%lu ht=%lu tc=%lu raw=%lu boundaries=%lu\r\n",
        (unsigned long)s_cli_diag.dma_starts, (unsigned long)s_cli_diag.dma_start_result,
        (unsigned long)s_cli_diag.dma_rx_irqs, (unsigned long)s_cli_diag.dma_tx_irqs,
        (unsigned long)s_cli_diag.idle_events, (unsigned long)s_cli_diag.half_events,
        (unsigned long)s_cli_diag.full_events, (unsigned long)s_cli_diag.raw_bytes,
        (unsigned long)s_cli_diag.boundaries);
    printf("hmi dma rx_ndtr=%lu tx_ndtr=%lu rx_cr=%08lX tx_cr=%08lX rx_error=%08lX tx_error=%08lX\r\n",
        (unsigned long)s_cli_diag.rx_ndtr, (unsigned long)s_cli_diag.tx_ndtr,
        (unsigned long)s_cli_diag.rx_stream, (unsigned long)s_cli_diag.tx_stream,
        (unsigned long)s_cli_diag.dma_rx_error, (unsigned long)s_cli_diag.dma_tx_error);
    printf("hmi dma queue_max=%lu events=%u event_max=%u event_overflow=%lu dropped=%lu tx_dropped=%lu tx_dma_done=%lu\r\n",
        (unsigned long)s_cli_diag.queue_high_water, (unsigned)s_cli_diag.event_depth,
        (unsigned)s_cli_diag.event_high_water, (unsigned long)s_cli_diag.event_overflow,
        (unsigned long)s_cli_diag.dropped_frames, (unsigned long)s_cli_diag.tx_dropped,
        (unsigned long)s_cli_diag.tx_dma_complete);
    printf("hmi dma rx_abort_done=%lu tx_abort_done=%lu rx_abort_timeout=%lu tx_abort_timeout=%lu rx_last_ms=%lu tx_last_ms=%lu parse_max_cycles=%lu capture_max_cycles=%lu\r\n",
        (unsigned long)s_cli_diag.rx_abort_callbacks, (unsigned long)s_cli_diag.tx_abort_callbacks,
        (unsigned long)s_cli_diag.rx_abort_timeouts, (unsigned long)s_cli_diag.tx_abort_timeouts,
        (unsigned long)s_cli_diag.rx_last_ms, (unsigned long)s_cli_diag.tx_last_ms,
        (unsigned long)s_cli_diag.parse_max_cycles, (unsigned long)s_cli_diag.capture_max_cycles);
    return 1U;
}

static void finish_frame(void)
{
    uint16_t expected = expected_frame_length();
    if (s_discarding || s_length == 0U) return;
    if (s_length < 4U || (expected != 0U && s_length < expected))
        bsp_hmi_uart_record(HMI_UART_PARTIAL_TIMEOUT);
    else handle_frame();
}

void app_hmi_service(void)
{
    hmi_uart_rx_event_t event;
    uint32_t generation, last, start;
    uint32_t hz = bsp_hmi_uart_clock_hz();
    uint32_t budget = hz / 5000U;
    uint8_t frames = 0U, empty;

    bsp_hmi_uart_service();
#if APP_HMI_LINK_DIAG_ENABLED
    observe_tx_completion();
#endif
    start = bsp_hmi_uart_cycles();

    bsp_hmi_uart_rx_state(&generation, &last, &(uint8_t){0U});
    if (generation != s_rx_generation)
    {
        s_rx_generation = generation;
        s_length = 0U; s_discarding = 1U;
    }
    while (frames < 2U &&
           (uint32_t)(bsp_hmi_uart_cycles() - start) < budget && bsp_hmi_uart_pop(&event))
    {
        bsp_hmi_uart_rx_state(&generation, &last, &(uint8_t){0U});
        if (generation != s_rx_generation || event.generation != generation)
        {
            s_rx_generation = generation;
            s_length = 0U; s_discarding = 1U;
            continue;
        }
        if (event.boundary)
        {
#if APP_HMI_LINK_DIAG_ENABLED
            s_boundary_cycles = event.cycles;
            /* Per-frame dispatch, after dequeuing its quiet boundary. This
             * cannot precede a request arriving during this service batch. */
            s_service_cycles = bsp_hmi_uart_cycles();
#endif
            if (s_length != 0U && !s_discarding) finish_frame();
            frames++;
            s_length = 0U; s_discarding = 0U;
        }
        else if (!s_discarding)
        {
            s_last_byte_cycles = event.cycles;
            if ((uint32_t)s_length + event.length <= sizeof(s_frame))
            {
                memcpy(s_frame + s_length, event.data, event.length);
                s_length = (uint16_t)(s_length + event.length);
            }
            else
            {
                bsp_hmi_uart_record(HMI_UART_FRAME_ERROR);
                s_length = 0U; s_discarding = 1U;
            }
        }
    }
    bsp_hmi_uart_record_parse(bsp_hmi_uart_cycles() - start);
    /* Only bounded alarm observation runs at idle; no whole-table encoding. */
    bsp_hmi_uart_rx_state(&generation, &last, &empty);
    if (empty && s_length == 0U && !s_discarding) app_hmi_snapshot_service();
}

uint8_t app_hmi_work_pending(void)
{
    uint32_t generation, last;
    uint8_t empty;
    bsp_hmi_uart_rx_state(&generation, &last, &empty);
    return !empty;
}
