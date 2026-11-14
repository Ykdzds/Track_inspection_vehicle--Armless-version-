#define CAN_HMI_JOINT_MAIN previous_joint_main
#include "test_can_hmi_joint.c"

static void can_target(uint16_t sequence, int16_t target)
{
    uint8_t data[8] = {0};
    app_can_control_status_t status;
    assert(app_can_control_get_status(&status));
    can_protocol_write_u16(data, sequence);
    can_protocol_write_u16(data + 2U, (uint16_t)target);
    can_protocol_write_u16(data + 4U, status.motion_token);
    can_protocol_write_u16(data + 6U, 300U);
    assert(app_can_control_speed(data, HAL_GetTick()));
}

static void can_setup(void)
{
    unsigned index;
    reset_fixture();
    memset(&remote_input, 0, sizeof(remote_input));
    memset(&joint_hold, 0, sizeof(joint_hold));
    remote_input.link_up = 1U; remote_input.left_vertical = 1000U;
    joint_hold_progress = 1U; g_feedback_progress = 1U;
    app_remote_servo_init(); can_joint_init();
    production_step(0U); request_can(1U);
    for (index = 0U; index < 30U && !can_enabled; ++index) production_step(0U);
    assert(can_enabled);
    can_target(1U, 0);
    for (index = 0U; index < 10U; ++index) production_step(0U);
    assert(telemetry.control.first_zero_done);
    for (index = 0U; index < 100U; ++index)
    {
        if (index % 10U == 0U) can_target((uint16_t)(2U + index / 10U), 363);
        production_step(0U);
    }
    assert(telemetry.control.authorized);
}

