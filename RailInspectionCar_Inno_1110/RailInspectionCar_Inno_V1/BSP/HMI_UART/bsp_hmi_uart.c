#include "bsp_hmi_uart.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "app_config.h"
#include "usart.h"
#include "tim.h"

#define HMI_RECOVERY_RETRY_MS (10U)
#define HMI_ABORT_TIMEOUT_MS (20U)
#define HMI_RECOVERY_REQUESTED (1U)
#define HMI_RECOVERY_ABORTING (2U)
#define HMI_RECOVERY_DONE (3U)

typedef struct
{
    uint32_t end, cycles, generation;
    uint8_t boundary;
} hmi_rx_descriptor_t;

typedef struct
{
    uint32_t epoch, reason, last_attempt, abort_epoch, abort_at;
    uint8_t pending, attempted, timed_out;
} hmi_uart_recovery_t;

__ALIGN_BEGIN static uint8_t s_dma_rx[APP_HMI_DMA_RX_SIZE] __ALIGN_END;
__ALIGN_BEGIN static uint8_t s_tx_buffer[APP_HMI_TX_BUFFER_SIZE] __ALIGN_END;
static uint8_t s_rx_ring[APP_HMI_RX_BUFFER_SIZE];
static hmi_rx_descriptor_t s_events[APP_HMI_RX_EVENT_COUNT];
static volatile uint32_t s_rx_head, s_rx_tail, s_rx_generation;
static volatile uint16_t s_event_head, s_event_tail, s_dma_position;
static volatile uint8_t s_tx_active, s_quiet_pending;
static volatile hmi_uart_stats_t s_stats;
static volatile hmi_uart_diag_t s_diag;
static volatile hmi_uart_recovery_t s_rx_recovery, s_tx_recovery;
static uint32_t s_last_rx_cycles, s_quiet_head, s_quiet_generation, s_quiet_at;
static uint32_t s_tx_started, s_tx_limit, s_service_tick;
static uint32_t s_tx_request_cycles, s_tx_request_generation;
static uint8_t s_tx_exception, s_tx_region;
static uint16_t s_diag_tx_length;
static uint32_t s_diag_completed_info;
static TaskHandle_t s_task;

static void notify_isr(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (s_task != NULL)
    {
        if (__get_IPSR() != 0U)
        {
            vTaskNotifyGiveFromISR(s_task, &higher_priority_task_woken);
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }
        else (void)xTaskNotifyGive(s_task);
    }
}

static uint16_t dma_position(void)
{
    uint32_t remaining = __HAL_DMA_GET_COUNTER(huart5.hdmarx);
    return (uint16_t)((APP_HMI_DMA_RX_SIZE - remaining) % APP_HMI_DMA_RX_SIZE);
}

static uint8_t dma_config_valid(void)
{
    return huart5.hdmarx != NULL && huart5.hdmatx != NULL &&
        huart5.hdmarx->Instance == DMA1_Stream0 && huart5.hdmatx->Instance == DMA1_Stream7 &&
        huart5.hdmarx->Parent == &huart5 && huart5.hdmatx->Parent == &huart5 &&
        huart5.hdmarx->Init.Mode == DMA_CIRCULAR && huart5.hdmatx->Init.Mode == DMA_NORMAL &&
        huart5.hdmarx->Init.Channel == DMA_CHANNEL_4 && huart5.hdmatx->Init.Channel == DMA_CHANNEL_4 &&
        huart5.hdmarx->Init.Direction == DMA_PERIPH_TO_MEMORY &&
        huart5.hdmatx->Init.Direction == DMA_MEMORY_TO_PERIPH;
}

static uint8_t rx_armed(void)
{
    return dma_config_valid() && huart5.RxState == HAL_UART_STATE_BUSY_RX &&
        huart5.ReceptionType == HAL_UART_RECEPTION_TOIDLE &&
        huart5.RxXferSize == APP_HMI_DMA_RX_SIZE && huart5.pRxBuffPtr == s_dma_rx &&
        (huart5.hdmarx->Instance->CR & (DMA_SxCR_EN | DMA_SxCR_CIRC)) == (DMA_SxCR_EN | DMA_SxCR_CIRC) &&
        (huart5.Instance->CR3 & USART_CR3_DMAR) != 0U &&
        (huart5.Instance->CR1 & USART_CR1_IDLEIE) != 0U;
}

