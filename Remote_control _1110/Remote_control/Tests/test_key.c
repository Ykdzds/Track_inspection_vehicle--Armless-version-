#include "key.h"
#include "remote_display.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t pressed;
static uint32_t masked;
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin)
{
    (void)port;
    return pressed & (1U << pin) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
uint32_t __get_PRIMASK(void) { return masked; }
void __disable_irq(void) { masked = 1U; }
void __enable_irq(void) { masked = 0U; }
static void advance(unsigned ms)
{
    while (ms--) key_scan();
}
static void events(uint16_t s, uint16_t l, uint16_t d)
{
    uint16_t actual_s, actual_l, actual_d;
    key_get_and_clear_events(&actual_s, &actual_l, &actual_d);
    assert(actual_s == s && actual_l == l && actual_d == d);
    assert(!masked);
}
static void click(unsigned key)
{
    pressed = (uint16_t)(1U << key); advance(50U);
    pressed = 0U; advance(20U);
}
static void service_language(remote_display_t *display)
{
    uint16_t s,l,b;
    key_get_and_clear_events(&s,&l,&b);remote_display_keys(display,s,&s,&l,&b);
    assert(!(s|l|b));
}
static void test_real_k7_language(void)
{
    remote_display_t display;remote_display_init(&display);
    for(unsigned round=0U;round<4U;round++) {
        uint8_t original=display.requested_language;
        pressed=0x0040U;
        for(unsigned i=0U;i<990U;i++){advance(1U);service_language(&display);}
        assert(display.requested_language==original);
        for(unsigned i=0U;i<100U;i++){advance(1U);service_language(&display);}
        assert(display.requested_language!=original&&display.requested_page==0U);
        for(unsigned i=0U;i<3000U;i++){advance(1U);service_language(&display);}
        assert(display.requested_language!=original&&display.requested_page==0U);
        pressed=0U;
        for(unsigned i=0U;i<400U;i++){advance(1U);service_language(&display);}
        assert(display.requested_language!=original&&display.requested_page==0U);
    }
    click(6U);service_language(&display);advance(310U);service_language(&display);
    assert(display.requested_page==8U);
    click(7U);advance(310U);service_language(&display);assert(!display.requested_page);
    click(6U);click(6U);service_language(&display);assert(!display.requested_page);
    pressed=0x0080U;advance(1100U);service_language(&display);
    pressed=0U;advance(400U);service_language(&display);
    assert(!display.requested_language&&!display.requested_page);
    puts("PASS real K7: approximately 1s toggles once, 3s hold/release no repeat, EN/ZH home, short/double/K8 unchanged");
}
int main(void)
{
    memset(keys, 0, sizeof(KEY) * 12U);
    /* K5松手后立即短按，连续点击不生成双击。 */
    click(4U); events(0x0010U, 0U, 0U);
    click(4U); events(0x0010U, 0U, 0U);
    advance(400U); events(0U, 0U, 0U);
    pressed = 0x0010U; advance(1050U); events(0U, 0x0010U, 0U);
    advance(300U); events(0U, 0U, 0U);
    pressed = 0U; advance(20U); events(0U, 0U, 0U);
    /* K1恢复普通扫描，K6继续保留单击等待和双击识别。 */
    for (unsigned key_index = 0U; key_index < 12U; key_index++)
    {
        uint16_t mask = (uint16_t)(1U << key_index);
        if (key_index == 4U || key_index < 2U || key_index == 8U) continue;
        click(key_index); events(0U, 0U, 0U);
        advance(310U); events(mask, 0U, 0U);
        click(key_index); events(0U, 0U, 0U);
        click(key_index); events(0U, 0U, mask);
        pressed = mask; advance(1050U); events(0U, mask, 0U);
        pressed = 0U; advance(20U); events(0U, 0U, 0U);
    }
    {
        uint8_t held; uint16_t steps[2], before[2], clicks, baseline_clicks;
        key_get_speed_input(&held, before, &baseline_clicks);
        for (unsigned k = 0U; k < 2U; ++k) {
            pressed = (uint16_t)(1U << k); advance(20U);
            key_get_speed_input(&held, steps, &clicks);
            assert(held == (1U << k) && (uint16_t)(steps[k] - before[k]) == 1U);
            advance(280U); key_get_speed_input(&held, steps, &clicks);
            assert((uint16_t)(steps[k] - before[k]) == 1U);
            advance(9720U); key_get_speed_input(&held, steps, &clicks);
            assert((uint16_t)(steps[k] - before[k]) == 400U);
            advance(1000U); key_get_speed_input(&held, steps, &clicks);
            assert((uint16_t)(steps[k] - before[k]) == 440U);
            pressed = 0U; advance(20U); events(0U, (uint16_t)(1U << k), 0U);
            key_get_speed_input(&held, before, &clicks);
            advance(100U); key_get_speed_input(&held, steps, &clicks);
            assert(!held && steps[k] == before[k]);
            click(k); events((uint16_t)(1U << k), 0U, 0U);
            key_get_speed_input(&held, steps, &clicks);
            assert((uint16_t)(steps[k] - before[k]) == 1U);
            before[k] = steps[k];
        }
        click(8U); events(0x0100U, 0U, 0U);
        click(8U); events(0x0100U, 0U, 0U);
        key_get_speed_input(&held, steps, &clicks);
        assert((uint16_t)(clicks - baseline_clicks) == 2U);
        pressed = 0x0100U; advance(1050U); events(0U, 0x0100U, 0U);
        pressed = 0U; advance(20U); events(0U, 0U, 0U);
        key_get_speed_input(&held, before, &clicks);
        pressed = 3U; advance(1500U);
        key_get_speed_input(&held, steps, &clicks);
        assert(held == 3U && steps[0] == before[0] && steps[1] == before[1]);
        pressed = 1U; advance(1000U); key_get_speed_input(&held, steps, &clicks);
        assert(steps[0] == before[0]);
        pressed = 0U; advance(30U); key_get_and_clear_events(&before[0], &before[1], &clicks);
    }
    test_real_k7_language();
    puts("PASS speed scanner: 10s/400 steps, short, hold, no tail, conflict, K9 count");
    puts("PASS K5 scanner: immediate release, repeated short, long-only release, K1/K6 ordinary keys");
    return 0;
}
