/* Real 10 ms key scanner -> real parser -> real F407 controller. */
#define main legacy_remote_suite
#include "test_remote_servo.c"
#undef main
#include "key.h"
#include "nrf_remote_protocol.h"
/* Key scanner GPIO map in the host harness, independent of physical MCU pins. */
static uint16_t pressed;
static uint8_t control_frame[32];
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin)
{ (void)port; return pressed & (1U << pin) ? GPIO_PIN_RESET : GPIO_PIN_SET; }
uint32_t __get_PRIMASK(void) { return 0U; }
void __disable_irq(void) {}
void __enable_irq(void) {}

static void sample(nrf_remote_snapshot_t *in, unsigned time_ms)
{
    uint8_t held;
    uint16_t steps[2], clicks, crc;
    nrf_remote_frame_t decoded;
    key_scan();
    if (time_ms % 10U) return;
    key_get_speed_input(&held,steps,&clicks);
    memset(control_frame,0,32);
    control_frame[0]=0xA5; control_frame[1]=0x5A; control_frame[2]=control_frame[3]=1;
    control_frame[4]=in->frame_sequence++; control_frame[5]=17;
    control_frame[8]=3; control_frame[9]=0xE8;
    control_frame[23]=(uint8_t)(0xA0|held);
    control_frame[24]=(uint8_t)(steps[0]>>8); control_frame[25]=(uint8_t)steps[0];
    control_frame[26]=(uint8_t)(steps[1]>>8); control_frame[27]=(uint8_t)steps[1];
    control_frame[30]=13; control_frame[31]=10;
    crc=nrf_remote_crc16(control_frame,28); control_frame[28]=(uint8_t)(crc>>8); control_frame[29]=(uint8_t)crc;
    assert(nrf_remote_parse(control_frame,&decoded)==NRF_REMOTE_PARSE_OK);
    in->speed_keys_valid=decoded.speed_keys_valid; in->speed_keys_held=decoded.speed_keys_held;
    in->speed_steps[0]=decoded.speed_steps[0]; in->speed_steps[1]=decoded.speed_steps[1];
    ticks+=10; feed(in);
}
int main(void)
{
    nrf_remote_snapshot_t in;
    unsigned elapsed, reached;
    setup_stop_test(&in,0);
    in.speed_keys_valid=1; feed(&in);
    in.short_counts[8]++; feed(&in);
    assert(status().speed_mode==REMOTE_SPEED_KEYS);
    pressed=1;
    reached=0U;
    for(elapsed=1;elapsed<=10070;elapsed++) {
        sample(&in,elapsed);
        if (!reached && status().common_target_centi_kmh==2000) reached=elapsed;
    }
    assert(reached>=9970U && reached<=10070U); /* 20 ms debounce + 10 s, +/-50 ms */
    printf("K1 full target first reached at %u ms physical hold\n", reached);
    assert(status().common_target_centi_kmh==2000);
    assert(status().target_rpm==1529);
    pressed=0; for(elapsed=1;elapsed<=40;elapsed++) sample(&in,elapsed);
    assert(status().common_target_centi_kmh==2000);
    pressed=2;
    for(elapsed=1;elapsed<=40;elapsed++) sample(&in,elapsed);
    pressed=0; for(elapsed=1;elapsed<=40;elapsed++) sample(&in,elapsed);
    assert(status().common_target_centi_kmh==1995);
    in.short_counts[8]++; feed(&in);
    assert(!status().common_target_centi_kmh && status().key_exit_pending);
    pressed=0; for(elapsed=1;elapsed<=40;elapsed++) sample(&in,elapsed);
    pressed=2;
    reached=0U;
    for(elapsed=1;elapsed<=10070;elapsed++) {
        sample(&in,elapsed);
        if (!reached && status().common_target_centi_kmh==-2000) reached=elapsed;
    }
    assert(reached>=9970U && reached<=10070U);
    printf("K2 full target first reached at %u ms physical hold\n", reached);
    assert(status().common_target_centi_kmh==-2000 && status().target_rpm==-1529);
    assert(!status().key_exit_pending); /* pressing K2 cancels exit preparation */
    puts("PASS real scanner -> frame -> F407: 10.02s physical holds = +/-20.00 km/h, K2 single = 19.95");
    return 0;
}