static void quiet_cancel(void)
{
    htim2.Instance->CR1 &= ~TIM_CR1_CEN;
    htim2.Instance->DIER &= ~TIM_DIER_UIE;
    s_quiet_pending = 0U;
}

static void start_quiet_timer(void)
{
    s_quiet_head = s_rx_head; s_quiet_generation = s_rx_generation; s_quiet_at = DWT->CYCCNT;
    htim2.Instance->CR1 &= ~TIM_CR1_CEN;
    htim2.Instance->CNT = 0U; htim2.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
    htim2.Instance->DIER |= TIM_DIER_UIE;
    s_quiet_pending = 1U;
    htim2.Instance->CR1 |= TIM_CR1_OPM | TIM_CR1_CEN;
}

static void request_rx(uint32_t reason)
{
    if (!s_rx_recovery.pending) s_rx_recovery.attempted = 0U;
    if (s_rx_recovery.pending != HMI_RECOVERY_ABORTING)
        s_rx_recovery.pending = HMI_RECOVERY_REQUESTED;
    s_rx_recovery.reason = reason; s_rx_recovery.epoch++;
    huart5.Instance->CR1 &= ~(USART_CR1_IDLEIE | USART_CR1_RXNEIE | USART_CR1_PEIE);
    huart5.Instance->CR3 &= ~USART_CR3_EIE;
    quiet_cancel(); s_rx_generation++;
    s_rx_tail = s_rx_head; s_event_tail = s_event_head;
}

static void request_tx(uint32_t reason)
{
    if (!s_tx_recovery.pending)
    {
        s_tx_recovery.attempted = 0U;
        if (s_tx_active) s_stats.tx_errors++;
    }
    if (s_tx_recovery.pending != HMI_RECOVERY_ABORTING)
        s_tx_recovery.pending = HMI_RECOVERY_REQUESTED;
    s_tx_recovery.reason = reason; s_tx_recovery.epoch++;
}

static uint8_t queue_event(uint8_t boundary, uint32_t end, uint32_t cycles)
{
    uint16_t next = (uint16_t)((s_event_head + 1U) % APP_HMI_RX_EVENT_COUNT);
    uint16_t depth;
    if (next == s_event_tail)
    {
        s_diag.event_overflow++;
        request_rx(HMI_UART_RECOVERY_STATE);
        return 0U;
    }
    s_events[s_event_head].end = end;
    s_events[s_event_head].cycles = cycles;
    s_events[s_event_head].generation = s_rx_generation;
    s_events[s_event_head].boundary = boundary;
    __DMB();
    s_event_head = next;
    depth = (uint16_t)((s_event_head + APP_HMI_RX_EVENT_COUNT - s_event_tail) % APP_HMI_RX_EVENT_COUNT);
    if (depth > s_diag.event_high_water) s_diag.event_high_water = depth;
    return 1U;
}

static uint8_t capture_dma(void)
{
    uint32_t start = DWT->CYCCNT, count, offset, depth, duration;
    uint16_t position = dma_position();
    count = (position + APP_HMI_DMA_RX_SIZE - s_dma_position) % APP_HMI_DMA_RX_SIZE;
    if (count == 0U) return 1U;
    quiet_cancel();
    s_diag.raw_bytes += count;
    if ((s_rx_head - s_rx_tail) > APP_HMI_RX_BUFFER_SIZE ||
        count > APP_HMI_RX_BUFFER_SIZE - (s_rx_head - s_rx_tail))
    {
        s_stats.rx_overflow++;
        request_rx(HMI_UART_RECOVERY_STATE);
        return 0U;
    }
    for (offset = 0U; offset < count; offset++)
        s_rx_ring[(s_rx_head + offset) % APP_HMI_RX_BUFFER_SIZE] =
            s_dma_rx[(s_dma_position + offset) % APP_HMI_DMA_RX_SIZE];
    if (!queue_event(0U, s_rx_head + count, start)) return 0U;
    s_rx_head += count;
    s_dma_position = position;
    s_last_rx_cycles = start;
    s_diag.raw_last_cycles = start;
    s_stats.rx_bytes += count;
    s_diag.rx_last_ms = HAL_GetTick();
    depth = s_rx_head - s_rx_tail;
    if (depth > s_diag.queue_high_water) s_diag.queue_high_water = depth;
    duration = DWT->CYCCNT - start;
    if (duration > s_diag.capture_max_cycles) s_diag.capture_max_cycles = duration;
    return 1U;
}

