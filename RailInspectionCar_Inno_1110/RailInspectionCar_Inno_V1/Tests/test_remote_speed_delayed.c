#include <stdint.h>
#define NORMAL_TIMING_TEST 1
static uint32_t timing_delay, timing_due[2];
static int16_t timing_rpm[2];
#define main legacy_remote_suite
#include "test_remote_servo.c"
#undef main

int main(void)
{
    nrf_remote_snapshot_t in;
    uint32_t start;
    unsigned i;
    setup_stop_test(&in,0);
    in.speed_keys_valid=1U; feed(&in); in.short_counts[8]++; feed(&in);
    timing_delay=30U;
    /* Changing KEY targets every 25 ms while dual-axis writes remain latched. */
    for(i=0;i<400U;i++) {
        in.speed_steps[0]++; ticks+=25U; feed(&in);
    }
    assert(status().common_target_centi_kmh==2000 && status().authorized);
    assert(status().commanded_rpm < 1529); /* normal physical ramp still applies */
    in.short_counts[8]++; ticks+=1U; feed(&in);
    assert(status().key_exit_pending && !status().key_exit_ready && status().authorized);
    in.short_counts[8]++; ticks+=1U; feed(&in);
    assert(status().speed_mode==REMOTE_SPEED_KEYS);
    /* Timed acknowledgements complete only their own queued command. */
    for(i=0;i<6000U && !status().key_exit_ready;i++) {ticks+=10U;feed(&in);}
    assert(status().key_exit_ready && status().authorized);
    in.short_counts[8]++; ticks+=1U; feed(&in);
    assert(status().speed_mode==REMOTE_SPEED_JOYSTICK);
    /* 0 -> positive -> negative uses the existing reverse-through-zero path. */
    in.short_counts[8]++; feed(&in);
    for(i=0;i<40U;i++) {in.speed_steps[0]++;ticks+=25;feed(&in);}
    for(i=0;i<1200U;i++) {ticks+=10;feed(&in);}
    assert(status().target_rpm>0 && status().commanded_rpm>0);
    for(i=0;i<80U;i++) {in.speed_steps[1]++;ticks+=25;feed(&in);}
    start=ticks;
    for(i=0;i<4000U && status().commanded_rpm!=status().target_rpm;i++) {ticks+=10;feed(&in);}
    assert(status().authorized && status().target_rpm<0 && status().commanded_rpm==status().target_rpm);
    assert(ticks>start);
    /* Already stationary still requires NEW zero write acknowledgements,
     * then two NEW samples. Cached zero commands cannot satisfy K9. */
    timing_delay=0U; setup_stop_test(&in,0);
    in.speed_keys_valid=1U; feed(&in); in.short_counts[8]++; feed(&in);
    timing_delay=30U;
    in.short_counts[8]++; feed(&in);
    for(i=0;i<2U;i++) {ticks+=10U;feed(&in);}
    assert(status().key_exit_pending && !status().key_exit_ready);
    for(i=0;i<100U && !status().key_exit_ready;i++) {ticks+=10U;feed(&in);}
    assert(status().key_exit_ready && status().authorized);
    puts("PASS delayed KEY writes: 30ms per axis, targets in flight, new zero acknowledgements, reverse");
    return 0;
}
