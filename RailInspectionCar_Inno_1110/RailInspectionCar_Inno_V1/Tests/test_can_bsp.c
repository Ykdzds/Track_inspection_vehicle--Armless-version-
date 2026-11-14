#include "can.h"
#include "bsp_can.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 使用真实CMSIS寄存器布局和位定义，硬件W1C行为由测试边界显式推进。 */
CAN_TypeDef test_can_registers;
static uint32_t tick, notifications, enabled;
uint32_t HAL_GetTick(void) { return tick; }
void HAL_NVIC_EnableIRQ(IRQn_Type irq) { (void)irq; ++enabled; }
void HAL_NVIC_DisableIRQ(IRQn_Type irq) { (void)irq; }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken)
{ assert(task != NULL); *woken = pdFALSE; ++notifications; }

static void start(uint32_t now)
{
    memset(&test_can_registers, 0, sizeof(test_can_registers));
    tick = now;
    bsp_can_init((TaskHandle_t)1, now);
    assert(!bsp_can_ready());
    CAN1->MSR = CAN_MSR_INAK;
    bsp_can_service(now + 1U);
    assert((CAN1->MCR & CAN_MCR_INRQ) == 0U);
    assert((CAN1->MCR & CAN_MCR_NART) == 0U);
    assert(CAN1->sFilterRegister[0].FR1 == (0x080UL << 21U));
    assert((CAN1->FFA1R & 0x3FU) == 1U);
    CAN1->MSR = 0U;
    CAN1->TSR = CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2;
    bsp_can_service(now + 2U);
    assert(bsp_can_ready());
}
static void receive(uint16_t id, uint8_t dlc, uint32_t flags)
{
    unsigned fifo = id == 0x080U ? 1U : 0U;
    CAN1->sFIFOMailBox[fifo].RIR = ((uint32_t)id << 21U) | flags;
    CAN1->sFIFOMailBox[fifo].RDTR = dlc;
    CAN1->sFIFOMailBox[fifo].RDLR = 0x04030201U;
    CAN1->sFIFOMailBox[fifo].RDHR = 0x08070605U;
    if (fifo) CAN1->RF1R = 1U;
    else CAN1->RF0R = 1U;
    bsp_can_rx_isr(fifo);
    /* 普通RAM没有硬件FIFO释放副作用，下一次注入前复位计数字段。 */
    CAN1->RF0R = CAN1->RF1R = 0U;
}
static void test_rx(void)
{
    bsp_can_frame_t frame;
    bsp_can_stats_t stats;
    unsigned i;
    start(1000U);
    for (i = 0U; i < 65U; ++i) receive(0x100U, 8U, 0U);
    receive(0x080U, 8U, 0U);
    receive(0x080U, 8U, 0U);
    bsp_can_get_stats(&stats);
    assert(stats.rx_queued == 64U && stats.rx_dropped == 1U);
    assert(stats.priority_replaced == 0U && stats.rx_high_water == 64U);
    assert(bsp_can_receive(&frame) && frame.id == 0x080U);
    assert(frame.timestamp_ms == tick && frame.data[7] == 8U);
    assert(bsp_can_receive(&frame) && frame.id == 0x080U);
    for (i = 0U; i < 64U; ++i)
        assert(bsp_can_receive(&frame) && frame.id == 0x100U);
    assert(!bsp_can_receive(&frame));
    receive(0x100U, 7U, 0U);
    receive(0x100U, 8U, CAN_RI0R_RTR);
    receive(0x100U, 8U, CAN_RI0R_IDE);
    receive(0x555U, 8U, 0U);
    assert(!bsp_can_receive(&frame));
    bsp_can_get_stats(&stats);
    assert(stats.rx_invalid == 4U);
    for (i = 0U; i < 5U; ++i) receive(0x080U, 8U, 0U);
    bsp_can_get_stats(&stats);
    assert(stats.priority_replaced == 1U && stats.rx_dropped == 2U);
    for (i = 0U; i < 4U; ++i) assert(bsp_can_receive(&frame) && frame.id == 0x080U);
    assert(!bsp_can_receive(&frame));
}
static void test_tx_recovery(void)
{
    bsp_can_frame_t frame = {0U, 0x180U, 8U, {1,2,3,4,5,6,7,8}};
    bsp_can_tx_result_t result;
    bsp_can_stats_t stats;
    unsigned i;
    start(UINT32_MAX - 10U);
    for (i = 0U; i < 3U; ++i) assert(bsp_can_send(&frame, i + 1U, 10U));
    assert(!bsp_can_send(&frame, 4U, 10U));
    assert(CAN1->sTxMailBox[0].TDHR == 0x08070605U);
    CAN1->TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0;
    bsp_can_tx_isr();
    /* 完成通知未消费前禁止覆盖邮箱结果。 */
    CAN1->TSR = CAN_TSR_TME0;
    assert(!bsp_can_send(&frame, 4U, 10U));
    assert(bsp_can_take_tx_result(&result) && result.success && result.cookie == 1U);
    assert(bsp_can_send(&frame, 4U, 10U));
    tick = 10U;
    bsp_can_service(tick);
    tick = 30U;
    bsp_can_service(tick);
    bsp_can_get_stats(&stats);
    assert(stats.phase == BSP_CAN_BACKOFF && stats.tx_expired == 3U);
    for (i = 0U; i < 3U; ++i)
        assert(bsp_can_take_tx_result(&result) && !result.success);
    assert(!bsp_can_take_tx_result(&result));
    bsp_can_service(129U);
    bsp_can_get_stats(&stats);
    assert(stats.phase == BSP_CAN_BACKOFF);
    bsp_can_service(130U);
    CAN1->MSR = CAN_MSR_INAK;
    bsp_can_service(131U);
    CAN1->MSR = 0U;
    bsp_can_service(132U);
    assert(bsp_can_ready());
    receive(0x100U, 8U, 0U);
    CAN1->ESR = CAN_ESR_BOFF;
    bsp_can_error_isr();
    assert(!bsp_can_ready());
    bsp_can_service(133U);
    assert(!bsp_can_receive(&frame));
    bsp_can_get_stats(&stats);
    assert(stats.bus_off_count == 1U && stats.generation == 3U);
}
static void test_delayed_terminal(void)
{
    bsp_can_frame_t frame = {0U, 0x180U, 8U, {0}};
    bsp_can_tx_result_t result;
    start(1000U);
    assert(bsp_can_send(&frame, 91U, 1010U));
    CAN1->TSR = CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2;
    bsp_can_service(1005U);
    assert(!bsp_can_take_tx_result(&result));
    tick = 1010U; bsp_can_service(tick);
    CAN1->TSR = CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_TME0;
    bsp_can_tx_isr();
    assert(bsp_can_take_tx_result(&result) && result.cookie == 91U && result.success);
    assert(!bsp_can_take_tx_result(&result));
    CAN1->TSR = CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2;
    assert(bsp_can_send(&frame, 92U, 1020U));
    bsp_can_cancel(92U);
    CAN1->TSR = CAN_TSR_RQCP0 | CAN_TSR_TME0;
    bsp_can_tx_isr();
    assert(bsp_can_take_tx_result(&result) && result.cookie == 92U && !result.success);
    assert(!bsp_can_take_tx_result(&result));
    puts("PASS CAN BSP delayed IRQ/empty mailbox not terminal, abort-success race uses actual TXOK, cancelled transaction consumed once");
}
int main(void)
{
    test_rx();
    test_tx_recovery();
    test_delayed_terminal();
    assert(enabled >= 12U && notifications != 0U);
    puts("PASS CAN BSP production: filters, RX64/priority, invalid frames, three mailboxes, buffer ownership, timeout, wrap and Bus-off");
    return 0;
}
