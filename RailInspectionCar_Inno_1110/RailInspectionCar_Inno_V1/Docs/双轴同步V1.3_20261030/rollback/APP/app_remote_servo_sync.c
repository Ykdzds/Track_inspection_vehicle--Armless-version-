#include "app_remote_servo_sync.h"
#include "app_config.h"
#include "task.h"
#include <string.h>

static float sync_abs(float value) { return value < 0.0f ? -value : value; }
static uint32_t sync_ms(TickType_t ticks)
{
    return (uint32_t)((uint64_t)ticks * 1000U / configTICK_RATE_HZ);
}
static float sync_clamp(float value, float limit)
{
    return value > limit ? limit : value < -limit ? -limit : value;
}

static void sync_drop(remote_sync_status_t *s, unsigned axis, unsigned count)
{
    unsigned remaining = s->pending_count[axis] - count;
    memmove(s->pending[axis], s->pending[axis] + count,
            remaining * sizeof(servo_speed_sample_t));
    s->pending_count[axis] = (uint8_t)remaining;
}

static void sync_collect_axis(remote_sync_status_t *s, unsigned axis,
                              const servo_snapshot_t *snapshot, TickType_t now)
{
    unsigned i;
    servo_speed_sample_t sample;
    /* 回绕后仍只接受比上次接收更新的序号，不重新使用已消费样本。 */
    for (i = 0; i <= SERVO_SPEED_SAMPLE_CAPACITY; i++)
    {
        if (i < SERVO_SPEED_SAMPLE_CAPACITY)
            sample = snapshot->speed_samples[i];
        else
        {
            /* 兼容只提供最新快照的调用方及主机桩。 */
            sample.actual_speed_rpm = snapshot->actual_speed_rpm;
            sample.speed_sequence = snapshot->speed_sequence;
            sample.sample_tick = snapshot->speed_sample_tick;
            sample.valid = snapshot->speed_valid;
        }
        if (!sample.valid ||
            (int32_t)(sample.speed_sequence - s->received_sequence[axis]) <= 0)
            continue;
        s->received_sequence[axis] = sample.speed_sequence;
        if (sync_ms(now - sample.sample_tick) >= APP_REMOTE_SERVO_SYNC_MAX_AGE_MS)
            continue;
        if (s->pending_count[axis] == SERVO_SPEED_SAMPLE_CAPACITY)
            sync_drop(s, axis, 1U);
        s->pending[axis][s->pending_count[axis]++] = sample;
    }
    while (s->pending_count[axis] &&
           sync_ms(now - s->pending[axis][0].sample_tick) >=
           APP_REMOTE_SERVO_SYNC_MAX_AGE_MS)
        sync_drop(s, axis, 1U);
}

void app_remote_sync_collect(remote_sync_status_t *s,
                             const servo_snapshot_t *left,
                             const servo_snapshot_t *right, TickType_t now)
{
    sync_collect_axis(s, 0U, left, now);
    sync_collect_axis(s, 1U, right, now);
}

static uint8_t sync_pick_pair(remote_sync_status_t *s, TickType_t now,
                             servo_speed_sample_t *left,
                             servo_speed_sample_t *right)
{
    unsigned i, j, best_l = 0U, best_r = 0U;
    uint32_t best = UINT32_MAX, best_age = UINT32_MAX;
    for (i = 0U; i < s->pending_count[0]; i++)
        for (j = 0U; j < s->pending_count[1]; j++)
        {
            TickType_t a = s->pending[0][i].sample_tick;
            TickType_t b = s->pending[1][j].sample_tick;
            TickType_t d = a - b, reverse = b - a;
            uint32_t skew = sync_ms(d < reverse ? d : reverse);
            uint32_t age_l = sync_ms(now - a), age_r = sync_ms(now - b);
            uint32_t age = age_l > age_r ? age_l : age_r;
            /* 同样错时时选择较新的配对，丢弃更早样本而不追补历史。 */
            if (skew < best || (skew == best && age < best_age))
            {
                best = skew;
                best_age = age;
                best_l = i;
                best_r = j;
            }
        }
    if (best == UINT32_MAX)
    {
        s->reject_reason = s->pending_count[0] || s->pending_count[1] ?
                           REMOTE_PAIR_WAITING : REMOTE_PAIR_REPEAT;
        return 0U;
    }
    s->skew_ms = best;
    s->pair_last_skew_ms = best;
    if (best > s->pair_max_skew_ms) s->pair_max_skew_ms = best;
    if (best > APP_REMOTE_SERVO_SYNC_MAX_SKEW_MS)
    {
        uint32_t age_l = sync_ms(now - s->pending[0][0].sample_tick);
        uint32_t age_r = sync_ms(now - s->pending[1][0].sample_tick);
        sync_drop(s, age_l >= age_r ? 0U : 1U, 1U);
        s->reject_reason = REMOTE_PAIR_SKEW;
        return 0U;
    }
    *left = s->pending[0][best_l];
    *right = s->pending[1][best_r];
    sync_drop(s, 0U, best_l + 1U);
    sync_drop(s, 1U, best_r + 1U);
    s->reject_reason = REMOTE_PAIR_OK;
    return 1U;
}

