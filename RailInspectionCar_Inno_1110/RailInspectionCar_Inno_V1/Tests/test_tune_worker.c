/* Run the actual tuning protocol suite through production USART1 line parsing
 * and app_cli_process with human CLI absent (D0/T1). The hardware byte source
 * is mocked; neither the command dispatcher nor tuning protocol is replaced. */
#include <assert.h>
#include <string.h>
#include "app_config.h"
#include "app_tune_cli.h"
#include "app_debug_cli.h"
#include "debug_uart.h"
#include "queue.h"
static uint8_t worker_input_command(char *line);
#define app_tune_command worker_input_command
#define main tune_suite_main
#include "test_tune.c"
#undef main
#undef app_tune_command

static char incoming[200],queued[4][192];
static unsigned incoming_size,incoming_pos,queue_size,cycles;
static unsigned diagnostic_services,uart_services;
QueueHandle_t xQueueCreate(unsigned count,unsigned size)
{assert(count==4U && size==192U);return (void *)1;}
BaseType_t xQueueReset(QueueHandle_t queue)
{assert(queue==(void *)1);queue_size=0U;return pdTRUE;}
BaseType_t xQueueSend(QueueHandle_t queue,const void *line,TickType_t wait)
{assert(queue==(void *)1 && !wait && queue_size<4U);strcpy(queued[queue_size++],line);return pdTRUE;}
BaseType_t xQueueReceive(QueueHandle_t queue,void *line,TickType_t wait)
{unsigned i;assert(queue==(void *)1 && !wait);if(!queue_size)return pdFALSE;strcpy(line,queued[0]);for(i=1;i<queue_size;i++)strcpy(queued[i-1],queued[i]);--queue_size;return pdTRUE;}
void debug_uart_init(void){}
void debug_uart_write_char(uint8_t value){(void)value;assert(0);}
void debug_uart_service(void){++uart_services;}
void debug_uart_rx_service(void){}
uint32_t debug_uart_rx_epoch(void){return 0U;}
uint32_t debug_uart_cycles(void){return cycles++;}
uint32_t debug_uart_parse_limit_cycles(void){return 200U;}
uint16_t debug_uart_rx_read(uint8_t *value,uint16_t length)
{assert(length==1U);if(incoming_pos==incoming_size)return 0U;*value=(uint8_t)incoming[incoming_pos++];return 1U;}
void debug_uart_rx_note_line_drop(uint8_t full){(void)full;assert(0);}
void debug_uart_rx_note_parse(uint32_t duration){assert(duration<=205U);}
void app_hmi_link_diag_service(void){++diagnostic_services;}
void vTaskDelay(TickType_t delay){assert(delay==1U);}
static uint8_t worker_input_command(char *line)
{
    assert(!queue_size && incoming_pos==incoming_size);
    assert(strlen(line)+2U<=sizeof(incoming));
    strcpy(incoming,line);strcat(incoming,"\n");
    incoming_size=(unsigned)strlen(incoming);incoming_pos=0U;
    while(incoming_pos<incoming_size || queue_size)app_cli_process();
    return 1U;
}
int main(void)
{
    int result;
    assert(!APP_DEBUG_CLI_ENABLED && APP_TUNE_UART_ENABLED);
    app_cli_init();
    result=tune_suite_main();
    /* This profile leaves all automatic diagnostics disabled; their production
     * header provides an inline no-op. Public TX must still run each cycle. */
    assert(uart_services && !diagnostic_services);
    puts("PASS D0/T1: actual tune protocol suite through production RX parser and shared worker");
    return result;
}
