#define main hmi_regression_main
#include "test_hmi_integration.c"
#undef main

static app_system_snapshot_t observed;
static nrf_status_input_t radio_projection;
static nrf_snapshot_context_t radio_context;
static app_can_status_input_t can_projection;

int main(void)
{
    uint32_t sequence;
    fresh();
    /* Production snapshot must not report100% merely because startup booleans pass. */
    guard_permitted = false;
    app_system_snapshot_init();
    assert(lcd_task_handle == NULL);
    assert(sys_stack_words[6] == UINT32_MAX); /* Absent task has a retained neutral slot. */
    assert(app_system_snapshot_read(&observed));
    assert(!observed.init_complete && observed.init_progress != 100U && !observed.safety_ready);
    guard_permitted = true;
    tick += 10U; app_system_snapshot_service();
    assert(app_system_snapshot_read(&observed));
    assert(observed.init_complete && observed.init_progress == 100U);
    guard_permitted = false;
    tick += 10U; app_system_snapshot_service();
    assert(app_system_snapshot_read(&observed) && observed.init_complete && !observed.safety_ready);
    guard_permitted = true;
    tick += 10U; app_system_snapshot_service();
    assert(app_system_snapshot_read(&observed));
    sequence = observed.sequence;
    assert(sequence != 0U);
    assert(app_system_snapshot_read_nrf(&radio_projection, &radio_context));
    assert(app_system_snapshot_read_can(&can_projection));
    assert(radio_projection.snapshot_sequence == sequence);
    assert(can_projection.snapshot_sequence == sequence);
    assert(radio_projection.snapshot_ms == observed.now_ms);
    assert(can_projection.snapshot_ms == observed.now_ms);
    assert(memcmp(radio_projection.axis, observed.axis, sizeof(observed.axis)) == 0);
    assert(memcmp(can_projection.axis, observed.axis, sizeof(observed.axis)) == 0);
    tick = observed.now_ms + 9U;
    app_system_snapshot_service();
    assert(app_system_snapshot_read(&observed) && observed.sequence == sequence);
    tick++;
    app_system_snapshot_service();
    assert(app_system_snapshot_read(&observed) && observed.sequence == sequence + 1U);
    sequence = observed.sequence;
    sys_buffer[sys_active].owner = APP_CONTROL_OWNER_CAN;
    sys_buffer[sys_active].can_control.authorized = true;
    sys_buffer[sys_active].can_control_valid = 1U;
    sys_buffer[sys_active].can_control.target_mm_s = 100;
    sys_buffer[sys_active].can_control.commanded_rpm[0] = 10;
    sys_buffer[sys_active].can_control.commanded_rpm[1] = 11;
    sys_buffer[sys_active].can_control.sync.effective_error = 17;
    assert(app_system_snapshot_read_nrf(&radio_projection, &radio_context));
    assert(radio_context.authorized && radio_context.owner == APP_CONTROL_OWNER_CAN);
    assert(radio_context.target_rpm != 0 && radio_context.command_rpm[1] == 11);
    assert(radio_projection.remote.sync.effective_error == 17);
    sys_buffer[sys_active].owner = APP_CONTROL_OWNER_HMI;
    sys_buffer[sys_active].hmi_control.flags = 3U;
    sys_buffer[sys_active].hmi_control.target = 200;
    sys_buffer[sys_active].hmi_control.output[1] = 22;
    assert(app_system_snapshot_read_nrf(&radio_projection, &radio_context));
    assert(radio_context.authorized && radio_context.owner == APP_CONTROL_OWNER_HMI);
    assert(radio_context.command_rpm[1] == 22);
    sys_buffer[sys_active].hmi_control.flags = 1U;
    assert(app_system_snapshot_read_nrf(&radio_projection, &radio_context));
    assert(!radio_context.authorized);
    assert(radio_context.target_rpm == 0 && radio_context.command_rpm[0] == 0);
    sys_buffer[sys_active].hmi_control.flags = 3U;
    sys_buffer[sys_active].lock_reason = APP_CONTROL_LOCK_SAFETY;
    assert(app_system_snapshot_read_nrf(&radio_projection, &radio_context));
    assert(!radio_context.authorized && radio_context.target_rpm == 0);
    sys_buffer[sys_active].lock_reason = APP_CONTROL_LOCK_NONE;
    assert(app_system_snapshot_read(&observed) && observed.sequence == sequence);
    tick = observed.now_ms + APP_SYSTEM_SNAPSHOT_MAX_AGE_MS + 1U;
    assert(!app_system_snapshot_read(&observed));
    assert(!app_system_snapshot_read_nrf(&radio_projection, &radio_context));
    assert(!app_system_snapshot_read_can(&can_projection) && !can_projection.source_valid);
    app_system_snapshot_service();
    assert(app_system_snapshot_read(&observed));
    assert(observed.sequence == sequence + 1U);
    sequence = observed.sequence;
    snapshot_race = 1U;
    tick += APP_SYSTEM_SNAPSHOT_PERIOD_MS;
    app_system_snapshot_service();
    assert(app_system_snapshot_read(&observed) && observed.sequence == sequence);
    snapshot_owner = APP_CONTROL_OWNER_REMOTE;
    app_system_snapshot_service();
    assert(app_system_snapshot_read(&observed) && observed.sequence == sequence + 1U);
    assert(observed.owner == APP_CONTROL_OWNER_REMOTE && observed.arbiter_generation == snapshot_generation);
    {
        app_tune_input_t tune;app_tune_diag_system_t diagnostic;
        sys_buffer[sys_active].owner=APP_CONTROL_OWNER_REMOTE;
        sys_buffer[sys_active].remote_valid=1U;sys_buffer[sys_active].remote.authorized=1U;
        sys_buffer[sys_active].remote.link_valid=1U;sys_buffer[sys_active].axes_valid=1U;
        sys_buffer[sys_active].axis[0].speed_valid=1U;
        sys_buffer[sys_active].axis[0].speed_age_ms=1U;
        sys_buffer[sys_active].axis[0].speed_sample_tick=tick-1U;
        sys_buffer[sys_active].axis[1].speed_valid=0U;
        app_tune_snapshot(&tune);assert(tune.owner==2U && tune.authorized && tune.valid[0] && !tune.valid[1]);
        assert(app_system_snapshot_read_diagnostic(&diagnostic));
        assert(diagnostic.snapshot_fresh && diagnostic.control_authorized &&
            diagnostic.arbiter_generation==snapshot_generation);
        assert(tune.sample_ms[0]==HAL_GetTick()-1U);
        sys_buffer[sys_active].lock_reason=APP_CONTROL_LOCK_SAFETY;
        app_tune_snapshot(&tune);assert(!tune.authorized);
        app_system_snapshot_read_diagnostic(&diagnostic);assert(!diagnostic.control_authorized);
        tick+=101U;app_tune_snapshot(&tune);assert(!tune.valid[0] && !tune.authorized);
        app_system_snapshot_read_diagnostic(&diagnostic);assert(!diagnostic.snapshot_fresh);
        sys_buffer[sys_active].axis[1].speed_age_ms=UINT32_MAX;
        app_tune_snapshot(&tune);
        assert(tune.age[1]==UINT32_MAX && tune.sample_ms[1]==UINT32_MAX && !tune.valid[1]);
        sys_buffer[sys_active].axis[1].speed_age_ms=UINT32_MAX-1U;
        app_tune_snapshot(&tune);assert(tune.age[1]==UINT32_MAX && !tune.valid[1]);
    }
    assert(!app_system_snapshot_read(NULL));
    assert(!app_system_snapshot_read_nrf(NULL, &radio_context));
    assert(!app_system_snapshot_read_can(NULL));
    assert(unsafe_calls == 0U);
    printf("PASS unified snapshot: cadence, projections, owners, revoke, stale, recovery; size=%u bytes\n",
        (unsigned)sizeof(observed));
    return 0;
}