void app_remote_sync_reset(remote_sync_status_t *s,
                           const servo_snapshot_t *left,
                           const servo_snapshot_t *right, TickType_t now)
{
    memset(s, 0, sizeof(*s));
    s->enabled = APP_REMOTE_SERVO_SYNC_ENABLED;
    s->sequence[0] = left->speed_sequence;
    s->sequence[1] = right->speed_sequence;
    s->received_sequence[0] = left->speed_sequence;
    s->received_sequence[1] = right->speed_sequence;
    s->calculation_tick = now;
    s->pair_tick = now;
}

void app_servo_sync_policy_default(servo_sync_policy_t *p)
{
    p->maximum_rpm = APP_OPERATION_MAX_MOTOR_RPM;
    p->kp = APP_REMOTE_SERVO_SYNC_KP;
    p->correction_limit = APP_REMOTE_SERVO_SYNC_LIMIT_RPM; p->correction_rate = APP_REMOTE_SERVO_SYNC_RATE_RPM_S;
    p->deadband = APP_REMOTE_SERVO_SYNC_DEADBAND_RPM; p->minimum_rpm = APP_REMOTE_SERVO_SYNC_MIN_RPM;
    p->error_rpm = APP_REMOTE_SERVO_SYNC_ERROR_RPM; p->error_ms = APP_REMOTE_SERVO_SYNC_ERROR_MS;
    p->trip_enabled = APP_REMOTE_SERVO_SYNC_TRIP_ENABLED;
}

void app_remote_sync_update(remote_sync_status_t *s,
    const servo_snapshot_t *left, const servo_snapshot_t *right,
    int16_t base, uint8_t gate, TickType_t now)
{
    servo_sync_policy_t policy;
    app_servo_sync_policy_default(&policy);
    app_servo_sync_update(s, left, right, base, gate, gate, now, &policy);
}