static uint8_t arm_rx(void)
{
    HAL_StatusTypeDef result = HAL_ERROR;
    uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) timer_clock *= 2U;
    s_diag.dma_starts++;
if (dma_config_valid() && huart5.RxState == HAL_UART_STATE_READY &&
        (huart5.hdmarx->Instance->CR & DMA_SxCR_EN) == 0U &&
        timer_clock / (htim2.Instance->PSC + 1U) == 1000000U &&
        htim2.Instance->ARR == APP_HMI_RTU_QUIET_US - 1U)
    {
        s_dma_position = 0U;
        __HAL_UART_CLEAR_OREFLAG(&huart5);
        result = HAL_UARTEx_ReceiveToIdle_DMA(&huart5, s_dma_rx, APP_HMI_DMA_RX_SIZE);
    }
    s_diag.arm_result = s_diag.dma_start_result = (uint32_t)result;
    if (result == HAL_OK && rx_armed()) return 1U;
    s_diag.arm_failures++;
    return 0U;
}

void bsp_hmi_uart_init(void)
{
    memset((void *)&s_stats, 0, sizeof(s_stats));
    memset((void *)&s_diag, 0, sizeof(s_diag));
    s_diag.quiet_elapsed_min_cycles = UINT32_MAX;
    memset((void *)&s_rx_recovery, 0, sizeof(s_rx_recovery));
    memset((void *)&s_tx_recovery, 0, sizeof(s_tx_recovery));
    s_rx_head = s_rx_tail = s_rx_generation = 0U;
    s_event_head = s_event_tail = s_dma_position = 0U;
    s_quiet_head = 0U;
    s_tx_active = s_tx_exception = 0U;
    s_tx_region = HMI_UART_OTHER_REGION;
    s_diag_tx_length = 0U; s_diag_completed_info = 0U;
    s_task = NULL;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_last_rx_cycles = DWT->CYCCNT;
    s_service_tick = HAL_GetTick();
    quiet_cancel();
    if (!arm_rx()) request_rx(HMI_UART_RECOVERY_ARM);
}

void bsp_hmi_uart_bind_task(void *task)
{
    taskENTER_CRITICAL(); s_task = (TaskHandle_t)task; taskEXIT_CRITICAL();
}

uint32_t bsp_hmi_uart_cycles(void) { return DWT->CYCCNT; }
uint32_t bsp_hmi_uart_clock_hz(void) { return SystemCoreClock; }

uint8_t bsp_hmi_uart_pop(hmi_uart_rx_event_t *event)
{
    hmi_rx_descriptor_t descriptor;
    uint32_t offset, mask, length;
    if (event == NULL) return 0U;
    mask = __get_PRIMASK();
    __disable_irq();
    if (s_event_tail == s_event_head) { __set_PRIMASK(mask); return 0U; }
    descriptor = s_events[s_event_tail];
    length = descriptor.end - s_rx_tail;
    if (length > sizeof(event->data)) length = sizeof(event->data);
    event->cycles = descriptor.cycles;
    event->generation = descriptor.generation;
    /* 只有该描述符的最后一个分块才能携带帧边界。 */
    event->boundary = (uint8_t)(descriptor.boundary &&
        (s_rx_tail + length == descriptor.end));
    event->length = (uint16_t)length;
    for (offset = 0U; offset < length; offset++)
        event->data[offset] = s_rx_ring[(s_rx_tail + offset) % APP_HMI_RX_BUFFER_SIZE];
    s_rx_tail += length;
    s_diag.rx_dequeued += length;
    if (s_rx_tail == descriptor.end)
        s_event_tail = (uint16_t)((s_event_tail + 1U) % APP_HMI_RX_EVENT_COUNT);
    __set_PRIMASK(mask);
    return 1U;
}

void bsp_hmi_uart_rx_state(uint32_t *generation, uint32_t *last_cycles, uint8_t *empty)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *generation = s_rx_generation; *last_cycles = s_last_rx_cycles;
    *empty = s_event_tail == s_event_head;
    __set_PRIMASK(mask);
}

void bsp_hmi_uart_discard(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq(); s_rx_tail = s_rx_head; s_event_tail = s_event_head; __set_PRIMASK(mask);
}

