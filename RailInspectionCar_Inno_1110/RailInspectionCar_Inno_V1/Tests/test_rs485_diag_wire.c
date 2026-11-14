/* Existing fixture links the actual UART IRQ bridge and RS485 production driver. */
#define main baseline_rs485_main
#include "test_rs485.c"
#undef main
int main(void)
{
    rs485_diag_times_t time;
    uint8_t tx[8]={0};
    assert(baseline_rs485_main()==0);
    rs485_diag_begin();ticks=1000U;
    /* RX DMA completion must never claim a TX UART TC. */
    HAL_UART_RxCpltCallback(&huart2);
    rs485_diag_times(&time);assert(!(time.valid&2U));
    assert(rs485_send(tx,sizeof(tx))==RS485_OK);
    rs485_diag_times(&time);assert((time.valid&3U)==3U && time.start_ms==1000U && time.tc_ms==1000U);
    ticks=1002U;feed(7U,90U);uart.SR=UART_FLAG_IDLE;USART2_IRQHandler();
    rs485_diag_times(&time);assert((time.valid&7U)==7U && time.rx_ms==1002U);
    rs485_diag_begin();skip_tx_done=1U;
    assert(rs485_send(tx,sizeof(tx))==RS485_TIMEOUT);
    rs485_diag_times(&time);assert((time.valid&3U)==1U);
    puts("PASS production RS485 diagnostic wire timestamps: RX DMA != UART TC, actual TC, capture, missing TC");
    return 0;
}
