#include "main.h"
#include "spi.h"
#include "NRF24L01.h"
#include <assert.h>

SPI_HandleTypeDef hspi2;
static uint8_t registers[32][5], fifo[3][32], widths[3], fifo_count;
static uint8_t irq, ce, csn, locked, supported;
static unsigned activations, flush_rx;
static int fail_after;
static uint32_t ticks;

uint32_t HAL_GetTick(void) { return ticks; }
void HAL_Delay(uint32_t ms) { (void)ms; assert(!"blocking delay in driver hot path"); }
void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState value)
{
    (void)port;
    if (pin == CE_Pin) ce = (uint8_t)value;
    if (pin == CSN_Pin) csn = (uint8_t)value;
}

static void pop(void)
{
    if (!fifo_count) return;
    fifo_count--;
    memmove(fifo, fifo + 1U, fifo_count * 32U);
    memmove(widths, widths + 1U, fifo_count);
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, uint8_t *tx,
                                         uint8_t *rx, uint16_t length, uint32_t timeout)
{
    uint8_t command, reg;
    uint16_t i;
    (void)spi;
    assert(!csn && length >= 1U && length <= 33U && timeout == 2U);
    if (fail_after == 0) { ticks += timeout; return HAL_ERROR; }
    if (fail_after > 0) fail_after--;
    memset(rx, 0, length);
    rx[0] = (uint8_t)(irq | (fifo_count ? 0U : 0x0EU));
    command = tx[0]; reg = command & 31U;
    if (command == NRF24L01_NOP) return HAL_OK;
    if (command == 0x50U) {
        assert(length == 2U && tx[1] == 0x73U); activations++; locked = 0U;
    } else if (command == NRF24L01_FLUSH_RX) {
        fifo_count = 0U; flush_rx++;
    } else if (command == NRF24L01_FLUSH_TX) {
        /* 模拟发送FIFO，接收队列不得被发送清理误删。 */
    } else if (command == NRF24L01_R_RX_PL_WID) {
        assert(fifo_count && length == 2U); rx[1] = widths[0];
    } else if (command == NRF24L01_R_RX_PAYLOAD) {
        assert(fifo_count);
        for (i = 1U; i < length; i++) rx[i] = fifo[0][i - 1U];
        pop();
    } else if (command == NRF24L01_W_TX_PAYLOAD) {
        assert(length == 33U);
    } else if ((command & 0xE0U) == NRF24L01_W_REGISTER) {
        if (reg == NRF24L01_STATUS) irq &= (uint8_t)~tx[1];
        else if (!((reg == NRF24L01_FEATURE || reg == NRF24L01_DYNPD) &&
                   (!supported || locked))) {
            for (i = 1U; i < length; i++) registers[reg][i - 1U] = tx[i];
        }
    } else if ((command & 0xE0U) == NRF24L01_R_REGISTER) {
        for (i = 1U; i < length; i++) rx[i] = registers[reg][i - 1U];
        if (reg == NRF24L01_FIFO_STATUS) rx[1] = (uint8_t)(0x10U | (!fifo_count ? 1U : 0U));
    } else assert(!"unknown SPI command");
    return HAL_OK;
}

static void hardware_reset(uint8_t feature_supported, uint8_t initially_locked)
{
    memset(registers, 0, sizeof(registers)); memset(fifo, 0, sizeof(fifo));
    fifo_count = irq = ce = 0U; csn = 1U;
    supported = feature_supported; locked = initially_locked;
    activations = flush_rx = 0U; fail_after = -1; ticks = 0U;
    NRF24L01_Init();
}

static void initialize(void)
{
    unsigned i;
    for (i = 0U; i < 40U && !NRF24L01_IsInitialized(); i++) {
        NRF24L01_Service(); ticks++;
    }
    assert(NRF24L01_IsInitialized() && !ce);
    assert(registers[NRF24L01_CONFIG][0] == 0x0AU);
    assert(registers[NRF24L01_SETUP_RETR][0] == 0x13U);
}

static void push(uint8_t width, uint8_t marker)
{
    assert(fifo_count < 3U);
    memset(fifo[fifo_count], marker, 32U);
    widths[fifo_count++] = width; irq |= 0x40U;
}

