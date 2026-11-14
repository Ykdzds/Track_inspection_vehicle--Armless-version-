#include "app_control_arbiter.h"
#include "app_hmi_link_diag.h"

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include <string.h>

static uint8_t s_recovery_hold, s_parameter_lock, s_arm_work_lock;
static void (*s_arm_stop_callback)(void);
void app_control_arbiter_set_arm_stop(void (*callback)(void)){s_arm_stop_callback=callback;}
static app_control_owner_t s_owner;
static app_control_lock_reason_t s_reason;
static uint8_t s_axis_mask;
static uint32_t s_generation;
typedef struct { uint32_t ticket, submitted_generation, granted_generation; app_control_owner_t owner; uint8_t mask; int8_t result; } arbiter_entry_t;
static arbiter_entry_t s_queue[8];
static uint32_t s_ticket;
static uint8_t s_count, s_read, s_write, s_preparing, s_stopping;

static app_control_stop_t s_stop_event;
static uint8_t s_stop_pending;

uint8_t app_control_arbiter_submit_global_stop(app_control_owner_t requester,
    uint16_t reason, uint16_t deceleration, uint32_t sequence, uint16_t command)
{
    uint8_t accepted = 0U;
    uint8_t new_event = 0U;
    if(s_arm_stop_callback)s_arm_stop_callback();
    taskENTER_CRITICAL();
    if (s_owner != APP_CONTROL_OWNER_NONE && s_reason == APP_CONTROL_LOCK_NONE)
    {
        if (s_stop_event.result != 2U || s_stop_event.owner != s_owner)
        {
            uint32_t event = s_stop_event.event + 1U;
            memset(&s_stop_event, 0, sizeof(s_stop_event));
            s_stop_event.event = event ? event : 1U;
            s_stop_event.owner = s_owner;
            s_stop_event.requester = requester;
            s_stop_event.reason = reason;
            s_stop_event.request_sequence = sequence;
            s_stop_event.command = command;
            s_stop_event.started_ms = HAL_GetTick();
            s_stop_event.result = 2U;
            new_event = 1U;
            if (++s_generation == 0U) ++s_generation;
            s_stop_event.generation = s_generation;
        }
        if (deceleration > s_stop_event.deceleration_mm_s2)
        {
            s_stop_event.deceleration_mm_s2 = deceleration;
            s_stop_event.reason = reason;
            s_stop_pending = 1U;
            if (!new_event)
                app_hmi_link_diag_upgrade(&s_stop_event,(uint16_t)requester,sequence,command);
        }
        /* Freeze after the initial deceleration is installed, before another
         * caller can upgrade this same event. Never alter control history. */
        if (new_event) app_hmi_link_diag_stop(&s_stop_event, APP_HMI_LINK_STOP_FIRST);
        s_stopping = 1U;
        accepted = 1U;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

uint8_t app_control_arbiter_submit_stop(uint16_t reason, uint16_t deceleration,
    uint32_t sequence, uint16_t command)
{
    return app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_NONE,
        reason, deceleration, sequence, command);
}

uint8_t app_control_arbiter_take_stop(app_control_owner_t owner, app_control_stop_t *request)
{
    uint8_t available;
    taskENTER_CRITICAL();
    available = s_stop_pending && s_stop_event.owner == owner && s_owner == owner;
    if (available) { *request = s_stop_event; s_stop_pending = 0U; }
    taskEXIT_CRITICAL();
    return available;
}

void app_control_arbiter_finish_stop(app_control_owner_t owner,
    uint16_t result, uint16_t error, uint16_t zero_mask, uint16_t enabled_mask)
{
    taskENTER_CRITICAL();
    if (s_stop_event.owner == owner && s_stop_event.result == 2U)
    {
        if (s_stop_event.generation != s_generation || s_owner != owner)
        { taskEXIT_CRITICAL(); return; }
        s_stop_event.result = result; s_stop_event.error = error;
        s_stop_event.zero_mask = zero_mask; s_stop_event.enabled_mask = enabled_mask;
        s_stop_event.completed_ms = HAL_GetTick();
        s_stop_pending = 0U;
        app_hmi_link_diag_stop(&s_stop_event, APP_HMI_LINK_STOP_COMPLETE);
    }
    taskEXIT_CRITICAL();
}
void app_control_arbiter_stop_timeout(app_control_owner_t owner, uint32_t timeout)
{
    taskENTER_CRITICAL();
    if (s_stop_event.owner == owner && s_stop_event.result == 2U && !s_stop_event.timeout_ms)
        s_stop_event.timeout_ms = timeout;
    taskEXIT_CRITICAL();
}
void app_control_arbiter_get_stop(app_control_stop_t *status)
{
    taskENTER_CRITICAL(); *status = s_stop_event; taskEXIT_CRITICAL();
}

void app_control_arbiter_init(void)
{
    memset(&s_stop_event, 0, sizeof(s_stop_event));
    s_recovery_hold = s_parameter_lock = s_arm_work_lock = 0U;
    s_stop_pending = 0U;
    s_owner = APP_CONTROL_OWNER_NONE;
    s_reason = APP_CONTROL_LOCK_NONE;
    s_axis_mask = 0U;
    s_generation = 0U;
    s_count = s_read = s_write = s_preparing = s_stopping = 0U;
    s_ticket = 0U;
    { unsigned i; for (i = 0U; i < 8U; ++i) s_queue[i].ticket = 0U; }
}

uint32_t app_control_arbiter_submit(app_control_owner_t owner, uint8_t mask)
{
    uint32_t ticket = 0U;
    if (owner == APP_CONTROL_OWNER_NONE || mask == 0U) return 0U;
    taskENTER_CRITICAL();
    if (!s_recovery_hold && !s_parameter_lock && !s_arm_work_lock && s_count < 8U && s_owner == APP_CONTROL_OWNER_NONE &&
        !s_stopping && s_reason == APP_CONTROL_LOCK_NONE)
    {
        if (++s_ticket == 0U) ++s_ticket;
        ticket = s_ticket;
        s_queue[s_write].ticket = ticket;
        s_queue[s_write].submitted_generation = s_generation;
        s_queue[s_write].owner = owner;
        s_queue[s_write].mask = mask;
        s_queue[s_write].result = 0;
        s_write = (uint8_t)((s_write + 1U) % 8U);
        ++s_count;
    }
    taskEXIT_CRITICAL();
    return ticket;
}

void app_control_arbiter_service(void)
{
    uint8_t budget = 4U;
    taskENTER_CRITICAL();
    while (s_count != 0U && budget-- != 0U)
    {
        arbiter_entry_t *e = &s_queue[s_read];
        uint8_t cancelled = e->result == -1;
        e->result = -1;
        if (!s_recovery_hold && !s_parameter_lock && !s_arm_work_lock && !cancelled && e->submitted_generation == s_generation &&
            s_owner == APP_CONTROL_OWNER_NONE && !s_stopping &&
            s_reason == APP_CONTROL_LOCK_NONE)
        {
            s_owner = e->owner; s_axis_mask = e->mask; s_preparing = 1U;
            if (++s_generation == 0U) ++s_generation;
            e->granted_generation = s_generation;
            e->result = 1;
        }
        s_read = (uint8_t)((s_read + 1U) % 8U); --s_count;
    }
    taskEXIT_CRITICAL();
}

int8_t app_control_arbiter_poll(uint32_t ticket, uint32_t *generation)
{
    unsigned i; int8_t result = -1;
    taskENTER_CRITICAL();
    for (i = 0U; ticket != 0U && i < 8U; ++i)
        if (s_queue[i].ticket == ticket)
        {
            result = s_queue[i].result;
            if (result == 1 && (s_owner != s_queue[i].owner ||
                s_generation != s_queue[i].granted_generation || s_stopping ||
                s_reason != APP_CONTROL_LOCK_NONE)) result = -1;
            if (result == 1 && generation != 0) *generation = s_generation;
            break;
        }
    taskEXIT_CRITICAL(); return result;
}

void app_control_arbiter_stop(app_control_owner_t owner)
{
    taskENTER_CRITICAL();
    if (s_owner == owner) { s_stopping = 1U; s_preparing = 0U; }
    taskEXIT_CRITICAL();
}

void app_control_arbiter_cancel(uint32_t ticket)
{
    unsigned i;
    taskENTER_CRITICAL();
    for (i = 0U; ticket != 0U && i < 8U; ++i)
        if (s_queue[i].ticket == ticket)
        {
            if (s_queue[i].result == 1 && s_preparing && s_owner == s_queue[i].owner &&
                s_generation == s_queue[i].granted_generation)
            { s_owner = APP_CONTROL_OWNER_NONE; s_axis_mask = s_preparing = 0U; ++s_generation; }
            s_queue[i].result = -1; break;
        }
    taskEXIT_CRITICAL();
}

void app_control_arbiter_recovery_hold(bool hold)
{
    taskENTER_CRITICAL(); s_recovery_hold = hold ? 1U : 0U; taskEXIT_CRITICAL();
}
uint8_t app_control_arbiter_clear_safety(void)
{
    uint8_t ok;
    taskENTER_CRITICAL(); ok = !s_recovery_hold && !s_parameter_lock && s_owner == APP_CONTROL_OWNER_NONE;
    if (ok) { s_reason = APP_CONTROL_LOCK_NONE; s_stopping = 0U; }
    taskEXIT_CRITICAL(); return ok;
}
void app_control_arbiter_cancel_pending(void)
{
    unsigned index;
    taskENTER_CRITICAL();
    if (s_owner == APP_CONTROL_OWNER_NONE)
    {
        for (index = 0U; index < 8U; ++index) s_queue[index].result = -1;
        s_count = s_read = s_write = 0U;
        if (++s_generation == 0U) ++s_generation;
    }
    taskEXIT_CRITICAL();
}
uint8_t app_control_arbiter_preparing(void) { return s_preparing; }
uint8_t app_control_arbiter_stopping(void) { return s_stopping; }

uint8_t app_control_arbiter_request(app_control_owner_t owner,
                                    uint8_t axis_mask,
                                    uint32_t *generation)
{
    uint8_t accepted = 0U;
    if (owner == APP_CONTROL_OWNER_NONE || axis_mask == 0U) return 0U;
    taskENTER_CRITICAL();
    if (!s_recovery_hold && !s_parameter_lock && !s_arm_work_lock && s_reason == APP_CONTROL_LOCK_NONE && !s_stopping &&
        s_owner == owner && s_axis_mask == axis_mask && s_preparing)
    {
        s_owner = owner;
        s_axis_mask = axis_mask;
        s_reason = APP_CONTROL_LOCK_NONE;
        if (!s_preparing) s_generation++;
        s_preparing = 0U;
        if (s_generation == 0U) s_generation++;
        accepted = 1U;
    }
    if (accepted && generation != NULL) *generation = s_generation;
    taskEXIT_CRITICAL();
    return accepted;
}

void app_control_arbiter_release(app_control_owner_t owner)
{
    taskENTER_CRITICAL();
    if (s_owner == owner)
    {
        s_owner = APP_CONTROL_OWNER_NONE;
        s_axis_mask = 0U;
        s_reason = APP_CONTROL_LOCK_NONE;
        s_stopping = s_preparing = 0U;
        s_generation++;
        if (s_generation == 0U) s_generation++;
    }
    taskEXIT_CRITICAL();
}

void app_control_arbiter_safety_takeover(app_control_lock_reason_t reason)
{
    if(s_arm_stop_callback)s_arm_stop_callback();
    taskENTER_CRITICAL();
    if (s_stop_event.result == 2U)
    {
        s_stop_event.result = 5U; s_stop_event.error = 14U;
        s_stop_event.completed_ms = HAL_GetTick();
    }
    s_stop_pending = 0U;
    s_owner = APP_CONTROL_OWNER_NONE;
    s_axis_mask = 0U;
    s_reason = reason;
    s_preparing = 0U;
    s_stopping = 1U;
    s_generation++;
    if (s_generation == 0U) s_generation++;
    taskEXIT_CRITICAL();
}

app_control_owner_t app_control_arbiter_owner(void) { return s_owner; }
uint8_t app_control_arbiter_axis_mask(void) { return s_axis_mask; }
uint32_t app_control_arbiter_generation(void) { return s_generation; }
app_control_lock_reason_t app_control_arbiter_lock_reason(void) { return s_reason; }

/* Serializes the no-owner observation with every authorization path. */
uint8_t app_control_arbiter_parameter_begin(void) {
    uint8_t ok;taskENTER_CRITICAL();
    ok=!s_parameter_lock && !s_arm_work_lock && !s_recovery_hold && s_owner==APP_CONTROL_OWNER_NONE &&
        !s_preparing && !s_stopping && !s_count && !s_stop_pending && s_reason==APP_CONTROL_LOCK_NONE;
    if(ok)s_parameter_lock=1U;taskEXIT_CRITICAL();return ok;
}
void app_control_arbiter_parameter_end(void) {taskENTER_CRITICAL();s_parameter_lock=0U;taskEXIT_CRITICAL();}
uint8_t app_control_arbiter_parameter_locked(void) {return s_parameter_lock;}

uint8_t app_control_arbiter_arm_begin(void) {
    uint8_t ok; unsigned i;
    taskENTER_CRITICAL();
    ok = !s_parameter_lock && !s_recovery_hold && s_owner == APP_CONTROL_OWNER_NONE &&
        !s_preparing && !s_stopping && !s_stop_pending && s_reason == APP_CONTROL_LOCK_NONE;
    if (ok) {
        s_arm_work_lock = 1U;
        for (i=0U;i<8U;++i) s_queue[i].result=-1;
        s_count=s_read=s_write=0U;
        if (++s_generation==0U) ++s_generation;
    }
    taskEXIT_CRITICAL(); return ok;
}
void app_control_arbiter_arm_lock(uint8_t locked) {
    unsigned i;
    taskENTER_CRITICAL();
    if (locked && !s_arm_work_lock) {
        /* Invalidate pending tickets without replacing an active stop event. */
        for (i=0U;i<8U;++i) s_queue[i].result=-1;
        s_count=s_read=s_write=0U;
        if (s_owner==APP_CONTROL_OWNER_NONE && !s_stopping && ++s_generation==0U) ++s_generation;
    }
    s_arm_work_lock=locked?1U:0U;
    taskEXIT_CRITICAL();
}
uint8_t app_control_arbiter_arm_locked(void) { return s_arm_work_lock; }
uint8_t app_control_arbiter_motion_pending(void) { return s_count || s_preparing; }
