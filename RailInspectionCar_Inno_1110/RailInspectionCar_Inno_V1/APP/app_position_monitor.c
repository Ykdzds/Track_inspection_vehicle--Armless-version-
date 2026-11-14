#include "app_link_diag.h"
#include "main.h"
#include "app_position_monitor.h"

#include <string.h>
#include "app_config.h"
#include "app_remote_servo.h"
#include "motion_control.h"
#include "servo_debug_control.h"
#include "app_cli_servo_session.h"
#include "servo_pulse.h"
#include "task.h"

static position_monitor_status_t s_status;
static position_monitor_status_t s_published;
static volatile uint8_t s_reset_requested;

#if (APP_POSITION_MONITOR_ENABLED == 1U)
typedef struct
{
    TickType_t tick;
    uint32_t sequence;
    int64_t net_counts;
} position_pending_t;

typedef struct
{
    uint8_t seen;
    uint8_t sampled;
    uint8_t rejected;
    uint8_t anchored;
    int32_t raw;
    uint32_t sequence;
    uint32_t generation;
    TickType_t tick;
    position_pending_t pending[SERVO_POSITION_SAMPLE_CAPACITY];
    uint8_t pending_count;
} position_history_t;

/* 工作快照只由运动任务访问，避免扩大实时任务栈。 */
static servo_position_monitor_axis_t s_feedback[SERVO_AXIS_COUNT];
static remote_position_gate_t s_remote;
typedef char position_projection_budget[
    (sizeof(s_feedback) + sizeof(s_remote) <= 240U) ? 1 : -1];
static position_history_t s_history[SERVO_AXIS_COUNT];
static uint8_t s_start_ready;
static uint8_t s_session_active;
static uint8_t s_baselined;
static uint8_t s_continuity_lost;
static int64_t s_baseline[SERVO_AXIS_COUNT];
static TickType_t s_pair_tick;
static TickType_t s_pair_oldest_tick;
static uint32_t s_session_sequence[SERVO_AXIS_COUNT];

static uint32_t position_ms(TickType_t ticks)
{
    return (uint32_t)((uint64_t)ticks * 1000U / configTICK_RATE_HZ);
}

static uint8_t position_newer(uint32_t sequence, uint32_t previous)
{
    uint32_t delta = sequence - previous;
    return (uint8_t)(delta != 0U && delta < 0x80000000UL);
}

static int64_t position_delta(int32_t raw, int32_t previous)
{
    uint32_t delta = (uint32_t)raw - (uint32_t)previous;
    return delta <= 0x7FFFFFFFUL ? (int64_t)delta :
                                  (int64_t)delta - 4294967296LL;
}

static double position_scale(uint8_t axis)
{
    return axis == SERVO_AXIS_LEFT ?
        3.141592653589793 * APP_POSITION_LEFT_WHEEL_DIAMETER_MM *
        APP_POSITION_LEFT_GEAR_DEN /
        (APP_POSITION_LEFT_COUNTS_PER_REV * (double)APP_POSITION_LEFT_GEAR_NUM) :
        3.141592653589793 * APP_POSITION_RIGHT_WHEEL_DIAMETER_MM *
        APP_POSITION_RIGHT_GEAR_DEN /
        (APP_POSITION_RIGHT_COUNTS_PER_REV * (double)APP_POSITION_RIGHT_GEAR_NUM);
}

static void position_end_session(void)
{
    s_session_active = 0U;
    s_baselined = 0U;
    s_status.pair_valid = 0U;
    s_status.rate_valid = 0U;
    s_status.skew_estimate_valid = 0U;
    s_history[0].pending_count = 0U;
    s_history[1].pending_count = 0U;
}

static void position_break(uint8_t axis)
{
    position_history_t *history = &s_history[axis];
    if (history->anchored)
    {
        s_status.axis[axis].incomplete = 1U;
        s_status.axis[axis].lost_intervals++;
        s_status.lost_intervals++;
        s_continuity_lost = 1U;
    }
    history->anchored = 0U;
    history->pending_count = 0U;
    s_status.axis[axis].valid = 0U;
}

