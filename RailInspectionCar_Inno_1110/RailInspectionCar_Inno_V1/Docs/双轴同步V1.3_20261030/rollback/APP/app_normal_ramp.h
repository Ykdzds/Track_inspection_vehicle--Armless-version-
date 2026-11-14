#ifndef APP_NORMAL_RAMP_H
#define APP_NORMAL_RAMP_H

#include <string.h>
#include "app_motion_ramp.h"

/* Service-time ramp clock. Safety-stop ramps retain their dedicated clocks. */
typedef struct {
    uint32_t tick, max_service_ms, clipped, discarded_ms, integrated_ms;
    uint32_t batches, max_batch_ms;
    uint8_t active;
} app_normal_ramp_clock_t;

typedef struct {
    app_normal_ramp_clock_t clock;
    int16_t target[2], internal[2], batch[2];
    uint8_t authorized, phase;
} app_normal_ramp_status_t;

static inline void app_normal_ramp_begin(app_normal_ramp_clock_t *clock, uint32_t now)
{
    memset(clock, 0, sizeof(*clock)); clock->tick = now;
}

static inline void app_normal_ramp_boundary(app_normal_ramp_clock_t *clock, uint32_t now)
{
    clock->tick = now; clock->active = 0U;
}

static inline uint32_t app_normal_ramp_elapsed(app_normal_ramp_clock_t *clock, uint32_t now)
{
    uint32_t dt = now - clock->tick;
    clock->tick = now;
    if (!clock->active) { clock->active = 1U; return 0U; }
    if (dt > clock->max_service_ms) clock->max_service_ms = dt;
    if (dt > APP_MOTION_RAMP_MAX_DT_MS) {
        ++clock->clipped; clock->discarded_ms += dt - APP_MOTION_RAMP_MAX_DT_MS;
        dt = APP_MOTION_RAMP_MAX_DT_MS;
    }
    clock->integrated_ms += dt;
    return dt;
}

static inline void app_normal_ramp_batch_done(app_normal_ramp_clock_t *clock, uint32_t ms)
{
    ++clock->batches;
    if (ms > clock->max_batch_ms) clock->max_batch_ms = ms;
}

void app_hmi_control_ramp_status(app_normal_ramp_status_t *out);
void app_remote_servo_ramp_status(app_normal_ramp_status_t *out);
void app_can_control_ramp_status(app_normal_ramp_status_t *out);
#endif
