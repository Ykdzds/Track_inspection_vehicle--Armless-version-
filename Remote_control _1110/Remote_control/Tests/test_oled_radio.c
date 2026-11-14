/* Real app, display, OLED and NRF drivers; only physical HAL and input delivery are simulated. */
#include "app_main.h"
#include "remote_display.h"
#include "OLED.h"
#include "i2c.h"
#include "spi.h"
#include "NRF24L01.h"
#include "remote_status_protocol.h"
#include <assert.h>

TIM_HandleTypeDef htim1 = {TIM1}, htim2 = {TIM2};
ADC_HandleTypeDef hadc1;
IWDG_HandleTypeDef hiwdg;
SPI_HandleTypeDef hspi2;
I2C_HandleTypeDef hi2c1 = {HAL_I2C_STATE_READY, 0U};
extern uint16_t adc_data[4];
extern uint8_t OLED_DisplayBuf[8][128];
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

static uint32_t ticks, read_count, ce_tick, last_send, max_gap, tx_count;
static uint8_t registers[32][5], payload[32], ack[32], irq, ce, csn = 1U, ack_pending;
static uint8_t expected_page, cursor_page, cursor_valid, frozen_valid, low_line;
static uint8_t fail_next, fail_init, timing_enabled;
static uint8_t outstanding;
static unsigned pages, frames, oled_calls, recoveries, scans, delay_calls, resets;
static unsigned aborts, successes, seen_k5, seen_k9, seen_steps, seen_joystick;
static uint16_t short_events, long_events, double_events, speed_steps[2], k9_clicks;
static uint8_t held;
uint16_t key_get_held_mask(void) { return held; }
static uint8_t frozen[8][128];

static void advance(uint32_t ms)
{
    while (ms--) {
        ticks++;
        if (timing_enabled) {
            HAL_TIM_PeriodElapsedCallback(&htim1);
            HAL_TIM_PeriodElapsedCallback(&htim2);
        }
    }
}
void OLED_TestDrawCost(uint16_t bytes)
{ (void)bytes; if (timing_enabled) advance(1U); }
uint32_t HAL_GetTick(void)
{
    if (++read_count >= 100U) { read_count = 0U; advance(1U); }
    return ticks;
}
void HAL_Delay(uint32_t ms) { assert(!timing_enabled && ms == 100U); delay_calls++; advance(ms); }
void HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *timer) { (void)timer; timing_enabled = 1U; }
void HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc, uint32_t *data, uint32_t length)
{ (void)adc; (void)data; assert(length == 4U); }
void HAL_IWDG_Refresh(IWDG_HandleTypeDef *watchdog) { (void)watchdog; }
void key_scan(void) { scans++; }
void key_get_and_clear_events(uint16_t *s, uint16_t *l, uint16_t *d)
{ *s = short_events; *l = long_events; *d = double_events; short_events = long_events = double_events = 0U; }
void key_get_speed_input(uint8_t *h, uint16_t steps[2], uint16_t *clicks)
{ *h = held; memcpy(steps, speed_steps, sizeof(speed_steps)); *clicks = k9_clicks; }

GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin)
{ assert(port == GPIOB && (pin == GPIO_PIN_8 || pin == GPIO_PIN_9)); return low_line ? GPIO_PIN_RESET : GPIO_PIN_SET; }
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *i2c) { return i2c->State; }
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c) { return i2c->ErrorCode; }
uint32_t mock_i2c_flag(void) { return 0U; }
void mock_i2c_reset(uint8_t force) { resets += force; }
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *i2c)
{ assert(i2c->State == HAL_I2C_STATE_RESET); i2c->State = HAL_I2C_STATE_READY; recoveries++; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *i2c)
{ i2c->State = HAL_I2C_STATE_RESET; expected_page = cursor_valid = frozen_valid = 0U; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *i2c, uint16_t address,
    uint16_t control, uint16_t address_size, uint8_t *data, uint16_t size, uint32_t timeout)
{
    assert(i2c == &hi2c1 && address == 0x78U && address_size == I2C_MEMADD_SIZE_8BIT);
    assert(!low_line && timeout <= 5U); oled_calls++;
    if (fail_next || (fail_init && size == 23U)) {
        fail_next = 0U; advance(timeout + 1U); i2c->ErrorCode = 0x20U; return HAL_TIMEOUT;
    }
    if (control == 0U && size == 23U) {
        assert(data[0] == 0xAEU && data[22] == 0xAFU);
        expected_page = cursor_valid = frozen_valid = 0U;
        advance(1U);
    } else if (control == 0U) {
        assert(size == 3U && data[1] == 0x10U && data[2] == 0U);
        cursor_page = data[0] & 7U; cursor_valid = 1U;
        assert(cursor_page == expected_page); advance(1U);
    } else {
        assert(control == 0x40U && size == 128U && cursor_valid);
        if (!expected_page) { memcpy(frozen, OLED_DisplayBuf, sizeof(frozen)); frozen_valid = 1U; }
        assert(frozen_valid && memcmp(frozen, OLED_DisplayBuf, sizeof(frozen)) == 0);
        assert(memcmp(data, frozen[cursor_page], 128U) == 0);
        advance(3U); pages++; cursor_valid = 0U;
        expected_page = (uint8_t)((expected_page + 1U) % 8U);
        if (!expected_page) { frames++; frozen_valid = 0U; }
    }
    return HAL_OK;
}

