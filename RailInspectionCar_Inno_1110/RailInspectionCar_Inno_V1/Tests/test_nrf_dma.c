#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "spi.h"
#include "NRF24L01.h"
#include "app_config.h"

/* 直接编译生产状态机，模拟DMA异步完成和无线FIFO，不提供阻塞HAL接口。 */
static SPI_TypeDef spi;
static DMA_Stream_TypeDef rx_stream, tx_stream;
static DMA_HandleTypeDef rx_dma = { .Instance = &rx_stream };
static DMA_HandleTypeDef tx_dma = { .Instance = &tx_stream };
SPI_HandleTypeDef hspi2 = { .Instance = &spi, .hdmarx = &rx_dma, .hdmatx = &tx_dma };
static uint32_t now;
static unsigned pending, starts, notifications, ce, csn;
static unsigned fail_start, disconnected, feature_lock, feature_unsupported;
static unsigned ack_count, rx_count, rx_width = 32U;
static unsigned activations, continuous_rx, auto_ack, suppress_ack;
static unsigned force_tx_full, dynpd_unsupported;
static unsigned check_reads, have_check_tick, check_gap;
static uint32_t last_check_tick;
static uint8_t registers[32], address[5], packet[32], ack[32], status;
static uint8_t *tx_data, *rx_data;
static uint8_t tx_copy[33];
static uint16_t dma_size;