void bsp_hmi_uart_rx_event_isr(uint16_t position, uint32_t kind)
{
    s_diag.rx_callbacks++;
    if (kind == HAL_UART_RXEVENT_IDLE) s_diag.idle_events++;
    else if (kind == HAL_UART_RXEVENT_HT) s_diag.half_events++;
    else if (kind == HAL_UART_RXEVENT_TC) s_diag.full_events++;
    if (s_rx_recovery.pending || !rx_armed()) return;
    if (position > APP_HMI_DMA_RX_SIZE || huart5.ErrorCode != HAL_UART_ERROR_NONE)
    { request_rx(HMI_UART_RECOVERY_ERROR); notify_isr(); return; }
    if (!capture_dma()) { notify_isr(); return; }
    if (kind == HAL_UART_RXEVENT_IDLE && s_rx_head != s_quiet_head)
    {
        start_quiet_timer();
    }
    notify_isr();
}

void bsp_hmi_uart_quiet_isr(void)
{
    uint8_t candidate = s_quiet_pending;
    uint32_t quiet_elapsed;
    quiet_cancel();
    if (!candidate || s_rx_recovery.pending || !rx_armed()) return;
    if (dma_position() != s_dma_position || s_quiet_head != s_rx_head ||
        s_quiet_generation != s_rx_generation || huart5.ErrorCode != HAL_UART_ERROR_NONE)
    { (void)capture_dma(); return; }
    quiet_elapsed = (uint32_t)(DWT->CYCCNT - s_quiet_at);
    if (quiet_elapsed < s_diag.quiet_elapsed_min_cycles)
        s_diag.quiet_elapsed_min_cycles = quiet_elapsed;
    if (quiet_elapsed <
        (uint32_t)((uint64_t)SystemCoreClock * APP_HMI_RTU_QUIET_US / 1000000U))
    { s_diag.quiet_early_count++; return; }
    if (queue_event(1U, s_rx_head, DWT->CYCCNT)) s_diag.boundaries++;
    notify_isr();
}

void bsp_hmi_uart_irq_entry_isr(void)
{ s_diag.irq_entries++; s_diag.irq_sr = huart5.Instance->SR; }

void bsp_hmi_uart_dma_irq_isr(uint8_t transmit)
{
    if (transmit)
    {
        s_diag.dma_tx_irqs++;
        if (__HAL_DMA_GET_FLAG(huart5.hdmatx, __HAL_DMA_GET_TC_FLAG_INDEX(huart5.hdmatx)) &&
            s_tx_active && !s_tx_recovery.pending) s_diag.tx_dma_complete++;
    }
    else
    {
        s_diag.dma_rx_irqs++;
    }
}

