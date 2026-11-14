#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp_rs485.h"
#include "app_config.h"
#include "semphr.h"
#include "usart.h"
#include "main.h"
#include "stm32f4xx_it.h"

/* 直接链接生产接收驱动，用寄存器和HAL桩模拟DMA及中断事件。 */
DMA_Stream_TypeDef test_stream;
GPIO_TypeDef test_gpio;
static UART_TypeDef uart;
static DMA_HandleTypeDef dma = { &test_stream, NULL, { DMA_CIRCULAR, DMA_CHANNEL_4 } };
UART_HandleTypeDef huart2 = { &uart, &dma, 0U, 0U };
static uint8_t *dma_buffer;
static unsigned dma_size, write_position, notifications, starts, aborts;
static unsigned tx_aborts, fail_start, fail_abort, fail_dma_abort, skip_tx_done;
static unsigned inject_error_on_unmask;
static unsigned fail_dma_init, dma_needs_init;
static uint32_t mask;
static TickType_t ticks;
UART_TypeDef test_uart1;
UART_HandleTypeDef huart1 = { &test_uart1, NULL, 0U, 0U };
UART_TypeDef test_uart5;
UART_HandleTypeDef huart3, huart4, huart5 = { &test_uart5, NULL, 0U, 0U }, huart6;
DMA_HandleTypeDef hdma_usart2_rx, hdma_usart2_tx;
DMA_HandleTypeDef hdma_usart1_rx, hdma_usart1_tx;
DMA_HandleTypeDef hdma_spi2_rx, hdma_spi2_tx;
DMA_HandleTypeDef hdma_uart5_rx, hdma_uart5_tx;
TIM_HandleTypeDef htim1, htim2;
static unsigned cli_rx_events, cli_error_events, tx_completions, debug_rx_dma_events;
static unsigned debug_rx_aborts, debug_tx_aborts;
static unsigned debug_tx_events, debug_error_events, debug_dma_events;
static unsigned nrf_rx_events, nrf_tx_events, pulse9_events, pulse11_events;
static unsigned hmi_rx_dma_events, hmi_tx_dma_events, unexpected_hal_events;

void servo_pulse_stop_all(void) {}
void NRF24L01_DmaRxIsr(void) { nrf_rx_events++; }
void NRF24L01_DmaTxIsr(void) { nrf_tx_events++; }
void lcd_port_dma_tx_isr(void) { }
void servo_pulse_update_tim9_isr(void) { pulse9_events++; }
void servo_pulse_update_tim11_isr(void) { pulse11_events++; }
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *timer)
{ (void)timer; unexpected_hal_events++; }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *handle)
{
    if (handle == &hdma_usart1_tx) debug_dma_events++;
    else if (handle == &hdma_usart1_rx) debug_rx_dma_events++;
    else if (handle == &hdma_uart5_rx) hmi_rx_dma_events++;
    else if (handle == &hdma_uart5_tx) hmi_tx_dma_events++;
    else unexpected_hal_events++;
}
void app_cli_uart_rx_complete_isr(void) { cli_rx_events++; }
void app_cli_uart_error_isr(void)
{ cli_error_events++; huart1.ErrorCode = 0U; }
void debug_uart_rx_event_isr(uint16_t position, uint32_t kind)
{ (void)position; (void)kind; cli_rx_events++; }
void debug_uart_rx_error_isr(uint32_t sr, uint32_t hal)
{ (void)sr; (void)hal; cli_error_events++; }
void debug_uart_rx_abort_isr(void) { debug_rx_aborts++; }
void debug_uart_tx_abort_isr(void) { debug_tx_aborts++; }
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart,uint16_t position);
void HAL_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_AbortTransmitCpltCallback(UART_HandleTypeDef *huart);
void debug_uart_tx_complete_isr(void) { debug_tx_events++; }
void debug_uart_tx_error_isr(void)
{ assert(huart1.ErrorCode == HAL_UART_ERROR_DMA); debug_error_events++; }
void bsp_hmi_uart_rx_complete_isr(void) {}
void bsp_hmi_uart_tx_complete_isr(void) {}
void bsp_hmi_uart_error_isr(void) {}
void bsp_hmi_uart_irq_entry_isr(void) {}
void bsp_hmi_uart_dma_irq_isr(uint8_t transmit) { (void)transmit; }
void bsp_hmi_uart_rx_event_isr(uint16_t position, uint32_t kind) { (void)position; (void)kind; }
void bsp_hmi_uart_rx_abort_isr(void) {}
void bsp_hmi_uart_tx_abort_isr(void) {}
uint32_t HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *uart) { (void)uart; return 0U; }

