#ifndef APP_CLI_SERVO_SESSION_H
#define APP_CLI_SERVO_SESSION_H

#include "aimtor_servo.h"
#include "servo_debug_control.h"

typedef enum
{
    CLI_SESSION_IDLE = 0,
    CLI_SESSION_ENTERING,
    CLI_SESSION_READY,
    CLI_SESSION_STOPPING,
    CLI_SESSION_RESTORING,
    CLI_SESSION_LOCKED
} cli_session_state_t;

typedef enum
{
    CLI_EXIT_NONE = 0,
    CLI_EXIT_OFF,
    CLI_EXIT_TIMEOUT,
    CLI_EXIT_K1,
    CLI_EXIT_MANUAL,
    CLI_EXIT_SAFETY,
    CLI_EXIT_RESTORE_FAILED
} cli_session_reason_t;

typedef enum
{
    CLI_REQUEST_MODE = 0,
    CLI_REQUEST_ENABLE,
    CLI_REQUEST_SPEED,
    CLI_REQUEST_POSITION,
    CLI_REQUEST_PULSE,
    CLI_REQUEST_VERIFY,
    CLI_REQUEST_ZERO,
    CLI_REQUEST_SAVE
} cli_servo_request_kind_t;

typedef struct
{
    cli_servo_request_kind_t kind;
    servo_axis_t axis;
    servo_debug_mode_t mode;
    int32_t value;
    uint32_t frequency;
    uint16_t speed;
    uint16_t ramp;
    uint8_t forward;
    uint32_t generation;
} cli_servo_request_t;

typedef struct
{
    cli_session_state_t state;
    cli_session_reason_t reason;
    servo_axis_t axis;
    uint32_t session_id;
    uint32_t generation;
    uint32_t remaining_ms;
    servo_debug_mode_t mode;
    servo_debug_state_t debug_state;
    servo_result_t last_result;
    uint8_t authorized;
    uint8_t pending;
    uint8_t recoverable;
} cli_servo_session_status_t;

void app_cli_servo_session_init(void);
void app_cli_servo_session_recovery_reset(void);
servo_result_t app_cli_servo_session_on(servo_axis_t axis, uint32_t *session);
bool app_cli_servo_session_keepalive(uint32_t session);
servo_result_t app_cli_servo_session_off(void);
servo_result_t app_cli_servo_session_request(const cli_servo_request_t *request);
/* 停止优先于普通队列；急停先撤权，实物失能由通信任务尽力执行。 */
void app_cli_servo_session_stop(void);
void app_cli_servo_session_external_stop(void);
void app_cli_servo_session_abort(cli_session_reason_t reason);
void app_cli_servo_session_service(void);
bool app_cli_servo_session_get_status(cli_servo_session_status_t *status);
bool app_cli_servo_session_active(void);
const char *app_cli_servo_session_state_name(cli_session_state_t state);
const char *app_cli_servo_session_reason_name(cli_session_reason_t reason);
const char *app_cli_servo_control_owner(void);

#endif
