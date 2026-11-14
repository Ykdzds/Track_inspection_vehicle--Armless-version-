#include "OLED.h"
#include "i2c.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern uint8_t OLED_DisplayBuf[8][128];
I2C_HandleTypeDef hi2c1 = {HAL_I2C_STATE_READY, 0U};
typedef struct { uint16_t control, size; uint32_t timeout; uint8_t data[128]; } transfer_t;
static transfer_t transfers[256];
static unsigned writes, inits, deinits, resets, delays, tick_reads, line_reads;
static uint32_t ticks, command_ms = 1U, data_ms = 3U;
static uint32_t release_at;
static uint16_t low_pin;
static uint8_t busy, release_enabled, fail_timeout;
static unsigned fail_write;
static HAL_StatusTypeDef fail_result = HAL_ERROR, init_result = HAL_OK;
static const uint8_t initialization[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0xA1, 0xC8,
    0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x30, 0xA4, 0xA6, 0x8D, 0x14, 0xAF
};

uint32_t HAL_GetTick(void)
{
    /* SysTick progresses during polling as it does on the MCU. */
    if (++tick_reads == 100U) { ticks++; tick_reads = 0U; }
    return ticks;
}
void HAL_Delay(uint32_t ms) { assert(ms == 100U); delays++; ticks += ms; }
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin)
{
    assert(port == GPIOB && (pin == GPIO_PIN_8 || pin == GPIO_PIN_9));
    line_reads++;
    if (release_enabled && (int32_t)(ticks - release_at) >= 0) { low_pin = 0U; busy = 0U; }
    return (low_pin & pin) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *i2c) { assert(i2c == &hi2c1); return i2c->State; }
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c) { return i2c->ErrorCode; }
uint32_t mock_i2c_flag(void)
{
    if (release_enabled && (int32_t)(ticks - release_at) >= 0) busy = 0U;
    return busy;
}
void mock_i2c_reset(uint8_t force) { if (force) resets++; }
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *i2c)
{ assert(i2c == &hi2c1); i2c->State = HAL_I2C_STATE_RESET; deinits++; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *i2c)
{
    assert(i2c == &hi2c1 && i2c->State == HAL_I2C_STATE_RESET);
    inits++; i2c->State = HAL_I2C_STATE_READY; i2c->ErrorCode = 0U;
    return init_result;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *i2c, uint16_t address,
    uint16_t control, uint16_t address_size, uint8_t *data, uint16_t size, uint32_t timeout)
{
    transfer_t *t;
    assert(i2c == &hi2c1 && i2c->State == HAL_I2C_STATE_READY && !busy && !low_pin);
    assert(address == 0x78U && address_size == I2C_MEMADD_SIZE_8BIT);
    assert(control == 0U || control == 0x40U);
    assert(size > 0U && size <= 128U && timeout <= 5U && writes < 256U);
    t = &transfers[writes++]; t->control = control; t->size = size; t->timeout = timeout;
    memcpy(t->data, data, size);
    if (writes == fail_write) {
        ticks += fail_timeout ? timeout + 1U : 1U;
        i2c->ErrorCode = fail_timeout ? 0x20U : 4U;
        return fail_result;
    }
    ticks += control ? data_ms : command_ms;
    return HAL_OK;
}

static void clear_log(void) { writes = 0U; tick_reads = 0U; fail_write = 0U; release_enabled = 0U; }
static void recover(void)
{
    unsigned d = delays, n = inits;
    unsigned calls = writes;
    ticks += 499U; OLED_Service(); assert(inits == n && writes == calls && !OLED_IsReady());
    ticks++; OLED_Service();
    assert(OLED_IsReady() && inits == n + 1U && writes == calls + 1U && delays == d);
    assert(transfers[calls].size == sizeof(initialization) && transfers[calls].control == 0U);
    assert(memcmp(transfers[calls].data, initialization, sizeof(initialization)) == 0);
}

