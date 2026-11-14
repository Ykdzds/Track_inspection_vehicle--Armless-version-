#define main existing_can_test_main
#include "test_can_control.c"
#undef main
#include "app_can_status.h"
#include "app_can_config.h"
#include "can_upper_protocol.h"
#include "can_upper_control.h"
#include "can_upper_ui.h"
#include "can_link.h"
#include "can_upper.h"
typedef struct { uint16_t id; uint8_t data[8]; uint32_t cookie; } WireFrame;
static WireFrame upper_wire[8192], lower_wire[8192];
static unsigned upper_read, upper_write, lower_read, lower_write;
static UpperProtocol_t sim;
static UpperControl_t controller;
static can_link_t link;
static can_upper_cache_t cache;
static app_can_status_input_t telemetry;
static uint8_t lower_sequence;
static uint32_t cap_count, first_cap, ready_time;
static bool pause_targets, pause_heartbeats;
#ifdef F103_PAIR_REAL_BUS
void BusBridge_Init(void);
void BusBridge_FailTarget(void);
void BusBridge_DropMotion(void);
int BusBridge_Send(uint16_t id, const uint8_t *data);
void BusBridge_Service(uint32_t now, void (*deliver)(uint16_t, const uint8_t *), void (*outcome)(uint16_t, const uint8_t *, unsigned));
static void BusDeliver(uint16_t id, const uint8_t *data) { can_link_receive(&link, id, data, tick); }
static void BusOutcome(uint16_t id, const uint8_t *data, unsigned outcome)
{
    if (outcome != UPPER_TX_CANCELLED && outcome != UPPER_TX_REPLACED)
        Upper_TxResult(&sim, id, data, outcome == UPPER_TX_OK, tick);
    UpperControl_TxOutcome(&controller, id, data, (UpperTxOutcome_t)outcome, tick);
    if (controller.state == SIM_FAULT) BusBridge_DropMotion();
}
#endif
static uint8_t sim_send(void *context, uint16_t id, const uint8_t data[8])
{
    WireFrame *frame;
    (void)context;
    if ((id == 0x111U && pause_targets) || (id == 0x100U && pause_heartbeats)) return 0U;
#ifdef F103_PAIR_REAL_BUS
    if (id == 0x310U && !first_cap) { first_cap = tick; assert(link.phase == CAN_LINK_READY); }
    (void)frame;
    return (uint8_t)BusBridge_Send(id, data);
#else
    assert(upper_write < 8192U); frame = &upper_wire[upper_write++]; frame->id = id;
    memcpy(frame->data, data, 8U);
    if (id == 0x310U && !first_cap) { first_cap = tick; assert(link.phase == CAN_LINK_READY); }
    return 1U;
#endif
}
static bool lower_send(void *context, uint16_t id, const uint8_t data[8], uint32_t cookie, uint32_t deadline)
{
    WireFrame *frame;
    (void)context; (void)deadline;
    assert(lower_write < 8192U); frame = &lower_wire[lower_write++]; frame->id = id; frame->cookie = cookie;
    memcpy(frame->data, data, 8U); return true;
}
static void update_telemetry(void)
{
    unsigned axis;
    telemetry.now_ms = telemetry.snapshot_ms = telemetry.sample_tick = tick;
    telemetry.boot_id = link.lower_boot_id; telemetry.upper_boot_id = link.upper_boot_id;
    telemetry.token = link.token; telemetry.can_link_ready = link.phase == CAN_LINK_READY;
    telemetry.config_revision = link.config_revision;
    telemetry.arbiter_owner = app_control_arbiter_owner(); telemetry.arbiter_generation = app_control_arbiter_generation();
    telemetry.lock_reason = app_control_arbiter_lock_reason(); telemetry.arbiter_axes = APP_SERVO_CONTROL_AXIS;
    telemetry.selected = APP_SERVO_CONTROL_AXIS; telemetry.source_valid = true; telemetry.gates = 31U;
    assert(app_can_control_get_status(&telemetry.control));
    for (axis = 0U; axis < 2U; ++axis) telemetry.axis[axis] = feedback_axis[axis];
    app_can_status_describe(&telemetry);
}
static bool record_builder(void *context, uint8_t type, can_protocol_record_t *output, uint32_t now)
{
    (void)context; (void)now; update_telemetry();
    return app_can_status_record(&telemetry, type, output);
}
static bool upper_record(void *context, const can_protocol_record_t *record, uint32_t now)
{
    (void)context; if (record->type == 2U) cap_count++;
    return can_upper_publish(&cache, record, now, link.lower_boot_id, link.upper_boot_id);
}
static void refresh_control_link(void)
{
    bool strategy;
    can_upper_snapshot_t capability;
    strategy = can_upper_get(&cache, 2U, tick, &capability) && capability.valid && (can_protocol_read_u32(capability.payload + 8U) & (1UL << 29U));
    app_can_control_link(link.token, link.generation, link.upper_boot_id, link.last_heartbeat_ms, link.phase == CAN_LINK_READY, strategy);
}
static uint8_t control_submit(void *context, const uint8_t data[8], uint32_t received, uint32_t generation)
{ (void)context; refresh_control_link(); return app_can_control_submit(data, received, generation); }
static bool speed_submit(void *context, const uint8_t data[8], uint32_t received)
{ (void)context; refresh_control_link(); return app_can_control_speed(data, received); }
static bool stop_submit(void *context, const uint8_t data[8], uint32_t received)
{ (void)context; refresh_control_link(); return app_can_control_stop(data, received); }
static void board_cycle(void)
{
    unsigned axis, sample;
    app_can_control_result_t result;
    for (axis = 0U; axis < 2U; ++axis)
    {
        servo_snapshot_t *feedback = &feedback_axis[axis];
        if (feedback->busy && !no_complete && (int32_t)(tick - due[axis]) >= 0)
        {
            feedback->busy = 0U; feedback->speed_write_completed = feedback->speed_write_sequence;
            feedback->speed_write_result = SERVO_OK;
            if (move_feedback) feedback->actual_speed_rpm = write_rpm[axis];
        }
        if (fresh_ok && tick % 10U == 0U)
        {
            ++feedback->speed_sequence; feedback->speed_sample_tick = tick; feedback->enable_feedback_tick = tick;
            for (sample = 1U; sample < SERVO_SPEED_SAMPLE_CAPACITY; ++sample) feedback->speed_samples[sample-1] = feedback->speed_samples[sample];
            feedback->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].speed_sequence = feedback->speed_sequence;
            feedback->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].sample_tick = tick;
            feedback->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].actual_speed_rpm = feedback->actual_speed_rpm;
            feedback->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].valid = 1U;
        }
        feedback->speed_age_ms = tick - feedback->speed_sample_tick;
        feedback->fault_age_ms = fresh_ok ? 0U : feedback->speed_age_ms;
    }
    refresh_control_link(); app_control_arbiter_service(); app_can_control_pre_service(); app_can_control_service();
    assert(app_can_control_get_status(&state));
    while (app_can_control_take_result(&result)) can_link_control_result(&link, result.sequence, result.command, result.result, result.phase, result.detail, tick);
}
static void pair_tick(void)
{
#ifdef F103_PAIR_REAL_BUS
    BusBridge_Service(tick, BusDeliver, BusOutcome);
#endif
    while (upper_read < upper_write)
    {
        WireFrame frame = upper_wire[upper_read++];
        can_link_receive(&link, frame.id, frame.data, tick);
        Upper_TxResult(&sim, frame.id, frame.data, 1U, tick);
        UpperControl_TxResult(&controller, frame.id, frame.data, 1U, tick);
    }
    while (lower_read < lower_write)
    {
        WireFrame frame = lower_wire[lower_read++];
        can_link_tx_result(&link, frame.cookie, true, tick);
        Upper_Receive(&sim, frame.id, 8U, frame.data, tick, tick);
        UpperControl_Receive(&controller, &sim, frame.id, frame.data, tick, tick);
    }
    upper_read = upper_write = lower_read = lower_write = 0U;
    board_cycle();
    if (tick % 10U == 0U) { Upper_Tick(&sim, tick, 1U); UpperControl_Tick(&controller, &sim, tick); }
    if (link.phase == CAN_LINK_READY && !ready_time) ready_time = tick;
    if ((link.phase == CAN_LINK_READY || link.phase == CAN_LINK_WAIT_HEARTBEAT) && tick % 100U == 0U)
    {
        uint8_t data[8]; unsigned index;
        static const uint16_t ids[] = {0x185,0x180,0x186,0x187,0x188};
        update_telemetry();
        for (index = 0U; index < 5U; ++index)
        { assert(app_can_status_basic(&telemetry, ids[index], lower_sequence++, data)); can_link_offer_basic(&link, ids[index], data, tick + 500U); }
        if (state.authorized || state.stop_phase || tick % 500U == 0U) can_link_request_record(&link, 0x11U);
        if (state.stop_generation) can_link_request_record(&link, 0x13U);
        if (tick % 100U == 0U) can_link_request_record_priority(&link, 0x53U);
    }
    if (tick % 2U == 0U) can_link_service(&link, tick);
}
static void run_for(uint32_t duration) { while (duration--) { ++tick; pair_tick(); } }
static void setup_pair(void)
{
    can_link_callbacks_t callbacks = {0};
    reset(); memset(&telemetry, 0, sizeof(telemetry));
#ifdef F103_PAIR_REAL_BUS
    BusBridge_Init();
#endif
    upper_read = upper_write = lower_read = lower_write = 0U; first_cap = ready_time = cap_count = 0U;
    pause_targets = pause_heartbeats = false;
    callbacks.send = lower_send; callbacks.build_record = record_builder; callbacks.publish_upper = upper_record;
    callbacks.submit_control = control_submit; callbacks.submit_speed = speed_submit; callbacks.submit_stop = stop_submit;
    can_upper_init(&cache);
    can_link_init(&link, &callbacks, 42U, 100U, 131U, 7U, app_can_capabilities(), 1000U, tick);
    Upper_Init(&sim, 84U, sim_send, NULL, NULL); UpperControl_Init(&controller, sim_send, NULL); UpperUi_Init();
    run_for(4000U);
    printf("READY upper=%u lower=%u caps=%lu errs=%lu owner=%u flags=%lx\n", sim.snapshot.state, link.phase, (unsigned long)cap_count, (unsigned long)sim.snapshot.record_errors, sim.snapshot.control_source, UpperControl_Record(&sim.snapshot,0x11U) ? (unsigned long)Upper_ReadU32(UpperControl_Record(&sim.snapshot,0x11U)->payload+12U) : 0UL);
    assert(sim.snapshot.state == UPPER_READY && link.phase == CAN_LINK_READY);
    assert(cap_count >= 1U && first_cap >= ready_time); assert(sim.snapshot.record_errors == 0U);
}
static void acquire_sim(void)
{
    UpperUiView_t page_view;
    unsigned page_index;
    UpperUi_Publish(&controller, &sim, tick, &page_view);
    while (page_view.page != 2U)
    {
        UpperUi_ScanKeys(tick, 1U, &controller, &sim); run_for(21U);
        UpperUi_ScanKeys(tick, 1U, &controller, &sim); run_for(21U);
        UpperUi_ScanKeys(tick, 0U, &controller, &sim); run_for(21U);
        UpperUi_ScanKeys(tick, 0U, &controller, &sim);
        UpperUi_Publish(&controller, &sim, tick, &page_view);
    }
    UpperUi_ScanKeys(tick, 1U, &controller, &sim);
    for (page_index = 0U; page_index < 84U; ++page_index)
    { run_for(10U); UpperUi_ScanKeys(tick, 1U, &controller, &sim); }
    UpperUi_ScanKeys(tick, 0U, &controller, &sim); run_for(21U);
    UpperUi_ScanKeys(tick, 0U, &controller, &sim);
    run_for(700U);
    printf("AUTH state=%u detail=%u token=%u F4=%u seq=%u zeroobs=%u\n", controller.state, controller.detail, controller.motion_token, state.state, controller.speed_sequence,controller.zero_observations);
    assert(controller.state == SIM_AUTHORIZED_ZERO && state.first_zero_done);
    assert(sim.snapshot.rejected == 0U);
}
static void speed_key(uint8_t long_press)
{
    UpperUi_ScanKeys(tick, 2U, &controller, &sim); run_for(21U);
    UpperUi_ScanKeys(tick, 2U, &controller, &sim); run_for(long_press ? 800U : 30U);
    UpperUi_ScanKeys(tick, 2U, &controller, &sim);
    UpperUi_ScanKeys(tick, 0U, &controller, &sim); run_for(21U);
    UpperUi_ScanKeys(tick, 0U, &controller, &sim);
}

