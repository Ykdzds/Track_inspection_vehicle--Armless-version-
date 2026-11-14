/* First-cause fields from the production driver before emergency cleanup. */
#include "FreeRTOS.h"
static void enter_capture_critical(void);
#undef taskENTER_CRITICAL
#define taskENTER_CRITICAL() enter_capture_critical()
#define main previous_cold_startup_suite
#include "test_cold_startup.c"
#undef main

static uint32_t captured[24];
static unsigned captured_count;
static unsigned inject_feedback_preemption;
static void enter_capture_critical(void)
{
    if (inject_feedback_preemption) {
        inject_feedback_preemption = 0U;
        ticks += 20U;
        s_axes[0].speed_tick = s_axes[0].fault_tick = ticks;
        s_axes[0].snapshot.enable_feedback_tick = ticks;
    }
}
#if APP_LINK_DIAG_ACTIVE
void app_link_diag_startup_first(const uint32_t *words, uint8_t count)
{
    assert(count == 24U);
    if (captured_count++ == 0U) memcpy(captured, words, sizeof(captured));
}
#endif

static void seed_cause(void)
{
    ticks = 1234U;
    joint_seed(0U, 0U, 1UL << 27U, 0U);
    aimtor_cli_gate_init();
    s_cold_phase = SERVO_COLD_CLEAR_DONE;
    s_startup_stop[0].eligible = 1U;
    s_startup_stop[0].state = SERVO_STARTUP_STOP_CONFIGURING;
    memset(captured, 0, sizeof(captured)); captured_count = 0U;
    s_axes[0].speed_valid = s_axes[0].fault_valid = 1U;
    s_axes[0].speed_tick = ticks - 17U;
    s_axes[0].fault_tick = ticks - 23U;
    s_axes[0].snapshot.enable_feedback_valid = 1U;
    s_axes[0].snapshot.enable_feedback_tick = ticks - 31U;
    s_axes[0].snapshot.parameter_verified = 1U;
    s_axes[0].snapshot.disable_verified = 1U;
    aimtor_begin(SERVO_AXIS_LEFT, ACTION_CONFIGURE);
    aimtor_add(SERVO_AXIS_LEFT, AIMTOR_REG_ACCELERATION_TIME, 100U, 1U, 1U, 1U);
}

static void response_seed(modbus_response_t *response, uint32_t id,
                          uint16_t address, modbus_status_t status)
{
    memset(response, 0, sizeof(*response));
    response->context.transaction_id = s_pending_id = id;
    response->context.owner = AIMTOR_OWNER_ID;
    response->context.axis = s_pending_axis = SERVO_AXIS_LEFT;
    response->context.tag = s_pending_tag = AIMTOR_TAG_SEQUENCE;
    response->address = address;
    response->operation = MODBUS_OPERATION_WRITE_SINGLE;
    response->status = status;
}

static void consume(modbus_response_t *response)
{
    aimtor_test_enqueue_response(response);
    aimtor_handle_modbus_response();
}

