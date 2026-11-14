#ifndef APP_CONTROL_ARBITER_H
#define APP_CONTROL_ARBITER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    APP_CONTROL_OWNER_NONE = 0,
    APP_CONTROL_OWNER_HMI = 1,
    APP_CONTROL_OWNER_REMOTE = 2,
    APP_CONTROL_OWNER_AUTO = 3,
    APP_CONTROL_OWNER_CLI = 4,
    APP_CONTROL_OWNER_CAN = 5
} app_control_owner_t;

typedef enum
{
    APP_CONTROL_LOCK_NONE = 0,
    APP_CONTROL_LOCK_READ_ONLY = 1,
    APP_CONTROL_LOCK_UNAUTHORIZED = 2,
    APP_CONTROL_LOCK_OWNER_BUSY = 3,
    APP_CONTROL_LOCK_SAFETY = 4,
    APP_CONTROL_LOCK_EXPIRED = 5
} app_control_lock_reason_t;

typedef struct
{
    /* Public millisecond timestamps use HAL_GetTick, never the RTOS epoch. */
    uint32_t generation, event, request_sequence, started_ms, completed_ms;
    uint32_t timeout_ms;
    app_control_owner_t owner, requester;
    uint16_t reason, command, result, error, zero_mask, enabled_mask;
    uint16_t deceleration_mm_s2;
} app_control_stop_t;

uint8_t app_control_arbiter_submit_global_stop(app_control_owner_t requester,
    uint16_t reason, uint16_t deceleration, uint32_t sequence, uint16_t command);
uint8_t app_control_arbiter_submit_stop(uint16_t reason, uint16_t deceleration,
    uint32_t sequence, uint16_t command);
uint8_t app_control_arbiter_take_stop(app_control_owner_t owner,
    app_control_stop_t *request);
void app_control_arbiter_finish_stop(app_control_owner_t owner,
    uint16_t result, uint16_t error, uint16_t zero_mask, uint16_t enabled_mask);
void app_control_arbiter_get_stop(app_control_stop_t *status);
void app_control_arbiter_stop_timeout(app_control_owner_t owner, uint32_t timeout);
void app_control_arbiter_init(void);
void app_control_arbiter_set_arm_stop(void (*callback)(void));
/* Shared atomic exclusion; only arm manager may clear a transport lock. */
uint8_t app_control_arbiter_arm_begin(void);
void app_control_arbiter_arm_lock(uint8_t locked);
uint8_t app_control_arbiter_arm_locked(void);
uint8_t app_control_arbiter_motion_pending(void);
uint8_t app_control_arbiter_parameter_begin(void);
void app_control_arbiter_parameter_end(void);
uint8_t app_control_arbiter_parameter_locked(void);
/* 所有任务只提交申请；伺服任务串行取得准备预约。 */
uint32_t app_control_arbiter_submit(app_control_owner_t owner, uint8_t mask);
void app_control_arbiter_service(void);
int8_t app_control_arbiter_poll(uint32_t ticket, uint32_t *generation);
void app_control_arbiter_cancel(uint32_t ticket);
void app_control_arbiter_cancel_pending(void);
void app_control_arbiter_stop(app_control_owner_t owner);
void app_control_arbiter_recovery_hold(bool hold);
uint8_t app_control_arbiter_clear_safety(void);
uint8_t app_control_arbiter_preparing(void);
uint8_t app_control_arbiter_stopping(void);
uint8_t app_control_arbiter_request(app_control_owner_t owner,
                                     uint8_t axis_mask,
                                     uint32_t *generation);
void app_control_arbiter_release(app_control_owner_t owner);
void app_control_arbiter_safety_takeover(app_control_lock_reason_t reason);
app_control_owner_t app_control_arbiter_owner(void);
uint8_t app_control_arbiter_axis_mask(void);
uint32_t app_control_arbiter_generation(void);
app_control_lock_reason_t app_control_arbiter_lock_reason(void);

#endif
