#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "app_position_monitor.h"
#include "app_remote_servo.h"
#include "motion_control.h"
#include "servo_debug_control.h"
#include "servo_pulse.h"
#include "task.h"

static TickType_t now;
static servo_snapshot_t feedback[2];
static remote_servo_status_t remote;
static servo_debug_status_t debug;
static motion_state_t motion;
static uint8_t startup_ready;
static uint32_t pulse[2];
static position_monitor_status_t status;
bool app_cli_servo_session_active(void) { return false; }
TickType_t xTaskGetTickCount(void) { return now; }
bool aimtor_startup_stop_ready(void) { return startup_ready != 0U; }
bool aimtor_watchdog_recovery_ready(void) { return startup_ready != 0U; }
bool aimtor_boot_zero_ready(void) { return startup_ready != 0U; }
bool aimtor_startup_hold_ready(void) { return startup_ready != 0U; }
bool aimtor_motion_allowed(void) { return startup_ready != 0U; }
bool aimtor_get_snapshot(servo_axis_t axis, servo_snapshot_t *out)
{
    *out = feedback[axis];
    out->position_age_ms = now - out->position_sample_tick;
    return true;
}
bool aimtor_get_snapshot_pair(servo_snapshot_t *left, servo_snapshot_t *right)
{
    return aimtor_get_snapshot(SERVO_AXIS_LEFT, left) &&
           aimtor_get_snapshot(SERVO_AXIS_RIGHT, right);
}
bool app_remote_servo_get_status(remote_servo_status_t *out)
{
    *out = remote;
    return true;
}
bool aimtor_get_position_monitor_pair(servo_position_monitor_axis_t *left,
                                     servo_position_monitor_axis_t *right)
{
    unsigned axis;
    for (axis = 0U; axis < 2U; ++axis) {
        servo_position_monitor_axis_t *out = axis ? right : left;
        const servo_snapshot_t *in = &feedback[axis];
        memcpy(out->position_samples, in->position_samples, sizeof(out->position_samples));
        out->position_age_ms = now - in->position_sample_tick;
        out->position_sequence = in->position_sequence;
        out->position_generation = in->position_generation;
        out->speed_age_ms = in->speed_age_ms;
        out->actual_speed_rpm = in->actual_speed_rpm;
        out->online = in->online; out->fault = in->fault;
        out->parameter_verified = in->parameter_verified;
        out->control_mode = in->control_mode;
        out->speed_valid = in->speed_valid; out->position_valid = in->position_valid;
        out->busy = in->busy;
    }
    return true;
}
bool app_remote_servo_get_position_gate(remote_position_gate_t *out)
{
    out->authorization_generation = remote.authorization_generation;
    out->state = remote.state; out->authorized = remote.authorized;
    out->hold_stopping = remote.hold_stopping; out->estop_latched = remote.estop_latched;
    out->control_axis_mask = remote.control_axis_mask;
    return true;
}
bool servo_debug_get_status(servo_debug_status_t *out)
{
    *out = debug;
    return true;
}
motion_state_t motion_get_state(void) { return motion; }
uint8_t motion_get_pulse_active_mask(uint8_t mask) { (void)mask; return 0U; }
uint8_t motion_get_pulse_completion_pending_mask(uint8_t mask) { (void)mask; return 0U; }
uint32_t servo_pulse_get_frequency(servo_axis_t axis) { return pulse[axis]; }

/* 包含生产模块，额外检查64位边界，不替换计算算法。 */
#include "../APP/app_position_monitor.c"

static void service(void)
{
    app_position_monitor_service();
    assert(app_position_monitor_get_status(&status));
}

static void reset(TickType_t tick)
{
    now = tick;
    memset(feedback, 0, sizeof(feedback));
    memset(&remote, 0, sizeof(remote));
    memset(&debug, 0, sizeof(debug));
    memset(pulse, 0, sizeof(pulse));
    motion = MOTION_READY;
    startup_ready = 1U;
    remote.control_axis_mask = APP_SERVO_CONTROL_AXIS;
    remote.authorization_generation = 1U;
    for (unsigned axis = 0; axis < 2; axis++)
    {
        feedback[axis].online = 1U;
        feedback[axis].speed_valid = 1U;
        feedback[axis].fault_valid = 1U;
        feedback[axis].position_valid = 1U;
        feedback[axis].parameter_verified = 1U;
        feedback[axis].control_mode = SERVO_CONTROL_RS485_VELOCITY;
    }
    app_position_monitor_init();
    service();
}