int main(void)
{
    modbus_response_t response;
#if APP_LINK_DIAG_ACTIVE
    /* A higher priority feedback consumer can run just before the critical
     * observation begins.  Its new stamps must not be subtracted from an
     * earlier now value and wrap into a fictitious multi-day sample age. */
    seed_cause();
    inject_feedback_preemption = 1U;
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_BOARD_KEY, 0xFFU, 2U, 0U);
    assert(!inject_feedback_preemption && captured[3] == ticks);
    assert(captured[14] == 0U && captured[15] == 0U && captured[16] == 0U);

    seed_cause();
    s_pending_id = 0x12345U;
    s_pending_axis = SERVO_AXIS_LEFT; s_pending_tag = AIMTOR_TAG_SEQUENCE;
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_BOARD_KEY, 0xFFU, 2U, 0x20000U);
    aimtor_emergency_stop();
    assert(captured_count >= 2U && captured[8] == 0x12345U);
    assert(captured[1] == ((uint32_t)AIMTOR_STOP_SOURCE_BOARD_KEY << 24U | 0xFF0000U | 2U));
    assert(captured[6] >> 24U == ACTION_CONFIGURE);
    assert(captured[10] == ((uint32_t)AIMTOR_REG_ACCELERATION_TIME << 16U | 100U));
    assert(captured[14] == 17U && captured[15] == 23U && captured[16] == 31U);
    assert((captured[20] & 0x23U) == 3U); /* verified/disabled, not stop-latched */
    assert(s_axes[0].snapshot.stop_latched && !s_axes[0].snapshot.parameter_verified);

    seed_cause();
    memset(&response, 0, sizeof(response));
    response.context.transaction_id = s_pending_id = 0x45678U;
    response.context.owner = AIMTOR_OWNER_ID;
    response.context.axis = s_pending_axis = SERVO_AXIS_LEFT;
    response.context.tag = s_pending_tag = AIMTOR_TAG_SEQUENCE;
    response.address = AIMTOR_REG_ACCELERATION_TIME;
    response.operation = MODBUS_OPERATION_WRITE_SINGLE;
    response.status = MODBUS_TIMEOUT;
    aimtor_test_enqueue_response(&response);
    aimtor_handle_modbus_response();
    assert(captured_count && (captured[1] & 0xFFFFU) == 0x0501U);
    /* A response-triggered first cause must retain the actual response status
     * and transaction ID, rather than the adapter's pre-response BUSY value. */
    assert(captured[2] == MODBUS_TIMEOUT);
    assert(captured[8] == response.context.transaction_id);

    /* A new non-response stop must not inherit the previous response ID. */
    memset(captured, 0, sizeof(captured)); captured_count = 0U;
    assert(!s_pending_id && !s_diag_response_id);
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_UNKNOWN, 0xFFU, 0U, 0U);
    assert(captured_count == 1U && captured[8] == 0U);

    seed_cause();
    response_seed(&response, 0x56789U, AIMTOR_REG_ACCELERATION_TIME, MODBUS_OK);
    response.operation = MODBUS_OPERATION_READ_HOLDING;
    response.quantity = 1U; response.data_length = 3U;
    response.data[0] = 2U; response.data[1] = 0U; response.data[2] = 99U;
    s_axes[0].reading_back = 1U;
    consume(&response);
    assert((captured[1] & 0xFFFFU) == 0x0502U);
    assert(captured[2] == ((uint32_t)AIMTOR_REG_ACCELERATION_TIME << 16U | 99U));
    assert(captured[10] == ((uint32_t)AIMTOR_REG_ACCELERATION_TIME << 16U | 100U));
    assert(captured[8] == response.context.transaction_id);
    assert(!s_axes[0].snapshot.parameter_verified && !s_axes[0].snapshot.busy);
    assert(s_axes[0].snapshot.last_result == SERVO_NOT_READY);

    /* The first two failed disable attempts remain retryable.  At the third,
     * the frozen action/stage must precede their destructive reset. */
    seed_cause();
    s_startup_stop[0].state = SERVO_STARTUP_STOP_DISABLING;
    for (unsigned attempt = 1U; attempt <= 3U; ++attempt) {
        s_startup_stop[0].attempts = (uint8_t)attempt;
        aimtor_begin(SERVO_AXIS_LEFT, ACTION_STARTUP_DISABLE);
        aimtor_add(SERVO_AXIS_LEFT, AIMTOR_REG_SERVO_ENABLE, 0U, 1U, 1U, 0U);
        response_seed(&response, 0x60000U + attempt, AIMTOR_REG_SERVO_ENABLE, MODBUS_TIMEOUT);
        consume(&response);
        if (attempt < 3U) {
            assert(!captured_count);
            assert(s_startup_stop[0].state == SERVO_STARTUP_STOP_DISABLING);
        }
    }
    assert((captured[1] & 0xFFFFU) == 0x0101U && captured[2] == MODBUS_TIMEOUT);
    assert(captured[4] >> 24U == SERVO_STARTUP_STOP_DISABLING);
    assert(captured[6] >> 24U == ACTION_STARTUP_DISABLE);
    assert(captured[8] == response.context.transaction_id);
    assert(s_startup_stop[0].state == SERVO_STARTUP_STOP_FAILED);
    assert(s_startup_stop[0].failure == 1U && !s_axes[0].snapshot.enabled);

    seed_cause();
    s_boot_zero[0].state = SERVO_BOOT_ZERO_WRITING;
    for (unsigned attempt = 1U; attempt <= 3U; ++attempt) {
        s_boot_zero[0].attempts = (uint8_t)attempt;
        aimtor_begin(SERVO_AXIS_LEFT, ACTION_BOOT_ZERO);
        aimtor_add(SERVO_AXIS_LEFT, AIMTOR_REG_HOME_METHOD,
                   AIMTOR_HOME_RECORD_CURRENT, 1U, 1U, 0U);
        response_seed(&response, 0x70000U + attempt, AIMTOR_REG_HOME_METHOD, MODBUS_TRANSPORT_ERROR);
        consume(&response);
        if (attempt < 3U) {
            assert(!captured_count);
            assert(s_boot_zero[0].state == SERVO_BOOT_ZERO_WRITING);
        }
    }
    assert((captured[1] & 0xFFFFU) == 0x0202U);
    assert(captured[2] == ((uint32_t)AIMTOR_REG_HOME_METHOD << 16U | MODBUS_TRANSPORT_ERROR));
    assert((uint8_t)(captured[4] >> 16U) == SERVO_BOOT_ZERO_WRITING);
    assert(captured[6] >> 24U == ACTION_BOOT_ZERO);
    assert(captured[8] == response.context.transaction_id);
    assert(captured[10] == ((uint32_t)AIMTOR_REG_HOME_METHOD << 16U | AIMTOR_HOME_RECORD_CURRENT));
    assert(s_boot_zero[0].state == SERVO_BOOT_ZERO_FAILED);
    assert(s_boot_zero[0].failure == 2U && !s_axes[0].snapshot.enabled);
    puts("PASS startup first cause: real status/id, mismatch expected/actual, retry exhaustion before cleanup, no stale response id");
#else
    seed_cause();
    response_seed(&response, 0x45678U, AIMTOR_REG_ACCELERATION_TIME, MODBUS_TIMEOUT);
    consume(&response);
    aimtor_diag_startup_stop(AIMTOR_STOP_SOURCE_UNKNOWN, 0xFFU, 0U, 0U);
    assert(!captured_count && !s_pending_id);
    assert(!s_axes[0].snapshot.parameter_verified && !s_axes[0].snapshot.busy);
    assert(s_axes[0].snapshot.last_result == SERVO_NOT_READY);
    puts("PASS startup first-cause disabled: response failure behaviour preserved without capture");
#endif
    return 0;
}