static void verify_extended_speed(void)
{
    static const int16_t targets[] = {200,-200,300,-300,400,-400};
    unsigned index, axis;
    for (index = 0U; index < sizeof(targets) / sizeof(targets[0]); ++index)
    {
        int16_t expected;
        UpperUiView_t view;
        unsigned presses = 0U;
        setup_pair(); acquire_sim();
        assert(UpperControl_Convert(&sim.snapshot, targets[index], &expected));
        UpperUi_Publish(&controller, &sim, tick, &view);
        while (view.selected_rpm != targets[index])
        {
            assert(++presses <= 10U);
            speed_key(0U);
            UpperUi_Publish(&controller, &sim, tick, &view);
            assert(controller.target_rpm == view.selected_rpm);
        }
        assert(controller.target_rpm == targets[index]);
        run_for(12000U);
        assert(state.authorized && controller.state == SIM_RUNNING && state.target_mm_s == expected);
        for (axis = 0U; axis < 2U; ++axis)
            assert(abs(feedback_axis[axis].actual_speed_rpm - targets[index]) <= 1);
        UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, 0, tick);
        run_for(6500U);
        assert(state.authorized && controller.state == SIM_AUTHORIZED_ZERO && state.target_mm_s == 0);
        for (axis = 0U; axis < 2U; ++axis) assert(feedback_axis[axis].actual_speed_rpm == 0);
        UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, targets[index], tick);
        run_for(1500U);
        UpperControl_Intent(&controller, &sim, UPPER_INTENT_STOP, 0, tick);
        run_for(3000U);
        assert(controller.state == SIM_STOPPED_RELEASED && app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
        assert(fabsf(state.stop_deceleration - APP_CAN_NORMAL_STOP_DECEL_MPS2) < 0.00001f);
        printf("PASS SPEED400 pair target=%d mm/s=%d full-ramp, normal-zero and stop-release\n", targets[index], expected);
    }
    for (index = 0U; index < 4U; ++index)
    {
        setup_pair(); acquire_sim();
        UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, 400, tick);
        run_for(12000U);
        if (index == 0U) UpperControl_Intent(&controller, &sim, UPPER_INTENT_RELEASE, 0, tick);
        else if (index == 1U) pause_targets = true;
        else if (index == 2U) pause_heartbeats = true;
        else { k1 = true; run_for(10U); k1 = false; }
        run_for(7000U);
        assert(!state.authorized && controller.target_mm_s == 0 && app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
        assert(fabsf(state.stop_deceleration - (index == 3U ? APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 : APP_CAN_NORMAL_STOP_DECEL_MPS2)) < 0.00001f);
        printf("PASS SPEED400 pair 400rpm stop case=%u decel=%.2f\n", index, (double)state.stop_deceleration);
    }
}
static void verify_quick_and_random(void)
{
    uint32_t random = 0x20261021UL;
    unsigned index, axis;
    int16_t expected;
    setup_pair(); acquire_sim();
    for (index = 0U; index < 80U; ++index)
    {
        int16_t rpm;
        random = random * 1664525UL + 1013904223UL;
        rpm = (int16_t)((random >> 8U) % 801U) - 400;
        UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, rpm, tick);
        assert(controller.target_rpm == rpm && controller.detail == 0U);
        run_for(110U + random % 140U);
        assert(state.authorized && controller.state != SIM_FAULT);
    }
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, 200, tick); run_for(7000U);
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, -137, tick);
    assert(UpperControl_Convert(&sim.snapshot, -137, &expected));
    run_for(8000U); assert(state.target_mm_s == expected && state.authorized);
    for (axis = 0U; axis < 2U; ++axis) assert(feedback_axis[axis].actual_speed_rpm < 0);
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_QUICK_STOP, 0, tick);
    { uint32_t started = controller.stop_at; uint16_t sequence = controller.stop_sequence;
      for (index = 0U; index < 10U; ++index)
      { run_for(10U); UpperControl_Intent(&controller, &sim, UPPER_INTENT_QUICK_STOP, 0, tick); }
      assert(controller.stop_at == started && controller.stop_sequence == sequence); }
    run_for(4000U);
    printf("QUICK local=%u ack=%u phase=%u detail=%u F4phase=%u decel=%.2f seq=%u\n",controller.state,controller.quick_ack,controller.stop_phase,controller.detail,state.stop_phase,state.stop_deceleration,state.stop_sequence);
    assert(controller.state == SIM_STOPPED_RELEASED && controller.quick_ack && !state.authorized);
    assert(fabsf(state.stop_deceleration - APP_CAN_QUICK_STOP_DECEL_MPS2) < 0.00001f && controller.quick_attempts <= 4U);
    acquire_sim();
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, 200, tick); run_for(2000U);
    speed_key(1U); assert(!controller.quick_stop && controller.target_rpm == 0);
    run_for(3000U); assert(controller.state == SIM_STOPPED_RELEASED);
    assert(fabsf(state.stop_deceleration - APP_CAN_NORMAL_STOP_DECEL_MPS2) < 0.00001f);
    setup_pair();
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_QUICK_STOP, 0, tick); run_for(1800U);
    assert(controller.state == SIM_STOPPED_RELEASED && controller.quick_ack);
    acquire_sim();
    puts("PASS STOP50 random seed=20261021: latest arbitrary +/- target, direct reversal, quick stop ACK/release/regrant, long K2 normal stop, idle global stop");
}

