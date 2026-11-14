#include "bsp_rs485.h"

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "app_config.h"
#include "usart.h"

#define RS485_RECOVERY_RETRY_MS (20U)

static uint8_t s_dma_rx_buffer[APP_RS485_DMA_RX_SIZE];
static uint8_t s_rx_ring[APP_RS485_RING_SIZE];
static volatile uint16_t s_dma_last_position;
static volatile uint16_t s_ring_head;
static volatile uint16_t s_ring_tail;
static volatile uint32_t s_last_rx_tick;
static volatile uint8_t s_initialized;
static volatile uint8_t s_rx_recovery_pending;
static SemaphoreHandle_t s_tx_mutex;
static SemaphoreHandle_t s_tx_complete;
static rs485_stats_t s_stats;
static TaskHandle_t s_rx_task;
static volatile uint8_t s_tx_inhibited;
static volatile uint8_t s_dma_running;
static TickType_t s_recovery_tick;
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
static volatile rs485_diag_times_t s_wire_time;
void rs485_diag_begin(void)
{
    taskENTER_CRITICAL();
    memset((void *)&s_wire_time, 0, sizeof(s_wire_time));
    taskEXIT_CRITICAL();
}
void rs485_diag_times(rs485_diag_times_t *out)
{
    taskENTER_CRITICAL(); *out = s_wire_time; taskEXIT_CRITICAL();
}
#if defined(__CC_ARM) || defined(__arm__)
#define RS485_DIAG_MS(t) (HAL_GetTick())
#else
#define RS485_DIAG_MS(t) ((uint32_t)((uint64_t)(t) * 1000U / configTICK_RATE_HZ))
#endif
#endif

void rs485_inhibit_transmit(uint8_t inhibit)
{
    s_tx_inhibited = inhibit;
}

static void rs485_copy_dma_region(uint16_t start, uint16_t end)
{
    uint16_t position;
    uint16_t next;

    position = start;
    while (position != end)
    {
        next = (uint16_t)((s_ring_head + 1U) % APP_RS485_RING_SIZE);
        if (next == s_ring_tail)
        {
            s_ring_tail = (uint16_t)((s_ring_tail + 1U) % APP_RS485_RING_SIZE);
            s_stats.rx_overflows++;
        }
        s_rx_ring[s_ring_head] = s_dma_rx_buffer[position];
        s_ring_head = next;
        s_stats.rx_bytes++;
        position = (uint16_t)((position + 1U) % APP_RS485_DMA_RX_SIZE);
    }
}


static uint16_t rs485_get_dma_position(void)
{
    uint16_t position;

    if ((huart2.hdmarx == NULL) || (huart2.hdmarx->Instance == NULL))
    {
        return 0U;
    }
    position = (uint16_t)(APP_RS485_DMA_RX_SIZE -
                          __HAL_DMA_GET_COUNTER(huart2.hdmarx));
    if (position >= APP_RS485_DMA_RX_SIZE)
    {
        position = 0U;
    }
    return position;
}

/* 统一启动USART2接收DMA，初始化和异常恢复都必须经过这里。 */
static HAL_StatusTypeDef rs485_start_rx_dma(void)
{
    HAL_StatusTypeDef status;
    uint32_t interrupt_mask;

    /* 此函数也在调度器启动前调用，保存并恢复原中断屏蔽状态。 */
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    s_dma_running = 0U;
    __HAL_UART_DISABLE_IT(&huart2, UART_IT_IDLE);
    s_dma_last_position = 0U;
    s_rx_recovery_pending = 0U;
    /* 读SR再读DR同时清除残留IDLE、ORE、FE、NE和PE。 */
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);
    status = HAL_ERROR;
    if ((huart2.hdmarx != NULL) &&
        (huart2.hdmarx->Instance == DMA1_Stream5) &&
        (huart2.hdmarx->Parent == &huart2) &&
        (huart2.hdmarx->Init.Mode == DMA_CIRCULAR) &&
        (huart2.hdmarx->Init.Channel == DMA_CHANNEL_4))
    {
        status = HAL_UART_Receive_DMA(&huart2, s_dma_rx_buffer,
                                      APP_RS485_DMA_RX_SIZE);
    }
    if (status != HAL_OK)
    {
        s_stats.dma_start_failures++;
        s_rx_recovery_pending = 1U;
    }
    else
    {
        s_dma_running = 1U;
        __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
    }
    __set_PRIMASK(interrupt_mask);
    return status;
}

