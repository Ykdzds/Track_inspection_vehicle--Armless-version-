#include "main.h"
#include "spi.h"
#include "NRF24L01.h"
#include "app_config.h"
#include <string.h>

uint8_t NRF24L01_RxAddress[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
uint8_t NRF24L01_RxPacket[32];

typedef enum
{
    NRF_BOOT_WAIT = 0,
    NRF_CONFIG_WRITE,
    NRF_CONFIG_VERIFY,
    NRF_FEATURE_WRITE,
    NRF_FEATURE_VERIFY,
    NRF_FEATURE_ACTIVATE,
    NRF_DYNPD_WRITE,
    NRF_DYNPD_VERIFY,
    NRF_FLUSH_TX_INIT,
    NRF_FLUSH_RX_INIT,
    NRF_CLEAR_INIT,
    NRF_POWER_UP,
    NRF_POWER_VERIFY,
    NRF_SETTLE,
    NRF_FIRST_ACK,
    NRF_IDLE,
    NRF_FIFO,
    NRF_WIDTH,
    NRF_PAYLOAD,
    NRF_CLEAR_IRQ,
    NRF_ACK,
    NRF_ACK_STANDBY,
    NRF_ACK_FLUSH,
    NRF_CONFIG_CHECK,
    NRF_RECOVER
} nrf_state_t;

typedef struct { uint8_t reg; uint8_t value; } nrf_register_t;
static const nrf_register_t s_config[] = {
    {NRF24L01_CONFIG, 0x08U}, {NRF24L01_EN_AA, 0x3FU},
    {NRF24L01_EN_RXADDR, 1U}, {NRF24L01_SETUP_AW, 3U},
    {NRF24L01_SETUP_RETR, 3U}, {NRF24L01_RF_CH, 2U},
    {NRF24L01_RF_SETUP, 0x0EU}, {NRF24L01_RX_PW_P0, 32U},
    {NRF24L01_RX_ADDR_P0, 0U}
};

/* DMA缓冲区独立于调用栈；事务结束前不复用。 */
static uint8_t s_tx[33];
static uint8_t s_rx[33];
static uint8_t s_received[3][32];
static uint8_t s_rx_head;
static uint8_t s_rx_count;
static uint8_t s_ack[32];
static uint8_t s_ack_pending;
static uint8_t s_ack_in_fifo;
static uint8_t s_active;
static volatile uint8_t s_done;
static volatile uint8_t s_error;
static nrf_state_t s_state;
static uint8_t s_config_index;
static uint8_t s_activate_tried;
static uint8_t s_feature_value;
static uint32_t s_started;
static uint8_t s_command, s_length;
static uint32_t s_wait_tick;
static uint32_t s_ack_tick;
static uint32_t s_poll_tick;
static uint32_t s_ack_generation;
static uint32_t s_loading_generation;
static uint32_t s_ack_pending_tick;
static uint32_t s_ack_progress_tick;
static uint32_t s_first_ack_tick;
static uint32_t s_check_tick;
static uint32_t s_check_slot_tick;
static uint8_t s_check_active;
static uint8_t s_check_index;
static nrf_state_t s_check_resume_state;
static uint8_t s_rx_since_ack;
static uint8_t s_ack_timeout_cleaned;
static uint8_t s_ack_clear_reason;
static uint8_t s_need_dma_reset;
static uint8_t s_have_control_frame;
static uint32_t s_control_frame_tick;
static TaskHandle_t s_task;
static NRF24L01_Stats s_stats;
static NRF24L01_Event s_events[16];
static uint8_t s_event_head, s_event_count;

static void nrf_record_event(uint8_t kind)
{
    NRF24L01_Event *event;
    taskENTER_CRITICAL();
    event = &s_events[(s_event_head + s_event_count) % 16U];
    event->timestamp_ms = HAL_GetTick();
    event->kind = kind;
    event->type = s_ack[3];
    event->sequence = s_ack[4];
    event->session = s_ack[6];
    if (s_event_count < 16U) s_event_count++;
    else {
        s_event_head = (uint8_t)((s_event_head + 1U) % 16U);
        s_stats.event_overwrites++;
    }
    taskEXIT_CRITICAL();
}

bool NRF24L01_PopEvent(NRF24L01_Event *event)
{
    bool found = false;
    if (event == NULL) return false;
    taskENTER_CRITICAL();
    if (s_event_count != 0U) {
        *event = s_events[s_event_head];
        s_event_head = (uint8_t)((s_event_head + 1U) % 16U);
        s_event_count--;
        found = true;
    }
    taskEXIT_CRITICAL();
    return found;
}

static void nrf_ce(uint8_t high)
{
    HAL_GPIO_WritePin(CE_GPIO_Port, CE_Pin,
                     high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void nrf_csn(uint8_t high)
{
    HAL_GPIO_WritePin(CSN_GPIO_Port, CSN_Pin,
                     high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void nrf_notify(void)
{
    BaseType_t higher = pdFALSE;
    if (s_task != NULL)
    {
        vTaskNotifyGiveFromISR(s_task, &higher);
        portYIELD_FROM_ISR(higher);
    }
}

/* 替换HAL的SPI收尾回调，避免在DMA中断里轮询TXE或BSY。 */
static void nrf_dma_complete(DMA_HandleTypeDef *dma)
{
    (void)dma;
    CLEAR_BIT(hspi2.Instance->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    s_done = 1U;
    nrf_notify();
}

static void nrf_dma_error(DMA_HandleTypeDef *dma)
{
    (void)dma;
    CLEAR_BIT(hspi2.Instance->CR2, SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    s_error = 1U;
    nrf_notify();
}

/* 仅处理SPI2普通模式DMA；错误中断不调用HAL内部的关流等待循环。 */
static void nrf_dma_irq(DMA_HandleTypeDef *dma)
{
    uint32_t errors = HAL_DMA_ERROR_NONE;
    uint32_t tc = __HAL_DMA_GET_TC_FLAG_INDEX(dma);
    uint32_t ht = __HAL_DMA_GET_HT_FLAG_INDEX(dma);
    uint32_t te = __HAL_DMA_GET_TE_FLAG_INDEX(dma);
    uint32_t dme = __HAL_DMA_GET_DME_FLAG_INDEX(dma);
    uint32_t fe = __HAL_DMA_GET_FE_FLAG_INDEX(dma);
    uint32_t flags = __HAL_DMA_GET_FLAG(dma, tc | ht | te | dme | fe);
    uint32_t control = dma->Instance->CR;
    uint32_t fifo_control = dma->Instance->FCR;
    uint8_t complete = (uint8_t)(
        (flags & tc) != RESET && (control & DMA_IT_TC) != RESET);
    if ((flags & te) != RESET && (control & DMA_IT_TE) != RESET)
        errors |= HAL_DMA_ERROR_TE;
    if ((flags & dme) != RESET && (control & DMA_IT_DME) != RESET)
        errors |= HAL_DMA_ERROR_DME;
    if ((flags & fe) != RESET && (fifo_control & DMA_IT_FE) != RESET)
        errors |= HAL_DMA_ERROR_FE;
    /* W1C only sampled flags: TC may arrive after an HT-only snapshot. */
    if (flags != 0U) __HAL_DMA_CLEAR_FLAG(dma, flags);
    if ((flags & ht) != 0U) __HAL_DMA_DISABLE_IT(dma, DMA_IT_HT);
    if (errors != HAL_DMA_ERROR_NONE)
    {
        __HAL_DMA_DISABLE_IT(dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE | DMA_IT_DME);
        __HAL_DMA_DISABLE_IT(dma, DMA_IT_FE);
        __HAL_DMA_DISABLE(dma);
        dma->ErrorCode |= errors;
        /* 不在硬件EN尚未清零时标为READY，由任务收尾后再复位句柄。 */
        nrf_dma_error(dma);
    }
    else if (complete)
    {
        __HAL_DMA_DISABLE_IT(dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE | DMA_IT_DME);
        __HAL_DMA_DISABLE_IT(dma, DMA_IT_FE);
        dma->State = HAL_DMA_STATE_READY;
        dma->Lock = HAL_UNLOCKED;
        if (dma == hspi2.hdmarx) nrf_dma_complete(dma);
    }
}

void NRF24L01_DmaRxIsr(void) { nrf_dma_irq(hspi2.hdmarx); }
void NRF24L01_DmaTxIsr(void) { nrf_dma_irq(hspi2.hdmatx); }

static void nrf_fail(nrf_failure_t reason)
{
    NRF24L01_FailureSnapshot *snapshot = &s_stats.failure_snapshot;
    taskENTER_CRITICAL();
    snapshot->valid = 1U;
    snapshot->tick_ms = HAL_GetTick();
    snapshot->elapsed_ms = s_active ? snapshot->tick_ms - s_started : 0U;
    snapshot->state = (uint8_t)s_state;
    snapshot->command = s_command;
    snapshot->length = s_length;
    snapshot->active = s_active;
    snapshot->done = s_done;
    snapshot->error = s_error;
    snapshot->spi_cr1 = hspi2.Instance->CR1;
    snapshot->spi_cr2 = hspi2.Instance->CR2;
    snapshot->spi_sr = hspi2.Instance->SR;
    snapshot->rx_remaining = hspi2.hdmarx->Instance->NDTR;
    snapshot->tx_remaining = hspi2.hdmatx->Instance->NDTR;
    snapshot->rx_cr = hspi2.hdmarx->Instance->CR;
    snapshot->tx_cr = hspi2.hdmatx->Instance->CR;
    snapshot->rx_flags = __HAL_DMA_GET_FLAG(hspi2.hdmarx,
        __HAL_DMA_GET_TC_FLAG_INDEX(hspi2.hdmarx) | __HAL_DMA_GET_HT_FLAG_INDEX(hspi2.hdmarx) |
        __HAL_DMA_GET_TE_FLAG_INDEX(hspi2.hdmarx) | __HAL_DMA_GET_DME_FLAG_INDEX(hspi2.hdmarx) |
        __HAL_DMA_GET_FE_FLAG_INDEX(hspi2.hdmarx));
    snapshot->tx_flags = __HAL_DMA_GET_FLAG(hspi2.hdmatx,
        __HAL_DMA_GET_TC_FLAG_INDEX(hspi2.hdmatx) | __HAL_DMA_GET_HT_FLAG_INDEX(hspi2.hdmatx) |
        __HAL_DMA_GET_TE_FLAG_INDEX(hspi2.hdmatx) | __HAL_DMA_GET_DME_FLAG_INDEX(hspi2.hdmatx) |
        __HAL_DMA_GET_FE_FLAG_INDEX(hspi2.hdmatx));
    snapshot->rx_error = hspi2.hdmarx->ErrorCode;
    snapshot->tx_error = hspi2.hdmatx->ErrorCode;
    s_stats.last_failure = reason;
    taskEXIT_CRITICAL();
    if (reason == NRF_FAILURE_SPI || reason == NRF_FAILURE_DMA_TIMEOUT)
        s_stats.spi_errors++;
    if ((s_state == NRF_ACK) || s_ack_pending)
        s_stats.ack_failures++;
    s_stats.initialized = 0U;
    s_stats.radio_ready = 0U;
    s_stats.recovery_generation++;
    s_stats.ack_supported = 0U;
    s_stats.ack_clear_reason = NRF24L01_ACK_CLEAR_RECOVERY;
    s_stats.ack_clear_by_reason[NRF24L01_ACK_CLEAR_RECOVERY]++;
    nrf_record_event((uint8_t)(16U + reason));
    s_ack_pending = 0U;
    nrf_ce(0U);
    CLEAR_BIT(hspi2.Instance->CR2,
              SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN | SPI_CR2_ERRIE);
    __HAL_SPI_DISABLE(&hspi2);
    __HAL_DMA_DISABLE(hspi2.hdmarx);
    __HAL_DMA_DISABLE(hspi2.hdmatx);
    s_need_dma_reset = 1U;
    s_have_control_frame = 0U;
    s_state = NRF_RECOVER;
    s_wait_tick = HAL_GetTick();
}

static void nrf_reset_dma(DMA_HandleTypeDef *dma)
{
    __HAL_DMA_CLEAR_FLAG(dma, __HAL_DMA_GET_TC_FLAG_INDEX(dma) |
        __HAL_DMA_GET_HT_FLAG_INDEX(dma) | __HAL_DMA_GET_TE_FLAG_INDEX(dma) |
        __HAL_DMA_GET_DME_FLAG_INDEX(dma) | __HAL_DMA_GET_FE_FLAG_INDEX(dma));
    dma->State = HAL_DMA_STATE_READY;
    dma->Lock = HAL_UNLOCKED;
    dma->ErrorCode = HAL_DMA_ERROR_NONE;
}

static void nrf_start(uint8_t command, const uint8_t *data, uint8_t count)
{
    HAL_StatusTypeDef result;
    s_tx[0] = command;
    memset(&s_tx[1], NRF24L01_NOP, count);
    if (data != NULL) memcpy(&s_tx[1], data, count);
    memset(s_rx, 0, sizeof(s_rx));
    s_done = 0U;
    s_error = 0U;
    s_started = HAL_GetTick();
    s_command = command;
    s_length = (uint8_t)(count + 1U);
    s_active = 1U;
    nrf_csn(0U);
    /* DMA可能立刻完成，配置回调与启动必须作为同一短临界操作。 */
    taskENTER_CRITICAL();
    result = HAL_SPI_TransmitReceive_DMA(&hspi2, s_tx, s_rx,
                                        (uint16_t)(count + 1U));
    if (result == HAL_OK)
    {
        /* HAL enables RX HT before returning; a NULL callback alone leaves HTIE set. */
        __HAL_DMA_DISABLE_IT(hspi2.hdmarx, DMA_IT_HT);
        __HAL_DMA_DISABLE_IT(hspi2.hdmatx, DMA_IT_HT);
        hspi2.hdmarx->XferCpltCallback = nrf_dma_complete;
        hspi2.hdmarx->XferHalfCpltCallback = NULL;
        hspi2.hdmarx->XferErrorCallback = nrf_dma_error;
        hspi2.hdmatx->XferErrorCallback = nrf_dma_error;
        /* SPI错误由任务采样，不启用未配置的SPI2中断。 */
        CLEAR_BIT(hspi2.Instance->CR2, SPI_CR2_ERRIE);
    }
    taskEXIT_CRITICAL();
    if (result == HAL_OK) s_stats.dma_started++;
    else nrf_fail(NRF_FAILURE_SPI);
}

static void nrf_write(uint8_t reg, uint8_t value)
{
    nrf_start((uint8_t)(NRF24L01_W_REGISTER | reg), &value, 1U);
}

static void nrf_read(uint8_t reg)
{
    nrf_start((uint8_t)(NRF24L01_R_REGISTER | reg), NULL, 1U);
}

static void nrf_ack_standby(void)
{
    nrf_ce(0U);
    s_wait_tick = HAL_GetTick();
    s_state = NRF_ACK_STANDBY;
}

static void nrf_set_ack_clear_reason(uint8_t reason)
{
    s_ack_clear_reason = reason;
}

static void nrf_enter_receive(void)
{
    nrf_ce(1U);
    s_stats.radio_ready = 1U;
    s_check_tick = s_check_slot_tick = HAL_GetTick();
    s_state = NRF_FIFO;
}

/* 初始化和巡检共用同一配置表，仅运行态CONFIG使用PRX值。 */
static uint8_t nrf_check_reg(void)
{
    if (s_check_index < sizeof(s_config) / sizeof(s_config[0]))
        return s_config[s_check_index].reg;
    return s_check_index == sizeof(s_config) / sizeof(s_config[0]) ?
        NRF24L01_FEATURE : NRF24L01_DYNPD;
}

static uint8_t nrf_check_matches(uint8_t reg, uint8_t value)
{
    if (s_rx[0] == 0xFFU) return 0U;
    if (reg == NRF24L01_RX_ADDR_P0)
        return (uint8_t)(memcmp(s_rx + 1, NRF24L01_RxAddress, 5U) == 0);
    if (reg == NRF24L01_CONFIG) return (uint8_t)(value == 0x0BU);
    if (reg == NRF24L01_FEATURE) return (uint8_t)(value == s_feature_value);
    if (reg == NRF24L01_DYNPD)
        return (uint8_t)(value == (s_feature_value ? 1U : 0U));
    return (uint8_t)(value == s_config[s_check_index].value);
}

static void nrf_complete(void)
{
    uint8_t value = s_rx[1];
    s_stats.status = s_rx[0];
    s_stats.dma_completed++;
    switch (s_state)
    {
        case NRF_CONFIG_WRITE: s_state = NRF_CONFIG_VERIFY; break;
        case NRF_CONFIG_VERIFY:
            if (s_rx[0] == 0xFFU ||
                (s_config[s_config_index].reg == NRF24L01_RX_ADDR_P0 ?
                    memcmp(&s_rx[1], NRF24L01_RxAddress, 5U) != 0 :
                    value != s_config[s_config_index].value))
            {
                s_stats.verify_failures++;
                nrf_fail(NRF_FAILURE_CONFIG);
                break;
            }
            if (++s_config_index == sizeof(s_config) / sizeof(s_config[0]))
                s_state = NRF_FEATURE_WRITE;
            else s_state = NRF_CONFIG_WRITE;
            break;
        case NRF_FEATURE_WRITE: s_state = NRF_FEATURE_VERIFY; break;
        case NRF_FEATURE_VERIFY:
            s_stats.feature = value;
            if (value == s_feature_value && s_rx[0] != 0xFFU)
                s_state = NRF_DYNPD_WRITE;
            else
            {
                s_stats.verify_failures++;
                if (s_feature_value && !s_activate_tried && s_rx[0] != 0xFFU)
                    s_state = NRF_FEATURE_ACTIVATE;
                else nrf_fail(NRF_FAILURE_FEATURE);
            }
            break;
        case NRF_FEATURE_ACTIVATE:
            s_activate_tried = 1U;
            s_state = NRF_FEATURE_WRITE;
            break;
        case NRF_DYNPD_WRITE: s_state = NRF_DYNPD_VERIFY; break;
        case NRF_DYNPD_VERIFY:
            s_stats.dynpd = value;
            if (value != (s_feature_value ? 1U : 0U) || s_rx[0] == 0xFFU)
            {
                s_stats.verify_failures++;
                nrf_fail(NRF_FAILURE_DYNPD);
            }
            else
            {
                s_stats.ack_supported = (uint8_t)(s_feature_value != 0U);
                s_state = NRF_FLUSH_TX_INIT;
            }
            break;
        case NRF_FLUSH_TX_INIT: s_state = NRF_FLUSH_RX_INIT; break;
        case NRF_FLUSH_RX_INIT: s_state = NRF_CLEAR_INIT; break;
        case NRF_CLEAR_INIT: s_state = NRF_POWER_UP; break;
        case NRF_POWER_UP: s_state = NRF_POWER_VERIFY; break;
        case NRF_POWER_VERIFY:
            s_stats.config = value;
            if (value != 0x0BU || s_rx[0] == 0xFFU)
            {
                s_stats.verify_failures++;
                nrf_fail(NRF_FAILURE_CONFIG);
            }
            else
            {
                s_wait_tick = HAL_GetTick();
                s_state = NRF_SETTLE;
            }
            break;
        case NRF_FIFO:
            s_poll_tick = HAL_GetTick();
            s_stats.fifo_status = value;
            if (value & 0x10U) s_ack_in_fifo = 0U;
            if ((value == 0xFFU) || ((value & 3U) == 3U) ||
                (s_rx[0] == 0xFFU))
            {
                s_stats.fifo_errors++;
                nrf_fail(NRF_FAILURE_FIFO);
            }
            else if (s_rx[0] & 0x10U)
            {
                /* 异常MAX_RT先退到待机并清TX，随后统一清除中断标志。 */
                s_stats.ack_failures++;
                nrf_set_ack_clear_reason(NRF24L01_ACK_CLEAR_MAX_RT);
                nrf_ack_standby();
            }
            else if (s_ack_pending && s_stats.ack_supported &&
                     ((value & 1U) || s_rx_since_ack))
            {
                if (value & 0x10U) s_state = NRF_ACK;
                else if ((value & 0x20U) ||
                         HAL_GetTick() - s_ack_tick >=
                             APP_NRF_ACK_LOAD_TIMEOUT_MS)
                {
                    nrf_set_ack_clear_reason((value & 0x20U) ?
                        NRF24L01_ACK_CLEAR_TX_FULL : NRF24L01_ACK_CLEAR_TIMEOUT);
                    nrf_ack_standby();
                }
                else if ((value & 1U) == 0U)
                    s_state = NRF_WIDTH;
                else s_state = NRF_IDLE;
            }
            else if ((value & 1U) == 0U)
            {
                if (value & 2U) s_stats.fifo_full_events++;
                s_state = s_stats.ack_supported ? NRF_WIDTH : NRF_PAYLOAD;
            }
            else if (s_rx[0] & 0x70U) s_state = NRF_CLEAR_IRQ;
            else s_state = NRF_IDLE;
            break;
        case NRF_WIDTH:
            if (value == 32U) s_state = NRF_PAYLOAD;
            else
            {
                s_stats.invalid_widths++;
                nrf_fail(NRF_FAILURE_WIDTH);
            }
            break;
        case NRF_PAYLOAD:
            s_rx_since_ack = 1U;
            if (s_rx_count < 3U)
            {
                memcpy(s_received[(s_rx_head + s_rx_count) % 3U],
                       &s_rx[1], 32U);
                s_rx_count++;
                s_stats.rx_packets++;
            }
            else s_stats.rx_dropped++;
            s_state = NRF_FIFO;
            break;
        case NRF_CLEAR_IRQ: s_state = NRF_FIFO; break;
        case NRF_ACK:
            if (s_rx[0] & 1U)
            {
                /* 命令返回TX_FULL时没有装载成功，保留软件帧供清FIFO后重试。 */
                s_stats.ack_failures++;
                nrf_set_ack_clear_reason(NRF24L01_ACK_CLEAR_TX_FULL);
                s_rx_since_ack = 0U;
                nrf_ack_standby();
                break;
            }
            s_stats.ack_loaded++;
            {
                uint32_t wait = HAL_GetTick() - s_ack_pending_tick;
                if (wait > s_stats.ack_max_wait_ms)
                    s_stats.ack_max_wait_ms = wait;
            }
            s_ack_in_fifo = 1U;
            s_rx_since_ack = 0U;
            s_ack_timeout_cleaned = 0U;
            if (s_loading_generation == s_ack_generation)
                s_ack_pending = 0U;
            else
                s_ack_pending_tick = s_ack_progress_tick = HAL_GetTick();
            s_ack_tick = HAL_GetTick();
            if (!s_stats.radio_ready) nrf_enter_receive();
            else s_state = NRF_FIFO;
            break;
        case NRF_ACK_FLUSH:
            s_stats.ack_flushed++;
            s_stats.ack_clear_count++;
            s_stats.ack_clear_reason = s_ack_clear_reason;
            s_stats.ack_clear_by_reason[s_ack_clear_reason]++;
            nrf_record_event(s_ack_clear_reason);
            s_ack_clear_reason = NRF24L01_ACK_CLEAR_NONE;
            s_ack_in_fifo = 0U;
            if (s_stats.radio_ready) nrf_ce(1U);
            s_state = s_stats.radio_ready ? NRF_CLEAR_IRQ : NRF_FIRST_ACK;
            break;
        case NRF_CONFIG_CHECK:
            if (nrf_check_reg() == NRF24L01_FEATURE) s_stats.feature = value;
            if (nrf_check_reg() == NRF24L01_DYNPD) s_stats.dynpd = value;
            if (!nrf_check_matches(nrf_check_reg(), value))
            {
                s_stats.config_check_mismatches++;
                nrf_fail(NRF_FAILURE_CONFIG_CHECK);
            }
            else
            {
                if (++s_check_index == sizeof(s_config) / sizeof(s_config[0]) + 2U)
                {
                    s_check_active = 0U;
                    s_stats.config_check_count++;
                }
                s_state = s_check_resume_state;
            }
            break;
        default: break;
    }
}

void NRF24L01_Init(void)
{
    s_stats.initialized = 0U;
    s_stats.radio_ready = 0U;
    s_stats.ack_supported = 0U;
    s_stats.recoveries++;
    s_stats.recovery_generation++;
    s_config_index = 0U;
    s_activate_tried = 0U;
    s_feature_value = APP_NRF_ACK_PAYLOAD_ENABLED ? 6U : 0U;
    s_rx_head = 0U;
    s_rx_count = 0U;
    s_ack_pending = 0U;
    s_ack_in_fifo = 0U;
    s_check_active = 0U;
    s_rx_since_ack = 0U;
    s_ack_timeout_cleaned = 0U;
    s_ack_clear_reason = NRF24L01_ACK_CLEAR_NONE;
    s_have_control_frame = 0U;
    s_need_dma_reset = 1U;
    CLEAR_BIT(hspi2.Instance->CR2,
              SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN | SPI_CR2_ERRIE);
    __HAL_SPI_DISABLE(&hspi2);
    __HAL_DMA_DISABLE(hspi2.hdmarx);
    __HAL_DMA_DISABLE(hspi2.hdmatx);
    s_done = 0U;
    s_error = 0U;
    nrf_ce(0U);
    nrf_csn(1U);
    s_wait_tick = HAL_GetTick();
    s_state = NRF_BOOT_WAIT;
}

void NRF24L01_BindTask(TaskHandle_t task) { s_task = task; }

void NRF24L01_Service(void)
{
    uint8_t reg;
    uint32_t now = HAL_GetTick();
    if (s_need_dma_reset)
    {
        /* 禁用流后等待硬件自行结束，不在任务里阻塞等待DMA关闭。 */
        if ((hspi2.hdmarx->Instance->CR & DMA_SxCR_EN) ||
            (hspi2.hdmatx->Instance->CR & DMA_SxCR_EN)) return;
        nrf_reset_dma(hspi2.hdmarx);
        nrf_reset_dma(hspi2.hdmatx);
        __HAL_SPI_CLEAR_OVRFLAG(&hspi2);
        hspi2.State = HAL_SPI_STATE_READY;
        hspi2.Lock = HAL_UNLOCKED;
        hspi2.ErrorCode = HAL_SPI_ERROR_NONE;
        s_active = 0U;
        s_need_dma_reset = 0U;
        nrf_csn(1U);
    }
    if (s_state == NRF_RECOVER)
    {
        if (HAL_GetTick() - s_wait_tick >= APP_NRF_RECOVERY_BACKOFF_MS)
            NRF24L01_Init();
        return;
    }
    /* 首帧期限覆盖装载、待机及清FIFO全过程，不能被重试重置。 */
    if (s_stats.initialized && !s_stats.radio_ready &&
        APP_NRF_TELEMETRY_ENABLED &&
        now - s_first_ack_tick >= APP_NRF_ACK_LOAD_TIMEOUT_MS)
    {
        s_stats.ack_load_timeouts++;
        nrf_fail(NRF_FAILURE_FIRST_ACK);
        return;
    }
    if (s_active)
    {
        if (s_error || (__HAL_SPI_GET_FLAG(&hspi2, SPI_FLAG_OVR) != RESET))
        {
            nrf_fail(NRF_FAILURE_SPI);
            return;
        }
        if (s_done && (__HAL_SPI_GET_FLAG(&hspi2, SPI_FLAG_BSY) == RESET))
        {
            nrf_csn(1U);
            hspi2.TxXferCount = 0U;
            hspi2.RxXferCount = 0U;
            hspi2.State = HAL_SPI_STATE_READY;
            s_active = 0U;
            nrf_complete();
            if (s_state == NRF_RECOVER) return;
        }
        else if (HAL_GetTick() - s_started >=
                 APP_NRF_SPI_TRANSACTION_TIMEOUT_MS)
        {
            s_stats.dma_timeouts++;
            nrf_fail(NRF_FAILURE_DMA_TIMEOUT);
            return;
        }
        else return;
    }
    if (s_stats.radio_ready && !s_check_active &&
        now - s_check_tick >= APP_NRF_CONFIG_CHECK_PERIOD_MS)
    {
        s_check_active = 1U;
        s_check_index = 0U;
        s_check_tick = now;
        s_check_slot_tick = now - APP_NRF_CONFIG_CHECK_SLOT_MS;
    }
    /* 待装载计时不被最新帧替换重置，清理一次仍无进展则重建。 */
    if (s_stats.radio_ready && s_ack_pending && s_have_control_frame &&
        now - s_control_frame_tick < APP_REMOTE_SERVO_LINK_TIMEOUT_MS &&
        now - s_ack_progress_tick >= APP_NRF_ACK_LOAD_TIMEOUT_MS &&
        !s_active)
    {
        if (s_ack_timeout_cleaned)
        {
            s_stats.ack_load_timeouts++;
            nrf_set_ack_clear_reason(NRF24L01_ACK_CLEAR_TIMEOUT);
            nrf_fail(NRF_FAILURE_ACK_STALLED);
            return;
        }
        s_stats.ack_load_timeouts++;
        nrf_set_ack_clear_reason(NRF24L01_ACK_CLEAR_TIMEOUT);
        s_ack_timeout_cleaned = 1U;
        s_ack_progress_tick = now;
        nrf_ack_standby();
    }
    /* 提前一个轮询周期安排回读，保留接收/ACK进度，预留任务唤醒误差。 */
    if (s_stats.radio_ready && s_check_active &&
        now - s_check_slot_tick >=
            APP_NRF_CONFIG_CHECK_SLOT_MS - APP_NRF_TASK_PERIOD_MS &&
        (s_state == NRF_IDLE || s_state == NRF_FIFO || s_state == NRF_WIDTH ||
         s_state == NRF_PAYLOAD || s_state == NRF_CLEAR_IRQ || s_state == NRF_ACK))
    {
        s_check_resume_state = s_state;
        s_state = NRF_CONFIG_CHECK;
    }
    switch (s_state)
    {
        case NRF_IDLE:
            if (HAL_GetTick() - s_poll_tick < APP_NRF_TASK_PERIOD_MS) return;
            s_state = NRF_FIFO;
            break;
        case NRF_BOOT_WAIT:
            if (HAL_GetTick() - s_wait_tick < 100U) return;
            s_state = NRF_CONFIG_WRITE;
            break;
        case NRF_SETTLE:
            if (HAL_GetTick() - s_wait_tick < 2U) return;
            s_stats.initialized = 1U;
            if (APP_NRF_TELEMETRY_ENABLED)
            {
                s_first_ack_tick = now;
                s_state = NRF_FIRST_ACK;
            }
            else nrf_enter_receive();
            break;
        case NRF_FIRST_ACK:
            if (now - s_first_ack_tick >= APP_NRF_ACK_LOAD_TIMEOUT_MS)
            {
                s_stats.ack_load_timeouts++;
                nrf_set_ack_clear_reason(NRF24L01_ACK_CLEAR_TIMEOUT);
                nrf_fail(NRF_FAILURE_FIRST_ACK);
                return;
            }
            if (!s_ack_pending) return;
            s_state = NRF_ACK;
            break;
        case NRF_ACK_STANDBY:
            if (HAL_GetTick() - s_wait_tick < 2U) return;
            s_state = NRF_ACK_FLUSH;
            break;
        default: break;
    }
    switch (s_state)
    {
        case NRF_CONFIG_WRITE:
            reg = s_config[s_config_index].reg;
            if (reg == NRF24L01_RX_ADDR_P0)
                nrf_start((uint8_t)(NRF24L01_W_REGISTER | reg),
                          NRF24L01_RxAddress, 5U);
            else nrf_write(reg, s_config[s_config_index].value);
            break;
        case NRF_CONFIG_VERIFY:
            reg = s_config[s_config_index].reg;
            nrf_start(reg, NULL, reg == NRF24L01_RX_ADDR_P0 ? 5U : 1U);
            break;
        case NRF_FEATURE_WRITE: nrf_write(NRF24L01_FEATURE, s_feature_value); break;
        case NRF_FEATURE_VERIFY: nrf_read(NRF24L01_FEATURE); break;
        case NRF_FEATURE_ACTIVATE:
            reg = 0x73U;
            nrf_start(0x50U, &reg, 1U);
            break;
        case NRF_DYNPD_WRITE:
            nrf_write(NRF24L01_DYNPD, s_feature_value ? 1U : 0U);
            break;
        case NRF_DYNPD_VERIFY: nrf_read(NRF24L01_DYNPD); break;
        case NRF_FLUSH_TX_INIT: case NRF_ACK_FLUSH:
            nrf_start(NRF24L01_FLUSH_TX, NULL, 0U); break;
        case NRF_FLUSH_RX_INIT: nrf_start(NRF24L01_FLUSH_RX, NULL, 0U); break;
        case NRF_CLEAR_INIT: case NRF_CLEAR_IRQ:
            nrf_write(NRF24L01_STATUS, 0x70U); break;
        case NRF_POWER_UP: nrf_write(NRF24L01_CONFIG, 0x0BU); break;
        case NRF_POWER_VERIFY: nrf_read(NRF24L01_CONFIG); break;
        case NRF_FIFO: nrf_read(NRF24L01_FIFO_STATUS); break;
        case NRF_WIDTH: nrf_start(NRF24L01_R_RX_PL_WID, NULL, 1U); break;
        case NRF_PAYLOAD: nrf_start(NRF24L01_R_RX_PAYLOAD, NULL, 32U); break;
        case NRF_ACK:
            s_loading_generation = s_ack_generation;
            nrf_start(NRF24L01_W_ACK_PAYLOAD, s_ack, 32U);
            break;
        case NRF_CONFIG_CHECK:
            s_check_slot_tick = now;
            reg = nrf_check_reg();
            nrf_start(reg, NULL, reg == NRF24L01_RX_ADDR_P0 ? 5U : 1U);
            break;
        default: break;
    }
}

bool NRF24L01_IsInitialized(void) { return s_stats.initialized != 0U; }
bool NRF24L01_IsRadioReady(void) { return s_stats.radio_ready != 0U; }

void NRF24L01_ReportControlFrame(void)
{
    /* 只由无线任务在控制帧通过应用层校验后调用。 */
    s_have_control_frame = 1U;
    s_control_frame_tick = HAL_GetTick();
}

void NRF24L01_GetStats(NRF24L01_Stats *stats)
{
    if (stats == NULL) return;
    taskENTER_CRITICAL();
    *stats = s_stats;
    stats->dma_busy = s_active;
    stats->ack_pending = s_ack_pending;
    stats->ack_in_fifo = s_ack_in_fifo;
    stats->ack_pending_age_ms = s_ack_pending ? HAL_GetTick() - s_ack_pending_tick : 0U;
    stats->ack_fifo_age_ms = s_ack_in_fifo ? HAL_GetTick() - s_ack_tick : 0U;
    stats->ack_clear_reason = s_stats.ack_clear_reason;
    taskEXIT_CRITICAL();
}

#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
uint8_t NRF24L01_DiagnosticWords(uint32_t *words, uint8_t phase)
{
    uint8_t count = 0U;
    if (words == NULL) return 0U;
    taskENTER_CRITICAL();
    if (phase == 0U) {
        words[0] = s_stats.spi_errors; words[1] = s_stats.fifo_errors;
        words[2] = s_stats.recoveries; words[3] = s_stats.dma_started;
        words[4] = s_stats.dma_completed; words[5] = s_stats.dma_timeouts;
        words[6] = s_stats.rx_dropped; words[7] = s_stats.invalid_widths;
        words[8] = s_stats.config_check_mismatches; words[9] = s_stats.last_failure;
        words[10] = s_stats.verify_failures; words[11] = s_stats.rx_packets;
        count = 12U;
    } else if (phase == 1U) {
        words[0] = s_stats.ack_queued; words[1] = s_stats.ack_loaded;
        words[2] = s_stats.ack_replaced; words[3] = s_stats.ack_flushed;
        words[4] = s_stats.ack_failures; words[5] = s_stats.ack_load_timeouts;
        words[6] = s_stats.ack_max_wait_ms;
        words[7] = s_ack_pending ? HAL_GetTick() - s_ack_pending_tick : 0U;
        words[8] = s_ack_in_fifo ? HAL_GetTick() - s_ack_tick : 0U;
        words[9] = s_ack_pending | ((uint32_t)s_ack_in_fifo << 1U) |
            ((uint32_t)s_stats.ack_clear_reason << 8U) | ((uint32_t)s_stats.ack_supported << 16U) |
            ((uint32_t)s_active << 17U) | ((uint32_t)s_stats.radio_ready << 18U) |
            ((uint32_t)s_stats.initialized << 19U);
        words[10] = s_stats.ack_clear_count;
        words[11] = s_stats.ack_clear_by_reason[NRF24L01_ACK_CLEAR_MAX_RT];
        words[12] = s_stats.ack_clear_by_reason[NRF24L01_ACK_CLEAR_TIMEOUT];
        words[13] = s_stats.ack_clear_by_reason[NRF24L01_ACK_CLEAR_TX_FULL];
        words[14] = s_stats.ack_clear_by_reason[NRF24L01_ACK_CLEAR_RECOVERY];
        words[15] = s_stats.event_overwrites;
        count = 16U;
    } else if (phase == 2U) {
        const NRF24L01_FailureSnapshot *f = &s_stats.failure_snapshot;
        words[0] = f->tick_ms; words[1] = f->elapsed_ms;
        words[2] = f->spi_cr1; words[3] = f->spi_cr2; words[4] = f->spi_sr;
        words[5] = f->rx_remaining; words[6] = f->tx_remaining;
        words[7] = f->rx_cr; words[8] = f->tx_cr;
        words[9] = f->rx_flags; words[10] = f->tx_flags;
        words[11] = f->rx_error; words[12] = f->tx_error;
        words[13] = f->valid | ((uint32_t)f->state << 8U) |
            ((uint32_t)f->command << 16U) | ((uint32_t)f->length << 24U);
        words[14] = f->active | ((uint32_t)f->done << 8U) | ((uint32_t)f->error << 16U);
        words[15] = s_stats.config | ((uint32_t)s_stats.status << 8U) |
            ((uint32_t)s_stats.fifo_status << 16U) | ((uint32_t)s_stats.feature << 24U);
        count = 16U;
    }
    taskEXIT_CRITICAL();
    return count;
}
#endif

uint8_t NRF24L01_Receive(void)
{
    if (s_rx_count == 0U) return s_stats.radio_ready ? 0U : 3U;
    memcpy(NRF24L01_RxPacket, s_received[s_rx_head], 32U);
    s_rx_head = (uint8_t)((s_rx_head + 1U) % 3U);
    s_rx_count--;
    return 1U;
}

bool NRF24L01_CanQueueAck(void)
{
    return s_stats.initialized && s_stats.ack_supported && !s_ack_pending &&
        (!s_ack_in_fifo || HAL_GetTick() - s_ack_tick >= 100U);
}

bool NRF24L01_QueueAckPayload(const uint8_t payload[32])
{
    if ((payload == NULL) || !s_stats.initialized || !s_stats.ack_supported)
        return false;
    if (s_ack_pending) s_stats.ack_replaced++;
    else s_ack_pending_tick = s_ack_progress_tick = HAL_GetTick();
    memcpy(s_ack, payload, 32U);
    s_ack_pending = 1U;
    s_ack_generation++;
    s_stats.ack_queued++;
    return true;
}