uint32_t HAL_GetTick(void) { return now; }
void HAL_GPIO_WritePin(void *port, uint32_t pin, GPIO_PinState value)
{
    (void)port;
    if (pin == CE_Pin) ce = value;
    else { assert(pin == CSN_Pin); csn = value; }
}
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *higher)
{ assert(task == (void *)1); notifications++; *higher = pdTRUE; }
void test_spi_disable(SPI_HandleTypeDef *handle)
{ assert(handle == &hspi2); pending = 0U; handle->Instance->CR1 = 0U; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(
    SPI_HandleTypeDef *handle, uint8_t *tx, uint8_t *rx, uint16_t size)
{
    assert(handle == &hspi2 && !pending && !csn);
    assert(size >= 1U && size <= 33U);
    if (fail_start) { fail_start--; return HAL_BUSY; }
    pending = 1U; starts++; tx_data = tx; rx_data = rx; dma_size = size;
    if (NRF24L01_IsRadioReady() && tx[0] <= 31U &&
        tx[0] != NRF24L01_FIFO_STATUS)
    {
        /* 两轮巡检之间的正常空闲期不计入轮内事务间隔。 */
        if (tx[0] == NRF24L01_CONFIG) have_check_tick = 0U;
        unsigned gap = (unsigned)(now - last_check_tick);
        if (have_check_tick && gap > check_gap) check_gap = gap;
        have_check_tick = 1U; last_check_tick = now; check_reads++;
    }
    memcpy(tx_copy, tx, size);
    rx_stream.CR = tx_stream.CR = DMA_SxCR_EN | DMA_IT_TC | DMA_IT_TE | DMA_IT_DME;
    /* HAL_SPI_TransmitReceive_DMA installs the RX HT callback before starting DMA. */
    rx_stream.CR |= DMA_IT_HT;
    rx_stream.NDTR = tx_stream.NDTR = size;
    rx_dma.Flags = tx_dma.Flags = 0U;
    spi.CR2 = SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN | SPI_CR2_ERRIE;
    return HAL_OK;
}

static void complete(void)
{
    uint8_t command = tx_copy[0];
    uint8_t reg = (uint8_t)(command & 31U);
    assert(pending && memcmp(tx_data, tx_copy, dma_size) == 0);
    assert(!(rx_stream.CR & DMA_IT_HT) && !(tx_stream.CR & DMA_IT_HT));
    pending = 0U;
    memset(rx_data, 0, dma_size);
    rx_data[0] = (uint8_t)(status |
        ((ack_count >= 3U || (force_tx_full &&
          command == NRF24L01_W_ACK_PAYLOAD)) ? 1U : 0U));
    if (disconnected) memset(rx_data, 255, dma_size);
    else if (command <= 31U)
    {
        if (reg == NRF24L01_FIFO_STATUS)
            rx_data[1] = (uint8_t)((!rx_count ? 1U : 0U) |
                (rx_count >= 3U ? 2U : 0U) | (!ack_count ? 16U : 0U) |
                (ack_count >= 3U ? 32U : 0U));
        else if (reg == NRF24L01_RX_ADDR_P0)
            memcpy(rx_data + 1, address, 5U);
        else rx_data[1] = registers[reg];
    }
    else if (command >= 32U && command <= 63U)
    {
        if (reg == NRF24L01_STATUS) status &= (uint8_t)~tx_copy[1];
        else if (reg == NRF24L01_RX_ADDR_P0) memcpy(address, tx_copy + 1, 5U);
        else if (reg == NRF24L01_DYNPD && dynpd_unsupported) {}
        else if (reg != NRF24L01_FEATURE || (!feature_lock && !feature_unsupported))
            registers[reg] = tx_copy[1];
    }
    else if (command == 0x50U) { feature_lock ^= 1U; activations++; }
    else if (command == NRF24L01_FLUSH_TX) { assert(!ce); ack_count = 0U; }
    else if (command == NRF24L01_FLUSH_RX) rx_count = 0U;
    else if (command == NRF24L01_R_RX_PL_WID) rx_data[1] = (uint8_t)rx_width;
    else if (command == NRF24L01_R_RX_PAYLOAD)
    {
        assert(rx_count && dma_size == 33U);
        memcpy(rx_data + 1, packet, 32U); rx_count--;
    }
    else if (command == NRF24L01_W_ACK_PAYLOAD)
    {
        if (!suppress_ack && ack_count < 3U)
        { memcpy(ack, tx_copy + 1, 32U); ack_count++; }
    }
    else assert(0);
    rx_stream.CR &= ~DMA_SxCR_EN;
    tx_stream.CR &= ~DMA_SxCR_EN;
    rx_stream.NDTR = tx_stream.NDTR = 0U;
    rx_dma.Flags = __HAL_DMA_GET_TC_FLAG_INDEX(&rx_dma);
    tx_dma.Flags = __HAL_DMA_GET_TC_FLAG_INDEX(&tx_dma);
    NRF24L01_DmaRxIsr();
    NRF24L01_DmaTxIsr();
    assert(rx_dma.Flags == 0U && tx_dma.Flags == 0U);
    assert(rx_dma.State == HAL_DMA_STATE_READY && tx_dma.State == HAL_DMA_STATE_READY);
    if (continuous_rx && ce) rx_count = 1U;
    if (auto_ack && ce) ack_count = 0U;
}

static void first_ack(void)
{
#if APP_NRF_TELEMETRY_ENABLED
    uint8_t first[32] = { 0x5A, 0xA5, 1U, 0x81U };
    if (NRF24L01_IsInitialized() && !NRF24L01_IsRadioReady() &&
        NRF24L01_CanQueueAck())
        assert(NRF24L01_QueueAckPayload(first));
#endif
}

static void steps(unsigned count)
{
    while (count--)
    {
        NRF24L01_Service();
        first_ack();
        if (pending) complete();
        now++;
    }
}
static void init_chip(unsigned lock, unsigned unsupported)
{
    memset(registers, 0, sizeof(registers)); status = 0U;
    pending = ack_count = rx_count = disconnected = fail_start = 0U;
    continuous_rx = auto_ack = suppress_ack = force_tx_full = dynpd_unsupported = 0U;
    feature_lock = lock; feature_unsupported = unsupported;
    spi.SR = 0U; rx_width = 32U;
    NRF24L01_Init();
    steps(99U);
    assert(!NRF24L01_IsInitialized() && !pending && !ce);
    steps(100U);
    assert(NRF24L01_IsInitialized() && ce && registers[NRF24L01_CONFIG] == 11U);
    assert(NRF24L01_IsRadioReady());
    ack_count = 0U; steps(10U);
}
static NRF24L01_Stats stats(void)
{ NRF24L01_Stats result; NRF24L01_GetStats(&result); return result; }

int main(void)
{
    uint8_t payload[32];
    uint32_t loaded, recoveries, timeouts, errors, flushed;
    NRF24L01_BindTask((void *)1);
    init_chip(1U, 0U);
    assert(stats().ack_supported == APP_NRF_ACK_PAYLOAD_ENABLED);
    /* 多包到达时先完整搬运接收包，Receive自身不启动SPI事务。 */
    memset(packet, 0x39, 32U); rx_count = 3U; status = 0x40U;
    steps(30U);
    for (unsigned i = 0U; i < 3U; i++)
    {
        unsigned before = starts;
        assert(NRF24L01_Receive() == 1U && starts == before);
        assert(memcmp(NRF24L01_RxPacket, packet, 32U) == 0);
    }
    assert(NRF24L01_Receive() == 0U);
    memset(payload, 0x5A, sizeof(payload));
#if APP_NRF_ACK_PAYLOAD_ENABLED
    loaded = stats().ack_loaded;
    assert(NRF24L01_QueueAckPayload(payload));
    assert(stats().ack_pending && stats().ack_pending_age_ms == 0U);
    steps(30U);
    assert(stats().ack_loaded == loaded + 1U && ack_count == 1U);
    assert(stats().ack_in_fifo && !stats().ack_pending && stats().ack_fifo_age_ms > 0U);
    assert(memcmp(ack, payload, 32U) == 0);
    /* ACK未被远端取走时不填满FIFO，超龄后异步待机清旧帧。 */
    assert(!NRF24L01_CanQueueAck());
    payload[0] = 0x83U;
    assert(NRF24L01_QueueAckPayload(payload));
    flushed = stats().ack_flushed;
    steps(150U);
    assert(stats().ack_flushed > flushed && ack_count == 1U && ack[0] == 0x83U);
    /* 装载中的缓冲不能被新帧覆盖，下一段必须取最新软件帧。 */
    ack_count = 0U; steps(10U);
    payload[0] = 0x84U; assert(NRF24L01_QueueAckPayload(payload));
    for (unsigned i = 0U; i < 30U; i++)
    {
        NRF24L01_Service();
        if (pending && tx_copy[0] == NRF24L01_W_ACK_PAYLOAD)
        {
            payload[0] = 0x85U; assert(NRF24L01_QueueAckPayload(payload));
            complete(); now++; break;
        }
        if (pending) complete();
        now++;
    }
    steps(150U);
    assert(ack[0] == 0x85U && stats().ack_replaced > 0U);
    /* MAX_RT恢复不能永久循环，且不清掉未处理的控制FIFO。 */
    status = 0x10U; rx_count = 1U;
    flushed = stats().ack_flushed; steps(30U);
    assert(stats().ack_flushed > flushed && !(status & 0x10U));
    assert(stats().ack_clear_by_reason[NRF24L01_ACK_CLEAR_MAX_RT] > 0U);
    assert(NRF24L01_Receive() == 1U);
    /* 非法动态长度使模块退避重建，不把残帧发布为遥控命令。 */
    rx_count = 1U; rx_width = 33U; steps(15U);
    assert(!NRF24L01_IsInitialized() && stats().invalid_widths > 0U);
    rx_width = 32U;
    steps(750U);
    assert(NRF24L01_IsInitialized() && NRF24L01_Receive() == 0U);
#else
    (void)loaded; (void)flushed;
    assert(!NRF24L01_QueueAckPayload(payload) && stats().ack_loaded == 0U);
#endif
    /* DMA丢完成、SPI忙和断线均及时返回；退避期间没有重试风暴。 */
    while (!pending) { NRF24L01_Service(); now++; }
    timeouts = stats().dma_timeouts; recoveries = stats().recoveries;
    now += 3U; NRF24L01_Service();
    assert(!NRF24L01_IsInitialized() && stats().dma_timeouts == timeouts + 1U);
    steps(450U); assert(stats().recoveries == recoveries);
    steps(250U); assert(NRF24L01_IsInitialized());
    while (!pending) { NRF24L01_Service(); now++; }
    errors = stats().spi_errors;
    /* TE与TC同时到来时只发布错误，不能误报事务成功。 */
    rx_dma.Flags = __HAL_DMA_GET_TE_FLAG_INDEX(&rx_dma) |
        __HAL_DMA_GET_TC_FLAG_INDEX(&rx_dma);
    NRF24L01_DmaRxIsr();
    assert(rx_dma.ErrorCode & HAL_DMA_ERROR_TE);
    NRF24L01_Service();
    assert(stats().spi_errors == errors + 1U && !ce);
    steps(700U); assert(NRF24L01_IsInitialized());
    while (!pending) { NRF24L01_Service(); now++; }
    errors = stats().spi_errors;
    tx_dma.Flags = __HAL_DMA_GET_DME_FLAG_INDEX(&tx_dma);
    NRF24L01_DmaTxIsr(); NRF24L01_Service();
    assert(stats().spi_errors == errors + 1U);
    steps(700U); assert(NRF24L01_IsInitialized());
    while (!pending) { NRF24L01_Service(); now++; }
    errors = stats().spi_errors;
    rx_stream.FCR |= DMA_IT_FE;
    rx_dma.Flags = __HAL_DMA_GET_FE_FLAG_INDEX(&rx_dma);
    NRF24L01_DmaRxIsr(); NRF24L01_Service();
    assert(stats().spi_errors == errors + 1U);
    steps(700U); assert(NRF24L01_IsInitialized());
    fail_start = 1U; steps(10U); assert(!NRF24L01_IsInitialized());
    steps(700U); assert(NRF24L01_IsInitialized());
    disconnected = 1U; steps(10U); assert(!NRF24L01_IsInitialized());
    disconnected = 0U; steps(700U); assert(NRF24L01_IsInitialized());
#if APP_NRF_ACK_PAYLOAD_ENABLED
    /* 永久不支持不能静默回退；恢复能力后自动完成，不用模块断电。 */
    feature_unsupported = 1U; registers[NRF24L01_FEATURE] = 0U;
    NRF24L01_Init(); steps(200U);
    assert(!NRF24L01_IsInitialized() && !ce &&
        stats().last_failure == NRF_FAILURE_FEATURE);
    recoveries = stats().recoveries;
    steps(350U); assert(stats().recoveries == recoveries);
    feature_unsupported = 0U; steps(500U);
    assert(NRF24L01_IsRadioReady() && stats().ack_supported);
    /* DYNPD核验失败同样不能冒充可用；恢复后无需人工重启。 */
    dynpd_unsupported = 1U; registers[NRF24L01_DYNPD] = 0U;
    NRF24L01_Init(); steps(200U);
    assert(!ce && !NRF24L01_IsInitialized() &&
        stats().last_failure == NRF_FAILURE_DYNPD);
    dynpd_unsupported = 0U; steps(800U);
    assert(NRF24L01_IsRadioReady());
#endif
    /* 主控热重启保留模块寄存器、满FIFO和IRQ，不能盲发ACTIVATE。 */
    rx_count = ack_count = 3U; status = 0x70U;
    unsigned activate_before = activations;
    uint32_t generation = stats().recovery_generation;
    NRF24L01_Init(); assert(!ce);
    steps(200U);
    assert(NRF24L01_IsRadioReady() && !rx_count && !status);
    assert(activations == activate_before &&
        stats().recovery_generation != generation);
    assert(ack_count == (APP_NRF_TELEMETRY_ENABLED ? 1U : 0U));
#if APP_NRF_TELEMETRY_ENABLED
    /* 首ACK未提供时必须超时重建，不能先开放接收。 */
    NRF24L01_Init();
    for (unsigned i = 0U; i < 300U; i++)
    {
        NRF24L01_Service(); if (pending) complete(); now++;
    }
    assert(!ce && !NRF24L01_IsRadioReady() &&
        stats().last_failure == NRF_FAILURE_FIRST_ACK);
    steps(800U); assert(NRF24L01_IsRadioReady());
    /* 反复TX_FULL清理不能把首ACK期限无限延长。 */
    force_tx_full = 1U;
    NRF24L01_Init(); steps(300U);
    assert(!ce && !NRF24L01_IsRadioReady() &&
        stats().last_failure == NRF_FAILURE_FIRST_ACK);
    force_tx_full = 0U; steps(800U);
    assert(NRF24L01_IsRadioReady());
#endif
    /* 持续RX仍允许ACK和巡检前进；巡检仅回读，不关闭正常接收。 */
    continuous_rx = auto_ack = 1U;
    ack_count = 0U;
    steps(10U);
    loaded = stats().ack_loaded;
    uint32_t checks = stats().config_check_count;
    check_gap = have_check_tick = check_reads = 0U;
    for (unsigned i = 0U; i < 1600U; i++)
    {
#if APP_NRF_ACK_PAYLOAD_ENABLED
        if (NRF24L01_CanQueueAck()) assert(NRF24L01_QueueAckPayload(payload));
#endif
        steps(1U);
        (void)NRF24L01_Receive();
        NRF24L01_ReportControlFrame();
        assert(ce && NRF24L01_IsRadioReady());
    }
    assert(stats().config_check_count > checks);
    assert(check_reads >= 11U && check_gap <= APP_NRF_CONFIG_CHECK_SLOT_MS);
#if APP_NRF_ACK_PAYLOAD_ENABLED
    assert(stats().ack_loaded > loaded + 10U);
#endif
    /* 按无线任务的5ms轮询节拍验证，而不只依赖1ms快速模拟。 */
    loaded = stats().ack_loaded;
    checks = stats().config_check_count;
    check_gap = have_check_tick = check_reads = 0U;
    uint32_t polled_rx = stats().rx_packets;
    for (unsigned i = 0U; i < 480U; i++)
    {
#if APP_NRF_ACK_PAYLOAD_ENABLED
        if (NRF24L01_CanQueueAck()) assert(NRF24L01_QueueAckPayload(payload));
#endif
        steps(1U);
        now += APP_NRF_TASK_PERIOD_MS - 1U;
        (void)NRF24L01_Receive();
        NRF24L01_ReportControlFrame();
        assert(ce && NRF24L01_IsRadioReady());
    }
    assert(stats().config_check_count >= checks + 2U);
    assert(check_reads >= 22U && check_gap <= APP_NRF_CONFIG_CHECK_SLOT_MS);
    assert(stats().rx_packets > polled_rx + 10U);
#if APP_NRF_ACK_PAYLOAD_ENABLED
    assert(stats().ack_loaded > loaded + 10U);
#endif
    continuous_rx = auto_ack = 0U;
#if APP_NRF_ACK_PAYLOAD_ENABLED
    /* 合法上行持续到达却无法装载时：100ms清理，第二个100ms完整重建。 */
    force_tx_full = 1U;
    continuous_rx = 1U;
    assert(NRF24L01_QueueAckPayload(payload));
    timeouts = stats().ack_load_timeouts;
    uint32_t received_before = stats().rx_packets;
    for (unsigned i = 0U; i < 250U && NRF24L01_IsRadioReady(); i++)
    {
        NRF24L01_ReportControlFrame(); steps(1U);
        (void)NRF24L01_Receive();
    }
    assert(!NRF24L01_IsRadioReady() &&
        stats().last_failure == NRF_FAILURE_ACK_STALLED &&
        stats().ack_load_timeouts == timeouts + 2U);
    assert(stats().rx_packets > received_before + 5U);
    force_tx_full = continuous_rx = 0U; steps(800U);
    assert(NRF24L01_IsRadioReady());
    /* 遥控关机没有有效上行，不因状态装载不畅反复重建模块。 */
    force_tx_full = 1U; steps(APP_REMOTE_SERVO_LINK_TIMEOUT_MS + 10U);
    recoveries = stats().recoveries;
    assert(NRF24L01_QueueAckPayload(payload)); steps(400U);
    assert(NRF24L01_IsRadioReady() && stats().recoveries == recoveries);
    force_tx_full = 0U; steps(30U);
#endif
    /* 配置被模块复位或干扰改写时，即使SPI还正常也能发现并恢复。 */
    registers[NRF24L01_RF_CH] = 41U;
    for (unsigned i = 0U; i < 1350U && NRF24L01_IsRadioReady(); i++)
        steps(1U);
    assert(!NRF24L01_IsRadioReady() &&
        stats().config_check_mismatches &&
        stats().last_failure == NRF_FAILURE_CONFIG_CHECK);
    steps(800U);
    assert(NRF24L01_IsRadioReady() && registers[NRF24L01_RF_CH] == 2U);
    /* 模块独立掉电：总线读FF后退避，寄存器冷复位后重新配置全部参数。 */
    disconnected = 1U; steps(10U);
    assert(!NRF24L01_IsRadioReady());
    memset(registers, 0, sizeof(registers));
    memset(address, 0, sizeof(address));
    ack_count = rx_count = status = 0U;
    feature_lock = 1U; disconnected = 0U;
    steps(800U);
    assert(NRF24L01_IsRadioReady() && registers[NRF24L01_RF_CH] == 2U &&
        memcmp(address, NRF24L01_RxAddress, 5U) == 0);
    assert(notifications > 0U && stats().dma_completed > 50U);
    puts("PASS NRF SPI DMA: hot boot/first ACK/fair RX/check/errors/backoff");
    return 0;
}
