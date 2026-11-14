/* Production startup order + real safety/Modbus executors. Only wire/HAL are
 * mocked; every request, readback and safety proof uses production code. */
#define main existing_cold_startup_regression
#define modbus_submit startup_submit_audited
#include "test_cold_startup.c"
#undef modbus_submit
#undef main

/* Audit production submissions with their real transaction contexts, so the
 * independent safety queue is distinguished from ordinary servo feedback. */
static uint8_t audit_startup_steps, audit_readback_pending[3];
static uint16_t audit_readback_address[3], audit_readback_count[3];
static unsigned audited_steps[3], audited_feedback[3];
modbus_status_t modbus_submit(const modbus_request_t *request, TickType_t wait_ticks);

modbus_status_t startup_submit_audited(const modbus_request_t *request,
                                     TickType_t wait_ticks)
{
    modbus_status_t result = modbus_submit(request, wait_ticks);
    if (audit_startup_steps && result == MODBUS_OK) {
        unsigned slave = request->slave;
        uint16_t tag = request->context.tag;
        assert(slave == 1U || slave == 2U);
        if (tag == AIMTOR_TAG_SPEED || tag == AIMTOR_TAG_FAULT ||
            tag == AIMTOR_TAG_HOLD_ENABLE || tag == AIMTOR_TAG_POSITION) {
            assert(!audit_readback_pending[1] && !audit_readback_pending[2]);
            ++audited_feedback[slave];
        }
        if (tag == AIMTOR_TAG_SEQUENCE) {
            const aimtor_axis_state_t *state = &s_axes[slave - 1U];
            assert(state->index < state->count);
            if (request->function == 3U && audit_readback_pending[slave]) {
                assert(request->address == audit_readback_address[slave]);
                assert(request->quantity == audit_readback_count[slave]);
                audit_readback_pending[slave] = 0U;
                ++audited_steps[slave];
            } else if (request->function != 3U && state->steps[state->index].compare) {
                assert(!audit_readback_pending[slave]);
                audit_readback_pending[slave] = 1U;
                audit_readback_address[slave] = request->address;
                audit_readback_count[slave] = state->steps[state->index].count;
            }
        }
    }
    return result;
}

static void production_seed(TickType_t start)
{
    ticks = start;
    joint_seed(0U, 0U, 1UL << 27U, 0U);
    /* The public gate is initialized in production before startup services. */
    aimtor_cli_gate_init();
}

static void wait_for_enabling(void)
{
    TickType_t start = ticks;
    while (s_hold[0].state != SERVO_STARTUP_HOLD_ENABLING &&
           s_hold[1].state != SERVO_STARTUP_HOLD_ENABLING &&
           ticks - start < 15000U) joint_pump(1U);
    assert(s_hold[0].state == SERVO_STARTUP_HOLD_ENABLING ||
           s_hold[1].state == SERVO_STARTUP_HOLD_ENABLING);
}

static void wait_for_startup_ready(void)
{
    TickType_t start = ticks;
    while (!(aimtor_cold_clear_ready() && aimtor_startup_stop_ready() &&
             aimtor_boot_zero_ready() && aimtor_startup_hold_ready()) &&
           ticks - start < 15000U)
        joint_pump(1U);
    assert(aimtor_cold_clear_ready() && aimtor_startup_stop_ready() &&
           aimtor_boot_zero_ready() && aimtor_startup_hold_ready());
}

static void verify_startup_phase_and_jitter(TickType_t phase)
{
    TickType_t start, health;
    unsigned turn = 0U, axis;
    memset(audit_readback_pending, 0, sizeof(audit_readback_pending));
    memset(audited_steps, 0, sizeof(audited_steps));
    memset(audited_feedback, 0, sizeof(audited_feedback));
    production_seed(phase);
    audit_startup_steps = 1U;
    start = health = ticks;
    while (!aimtor_startup_hold_ready() && ticks - start < 15000U) {
        for (axis = 0U; axis < APP_HEARTBEAT_COUNT; ++axis)
            app_health_beat((app_heartbeat_t)axis);
        if (ticks - health >= 20U) {
            app_health_watchdog_service(); health = ticks;
        }
        aimtor_service(); modbus_process(); aimtor_handle_modbus_response();
        /* Deterministic scheduler jitter, separate from the wire response
         * delay already injected by TEST_POLL_WIRE_DELAY_MS. */
        ticks += 1U + turn++ % 3U;
    }
    audit_startup_steps = 0U;
    assert(aimtor_cold_clear_ready() && aimtor_startup_stop_ready() &&
           aimtor_boot_zero_ready() && aimtor_startup_hold_ready());
    assert(!aimtor_motion_allowed());
    for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis) {
        assert(audited_steps[axis + 1U] > 0U && audited_feedback[axis + 1U] > 0U);
        assert(!audit_readback_pending[axis + 1U]);
        assert(s_poll_stats[axis][0].succeeded && s_poll_stats[axis][1].succeeded &&
               s_poll_stats[axis][2].succeeded);
    }
}

