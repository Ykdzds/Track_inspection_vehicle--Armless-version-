#include <assert.h>
#include <stdio.h>
#include "main.h"

/* Inject hardware TC between the ISR flag sample and its W1C write. */
static unsigned inject_tc;
static void race_clear(DMA_HandleTypeDef *dma, uint32_t flags)
{
    if (inject_tc) {
        dma->Flags |= __HAL_DMA_GET_TC_FLAG_INDEX(dma);
        inject_tc = 0U;
    }
    dma->Flags &= ~flags;
}
#undef __HAL_DMA_CLEAR_FLAG
#define __HAL_DMA_CLEAR_FLAG(h, flags) race_clear(h, flags)
#include "NRF24L01.c"

static SPI_TypeDef spi;
static DMA_Stream_TypeDef rx_stream, tx_stream;
static DMA_HandleTypeDef rx_dma = { .Instance = &rx_stream };
static DMA_HandleTypeDef tx_dma = { .Instance = &tx_stream };
SPI_HandleTypeDef hspi2 = { .Instance = &spi, .hdmarx = &rx_dma, .hdmatx = &tx_dma };
static uint32_t now;
static unsigned notifications;
uint32_t HAL_GetTick(void) { return now; }
void HAL_GPIO_WritePin(void *port, uint32_t pin, GPIO_PinState value)
{ (void)port; (void)pin; (void)value; }
void test_spi_disable(SPI_HandleTypeDef *handle) { handle->Instance->CR1 = 0U; }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *higher)
{ (void)task; *higher = pdFALSE; notifications++; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(
    SPI_HandleTypeDef *handle, uint8_t *tx, uint8_t *rx, uint16_t size)
{
    (void)handle; (void)tx; (void)rx;
    /* Real HAL installs an RX half callback and enables HT before returning. */
    rx_stream.CR = DMA_SxCR_EN | DMA_IT_TC | DMA_IT_HT | DMA_IT_TE | DMA_IT_DME;
    tx_stream.CR = DMA_SxCR_EN | DMA_IT_TC | DMA_IT_TE | DMA_IT_DME;
    rx_stream.NDTR = tx_stream.NDTR = size;
    rx_dma.Flags = tx_dma.Flags = 0U;
    rx_dma.ErrorCode = tx_dma.ErrorCode = HAL_DMA_ERROR_NONE;
    spi.CR2 = SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN;
    return HAL_OK;
}

static void begin(unsigned length)
{
    uint8_t payload[32] = {0};
    notifications = inject_tc = 0U;
    spi.SR = 0U;
    s_state = NRF_CONFIG_WRITE;
    s_task = (void *)1;
    nrf_start(NRF24L01_W_REGISTER | NRF24L01_RX_ADDR_P0, payload, (uint8_t)(length - 1U));
}

static void test_late_tc(unsigned length)
{
    begin(length);
    /* Force an HT IRQ even after the new launch guard, independently testing W1C. */
    rx_stream.CR |= DMA_IT_HT;
    rx_dma.Flags = __HAL_DMA_GET_HT_FLAG_INDEX(&rx_dma);
    inject_tc = 1U;
    NRF24L01_DmaRxIsr();
    assert(!s_done && notifications == 0U);
    assert(rx_dma.Flags & __HAL_DMA_GET_TC_FLAG_INDEX(&rx_dma));
    NRF24L01_DmaRxIsr();
    assert(s_done && notifications == 1U && rx_dma.Flags == 0U);
    NRF24L01_DmaRxIsr();
    assert(notifications == 1U);
}

int main(int argc, char **argv)
{
    const unsigned lengths[] = {2U, 6U, 33U};
    unsigned i, error;
    (void)argv;
    /* This mode also compiles against the saved original driver. */
    if (argc > 1) { test_late_tc(6U); return 0; }
    for (i = 0U; i < 3U; ++i) {
        begin(lengths[i]);
        assert(!(rx_stream.CR & DMA_IT_HT) && !(tx_stream.CR & DMA_IT_HT));
        rx_stream.CR |= DMA_IT_HT;
        rx_dma.Flags = __HAL_DMA_GET_HT_FLAG_INDEX(&rx_dma);
        NRF24L01_DmaRxIsr();
        assert(!s_done && !s_error && notifications == 0U);
        rx_dma.Flags = __HAL_DMA_GET_TC_FLAG_INDEX(&rx_dma);
        NRF24L01_DmaRxIsr();
        assert(s_done && notifications == 1U);
        begin(lengths[i]);
        rx_stream.CR |= DMA_IT_HT;
        rx_dma.Flags = __HAL_DMA_GET_HT_FLAG_INDEX(&rx_dma) | __HAL_DMA_GET_TC_FLAG_INDEX(&rx_dma);
        NRF24L01_DmaRxIsr();
        assert(s_done && notifications == 1U && rx_dma.Flags == 0U);
        test_late_tc(lengths[i]);
        begin(lengths[i]);
        tx_dma.Flags = __HAL_DMA_GET_TC_FLAG_INDEX(&tx_dma);
        NRF24L01_DmaTxIsr();
        assert(!s_done && notifications == 0U);
        rx_dma.Flags = __HAL_DMA_GET_TC_FLAG_INDEX(&rx_dma);
        NRF24L01_DmaRxIsr();
        assert(s_done && notifications == 1U);
        for (error = 0U; error < 3U; ++error) {
            const uint32_t flags[] = {__HAL_DMA_GET_TE_FLAG_INDEX(&rx_dma),
                __HAL_DMA_GET_DME_FLAG_INDEX(&rx_dma), __HAL_DMA_GET_FE_FLAG_INDEX(&rx_dma)};
            begin(lengths[i]);
            rx_stream.FCR = DMA_IT_FE;
            rx_dma.Flags = flags[error] | __HAL_DMA_GET_TC_FLAG_INDEX(&rx_dma);
            NRF24L01_DmaRxIsr();
            assert(s_error && !s_done && notifications == 1U);
        }
    }
    /* Failure evidence survives task cleanup/reinitialization and remains query-only. */
    {
        NRF24L01_Stats result;
        uint32_t timeouts = s_stats.dma_timeouts;
        begin(6U);
        s_need_dma_reset = 0U;
        spi.CR1 = 0x123U;
        spi.SR = SPI_FLAG_BSY;
        rx_stream.NDTR = 3U; tx_stream.NDTR = 2U;
        now += APP_NRF_SPI_TRANSACTION_TIMEOUT_MS;
        NRF24L01_Service();
        NRF24L01_GetStats(&result);
        assert(result.dma_timeouts == timeouts + 1U && result.last_failure == NRF_FAILURE_DMA_TIMEOUT);
        assert(result.failure_snapshot.valid && result.failure_snapshot.state == NRF_CONFIG_WRITE);
        assert(result.failure_snapshot.command == (NRF24L01_W_REGISTER | NRF24L01_RX_ADDR_P0));
        assert(result.failure_snapshot.length == 6U && result.failure_snapshot.active);
        assert(!result.failure_snapshot.done && !result.failure_snapshot.error);
        assert(result.failure_snapshot.elapsed_ms == APP_NRF_SPI_TRANSACTION_TIMEOUT_MS);
        assert(result.failure_snapshot.spi_cr1 == 0x123U && result.failure_snapshot.spi_sr == SPI_FLAG_BSY);
        assert(result.failure_snapshot.rx_remaining == 3U && result.failure_snapshot.tx_remaining == 2U);
        assert(result.failure_snapshot.rx_cr & DMA_SxCR_EN);
        NRF24L01_Init();
        assert(s_stats.failure_snapshot.tick_ms == result.failure_snapshot.tick_ms);
        begin(33U);
        rx_dma.Flags = __HAL_DMA_GET_TE_FLAG_INDEX(&rx_dma);
        NRF24L01_DmaRxIsr();
        s_need_dma_reset = 0U;
        NRF24L01_Service();
        NRF24L01_GetStats(&result);
        assert(result.last_failure == NRF_FAILURE_SPI && result.failure_snapshot.error);
        assert(result.failure_snapshot.length == 33U && result.failure_snapshot.rx_error == HAL_DMA_ERROR_TE);
    }
    puts("PASS NRF IRQ: HAL HT launch guard; HT-only, HT+TC, late TC, duplicate and TX/RX order, errors; 2/6/33 bytes");
    puts("PASS NRF failure snapshot: pre-cleanup SPI/DMA evidence, timeout, DMA error, retained across init");
    return 0;
}