#ifndef K5_RECOVERY_MAIN
#define K5_RECOVERY_MAIN main
#endif
static void idle_recovery_matrix(void)
{
    unsigned recovery, index;
    uint16_t mailbox[24];
    uint8_t quick[8] = {1U, 0U, 42U, 0U, 1U, 0U, 0U, 0U};
    for (recovery = 0U; recovery < 3U; ++recovery)
    {
        reset_fixture();
        memset(&remote_input, 0, sizeof(remote_input));
        memset(&joint_hold, 0, sizeof(joint_hold));
        remote_input.link_up = 1U; remote_input.left_vertical = 1000U;
        joint_hold_progress = 1U;
        app_remote_servo_init(); can_joint_init(); production_step(0U);
        assert(app_can_control_stop(quick, HAL_GetTick())); production_step(0U);
        assert(telemetry.control.stop_phase == 2U && !can_enabled);
        if (recovery == 0U) request_can(200U);
        else if (recovery == 1U)
        {
            make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 200U, 0, 0U);
            assert(app_hmi_control_submit(mailbox));
        }
        else remote_input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        production_step(0U);
        assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
        g_feedback_progress = 1U;
        for (index = 0U; index < 40U; ++index) production_step(0U);
        printf("IDLE recovery=%u owner=%u phase=%u hmi_error=%u emergency=%lu\n",
            recovery, app_control_arbiter_owner(), telemetry.control.stop_phase,
            telemetry.hmi.error, (unsigned long)g_emergency_calls);
        assert(app_control_arbiter_owner() == (recovery == 0U ? APP_CONTROL_OWNER_CAN :
            recovery == 1U ? APP_CONTROL_OWNER_HMI : APP_CONTROL_OWNER_REMOTE));
        assert(!g_emergency_calls && telemetry.control.target_mm_s == 0 &&
            telemetry.hmi.target == 0 && telemetry.remote.target_rpm == 0);
    }
}
static void idle_recovery_failures(void)
{
    unsigned recovery, scenario, index;
    uint16_t mailbox[24];
    uint8_t quick[8] = {1U, 0U, 42U, 0U, 1U, 0U, 0U, 0U};
    for (recovery = 0U; recovery < 3U; ++recovery)
        for (scenario = 0U; scenario < 3U; ++scenario)
    {
        reset_fixture();
        memset(&remote_input, 0, sizeof(remote_input));
        memset(&joint_hold, 0, sizeof(joint_hold));
        remote_input.link_up = 1U; remote_input.left_vertical = 1000U;
        joint_hold_progress = 1U;
        app_remote_servo_init(); can_joint_init(); production_step(0U);
        assert(app_can_control_stop(quick, HAL_GetTick())); production_step(0U);
        if (recovery == 0U) request_can(200U);
        else if (recovery == 1U)
        {
            make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 200U, 0, 0U);
            assert(app_hmi_control_submit(mailbox));
        }
        else remote_input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        production_step(0U);
        if (scenario == 0U)
        {
            advance_ms(APP_CAN_ACQUIRE_TIMEOUT_MS + APP_SERVO_STATUS_MAX_AGE_MS);
            production_step(0U);
        }
        else if (scenario == 1U)
        {
            app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
            production_step(0U);
        }
        else
        {
            if (recovery == 0U) app_can_control_link(43U, 2U, 8U, HAL_GetTick(), true, true);
            else if (recovery == 1U)
            {
                make_mailbox(mailbox, TEST_CMD_STOP, 0U, 201U, 0, 0U);
                assert(app_hmi_control_submit(mailbox));
            }
            else remote_input.link_up = 0U;
            if (recovery == 0U)
            { app_can_control_pre_service(); app_can_control_service(); }
            else production_step(0U);
            remote_input.link_up = 1U;
        }
        g_feedback_progress = 1U;
        for (index = 0U; index < 40U; ++index) production_step(0U);
        printf("IDLE-REJECT recovery=%u scenario=%u owner=%u lock=%u\n", recovery,
            scenario, app_control_arbiter_owner(), app_control_arbiter_lock_reason());
        assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
        assert(!can_enabled && !app_hmi_control_session() && !telemetry.remote.authorized);
    }
}
static void global_matrix(void)
{
    unsigned owner, source, recovery, index;
    uint16_t mailbox[24];
    uint8_t quick[8] = {1U, 0U, 42U, 0U, 1U, 0U, 0U, 0U};
    for (owner = 0U; owner < 3U; ++owner)
        for (source = 0U; source < 4U; ++source)
          for (recovery = 0U; recovery < 3U; ++recovery)
        {
            app_control_stop_t original, current;
            if (owner == 0U) can_setup();
            else
            {
                joint_setup(owner == 1U ? 100 : 0);
                can_joint_init();
                if (owner == 2U)
                {
                    joint_stop(0U);
                    joint_hold_progress = 1U;
                    remote_input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
                    for (index = 0U; index < 35U; ++index) production_step(0U);
                    assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_REMOTE);
                    remote_input.left_vertical = 1400U;
                    for (index = 0U; index < 30U; ++index) production_step(0U);
                }
            }
            for (index = 0U; index < 20U; ++index)
            {
                if (source == 0U) production_step(1U);
                else if (source == 1U)
                {
                    make_mailbox(mailbox, TEST_CMD_STOP, 0U, 100U + index, 0, 0U);
                    assert(app_hmi_control_submit(mailbox)); production_step(0U);
                }
                else if (source == 2U)
                {
                    can_protocol_write_u16(quick, (uint16_t)(1U + index));
                    assert(app_can_control_stop(quick, HAL_GetTick())); production_step(0U);
                }
                else
                {
                    uint8_t command[8] = {0U, 5U, 1U, 0U, 0U, 0U, 42U, 0U};
                    command[0] = (uint8_t)(100U + index);
                    assert(app_can_control_submit(command, HAL_GetTick(), 1U) == 0U);
                    production_step(0U);
                }
                app_control_arbiter_get_stop(&current);
                { app_can_control_result_t reply; while (app_can_control_take_result(&reply)) {} }
                if (!index) original = current;
                assert(current.event == original.event && current.started_ms == original.started_ms);
                assert(current.timeout_ms == original.timeout_ms || original.timeout_ms == 0U);
                assert(current.deceleration_mm_s2 == (source == 1U ? APP_HMI_EMERGENCY_STOP_DECEL_MM_S2 :
                    source == 0U ? APP_REMOTE_SERVO_K5_STOP_DECEL_MM_S2 :
                    (uint16_t)(APP_CAN_QUICK_STOP_DECEL_MPS2 * 1000.0f + 0.5f)));
            }
            for (index = 0U; index < 1000U && app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE; ++index)
                production_step(0U);
            app_control_arbiter_get_stop(&current);
            printf("MATRIX owner=%u source=%u result=%u lock=%u phase=%u emergency=%lu\n", owner, source,
                current.result, app_control_arbiter_lock_reason(), telemetry.control.stop_phase, (unsigned long)g_emergency_calls);
            assert(current.result == 3U && current.zero_mask == APP_SERVO_CONTROL_AXIS &&
                current.enabled_mask == APP_SERVO_CONTROL_AXIS);
            assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE && !g_emergency_calls);
            assert(!app_remote_servo_k1_latched());
            if (recovery == 1U)
            {
                make_mailbox(mailbox, TEST_CMD_ACQUIRE, 0U, 200U, 0, 0U);
                assert(app_hmi_control_submit(mailbox));
                for (index = 0U; index < 40U && !app_hmi_control_session(); ++index) production_step(0U);
                printf("HMI-RECOVER session=%lu owner=%u lock=%u error=%u emergency=%lu\n", (unsigned long)app_hmi_control_session(), app_control_arbiter_owner(), app_control_arbiter_lock_reason(), telemetry.hmi.error, (unsigned long)g_emergency_calls);
                assert(app_hmi_control_session() && app_control_arbiter_owner() == APP_CONTROL_OWNER_HMI);
                assert(telemetry.hmi.target == 0 && !g_emergency_calls);
                continue;
            }
            if (recovery == 2U)
            {
                remote_input.left_vertical = 1000U;
                remote_input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
                for (index = 0U; index < 40U && app_control_arbiter_owner() != APP_CONTROL_OWNER_REMOTE; ++index) production_step(0U);
                assert(app_control_arbiter_owner() == APP_CONTROL_OWNER_REMOTE);
                assert(telemetry.remote.target_rpm == 0 && !g_emergency_calls);
                continue;
            }
            request_can(200U);
            for (index = 0U; index < 30U && !can_enabled; ++index) production_step(0U);
            if (!can_enabled) printf("REGRANT owner=%u source=%u canstate=%u otherphase=%u arbowner=%u lock=%u hold=%u valid=%u latch=%u\n", owner, source, telemetry.control.state, telemetry.control.stop_phase, app_control_arbiter_owner(), app_control_arbiter_lock_reason(), joint_hold.state, joint_hold.valid, app_remote_servo_k1_latched());
            assert(can_enabled && joint_hold.state == SERVO_K1_HOLD_INACTIVE);
            can_target(1U, 0);
            for (index = 0U; index < 10U; ++index) production_step(0U);
            assert(telemetry.control.first_zero_done);
        }
}
static void communication_regrant_matrix(void)
{
    unsigned source, index;
    uint16_t mailbox[24];
    uint8_t request[8]={1U,4U,1U,0U,0U,0U,42U,0U};
    for(source=0U;source<3U;++source)
    {
        reset_fixture(); memset(&remote_input,0,sizeof(remote_input));
        remote_input.link_up=1U;
        remote_input.left_vertical=remote_input.left_horizontal=remote_input.right_vertical=remote_input.right_horizontal=1000U;
        memset(&joint_hold,0,sizeof(joint_hold));
        app_remote_servo_init(); can_joint_init();
        joint_recovery.state=SERVO_RECOVERY_READY;
        app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
        app_control_arbiter_recovery_hold(true);
        make_mailbox(mailbox,TEST_CMD_ACQUIRE,0U,200U,0,0U);
        assert(!app_hmi_control_submit(mailbox));
        assert(app_can_control_submit(request,HAL_GetTick(),1U)!=0U);
        assert(!app_control_arbiter_clear_safety());
        assert(!app_control_arbiter_submit(APP_CONTROL_OWNER_CLI,APP_SERVO_CONTROL_AXIS));
        production_step(0U);
        remote_input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++; production_step(0U);
        assert(joint_recovery.state==SERVO_RECOVERY_REQUESTED && joint_recovery.source==1U);
        assert(!can_enabled && !app_hmi_control_session() && !telemetry.remote.authorized);
        app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
        app_control_arbiter_cancel_pending();
        app_hmi_control_init(); app_can_control_recovery_reset(); app_remote_servo_recovery_reset();
        for(unsigned a=0U;a<2U;++a)
        {g_snapshot[a].enabled=g_snapshot[a].enable_feedback=g_snapshot[a].parameter_verified=0U;g_snapshot[a].disable_verified=1U;g_snapshot[a].actual_speed_rpm=0;g_snapshot[a].control_mode=SERVO_CONTROL_RS485_VELOCITY;}
        joint_recovery.state=SERVO_RECOVERY_DONE;
        app_control_arbiter_recovery_hold(false); assert(app_control_arbiter_clear_safety());
        g_feedback_progress=1U;
        production_step(0U); assert(!can_enabled && !app_hmi_control_session() && !telemetry.remote.authorized);
        if(source==0U) request_can(201U);
        else if(source==1U)
        {make_mailbox(mailbox,TEST_CMD_ACQUIRE,0U,201U,0,0U);assert(app_hmi_control_submit(mailbox));}
        else remote_input.long_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
        for(index=0U;index<50U;++index) production_step(0U);
        assert(app_control_arbiter_owner()==(source==0U?APP_CONTROL_OWNER_CAN:source==1U?APP_CONTROL_OWNER_HMI:APP_CONTROL_OWNER_REMOTE));
        assert(telemetry.control.target_mm_s==0 && telemetry.hmi.target==0 && telemetry.remote.target_rpm==0);
        if(source==0U){assert(!app_can_control_speed((uint8_t[8]){1U,0U,100U,0U,0U,0U,44U,1U},HAL_GetTick())); can_target(1U,0);for(index=0U;index<10U;++index)production_step(0U);assert(telemetry.control.first_zero_done);}
    }
    memset(&joint_recovery,0,sizeof(joint_recovery));
    puts("PASS communication reset/regrant: old inboxes rejected, K5 reset only, disabled preparation, CAN/HMI/REMOTE explicit regrant and zero targets");
}

