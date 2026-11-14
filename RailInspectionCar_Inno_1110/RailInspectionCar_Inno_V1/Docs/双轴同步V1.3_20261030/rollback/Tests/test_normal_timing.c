/* Delayed, serial dual-axis transactions; real production controllers. */
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#define NORMAL_TIMING_TEST 1
#define main fixture_regression_main
#if TIMING_SOURCE == 1
#include "test_hmi_control.c"
#elif TIMING_SOURCE == 2
static uint32_t timing_delay, timing_due[2];
static int16_t timing_rpm[2];
#include "test_remote_servo.c"
#else
#include "test_can_control.c"
#endif
#undef main
#include "app_motion_ramp.h"
#include "app_normal_ramp.h"

int main(int argc, char **argv)
{
    const unsigned delays[] = {30, 60, 100, 60};
    const int targets[] = {500, 1000, 1600, -500, -1000, -1600};
    unsigned d, t, axis, step;
    int failures = 0;
    setbuf(stdout, NULL);
    for (d = 0; d < 4; ++d) for (t = 0; t < 6; ++t)
    {
        uint32_t began, duration = 0;
        unsigned phase;
        uint32_t observed_sequence[2] = {0}, observed_tick[2] = {0};
        int16_t observed_rpm[2] = {0};
        int initial;
        int target = targets[t], done;
#if TIMING_SOURCE == 1
        uint16_t mailbox[24]; uint32_t session, sequence = 1;
        reset_fixture(); g_feedback_progress = 1; timing_delay = 0;
        make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0, sequence++, 0, 0);
        submit_service(mailbox); session = app_hmi_control_session(); assert(session);
        for (step = 0; step < 20; ++step) { ++g_tick; app_hmi_control_service(); }
        began = g_tick;
#elif TIMING_SOURCE == 2
        nrf_remote_snapshot_t input;
        memset(axes, 0, sizeof(axes)); memset(&input, 0, sizeof(input));
        memset(&debug, 0, sizeof(debug)); timing_delay = 0;
        startup_hold = 0; hold_ready = boot_ready = 1;
        hold_response = prepare_fail_mask = enable_fail_mask = 0;
        for (axis = 0; axis < 2; ++axis) {
            axes[axis].online = axes[axis].enabled = axes[axis].parameter_verified = 1;
            axes[axis].control_mode = SERVO_CONTROL_RS485_VELOCITY;
        }
        input.link_up = 1; reset_remote(); arm(&input); assert(status().authorized);
        input.left_vertical = joystick_for_rpm(target); feed(&input);
        target = status().target_rpm; began = ticks;
#else
        int16_t mm;
        timing_delay = 0; reset(); acquire();
        axis = APP_SERVO_CONTROL_AXIS == 2 ? 1 : 0;
        mm = (int16_t)(target * app_can_control_rpm_mm_s(1, (uint8_t)axis));
        target = (int16_t)(mm / app_can_control_rpm_mm_s(1, (uint8_t)axis));
        began = tick;
#endif
        timing_delay = delays[d];
        initial = target;
        for (phase = 0; phase < 2; ++phase) {
        FILE *curve = NULL;
        const char *directory = getenv("NORMAL_CURVE_DIR");
        if (directory && d == 1 && t == 1 && APP_SERVO_CONTROL_AXIS == 3 && APP_REMOTE_SERVO_SYNC_ENABLED) {
            char path[512];
            snprintf(path, sizeof(path), "%s/source_%d_phase_%u.csv", directory, TIMING_SOURCE, phase);
            curve = fopen(path, "w"); assert(curve);
            fprintf(curve, "time_ms,target_rpm,internal_rpm,latched_left,latched_right,sim_actual_left,sim_actual_right\n");
        }
        if (phase) {
            memset(observed_tick, 0, sizeof(observed_tick));
            target = 0;
#if TIMING_SOURCE == 1
            began = g_tick;
#elif TIMING_SOURCE == 2
            input.left_vertical = 1000; began = ticks;
#else
            mm = 0; began = tick;
#endif
        }
        for (step = 0; step < 250000; ++step)
        {
            if (d == 3) timing_delay = (step / 17 % 2) ? 30 : 100;
#if TIMING_SOURCE == 1
            ++g_tick;
            if (step % 100 == 0) {
                make_mailbox(mailbox, TEST_CMD_SPEED, session, sequence++, target, 500);
                submit_service(mailbox);
            } else app_hmi_control_service();
            assert(app_hmi_control_session() == session);
            duration = g_tick - began;
            done = 1;
            for (axis = 0; axis < 2; ++axis)
                if ((APP_SERVO_CONTROL_AXIS & (1 << axis)) &&
                    (g_snapshot[axis].busy || timing_rpm[axis] != target)) done = 0;
#elif TIMING_SOURCE == 2
            ++ticks; feed(&input); assert(status().authorized);
            duration = ticks - began; done = 1;
            for (axis = 0; axis < 2; ++axis)
                if ((APP_SERVO_CONTROL_AXIS & (1 << axis)) &&
                    (axes[axis].busy || timing_rpm[axis] != target)) done = 0;
#else
            if (step % 100 == 0) assert(speed(mm, 300));
            advance(1, true); assert(state.authorized);
            duration = tick - began; done = 1;
            for (axis = 0; axis < 2; ++axis)
                if ((APP_SERVO_CONTROL_AXIS & (1 << axis)) &&
                    (feedback_axis[axis].busy || state.commanded_rpm[axis] != target)) done = 0;
#endif
            for (axis = 0; axis < 2; ++axis) {
                const servo_snapshot_t *sample;
                int16_t submitted_rpm;
#if TIMING_SOURCE == 1
                sample = &g_snapshot[axis]; submitted_rpm = timing_rpm[axis];
#elif TIMING_SOURCE == 2
                sample = &axes[axis]; submitted_rpm = timing_rpm[axis];
#else
                sample = &feedback_axis[axis]; submitted_rpm = write_rpm[axis];
#endif
                if (!(APP_SERVO_CONTROL_AXIS & (1 << axis))) continue;
                if (observed_sequence[axis] == sample->speed_write_sequence && sample->busy)
                    assert(observed_rpm[axis] == submitted_rpm); /* active request immutable */
                if (observed_sequence[axis] != sample->speed_write_sequence) {
                    double rate = (phase ? APP_DESIGN_DECELERATION_MPS2 : APP_DESIGN_ACCELERATION_MPS2) *
                        app_motion_ramp_motor_rpm_per_mps();
                    if (observed_tick[axis] && step > 400)
                        assert(abs(submitted_rpm - observed_rpm[axis]) <=
                            rate * (duration - observed_tick[axis] + (d == 3 ? 70U : 0U)) / 1000.0 + 2.0);
                    observed_sequence[axis] = sample->speed_write_sequence;
                    observed_rpm[axis] = submitted_rpm; observed_tick[axis] = duration;
                }
            }
            if (curve && step % 10 == 0) {
                app_normal_ramp_status_t diagnostic;
#if TIMING_SOURCE == 1
                app_hmi_control_ramp_status(&diagnostic);
                fprintf(curve, "%lu,%d,%d,%d,%d,%d,%d\n", (unsigned long)duration, target, diagnostic.internal[0],
                    diagnostic.batch[0], diagnostic.batch[1], g_snapshot[0].actual_speed_rpm, g_snapshot[1].actual_speed_rpm);
#elif TIMING_SOURCE == 2
                app_remote_servo_ramp_status(&diagnostic);
                fprintf(curve, "%lu,%d,%d,%d,%d,%d,%d\n", (unsigned long)duration, target, diagnostic.internal[0],
                    diagnostic.batch[0], diagnostic.batch[1], axes[0].actual_speed_rpm, axes[1].actual_speed_rpm);
#else
                app_can_control_ramp_status(&diagnostic);
                fprintf(curve, "%lu,%d,%d,%d,%d,%d,%d\n", (unsigned long)duration, target, diagnostic.internal[0],
                    diagnostic.batch[0], diagnostic.batch[1], feedback_axis[0].actual_speed_rpm, feedback_axis[1].actual_speed_rpm);
#endif
            }
            if (done) break;
        }
        if (curve) fclose(curve);
        {
            double expected = fabs((double)initial) * 1000.0 /
                ((phase ? APP_DESIGN_DECELERATION_MPS2 : APP_DESIGN_ACCELERATION_MPS2) * app_motion_ramp_motor_rpm_per_mps());
            printf("source=%d axes=%d sync=%d per_axis_delay=%u jitter=%u phase=%u from_abs=%d target=%d last_ack_ms=%lu theory_ms=%.1f\n",
                TIMING_SOURCE, APP_SERVO_CONTROL_AXIS, APP_REMOTE_SERVO_SYNC_ENABLED,
                delays[d], d == 3, phase, abs(initial), target, (unsigned long)duration, expected);
            if (step == 250000 || fabs(duration - expected) > 500.0) ++failures;
        }
        }
        /* Inject a failure into a real outstanding asynchronous speed batch. */
        if (d == 1 && t == 0) {
            uint32_t attempts_before;
#if TIMING_SOURCE == 1
            uint32_t stopped_session = session;
            attempts_before = g_velocity_calls;
            make_mailbox(mailbox, TEST_CMD_SPEED, session, sequence++, initial, 500);
            submit_service(mailbox);
            for (step = 0; step < 500 && g_velocity_calls == attempts_before; ++step) { ++g_tick; app_hmi_control_service(); }
            assert(g_velocity_calls > attempts_before);
            for (axis = 0; axis < 2; ++axis) if (g_snapshot[axis].busy) {
                g_snapshot[axis].busy = 0;
                g_snapshot[axis].speed_write_completed = g_snapshot[axis].speed_write_sequence;
                g_snapshot[axis].speed_write_result = SERVO_NOT_READY;
            }
            app_hmi_control_service(); assert(!app_hmi_control_session());
            attempts_before = g_velocity_calls;
            make_mailbox(mailbox, TEST_CMD_SPEED, stopped_session, sequence++, initial, 500);
            submit_service(mailbox); assert(g_velocity_calls == attempts_before);
#elif TIMING_SOURCE == 2
            attempts_before = writes[0] + writes[1];
            input.left_vertical = joystick_for_rpm(initial);
            for (step = 0; step < 500 && writes[0] + writes[1] == attempts_before; ++step) { ++ticks; feed(&input); }
            assert(writes[0] + writes[1] > attempts_before);
            input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
            feed(&input); assert(!status().authorized);
            for (step = 0; step < 200; ++step) { ++ticks; feed(&input); }
            assert(!status().authorized); /* late completion cannot rearm */
#else
            attempts_before = calls[0] + calls[1];
            assert(speed((int16_t)(initial * app_can_control_rpm_mm_s(1, 0)), 300));
            for (step = 0; step < 500 && calls[0] + calls[1] == attempts_before; ++step) advance(1, true);
            assert(calls[0] + calls[1] > attempts_before);
            write_fail = true; failed_axes = 3;
            advance(200, true); assert(!state.authorized && state.state == CAN_CONTROL_FAULT);
            attempts_before = calls[0] + calls[1];
            assert(!speed(100, 300)); advance(100, true);
            assert(calls[0] + calls[1] == attempts_before);
#endif
            continue;
        }
        /* A real service stall is clipped once; the next service has no debt. */
        {
            app_normal_ramp_status_t before, after;
#if TIMING_SOURCE == 1
            app_hmi_control_ramp_status(&before);
            g_tick += 120; app_hmi_control_service();
            app_hmi_control_ramp_status(&after);
#elif TIMING_SOURCE == 2
            app_remote_servo_ramp_status(&before);
            ticks += 120; feed(&input);
            app_remote_servo_ramp_status(&after);
#else
            app_can_control_ramp_status(&before);
            tick += 120; app_can_control_link(42U, 1U, 7U, tick, true, true); cycle();
            app_can_control_ramp_status(&after);
#endif
            assert(after.clock.clipped == before.clock.clipped + 1);
            assert(after.clock.discarded_ms >= before.clock.discarded_ms + 70);
            before = after;
#if TIMING_SOURCE == 1
            ++g_tick; app_hmi_control_service(); app_hmi_control_ramp_status(&after);
#elif TIMING_SOURCE == 2
            ++ticks; feed(&input); app_remote_servo_ramp_status(&after);
#else
            advance(1, true); app_can_control_ramp_status(&after);
#endif
            assert(after.clock.integrated_ms == before.clock.integrated_ms + 1);
            assert(after.clock.discarded_ms == before.clock.discarded_ms);
        }
    }
    (void)argv;
    return argc > 1 ? 0 : failures != 0;
}