int main(void)
{
    uint8_t axis;
    TickType_t deadline;
    verify_startup_phase_and_jitter(37U);
    verify_startup_phase_and_jitter(123U);
    production_seed(0U);
    wait_for_startup_ready();
    joint_pump(65000U);
    assert(aimtor_cold_clear_ready() && aimtor_startup_stop_ready() &&
           aimtor_boot_zero_ready() && aimtor_startup_hold_ready());
    assert(!aimtor_motion_allowed() && !s_hmi_authorized && !s_can_authorized);
    assert(registers[1][AIMTOR_REG_SPEED_COMMAND] == 0U &&
           registers[2][AIMTOR_REG_SPEED_COMMAND] == 0U);
    joint_pump(60000U);
    assert(aimtor_startup_hold_ready());
    for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis) {
        assert(s_axes[axis].snapshot.enabled && aimtor_fresh(&s_axes[axis]));
        assert(s_axes[axis].snapshot.enable_feedback_valid &&
               s_axes[axis].snapshot.enable_feedback);
        assert(s_poll_stats[axis][0].succeeded && s_poll_stats[axis][1].succeeded &&
               s_poll_stats[axis][2].succeeded);
    }
    aimtor_set_motion_authorized(true);
    assert(aimtor_motion_allowed());
    /* Already enabled protection must remain the original 500 ms. */
    ticks += pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS);
    aimtor_service();
    assert(!aimtor_motion_allowed());

    production_seed(UINT32_MAX - 700U);
    wait_for_startup_ready();
    joint_pump(60000U);
    assert(aimtor_startup_hold_ready());

    /* An old cache before enable holds zero/disabled, obtains new proofs,
     * and neither latches a stop nor extends its original startup deadline. */
    production_seed(0U);
    wait_for_enabling();
    axis = s_hold[0].state == SERVO_STARTUP_HOLD_ENABLING ? 0U : 1U;
    deadline = s_hold_deadline;
    s_axes[axis].speed_tick = ticks - pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS);
    s_axes[axis].fault_tick = ticks - pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS);
    s_axes[axis].snapshot.enable_feedback_tick = ticks - pdMS_TO_TICKS(APP_SERVO_STATUS_MAX_AGE_MS);
    aimtor_startup_hold_service(ticks);
    assert(s_hold[axis].state == SERVO_STARTUP_HOLD_ENABLING &&
           !s_axes[axis].snapshot.enabled && !s_axes[axis].snapshot.stop_latched);
    assert(s_hold_deadline == deadline);
    wait_for_startup_ready();
    assert(s_hold_deadline == deadline);
    joint_pump(60000U);
    assert(aimtor_startup_hold_ready());

    production_seed(0U);
    wait_for_enabling();
    axis = s_hold[0].state == SERVO_STARTUP_HOLD_ENABLING ? 0U : 1U;
    s_axes[axis].snapshot.actual_speed_rpm = 1;
    s_axes[axis].speed_tick = s_axes[axis].fault_tick = ticks;
    aimtor_startup_hold_service(ticks);
    assert(s_hold[axis].state == SERVO_STARTUP_HOLD_FAILED);

    production_seed(0U);
    wait_for_enabling();
    axis = s_hold[0].state == SERVO_STARTUP_HOLD_ENABLING ? 0U : 1U;
    s_axes[axis].snapshot.parameter_verified = 0U;
    s_axes[axis].speed_tick = s_axes[axis].fault_tick = ticks;
    s_axes[axis].snapshot.enable_feedback_tick = ticks;
    aimtor_startup_hold_service(ticks);
    assert(s_hold[axis].state == SERVO_STARTUP_HOLD_FAILED);

    production_seed(0U);
    wait_for_enabling();
    deadline = s_hold_deadline;
    ticks = deadline;
    aimtor_startup_hold_service(ticks);
    assert(!aimtor_startup_hold_ready() && s_hold[0].state == SERVO_STARTUP_HOLD_FAILED);

    printf("PASS production startup wire=%ums: phases37/123+jitter1..3, parameter/readback ordering, dual safety/config/zero/hold, authorization gate, wrap, stale refresh, fixed deadline, nonzero, bad proof, runtime age\n",
           (unsigned)TEST_POLL_WIRE_DELAY_MS);
    return 0;
}

