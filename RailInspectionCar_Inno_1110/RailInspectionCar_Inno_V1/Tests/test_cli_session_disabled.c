#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "app_cli_servo_session.h"
#include "app_control_arbiter.h"

static unsigned resets, stops;
static app_control_owner_t owner;
void aimtor_cli_gate_init(void) { ++resets; }
app_control_owner_t app_control_arbiter_owner(void) { return owner; }
uint8_t app_control_arbiter_submit_stop(uint16_t reason, uint16_t deceleration,
                                      uint32_t sequence, uint16_t command)
{
    assert(reason == 5U && deceleration == 500U && sequence == 0U && command == 0x10U);
    ++stops;
    return 1U;
}
int main(void)
{
    cli_servo_session_status_t status;
    cli_servo_request_t request;
    uint32_t session = 999U;
    assert(!APP_CLI_SERVO_ACTIVE);
    memset(&status, 0xA5, sizeof(status));
    memset(&request, 0, sizeof(request));
    app_cli_servo_session_init();
    assert(!app_cli_servo_session_active());
    assert(app_cli_servo_session_on(SERVO_AXIS_LEFT, &session) == SERVO_NOT_CONFIGURED);
    assert(session == 0U && !app_cli_servo_session_keepalive(999U));
    assert(app_cli_servo_session_off() == SERVO_OK);
    assert(app_cli_servo_session_request(NULL) == SERVO_INVALID_ARGUMENT);
    assert(app_cli_servo_session_request(&request) == SERVO_NOT_CONFIGURED);
    app_cli_servo_session_stop();
    app_cli_servo_session_abort(CLI_EXIT_SAFETY);
    app_cli_servo_session_service();
    assert(stops == 0U && resets == 0U);
    assert(!app_cli_servo_session_get_status(NULL));
    assert(app_cli_servo_session_get_status(&status));
    assert(status.state == CLI_SESSION_IDLE && status.reason == CLI_EXIT_NONE);
    assert(status.mode == SERVO_DEBUG_MODE_NONE && status.debug_state == SERVO_DEBUG_IDLE);
    assert(status.last_result == SERVO_OK && !status.authorized && !status.pending);
    assert(status.session_id == 0U && status.generation == 0U && status.remaining_ms == 0U);
    owner = APP_CONTROL_OWNER_HMI;
    assert(!strcmp(app_cli_servo_control_owner(), "hmi"));
    app_cli_servo_session_external_stop();
    assert(stops == 1U && owner == APP_CONTROL_OWNER_HMI);
    app_cli_servo_session_recovery_reset();
    assert(resets == 1U && !app_cli_servo_session_active());
    puts("PASS disabled CLI session: neutral valid status, no authorization, real external STOP and recovery gate");
    return 0;
}