int main(void)
{
    unsigned n, calls;
    uint32_t before;
    OLED_Init();
    assert(OLED_IsReady() && delays == 1U && writes == 17U);
    assert(transfers[0].control == 0U && transfers[0].size == sizeof(initialization));
    assert(memcmp(transfers[0].data, initialization, sizeof(initialization)) == 0);
    for (n = 0; n < 8U; n++) {
        assert(transfers[1U + 2U*n].data[0] == (uint8_t)(0xB0U | n));
        assert(transfers[2U + 2U*n].size == 128U && transfers[2U + 2U*n].control == 0x40U);
        for (unsigned x = 0; x < 128U; x++) assert(transfers[2U + 2U*n].data[x] == 0U);
    }
    for (n = 0; n < sizeof(OLED_DisplayBuf); n++) ((uint8_t *)OLED_DisplayBuf)[n] = (uint8_t)(n*13U);
    clear_log(); before = ticks;
    OLED_UpdateArea(0, 24, 128, 8);
    assert(writes == 2U && ticks - before == 4U);
    assert(transfers[0].size == 3U && transfers[0].data[0] == 0xB3U && transfers[0].data[1] == 0x10U && transfers[0].data[2] == 0U);
    assert(transfers[1].timeout == transfers[0].timeout - command_ms);
    assert(memcmp(transfers[1].data, OLED_DisplayBuf[3], 128U) == 0);
    clear_log(); OLED_UpdateArea(37, 8, 19, 8);
    assert(writes == 2U && transfers[0].data[1] == 0x12U && transfers[0].data[2] == 5U);
    assert(transfers[1].size == 19U && memcmp(transfers[1].data, &OLED_DisplayBuf[1][37], 19U) == 0);
    clear_log(); OLED_UpdateArea(127, 0, 128, 8); assert(transfers[1].size == 1U);
    clear_log(); OLED_UpdateArea(0, 0, 0, 8); OLED_UpdateArea(-1, 0, 128, 8); assert(writes == 0U);
    clear_log(); OLED_UpdateArea(0, 0, 128, 64); assert(OLED_IsReady() && writes == 16U);
    for (n = 0U; n < 8U; n++) {
        assert(transfers[n*2U].data[0] == (0xB0U | n));
        assert(memcmp(transfers[n*2U + 1U].data, OLED_DisplayBuf[n], 128U) == 0);
    }
    OLED_DrawPoint(17, 19); assert(OLED_GetPoint(17, 19));

    /* NACK at cursor: no data and no later pages are sent. */
    clear_log(); fail_write = 1U; OLED_Update();
    assert(!OLED_IsReady() && writes == 1U && deinits == 1U && resets == 1U);
    recover();
    /* NACK at data: stop the same multi-page request immediately. */
    clear_log(); fail_write = 2U; OLED_UpdateArea(0, 0, 128, 64);
    assert(!OLED_IsReady() && writes == 2U); recover();

    /* F1 timeout uses > Timeout. Cursor and data share one six-tick budget. */
    for (n = 1U; n <= 2U; n++) {
        clear_log(); fail_write = n; fail_timeout = 1U; fail_result = HAL_TIMEOUT; before = ticks;
        OLED_UpdateArea(0, 0, 128, 8);
        assert(!OLED_IsReady() && writes == n && ticks - before <= 6U);
        recover();
    }
    fail_timeout = 0U;
    clear_log(); fail_write = 1U; fail_result = HAL_BUSY; OLED_UpdateArea(0, 0, 128, 8);
    assert(!OLED_IsReady() && writes == 1U); recover();

    /* BUSY, non-ready handle, and either stuck line never enter blocking HAL. */
    for (n = 0U; n < 4U; n++) {
        clear_log(); before = ticks;
        busy = (uint8_t)(n == 0U);
        if (n == 1U) hi2c1.State = HAL_I2C_STATE_BUSY_TX;
        low_pin = n == 2U ? GPIO_PIN_8 : (n == 3U ? GPIO_PIN_9 : 0U);
        OLED_UpdateArea(0, 0, 128, 8);
        assert(!OLED_IsReady() && writes == 0U && ticks - before <= 1U);
        busy = 0U; low_pin = 0U; recover();
    }
    clear_log(); busy = 1U; release_enabled = 1U; release_at = ticks + 1U;
    before = ticks; OLED_UpdateArea(0, 0, 128, 8);
    assert(OLED_IsReady() && writes == 2U && ticks - before <= 6U);

    /* A stuck line skips even HAL initialization, at most one probe per 500 ms. */
    clear_log(); low_pin = GPIO_PIN_9; OLED_UpdateArea(0, 0, 128, 8);
    calls = inits; ticks += 500U; OLED_Service(); n = line_reads;
    for (unsigned i = 0; i < 499U; i++) { ticks++; tick_reads = 0U; OLED_Service(); }
    assert(line_reads == n && inits == calls && !OLED_IsReady());
    low_pin = 0U; ticks++; OLED_Service(); assert(OLED_IsReady() && inits == calls + 1U);

    /* Failed HAL reinit and failed OLED init retain the same back-off. */
    clear_log(); fail_write = 1U; OLED_UpdateArea(0, 0, 128, 8);
    calls = writes; init_result = HAL_ERROR; ticks += 500U; OLED_Service(); assert(!OLED_IsReady() && writes == calls);
    init_result = HAL_OK; ticks += 500U; fail_write = writes + 1U; OLED_Service(); assert(!OLED_IsReady());
    fail_write = 0U; recover();

    /* Back-off and page deadlines survive the 32-bit tick wrap. */
    ticks = UINT32_MAX - 2U; clear_log(); OLED_UpdateArea(0, 0, 128, 8); assert(OLED_IsReady());
    ticks = UINT32_MAX - 100U; clear_log(); fail_write = 1U; OLED_UpdateArea(0, 0, 128, 8);
    recover();
    assert(resets == deinits);
    puts("PASS real OLED: init/address/control, framebuffer/pages, shared 6ms budget, NACK/BUSY/timeout/lines, 500ms recovery/wrap");
    return 0;
}
