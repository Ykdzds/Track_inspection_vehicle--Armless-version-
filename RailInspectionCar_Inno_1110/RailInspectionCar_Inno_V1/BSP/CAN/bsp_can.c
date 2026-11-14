#include "bsp_can.h"
#include "can.h"
#include "can_protocol.h"
#include <string.h>

#define CAN_INIT_TIMEOUT_MS 100U
#define CAN_RX_ISR_BUDGET 3U

typedef struct
{
    bsp_can_tx_result_t result;
    uint32_t deadline_ms;
    uint32_t abort_started_ms;
    bool pending;
    bool completed;
    bool expired;
} can_mailbox_t;

static bsp_can_frame_t rx_queue[BSP_CAN_RX_CAPACITY];
static bsp_can_frame_t priority_frames[4];
static uint8_t priority_read, priority_write, priority_count;
static can_mailbox_t mailboxes[3];
static bsp_can_stats_t stats;
static TaskHandle_t can_task;
static uint16_t rx_read, rx_write, rx_count;
static volatile bool bus_off_pending;
static uint32_t phase_started_ms;
static uint32_t backoff_ms;
static uint8_t recovery_level;

static bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void notify_task(void)
{
    BaseType_t woken = pdFALSE;
    if (can_task != NULL)
    {
        vTaskNotifyGiveFromISR(can_task, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

static void irq_enable(bool enabled)
{
    if (enabled)
    {
        HAL_NVIC_EnableIRQ(CAN1_TX_IRQn);
        HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
        HAL_NVIC_EnableIRQ(CAN1_RX1_IRQn);
        HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);
    }
    else
    {
        HAL_NVIC_DisableIRQ(CAN1_TX_IRQn);
        HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
        HAL_NVIC_DisableIRQ(CAN1_RX1_IRQn);
        HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);
    }
}

static void configure_filters(void)
{
    static const uint16_t ids[12] =
    {
        0x080U, 0x080U, 0x100U, 0x102U, 0x104U, 0x105U,
        0x110U, 0x111U, 0x124U, 0x310U, 0x311U, 0x312U
    };
    uint32_t i;
    /* 32位列表匹配标准数据帧；停车帧单独进入FIFO1。 */
    CAN1->FMR |= CAN_FMR_FINIT;
    /* CAN2未使用，限定CAN1拥有前14组，并停用旧的额外过滤器。 */
    CAN1->FMR = (CAN1->FMR & ~CAN_FMR_CAN2SB) |
                (14UL << 8U) | CAN_FMR_FINIT;
    CAN1->FA1R &= ~0x3FFFUL;
    CAN1->FS1R |= 0x3FUL;
    CAN1->FM1R |= 0x3FUL;
    CAN1->FFA1R = (CAN1->FFA1R & ~0x3FUL) | 1UL;
    for (i = 0U; i < 6U; ++i)
    {
        CAN1->sFilterRegister[i].FR1 = (uint32_t)ids[i * 2U] << 21U;
        CAN1->sFilterRegister[i].FR2 = (uint32_t)ids[i * 2U + 1U] << 21U;
    }
    CAN1->FA1R |= 0x3FUL;
    CAN1->FMR &= ~CAN_FMR_FINIT;
}

static void clear_receive_fifos(void)
{
    uint32_t fifo, i;
    for (fifo = 0U; fifo < 2U; ++fifo)
    {
        volatile uint32_t *rfr = fifo == 0U ? &CAN1->RF0R : &CAN1->RF1R;
        /* 每个硬件FIFO至多3帧，不等待总线或新的数据。 */
        for (i = 0U; i < 3U && (*rfr & CAN_RF0R_FMP0) != 0U; ++i)
            *rfr = CAN_RF0R_RFOM0;
        *rfr = CAN_RF0R_FULL0 | CAN_RF0R_FOVR0;
    }
}

static void enter_backoff(uint32_t now_ms)
{
    static const uint32_t delays[4] = {100U, 200U, 500U, 1000U};
    uint32_t i;
    irq_enable(false);
    CAN1->IER = 0U;
    CAN1->TSR = CAN_TSR_ABRQ0 | CAN_TSR_ABRQ1 | CAN_TSR_ABRQ2;
    CAN1->MCR |= CAN_MCR_INRQ;
    taskENTER_CRITICAL();
    for (i = 0U; i < 3U; ++i)
    {
        if (mailboxes[i].pending)
        {
            mailboxes[i].pending = false;
            mailboxes[i].completed = true;
            mailboxes[i].result.success = false;
            ++stats.tx_failed;
        }
    }
    /* 不跨恢复代次保留控制帧，查询结果仍由任务消费。 */
    rx_read = rx_write = rx_count = 0U;
    priority_read = priority_write = priority_count = 0U;
    stats.rx_queued = 0U;
    stats.phase = BSP_CAN_BACKOFF;
    bus_off_pending = false;
    ++stats.generation;
    taskEXIT_CRITICAL();
    phase_started_ms = now_ms;
    backoff_ms = delays[recovery_level];
    if (recovery_level < 3U) ++recovery_level;
}

void bsp_can_init(TaskHandle_t task, uint32_t now_ms)
{
    irq_enable(false);
    CAN1->IER = 0U;
    memset(&stats, 0, sizeof(stats));
    memset(mailboxes, 0, sizeof(mailboxes));
    can_task = task;
    rx_read = rx_write = rx_count = 0U;
    priority_read = priority_write = priority_count = 0U;
    bus_off_pending = false;
    recovery_level = 0U;
    stats.generation = 1U;
    phase_started_ms = now_ms;
    /* 每轮仅请求或核验一个硬件阶段，不等待INAK变化。 */
    CAN1->MCR &= ~CAN_MCR_SLEEP;
    CAN1->MCR |= CAN_MCR_INRQ;
    stats.phase = BSP_CAN_WAIT_INIT;
}

void bsp_can_service(uint32_t now_ms)
{
    uint32_t i;
    if (bus_off_pending)
    {
        enter_backoff(now_ms);
        return;
    }
    switch (stats.phase)
    {
        case BSP_CAN_WAIT_INIT:
            if ((CAN1->MSR & CAN_MSR_INAK) != 0U)
            {
                configure_filters();
                clear_receive_fifos();
                CAN1->TSR = CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2;
                CAN1->MSR = CAN_MSR_ERRI;
                CAN1->MCR &= ~(CAN_MCR_SLEEP | CAN_MCR_ABOM |
                               CAN_MCR_AWUM | CAN_MCR_TXFP | CAN_MCR_NART);
                CAN1->MCR |= CAN_MCR_RFLM;
                CAN1->MCR &= ~CAN_MCR_INRQ;
                phase_started_ms = now_ms;
                stats.phase = BSP_CAN_WAIT_READY;
            }
            else if ((uint32_t)(now_ms - phase_started_ms) >= CAN_INIT_TIMEOUT_MS)
            {
                ++stats.init_timeouts;
                enter_backoff(now_ms);
            }
            break;
        case BSP_CAN_WAIT_READY:
            if ((CAN1->MSR & CAN_MSR_INAK) == 0U)
            {
                CAN1->IER = CAN_IER_TMEIE | CAN_IER_FMPIE0 |
                    CAN_IER_FMPIE1 | CAN_IER_FOVIE0 | CAN_IER_FOVIE1 |
                    CAN_IER_ERRIE | CAN_IER_BOFIE;
                stats.phase = BSP_CAN_READY;
                phase_started_ms = now_ms;
                ++stats.recovery_count;
                irq_enable(true);
            }
            else if ((uint32_t)(now_ms - phase_started_ms) >= CAN_INIT_TIMEOUT_MS)
            {
                ++stats.init_timeouts;
                enter_backoff(now_ms);
            }
            break;
        case BSP_CAN_BACKOFF:
            if ((uint32_t)(now_ms - phase_started_ms) >= backoff_ms)
            {
                CAN1->MCR &= ~CAN_MCR_SLEEP;
                CAN1->MCR |= CAN_MCR_INRQ;
                phase_started_ms = now_ms;
                stats.phase = BSP_CAN_WAIT_INIT;
            }
            break;
        case BSP_CAN_READY:
            /* 取消请求未产生完成中断时，不无限占用邮箱和记录通道。 */
            for (i = 0U; i < 3U; ++i)
            {
                if (mailboxes[i].pending && mailboxes[i].expired &&
                    (int32_t)(now_ms - mailboxes[i].abort_started_ms) >= 20)
                {
                    enter_backoff(now_ms);
                    return;
                }
            }
            taskENTER_CRITICAL();
            for (i = 0U; i < 3U; ++i)
            {
                if (mailboxes[i].pending && !mailboxes[i].expired &&
                    deadline_reached(now_ms, mailboxes[i].deadline_ms))
                {
                    mailboxes[i].expired = true;
                    mailboxes[i].abort_started_ms = now_ms;
                    ++stats.tx_expired;
                    CAN1->TSR = CAN_TSR_ABRQ0 << (8U * i);
                }
            }
            taskEXIT_CRITICAL();
            /* 连续稳定运行后恢复最短退避，避免故障边缘快速重启。 */
            if ((uint32_t)(now_ms - phase_started_ms) >= 1000U)
                recovery_level = 0U;
            break;
        default:
            break;
    }
}

bool bsp_can_ready(void)
{
    return stats.phase == BSP_CAN_READY && !bus_off_pending;
}

bool bsp_can_receive(bsp_can_frame_t *frame)
{
    bool found = false;
    if (frame == NULL) return false;
    taskENTER_CRITICAL();
    if (priority_count)
    {
        *frame = priority_frames[priority_read];
        priority_read = (uint8_t)((priority_read + 1U) % 4U);
        --priority_count;
        found = true;
    }
    else if (rx_count != 0U)
    {
        *frame = rx_queue[rx_read];
        rx_read = (uint16_t)((rx_read + 1U) % BSP_CAN_RX_CAPACITY);
        --rx_count;
        stats.rx_queued = rx_count;
        found = true;
    }
    taskEXIT_CRITICAL();
    return found;
}

bool bsp_can_send(const bsp_can_frame_t *frame, uint32_t cookie,
                  uint32_t deadline_ms)
{
    uint32_t i, low = 0U, high = 0U;
    bool submitted = false;
    if (frame == NULL || frame->id > 0x7FFU || frame->dlc != 8U ||
        deadline_reached(HAL_GetTick(), deadline_ms)) return false;
    memcpy(&low, frame->data, 4U);
    memcpy(&high, frame->data + 4U, 4U);
    taskENTER_CRITICAL();
    if (bsp_can_ready())
    {
        for (i = 0U; i < 3U; ++i)
        {
            if (!mailboxes[i].pending && !mailboxes[i].completed &&
                (CAN1->TSR & (CAN_TSR_TME0 << i)) != 0U)
            {
                mailboxes[i].result.frame = *frame;
                mailboxes[i].result.cookie = cookie;
                mailboxes[i].result.success = false;
                mailboxes[i].deadline_ms = deadline_ms;
                mailboxes[i].expired = false;
                mailboxes[i].pending = true;
                CAN1->sTxMailBox[i].TIR = (uint32_t)frame->id << 21U;
                CAN1->sTxMailBox[i].TDTR = 8U;
                CAN1->sTxMailBox[i].TDLR = low;
                CAN1->sTxMailBox[i].TDHR = high;
                CAN1->sTxMailBox[i].TIR |= CAN_TI0R_TXRQ;
                ++stats.tx_submitted;
                submitted = true;
                break;
            }
        }
    }
    taskEXIT_CRITICAL();
    return submitted;
}

bool bsp_can_take_tx_result(bsp_can_tx_result_t *result)
{
    uint32_t i;
    bool found = false;
    if (result == NULL) return false;
    taskENTER_CRITICAL();
    for (i = 0U; i < 3U; ++i)
    {
        if (mailboxes[i].completed)
        {
            *result = mailboxes[i].result;
            mailboxes[i].completed = false;
            found = true;
            break;
        }
    }
    taskEXIT_CRITICAL();
    return found;
}

void bsp_can_cancel(uint32_t cookie)
{
    uint32_t i;
    taskENTER_CRITICAL();
    for (i = 0U; i < 3U; ++i)
    {
        if (mailboxes[i].pending && mailboxes[i].result.cookie == cookie)
        {
            if (!mailboxes[i].expired)
                mailboxes[i].abort_started_ms = HAL_GetTick();
            mailboxes[i].expired = true;
            CAN1->TSR = CAN_TSR_ABRQ0 << (8U * i);
        }
    }
    taskEXIT_CRITICAL();
}

void bsp_can_get_stats(bsp_can_stats_t *out)
{
    if (out == NULL) return;
    taskENTER_CRITICAL();
    *out = stats;
    taskEXIT_CRITICAL();
}

void bsp_can_tx_isr(void)
{
    uint32_t i, tsr = CAN1->TSR;
    for (i = 0U; i < 3U; ++i)
    {
        uint32_t complete = CAN_TSR_RQCP0 << (8U * i);
        if ((tsr & complete) != 0U)
        {
            if (mailboxes[i].pending)
            {
                bool success = (tsr & (CAN_TSR_TXOK0 << (8U * i))) != 0U;
                mailboxes[i].result.success = success;
                mailboxes[i].result.frame.timestamp_ms = HAL_GetTick();
                mailboxes[i].pending = false;
                mailboxes[i].completed = true;
                if (mailboxes[i].result.success) ++stats.tx_success;
                else ++stats.tx_failed;
            }
            CAN1->TSR = complete;
        }
    }
    notify_task();
}

void bsp_can_rx_isr(uint32_t fifo)
{
    bsp_can_frame_t frame;
    uint32_t i;
    volatile uint32_t *rfr;
    if (fifo > 1U) return;
    rfr = fifo == 0U ? &CAN1->RF0R : &CAN1->RF1R;
    if ((*rfr & CAN_RF0R_FOVR0) != 0U)
    {
        ++stats.rx_fifo_overrun;
        *rfr = CAN_RF0R_FOVR0;
    }
    for (i = 0U; i < CAN_RX_ISR_BUDGET &&
         (*rfr & CAN_RF0R_FMP0) != 0U; ++i)
    {
        uint32_t rir = CAN1->sFIFOMailBox[fifo].RIR;
        uint32_t low = CAN1->sFIFOMailBox[fifo].RDLR;
        uint32_t high = CAN1->sFIFOMailBox[fifo].RDHR;
        frame.timestamp_ms = HAL_GetTick();
        frame.id = (uint16_t)(rir >> 21U);
        frame.dlc = (uint8_t)(CAN1->sFIFOMailBox[fifo].RDTR & 0xFU);
        memcpy(frame.data, &low, 4U);
        memcpy(frame.data + 4U, &high, 4U);
        *rfr = CAN_RF0R_RFOM0;
        if ((rir & (CAN_RI0R_IDE | CAN_RI0R_RTR)) != 0U ||
            frame.dlc != 8U || !can_protocol_upper_id(frame.id))
        {
            ++stats.rx_invalid;
            continue;
        }
        ++stats.rx_count;
        if (frame.id == 0x080U)
        {
            if (priority_count == 4U) { ++stats.priority_replaced; ++stats.rx_dropped; }
            else
            {
                priority_frames[priority_write] = frame;
                priority_write = (uint8_t)((priority_write + 1U) % 4U);
                ++priority_count;
            }
        }
        else if (rx_count == BSP_CAN_RX_CAPACITY)
        {
            ++stats.rx_dropped;
        }
        else
        {
            rx_queue[rx_write] = frame;
            rx_write = (uint16_t)((rx_write + 1U) % BSP_CAN_RX_CAPACITY);
            ++rx_count;
            stats.rx_queued = rx_count;
            if (rx_count > stats.rx_high_water) stats.rx_high_water = rx_count;
        }
    }
    notify_task();
}

void bsp_can_error_isr(void)
{
    stats.last_esr = CAN1->ESR;
    if ((stats.last_esr & CAN_ESR_BOFF) != 0U && !bus_off_pending)
    {
        bus_off_pending = true;
        ++stats.bus_off_count;
    }
    CAN1->MSR = CAN_MSR_ERRI;
    notify_task();
}
