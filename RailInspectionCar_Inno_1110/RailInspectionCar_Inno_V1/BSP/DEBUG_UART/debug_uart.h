#ifndef DEBUG_UART_H
#define DEBUG_UART_H
#include <stdint.h>
#ifndef DEBUG_UART_TX_BUFFER_SIZE
#define DEBUG_UART_TX_BUFFER_SIZE (8192U)
#endif
#if (DEBUG_UART_TX_BUFFER_SIZE < 8192U) || (DEBUG_UART_TX_BUFFER_SIZE > 32768U)
#error "DEBUG_UART_TX_BUFFER_SIZE must be between 8192 and 32768"
#endif
#define DEBUG_UART_RX_BUFFER_SIZE (512U)
#define DEBUG_UART_PARSE_BYTES (256U)
#define DEBUG_UART_PARSE_US (200U)
typedef struct {
    uint32_t queued_bytes, sent_bytes, dropped_bytes;
    uint32_t dma_start_count, dma_complete_count, dma_error_count, dma_start_failures;
    uint16_t buffer_used, buffer_free, buffer_high_water;
    uint8_t tx_busy;
    uint32_t tx_lock_drops, tx_isr_drops, tx_critical_max_cycles;
    uint32_t rx_bytes, rx_consumed, rx_overflows, rx_dropped_bytes;
    uint32_t rx_uart_errors, rx_ore, rx_fe, rx_ne, rx_pe, rx_dma_errors;
    uint32_t rx_restarts, rx_start_failures, rx_abort_failures;
    uint32_t rx_idle_events, rx_half_events, rx_full_events, rx_epoch;
    uint32_t rx_parse_max_cycles, rx_line_overflows, rx_queue_drops;
    uint16_t rx_buffer_used, rx_high_water;
    uint8_t rx_running, rx_recovery_pending;
} debug_uart_stats_t;
void debug_uart_init(void);
void debug_uart_write_char(uint8_t data);
uint8_t debug_uart_try_write(const uint8_t *data, uint16_t length, uint16_t reserve);
uint8_t debug_uart_try_write_frame(const uint8_t *data, uint16_t length, uint16_t reserve);
/* CLI is the only context that starts/aborts DMA. No ISR waits or restarts. */
void debug_uart_service(void);
void debug_uart_tx_complete_isr(void);
void debug_uart_tx_error_isr(void);
void debug_uart_tx_abort_isr(void);
void debug_uart_rx_service(void);
uint16_t debug_uart_rx_read(uint8_t *data, uint16_t length);
uint32_t debug_uart_rx_epoch(void);
void debug_uart_rx_event_isr(uint16_t position, uint32_t kind);
void debug_uart_rx_error_isr(uint32_t sr, uint32_t hal_error);
void debug_uart_rx_abort_isr(void);
void debug_uart_rx_note_parse(uint32_t cycles);
void debug_uart_rx_note_line_drop(uint8_t queue_full);
uint32_t debug_uart_cycles(void);
uint32_t debug_uart_parse_limit_cycles(void);
void debug_uart_get_stats(debug_uart_stats_t *stats);
#endif
