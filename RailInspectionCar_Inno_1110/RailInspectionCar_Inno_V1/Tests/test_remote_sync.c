#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app_remote_servo_sync.h"
#include "app_config.h"

static servo_snapshot_t left, right;
static remote_sync_status_t sync_state;
static TickType_t now;

static void reset(TickType_t tick)
{
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    left.speed_valid = right.speed_valid = 1;
    left.fault_valid = right.fault_valid = 1;
    now = tick;
    app_remote_sync_reset(&sync_state, &left, &right, now);
}
static void sample(int16_t base, int16_t l, int16_t r, uint32_t dt)
{
    now += dt;
    left.actual_speed_rpm = l;
    right.actual_speed_rpm = r;
    left.speed_sample_tick = right.speed_sample_tick = now;
    left.speed_sequence++;
    right.speed_sequence++;
    app_remote_sync_update(&sync_state, &left, &right, base, 1, now);
}
static void test_batch_timing(void)
{
    unsigned i;
    float held;
    reset(0);
    for (i = 0U; i < 5U; ++i) {
        sample(200, 240, 200, 120);
        assert(sync_state.new_pair && sync_state.feedback_valid && sync_state.active);
        assert(sync_state.calc_interval_ms == 120 && sync_state.pair_gap_ms == 120);
        assert(sync_state.slew_dt_ms == 50 && sync_state.dt_ms == 0);
        assert(fabsf(sync_state.applied_correction - (float)(i + 1U)) < 0.0001f);
        /* Long batch gaps do not prove continuous zero or sustained over-error. */
        assert(sync_state.over_limit_duration_ms == 0);
    }
    assert(sync_state.new_pair_count == 5 && sync_state.slew_update_count == 5);
    assert(sync_state.zero_slew_fresh_count == 0 && sync_state.invalidation_count == 0);
    assert(sync_state.long_gap_count == 5 && sync_state.calc_interval_max_ms == 120);
    assert(sync_state.slew_dt_max_ms == 50);
    held = sync_state.applied_correction;
    now += 50;
    app_remote_sync_update(&sync_state, &left, &right, 200, 1, now);
    assert(!sync_state.new_pair && sync_state.feedback_valid);
    assert(sync_state.dt_ms == 0 && sync_state.slew_dt_ms == 0);
    assert(sync_state.applied_correction == held && sync_state.slew_update_count == 5);
    now += 49;
    app_remote_sync_update(&sync_state, &left, &right, 200, 1, now);
    assert(sync_state.feedback_valid && sync_state.age_ms[0] == 99);
    ++now;
    app_remote_sync_update(&sync_state, &left, &right, 200, 1, now);
    assert(!sync_state.feedback_valid && !sync_state.active && sync_state.applied_correction == 0.0f);
    assert(sync_state.invalidation_count == 1);
    ++now;
    app_remote_sync_update(&sync_state, &left, &right, 200, 1, now);
    assert(!sync_state.feedback_valid && sync_state.invalidation_count == 1);
    sample(200, 240, 200, 50);
    assert(sync_state.slew_dt_ms == 50 && sync_state.dt_ms == 0);
    assert(fabsf(sync_state.applied_correction - 1.0f) < 0.0001f);
    sample(200, 240, 200, 50);
    assert(sync_state.dt_ms == 50 && sync_state.applied_correction == 2.0f);
    /* An unobserved long interval does not reuse old error or catch up time. */
    sample(200, 200, 240, 1000);
    assert(sync_state.dt_ms == 0 && sync_state.slew_dt_ms == 50);
    assert(sync_state.requested_correction < 0.0f && sync_state.applied_correction == 1.0f);
    left.fault = 1U;
    app_remote_sync_update(&sync_state, &left, &right, 200, 1, ++now);
    assert(!sync_state.feedback_valid && sync_state.applied_correction == 0.0f);
    assert(sync_state.invalidation_count == 2);
    left.fault = 0U;
    sample(200, 240, 200, 50);
    assert(sync_state.slew_dt_ms == 50 && sync_state.dt_ms == 0);
    assert(sync_state.applied_correction == 1.0f);
    app_remote_sync_update(&sync_state, &left, &right, 200, 0, ++now);
    assert(!sync_state.active && sync_state.applied_correction == 0.0f);
    sample(200, 240, 200, 50);
    assert(sync_state.dt_ms == 0 && sync_state.slew_dt_ms == 50);
    /* The latest raw feedback age is an independent, strict boundary. */
    left.speed_age_ms = 100U;
    sample(200, 240, 200, 50);
    assert(!sync_state.feedback_valid && sync_state.raw_age_ms[0] == 100U);
    left.speed_age_ms = 101U;
    sample(200, 240, 200, 50);
    assert(!sync_state.feedback_valid);
    left.speed_age_ms = 99U;
    sample(200, 240, 200, 50);
    assert(sync_state.feedback_valid && sync_state.dt_ms == 0 && sync_state.slew_dt_ms == 50);
    reset(UINT32_MAX - 20U);
    sample(200, 240, 200, 120);
    assert(sync_state.feedback_valid && sync_state.calc_interval_ms == 120);
    assert(sync_state.dt_ms == 0 && sync_state.slew_dt_ms == 50);
    puts("PASS 120ms fresh batches, held samples, strict expiry, recovery, faults and bounded long-gap P slew");
}
int main(void)
{
    unsigned i;
    reset(0);
    sample(100, 106, 100, 50);
    assert(sync_state.integer_correction == 1);
    assert(sync_state.effective_error == 5);
    sample(100, 101, 100, 50);
    assert(sync_state.integer_correction == 0);
    sample(-100, -106, -100, 50);
    assert(sync_state.integer_correction == -1);
    /* 同一个补偿量对正反转都降低领先轴速度的绝对值。 */
    assert(-100 - sync_state.integer_correction == -99);
    sample(100, 104, 100, 50);
    assert(sync_state.integer_correction == 0);
    sample(100, 104, 100, 50);
    assert(sync_state.integer_correction == 1);
    sample(100, 96, 100, 50);
    assert(sync_state.integer_correction == 0);
    sample(100, 96, 100, 50);
    assert(sync_state.integer_correction == -1);
    sample(9, 30, 0, 50);
    assert(sync_state.integer_correction == 0);
    sample(APP_OPERATION_MAX_MOTOR_RPM, APP_OPERATION_MAX_MOTOR_RPM + 10,
        APP_OPERATION_MAX_MOTOR_RPM, 50);
    assert(sync_state.integer_correction == 0 && sync_state.state == REMOTE_SYNC_LIMITED);
    for (i = 0; i < 10; i++) sample(100, 140, 100, 50);
    assert(sync_state.integer_correction == 5);
    assert(sync_state.over_limit_duration_ms >= 300);
    assert(sync_state.trip == APP_REMOTE_SERVO_SYNC_TRIP_ENABLED);
    app_remote_sync_update(&sync_state, &left, &right, 9, 1, now + 1);
    assert(sync_state.integer_correction == 0);
    /* 重复样本不能重复消费，也不能增加超差持续时间。 */
    now += 50;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(!sync_state.new_pair && sync_state.reject_reason == REMOTE_PAIR_REPEAT);
    left.speed_sequence++;
    now += 50;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(!sync_state.new_pair);
    reset(now);
    left.speed_sequence = right.speed_sequence = 1;
    left.speed_sample_tick = now;
    right.speed_sample_tick = now - 41;
    now += 50;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.reject_reason == REMOTE_PAIR_SKEW);
    assert(sync_state.over_limit_duration_ms == 0);
    right.speed_sample_tick = left.speed_sample_tick;
    left.speed_age_ms = 100;
    now += 50;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.reject_reason == REMOTE_PAIR_AGE);
    left.speed_age_ms = 0;
    sample(100, 200, 100, 1000);
    assert(sync_state.dt_ms == 0);
    sample(100, 200, 100, 50);
    assert(sync_state.dt_ms == 50);
    reset(UINT32_MAX - 20U);
    sample(100, 106, 100, 50);
    assert(sync_state.integer_correction == 1 && sync_state.skew_ms == 0);
    sample(100, 0, 0, 50);
    sample(100, 0, 0, 50);
    assert(sync_state.zero_pairs == 2);
    sample(100, 1, 0, 50);
    assert(sync_state.zero_pairs == 0);
    app_remote_sync_update(&sync_state, &left, &right, 100, 0, now + 50);
    assert(!sync_state.active && sync_state.integer_correction == 0);
    /* 单侧等待不消费，随后另一侧在边界时间内到达才配对。 */
    reset(0);
    now = 50;
    left.speed_sequence = 1;
    left.speed_sample_tick = now;
    left.actual_speed_rpm = 106;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.reject_reason == REMOTE_PAIR_WAITING);
    assert(sync_state.pending_count[0] == 1 && sync_state.pair_valid_count == 0);
    now = 100;
    right.speed_sequence = 1;
    right.speed_sample_tick = 90;
    right.actual_speed_rpm = 100;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.new_pair && sync_state.skew_ms == 40);
    assert(sync_state.pair_valid_count == 1 && sync_state.pair_attempts == 2);
    now += 50;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(!sync_state.new_pair && sync_state.pair_reject_repeat_count == 1);
    /* 从生产应答历史中选最近时间的一对，不只观察最新快照。 */
    reset(0);
    now = 50;
    left.speed_sequence = right.speed_sequence = 2;
    left.speed_sample_tick = 45;
    right.speed_sample_tick = 49;
    for (i = 0; i < 2; i++)
    {
        left.speed_samples[i].valid = right.speed_samples[i].valid = 1;
        left.speed_samples[i].speed_sequence = right.speed_samples[i].speed_sequence = i + 1;
        left.speed_samples[i].sample_tick = i == 0 ? 20 : 45;
        right.speed_samples[i].sample_tick = i == 0 ? 20 : 49;
        left.speed_samples[i].actual_speed_rpm = 106;
        right.speed_samples[i].actual_speed_rpm = 100;
    }
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.new_pair && sync_state.sequence[0] == 1);
    assert(sync_state.skew_ms == 0 && sync_state.pending_count[0] == 1);
    now = 100;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.new_pair && sync_state.sequence[0] == 2);
    assert(sync_state.pair_valid_count == 2);
    /* 41毫秒错时丢弃较旧轴，保留另一轴用于恢复。 */
    reset(0);
    now = 50;
    left.speed_sequence = right.speed_sequence = 1;
    left.speed_sample_tick = 1;
    right.speed_sample_tick = 42;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.reject_reason == REMOTE_PAIR_SKEW);
    assert(sync_state.pending_count[0] == 0 && sync_state.pending_count[1] == 1);
    now = 100;
    left.speed_sequence = 2;
    left.speed_sample_tick = 60;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.new_pair && sync_state.state == REMOTE_SYNC_TRACKING);
    /* 等错时优先最新样本，四项缓存饱和后只保留最近反馈。 */
    reset(0);
    for (i = 1; i <= 6; i++)
    {
        now = i * 5;
        left.speed_sequence = right.speed_sequence = i;
        left.speed_sample_tick = right.speed_sample_tick = now;
        app_remote_sync_collect(&sync_state, &left, &right, now);
    }
    assert(sync_state.pending_count[0] == 4 && sync_state.pending_count[1] == 4);
    now = 50;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.new_pair && sync_state.sequence[0] == 6);
    assert(sync_state.pending_count[0] == 0 && sync_state.pending_count[1] == 0);
    /* 序号及时间戳同时回绕，仍然消费真正的新反馈。 */
    left.speed_sequence = right.speed_sequence = UINT32_MAX;
    now = UINT32_MAX - 20U;
    app_remote_sync_reset(&sync_state, &left, &right, now);
    sample(100, 106, 100, 50);
    assert(sync_state.new_pair && sync_state.sequence[0] == 0);
    /* 二十毫秒错时以及单侧等待可连续恢复，统计不按服务次数累加。 */
    reset(0);
    now = 10;
    left.speed_sequence = 1;
    left.speed_sample_tick = 10;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.pair_attempts == 0);
    now = 50;
    right.speed_sequence = 1;
    right.speed_sample_tick = 30;
    app_remote_sync_update(&sync_state, &left, &right, 100, 1, now);
    assert(sync_state.new_pair && sync_state.skew_ms == 20);
    assert(sync_state.pair_attempts == 1 && sync_state.pair_valid_count == 1);
    reset(0U); sample(200,200,230,50U);
    left.feedback_degraded=1U;
    sample(200,200,300,50U);
    assert(!sync_state.trip && !sync_state.active && sync_state.integer_correction==0);
    assert(sync_state.over_limit_duration_ms==0U);
    left.feedback_degraded=0U;
    sample(200,200,200,50U);
    assert(!sync_state.trip);
    test_batch_timing();
    puts("Remote P synchronization algorithm tests passed");
    return 0;
}