static uint8_t position_compare_allowed(void)
{
    uint8_t axis;
    if (s_remote.authorized == 0U || s_remote.estop_latched ||
        s_remote.control_axis_mask != APP_SERVO_CONTROL_AXIS_BOTH ||
        (s_remote.state != REMOTE_SERVO_ARMED &&
         s_remote.state != REMOTE_SERVO_RUNNING) ||
        !aimtor_startup_hold_ready() || !aimtor_motion_allowed() ||
        !app_config_motion_hardware_ready() || app_config_servo_readonly_test())
        return 0U;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_config_servo_axis_selected(axis) ||
            !s_feedback[axis].online || s_feedback[axis].fault ||
            !s_feedback[axis].parameter_verified ||
            s_feedback[axis].control_mode != SERVO_CONTROL_RS485_VELOCITY)
            return 0U;
    }
    return 1U;
}

static void position_queue(uint8_t axis, const servo_position_sample_t *sample)
{
    position_history_t *history = &s_history[axis];
    position_pending_t *pending;
    if (!s_session_active ||
        !position_newer(sample->position_sequence, s_session_sequence[axis]))
        return;
    if (history->pending_count == SERVO_POSITION_SAMPLE_CAPACITY)
    {
        memmove(history->pending, history->pending + 1,
                (SERVO_POSITION_SAMPLE_CAPACITY - 1U) * sizeof(history->pending[0]));
        history->pending_count--;
    }
    pending = &history->pending[history->pending_count++];
    pending->tick = sample->sample_tick;
    pending->sequence = sample->position_sequence;
    pending->net_counts = s_status.axis[axis].boot_net_counts;
}

static void position_consume(uint8_t axis, const servo_position_sample_t *sample,
                             TickType_t now)
{
    position_history_t *history = &s_history[axis];
    position_axis_status_t *status = &s_status.axis[axis];
    int64_t delta;
    uint64_t magnitude;
    uint64_t limit;
    uint32_t elapsed;
    if (!sample->valid || sample->position_generation != s_feedback[axis].position_generation)
        return;
    /* 异常序号重新锚定后，缓存中更早时代的样本也必须排除。 */
    if (history->sampled && position_ms(now - history->tick) <= APP_POSITION_MAX_AGE_MS &&
        (uint32_t)(sample->sample_tick - history->tick) >= 0x80000000UL)
        return;
    if (history->seen &&
        !position_newer(sample->position_sequence, history->sequence))
        return;
    if (position_ms(now - sample->sample_tick) > APP_POSITION_MAX_AGE_MS)
        return;
    if (history->anchored)
    {
        elapsed = position_ms(sample->sample_tick - history->tick);
        delta = position_delta(sample->actual_position, history->raw);
        magnitude = (uint64_t)(delta < 0 ? -delta : delta);
        limit = (uint64_t)(axis == 0U ? APP_POSITION_LEFT_COUNTS_PER_REV :
                                      APP_POSITION_RIGHT_COUNTS_PER_REV) *
                APP_OPERATION_MAX_MOTOR_RPM * elapsed / 60000U +
                APP_POSITION_JUMP_MARGIN;
        if (magnitude > limit)
        {
            position_break(axis);
            history->sequence = sample->position_sequence;
            history->tick = sample->sample_tick;
            history->rejected = 1U;
            return;
        }
        if (sample->position_generation != history->generation ||
            sample->position_sequence - history->sequence != 1U ||
            elapsed > APP_POSITION_MAX_AGE_MS)
        {
            position_break(axis);
        }
        else
        {
            delta *= axis == 0U ? APP_POSITION_LEFT_SIGN : APP_POSITION_RIGHT_SIGN;
            status->boot_net_counts += delta;
            status->boot_path_counts += magnitude;
            if (!status->trip_anchor_pending)
            {
                status->trip_net_counts += delta;
                status->trip_path_counts += magnitude;
            }
        }
    }
    history->seen = 1U;
    history->sampled = 1U;
    history->rejected = 0U;
    status->trip_anchor_pending = 0U;
    history->anchored = 1U;
    history->raw = sample->actual_position;
    history->tick = sample->sample_tick;
    history->sequence = sample->position_sequence;
    history->generation = sample->position_generation;
    status->raw_position = history->raw;
    status->sequence = history->sequence;
    status->generation = history->generation;
    status->valid = 1U;
    position_queue(axis, sample);
}

