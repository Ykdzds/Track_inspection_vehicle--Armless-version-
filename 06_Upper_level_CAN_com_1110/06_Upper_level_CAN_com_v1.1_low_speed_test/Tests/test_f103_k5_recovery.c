#define main previous_unit_main
#include "test_f103_unit.c"
#undef main

static void Evidence(uint32_t now, uint8_t phase)
{
    unsigned axis;
    UpperRecord_t *record = &protocol.snapshot.records[2];
    UpperRecord_t *stop = &protocol.snapshot.records[3];
    protocol.snapshot.heartbeat_at = protocol.snapshot.basic[5].timestamp = now;
    protocol.snapshot.control_source = 0U; protocol.snapshot.lifecycle = 1U;
    record->timestamp = stop->timestamp = now;
    Put32(record->payload, 2U);
    record->payload[6] = 0U; record->payload[7] = 1U;
    record->payload[34] = phase; record->payload[35] = 0U;
    Put16(record->payload + 4U, 0U); Put32(record->payload + 12U, 1028U);
    Put32(stop->payload, 1U); stop->payload[4] = 1U;
    stop->payload[5] = phase; stop->payload[6] = 3U;
    for (axis = 0U; axis < 2U; ++axis) protocol.snapshot.basic[axis + 6U].timestamp = now;
}

