#include "app_dual_axis_motion.h"
#include "app_config.h"
#include "app_sync_params.h"
#include "app_motion_ramp.h"
#include <float.h>
#include <math.h>
#include <string.h>
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
extern uint32_t HAL_GetTick(void);
#endif

#define DUAL_PI (3.14159265358979323846f)
#define DUAL_RADIUS (APP_WHEEL_DIAMETER_M * 0.5f)
#define DUAL_EPS (0.000001f)
static float da(float x) { return x < 0.0f ? -x : x; }
static float dn(float a, float b) { return a < b ? a : b; }
static float dx(float a, float b) { return a > b ? a : b; }
static float dc(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static uint8_t finite_value(float x) { return x >= -FLT_MAX && x <= FLT_MAX; }
static float gear(uint8_t axis) { return axis ? APP_DUAL_REAR_GEAR : APP_DUAL_FRONT_GEAR; }
static float direction(uint8_t axis) { return (float)(axis ? APP_DUAL_REAR_SIGN : APP_DUAL_FRONT_SIGN); }
float app_dual_motor_to_wheel(float rpm, uint8_t axis)
{ return direction(axis) * rpm * (2.0f * DUAL_PI / 60.0f) / gear(axis); }
float app_dual_wheel_to_motor(float omega, uint8_t axis)
{ return direction(axis) * omega * gear(axis) * (60.0f / (2.0f * DUAL_PI)); }
float app_dual_nominal_rpm_to_wheel(float rpm)
{ return rpm * (2.0f * DUAL_PI / 60.0f) / APP_GEAR_REDUCTION_RATIO; }
float app_dual_vehicle_mm_s_to_wheel(float mm_s) { return mm_s / (1000.0f * DUAL_RADIUS); }
bool app_dual_config_valid(void)
{
    return finite_value(APP_DUAL_FRONT_GEAR) && APP_DUAL_FRONT_GEAR > 0.0f &&
        finite_value(APP_DUAL_REAR_GEAR) && APP_DUAL_REAR_GEAR > 0.0f &&
        (APP_DUAL_FRONT_SIGN == 1 || APP_DUAL_FRONT_SIGN == -1) &&
        (APP_DUAL_REAR_SIGN == 1 || APP_DUAL_REAR_SIGN == -1) &&
        (!APP_DUAL_S_ENABLED || (finite_value(APP_DUAL_JERK_MPS3) && APP_DUAL_JERK_MPS3 > 0.0f)) &&
        (!APP_DUAL_PI_ENABLED || (finite_value(APP_DUAL_KI) && APP_DUAL_KI > 0.0f &&
            finite_value(APP_DUAL_KAW) && APP_DUAL_KAW > 0.0f));
}

bool app_dual_trajectory_plan(app_dual_trajectory_t *s, float target,
    float positive_acceleration, float negative_acceleration, float jerk)
{
    float displacement, brake, sign, initial, limit, peak, first, middle, last;
    if (!s || !finite_value(target) || !finite_value(s->velocity) ||
        !finite_value(s->acceleration) || !finite_value(jerk) || jerk <= 0.0f ||
        !finite_value(positive_acceleration) || positive_acceleration <= 0.0f ||
        !finite_value(negative_acceleration) || negative_acceleration <= 0.0f) return false;
    s->target = target; s->count = s->index = 0U;
    displacement = target - s->velocity;
    if (da(displacement) < DUAL_EPS && da(s->acceleration) < DUAL_EPS)
    { s->velocity = target; s->acceleration = 0.0f; return true; }
    /* Direction is chosen AFTER accounting for acceleration removal. This
     * also handles a new target inside the unavoidable overshoot interval. */
    brake = s->acceleration * da(s->acceleration) / (2.0f * jerk);
    sign = displacement >= brake ? 1.0f : -1.0f;
    initial = sign * s->acceleration;
    limit = sign > 0.0f ? positive_acceleration : negative_acceleration;
    peak = sqrtf(dx(0.0f, jerk * sign * displacement + initial * initial * 0.5f));
    peak = dn(peak, limit);
    first = (peak - initial) / jerk;
    if (first < -DUAL_EPS) return false; /* initial state outside this feasible branch */
    first = dx(0.0f, first); last = peak / jerk;
    middle = peak > DUAL_EPS ?
        (sign * displacement - (2.0f * peak * peak - initial * initial) / (2.0f * jerk)) / peak : 0.0f;
    if (middle < -0.0001f) return false;
    s->segment[0].duration = first; s->segment[0].jerk = sign * jerk;
    s->segment[1].duration = dx(0.0f, middle); s->segment[1].jerk = 0.0f;
    s->segment[2].duration = last; s->segment[2].jerk = -sign * jerk;
    s->count = 3U;
    return true;
}

void app_dual_trajectory_step(app_dual_trajectory_t *s, float seconds)
{
    unsigned iterations;
    if (!s || !finite_value(seconds) || seconds <= 0.0f) return;
    /* At most three phase boundaries; independent of dt and target magnitude. */
    for (iterations = 0U; iterations < 3U && s->index < s->count; ++iterations)
    {
        app_dual_segment_t *phase = &s->segment[s->index];
        float dt = dn(seconds, phase->duration), j = phase->jerk;
        s->velocity += s->acceleration * dt + 0.5f * j * dt * dt;
        s->acceleration += j * dt;
        phase->duration -= dt; seconds -= dt;
        if (phase->duration <= DUAL_EPS) ++s->index;
        else break;
    }
    if (s->index == s->count) { s->velocity = s->target; s->acceleration = 0.0f; }
}

void app_dual_cancel(app_dual_axis_motion_t *s) { memset(s, 0, sizeof(*s)); }
void app_dual_begin(app_dual_axis_motion_t *s, uint8_t owner, uint32_t generation,
    uint8_t mask, uint32_t now_ms, float acceleration_mps2, float deceleration_mps2)
{
    app_sync_params_t tuning;
    app_sync_params_read(&tuning);
    app_dual_cancel(s);
    s->owner = owner; s->generation = generation; s->mask = mask;
    s->service_ms = s->allocation_ms = now_ms;
    s->acceleration_limit = acceleration_mps2 / DUAL_RADIUS;
    s->deceleration_limit = deceleration_mps2 / DUAL_RADIUS;
    s->jerk_limit = tuning.jerk / DUAL_RADIUS;
    s->s_mode = (uint8_t)tuning.s_mode; s->pi_mode = (uint8_t)tuning.pi;
    s->initialized = app_dual_config_valid() && owner && mask && !(mask & ~3U) &&
        finite_value(s->acceleration_limit) && s->acceleration_limit > 0.0f &&
        finite_value(s->deceleration_limit) && s->deceleration_limit > 0.0f;
}

static void replan(app_dual_axis_motion_t *s)
{
    float positive = s->trajectory.velocity < 0.0f ? s->deceleration_limit : s->acceleration_limit;
    float negative = s->trajectory.velocity < 0.0f ? s->acceleration_limit : s->deceleration_limit;
    if (!app_dual_trajectory_plan(&s->trajectory, s->trajectory.target,
        positive, negative, s->jerk_limit))
    { s->constraints |= DUAL_HARD_FALLBACK; s->trajectory.acceleration = 0.0f; s->trajectory.count = 0U; }
}

void app_dual_advance(app_dual_axis_motion_t *s, uint32_t now_ms)
{
    uint32_t elapsed;
    float target, dt, rate, next;
    if (!s->initialized) return;
    elapsed = now_ms - s->service_ms; s->service_ms = now_ms;
    if (elapsed > APP_MOTION_RAMP_MAX_DT_MS) {
        ++s->clipped; s->discarded_ms += elapsed - APP_MOTION_RAMP_MAX_DT_MS;
        elapsed = APP_MOTION_RAMP_MAX_DT_MS;
    }
    s->integrated_ms += elapsed;
    /* Bounded budget: a stalled batch cannot accumulate an unbounded catch-up. */
    s->allocation_budget_ms += elapsed;
    if (s->allocation_budget_ms > APP_CAN_WRITE_CONFIRM_TIMEOUT_MS)
        s->allocation_budget_ms = APP_CAN_WRITE_CONFIRM_TIMEOUT_MS;
    dt = elapsed / 1000.0f;
    target = s->zeroing ? 0.0f : s->trajectory.target;
    if (s->trajectory.velocity * target < 0.0f) target = 0.0f;
    if (s->s_mode && target != 0.0f && !(s->constraints & DUAL_HARD_FALLBACK))
    {
        app_dual_trajectory_step(&s->trajectory, dt);
        return;
    }
    rate = da(target) > da(s->trajectory.velocity) ? s->acceleration_limit : s->deceleration_limit;
    next = s->trajectory.velocity + dc(target - s->trajectory.velocity, -rate * dt, rate * dt);
    s->trajectory.acceleration = dt > 0.0f ? (next - s->trajectory.velocity) / dt : s->trajectory.acceleration;
    s->trajectory.velocity = next;
}

void app_dual_target(app_dual_axis_motion_t *s, float omega, uint32_t sequence,
    uint32_t received_ms, uint32_t deadline_ms, uint32_t now_ms)
{
    uint32_t boundary = received_ms;
    float old = s->trajectory.target;
    if (!s->initialized || !finite_value(omega)) return;
    if ((int32_t)(boundary - s->service_ms) < 0 || (int32_t)(now_ms - boundary) < 0) boundary = now_ms;
    app_dual_advance(s, boundary);
    s->target_sequence = sequence; s->target_received_ms = received_ms; s->deadline_ms = deadline_ms;
    s->trajectory.target = omega;
    if ((old != omega || (!s->trajectory.count && s->trajectory.velocity != omega)) &&
        s->s_mode && !s->zeroing && omega != 0.0f) {
        s->constraints &= ~DUAL_HARD_FALLBACK;
        replan(s);
        s->overshoot_bound = da(s->trajectory.acceleration) > DUAL_EPS ?
            s->trajectory.acceleration * s->trajectory.acceleration / (2.0f * s->jerk_limit) : 0.0f;
        s->overshoot_retract_seconds = da(s->trajectory.acceleration) / s->jerk_limit;
    }
    if (omega == 0.0f && s->s_mode) s->constraints |= DUAL_HARD_FALLBACK;
    if (deadline_ms && (int32_t)(now_ms - deadline_ms) >= 0) {
        s->zeroing = 1U; s->integral = 0.0f; s->constraints |= DUAL_HARD_FALLBACK;
    }
    app_dual_advance(s, now_ms);
}

void app_dual_anchor(app_dual_axis_motion_t *s, const int16_t rpm[2], uint32_t now_ms)
{
    unsigned axis, count = 0U;
    float common = 0.0f;
    for (axis = 0U; axis < 2U; ++axis) if (s->mask & (1U << axis)) {
        s->command[axis] = app_dual_motor_to_wheel((float)rpm[axis], (uint8_t)axis);
        s->command_acceleration[axis] = 0.0f;
        common += s->command[axis]; ++count;
    }
    s->trajectory.velocity = count ? common / count : 0.0f;
    s->trajectory.acceleration = 0.0f;
    s->service_ms = s->allocation_ms = now_ms; s->allocation_budget_ms = 0U;
    s->integral = 0.0f;
    if (s->s_mode && !s->zeroing && s->trajectory.target != 0.0f) replan(s);
}

void app_dual_zero(app_dual_axis_motion_t *s, const int16_t rpm[2], uint32_t now_ms)
{
    app_dual_anchor(s, rpm, now_ms); s->zeroing = 1U;
    s->trajectory.target = 0.0f; s->trajectory.count = 0U;
    s->constraints |= DUAL_HARD_FALLBACK;
}

bool app_dual_output(app_dual_axis_motion_t *s, remote_sync_status_t *sync,
    const servo_snapshot_t *front, const servo_snapshot_t *rear, TickType_t tick,
    uint32_t now_ms, uint8_t compensate, int16_t rpm[2])
{
    servo_sync_policy_t policy;
    app_sync_params_t tuning;
    app_sync_observation_t observation;
    uint32_t parameter_version;
    uint8_t changed;
    const servo_snapshot_t *feedback[2];
    float lo[2], hi[2], dt, base, delta, common, dlo, dhi, clo, chi;
    float unit = app_dual_nominal_rpm_to_wheel(1.0f), previous[2];
    uint8_t axis, stopping;
    if (!s->initialized) { rpm[0] = rpm[1] = 0; return false; }
    if (s->deadline_ms && (int32_t)(now_ms - s->deadline_ms) >= 0) {
        s->zeroing = 1U; s->integral = 0.0f; s->constraints |= DUAL_HARD_FALLBACK;
    }
    app_dual_advance(s, now_ms);
    base = s->trajectory.velocity; dt = s->allocation_budget_ms / 1000.0f;
    feedback[0] = front; feedback[1] = rear;
    stopping = s->zeroing || s->trajectory.target == 0.0f;
    app_servo_sync_policy_default(&policy);
    parameter_version = app_sync_params_read(&tuning);
    changed = parameter_version != s->parameter_version;
    policy.kp = tuning.kp; policy.correction_limit = tuning.limit;
    policy.correction_rate = tuning.rate; policy.deadband = (int32_t)tuning.deadband;
    s->pi_mode = (uint8_t)tuning.pi;
    for (axis = 0U; axis < 2U; ++axis)
        policy.feedback_scale[axis] = direction(axis) * APP_GEAR_REDUCTION_RATIO / gear(axis);
    /* Sync compatibility fields remain nominal motor rpm, not raw axis rpm. */
    /* The joint allocator reserves correction room by reducing the common
     * component at full speed. Do not erase the differential request here. */
    policy.maximum_rpm = (float)APP_OPERATION_MAX_MOTOR_RPM + policy.correction_limit;
    app_servo_sync_update(sync, front, rear, app_motion_ramp_round_rpm(base / unit),
        s->mask == 3U,
        s->mask == 3U && APP_REMOTE_SERVO_SYNC_ENABLED && compensate, tick, &policy);
    s->actual[0] = sync->paired_rpm[0] * unit;
    s->actual[1] = sync->paired_rpm[1] * unit;
    s->common_error = (s->actual[0] + s->actual[1]) * 0.5f - base;
    if (changed) {
        float e = sync->error_f > policy.deadband ? sync->error_f - policy.deadband :
            sync->error_f < -policy.deadband ? sync->error_f + policy.deadband : 0.0f;
        s->integral = s->pi_mode && s->parameter_version ? dc(s->applied - sync->applied_correction * unit,
            -tuning.limit * unit, tuning.limit * unit) : 0.0f;
        /* Old held P request must not survive a parameter generation. */
        sync->requested_correction = tuning.kp * e;
        s->parameter_version = parameter_version; s->parameter_used_ms = now_ms;
    }
    if (!s->pi_mode || !sync->correction_valid || !compensate || stopping) s->integral = 0.0f;
    if (s->pi_mode && sync->new_pair && sync->aligned && sync->correction_valid && !stopping)
        s->integral += tuning.ki * (sync->error_f > policy.deadband ? sync->error_f - policy.deadband :
            sync->error_f < -policy.deadband ? sync->error_f + policy.deadband : 0.0f) * unit * sync->dt_ms / 1000.0f;
    s->requested = sync->correction_valid && compensate ? sync->requested_correction * unit + s->integral : 0.0f;
    delta = sync->correction_valid && compensate ? sync->applied_correction * unit + s->integral : 0.0f;
    delta = dc(delta, -tuning.limit * unit, tuning.limit * unit);
    if (da(base) < APP_REMOTE_SERVO_SYNC_MIN_RPM * unit || !compensate) delta = s->integral = 0.0f;
    delta = dc(delta, s->correction - tuning.rate * unit * dt,
        s->correction + tuning.rate * unit * dt);
    /* A tightened hard limit wins over the correction slew bound. */
    delta = dc(delta, -tuning.limit * unit, tuning.limit * unit);
    if (base == 0.0f) delta = 0.0f;
    s->constraints &= DUAL_HARD_FALLBACK;
    for (axis = 0U; axis < 2U; ++axis) {
        float speed_limit = da(app_dual_motor_to_wheel((float)APP_OPERATION_MAX_MOTOR_RPM, axis));
        float pos = s->command[axis] < 0.0f ? s->deceleration_limit : s->acceleration_limit;
        float neg = s->command[axis] > 0.0f ? s->deceleration_limit : s->acceleration_limit;
        previous[axis] = s->command[axis];
        if (da(base) + da(delta) > speed_limit) s->constraints |= DUAL_LIMIT_SPEED;
        lo[axis] = dx(-speed_limit, previous[axis] - neg * dt);
        hi[axis] = dn(speed_limit, previous[axis] + pos * dt);
        if (base > 0.0f || (base == 0.0f && previous[axis] > 0.0f)) lo[axis] = dx(lo[axis], 0.0f);
        if (base < 0.0f || (base == 0.0f && previous[axis] < 0.0f)) hi[axis] = dn(hi[axis], 0.0f);
        if (s->s_mode && !stopping && dt > 0.0f && !(s->constraints & DUAL_HARD_FALLBACK)) {
            float jlo = previous[axis] + (s->command_acceleration[axis] - s->jerk_limit * dt) * dt;
            float jhi = previous[axis] + (s->command_acceleration[axis] + s->jerk_limit * dt) * dt;
            if (dx(lo[axis], jlo) <= dn(hi[axis], jhi)) {
                lo[axis] = dx(lo[axis], jlo); hi[axis] = dn(hi[axis], jhi);
                s->constraints |= DUAL_LIMIT_JERK;
            } else s->constraints |= DUAL_HARD_FALLBACK;
        }
        if (stopping || feedback[axis]->feedback_degraded) {
            if (previous[axis] >= 0.0f) hi[axis] = dn(hi[axis], previous[axis]);
            if (previous[axis] <= 0.0f) lo[axis] = dx(lo[axis], previous[axis]);
        }
        if (feedback[axis]->feedback_degraded) s->constraints |= DUAL_LIMIT_FEEDBACK;
        if (!(s->mask & (1U << axis))) lo[axis] = hi[axis] = 0.0f;
        if (lo[axis] > hi[axis]) { /* hard zero/direction wins over comfort constraints */
            lo[axis] = hi[axis] = 0.0f; s->constraints |= DUAL_LIMIT_ZERO | DUAL_HARD_FALLBACK;
        }
    }
    if (s->mask == 3U) {
        /* Lexicographic solution: nearest feasible differential first, then
         * nearest feasible common component. No solver or unbounded iteration. */
        dlo = (lo[1] - hi[0]) * 0.5f; dhi = (hi[1] - lo[0]) * 0.5f;
        s->applied = dc(delta, dlo, dhi);
        clo = dx(lo[0] + s->applied, lo[1] - s->applied);
        chi = dn(hi[0] + s->applied, hi[1] - s->applied);
        common = dc(base, clo, chi);
        s->command[0] = common - s->applied; s->command[1] = common + s->applied;
    } else {
        axis = s->mask == 2U ? 1U : 0U;
        common = dc(base, lo[axis], hi[axis]);
        s->command[0] = s->command[1] = 0.0f; s->command[axis] = common; s->applied = 0.0f;
    }
    if (da(common - base) > DUAL_EPS || da(s->applied - delta) > DUAL_EPS) {
        s->constraints |= DUAL_LIMIT_RATE;
        s->trajectory.velocity = common;
        /* Keep acceleration continuous for feasible S replanning. */
        if (s->s_mode && !stopping && !(s->constraints & DUAL_HARD_FALLBACK)) replan(s);
    }
    /* Tightened differential hard limit outranks a one-cycle slew constraint. */
    if (changed && da(s->applied) > tuning.limit * unit) {
        s->applied = dc(s->applied,-tuning.limit*unit,tuning.limit*unit);
        /* Never revive an already stopped/degraded axis when tightening. */
        if (common >= 0.0f) {
            if (stopping || front->feedback_degraded) common=dn(common,previous[0]+s->applied);
            if (stopping || rear->feedback_degraded) common=dn(common,previous[1]-s->applied);
            common=dx(0.0f,common);
        } else {
            if (stopping || front->feedback_degraded) common=dx(common,previous[0]+s->applied);
            if (stopping || rear->feedback_degraded) common=dx(common,previous[1]-s->applied);
            common=dn(0.0f,common);
        }
        if (da(common) < da(s->applied)) s->applied = dc(s->applied,-da(common),da(common));
        s->trajectory.velocity=common;
        s->command[0]=common-s->applied; s->command[1]=common+s->applied;
        s->constraints |= DUAL_HARD_FALLBACK;
    }
    s->correction = s->applied;
    for (axis = 0U; axis < 2U; ++axis) {
        if (dt > 0.0f) s->command_acceleration[axis] = (s->command[axis] - previous[axis]) / dt;
        rpm[axis] = app_motion_ramp_round_rpm(app_dual_wheel_to_motor(s->command[axis], axis));
        if (rpm[axis] > APP_OPERATION_MAX_MOTOR_RPM) rpm[axis] = APP_OPERATION_MAX_MOTOR_RPM;
        if (rpm[axis] < -APP_OPERATION_MAX_MOTOR_RPM) rpm[axis] = -APP_OPERATION_MAX_MOTOR_RPM;
        if (!(s->mask & (1U << axis))) rpm[axis] = 0;
        s->planned_rpm[axis] = rpm[axis];
    }
    /* PI anti-windup uses the final quantized differential. */
    s->applied = s->mask == 3U ? (app_dual_motor_to_wheel(rpm[1], 1U) -
        app_dual_motor_to_wheel(rpm[0], 0U)) * 0.5f : 0.0f;
    if (s->pi_mode && sync->new_pair && sync->aligned && sync->correction_valid && !stopping) {
        s->integral += tuning.kaw * (s->applied - s->requested) * sync->dt_ms / 1000.0f;
        s->integral = dc(s->integral, -tuning.limit * unit, tuning.limit * unit);
    }
    sync->integer_correction = app_motion_ramp_round_rpm(s->applied / unit);
    s->allocation_ms = now_ms; s->allocation_budget_ms = 0U; ++s->batch;
    s->submitted_mask = s->acknowledged_mask = s->failed_mask = 0U;
    app_sync_observation_read(s->owner,&observation);
    if(observation.generation!=s->generation) memset(&observation,0,sizeof(observation));
    observation.target=s->trajectory.target/unit; observation.reference=s->trajectory.velocity/unit;
    observation.integral=s->integral/unit; observation.requested=s->requested/unit;
    observation.applied=s->applied/unit; observation.error=sync->error_f;
    observation.generation=s->generation; observation.batch=s->batch;
    observation.used_version=s->parameter_version; observation.used_ms=s->parameter_used_ms;
    observation.constraints=s->constraints; observation.pair_sequence[0]=sync->sequence[0];
    observation.pair_sequence[1]=sync->sequence[1];
    observation.pair_ms=(uint32_t)((uint64_t)sync->pair_tick*1000U/configTICK_RATE_HZ);
    observation.pair_valid=sync->feedback_valid; observation.aligned=sync->aligned;
    observation.s_mode=s->s_mode;
    if(sync->new_pair) {
        uint32_t tick_ms=(uint32_t)((uint64_t)tick*1000U/configTICK_RATE_HZ);
        uint32_t hal_ms=now_ms;
#if defined(__CC_ARM) || defined(APP_SYNC_STORAGE_TEST)
        /* Remote planning uses RTOS uptime. Enhanced metadata always uses HAL uptime. */
        if(s->owner==2U)hal_ms=HAL_GetTick();
#endif
        observation.pair_aligned_ms=hal_ms-(tick_ms-(uint32_t)((uint64_t)sync->aligned_tick*1000U/configTICK_RATE_HZ));
        observation.pair_generated_ms=hal_ms;observation.pair_version=s->parameter_version;
        observation.pair_reference=observation.reference;observation.pair_target=observation.target;
        observation.pair_meta_flags=1U|2U|(sync->aligned?4U:0U);
    }
    app_sync_observe(s->owner,&observation);
    {
        app_sync_timing_t timing;
        timing.generation=s->generation;timing.batch=s->batch;timing.observed_ms=now_ms;
        timing.calc_interval_ms=sync->calc_interval_ms;timing.pair_gap_ms=sync->pair_gap_ms;
        timing.integral_dt_ms=sync->dt_ms;timing.slew_dt_ms=sync->slew_dt_ms;
        timing.raw_age_ms[0]=sync->raw_age_ms[0];timing.raw_age_ms[1]=sync->raw_age_ms[1];
        timing.flags=(sync->new_pair?APP_SYNC_TIMING_NEW_PAIR:0U)|
            (sync->feedback_valid?APP_SYNC_TIMING_FEEDBACK_VALID:0U)|
            (sync->correction_valid?APP_SYNC_TIMING_CORRECTION_VALID:0U)|
            (sync->aligned?APP_SYNC_TIMING_ALIGNED:0U)|
            (compensate && s->mask==3U && APP_REMOTE_SERVO_SYNC_ENABLED?APP_SYNC_TIMING_COMPENSATE:0U);
        timing.reject_reason=sync->reject_reason;
        timing.requested=s->requested/unit;timing.applied=s->applied/unit;
        timing.new_pair_count=sync->new_pair_count;timing.slew_update_count=sync->slew_update_count;
        timing.zero_slew_fresh_count=sync->zero_slew_fresh_count;
        timing.invalidation_count=sync->invalidation_count;timing.long_gap_count=sync->long_gap_count;
        timing.calc_interval_max_ms=sync->calc_interval_max_ms;timing.slew_dt_max_ms=sync->slew_dt_max_ms;
        app_sync_timing_observe(s->owner,&timing);
    }
    return true;
}

void app_dual_execution(app_dual_axis_motion_t *s, uint32_t generation, uint32_t batch,
    uint8_t axis, int16_t actual_rpm, uint32_t now_ms, uint8_t completed, uint8_t success)
{
    uint8_t bit;
    if (!s->initialized || generation != s->generation || batch != s->batch || axis > 1U) return;
    bit = (uint8_t)(1U << axis);
    if (!completed) { s->submitted_mask |= bit; s->submitted_ms[axis] = now_ms; s->submitted_rpm[axis] = actual_rpm; }
    else {
        s->acknowledged_ms[axis] = now_ms; s->acknowledged_rpm[axis] = actual_rpm;
        if (success) s->acknowledged_mask |= bit; else s->failed_mask |= bit;
        if (success && actual_rpm != s->planned_rpm[axis]) {
            s->constraints |= DUAL_WRITE_CHANGED;
            s->command[axis] = app_dual_motor_to_wheel((float)actual_rpm, axis);
            s->command_acceleration[axis] = 0.0f; s->integral = 0.0f;
            s->trajectory.velocity = s->mask == 3U ? (s->command[0] + s->command[1]) * 0.5f : s->command[axis];
            s->applied = s->correction = s->mask == 3U ? (s->command[1] - s->command[0]) * 0.5f : 0.0f;
            s->trajectory.acceleration = 0.0f;
            if (s->s_mode && !s->zeroing && s->trajectory.target != 0.0f) replan(s);
        }
    }
}