static void position_pair(TickType_t now)
{
    position_history_t *left = &s_history[0];
    position_history_t *right = &s_history[1];
    uint8_t axis;
    uint8_t i;
    uint8_t j;
    uint8_t best_l = 0U;
    uint8_t best_r = 0U;
    uint32_t best_skew = UINT32_MAX;
    uint32_t best_age = UINT32_MAX;
    TickType_t pair_tick;
    double old_difference = s_status.difference_mm;
    uint8_t old_valid = s_status.pair_valid;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        position_history_t *h = &s_history[axis];
        while (h->pending_count &&
               position_ms(now - h->pending[0].tick) > APP_POSITION_MAX_AGE_MS)
        {
            memmove(h->pending, h->pending + 1,
                    (--h->pending_count) * sizeof(h->pending[0]));
        }
    }
    for (i = 0U; i < left->pending_count; i++)
    {
        for (j = 0U; j < right->pending_count; j++)
        {
            uint32_t la = position_ms(now - left->pending[i].tick);
            uint32_t ra = position_ms(now - right->pending[j].tick);
            uint32_t skew = la > ra ? la - ra : ra - la;
            uint32_t age = la > ra ? la : ra;
            if (skew < best_skew || (skew == best_skew && age < best_age))
            {
                best_skew = skew;
                best_age = age;
                best_l = i;
                best_r = j;
            }
        }
    }
    if (best_skew <= APP_POSITION_MAX_SKEW_MS)
    {
        position_pending_t pair[2];
        uint32_t elapsed;
        pair[0] = left->pending[best_l];
        pair[1] = right->pending[best_r];
        pair_tick = (now - pair[0].tick < now - pair[1].tick) ?
                     pair[0].tick : pair[1].tick;
        elapsed = position_ms(pair_tick - s_pair_tick);
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        {
            position_history_t *h = &s_history[axis];
            uint8_t consumed = (uint8_t)((axis == 0U ? best_l : best_r) + 1U);
            if (!s_baselined) s_baseline[axis] = pair[axis].net_counts;
            s_status.relative_counts[axis] = pair[axis].net_counts - s_baseline[axis];
            s_status.pair_sequence[axis] = pair[axis].sequence;
            h->pending_count -= consumed;
            memmove(h->pending, h->pending + consumed,
                    h->pending_count * sizeof(h->pending[0]));
        }
        s_status.difference_counts = s_status.relative_counts[0] - s_status.relative_counts[1];
        s_status.difference_mm = s_status.relative_counts[0] * position_scale(0U) -
                                 s_status.relative_counts[1] * position_scale(1U);
        s_status.rate_valid = (uint8_t)(s_baselined && old_valid &&
                              elapsed != 0U && elapsed <= APP_POSITION_MAX_AGE_MS);
        if (s_status.rate_valid)
            s_status.difference_rate_mm_s =
                (s_status.difference_mm - old_difference) * 1000.0 / elapsed;
        s_baselined = 1U;
        s_pair_tick = pair_tick;
        s_pair_oldest_tick = (now - pair[0].tick > now - pair[1].tick) ?
                             pair[0].tick : pair[1].tick;
        s_status.sample_skew_ms = best_skew;
        s_status.pair_valid = 1U;
        s_status.pair_count++;
    }
    else if (left->pending_count && right->pending_count)
    {
        /* 仅丢弃过早的一侧样本，较新样本继续等待。 */
        position_history_t *older = now - left->pending[0].tick >
                                   now - right->pending[0].tick ? left : right;
        memmove(older->pending, older->pending + 1,
                (--older->pending_count) * sizeof(older->pending[0]));
    }
    s_status.skew_estimate_valid = (uint8_t)(s_status.pair_valid &&
        s_feedback[0].speed_valid && s_feedback[1].speed_valid &&
        s_feedback[0].speed_age_ms < 100U && s_feedback[1].speed_age_ms < 100U);
    if (s_status.skew_estimate_valid)
    {
        double l = s_feedback[0].actual_speed_rpm;
        double r = s_feedback[1].actual_speed_rpm;
        l = (l < 0 ? -l : l) * APP_POSITION_LEFT_COUNTS_PER_REV * position_scale(0U);
        r = (r < 0 ? -r : r) * APP_POSITION_RIGHT_COUNTS_PER_REV * position_scale(1U);
        s_status.skew_estimate_mm = (l > r ? l : r) * s_status.sample_skew_ms / 60000.0;
    }
}