static void Active(void)
{
    Setup(1000U);
    control.state = SIM_RUNNING; control.motion_token = 23U;
    control.last_motion_token = 23U; control.target_rpm = 100;
    control.axes = 3U; control.link_token = 100U;
    control.lower_boot = 42U; control.upper_boot = 84U;
    control.stop_baseline = 99U;
    control.motion_generation = 1U;
    Record(3U, 0x13U, 40U, 1000U);
}
static void Key(uint8_t mask, uint32_t *now, uint32_t duration)
{
    uint32_t end = *now + duration;
    for (; *now < end; *now += 10U) UpperUi_ScanKeys(*now, mask, &control, &protocol);
}
static void LongK1(uint32_t *now)
{
    Key(1U, now, UPPER_KEY_LONG_MS + UPPER_KEY_DEBOUNCE_MS + 30U);
    Key(0U, now, UPPER_KEY_DEBOUNCE_MS + 20U);
}
int main(void)
{
    unsigned index;
    uint32_t now;
    Active();
    Evidence(1010U, 1U);
    UpperControl_Tick(&control, &protocol, 1010U);
    assert(control.state == SIM_CROSS_SOURCE_STOP);
    Evidence(1020U, 4U); UpperControl_Tick(&control, &protocol, 1020U);
    Evidence(1030U, 4U); UpperControl_Tick(&control, &protocol, 1030U);
    Evidence(1040U, 4U); UpperControl_Tick(&control, &protocol, 1040U);
    assert(control.state == SIM_STOPPED_RELEASED && !control.detail);
    assert(!control.motion_token && !control.target_rpm && control.command == 8U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 1040U);
    Evidence(1050U, 4U); UpperControl_Tick(&control, &protocol, 1050U);
    assert(control.state == SIM_AUTH_REQUESTED);
    puts("PASS K5 recovery with unrelated previous stop counter");
    Active(); Evidence(1010U, 4U); UpperControl_Tick(&control, &protocol, 1010U);
    Evidence(1020U, 4U); UpperControl_Tick(&control, &protocol, 1020U);
    for (index = 0U; index < 10U; ++index) UpperControl_Tick(&control, &protocol, 1020U);
    assert(control.cross_zero_observations == 1U && control.state == SIM_CROSS_SOURCE_STOP);
    Evidence(1030U, 4U); protocol.snapshot.basic[7].timestamp = 1020U;
    UpperControl_Tick(&control, &protocol, 1030U);
    assert(control.cross_zero_observations == 1U);
    Evidence(1040U, 4U); Put32(protocol.snapshot.records[2].payload + 12U, 1540U);
    UpperControl_Tick(&control, &protocol, 1040U); assert(!control.cross_zero_observations && control.recovery_reason == 4U);
    Evidence(1050U, 4U); UpperControl_Tick(&control, &protocol, 1050U);
    assert(control.state == SIM_CROSS_SOURCE_STOP && control.cross_zero_observations == 1U);
    Evidence(1060U, 4U); UpperControl_Tick(&control, &protocol, 1060U);
    assert(control.state == SIM_STOPPED_RELEASED);
    puts("PASS first-completed snapshot; distinct per-axis receptions; safety lock resets confirmations");
    for (index = 0U; index < 6U; ++index)
    {
        Active(); Evidence(1010U, 1U); UpperControl_Tick(&control, &protocol, 1010U);
        Evidence(1020U, 4U);
        if (index == 0U) protocol.snapshot.records[3].timestamp = 1000U;
        if (index == 1U) Put32(protocol.snapshot.records[2].payload, 1U);
        if (index == 2U) protocol.snapshot.control_source = 3U;
        if (index == 3U) Put16(protocol.snapshot.basic[6].data + 2U, 1U);
        if (index == 4U) protocol.snapshot.basic[7].data[0] &= (uint8_t)~2U;
        if (index == 5U) protocol.snapshot.records[3].payload[5] = 3U;
        UpperControl_Tick(&control, &protocol, 1020U); UpperControl_Tick(&control, &protocol, 1030U);
        assert(control.state == SIM_CROSS_SOURCE_STOP && !control.cross_zero_observations);
    }
    Active(); Evidence(1010U, 1U); UpperControl_Tick(&control, &protocol, 1010U);
    Evidence(1020U, 5U); UpperControl_Tick(&control, &protocol, 1020U);
    assert(control.state == SIM_FAULT && control.recovery_hard);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 1030U);
    assert(control.state == SIM_FAULT);
    Active(); Evidence(1010U, 1U); UpperControl_Tick(&control, &protocol, 1010U);
    protocol.snapshot.lower_boot_id++;
    UpperControl_Tick(&control, &protocol, 1020U); assert(control.detail == 111U);
    puts("PASS stale/history/owner/nonzero/disabled/conflicting phases/hard failure/session guards");
    Active(); Evidence(1010U, 1U); UpperControl_Tick(&control, &protocol, 1010U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 1020U);
    assert(control.state == SIM_CROSS_SOURCE_STOP && control.cross_stop_at == 1010U);
    Evidence(31010U, 1U); UpperControl_Tick(&control, &protocol, 31010U);
    assert(control.state == SIM_FAULT && control.detail == 115U && !control.recovery_hard);
    now = 31020U;
    Key(1U, &now, 80U); Key(0U, &now, 80U); Key(1U, &now, 80U); Key(0U, &now, 80U);
    LongK1(&now); assert(control.state == SIM_RECOVERY_VERIFY);
    Evidence(now, 4U); UpperControl_Tick(&control, &protocol, now);
    Evidence(now + 10U, 4U); UpperControl_Tick(&control, &protocol, now + 10U);
    assert(control.state == SIM_AUTH_PREPARING);
    Evidence(now + 20U, 4U); UpperControl_Tick(&control, &protocol, now + 20U);
    assert(control.state == SIM_AUTH_REQUESTED && control.command == 4U);
    puts("PASS observation timeout; real debounced K1 -> bounded read-only verification -> active grant");
    Active(); Evidence(1010U, 1U); UpperControl_Tick(&control, &protocol, 1010U);
    Evidence(31010U, 1U); UpperControl_Tick(&control, &protocol, 31010U);
    protocol.snapshot.records[2].timestamp = protocol.snapshot.records[3].timestamp = 30000U;
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 31020U);
    assert(control.state == SIM_RECOVERY_VERIFY);
    Evidence(36020U, 1U); UpperControl_Tick(&control, &protocol, 36020U);
    assert(control.state == SIM_FAULT && !control.request_active);
    Evidence(36030U, 4U); UpperControl_Tick(&control, &protocol, 36030U);
    Evidence(36040U, 4U); UpperControl_Tick(&control, &protocol, 36040U);
    assert(control.state == SIM_STOPPED_RELEASED && !control.request_active && !control.motion_token);
    puts("PASS verification timeout drops active authorization intent; late success never grants");
    for (index = 0U; index < 3U; ++index)
    {
        Active(); Evidence(1010U, 4U);
        Put32(protocol.snapshot.records[3].payload, index == 0U ? 99U : index == 1U ? 0U : 0xFFFFFFFFUL);
        UpperControl_Tick(&control, &protocol, 1010U);
        Evidence(1020U, 4U); Put32(protocol.snapshot.records[3].payload, index == 0U ? 99U : index == 1U ? 0U : 0xFFFFFFFFUL);
        UpperControl_Tick(&control, &protocol, 1020U);
        Evidence(1030U, 4U); Put32(protocol.snapshot.records[3].payload, index == 0U ? 99U : index == 1U ? 0U : 0xFFFFFFFFUL);
        UpperControl_Tick(&control, &protocol, 1030U);
        if (index == 1U)
            assert(control.state != SIM_STOPPED_RELEASED && !control.motion_token && control.recovery_required);
        else
            assert(control.state == SIM_STOPPED_RELEASED);
    }
    puts("PASS missing stop event never fabricates release success");
    Active(); Evidence(1010U, 1U); UpperControl_Tick(&control, &protocol, 1010U);
    Evidence(1020U, 4U); protocol.snapshot.records[3].payload[5] = 1U;
    UpperControl_Tick(&control, &protocol, 1020U); assert(!control.cross_zero_observations);
    Evidence(1030U, 4U); protocol.snapshot.records[2].payload[34] = 1U;
    UpperControl_Tick(&control, &protocol, 1030U); assert(!control.cross_zero_observations);
    Evidence(1040U, 4U); UpperControl_Tick(&control, &protocol, 1040U);
    Evidence(1050U, 4U); UpperControl_Tick(&control, &protocol, 1050U);
    assert(control.state == SIM_STOPPED_RELEASED);
    Active(); Evidence(1010U, 1U); UpperControl_Tick(&control, &protocol, 1010U);
    Evidence(31010U, 1U); UpperControl_Tick(&control, &protocol, 31010U);
    protocol.snapshot.records[2].timestamp = protocol.snapshot.records[3].timestamp = 30000U;
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 31020U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_RELEASE, 0, 31030U);
    Evidence(31040U, 4U); UpperControl_Tick(&control, &protocol, 31040U);
    Evidence(31050U, 4U); UpperControl_Tick(&control, &protocol, 31050U);
    assert(control.state == SIM_STOPPED_RELEASED && !control.request_active);
    puts("PASS event equality/rollback/wrap, 0x11/0x13 ordering, cancellation cannot leave a grant intent");
    Active(); Evidence(1010U, 1U); UpperControl_Tick(&control, &protocol, 1010U);
    Evidence(31010U, 1U); UpperControl_Tick(&control, &protocol, 31010U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 31020U);
    assert(control.state == SIM_FAULT && control.recovery_reason == 1U);
    Evidence(31030U, 4U); UpperControl_Tick(&control, &protocol, 31030U);
    Evidence(31040U, 4U); UpperControl_Tick(&control, &protocol, 31040U);
    assert(control.state == SIM_STOPPED_RELEASED && !control.request_active);
    puts("PASS K1 during confirmed ongoing stop never arms recovery/grant, even after observation timeout");
    Active();
    protocol.snapshot.records[2].timestamp = 400U;
    UpperControl_Tick(&control, &protocol, 1010U);
    assert(control.state == SIM_STOP_REQUESTED && control.detail == 116U);
    assert(!control.motion_token && !control.target_rpm && !control.target_mm_s);
    assert(control.command == 5U && control.request_parameter == 0);
    assert(control.recovery_reason == 7U && control.recovery_required);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 1011U);
    assert(control.state == SIM_STOP_REQUESTED && !control.motion_token);
    Evidence(1020U, 4U); UpperControl_Tick(&control, &protocol, 1020U);
    assert(control.state == SIM_STOPPED_RELEASED && !control.request_active);
    assert(!control.motion_token && !control.recovery_required);
    Active();
    protocol.snapshot.basic[6].timestamp = 400U;
    protocol.snapshot.records[2].payload[6] = 4U;
    Put16(protocol.snapshot.records[2].payload + 4U, 23U);
    Put32(protocol.snapshot.records[2].payload + 12U, 1036U);
    UpperControl_Tick(&control, &protocol, 1010U);
    assert(control.state == SIM_STOP_REQUESTED && control.detail == 116U);
    assert(control.recovery_reason == 8U && !control.motion_token);
    Evidence(31010U, 1U); UpperControl_Tick(&control, &protocol, 31010U);
    assert(control.state == SIM_FAULT && control.detail == 114U);
    assert(!control.motion_token && !control.target_rpm);
    puts("PASS transient feedback revokes motion and requests bounded stop; only confirmed release permits a fresh K1 grant");
    Active();
    protocol.snapshot.records[2].payload[6] = 4U;
    Put16(protocol.snapshot.records[2].payload + 4U, 23U);
    Put32(protocol.snapshot.records[2].payload + 12U, 1036U);
    Put16(protocol.snapshot.basic[6].data + 4U, 1U);
    UpperControl_Tick(&control, &protocol, 1010U);
    assert(control.state == SIM_FAULT && control.detail == 116U && control.recovery_hard);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 1011U);
    assert(control.state == SIM_FAULT && !control.motion_token);
    return 0;
}

