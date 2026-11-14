#define main existing_unit_main
#include "test_f103_unit.c"
#undef main
static unsigned assertions;
#define CHECK(expression) do { assert(expression); assertions++; } while (0)
static unsigned failed_submissions;
static uint8_t RejectSubmission(void *context, uint16_t id, const uint8_t data[8])
{
    (void)context; (void)id; (void)data; ++failed_submissions; return 0U;
}
static void RefreshEvidence(uint32_t now)
{
    protocol.snapshot.heartbeat_at = now;
    protocol.snapshot.basic[5].timestamp = now;
    protocol.snapshot.records[2].timestamp = now;
    protocol.snapshot.basic[6].timestamp = protocol.snapshot.basic[7].timestamp = now;
}
static void TestBoundedStopAndCommand(void)
{
    unsigned index;
    uint16_t sequence;
    uint32_t started;
    UpperRecord_t *stop;
    Setup(1000U); UpperControl_Tick(&control, &protocol, 1000U);
    control.send = RejectSubmission; failed_submissions = 0U;
    stop = Record(3U, 0x13U, 40U, 1000U);
    Put32(stop->payload, 99U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_QUICK_STOP, 0, 1000U);
    sequence = control.stop_sequence; started = control.stop_at;
    for (index = 0U; index < 1000U; ++index)
    {
        uint32_t now = 1000U + index;
        RefreshEvidence(now); UpperControl_Tick(&control, &protocol, now);
        UpperControl_Intent(&control, &protocol, UPPER_INTENT_QUICK_STOP, 0, now);
        CHECK(failed_submissions == (index < 300U ? index / 100U + 1U : 4U));
    }
    CHECK(control.quick_attempts == 4U && !control.quick_ack && control.state == SIM_STOPPING);
    CHECK(control.stop_sequence == sequence && control.stop_at == started && !control.target_rpm);
    stop = Record(3U, 0x13U, 40U, 2000U);
    Put32(stop->payload, 5U); stop->payload[5] = 4U; stop->payload[6] = 3U;
    RefreshEvidence(2000U); protocol.snapshot.records[2].payload[34] = 4U;
    protocol.snapshot.records[2].payload[35] = 0U;
    Put16(protocol.snapshot.records[2].payload + 32U, (uint16_t)(sequence - 1U));
    UpperControl_Tick(&control, &protocol, 2000U);
    CHECK(!control.quick_ack && control.state == SIM_STOPPING);
    Put16(protocol.snapshot.records[2].payload + 32U, sequence);
    UpperControl_Tick(&control, &protocol, 2000U);
    CHECK(control.quick_ack && control.cross_zero_observations == 1U);
    RefreshEvidence(2010U); stop->timestamp = 2010U;
    Put16(protocol.snapshot.records[2].payload + 32U, (uint16_t)(sequence + 1U));
    UpperControl_Tick(&control, &protocol, 2010U);
    CHECK(control.state == SIM_STOPPING && control.cross_zero_observations == 0U);
    Put16(protocol.snapshot.records[2].payload + 32U, sequence);
    UpperControl_Tick(&control, &protocol, 2010U);
    CHECK(control.cross_zero_observations == 1U);
    RefreshEvidence(2020U); stop->timestamp = 2020U;
    UpperControl_Tick(&control, &protocol, 2020U);
    CHECK(control.state == SIM_STOPPED_RELEASED && failed_submissions == 4U);
    Setup(1000U); UpperControl_Tick(&control, &protocol, 1000U);
    control.send = RejectSubmission; failed_submissions = 0U;
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 1000U);
    RefreshEvidence(1001U); UpperControl_Tick(&control, &protocol, 1001U);
    CHECK(control.state == SIM_AUTH_REQUESTED);
    sequence = control.request_sequence;
    for (index = 0U; index < 1200U; ++index)
    {
        uint32_t now = 1001U + index;
        RefreshEvidence(now); UpperControl_Tick(&control, &protocol, now);
        CHECK(failed_submissions == (index < 900U ? index / 300U + 1U : 4U));
        CHECK(control.request_sequence == sequence);
    }
    CHECK(control.attempts == 4U && control.request_at == 1001U);
    puts("PASS STOP50 bounded quick x4/100ms, command x4/300ms, queue failure throttling, unchanged sequence/deadline, fresh correlated release");
}
static void Active(void)
{
    Setup(1000U);
    control.state = SIM_RUNNING; control.motion_token = 23U; control.target_rpm = 100; control.target_mm_s = 363;
    control.link_token = 100U; control.lower_boot = 42U; control.upper_boot = 84U;
    Put16(protocol.snapshot.records[2].payload + 24U, 5811U);
    protocol.snapshot.records[2].payload[6] = 4U;
    Put16(protocol.snapshot.records[2].payload + 4U, 23U);
    Put32(protocol.snapshot.records[2].payload + 12U, 1036U);
}
static void FormatCheck(const char *expected)
{
    UpperUiView_t view;
    char text[64];
    UpperUi_Publish(&control, &protocol, 1000U, &view); view.page = 2U;
    UpperUi_Format(&view, 6U, text, sizeof(text));
    CHECK(strstr(text, expected) != NULL);
}
int main(void)
{
    uint8_t data[8] = {1U, 0U, 0U, 0U, 23U, 0U, 44U, 1U};
    unsigned outcome;
    Active();
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, -100, 1000U);
    CHECK(control.operation_detail == 0U && control.target_rpm == -100 && control.state == SIM_RUNNING);
    CHECK(control.operation_count == 0U);
    UpperControl_TxOutcome(&control, 0x111U, data, UPPER_TX_OK, 1001U);
    CHECK(control.operation_detail == 0U && control.detail == 0U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, 0, 1000U);
    CHECK(!control.operation_detail && !control.target_rpm);
    Put16(protocol.snapshot.basic[6].data + 2U, 50U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, -100, 1000U);
    CHECK(control.operation_detail == 0U && control.target_rpm == -100);
    protocol.snapshot.basic[6].timestamp = 700U;
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, -100, 1000U);
    CHECK(control.operation_detail == 104U && control.target_rpm == -100); FormatCheck("FB INVALID");
    protocol.snapshot.basic[6].timestamp = 1000U;
    Put16(protocol.snapshot.basic[6].data + 2U, 0U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, -100, 1000U);
    CHECK(control.target_rpm == -100 && !control.operation_detail);
    Active();
    UpperControl_TxOutcome(&control, 0x111U, data, UPPER_TX_CANCELLED, 1001U);
    UpperControl_TxOutcome(&control, 0x111U, data, UPPER_TX_REPLACED, 1001U);
    CHECK(control.state == SIM_RUNNING && control.motion_token == 23U);
    for (outcome = UPPER_TX_QUEUE_EXPIRED; outcome <= UPPER_TX_ABORT_TIMEOUT; ++outcome)
    {
        Active(); UpperControl_TxOutcome(&control, 0x111U, data, (UpperTxOutcome_t)outcome, 1001U);
        CHECK(control.state == SIM_FAULT && control.detail == 110U && control.recovery_required);
        CHECK(!control.motion_token && !control.target_rpm && control.tx_failure == outcome);
        UpperControl_Intent(&control, &protocol, UPPER_INTENT_RELEASE, 0, 1001U);
        CHECK(control.state == SIM_FAULT && !control.request_active);
        UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 1001U);
        CHECK(control.state == SIM_FAULT && control.detail == 110U);
        UpperControl_TxOutcome(&control, 0x111U, data, UPPER_TX_OK, 1002U);
        CHECK(control.state == SIM_FAULT && !control.motion_token);
        FormatCheck("K3 STOP");
        UpperControl_Intent(&control, &protocol, UPPER_INTENT_STOP, 0, 1001U);
        CHECK(control.state == SIM_STOP_REQUESTED && control.command == 4U);
        CHECK(!control.operation_detail && !control.target_rpm);
        UpperControl_Intent(&control, &protocol, UPPER_INTENT_STOP, 0, 1002U);
        CHECK(control.stop_at == 1001U);
        control.accepted = control.completed = 1U;
        protocol.snapshot.records[2].timestamp = 1002U;
        protocol.snapshot.records[2].payload[34] = 0U;
        protocol.snapshot.records[2].payload[6] = 0U;
        Put16(protocol.snapshot.records[2].payload + 4U, 0U);
        Put32(protocol.snapshot.records[2].payload + 12U, 1028U);
        UpperControl_Tick(&control, &protocol, 1002U);
        CHECK(control.state == SIM_STOPPED_RELEASED && !control.recovery_required);
    }
    Active(); data[4] = 22U;
    UpperControl_TxOutcome(&control, 0x111U, data, UPPER_TX_TIMEOUT, 1001U);
    CHECK(control.state == SIM_RUNNING && control.motion_token == 23U);
    Active(); data[4] = 23U;
    UpperControl_TxOutcome(&control, 0x111U, data, UPPER_TX_TIMEOUT, 1001U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_STOP, 0, 1001U);
    control.accepted = control.completed = 1U;
    protocol.snapshot.records[2].timestamp = 1002U;
    protocol.snapshot.records[2].payload[34] = 0U;
    protocol.snapshot.records[2].payload[6] = 0U;
    Put16(protocol.snapshot.records[2].payload + 4U, 0U);
    Put32(protocol.snapshot.records[2].payload + 12U, 1028U);
    protocol.snapshot.basic[6].timestamp = 700U;
    UpperControl_Tick(&control, &protocol, 1002U);
    CHECK(control.state == SIM_STOPPING && control.recovery_required);
    protocol.snapshot.basic[6].timestamp = 1002U;
    Put32(protocol.snapshot.records[2].payload + 12U, 512U);
    UpperControl_Tick(&control, &protocol, 1002U);
    CHECK(control.state == SIM_FAULT && control.recovery_required && !control.motion_token);
    Active(); control.axes = 3U; control.motion_generation = 10U; protocol.snapshot.upper_boot_id = 84U;
    UpperControl_TxOutcome(&control, 0x111U, data, UPPER_TX_TIMEOUT, 1001U);
    CHECK(control.first_error == 110U && control.first_error_at == 1001U);
    RefreshEvidence(1002U); UpperControl_Tick(&control, &protocol, 1002U);
    CHECK(control.state == SIM_CROSS_SOURCE_STOP && !control.motion_token && !control.target_rpm);
    /* No extra STOP: release only against a newer, correlated successful event. */
    {
        UpperRecord_t *r = &protocol.snapshot.records[2];
        UpperRecord_t *st = Record(3U, 0x13U, 40U, 1010U);
        Put32(r->payload, 11U); Put16(r->payload + 4U, 0U);
        r->payload[6] = 0U; r->payload[7] = 1U; r->payload[34] = 4U; r->payload[35] = 0U;
        Put32(r->payload + 12U, 1028U);
        Put32(st->payload, 7U); st->payload[5] = 4U; st->payload[6] = 3U;
        protocol.snapshot.control_source = 0U; protocol.snapshot.lifecycle = 1U;
        RefreshEvidence(1010U); UpperControl_Tick(&control, &protocol, 1010U);
        CHECK(control.state == SIM_CROSS_SOURCE_STOP);
        RefreshEvidence(1020U); st->timestamp = 1020U; UpperControl_Tick(&control, &protocol, 1020U);
        CHECK(control.state == SIM_STOPPED_RELEASED && !control.recovery_required);
        CHECK(control.first_error == 110U && !control.target_rpm && !control.motion_token);
        UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 1020U);
        CHECK(control.state == SIM_AUTH_PREPARING);
    }
    Active();
    UpperControl_TxOutcome(&control, 0x311U, data, UPPER_TX_TIMEOUT, 1001U);
    CHECK(control.state == SIM_RUNNING && control.motion_token == 23U && control.target_rpm == 100);
    Setup(1000U); UpperControl_Tick(&control, &protocol, 1000U);
    for (unsigned at = 1000U; at <= 1020U; at += 10U) UpperUi_ScanKeysOnly(at, 4U);
    UpperUi_ConsumeEventsAt(&control, &protocol, 6000U);
    CHECK(control.state == SIM_STOP_REQUESTED && control.stop_at == 6000U);
    TestBoundedStopAndCommand();
    printf("PASS TXFIX %u assertions: bottom-controlled reversal, invalid feedback, real failures, old token, K1 locked and K3 recovery\n", assertions);
    return 0;
}