/* HAL桩仅模拟错误和TC分支；被测IDLE入口及UART回调直接链接生产代码。 */
void HAL_UART_IRQHandler(UART_HandleTypeDef *h)
{
    uint32_t flags = h->Instance->SR;
    if ((flags & 15U) && __HAL_UART_GET_IT_SOURCE(h, UART_IT_ERR)) {
        h->ErrorCode = HAL_UART_ERROR_ORE;
        HAL_UART_ErrorCallback(h);
        return;
    }
    if ((flags & UART_FLAG_TC) && __HAL_UART_GET_IT_SOURCE(h, UART_IT_TC)) {
        __HAL_UART_DISABLE_IT(h, UART_IT_TC);
        tx_completions++;
        HAL_UART_TxCpltCallback(h);
    }
}

TickType_t xTaskGetTickCount(void) { return ticks; }
TickType_t xTaskGetTickCountFromISR(void) { return ticks; }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken)
{ assert(task != NULL); notifications++; *woken = pdTRUE; }
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask = 1U; }
void __set_PRIMASK(uint32_t value)
{
    mask = value;
    if (!mask && inject_error_on_unmask) {
        inject_error_on_unmask = 0U;
        huart2.ErrorCode = HAL_UART_ERROR_ORE;
        rs485_uart_error_isr();
    }
}
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{ unsigned *p = malloc(sizeof(*p)); assert(p); *p = 1U; return p; }
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{ unsigned *p = xSemaphoreCreateMutex(); *p = 0U; return p; }
void vSemaphoreDelete(SemaphoreHandle_t semaphore) { free(semaphore); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    unsigned *p = semaphore;
    if (*p) { *p = 0U; return pdTRUE; }
    ticks += timeout;
    return pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{ *(unsigned *)semaphore = 1U; return pdTRUE; }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t *woken)
{ *woken = pdTRUE; return xSemaphoreGive(semaphore); }
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *h, uint8_t *data, uint16_t size)
{
    assert(mask == 1U);
    starts++;
    if (fail_start || dma_needs_init) return HAL_ERROR;
    dma_buffer = data; dma_size = size; write_position = 0U;
    h->hdmarx->Instance->NDTR = size;
    h->hdmarx->Instance->CR = DMA_SxCR_EN | DMA_SxCR_CIRC;
    h->Instance->CR3 |= USART_CR3_DMAR;
    __HAL_UART_ENABLE_IT(h, UART_IT_ERR);
    h->ErrorCode = 0U; h->RxState = 1U;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *h)
{
    if (fail_dma_abort) {
        h->Instance->CR &= ~DMA_SxCR_EN;
        dma_needs_init = 1U;
        return HAL_TIMEOUT;
    }
    h->Instance->CR &= ~DMA_SxCR_EN;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h)
{
    if (fail_dma_init) return HAL_ERROR;
    assert(!(h->Instance->CR & DMA_SxCR_EN));
    dma_needs_init = 0U;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h)
{
    aborts++;
    if (fail_abort) return HAL_TIMEOUT;
    if (h->Instance->CR3 & USART_CR3_DMAR) {
        h->Instance->CR3 &= ~USART_CR3_DMAR;
        if (HAL_DMA_Abort(h->hdmarx) != HAL_OK) return HAL_TIMEOUT;
    }
    h->RxState = 0U;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, uint8_t *data, uint16_t size)
{
    (void)h; (void)data; assert(size > 0U);
    if (!skip_tx_done) rs485_uart_tx_complete_isr();
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *h)
{ (void)h; tx_aborts++; return HAL_OK; }

