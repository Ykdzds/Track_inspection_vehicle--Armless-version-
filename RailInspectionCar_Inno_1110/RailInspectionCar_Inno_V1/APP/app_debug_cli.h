#ifndef APP_DEBUG_CLI_H
#define APP_DEBUG_CLI_H

#include <stdint.h>

/* Always present: USART1 TX, automatic diagnostics and shared Flash worker.
 * Interactive RX/parser resources are controlled separately by app_config.h. */
void app_cli_init(void);
void app_cli_process(void);
void app_cli_uart_rx_complete_isr(void);
void app_cli_uart_error_isr(void);

#endif
