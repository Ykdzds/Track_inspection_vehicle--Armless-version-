#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "debug_uart.h"
static UART_TypeDef uart;
static DMA_Stream_TypeDef rx_stream, tx_stream;
static DMA_HandleTypeDef rx_dma={&rx_stream,0U},tx_dma={&tx_stream,0U};
UART_HandleTypeDef huart1={&uart,&rx_dma,&tx_dma,0U};
test_dwt_t test_dwt;
test_core_t test_core;
uint32_t SystemCoreClock=168000000U;
static uint32_t basepri,tick;
static unsigned rx_starts,rx_aborts,tx_starts,tx_aborts,fail_tx;
static uint8_t *active;
static uint16_t active_length;
uint32_t __get_BASEPRI(void){return basepri;}
void __set_BASEPRI_MAX(uint32_t n){if(!basepri || n<basepri)basepri=n;}
void __set_BASEPRI(uint32_t n){basepri=n;}
uint32_t __get_IPSR(void){return 0U;}
void test_dmb(void){}
uint32_t HAL_GetTick(void){return tick;}
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h,uint8_t *p,uint16_t n)
{(void)h;(void)p;(void)n;++rx_starts;return HAL_ERROR;}
HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *h)
{(void)h;++rx_aborts;return HAL_ERROR;}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h,uint8_t *p,uint16_t n)
{
    assert(h==&huart1 && !basepri && !active && n);++tx_starts;
    if(fail_tx){fail_tx=0U;return HAL_BUSY;}
    active=p;active_length=n;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit_IT(UART_HandleTypeDef *h)
{assert(h==&huart1 && !basepri);++tx_aborts;active=NULL;tx_dma.ErrorCode=0U;debug_uart_tx_abort_isr();return HAL_OK;}
static debug_uart_stats_t stats(void){debug_uart_stats_t s;debug_uart_get_stats(&s);return s;}
int main(void)
{
    uint8_t byte=0xA5;
    debug_uart_init();
    assert(!rx_starts && !stats().rx_running && !stats().rx_recovery_pending);
    debug_uart_rx_event_isr(512U,HAL_UART_RXEVENT_TC);
    debug_uart_rx_error_isr(15U,31U);debug_uart_rx_abort_isr();debug_uart_rx_service();
    debug_uart_rx_note_line_drop(1U);debug_uart_rx_note_parse(99U);
    assert(!debug_uart_rx_read(&byte,1U) && byte==0xA5 && !debug_uart_rx_epoch());
    assert(!rx_starts && !rx_aborts && !stats().rx_uart_errors && !stats().rx_queue_drops);
    assert(debug_uart_try_write_frame((const uint8_t *)"HLD log\n",8U,0U));
    fail_tx=1U;debug_uart_service();assert(!active && stats().dma_start_failures==1U);
    debug_uart_service();assert(active_length==8U && !memcmp(active,"HLD log\n",8U));
    tx_dma.ErrorCode=1U;debug_uart_tx_error_isr();debug_uart_service();
    assert(tx_aborts==1U && active && active_length==8U && stats().buffer_used==8U);
    active=NULL;debug_uart_tx_complete_isr();debug_uart_service();
    assert(!stats().buffer_used && stats().sent_bytes==8U && tx_starts==3U);
    assert(!rx_starts && !rx_aborts && !stats().rx_running);
    puts("PASS logging-only USART1: no RX DMA/state, retained TX output/retry/recovery");
    return 0;
}