void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState value)
{
    (void)port;
    if (pin == CSN_Pin) csn = (uint8_t)value;
    if (pin == CE_Pin) {
        if (value && !ce) ce_tick = ticks;
        ce = (uint8_t)value;
    }
}
static void make_ack(void)
{
    uint16_t crc;
    memset(ack, 0, sizeof(ack)); ack[0] = 0x5AU; ack[1] = 0xA5U; ack[2] = 1U; ack[3] = 0x81U;
    ack[4] = (uint8_t)successes; ack[5] = 22U; ack[6] = 1U; ack[7] = 7U; ack[8] = 3U;
    ack[12] = 7U; ack[14] = 7U; ack[30] = 13U; ack[31] = 10U;
    crc = remote_status_crc16(ack, 28U); ack[28] = (uint8_t)(crc >> 8U); ack[29] = (uint8_t)crc;
    ack_pending = 1U; irq |= 0x60U;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, uint8_t *tx,
    uint8_t *rx, uint16_t length, uint32_t timeout)
{
    uint8_t command = tx[0], reg = command & 31U;
    assert(spi == &hspi2 && !csn && timeout == 2U);
    /* Real driver supplies its 2 ms settling stage; hardware ACK takes 1 ms after CE. */
    if (ce && outstanding && ticks - ce_tick >= 1U) { successes++; outstanding = 0U; make_ack(); }
    memset(rx, 0, length); rx[0] = (uint8_t)(irq | (ack_pending ? 0U : 0x0EU));
    if (command == NRF24L01_NOP) return HAL_OK;
    if (command == NRF24L01_FLUSH_RX) ack_pending = 0U;
    else if (command == NRF24L01_FLUSH_TX) {
        if (outstanding) aborts++;
        outstanding = 0U;
    } else if (command == NRF24L01_R_RX_PL_WID) { assert(ack_pending); rx[1] = 32U; }
    else if (command == NRF24L01_R_RX_PAYLOAD) { assert(ack_pending && length == 33U); memcpy(rx + 1U, ack, 32U); ack_pending = 0U; }
    else if (command == NRF24L01_W_TX_PAYLOAD) {
        uint32_t gap = ticks - last_send;
        assert(length == 33U); memcpy(payload, tx + 1U, 32U);
        assert(remote_status_crc16(payload, 28U) == ((uint16_t)payload[28] << 8U | payload[29]));
        if (tx_count && gap > max_gap) max_gap = gap;
        last_send = ticks; tx_count++; outstanding = 1U;
        if (payload[14] & 0x10U) seen_k5++;
        if (payload[15] & 1U) seen_k9++;
        if (payload[24] == 1U && payload[25] == 44U && payload[26] == 1U && payload[27] == 45U) seen_steps++;
        assert(!(payload[14] & 0xC0U) && !(payload[16] & 0xC0U) && !(payload[18] & 0xC0U));
        if (payload[6] == 0U && payload[7] == 0U && payload[8] == 7U && payload[9] == 0xD0U &&
            payload[10] == 3U && payload[11] == 0xE8U && payload[12] == 1U && payload[13] == 0xF4U) seen_joystick++;
    } else if ((command & 0xE0U) == NRF24L01_W_REGISTER) {
        if (reg == NRF24L01_STATUS) irq &= (uint8_t)~tx[1];
        else for (unsigned i = 1U; i < length; i++) registers[reg][i-1U] = tx[i];
    } else if ((command & 0xE0U) == NRF24L01_R_REGISTER) {
        for (unsigned i = 1U; i < length; i++) rx[i] = registers[reg][i-1U];
        if (reg == NRF24L01_FIFO_STATUS) rx[1] = (uint8_t)(0x10U | (ack_pending ? 0U : 1U));
    } else assert(!"unhandled SPI command");
    return HAL_OK;
}
static void run(uint32_t duration)
{
    uint32_t start = ticks;
    while (ticks - start < duration) { main_loop_task(); advance(1U); }
}

