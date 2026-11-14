#ifndef BSP_HMI_UART_H
#define BSP_HMI_UART_H

#include <stdint.h>

typedef struct
{
    uint32_t rx_bytes;
    uint32_t rx_overflow;
    uint32_t rx_errors;
    uint32_t tx_frames;
    uint32_t tx_busy;
    uint32_t tx_errors;
    uint8_t tx_active;
    uint32_t requests, responses, crc_errors, frame_errors;
    uint32_t partial_timeouts, exceptions, last_request_ms;
    uint8_t request_seen;
} hmi_uart_stats_t;

typedef enum
{
    HMI_UART_REQUEST, HMI_UART_CRC_ERROR, HMI_UART_FRAME_ERROR,
    HMI_UART_PARTIAL_TIMEOUT, HMI_UART_ADDRESS_IGNORE
} hmi_uart_event_t;
typedef enum
{
    HMI_UART_RECOVERY_NONE, HMI_UART_RECOVERY_ARM,
    HMI_UART_RECOVERY_STATE, HMI_UART_RECOVERY_ERROR,
    HMI_UART_RECOVERY_TIMEOUT, HMI_UART_RECOVERY_SUBMIT
} hmi_uart_recovery_reason_t;

#define HMI_UART_REGION_COUNT (7U)
#define HMI_UART_OTHER_REGION (6U)
/* 详细诊断独立存储，不扩大每次状态发布的业务快照。 */
typedef struct
{
    uint32_t irq_entries, irq_sr, error_sr, last_error;
    uint32_t ore, fe, ne, pe;
    uint32_t rx_callbacks, rx_dequeued, address_ignored, arm_failures;
    uint32_t tx_callbacks, tx_timeouts, tx_max_ms, service_max_ms;
    uint32_t response_delay_max_cycles;
    /* Request last-byte capture to UART TC, unsigned DWT delta. */
    uint32_t response_complete_last_cycles, response_complete_max_cycles;
    uint32_t rx_attempts, rx_success, rx_failures;
    uint32_t tx_attempts, tx_success, tx_failures;
    uint32_t rx_epoch, tx_epoch, rx_reason, tx_reason;
    uint32_t arm_result, rx_abort_result, tx_abort_result;
    uint32_t gstate, rxstate, error_code, cr1, cr3;
    uint32_t region_requests[HMI_UART_REGION_COUNT];
    uint32_t region_responses[HMI_UART_REGION_COUNT];
    uint16_t queue_depth, rx_count, tx_count;
    uint8_t rx_pending, tx_pending, rx_armed;
    uint32_t dma_rx_irqs, dma_tx_irqs, dma_starts, dma_start_result;
    uint32_t idle_events, half_events, full_events, raw_bytes, boundaries;
    uint32_t dma_rx_error, dma_tx_error, rx_ndtr, tx_ndtr, rx_stream, tx_stream;
    uint32_t event_overflow, dropped_frames, complete_frames, tx_dropped;
    uint32_t tx_dma_complete, rx_abort_callbacks, tx_abort_callbacks;
    uint32_t rx_abort_timeouts, tx_abort_timeouts, rx_last_ms, tx_last_ms;
    uint32_t parse_max_cycles, capture_max_cycles, queue_high_water;
    /* ISR evidence only: formatting and reporting belong to the CLI task. */
    uint32_t raw_last_cycles, tx_complete_cycles;
    uint32_t quiet_early_count, quiet_elapsed_min_cycles;
    uint16_t event_depth, event_high_water;
} hmi_uart_diag_t;
/* 任务只记录协议事件；响应计数由发送完成中断更新。 */
void bsp_hmi_uart_record(hmi_uart_event_t event);

void bsp_hmi_uart_init(void);
typedef struct
{
    /* Data descriptor: last-byte capture. Boundary: actual quiet publication. */
    uint32_t cycles;
    uint32_t generation;
    uint16_t length;
    uint8_t boundary;
    uint8_t data[64];
} hmi_uart_rx_event_t;
uint8_t bsp_hmi_uart_pop(hmi_uart_rx_event_t *event);
void bsp_hmi_uart_rx_state(uint32_t *generation, uint32_t *last_cycles, uint8_t *empty);
void bsp_hmi_uart_discard(void);
uint32_t bsp_hmi_uart_cycles(void);
uint32_t bsp_hmi_uart_clock_hz(void);
uint8_t bsp_hmi_uart_send(const uint8_t *data, uint16_t length);
uint8_t bsp_hmi_uart_send_response(const uint8_t *data, uint16_t length,
                                  uint8_t region, uint32_t last_byte_cycles);
uint8_t bsp_hmi_uart_request_region(uint16_t start, uint16_t quantity);
void bsp_hmi_uart_irq_entry_isr(void);
void bsp_hmi_uart_bind_task(void *task);
void bsp_hmi_uart_rx_event_isr(uint16_t position, uint32_t kind);
void bsp_hmi_uart_dma_irq_isr(uint8_t transmit);
void bsp_hmi_uart_quiet_isr(void);
void bsp_hmi_uart_rx_abort_isr(void);
void bsp_hmi_uart_tx_abort_isr(void);
void bsp_hmi_uart_record_parse(uint32_t cycles);
void bsp_hmi_uart_tx_complete_isr(void);
void bsp_hmi_uart_error_isr(void);
void bsp_hmi_uart_service(void);
void bsp_hmi_uart_get_stats(hmi_uart_stats_t *stats);
void bsp_hmi_uart_get_diag(hmi_uart_diag_t *diag);
/* Small task-level observation of ISR completion; no formatting in ISR. */
void bsp_hmi_uart_get_completion(uint32_t *count, uint32_t *at_ms, uint32_t *cycles,
    uint32_t *region_length);

#endif