static uint8_t position_reset_allowed(void)
{
    uint8_t axis;
    servo_position_monitor_axis_t feedback[SERVO_AXIS_COUNT];
    remote_position_gate_t remote;
    servo_debug_status_t debug;
    if (app_cli_servo_session_active() ||
        !aimtor_startup_stop_ready() || !aimtor_boot_zero_ready() ||
        !aimtor_watchdog_recovery_ready() ||
        !app_remote_servo_get_position_gate(&remote) || remote.authorized ||
        remote.hold_stopping || remote.state == REMOTE_SERVO_ARMING ||
        motion_get_state() == MOTION_RUNNING ||
        motion_get_state() == MOTION_STOPPING ||
        motion_get_pulse_active_mask(APP_SERVO_CONTROL_AXIS_BOTH) ||
        motion_get_pulse_completion_pending_mask(APP_SERVO_CONTROL_AXIS_BOTH))
        return 0U;
    if (servo_debug_get_status(&debug) &&
        (debug.state == SERVO_DEBUG_PREPARING ||
         debug.state == SERVO_DEBUG_RUNNING || debug.state == SERVO_DEBUG_STOPPING))
        return 0U;
    if (!aimtor_get_position_monitor_pair(&feedback[0], &feedback[1])) return 0U;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        if (!app_config_servo_axis_selected(axis)) continue;
        if (!feedback[axis].online || feedback[axis].busy || !feedback[axis].speed_valid ||
            feedback[axis].speed_age_ms >= 100U || feedback[axis].actual_speed_rpm != 0 ||
            servo_pulse_get_frequency((servo_axis_t)axis) != 0U)
            return 0U;
    }
    return 1U;
}
#endif

void app_position_monitor_init(void)
{
    uint8_t axis;
    memset(&s_status, 0, sizeof(s_status));
    s_status.enabled = APP_POSITION_MONITOR_ENABLED;
    s_status.state = APP_POSITION_MONITOR_ENABLED ?
                     POSITION_MONITOR_WAITING : POSITION_MONITOR_DISABLED;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        s_status.axis[axis].selected = app_config_servo_axis_selected(axis);
        s_status.axis[axis].state = s_status.enabled && s_status.axis[axis].selected ?
                                   POSITION_MONITOR_WAITING : POSITION_MONITOR_DISABLED;
        s_status.axis[axis].age_ms = UINT32_MAX;
        if (s_status.axis[axis].selected) s_status.source_mask |= (uint8_t)(1U << axis);
    }
#if (APP_POSITION_MONITOR_ENABLED == 1U)
    memset(s_history, 0, sizeof(s_history));
    s_start_ready = 0U;
    s_session_active = 0U;
    s_baselined = 0U;
    s_continuity_lost = 0U;
    s_pair_tick = 0U;
    s_pair_oldest_tick = 0U;
#endif
    s_reset_requested = 0U;
    s_published = s_status;
}

