/* The runner inserts the production parser, init and worker verbatim. Manual
 * command execution and tune protocol are tested by their separate suites;
 * this harness checks only their compile-time routing and shared services. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "debug_uart.h"
#include "FreeRTOS.h"
#include "queue.h"
#define CLI_LINE_SIZE 192U
#define CLI_QUEUE_LENGTH 4U
#if APP_UART_RX_ACTIVE
static char s_build_line[CLI_LINE_SIZE];
static uint16_t s_build_length;
static uint8_t s_line_overflow;
static QueueHandle_t s_line_queue;
static uint32_t s_rx_epoch;
#endif
#if APP_DEBUG_CLI_ENABLED
static uint32_t s_sync_debug_skipped, s_position_summary_skipped;
#endif
static unsigned queue_create, queue_count, input_pos, input_size, cycles;
static unsigned tx_init, tx_service, storage_service, tune_service, diag_service;
static unsigned rx_service, tune_init, tune_commands, manual_commands, delays;
static char queue_lines[4][CLI_LINE_SIZE];
static const char *input;
QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{ assert(count == 4U && size == CLI_LINE_SIZE); ++queue_create; return (void *)1; }
BaseType_t xQueueReset(QueueHandle_t q) { assert(q == (void *)1); queue_count = 0; return pdTRUE; }
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{ assert(q == (void *)1 && !wait); if(queue_count == 4U) return pdFALSE; strcpy(queue_lines[queue_count++],item); return pdTRUE; }
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{ unsigned i; assert(q == (void *)1 && !wait); if(!queue_count) return pdFALSE; strcpy(item, queue_lines[0]); for(i=1;i<queue_count;i++) strcpy(queue_lines[i-1],queue_lines[i]); --queue_count; return pdTRUE; }
uint32_t debug_uart_cycles(void) { return cycles++; }
uint32_t debug_uart_parse_limit_cycles(void) { return 200U; }
void debug_uart_rx_service(void) { ++rx_service; }
uint32_t debug_uart_rx_epoch(void) { return 0U; }
uint16_t debug_uart_rx_read(uint8_t *p,uint16_t n)
{ assert(n == 1U); if(input_pos == input_size) return 0U; *p = (uint8_t)input[input_pos++]; return 1U; }
void debug_uart_rx_note_line_drop(uint8_t full) { (void)full; assert(0); }
void debug_uart_rx_note_parse(uint32_t elapsed) { assert(elapsed <= 205U); }
void debug_uart_init(void) { ++tx_init; }
void debug_uart_service(void) { ++tx_service; }
void app_sync_storage_service(void) { ++storage_service; }
void app_tune_init(void) { ++tune_init; }
void app_tune_service(void) { ++tune_service; }
void app_hmi_link_diag_service(void) { ++diag_service; }
void vTaskDelay(TickType_t ticks) { assert(ticks == 1U); ++delays; }
#if APP_TUNE_UART_ENABLED
uint8_t app_tune_command(char *line)
{ ++tune_commands; return !strncmp(line, "tune ", 5U); }
#endif
#if APP_DEBUG_CLI_ENABLED
static void app_cli_execute(char *line)
{ assert(!strcmp(line,"status") || (!APP_TUNE_UART_ENABLED && !strcmp(line,"tune hello 1"))); ++manual_commands; }
#endif
/* PRODUCTION_INIT */
/* PRODUCTION_PARSER */
/* PRODUCTION_PROCESS */
int main(void)
{
    unsigned i;
    app_cli_init();
    assert(tx_init == 1U && tune_init == 1U);
    assert(queue_create == (APP_UART_RX_ACTIVE ? 1U : 0U));
    input = "tune hello 1\nstatus\n";
    input_size = (unsigned)strlen(input);
    for(i=0;i<3U;i++) app_cli_process();
    assert(tx_service == 6U && storage_service == 3U && diag_service == 3U);
    assert(tune_service == 3U && delays == 3U);
    assert(rx_service == (APP_UART_RX_ACTIVE ? 3U : 0U));
    assert(input_pos == (APP_UART_RX_ACTIVE ? input_size : 0U));
    assert(tune_commands == (APP_TUNE_UART_ENABLED ? 2U : 0U));
    assert(manual_commands == (APP_DEBUG_CLI_ENABLED ? (APP_TUNE_UART_ENABLED ? 1U : 2U) : 0U));
    puts("PASS production CLI worker: D/T dispatch, shared TX, Flash, diagnostics and bounded RX");
    return 0;
}