static void rs485_capture_dma_isr(void)
{
    uint16_t position;
    BaseType_t higher_priority_task_woken;

    if ((s_dma_running == 0U) || (huart2.hdmarx == NULL))
    {
        return;
    }
    position = rs485_get_dma_position();
    /* USART2与RX DMA使用相同抢占优先级，三个入口不会嵌套搬运。 */
    if (position != s_dma_last_position)
    {
        rs485_copy_dma_region(s_dma_last_position, position);
        s_dma_last_position = position;
        s_last_rx_tick = xTaskGetTickCountFromISR();
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
        s_wire_time.rx_ms = RS485_DIAG_MS(s_last_rx_tick);
        s_wire_time.valid |= 4U;
#endif
        higher_priority_task_woken = pdFALSE;
        if (s_rx_task != NULL)
        {
            vTaskNotifyGiveFromISR(s_rx_task, &higher_priority_task_woken);
        }
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}
/*
 * 初始化RS485通信底层：清空统计和缓冲状态，创建发送同步对象，
 * 开启USART2空闲线中断并启动Circular DMA接收。
 */
rs485_status_t rs485_init(void)
{
    HAL_StatusTypeDef hal_status;

    /* 已创建的同步对象及正在运行的DMA不能被重复初始化覆盖。 */
    if (s_initialized != 0U)
    {
        return (s_dma_running && !s_rx_recovery_pending) ?
               RS485_OK : RS485_HAL_ERROR;
    }
    s_initialized = 0U;
    s_tx_inhibited = 0U;
    s_dma_running = 0U;
    s_rx_task = NULL;
    s_tx_mutex = NULL;
    s_tx_complete = NULL;
    memset((void *)&s_stats, 0, sizeof(s_stats));
    s_dma_last_position = 0U;
    s_ring_head = 0U;
    s_ring_tail = 0U;
    s_last_rx_tick = 0U;
    s_rx_recovery_pending = 0U;
    s_recovery_tick = xTaskGetTickCount() - pdMS_TO_TICKS(RS485_RECOVERY_RETRY_MS);

    /* 发送互斥锁保证总线上同一时刻只有一个发送事务。 */
    s_tx_mutex = xSemaphoreCreateMutex();
    /* 二值信号量由TX DMA完成回调释放。 */
    s_tx_complete = xSemaphoreCreateBinary();
    if ((s_tx_mutex == NULL) || (s_tx_complete == NULL))
    {
        if (s_tx_mutex != NULL)
        {
            vSemaphoreDelete(s_tx_mutex);
            s_tx_mutex = NULL;
        }
        if (s_tx_complete != NULL)
        {
            vSemaphoreDelete(s_tx_complete);
            s_tx_complete = NULL;
        }
        return RS485_HAL_ERROR;
    }

    s_initialized = 1U;
    hal_status = rs485_start_rx_dma();
    if (hal_status != HAL_OK)
    {
        /* 保留同步对象，由通信任务重试；调用者记录初始化诊断故障。 */
        return RS485_HAL_ERROR;
    }

    return RS485_OK;
}

rs485_status_t rs485_send(const uint8_t *data, uint16_t length)
{
    TickType_t timeout;
    HAL_StatusTypeDef hal_status;
    rs485_status_t result;

    if ((data == NULL) || (length == 0U) || (length > APP_MODBUS_MAX_FRAME_SIZE))
    {
        return RS485_INVALID_ARGUMENT;
    }
    if (s_initialized == 0U)
    {
        return RS485_NOT_INITIALIZED;
    }
    if ((s_dma_running == 0U) || (s_rx_recovery_pending != 0U))
    {
        return RS485_HAL_ERROR;
    }
    timeout = pdMS_TO_TICKS(APP_MODBUS_RESPONSE_TIMEOUT_MS);
    if (xSemaphoreTake(s_tx_mutex, timeout) != pdTRUE)
    {
        return RS485_BUSY;
    }
    while (xSemaphoreTake(s_tx_complete, 0U) == pdTRUE)
    {
    }
    result = RS485_OK;
    taskENTER_CRITICAL();
    if (s_tx_inhibited)
    {
        taskEXIT_CRITICAL();
        xSemaphoreGive(s_tx_mutex);
        return RS485_BUSY;
    }
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    s_wire_time.start_ms = RS485_DIAG_MS(xTaskGetTickCount());
#endif
    hal_status = HAL_UART_Transmit_DMA(&huart2, (uint8_t *)data, length);
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    if (hal_status == HAL_OK) s_wire_time.valid |= 1U;
#endif
    taskEXIT_CRITICAL();
    if (hal_status != HAL_OK)
    {
        result = RS485_HAL_ERROR;
    }
    else if (xSemaphoreTake(s_tx_complete, timeout) != pdTRUE)
    {
        s_stats.tx_timeouts++;
        /* 返回前终止TX对调用者缓冲区的引用，保留Circular RX。 */
        (void)HAL_UART_AbortTransmit(&huart2);
        result = RS485_TIMEOUT;
    }
    else
    {
        s_stats.tx_bytes += length;
    }
    xSemaphoreGive(s_tx_mutex);
    return result;
}

uint16_t rs485_read(uint8_t *data, uint16_t capacity)
{
    uint16_t count;

    if ((data == NULL) || (capacity == 0U))
    {
        return 0U;
    }
    taskENTER_CRITICAL();
    count = 0U;
    while ((count < capacity) && (s_ring_tail != s_ring_head))
    {
        data[count] = s_rx_ring[s_ring_tail];
        s_ring_tail = (uint16_t)((s_ring_tail + 1U) % APP_RS485_RING_SIZE);
        count++;
    }
    taskEXIT_CRITICAL();
    return count;
}

uint16_t rs485_available(void)
{
    uint16_t head;
    uint16_t tail;

    taskENTER_CRITICAL();
    head = s_ring_head;
    tail = s_ring_tail;
    taskEXIT_CRITICAL();
    if (head >= tail)
    {
        return (uint16_t)(head - tail);
    }
    return (uint16_t)(APP_RS485_RING_SIZE - tail + head);
}

uint32_t rs485_last_rx_tick(void)
{
    return s_last_rx_tick;
}

void rs485_bind_rx_task(TaskHandle_t task)
{
    s_rx_task = task;
}

void rs485_service(void)
{
    HAL_StatusTypeDef status;
    TickType_t now;

    if ((s_initialized == 0U) || (s_rx_recovery_pending == 0U))
    {
        return;
    }
    now = xTaskGetTickCount();
    if ((now - s_recovery_tick) < pdMS_TO_TICKS(RS485_RECOVERY_RETRY_MS))
    {
        return;
    }
    s_recovery_tick = now;
    /* 仅在任务上下文中恢复RX，失败后限频重试，不影响TX完成信号。 */
    s_dma_running = 0U;
    __HAL_UART_DISABLE_IT(&huart2, UART_IT_IDLE);
    status = HAL_UART_AbortReceive(&huart2);
    if (status != HAL_OK)
    {
        s_stats.rx_abort_failures++;
        return;
    }
    /* HAL错误路径可能已清DMAR，但DMA流仍需独立终止。 */
    if ((huart2.hdmarx != NULL) &&
        (huart2.hdmarx->Instance != NULL) &&
        ((huart2.hdmarx->Instance->CR & DMA_SxCR_EN) != 0U))
    {
        if (HAL_DMA_Abort(huart2.hdmarx) != HAL_OK)
        {
            s_stats.rx_abort_failures++;
            return;
        }
    }
    taskENTER_CRITICAL();
    s_dma_last_position = 0U;
    s_ring_head = 0U;
    s_ring_tail = 0U;
    taskEXIT_CRITICAL();
    /* 终止超时后HAL可能保留TIMEOUT状态，重建RX流状态后才能再次启动。 */
    if ((huart2.hdmarx == NULL) ||
        (HAL_DMA_Init(huart2.hdmarx) != HAL_OK))
    {
        s_stats.dma_start_failures++;
        return;
    }
    status = rs485_start_rx_dma();
    if (status == HAL_OK)
    {
        s_stats.rx_restarts++;
    }
}

void rs485_get_stats(rs485_stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }
    taskENTER_CRITICAL();
    *stats = s_stats;
    stats->dma_position = rs485_get_dma_position();
    stats->dma_last_position = s_dma_last_position;
    stats->uart_sr = huart2.Instance->SR;
    stats->uart_cr1 = huart2.Instance->CR1;
    stats->uart_cr3 = huart2.Instance->CR3;
    stats->rx_state = (uint32_t)huart2.RxState;
    stats->recovery_pending = s_rx_recovery_pending;
    stats->dma_control = 0U;
    stats->dma_remaining = 0U;
    if ((huart2.hdmarx != NULL) && (huart2.hdmarx->Instance != NULL))
    {
        stats->dma_control = huart2.hdmarx->Instance->CR;
        stats->dma_remaining = (uint16_t)__HAL_DMA_GET_COUNTER(huart2.hdmarx);
    }
    /* 运行状态同时检查软件标志、Circular DMA使能和UART接收DMA请求。 */
    stats->dma_running = (uint8_t)(s_dma_running &&
        ((stats->dma_control & (DMA_SxCR_EN | DMA_SxCR_CIRC)) ==
         (DMA_SxCR_EN | DMA_SxCR_CIRC)) &&
        ((stats->uart_cr3 & USART_CR3_DMAR) != 0U));
    stats->pa3_mode = (uint8_t)((GPIOA->MODER >> 6U) & 3U);
    stats->pa3_af = (uint8_t)((GPIOA->AFR[0] >> 12U) & 15U);
    taskEXIT_CRITICAL();
}