void app_position_monitor_service(void)
{
#if (APP_POSITION_MONITOR_ENABLED == 1U)
    TickType_t now = xTaskGetTickCount();
    uint8_t axis;
    uint8_t ready;
    uint8_t eligible;
    uint8_t count = 0U;
    uint8_t reset;
    (void)aimtor_get_position_monitor_pair(&s_feedback[0], &s_feedback[1]);
    (void)app_remote_servo_get_position_gate(&s_remote);
    ready = (uint8_t)(aimtor_startup_stop_ready() &&
                     aimtor_watchdog_recovery_ready() && aimtor_boot_zero_ready());
    if (!ready)
    {
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++) position_break(axis);
        s_start_ready = 0U;
        position_end_session();
    }
    else
    {
        if (!s_start_ready)
        {
            /* 启动清理阶段的旧缓存不能成为后续行程。 */
            for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
            {
                s_history[axis].seen = 1U;
                s_history[axis].sequence = s_feedback[axis].position_sequence;
                s_history[axis].generation = s_feedback[axis].position_generation;
            }
            s_start_ready = 1U;
        }
        eligible = position_compare_allowed();
        if (!eligible || (s_session_active &&
            s_remote.authorization_generation != s_status.authorization_generation))
            position_end_session();
        if (eligible && !s_session_active)
        {
            s_session_active = 1U;
            s_status.baseline_generation++;
            s_status.authorization_generation = s_remote.authorization_generation;
            for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
                s_session_sequence[axis] = s_feedback[axis].position_sequence;
        }
        s_continuity_lost = 0U;
        for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
        {
            position_history_t *h = &s_history[axis];
            uint8_t i;
            if (!s_status.axis[axis].selected) continue;
            if (!s_feedback[axis].online || !s_feedback[axis].position_valid ||
                s_feedback[axis].position_age_ms > APP_POSITION_MAX_AGE_MS)
            {
                position_break(axis);
                continue;
            }
            if (h->seen && h->generation != s_feedback[axis].position_generation)
            {
                position_break(axis);
                h->generation = s_feedback[axis].position_generation;
            }
            if (h->seen && s_feedback[axis].position_sequence != h->sequence &&
                !position_newer(s_feedback[axis].position_sequence, h->sequence))
            {
                position_break(axis);
                h->seen = 0U;
                /* 序号异常时只用最新反馈锚定，禁止重放旧序号时代的缓存。 */
                for (i = SERVO_POSITION_SAMPLE_CAPACITY; i > 0U; i--)
                    if (s_feedback[axis].position_samples[i - 1U].valid &&
                        s_feedback[axis].position_samples[i - 1U].position_sequence ==
                        s_feedback[axis].position_sequence)
                    {
                        position_consume(axis, &s_feedback[axis].position_samples[i - 1U], now);
                        break;
                    }
                continue;
            }
            for (i = 0U; i < SERVO_POSITION_SAMPLE_CAPACITY; i++)
                position_consume(axis, &s_feedback[axis].position_samples[i], now);
        }
        if (s_continuity_lost) position_end_session();
        if (s_status.pair_valid &&
            position_ms(now - s_pair_oldest_tick) > APP_POSITION_MAX_AGE_MS)
        {
            s_status.pair_valid = 0U;
            s_status.rate_valid = 0U;
        }
        if (s_session_active) position_pair(now);
    }
    s_status.vehicle_valid = ready;
    s_status.vehicle_boot_net_mm = 0;
    s_status.vehicle_trip_net_mm = 0;
    s_status.vehicle_boot_path_mm = 0;
    s_status.vehicle_trip_path_mm = 0;
    s_status.state = ready ? POSITION_MONITOR_TRACKING : POSITION_MONITOR_WAITING;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
    {
        position_axis_status_t *a = &s_status.axis[axis];
        position_history_t *h = &s_history[axis];
        double scale = position_scale(axis);
        if (!a->selected) continue;
        count++;
        a->age_ms = h->sampled ? position_ms(now - h->tick) : UINT32_MAX;
        if (!ready) a->state = POSITION_MONITOR_WAITING;
        else if (!s_feedback[axis].online) a->state = POSITION_MONITOR_INVALID;
        else if (h->rejected) a->state = POSITION_MONITOR_INVALID;
        else if (!h->anchored) a->state = h->sampled ? POSITION_MONITOR_STALE : POSITION_MONITOR_WAITING;
        else a->state = POSITION_MONITOR_TRACKING;
        a->valid = (uint8_t)(a->state == POSITION_MONITOR_TRACKING);
        if (!a->valid)
        {
            s_status.vehicle_valid = 0U;
            if (ready) s_status.state = a->state;
        }
        a->boot_net_mm = a->boot_net_counts * scale;
        a->trip_net_mm = a->trip_net_counts * scale;
        a->boot_path_mm = a->boot_path_counts * scale;
        a->trip_path_mm = a->trip_path_counts * scale;
        s_status.vehicle_boot_net_mm += a->boot_net_mm;
        s_status.vehicle_trip_net_mm += a->trip_net_mm;
        s_status.vehicle_boot_path_mm += a->boot_path_mm;
        s_status.vehicle_trip_path_mm += a->trip_path_mm;
    }
    if (count)
    {
        s_status.vehicle_boot_net_mm /= count;
        s_status.vehicle_trip_net_mm /= count;
        s_status.vehicle_boot_path_mm /= count;
        s_status.vehicle_trip_path_mm /= count;
    }
    else s_status.vehicle_valid = 0U;
    s_status.raw_difference_counts = (int64_t)s_status.axis[0].raw_position -
                                     s_status.axis[1].raw_position;
    s_status.pair_age_ms = s_baselined ? position_ms(now - s_pair_oldest_tick) : UINT32_MAX;
    if (!s_status.vehicle_valid || s_status.pair_age_ms > APP_POSITION_MAX_AGE_MS)
    {
        s_status.pair_valid = 0U;
        s_status.rate_valid = 0U;
        s_status.skew_estimate_valid = 0U;
    }
    taskENTER_CRITICAL();
    reset = s_reset_requested;
    s_reset_requested = 0U;
    taskEXIT_CRITICAL();
    if (reset)
    {
        if (position_reset_allowed())
        {
            for (axis = 0U; axis < SERVO_AXIS_COUNT; axis++)
            {
                s_status.axis[axis].trip_net_counts = 0;
                s_status.axis[axis].trip_path_counts = 0U;
                /* 新分段从下一有效位置锚定，跨清零边界的未知移动不补算。 */
                s_status.axis[axis].trip_anchor_pending = s_status.axis[axis].selected;
                s_status.axis[axis].trip_net_mm = 0;
                s_status.axis[axis].trip_path_mm = 0;
            }
            s_status.vehicle_trip_net_mm = 0;
            s_status.vehicle_trip_path_mm = 0;
            s_status.trip_reset_count++;
        }
        else s_status.trip_reset_rejected++;
    }
    taskENTER_CRITICAL();
    s_published = s_status;
    taskEXIT_CRITICAL();