int main(void)
{
    unsigned calls, base_frames, base_pages, base_scans;
    NRF24L01_Stats stats;
    adc_data[0] = 0U; adc_data[1] = 4095U; adc_data[2] = 2048U; adc_data[3] = 1024U;
    app_init(); assert(delay_calls == 1U && pages == 8U);
    /* Exercise every logical page with one millisecond per rendered text item. */
    for (unsigned language=0U; language<2U; language++) {
        if(language) long_events=0x40U;
        for (unsigned logical=0U; logical<remote_display_page_count((remote_display_language_t)language); logical++) {
            if (logical) short_events = 0x80U;
            run(400U);
        }
    }
    /* Repeated switches during partial transfers and ACK waits. K5/K9 and
       cumulative speed input must still reach the real radio payload. */
    held=1U;speed_steps[0]=300U;speed_steps[1]=301U;
    for(unsigned flip=0U;flip<30U;flip++) {
        while(expected_page!=2U){main_loop_task();advance(1U);}
        long_events=0x40U;short_events=0x90U;k9_clicks++;
        run(300U);
    }
    assert(seen_k5>=30U&&seen_k9>=30U&&seen_steps&&seen_joystick);
    assert(NRF24L01_IsInitialized() && NRF24L01_HasAckPayload() && frames > 20U && successes > 200U);
    NRF24L01_GetStats(&stats); assert(stats.tx_success > 200U && stats.ack_payloads > 200U);
    printf("Timing: aborts %u max gap %lu ms\n",aborts,(unsigned long)max_gap);
    assert(aborts == 0U && max_gap <= 15U);
    printf("PASS real OLED+radio normal: max send gap %lu ms, %lu ACKs\n", (unsigned long)max_gap, (unsigned long)stats.tx_success);

    /* Flip the logical page during a partial frame: all 8 hardware pages stay frozen. */
    while (expected_page != 2U) { main_loop_task(); advance(1U); }
    short_events = 0x80U; main_loop_task(); advance(1U);
    fail_next = 1U;
    while (OLED_IsReady()) { main_loop_task(); advance(1U); }
    base_frames = frames; base_pages = pages; base_scans = scans;
    low_line = 1U; calls = oled_calls;
    short_events = 0x10U; k9_clicks += 2U; held = 1U; speed_steps[0] = 300U; speed_steps[1] = 301U;
    max_gap = 0U; run(1600U);
    assert(!OLED_IsReady() && oled_calls == calls && pages == base_pages && scans - base_scans >= 1600U);
    printf("Fault: k5 %u k9 %u steps %u joy %u aborts %u gap %lu\n",seen_k5,seen_k9,seen_steps,seen_joystick,aborts,(unsigned long)max_gap);
    assert(seen_k5 && seen_k9 >= 2U && seen_steps && seen_joystick && aborts == 0U && max_gap <= 20U);
    /* Language/navigation requests remain accepted while lines are low.
       The final request selects English DBG after three offline switches. */
    for(unsigned flip=0U;flip<3U;flip++){long_events=0x40U;run(20U);}
    short_events = 0x40U; run(20U);
    low_line = 0U; fail_init = 1U; run(1100U);
    assert(!OLED_IsReady() && pages == base_pages);
    fail_init = 0U; run(1000U);
    assert(OLED_IsReady() && frames > base_frames && recoveries >= 3U && resets >= 3U);
    assert(delay_calls == 1U && aborts == 0U && max_gap <= 20U);
    printf("PASS real OLED+radio fault/recovery: max gap %lu ms, K5/K9/steps/joystick continue, frozen pages restart at zero\n", (unsigned long)max_gap);
    return 0;
}
