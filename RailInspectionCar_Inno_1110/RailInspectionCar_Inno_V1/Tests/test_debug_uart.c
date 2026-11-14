#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "debug_uart.h"
static UART_TypeDef uart;
static DMA_Stream_TypeDef rx_stream,tx_stream;
static DMA_HandleTypeDef rx_dma={&rx_stream,0U},tx_dma={&tx_stream,0U};
UART_HandleTypeDef huart1={&uart,&rx_dma,&tx_dma,0U};
test_dwt_t test_dwt; test_core_t test_core;
uint32_t SystemCoreClock=168000000U;
static uint32_t basepri,ipsr,tick,starts,aborts,rx_starts;
static uint8_t *active,*rx;
static uint16_t active_length,rx_pos;
static unsigned fail_tx,fail_rx,fail_abort,async_abort,inject_producer;
static uint8_t output[DEBUG_UART_TX_BUFFER_SIZE*8U];
static unsigned output_length;
uint32_t __get_BASEPRI(void){return basepri;}
void __set_BASEPRI_MAX(uint32_t v){if(!basepri || v<basepri)basepri=v;}
void __set_BASEPRI(uint32_t v){basepri=v;test_dwt.CYCCNT+=3U;}
uint32_t __get_IPSR(void){return ipsr;}
void test_dmb(void){
 if(inject_producer){inject_producer=0U;debug_uart_write_char('!');}
}
uint32_t HAL_GetTick(void){return tick;}
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h,uint8_t *p,uint16_t n){
 assert(h==&huart1 && n==512U && basepri==0U);rx_starts++;
 if(fail_rx){fail_rx=0U;return HAL_BUSY;}
 rx=p;rx_pos=0U;rx_stream.NDTR=n;rx_stream.flags=0U;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h,uint8_t *p,uint16_t n){
 assert(h==&huart1 && n && n<=2047U && basepri==0U && ipsr==0U);starts++;
 if(fail_tx){fail_tx=0U;return HAL_BUSY;} assert(!active);
 active=p;active_length=n;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *h){
 assert(h==&huart1 && basepri==0U && ipsr==0U);aborts++;
 if(fail_abort){fail_abort=0U;return HAL_ERROR;}
 if(!async_abort) debug_uart_rx_abort_isr();return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit_IT(UART_HandleTypeDef *h){
 assert(h==&huart1 && basepri==0U && ipsr==0U);aborts++;
 if(fail_abort){fail_abort=0U;return HAL_ERROR;}
 if(!async_abort){active=NULL;tx_dma.ErrorCode=0U;debug_uart_tx_abort_isr();}return HAL_OK;
}
static debug_uart_stats_t stats(void){debug_uart_stats_t s;debug_uart_get_stats(&s);return s;}
static void complete(void){
 assert(active);assert(output_length+active_length<=sizeof(output));
 memcpy(output+output_length,active,active_length);output_length+=active_length;active=NULL;
 ipsr=16U;debug_uart_tx_complete_isr();ipsr=0U;
 assert(!active); /* ISR never starts next DMA. */
}
static void drain(void){while(stats().buffer_used){debug_uart_service();if(active)complete();}}
static void receive(const uint8_t *p,unsigned n,unsigned callbacks){
 while(n--){rx[rx_pos++]=*p++; if(rx_pos==512U){rx_pos=0U;rx_stream.flags|=1U;
   if(callbacks){rx_stream.flags&=~1U;ipsr=16U;debug_uart_rx_event_isr(512U,HAL_UART_RXEVENT_TC);ipsr=0U;}}
 rx_stream.NDTR=512U-rx_pos;}
 if(callbacks){ipsr=16U;debug_uart_rx_event_isr(rx_pos,HAL_UART_RXEVENT_IDLE);ipsr=0U;}
}
static void write_bytes(unsigned n,uint8_t c){while(n--)debug_uart_write_char(c);}
int main(void){
 uint8_t data[600],readback[600],frame[256];unsigned i,before;uint32_t epoch;
 debug_uart_init();assert(rx_starts==1U && stats().rx_running);
 basepri=0x50U;debug_uart_write_char('a');assert(basepri==0x50U);basepri=0U;
 debug_uart_service();write_bytes(100U,'b');complete();drain();
 assert(output_length==101U && output[0]=='a' && output[100]=='b');
 output_length=0U;
 for(i=0U;i<6000U;++i)debug_uart_write_char((uint8_t)(i%251U));
 drain();assert(output_length==6000U);
 for(i=0U;i<6000U;++i)assert(output[i]==(uint8_t)(i%251U));
 output_length=0U;
 write_bytes(DEBUG_UART_TX_BUFFER_SIZE+2U,'x');assert(stats().dropped_bytes==3U);drain();
 assert(stats().buffer_high_water==8191U);
 memset(frame,0xA5,sizeof(frame));before=stats().queued_bytes;
 inject_producer=1U;assert(debug_uart_try_write_frame(frame,256U,1024U));
 assert(stats().tx_lock_drops==1U && stats().queued_bytes-before==256U);output_length=0U;drain();
 assert(output_length==256U);for(i=0U;i<256U;++i)assert(output[i]==0xA5U);
 ipsr=16U;debug_uart_write_char('!');ipsr=0U;assert(stats().tx_isr_drops==1U);
 debug_uart_write_char('f');fail_tx=1U;debug_uart_service();assert(stats().dma_start_failures==1U);debug_uart_service();
 huart1.ErrorCode=HAL_UART_ERROR_DMA;rx_dma.ErrorCode=1U;debug_uart_tx_error_isr();assert(stats().dma_error_count==0U);
 rx_dma.ErrorCode=0U;tx_dma.ErrorCode=1U;debug_uart_rx_error_isr(0U,HAL_UART_ERROR_DMA);assert(!stats().rx_recovery_pending);
 tx_dma.ErrorCode=1U;debug_uart_tx_error_isr();debug_uart_tx_complete_isr();
 assert(stats().dma_error_count==1U && stats().buffer_used==1U);
 fail_abort=1U;debug_uart_service();assert(active);debug_uart_service();complete();drain();
 debug_uart_write_char('g');debug_uart_service();tick+=501U;debug_uart_service();complete();drain();assert(stats().dma_error_count==2U);
 /* RX: normal chunks, circular wrap, a TC pending before its callback. */
 (void)debug_uart_rx_read(readback,1U);
 for(i=0U;i<sizeof(data);++i)data[i]=(uint8_t)i;
 receive(data,250U,1U);assert(debug_uart_rx_read(readback,256U)==250U && !memcmp(data,readback,250U));
 receive(data,250U,1U);assert(debug_uart_rx_read(readback,256U)==250U && !memcmp(data,readback,250U));
 receive(data,12U,0U);rx_stream.NDTR=0U;rx_stream.flags=0U;assert(debug_uart_rx_read(readback,12U)==12U && !memcmp(data,readback,12U));
 rx_stream.NDTR=512U;rx_stream.flags=1U;
 receive(data+12U,18U,0U);assert(debug_uart_rx_read(readback,18U)==18U && !memcmp(data+12U,readback,18U));
 rx_stream.flags&=~1U;debug_uart_rx_event_isr(512U,HAL_UART_RXEVENT_TC);
 /* Real overwrite is discarded as a unit; caller sees a new epoch. */
 epoch=debug_uart_rx_epoch();receive(data,600U,1U);assert(debug_uart_rx_read(readback,256U)==0U);
 assert(debug_uart_rx_epoch()!=epoch && stats().rx_overflows==1U);
 receive((const uint8_t *)"new\n",4U,1U);assert(debug_uart_rx_read(readback,4U)==4U && !memcmp(readback,"new\n",4U));
 epoch=debug_uart_rx_epoch();debug_uart_rx_error_isr(UART_FLAG_ORE|UART_FLAG_FE,0U);
 debug_uart_rx_error_isr(0U,HAL_UART_ERROR_ORE|HAL_UART_ERROR_FE);
 assert(stats().rx_ore==1U && stats().rx_fe==1U && stats().rx_uart_errors==1U);
 assert(debug_uart_rx_epoch()==epoch+1U && debug_uart_rx_read(readback,5U)==0U);
 async_abort=1U;debug_uart_rx_service();assert(stats().rx_recovery_pending);
 tick+=101U;debug_uart_rx_service();assert(stats().rx_abort_failures==1U && rx_starts==1U);
 async_abort=0U;debug_uart_rx_abort_isr();fail_rx=1U;debug_uart_rx_service();assert(stats().rx_start_failures==1U);
 tick+=10U;debug_uart_rx_service();assert(stats().rx_running && !stats().rx_recovery_pending && rx_starts==3U);
 receive(data,30U,1U);assert(debug_uart_rx_read(readback,30U)==30U && !memcmp(data,readback,30U));
 /* Long idle followed by a short hello must not be mistaken for an overwrite. */
 epoch=debug_uart_rx_epoch();tick+=5000U;receive(data,10U,0U);
 assert(debug_uart_rx_read(readback,10U)==10U && debug_uart_rx_epoch()==epoch);
 /* A TC left pending across multiple turns can alias to zero apparent depth. */
 receive(data,472U,0U);assert(debug_uart_rx_read(readback,472U)==472U);
 tick+=45U;receive(data,512U,0U);
 assert(!debug_uart_rx_read(readback,10U) && debug_uart_rx_epoch()==epoch+1U);
 rx_stream.flags&=~1U;debug_uart_rx_event_isr(512U,HAL_UART_RXEVENT_TC);
 receive((const uint8_t *)"ok\n",3U,1U);assert(debug_uart_rx_read(readback,3U)==3U);
 debug_uart_rx_note_line_drop(0U);debug_uart_rx_note_line_drop(1U);debug_uart_rx_note_parse(45U);
 assert(stats().rx_line_overflows==1U && stats().rx_queue_drops==1U && stats().rx_parse_max_cycles==45U);
 debug_uart_get_stats(NULL);puts("PASS USART1 RX circular DMA, overwrite/error epochs, async recovery, TX publication and ISR isolation");return 0;
}