void rs485_uart_irq_entry_isr(void)
{
    s_stats.irq_entries++;
}

void rs485_uart_idle_isr(void)
{
    /* IDLE标志由USART2_IRQHandler统一判断和清除，这里只搬运新增数据。 */
    s_stats.idle_interrupts++;
    rs485_capture_dma_isr();
}

void rs485_uart_rx_half_isr(void)
{
    s_stats.dma_half_events++;
    rs485_capture_dma_isr();
}

void rs485_uart_rx_complete_isr(void)
{
    s_stats.dma_full_events++;
    rs485_capture_dma_isr();
}

void rs485_uart_tx_complete_isr(void)
{
    BaseType_t higher_priority_task_woken;

    higher_priority_task_woken = pdFALSE;
#if APP_LINK_DIAG_ACTIVE && APP_SERVO_RS485_DIAG_ENABLED
    s_wire_time.tc_ms = RS485_DIAG_MS(xTaskGetTickCountFromISR());
    s_wire_time.valid |= 2U;
#endif
    if (s_tx_complete != NULL)
    {
        xSemaphoreGiveFromISR(s_tx_complete, &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void rs485_uart_error_isr(void)
{
    BaseType_t higher_priority_task_woken;
    uint32_t error_flags;

    error_flags = READ_REG(huart2.Instance->SR) &
                  (UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE);
    /* HAL回调前可能已清SR，补录句柄中的错误，按SR位定义统一输出。 */
    if ((huart2.ErrorCode & HAL_UART_ERROR_ORE) != 0U) error_flags |= UART_FLAG_ORE;
    if ((huart2.ErrorCode & HAL_UART_ERROR_FE) != 0U) error_flags |= UART_FLAG_FE;
    if ((huart2.ErrorCode & HAL_UART_ERROR_NE) != 0U) error_flags |= UART_FLAG_NE;
    if ((huart2.ErrorCode & HAL_UART_ERROR_PE) != 0U) error_flags |= UART_FLAG_PE;
    s_stats.uart_errors++;
    s_stats.uart_error_flags |= error_flags;
    s_stats.hal_error_flags |= huart2.ErrorCode;
    s_dma_running = 0U;
    /* 只屏蔽接收侧事件，避免错误风暴；阻塞式终止留给通信任务。 */
    __HAL_UART_DISABLE_IT(&huart2, UART_IT_IDLE);
    __HAL_UART_DISABLE_IT(&huart2, UART_IT_PE);
    __HAL_UART_DISABLE_IT(&huart2, UART_IT_ERR);
    s_rx_recovery_pending = 1U;
    higher_priority_task_woken = pdFALSE;
    if (s_rx_task != NULL)
    {
        vTaskNotifyGiveFromISR(s_rx_task, &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}