void app_servo_sync_update(remote_sync_status_t *s,
                            const servo_snapshot_t *left,
                            const servo_snapshot_t *right,
                            int16_t base, uint8_t gate, uint8_t compensate, TickType_t now,
                            const servo_sync_policy_t *policy)
{
    TickType_t skew = left->speed_sample_tick - right->speed_sample_tick;
    TickType_t reverse_skew = right->speed_sample_tick - left->speed_sample_tick;
    uint32_t elapsed = sync_ms(now - s->calculation_tick);
    uint32_t gap = sync_ms(now - s->pair_tick);
    float requested = 0.0f;
    float step;
    float magnitude = sync_abs((float)base);
    int32_t error;
    servo_speed_sample_t paired_left, paired_right;

    if (left->feedback_degraded || right->feedback_degraded)
    {
        s->active = s->trip = 0U;
        s->applied_correction = 0.0f; s->integer_correction = 0;
        s->calculation_tick = now;
        s->over_limit = 0U; s->over_limit_duration_ms = 0U; s->over_tick = now;
        return;
    }
    app_remote_sync_collect(s, left, right, now);
    s->new_pair = 0U;
    s->age_ms[0] = left->speed_age_ms;
    s->age_ms[1] = right->speed_age_ms;
    s->skew_ms = sync_ms(skew < reverse_skew ? skew : reverse_skew);
    s->pair_valid = 0U;
    s->active = 0U;
    s->trip = 0U;
    /* 量程及低速保护每次执行，不等待下一个补偿计算周期。 */
    s->effective_limit = policy->correction_limit;
    if (s->effective_limit > policy->maximum_rpm - magnitude)
        s->effective_limit = policy->maximum_rpm - magnitude;
    if (s->effective_limit < 0.0f) s->effective_limit = 0.0f;
    if (s->effective_limit > magnitude) s->effective_limit = magnitude;
    if (magnitude < policy->minimum_rpm || !gate || !compensate)
        s->applied_correction = 0.0f;
    s->applied_correction = sync_clamp(s->applied_correction, s->effective_limit);
    s->integer_correction = (int16_t)(s->applied_correction >= 0.0f ?
        s->applied_correction + 0.5f : s->applied_correction - 0.5f);
    if (!gate)
        s->reject_reason = REMOTE_PAIR_GATE;
    else if (!left->speed_valid || !right->speed_valid ||
             !left->fault_valid || !right->fault_valid ||
             left->speed_age_ms >= APP_REMOTE_SERVO_SYNC_MAX_AGE_MS ||
             right->speed_age_ms >= APP_REMOTE_SERVO_SYNC_MAX_AGE_MS ||
             left->fault_age_ms >= APP_REMOTE_SERVO_SYNC_MAX_AGE_MS ||
             right->fault_age_ms >= APP_REMOTE_SERVO_SYNC_MAX_AGE_MS)
        s->reject_reason = REMOTE_PAIR_AGE;
    else
        s->reject_reason = REMOTE_PAIR_OK;

    if (gap > APP_REMOTE_SERVO_SYNC_MAX_AGE_MS)
    {
        s->over_limit = 0U;
        s->over_limit_duration_ms = 0U;
        s->zero_pairs = 0U;
    }
    if (elapsed < APP_REMOTE_SERVO_SYNC_PERIOD_MS)
        return;
    s->calculation_tick = now;
    if (gate)
    {
        s->pair_attempts++;
        if (s->reject_reason == REMOTE_PAIR_OK)
            (void)sync_pick_pair(s, now, &paired_left, &paired_right);
        if (s->reject_reason == REMOTE_PAIR_OK)
            s->pair_valid_count++;
        else
        {
            s->pair_reject_count++;
            if (s->reject_reason == REMOTE_PAIR_REPEAT) s->pair_reject_repeat_count++;
            if (s->reject_reason == REMOTE_PAIR_SKEW) s->pair_reject_skew_count++;
            if (s->reject_reason == REMOTE_PAIR_AGE) s->pair_reject_age_count++;
        }
    }
    else
    {
        /* 撤销门禁后不能留下恢复时可消费的旧样本。 */
        s->pending_count[0] = s->pending_count[1] = 0U;
    }
    /* 长间隔重建基准，不能一次补偿整个停调时间。 */
    s->dt_ms = gap > APP_REMOTE_SERVO_SYNC_MAX_AGE_MS ?
               0U : elapsed;
    if (s->reject_reason != REMOTE_PAIR_OK)
        s->dt_ms = elapsed > APP_REMOTE_SERVO_SYNC_MAX_AGE_MS ?
                   APP_REMOTE_SERVO_SYNC_PERIOD_MS : elapsed;
    if (s->reject_reason == REMOTE_PAIR_OK)
    {
        s->new_pair = s->pair_valid = 1U;
        s->sequence[0] = paired_left.speed_sequence;
        s->sequence[1] = paired_right.speed_sequence;
        s->age_ms[0] = sync_ms(now - paired_left.sample_tick);
        s->age_ms[1] = sync_ms(now - paired_right.sample_tick);
        s->pair_tick = now;
        s->have_pair = 1U;
        error = (int32_t)paired_left.actual_speed_rpm - paired_right.actual_speed_rpm;
        s->raw_error = error;
        s->effective_error = error > policy->deadband ?
            error - policy->deadband : error < -policy->deadband ? error + policy->deadband : 0;
        requested = policy->kp * s->effective_error;
        if (sync_abs((float)error) > policy->error_rpm)
        {
            if (!s->over_limit) s->over_tick = now;
            s->over_limit = 1U;
            s->over_limit_duration_ms = sync_ms(now - s->over_tick);
        }
        else
        {
            s->over_limit = 0U;
            s->over_limit_duration_ms = 0U;
        }
        if (paired_left.actual_speed_rpm == 0 && paired_right.actual_speed_rpm == 0)
        {
            if (s->zero_pairs < 2U) s->zero_pairs++;
        }
        else s->zero_pairs = 0U;
    }
    else
    {
        s->zero_pairs = 0U;
        s->over_limit = 0U;
        s->over_limit_duration_ms = 0U;
    }
    s->trip = policy->trip_enabled && compensate &&
              s->over_limit_duration_ms >= policy->error_ms;
    s->requested_correction = requested;
    if (magnitude < policy->minimum_rpm || !gate || !compensate)
    {
        s->applied_correction = 0.0f;
    }
    else
    {
        requested = sync_clamp(requested, s->effective_limit);
        step = policy->correction_rate * s->dt_ms / 1000.0f;
        s->applied_correction += sync_clamp(requested - s->applied_correction, step);
        s->applied_correction = sync_clamp(s->applied_correction, s->effective_limit);
        s->active = s->pair_valid;
    }
    s->integer_correction = (int16_t)(s->applied_correction >= 0.0f ?
        s->applied_correction + 0.5f : s->applied_correction - 0.5f);
    s->state = !gate ? REMOTE_SYNC_DISABLED :
        !s->pair_valid ? REMOTE_SYNC_STALE :
        sync_abs(s->requested_correction) > s->effective_limit ?
        REMOTE_SYNC_LIMITED : REMOTE_SYNC_TRACKING;
}

const char *app_remote_sync_state_name(remote_sync_state_t state)
{
    switch (state)
    {
        case REMOTE_SYNC_TRACKING: return "tracking";
        case REMOTE_SYNC_LIMITED: return "limited";
        case REMOTE_SYNC_STALE: return "stale";
        case REMOTE_SYNC_REVERSING: return "reversing";
        case REMOTE_SYNC_FAULT: return "fault";
        default: return "disabled";
    }
}
const char *app_remote_sync_reject_name(remote_pair_reason_t reason)
{
    switch (reason)
    {
        case REMOTE_PAIR_REPEAT: return "repeat";
        case REMOTE_PAIR_AGE: return "age";
        case REMOTE_PAIR_SKEW: return "skew";
        case REMOTE_PAIR_GATE: return "gate";
        case REMOTE_PAIR_WAITING: return "waiting";
        default: return "none";
    }
}