static void push_sample(unsigned axis, int32_t raw, TickType_t tick)
{
    servo_position_sample_t *sample;
    servo_snapshot_t *s = &feedback[axis];
    now = tick;
    s->actual_position = raw;
    s->position_sample_tick = tick;
    s->position_sequence++;
    s->position_valid = 1U;
    memmove(s->position_samples, s->position_samples + 1,
            3 * sizeof(s->position_samples[0]));
    sample = &s->position_samples[3];
    sample->actual_position = raw;
    sample->position_sequence = s->position_sequence;
    sample->position_generation = s->position_generation;
    sample->sample_tick = tick;
    sample->valid = 1U;
}

static void sample(unsigned axis, int32_t raw, TickType_t tick)
{
    push_sample(axis, raw, tick);
    service();
}

static void both(int32_t l, int32_t r, TickType_t tick, uint32_t skew)
{
    sample(0, l, tick);
    sample(1, r, tick + skew);
}

int main(void)
{
    unsigned selected = app_config_servo_axis_selected(0) ? 0U : 1U;
    reset(0);
    assert(status.enabled == APP_POSITION_MONITOR_ENABLED);
    assert(status.source_mask == APP_SERVO_QUERY_MODE);
    assert(!app_position_monitor_get_status(NULL));
#if (APP_POSITION_MONITOR_ENABLED == 0U)
    both(10000, 10000, 1000, 10);
    both(20000, 20000, 2000, 10);
    assert(!status.vehicle_valid && !status.pair_valid);
    assert(!status.axis[selected].boot_path_counts);
    assert(!app_position_monitor_request_trip_reset());
    puts("PASS position disabled: no accumulation, no reset, no control writes");
#else
    const double selected_diameter_mm = selected == 0U ?
        APP_POSITION_LEFT_WHEEL_DIAMETER_MM : APP_POSITION_RIGHT_WHEEL_DIAMETER_MM;
    const double selected_counts_per_rev = selected == 0U ?
        APP_POSITION_LEFT_COUNTS_PER_REV : APP_POSITION_RIGHT_COUNTS_PER_REV;
    const double selected_gear_num = selected == 0U ?
        APP_POSITION_LEFT_GEAR_NUM : APP_POSITION_RIGHT_GEAR_NUM;
    const double selected_gear_den = selected == 0U ?
        APP_POSITION_LEFT_GEAR_DEN : APP_POSITION_RIGHT_GEAR_DEN;
    assert(status.axis[selected].state == POSITION_MONITOR_WAITING);
    assert(status.axis[selected].age_ms == UINT32_MAX);
    /* 启动旧缓存不计算；未授权仍采集净位移与采样累计行程。 */
    startup_ready = 0;
    both(30000, 30000, 500, 0);
    startup_ready = 1;
    service();
    both(40000, 40000, 1000, 10);
    assert(status.axis[selected].boot_net_counts == 0);
    both(50000, 50000, 2000, 10);
    assert(status.axis[selected].boot_net_counts == 10000);
    /* 位移按当前位置监控的轮径、编码器分辨率和齿轮比换算；
     * 这里验证生产配置，而不是使用整轮周长（整轮周长不包含 8/3 齿轮比）。 */
    assert(fabs(status.axis[selected].boot_net_mm -
                (10000.0 * 3.141592653589793 * selected_diameter_mm *
                 selected_gear_den / (selected_counts_per_rev * selected_gear_num))) < .001);
    both(40000, 40000, 3000, 10);
    assert(status.axis[selected].boot_net_counts == 0);
    assert(status.axis[selected].boot_path_counts == 20000);
    assert(!status.pair_valid);
    assert(status.axis[1U - selected].selected ||
           status.axis[1U - selected].boot_path_counts == 0);
    for (unsigned mode = SERVO_CONTROL_PULSE_POSITION;
         mode <= SERVO_CONTROL_RS485_SEGMENT_POSITION; mode++)
    {
        feedback[selected].control_mode = (servo_control_mode_t)mode;
        both(41000 + (int32_t)mode * 1000, 41000 + (int32_t)mode * 1000,
             3000 + mode * 1000, 10);
        assert(status.axis[selected].valid);
    }
    assert(app_position_monitor_request_trip_reset());
    service();
    assert(status.trip_reset_count == 1 && status.axis[selected].trip_path_counts == 0);
    assert(status.axis[selected].boot_path_counts != 0);
    remote.authorized = 1;
    assert(!app_position_monitor_request_trip_reset());
    remote.authorized = 0;
    feedback[selected].actual_speed_rpm = 1;
    assert(!app_position_monitor_request_trip_reset());
    feedback[selected].actual_speed_rpm = 0;
    feedback[selected].speed_age_ms = 100;
    assert(!app_position_monitor_request_trip_reset());
    feedback[selected].speed_age_ms = 0;
    feedback[selected].busy = 1;
    assert(!app_position_monitor_request_trip_reset());
    feedback[selected].busy = 0;
    debug.state = SERVO_DEBUG_PREPARING;
    assert(!app_position_monitor_request_trip_reset());
    debug.state = SERVO_DEBUG_IDLE;
    pulse[selected] = 1000;
    assert(!app_position_monitor_request_trip_reset());
    pulse[selected] = 0;
    assert(app_position_monitor_request_trip_reset());
    remote.authorized = 1;
    service();
    assert(status.trip_reset_rejected == 1);

    reset(0xFFFFFC00U);
    both(INT32_MAX - 99, INT32_MAX - 99, 0xFFFFFC10U, 0);
    both(INT32_MIN + 100, INT32_MIN + 100, 100U, 0);
    assert(status.axis[selected].boot_net_counts == 200);
    assert(!status.axis[selected].incomplete);
    /* 序号回绕按模运算接受一次，不把重复样本再次计算。 */
    feedback[selected].position_sequence = UINT32_MAX;
    s_history[selected].sequence = UINT32_MAX;
    memset(feedback[selected].position_samples, 0,
           sizeof(feedback[selected].position_samples));
    sample(selected, INT32_MIN + 200, 200U);
    assert(status.axis[selected].boot_net_counts == 300);
    service();
    assert(status.axis[selected].boot_net_counts == 300);
    s_status.axis[selected].boot_net_counts = 2147483648LL;
    s_status.axis[selected].boot_path_counts = 4294967296ULL;
    sample(selected, INT32_MIN + 300, 300U);
    assert(status.axis[selected].boot_net_counts == 2147483748LL);
    assert(status.axis[selected].boot_path_counts == 4294967396ULL);
    now += 1501;
    service();
    assert(!status.axis[selected].valid && status.axis[selected].incomplete);
    {
        int64_t saved = status.axis[selected].boot_net_counts;
        sample(selected, 0, now + 1);
        assert(status.axis[selected].valid && status.axis[selected].boot_net_counts == saved);
        sample(selected, 100, now + 100);
        assert(status.axis[selected].boot_net_counts == saved + 100);
        feedback[selected].position_generation++;
        feedback[selected].position_valid = 0;
        service();
        sample(selected, 0, now + 100);
        assert(status.axis[selected].boot_net_counts == saved + 100);
        sample(selected, 100000000, now + 100);
        assert(status.axis[selected].boot_net_counts == saved + 100);
        assert(!status.axis[selected].valid &&
               status.axis[selected].state == POSITION_MONITOR_INVALID);
        assert(status.axis[selected].incomplete);
        feedback[selected].online = 0;
        service();
        assert(!status.axis[selected].valid);
        feedback[selected].online = 1;
        sample(selected, 100000100, now + 100);
        assert(status.axis[selected].boot_net_counts == saved + 100);
    }
    /* 四个缓存按序消费；未知序号区间仅锚定且不重复记录丢失次数。 */
    reset(0);
    sample(selected, 0, 100);
    for (unsigned i = 1; i <= 4; i++)
        push_sample(selected, (int32_t)i * 100, 100 + i * 100);
    service();
    assert(status.axis[selected].boot_net_counts == 400);
    feedback[selected].position_sequence += 2;
    sample(selected, 10000, 600);
    assert(status.axis[selected].boot_net_counts == 400);
    assert(status.axis[selected].lost_intervals == 1);
    service();
    assert(status.axis[selected].lost_intervals == 1);
    /* 新序号与旧缓存中的序号重名时也只能选择最新样本。 */
    feedback[selected].position_sequence = 1;
    push_sample(selected, 20000, 700);
    service();
    assert(status.axis[selected].boot_net_counts == 400);
    assert(status.axis[selected].raw_position == 20000);
    sample(selected, 20100, 800);
    assert(status.axis[selected].boot_net_counts == 500);
    assert(app_position_monitor_request_trip_reset());
    service();
    assert(status.axis[selected].trip_anchor_pending);
    sample(selected, 20200, 900);
    assert(!status.axis[selected].trip_anchor_pending &&
           status.axis[selected].trip_net_counts == 0);
    assert(status.axis[selected].boot_net_counts == 600);
    sample(selected, 20300, 1000);
    assert(status.axis[selected].trip_net_counts == 100);

#if (APP_SERVO_CONTROL_AXIS == 3U) && (APP_SERVO_READONLY_TEST == 0U)
    const double left_position_scale =
        3.141592653589793 * APP_POSITION_LEFT_WHEEL_DIAMETER_MM *
        APP_POSITION_LEFT_GEAR_DEN /
        (APP_POSITION_LEFT_COUNTS_PER_REV * (double)APP_POSITION_LEFT_GEAR_NUM);
    const double right_position_scale =
        3.141592653589793 * APP_POSITION_RIGHT_WHEEL_DIAMETER_MM *
        APP_POSITION_RIGHT_GEAR_DEN /
        (APP_POSITION_RIGHT_COUNTS_PER_REV * (double)APP_POSITION_RIGHT_GEAR_NUM);
    reset(0);
    remote.authorized = 1;
    remote.state = REMOTE_SERVO_ARMED;
    service();
    both(1000, 5000, 1000, 200);
    assert(status.pair_valid && !status.rate_valid && status.difference_counts == 0);
    assert(status.sample_skew_ms == 200);
    both(11000, 14000, 2000, 200);
    assert(status.pair_valid && status.difference_counts == 1000 && status.rate_valid);
    assert(fabs(status.difference_mm -
                (10000.0 * left_position_scale -
                 9000.0 * right_position_scale)) < .001);
    assert(status.raw_difference_counts == -3000);
    {
        uint32_t pairs = status.pair_count;
        service();
        assert(status.pair_count == pairs);
        sample(0, 21000, 3000);
        assert(status.pair_count == pairs);
        sample(1, 24000, 3201);
        assert(status.pair_count == pairs);
        both(31000, 34000, 4000, 20);
        assert(status.pair_count == pairs + 1);
    }
    remote.authorized = 0;
    service();
    assert(!status.pair_valid && status.difference_counts == 1000);
    remote.authorized = 1;
    remote.authorization_generation++;
    service();
    both(32000, 35000, 5000, 0);
    assert(status.pair_valid && status.difference_counts == 0 && !status.rate_valid);
    feedback[0].control_mode = SERVO_CONTROL_PULSE_POSITION;
    service();
    assert(!status.pair_valid);
    feedback[0].control_mode = SERVO_CONTROL_RS485_VELOCITY;
    service();
    both(33000, 36000, 6000, 0);
    assert(status.pair_valid);
    feedback[0].speed_age_ms = 100;
    service();
    assert(!status.skew_estimate_valid);
    feedback[0].speed_age_ms = 0;
    feedback[0].actual_speed_rpm = 600;
    both(34000, 37000, 7000, 200);
    assert(status.skew_estimate_valid);
    {
        double left_rate = 600.0 * APP_POSITION_LEFT_COUNTS_PER_REV * left_position_scale;
        double right_rate = 600.0 * APP_POSITION_RIGHT_COUNTS_PER_REV * right_position_scale;
        double expected_skew = (left_rate > right_rate ? left_rate : right_rate) *
                               200.0 / 60000.0;
        assert(fabs(status.skew_estimate_mm - expected_skew) < .001);
    }
    remote.estop_latched = 1;
    service();
    assert(!status.pair_valid && status.axis[0].boot_path_counts != 0);
    /* 一侧最新样本刷新不能延长旧配对中较早样本的有效期。 */
    reset(0);
    remote.authorized = 1;
    remote.state = REMOTE_SERVO_RUNNING;
    service();
    both(0, 0, 1000, 200);
    sample(0, 100, 2000);
    now = 2501;
    service();
    assert(status.vehicle_valid && !status.pair_valid && !status.rate_valid);
    reset(0xFFFFFC00U);
    remote.authorized = 1;
    remote.state = REMOTE_SERVO_RUNNING;
    service();
    both(0, 0, 0xFFFFFC10U, 200);
    assert(status.pair_valid && status.sample_skew_ms == 200);
    both(1000, 900, 100U, 200);
    assert(status.pair_valid && status.difference_counts == 100);
#else
    reset(0);
    remote.authorized = 1;
    remote.state = REMOTE_SERVO_RUNNING;
    both(0, 0, 1000, 0);
    both(1000, 900, 2000, 0);
    assert(!status.pair_valid);
    assert(status.axis[selected].boot_path_counts > 0);
#endif
    puts("PASS position: full-mode mileage, resets, wraps, discontinuity, pairing and gates");
#endif
    return 0;
}