static void transmit(void)
{
    uint8_t control[32] = {0};
    assert(NRF24L01_StartSend(control, 32U));
    assert(NRF24L01_PollSend() == NRF24L01_TX_BUSY && !ce);
    ticks += 2U;
    assert(NRF24L01_PollSend() == NRF24L01_TX_BUSY && ce);
}

int main(void)
{
    uint8_t data[32], length;
    NRF24L01_Stats stats;
    unsigned i, base_flush;
    hardware_reset(1U, 0U); initialize();
    assert(NRF24L01_HasAckPayload() && activations == 0U);
    base_flush = flush_rx;
    transmit(); push(32U, 1U); push(32U, 2U); push(32U, 3U); irq |= 0x20U;
    assert(NRF24L01_PollSend() == NRF24L01_TX_SUCCESS && !ce);
    assert(!fifo_count && irq == 0U && flush_rx == base_flush);
    for (i = 1U; i <= 3U; i++) {
        assert(NRF24L01_PopAckPayload(data, &length) && length == 32U && data[0] == i);
    }
    assert(!NRF24L01_PopAckPayload(data, &length));
    transmit(); irq |= 0x20U;
    assert(NRF24L01_PollSend() == NRF24L01_TX_SUCCESS);
    NRF24L01_GetStats(&stats); assert(stats.empty_ack == 1U && stats.tx_success == 2U);
    /* IRQ置位但FIFO已经为空时也须清除RX_DR，且保留发送成功结果。 */
    transmit(); irq |= 0x60U;
    assert(NRF24L01_PollSend() == NRF24L01_TX_SUCCESS && irq == 0U && !ce);
    assert(!NRF24L01_PopAckPayload(data, &length));
    push(32U, 4U); push(32U, 5U); NRF24L01_Service();
    push(32U, 6U); push(32U, 7U); push(32U, 8U); NRF24L01_Service();
    NRF24L01_GetStats(&stats); assert(stats.queue_overflows == 2U);
    for (i = 6U; i <= 8U; i++) {
        assert(NRF24L01_PopAckPayload(data, &length) && data[0] == i);
    }
    push(0U, 0U); push(31U, 9U); NRF24L01_Service();
    assert(!NRF24L01_PopAckPayload(data, &length) && !fifo_count);
    push(33U, 9U); push(32U, 10U); NRF24L01_Service();
    NRF24L01_GetStats(&stats); assert(stats.bad_width == 3U && !fifo_count);
    transmit(); irq |= 0x10U;
    assert(NRF24L01_PollSend() == NRF24L01_TX_MAX_RT && !ce);
    NRF24L01_GetStats(&stats); assert(stats.max_rt == 1U);
    transmit(); push(32U, 11U); irq |= 0x20U; fail_after = 1;
    /* TX_DS已确认，后续ACK读失败仍不能将控制成功误报为失败。 */
    assert(NRF24L01_PollSend() == NRF24L01_TX_SUCCESS);
    assert(!NRF24L01_IsInitialized() && !ce);
    fail_after = -1;
    NRF24L01_Service(); assert(!NRF24L01_IsInitialized());
    ticks += 499U; NRF24L01_Service(); assert(!NRF24L01_IsInitialized());
    ticks++; initialize();
    NRF24L01_GetStats(&stats); assert(stats.spi_errors == 1U && stats.recoveries == 1U);
    hardware_reset(1U, 1U); initialize();
    assert(NRF24L01_HasAckPayload() && activations == 1U);
    hardware_reset(0U, 0U); initialize();
    assert(!NRF24L01_HasAckPayload() && activations == 1U);
    assert(registers[NRF24L01_FEATURE][0] == 0U && registers[NRF24L01_DYNPD][0] == 0U);
    transmit(); irq |= 0x20U;
    assert(NRF24L01_PollSend() == NRF24L01_TX_SUCCESS);
    NRF24L01_GetStats(&stats); assert(stats.capability_failures == 1U);
    puts("PASS ACK driver: PTX, simultaneous IRQ, FIFO, widths, empty ACK, SPI, fallback");
    return 0;
}
