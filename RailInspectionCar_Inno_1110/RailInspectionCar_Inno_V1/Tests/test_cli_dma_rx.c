#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "debug_uart.h"
#define CLI_LINE_SIZE 192U
#define pdTRUE 1
static char s_build_line[CLI_LINE_SIZE];
static uint16_t s_build_length;
static uint8_t s_line_overflow;
static void *s_line_queue=(void *)1;
static uint32_t s_rx_epoch;
static uint8_t input[1024];static unsigned in_length,in_pos;
static uint32_t epoch,cycles,queue_drops,line_drops,service_calls;
static char queued[4][192];static unsigned queued_count;
uint32_t debug_uart_cycles(void){return cycles++;}
uint32_t debug_uart_parse_limit_cycles(void){return 200U;}
void debug_uart_rx_service(void){service_calls++;}
uint32_t debug_uart_rx_epoch(void){return epoch;}
uint16_t debug_uart_rx_read(uint8_t *p,uint16_t n){assert(n==1U);if(in_pos==in_length)return 0;*p=input[in_pos++];return 1;}
void debug_uart_rx_note_line_drop(uint8_t q){if(q)queue_drops++;else line_drops++;}
void debug_uart_rx_note_parse(uint32_t c){assert(c<=205U);}
int xQueueReset(void *q){assert(q==(void *)1);queued_count=0U;return pdTRUE;}
int xQueueSend(void *q,const void *p,unsigned wait){assert(q==(void *)1 && !wait);if(queued_count==4U)return 0;strcpy(queued[queued_count++],p);return pdTRUE;}
/* PRODUCTION_PARSER */
static void feed(const char *s){in_length=(unsigned)strlen(s);in_pos=0U;memcpy(input,s,in_length);while(in_pos<in_length)app_cli_rx_service();}
int main(void){
 char longline[400];
 feed("tune hello 1 0000\r\nstatus\n");assert(queued_count==2U && !strcmp(queued[0],"tune hello 1 0000") && !strcmp(queued[1],"status"));
 queued_count=0U;feed("partial");assert(!queued_count);epoch++;
 feed("unsafe tail\nstatus\n");assert(queued_count==1U && !strcmp(queued[0],"status"));
 queued_count=0U;memset(longline,'X',sizeof(longline));longline[350]='\n';strcpy(longline+351,"status\n");feed(longline);
 assert(queued_count==1U && !strcmp(queued[0],"status") && line_drops==1U);
 queued_count=0U;feed("a\nb\nc\nd\ne\n");assert(queued_count==4U && queue_drops==1U);
 queued_count=0U;feed("bad\x01suffix\nstatus\n");assert(queued_count==1U && !strcmp(queued[0],"status"));
 assert(service_calls>4U);puts("PASS production CLI DMA parser: bounded work, residual discard, non-ASCII, long lines and queue congestion");return 0;
}