#ifndef F103_PAIR_MAIN
#define F103_PAIR_MAIN main
#endif
int F103_PAIR_MAIN(void)
{
    setup_pair(); sim.snapshot.records[1].valid = 0U; acquire_sim();
    assert(controller.query_attempts >= 1U && controller.command == 4U);
    printf("PASS pair missing config recovered via existing 0x110/0x08 query\n");
    setup_pair(); acquire_sim();
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, 50, tick); run_for(1000U);
    assert(controller.state == SIM_RUNNING && state.target_mm_s >= 180 && state.target_mm_s <= 183);
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_STOP, 0, tick); run_for(2500U);
    printf("STOP local=%u detail=%u event=%lu phase=%u F4=%u owner=%u\n",controller.state,controller.detail,(unsigned long)controller.stop_event,controller.stop_phase,state.stop_phase,app_control_arbiter_owner());
    assert(controller.state == SIM_STOPPED_RELEASED && app_control_arbiter_owner() == APP_CONTROL_OWNER_NONE);
    { unsigned repeat; uint32_t stop_event = controller.stop_event;
      for (repeat=0U;repeat<20U;++repeat) UpperControl_Intent(&controller,&sim,UPPER_INTENT_STOP,0,tick);
      assert(controller.stop_event==stop_event && controller.state==SIM_STOPPED_RELEASED); }
    acquire_sim();
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, -100, tick); run_for(800U);
    assert(state.target_mm_s <= -360 && state.target_mm_s >= -365);
    pause_targets = true; run_for(1600U);
    assert(!state.authorized && controller.target_mm_s == 0);
    setup_pair(); acquire_sim();
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, 100, tick); run_for(800U);
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_STOP, 0, tick);
    { unsigned repeat; uint32_t started=controller.stop_at; uint8_t sequence=controller.request_sequence;
      for(repeat=0U;repeat<20U;++repeat){UpperControl_Intent(&controller,&sim,UPPER_INTENT_STOP,0,tick);run_for(10U);}
      assert(controller.stop_at==started && controller.request_sequence==sequence); }
    run_for(2000U); assert(controller.state==SIM_STOPPED_RELEASED);
    setup_pair(); acquire_sim();
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, 50, tick); run_for(600U);
    pause_heartbeats=true; run_for(1800U); assert(!state.authorized && controller.target_mm_s==0);
    setup_pair(); acquire_sim();
    fresh_ok=false; run_for(600U); assert(controller.motion_token==0U && !state.authorized);
    setup_pair();
    grant_owner(APP_CONTROL_OWNER_HMI); run_for(200U);
    UpperControl_Intent(&controller,&sim,UPPER_INTENT_AUTHORIZE,0,tick);run_for(200U);
    assert(!state.authorized && app_control_arbiter_owner()==APP_CONTROL_OWNER_HMI);
    setup_pair();
    UpperControl_Intent(&controller,&sim,UPPER_INTENT_AUTHORIZE,0,tick);
    run_for(10U); UpperControl_Intent(&controller,&sim,UPPER_INTENT_STOP,0,tick); run_for(1800U);
    printf("CANCEL local=%u detail=%u F4owner=%u auth=%u\n",controller.state,controller.detail,app_control_arbiter_owner(),state.authorized);
    assert(!state.authorized && app_control_arbiter_owner()==APP_CONTROL_OWNER_NONE);
    setup_pair(); acquire_sim();
    UpperControl_Intent(&controller,&sim,UPPER_INTENT_APPLY,50,tick);run_for(600U);
    k1=true;run_for(10U);k1=false;run_for(1800U);
    printf("EXTERNAL_STOP local=%u detail=%u event=%lu phase=%u\n",controller.state,controller.detail,(unsigned long)controller.stop_event,controller.stop_phase);
    assert(controller.state==SIM_STOPPED_RELEASED);acquire_sim();
    puts("PASS real F4/F103 pair: grant, first zero, +/- low speed, stop, repeat stop, regrant, lease, heartbeat, stale feedback, owner mutex");
    verify_extended_speed();
    verify_quick_and_random();
