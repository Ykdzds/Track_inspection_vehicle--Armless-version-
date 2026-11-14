/* Real production controller, sharing the existing asynchronous servo harness. */
#define main legacy_remote_suite
#include "test_remote_servo.c"
#undef main

static void step(nrf_remote_snapshot_t *in, unsigned key, unsigned count)
{
    in->speed_steps[key] = (uint16_t)(in->speed_steps[key] + count);
    ticks += 10U; feed(in);
}
static void k9(nrf_remote_snapshot_t *in)
{
    in->short_counts[8]++; ticks += 10U; feed(in);
}
static void settle(nrf_remote_snapshot_t *in)
{
    unsigned i;
    for (i=0;i<5000U;i++) { ticks+=10U; feed(in); }
}
int main(void)
{
    nrf_remote_snapshot_t in;
    unsigned i, old_enables, old_prepares;
    setup_stop_test(&in, 0);
    in.speed_keys_valid = 1U;
    feed(&in);
    old_enables = enables; old_prepares = prepares;
    step(&in,0,1); assert(status().speed_mode == REMOTE_SPEED_JOYSTICK && !status().target_rpm);
    in.left_vertical=939U; k9(&in); assert(status().speed_mode == REMOTE_SPEED_JOYSTICK);
    in.left_vertical=1061U; k9(&in); assert(status().speed_mode == REMOTE_SPEED_JOYSTICK);
    in.left_vertical=940U; k9(&in); assert(status().speed_mode == REMOTE_SPEED_KEYS && !status().common_target_centi_kmh);
    in.left_vertical=2000U; feed(&in); assert(!status().target_rpm);
    step(&in,0,1); assert(status().common_target_centi_kmh==5 && status().target_rpm==4);
    feed(&in); assert(status().common_target_centi_kmh==5); /* same counters */
    step(&in,1,1); assert(!status().common_target_centi_kmh);
    for(i=0;i<200U;i++) step(&in,0,1);
    assert(status().common_target_centi_kmh==1000);
    step(&in,1,1); assert(status().common_target_centi_kmh==995);
    step(&in,1,1); assert(status().common_target_centi_kmh==990);
    for(i=0;i<300U;i++) step(&in,0,1);
    assert(status().common_target_centi_kmh==2000 && status().target_rpm==1529);
    step(&in,0,1); assert(status().common_target_centi_kmh==2000);
    step(&in,1,1); assert(status().common_target_centi_kmh==1995);
    for(i=0;i<900U;i++) step(&in,1,1);
    assert(status().common_target_centi_kmh==-2000 && status().target_rpm==-1529);
    step(&in,0,1); assert(status().common_target_centi_kmh==-1995);
    in.speed_keys_held=3U; step(&in,0,1); assert(status().common_target_centi_kmh==-1995);
    in.speed_keys_held=1U; step(&in,0,1); assert(status().common_target_centi_kmh==-1995);
    in.speed_keys_held=0U; feed(&in); step(&in,0,1); assert(status().common_target_centi_kmh==-1990);
    /* Must return to neutral before K9; K9 plus a step discards that step. */
    k9(&in); assert(!status().key_exit_pending);
    in.left_vertical=1060U; in.speed_steps[0]++; k9(&in);
    assert(status().key_exit_pending && !status().common_target_centi_kmh && status().authorized);
    k9(&in); assert(status().speed_mode==REMOTE_SPEED_KEYS);
    settle(&in); assert(status().key_exit_ready);
    /* Repeated service on the same sample cannot create zero confirmations. */
    step(&in,0,1); assert(!status().key_exit_pending && status().common_target_centi_kmh==5);
    k9(&in); assert(status().key_exit_pending);
    for(i=0;i<5U;i++) { app_remote_servo_update(&in); app_remote_servo_service(); }
    assert(!status().key_exit_ready);
    settle(&in); assert(status().key_exit_ready);
    axes[TEST_AXIS].actual_speed_rpm=1; k9(&in);
    assert(status().speed_mode==REMOTE_SPEED_KEYS && !status().key_exit_ready);
    axes[TEST_AXIS].actual_speed_rpm=0; settle(&in); k9(&in);
    assert(status().speed_mode==REMOTE_SPEED_JOYSTICK && !status().target_rpm);
    assert(enables==old_enables && prepares==old_prepares);
    /* Even already stationary requires first clear then a new second press. */
    in.left_vertical=1000; k9(&in); k9(&in);
    assert(status().speed_mode==REMOTE_SPEED_KEYS && status().key_exit_pending);
    settle(&in); k9(&in); assert(status().speed_mode==REMOTE_SPEED_JOYSTICK);
    /* Held at a mode boundary must be released, never recover previous repeat. */
    in.speed_keys_held=1U; k9(&in); step(&in,0,1); assert(!status().target_rpm);
    in.speed_keys_held=0U; step(&in,0,1); assert(!status().target_rpm);
    step(&in,0,1); assert(status().common_target_centi_kmh==5);
    setup_stop_test(&in,0);
    in.speed_keys_valid=1U; in.short_counts[8]=UINT32_MAX-2U; feed(&in);
    k9(&in); step(&in,0,1);
    k9(&in); /* first clear at UINT32_MAX */
    assert(status().key_exit_pending);
    settle(&in); in.short_counts[8]=0U; feed(&in);
    assert(status().speed_mode==REMOTE_SPEED_JOYSTICK);
    k9(&in); step(&in,0,1);
    assert(status().common_target_centi_kmh==5);
    /* Counter wrap baselines, not signed overflow or duplicate execution. */
    in.speed_steps[0]=65535U; step(&in,0,0); /* rejected rollback */
    step(&in,0,1); assert(status().common_target_centi_kmh==10);
    /* K5 wins over K9 and steps, resets mode; new authorization starts JOY. */
    in.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++; in.short_counts[8]++; step(&in,0,1);
    assert(!status().authorized && status().speed_mode==REMOTE_SPEED_JOYSTICK);
    setup_stop_test(&in,0);
    in.speed_keys_valid=1U; feed(&in); k9(&in); step(&in,0,1);
    in.recovery_generation++; feed(&in);
    assert(!status().authorized && status().speed_mode==REMOTE_SPEED_JOYSTICK);
    setup_stop_test(&in,0);
    in.speed_keys_valid=1U; feed(&in); k9(&in); step(&in,0,1);
    in.speed_keys_valid=0U; feed(&in);
    assert(!status().authorized && status().speed_mode==REMOTE_SPEED_JOYSTICK);
    finish_stop(&in); arm(&in); assert(status().authorized && status().speed_mode==REMOTE_SPEED_JOYSTICK);
    k9(&in); step(&in,0,1); in.link_up=0U; feed(&in);
    assert(!status().authorized && status().speed_mode==REMOTE_SPEED_JOYSTICK);
    /* Stale feedback uses the existing protection path, never a mode exit. */
    setup_stop_test(&in,0);
    in.speed_keys_valid=1U; feed(&in); k9(&in); k9(&in);
    settle(&in); assert(status().key_exit_ready);
    axes[TEST_AXIS].speed_age_ms=APP_SERVO_STATUS_MAX_AGE_MS; k9(&in);
    assert(!status().authorized && !status().key_exit_ready);
    assert(status().stop_reason==REMOTE_STOP_TRANSACTION);
    /* The RX service may drain several packets before publishing a snapshot. */
    setup_stop_test(&in,0); in.speed_keys_valid=1U; feed(&in);
    in.short_counts[8]+=2U; feed(&in);
    assert(status().speed_mode==REMOTE_SPEED_KEYS && status().key_exit_pending);
    settle(&in); k9(&in); assert(status().speed_mode==REMOTE_SPEED_JOYSTICK);
    k9(&in); in.short_counts[8]+=2U; feed(&in);
    assert(status().speed_mode==REMOTE_SPEED_KEYS && status().key_exit_pending);
    settle(&in); assert(status().speed_mode==REMOTE_SPEED_KEYS && status().key_exit_ready);
    puts("PASS speed modes: precision/limits, wrap, two-stage K9/fresh zero, K5, held boundary, loss/stale");
    return 0;
}