int K5_RECOVERY_MAIN(void)
{
    setbuf(stdout, NULL);
    unsigned index;
    uint32_t event, started, timeout;
    can_setup(); production_step(1U); production_step(0U);
    assert(telemetry.control.stop_reason == 1U);
    event = telemetry.control.stop_generation;
    started = g_tick - telemetry.control.stop_elapsed_ms;
    timeout = telemetry.control.stop_timeout_ms;
    for (index = 0U; index < 1000U && app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE; ++index)
    {
        production_step(index % 7U == 0U ? 1U : 0U);
        assert(telemetry.control.stop_generation == event);
        assert(telemetry.control.stop_timeout_ms == timeout);
        if (app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE)
            assert(g_tick - telemetry.control.stop_elapsed_ms - started <= 40U);
    }
    assert(telemetry.control.stop_phase == 4U && !g_emergency_calls);
    assert(!app_remote_servo_k1_latched());
    assert(telemetry.life == 1U && telemetry.owner == 0U);
    assert(app_can_status_record(&telemetry, 0x11U, &can_record));
    assert(!(can_protocol_read_u32(can_record.payload + 12U) & 520U));
    assert(can_record.payload[34] == 4U && can_record.payload[35] == 0U);
    request_can(2U);
    for (index = 0U; index < 30U && !can_enabled; ++index) production_step(0U);
    assert(can_enabled && telemetry.control.authorized);
    assert(joint_hold.state == SERVO_K1_HOLD_INACTIVE);
    can_target(1U, 0);
    for (index = 0U; index < 10U; ++index) production_step(0U);
    can_target(2U, 100);
    for (index = 0U; index < 10U; ++index) production_step(0U);
    assert(!g_emergency_calls && telemetry.control.authorized);
    can_setup(); production_step(1U); joint_hold_progress = 0U;
    for (index = 0U; index < 1000U && telemetry.control.state != CAN_CONTROL_HOLD; ++index) production_step(0U);
    assert(telemetry.control.state == CAN_CONTROL_HOLD && app_remote_servo_k1_latched());
    event = telemetry.control.stop_generation;
    assert(!app_remote_servo_can_k5_stop_complete(event + 1U, APP_SERVO_CONTROL_AXIS));
    assert(!app_remote_servo_can_k5_stop_complete(event, (uint8_t)(APP_SERVO_CONTROL_AXIS ^ 1U)));
    assert(!app_remote_servo_can_k5_stop_complete(event, APP_SERVO_CONTROL_AXIS));
    joint_hold_progress = 1U;
    assert(aimtor_k1_hold_get_status(&joint_hold) && joint_hold.valid);
    remote_input.short_counts[APP_REMOTE_STOP_AUTH_KEY_INDEX]++;
    app_remote_servo_update(&remote_input);
    assert(!app_remote_servo_can_k5_stop_complete(event, APP_SERVO_CONTROL_AXIS));
    assert(app_remote_servo_k1_latched());
    for (index = 0U; index < 30U && app_control_arbiter_owner() != APP_CONTROL_OWNER_NONE; ++index) production_step(0U);
    assert(!app_remote_servo_k1_latched() && telemetry.control.stop_generation == event && telemetry.control.stop_phase == 4U);
    assert(!app_remote_servo_can_k5_stop_complete(event, APP_SERVO_CONTROL_AXIS));
    puts("PASS wrong/late event, wrong axes, invalid hold, pending new K5 cannot consume old completion");
    can_setup(); production_step(1U); joint_hold_progress = 0U;
    for (index = 0U; index < 1000U && telemetry.control.state != CAN_CONTROL_HOLD; ++index) production_step(0U);
    assert(telemetry.control.state == CAN_CONTROL_HOLD && app_remote_servo_k1_latched());
    event = telemetry.control.stop_generation; joint_hold_progress = 1U;
    assert(aimtor_k1_hold_get_status(&joint_hold) && joint_hold.valid);
    app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
    assert(!app_remote_servo_can_k5_stop_complete(event, APP_SERVO_CONTROL_AXIS));
    assert(app_remote_servo_k1_latched() && app_control_arbiter_lock_reason() == APP_CONTROL_LOCK_SAFETY);
    puts("PASS genuine arbiter safety takeover cannot be cleared by CAN K5 completion");
    puts("PASS CAN owner -> real remote K5 -> stop/release -> regrant");
    communication_regrant_matrix();
    for (unsigned clock_case = 0U; clock_case < 4U; ++clock_case)
    {
        g_hal_offset = clock_case == 0U ? 6000 : clock_case == 1U ? -1 : clock_case == 2U ? 1 : 0;
        global_matrix();
    }
    g_hal_offset = 0;
    idle_recovery_matrix();
    idle_recovery_failures();
    return 0;
}