#endif
}

bool app_position_monitor_get_status(position_monitor_status_t *status)
{
    if (status == NULL) return false;
    taskENTER_CRITICAL();
    *status = s_published;
    status->trip_reset_pending = s_reset_requested;
    taskEXIT_CRITICAL();
    return true;
}

bool app_position_monitor_request_trip_reset(void)
{
#if (APP_POSITION_MONITOR_ENABLED == 1U)
    if (!position_reset_allowed()) return false;
    taskENTER_CRITICAL();
    s_reset_requested = 1U;
    taskEXIT_CRITICAL();
    return true;
#else
    return false;
#endif
}

const char *app_position_monitor_state_name(position_monitor_state_t state)
{
    switch (state)
    {
        case POSITION_MONITOR_DISABLED: return "disabled";
        case POSITION_MONITOR_WAITING: return "waiting";
        case POSITION_MONITOR_TRACKING: return "tracking";
        case POSITION_MONITOR_STALE: return "stale";
        default: return "invalid";
    }
}

#if APP_LINK_DIAG_ACTIVE && APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED
/* Integer-only projection, no hardware queries and no duplicate full snapshot. */
void app_position_diag_snapshot(app_link_diag_snapshot_t *out)
{
    unsigned a,n=0U;
    taskENTER_CRITICAL();
    out->sampled_ms=HAL_GetTick();
    out->words[n++]=s_published.baseline_generation;
    out->words[n++]=s_published.pair_valid;
    out->words[n++]=s_published.sample_skew_ms;
    out->words[n++]=s_published.authorization_generation;
    for(a=0U;a<2U;++a) {
        uint64_t relative=(uint64_t)s_published.relative_counts[a];
        out->words[n++]=(uint32_t)s_published.axis[a].raw_position;
        out->words[n++]=s_published.axis[a].valid;
        out->words[n++]=s_published.axis[a].age_ms;
        out->words[n++]=s_published.axis[a].sequence;
        out->words[n++]=(uint32_t)(relative>>32U);
        out->words[n++]=(uint32_t)relative;
    }
    out->count=(uint8_t)n;
    taskEXIT_CRITICAL();
}
#endif