#ifdef F103_PAIR_REAL_BUS
    setup_pair(); acquire_sim();
    UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, 100, tick);
    run_for(600U); BusBridge_FailTarget(); run_for(250U);
     assert(controller.first_error == 110U && !controller.motion_token && !controller.target_rpm);
     run_for(2200U); assert(controller.state == SIM_STOPPED_RELEASED && !state.authorized);
     acquire_sim();
     puts("PASS real bus transient retry and persistent failure -> bounded retry then ERR110");
#endif
    setup_pair();
    {
        unsigned round;
        can_identity_t endurance_identity;
        memset(&endurance_identity, 0, sizeof(endurance_identity));
        endurance_identity.result = CAN_IDENTITY_OK;
        endurance_identity.motion_first = 100U; endurance_identity.motion_last = 1000U;
        app_can_control_init(&endurance_identity);
        for (round = 0U; round < 100U; ++round)
        {
            acquire_sim();
            UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, round & 1U ? -100 : 100, tick);
            run_for(600U); assert(controller.state == SIM_RUNNING && state.authorized);
            UpperControl_Intent(&controller, &sim, UPPER_INTENT_STOP, 0, tick);
            run_for(2200U); assert(controller.state == SIM_STOPPED_RELEASED);
        }
        puts("PASS 100 consecutive grant/zero/speed/stop/regrant cycles without resetting pair");
    }
    return 0;
}