static rs485_stats_t stats(void)
{ rs485_stats_t s; rs485_get_stats(&s); return s; }
static void feed(unsigned count, unsigned seed)
{
    unsigned i;
    for (i = 0; i < count; ++i) {
        dma_buffer[write_position++] = (uint8_t)(seed + i);
        if (write_position == dma_size) write_position = 0U;
        test_stream.NDTR = dma_size - write_position;
        if (write_position == dma_size / 2U) rs485_uart_rx_half_isr();
        if (!write_position) rs485_uart_rx_complete_isr();
    }
}
static void expect_bytes(unsigned count, unsigned seed)
{
    uint8_t data[APP_RS485_RING_SIZE];
    unsigned i;
    assert(rs485_read(data, sizeof(data)) == count);
    for (i = 0; i < count; ++i) assert(data[i] == (uint8_t)(seed + i));
    assert(rs485_available() == 0U);
}
int main(void)
{
    unsigned n, old_starts;
    uint8_t tx[8] = {0};
    dma.Parent = &huart2;
    test_gpio.MODER = 2U << 6;
    test_gpio.AFR[0] = 7U << 12;

    /* 核验新增UART5 DMA分派，专用NRF及脉冲入口不能重复消费HAL事件。 */
    DMA1_Stream0_IRQHandler();
    DMA1_Stream7_IRQHandler();
    DMA1_Stream3_IRQHandler();
    DMA1_Stream4_IRQHandler();
    TIM1_BRK_TIM9_IRQHandler();
    TIM1_TRG_COM_TIM11_IRQHandler();
    assert(hmi_rx_dma_events == 1U && hmi_tx_dma_events == 1U);
    assert(nrf_rx_events == 1U && nrf_tx_events == 1U);
    assert(pulse9_events == 1U && pulse11_events == 1U);
    assert(unexpected_hal_events == 0U);

    /* 首次启动失败可恢复，重复初始化不会重建同步对象或重启DMA。 */
    fail_start = 1U;
    assert(rs485_init() == RS485_HAL_ERROR);
    assert(stats().dma_start_failures == 1U && stats().recovery_pending);
    fail_start = 0U;
    rs485_service();
    assert(stats().dma_running && !stats().recovery_pending);
    old_starts = starts;
    assert(rs485_init() == RS485_OK && starts == old_starts);
    assert(stats().pa3_mode == 2U && stats().pa3_af == 7U);
    rs485_bind_rx_task((TaskHandle_t)&ticks);

    /* 短帧、重复事件、半满边界和回绕均只搬运一次。 */
    feed(7U, 0U);
    assert(rs485_available() == 0U);
    rs485_uart_idle_isr();
    rs485_uart_idle_isr();
    rs485_uart_rx_half_isr();
    assert(stats().rx_bytes == 7U && notifications == 1U);
    expect_bytes(7U, 0U);
    feed(121U, 7U);
    rs485_uart_idle_isr();
    expect_bytes(121U, 7U);
    feed(128U, 128U);
    rs485_uart_idle_isr();
    expect_bytes(128U, 128U);
    feed(9U, 0U);
    rs485_uart_idle_isr();
    expect_bytes(9U, 0U);
    assert(stats().rx_bytes == 265U);

    /* 软件环满时保留最新数据，丢弃字节数应准确累计。 */
    feed(600U, 0U);
    rs485_uart_idle_isr();
    assert(stats().rx_bytes == 865U && stats().rx_overflows == 89U);
    expect_bytes(511U, 89U);

    /* SR已清除时仍从HAL错误码获取原因；恢复不会阻塞在ISR。 */
    huart2.ErrorCode = HAL_UART_ERROR_ORE | HAL_UART_ERROR_FE | HAL_UART_ERROR_DMA;
    uart.SR = 0U;
    n = aborts;
    rs485_uart_error_isr();
    assert(aborts == n && !stats().dma_running && stats().recovery_pending);
    assert(stats().uart_error_flags == (UART_FLAG_ORE | UART_FLAG_FE));
    assert(stats().hal_error_flags & HAL_UART_ERROR_DMA);
    assert(rs485_send(tx, sizeof(tx)) == RS485_HAL_ERROR);
    ticks += 20U;
    fail_start = 1U;
    rs485_service();
    old_starts = starts;
    rs485_service();
    assert(starts == old_starts && stats().recovery_pending);
    fail_start = 0U;
    ticks += 20U;
    /* 启动后马上再次报错，恢复函数不能覆盖新产生的恢复请求。 */
    inject_error_on_unmask = 1U;
    rs485_service();
    assert(stats().recovery_pending && !stats().dma_running);
    ticks += 20U;
    rs485_service();
    assert(stats().dma_running && !stats().recovery_pending);

    /* 终止失败后仍可重试，DMAR已关闭而流仍使能时也能恢复。 */
    rs485_uart_error_isr();
    fail_abort = 1U; ticks += 20U;
    rs485_service();
    assert(stats().rx_abort_failures == 1U && stats().recovery_pending);
    fail_abort = 0U; fail_dma_abort = 1U;
    uart.CR3 &= ~USART_CR3_DMAR;
    ticks += 20U;
    rs485_service();
    assert(stats().rx_abort_failures == 2U && stats().recovery_pending);
    fail_dma_abort = 0U; fail_dma_init = 1U; ticks += 20U;
    rs485_service();
    assert(stats().recovery_pending && !stats().dma_running);
    fail_dma_init = 0U; ticks += 20U;
    rs485_service();
    feed(7U, 11U);
    rs485_uart_idle_isr();
    expect_bytes(7U, 11U);
    test_stream.CR &= ~DMA_SxCR_EN;
    assert(!stats().dma_running);
    test_stream.CR |= DMA_SxCR_EN;

    /* TX完成与超时互不污染RX状态，发送超时后下一次发送可成功。 */
    assert(rs485_send(tx, sizeof(tx)) == RS485_OK);
    skip_tx_done = 1U;
    assert(rs485_send(tx, sizeof(tx)) == RS485_TIMEOUT && tx_aborts == 1U);
    assert(stats().dma_running);
    skip_tx_done = 0U;
    assert(rs485_send(tx, sizeof(tx)) == RS485_OK);
    /* 非IDLE中断不能搬运数据，IDLE关闭时也不能误计事件。 */
    n = stats().idle_interrupts;
    old_starts = stats().irq_entries;
    feed(7U, 31U);
    uart.SR = UART_FLAG_TC;
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_TC);
    USART2_IRQHandler();
    assert(stats().idle_interrupts == n && rs485_available() == 0U);
    assert(tx_completions == 1U);
    __HAL_UART_DISABLE_IT(&huart2, UART_IT_IDLE);
    uart.SR = UART_FLAG_IDLE;
    USART2_IRQHandler();
    assert(stats().idle_interrupts == n);
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
    USART2_IRQHandler();
    assert(!(uart.SR & UART_FLAG_IDLE));
    assert(stats().idle_interrupts == n + 1U);
    expect_bytes(7U, 31U);
    USART2_IRQHandler();
    assert(stats().idle_interrupts == n + 1U);
    assert(stats().irq_entries == old_starts + 4U);

    /* IDLE与错误同时出现时记录一次错误，TC仍可正常释放发送信号。 */
    n = stats().uart_errors;
    huart2.ErrorCode = 0U;
    uart.SR = UART_FLAG_IDLE | UART_FLAG_ORE | UART_FLAG_TC;
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_TC);
    USART2_IRQHandler();
    assert(stats().uart_errors == n + 1U && stats().recovery_pending);
    assert(tx_completions == 2U);
    USART2_IRQHandler();
    assert(stats().uart_errors == n + 1U);
    ticks += 20U;
    rs485_service();
    assert(stats().dma_running && !stats().recovery_pending);

    /* 无IDLE时走HAL错误回调，任务恢复后仍可接收短帧。 */
    uart.SR = UART_FLAG_ORE;
    USART2_IRQHandler();
    assert(stats().uart_errors == n + 2U && stats().recovery_pending);
    ticks += 20U;
    rs485_service();
    feed(7U, 51U);
    uart.SR = UART_FLAG_IDLE;
    USART2_IRQHandler();
    expect_bytes(7U, 51U);

    /* USART1和其他串口不能污染USART2的接收、错误及DMA事件统计。 */
    n = stats().uart_errors;
    old_starts = stats().dma_half_events + stats().dma_full_events;
    HAL_UART_RxHalfCpltCallback(&huart1);
    HAL_UART_RxCpltCallback(&huart1);
    huart1.ErrorCode = HAL_UART_ERROR_DMA;
    HAL_UART_ErrorCallback(&huart1);
    HAL_UART_TxCpltCallback(&huart1);
    DMA2_Stream7_IRQHandler();
    DMA2_Stream2_IRQHandler();
    HAL_UARTEx_RxEventCallback(&huart1,20U);
    HAL_UART_AbortReceiveCpltCallback(&huart1);
    HAL_UART_AbortTransmitCpltCallback(&huart1);
    HAL_UART_RxCpltCallback(&huart3);
    HAL_UART_ErrorCallback(&huart3);
    assert(cli_rx_events == 2U && cli_error_events == 1U);
    assert(debug_rx_dma_events == 1U && debug_rx_aborts == 1U && debug_tx_aborts == 1U);
    assert(debug_tx_events == 1U && debug_error_events == 1U && debug_dma_events == 1U);
    assert(stats().uart_errors == n);
    assert(stats().dma_half_events + stats().dma_full_events == old_starts);
    puts("RS485 RX tests passed");
    return 0;
}