uint8_t bsp_hmi_uart_send_response(const uint8_t *data, uint16_t length,
                                  uint8_t region, uint32_t last_byte_cycles)
{
    HAL_StatusTypeDef result;
    uint32_t generation, delay;
    if (data == NULL || !length || length > sizeof(s_tx_buffer)) return 0U;
    taskENTER_CRITICAL();
    if (s_tx_active || s_tx_recovery.pending || s_rx_recovery.pending || !dma_config_valid())
    {
        s_stats.tx_busy++; s_diag.tx_dropped++;
        taskEXIT_CRITICAL(); return 0U;
    }
    generation = s_rx_generation; s_tx_active = 1U; s_tx_started = HAL_GetTick();
    s_tx_limit = ((uint32_t)length * 10000U + APP_HMI_BAUD_RATE - 1U) / APP_HMI_BAUD_RATE + 20U;
    taskEXIT_CRITICAL();
    memcpy(s_tx_buffer, data, length);
    taskENTER_CRITICAL();
    if (s_tx_recovery.pending || s_rx_recovery.pending || generation != s_rx_generation)
    {
        s_tx_active = 0U; s_stats.tx_errors++; s_diag.tx_dropped++;
        taskEXIT_CRITICAL(); return 0U;
    }
    s_tx_exception = length >= 2U && (data[1] & 0x80U) != 0U;
    s_tx_region = region < HMI_UART_REGION_COUNT ? region : HMI_UART_OTHER_REGION;
    s_diag_tx_length = length;
    s_tx_request_cycles = last_byte_cycles;
    s_tx_request_generation = generation;
    result = HAL_UART_Transmit_DMA(&huart5, s_tx_buffer, length);
    if (result != HAL_OK)
    {
        s_diag.tx_dropped++; request_tx(HMI_UART_RECOVERY_SUBMIT);
        taskEXIT_CRITICAL(); return 0U;
    }
    delay = DWT->CYCCNT - last_byte_cycles;
    if (delay > s_diag.response_delay_max_cycles) s_diag.response_delay_max_cycles = delay;
    s_stats.tx_frames++;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t bsp_hmi_uart_send(const uint8_t *data, uint16_t length)
{ return bsp_hmi_uart_send_response(data, length, HMI_UART_OTHER_REGION, DWT->CYCCNT); }

void bsp_hmi_uart_tx_complete_isr(void)
{
    uint32_t duration = HAL_GetTick() - s_tx_started;
    s_diag.tx_callbacks++;
    if (s_tx_active && !s_tx_recovery.pending && huart5.gState == HAL_UART_STATE_READY)
    {
        if (duration > s_diag.tx_max_ms) s_diag.tx_max_ms = duration;
        if (s_tx_exception) s_stats.exceptions++;
        else { s_stats.responses++; s_diag.region_responses[s_tx_region]++; }
        s_diag.tx_last_ms = HAL_GetTick(); s_tx_active = 0U;
        s_diag.tx_complete_cycles = DWT->CYCCNT;
        if (s_tx_request_generation == s_rx_generation)
        {
            uint32_t elapsed = s_diag.tx_complete_cycles - s_tx_request_cycles;
            s_diag.response_complete_last_cycles = elapsed;
            if (elapsed > s_diag.response_complete_max_cycles)
                s_diag.response_complete_max_cycles = elapsed;
        }
        s_diag_completed_info = ((uint32_t)s_tx_region << 16U) | s_diag_tx_length;
    }
    notify_isr();
}

uint8_t bsp_hmi_uart_request_region(uint16_t start, uint16_t quantity)
{
    static const uint16_t starts[6] = {0U, 0x50U, 0x70U, 0x90U, 0xB0U, 0x200U};
    static const uint16_t counts[6] = {64U, 32U, 32U, 32U, 32U, 24U};
    uint8_t index, region = HMI_UART_OTHER_REGION;
    for (index = 0U; index < 6U; index++)
        if (start >= starts[index] && (uint32_t)start + quantity <= (uint32_t)starts[index] + counts[index])
        { region = index; break; }
    taskENTER_CRITICAL(); s_diag.region_requests[region]++; taskEXIT_CRITICAL();
    return region;
}

void bsp_hmi_uart_record(hmi_uart_event_t event)
{
    taskENTER_CRITICAL();
    switch (event)
    {
        case HMI_UART_REQUEST:
            s_diag.complete_frames++; s_stats.requests++; s_stats.last_request_ms = HAL_GetTick(); s_stats.request_seen = 1U; break;
        case HMI_UART_CRC_ERROR: s_stats.crc_errors++; s_diag.dropped_frames++; break;
        case HMI_UART_FRAME_ERROR: s_stats.frame_errors++; s_diag.dropped_frames++; break;
        case HMI_UART_PARTIAL_TIMEOUT: s_stats.partial_timeouts++; s_diag.dropped_frames++; break;
        case HMI_UART_ADDRESS_IGNORE: s_diag.address_ignored++; break;
        default: break;
    }
    taskEXIT_CRITICAL();
}

void bsp_hmi_uart_record_parse(uint32_t cycles)
{ if (cycles > s_diag.parse_max_cycles) s_diag.parse_max_cycles = cycles; }

void bsp_hmi_uart_error_isr(void)
{
    uint32_t error = huart5.ErrorCode;
    s_stats.rx_errors++;
    s_diag.last_error = error; s_diag.error_sr = huart5.Instance->SR;
    if (error & HAL_UART_ERROR_ORE) s_diag.ore++;
    if (error & HAL_UART_ERROR_FE) s_diag.fe++;
    if (error & HAL_UART_ERROR_NE) s_diag.ne++;
    if (error & HAL_UART_ERROR_PE) s_diag.pe++;
    if ((error & ~HAL_UART_ERROR_DMA) != 0U || !rx_armed() ||
        (huart5.hdmarx != NULL && huart5.hdmarx->ErrorCode != HAL_DMA_ERROR_NONE))
        request_rx(HMI_UART_RECOVERY_ERROR);
    if (s_tx_active && (huart5.gState != HAL_UART_STATE_BUSY_TX ||
        (huart5.hdmatx != NULL && huart5.hdmatx->ErrorCode != HAL_DMA_ERROR_NONE)))
        request_tx(HMI_UART_RECOVERY_ERROR);
    notify_isr();
}

void bsp_hmi_uart_rx_abort_isr(void)
{
    s_diag.rx_abort_callbacks++;
    if (s_rx_recovery.pending == HMI_RECOVERY_ABORTING) s_rx_recovery.pending = HMI_RECOVERY_DONE;
    notify_isr();
}

void bsp_hmi_uart_tx_abort_isr(void)
{
    s_diag.tx_abort_callbacks++;
    if (s_tx_recovery.pending == HMI_RECOVERY_ABORTING) s_tx_recovery.pending = HMI_RECOVERY_DONE;
    notify_isr();
}

static uint8_t stream_stopped(DMA_HandleTypeDef *dma)
{ return dma != NULL && dma->Instance != NULL && (dma->Instance->CR & DMA_SxCR_EN) == 0U; }

static void recover_direction(uint8_t transmit, uint32_t now)
{
    volatile hmi_uart_recovery_t *recovery = transmit ? &s_tx_recovery : &s_rx_recovery;
    DMA_HandleTypeDef *dma = transmit ? huart5.hdmatx : huart5.hdmarx;
    HAL_StatusTypeDef result;
    uint8_t ready;
    if (!recovery->pending) return;
    if (recovery->pending == HMI_RECOVERY_REQUESTED && (!recovery->attempted ||
        (uint32_t)(now - recovery->last_attempt) >= HMI_RECOVERY_RETRY_MS))
    {
        taskENTER_CRITICAL();
        recovery->attempted = 1U; recovery->last_attempt = now;
        recovery->abort_epoch = recovery->epoch; recovery->abort_at = now;
        recovery->timed_out = 0U; recovery->pending = HMI_RECOVERY_ABORTING;
        if (transmit) s_diag.tx_attempts++; else s_diag.rx_attempts++;
        taskEXIT_CRITICAL();
        result = transmit ? HAL_UART_AbortTransmit_IT(&huart5) : HAL_UART_AbortReceive_IT(&huart5);
        taskENTER_CRITICAL();
        if (transmit) s_diag.tx_abort_result = result; else s_diag.rx_abort_result = result;
        if (result != HAL_OK)
        {
            recovery->pending = HMI_RECOVERY_REQUESTED;
            if (transmit) s_diag.tx_failures++; else s_diag.rx_failures++;
        }
        taskEXIT_CRITICAL();
    }
    taskENTER_CRITICAL();
    if (recovery->pending == HMI_RECOVERY_ABORTING &&
        (uint32_t)(now - recovery->abort_at) >= HMI_ABORT_TIMEOUT_MS)
    {
        if (!recovery->timed_out)
        {
            recovery->timed_out = 1U;
            if (transmit) s_diag.tx_abort_timeouts++; else s_diag.rx_abort_timeouts++;
        }
        if (stream_stopped(dma))
        {
            void (*callback)(DMA_HandleTypeDef *) = dma->XferAbortCallback;
            dma->Instance->CR &= ~(DMA_IT_TC | DMA_IT_HT | DMA_IT_TE | DMA_IT_DME);
            dma->Instance->FCR &= ~DMA_IT_FE;
            __HAL_DMA_CLEAR_FLAG(dma, __HAL_DMA_GET_TC_FLAG_INDEX(dma) |
                __HAL_DMA_GET_HT_FLAG_INDEX(dma) | __HAL_DMA_GET_TE_FLAG_INDEX(dma) |
                __HAL_DMA_GET_DME_FLAG_INDEX(dma) | __HAL_DMA_GET_FE_FLAG_INDEX(dma));
            HAL_NVIC_ClearPendingIRQ(transmit ? DMA1_Stream7_IRQn : DMA1_Stream0_IRQn);
            dma->State = HAL_DMA_STATE_READY;
            __HAL_UNLOCK(dma);
            dma->XferAbortCallback = NULL;
            if (callback != NULL) callback(dma);
        }
    }
    if (recovery->pending == HMI_RECOVERY_DONE)
    {
        ready = stream_stopped(dma) &&
            (transmit ? huart5.gState == HAL_UART_STATE_READY : huart5.RxState == HAL_UART_STATE_READY);
        if (!ready || recovery->abort_epoch != recovery->epoch)
        {
            recovery->pending = HMI_RECOVERY_REQUESTED;
            if (transmit) s_diag.tx_failures++; else s_diag.rx_failures++;
        }
        else if (transmit)
        { s_tx_active = 0U; recovery->pending = 0U; s_diag.tx_success++; }
        else
        {
            s_rx_tail = s_rx_head; s_event_tail = s_event_head;
            s_quiet_head = s_rx_head; s_rx_generation++;
if (arm_rx()) { recovery->pending = 0U; s_diag.rx_success++; start_quiet_timer(); }
            else { recovery->pending = HMI_RECOVERY_REQUESTED; s_diag.rx_failures++; }
        }
    }
    taskEXIT_CRITICAL();
}

void bsp_hmi_uart_service(void)
{
    uint32_t now = HAL_GetTick(), elapsed = now - s_service_tick;
    taskENTER_CRITICAL();
    if (elapsed > s_diag.service_max_ms) s_diag.service_max_ms = elapsed;
    s_service_tick = now;
    if (!s_rx_recovery.pending && !rx_armed()) request_rx(HMI_UART_RECOVERY_STATE);
    if (s_tx_active)
    {
        elapsed = now - s_tx_started;
        if (elapsed > s_diag.tx_max_ms) s_diag.tx_max_ms = elapsed;
        if (!s_tx_recovery.pending && elapsed >= s_tx_limit)
        { s_diag.tx_timeouts++; request_tx(HMI_UART_RECOVERY_TIMEOUT); }
    }
    taskEXIT_CRITICAL();
    recover_direction(1U, now); recover_direction(0U, now);
}

void bsp_hmi_uart_get_stats(hmi_uart_stats_t *stats)
{
    if (stats == NULL) return;
    taskENTER_CRITICAL(); *stats = s_stats; stats->tx_active = s_tx_active; taskEXIT_CRITICAL();
}

void bsp_hmi_uart_get_completion(uint32_t *count, uint32_t *at_ms, uint32_t *cycles,
    uint32_t *region_length)
{
    taskENTER_CRITICAL();
    *count = s_stats.responses + s_stats.exceptions;
    *at_ms = s_diag.tx_last_ms;
    *cycles = s_diag.tx_complete_cycles;
    *region_length = s_diag_completed_info;
    taskEXIT_CRITICAL();
}

void bsp_hmi_uart_get_diag(hmi_uart_diag_t *diag)
{
    if (diag == NULL) return;
    taskENTER_CRITICAL();
    *diag = s_diag;
    diag->queue_depth = (uint16_t)(s_rx_head - s_rx_tail);
    diag->event_depth = (uint16_t)((s_event_head + APP_HMI_RX_EVENT_COUNT - s_event_tail) % APP_HMI_RX_EVENT_COUNT);
    diag->gstate = huart5.gState; diag->rxstate = huart5.RxState; diag->error_code = huart5.ErrorCode;
    diag->rx_count = huart5.RxXferCount; diag->tx_count = huart5.TxXferCount;
    diag->cr1 = huart5.Instance->CR1; diag->cr3 = huart5.Instance->CR3;
    diag->rx_pending = s_rx_recovery.pending; diag->tx_pending = s_tx_recovery.pending;
    diag->rx_epoch = s_rx_recovery.epoch; diag->tx_epoch = s_tx_recovery.epoch;
    diag->rx_reason = s_rx_recovery.reason; diag->tx_reason = s_tx_recovery.reason; diag->rx_armed = rx_armed();
    if (huart5.hdmarx != NULL)
    {
        diag->rx_ndtr = __HAL_DMA_GET_COUNTER(huart5.hdmarx);
        diag->rx_stream = huart5.hdmarx->Instance->CR; diag->dma_rx_error = huart5.hdmarx->ErrorCode;
    }
    if (huart5.hdmatx != NULL)
    {
        diag->tx_ndtr = __HAL_DMA_GET_COUNTER(huart5.hdmatx);
        diag->tx_stream = huart5.hdmatx->Instance->CR; diag->dma_tx_error = huart5.hdmatx->ErrorCode;
    }
    taskEXIT_CRITICAL();
}
