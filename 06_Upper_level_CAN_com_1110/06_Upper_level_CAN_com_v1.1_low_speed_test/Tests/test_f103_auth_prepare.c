#define main existing_unit_main
#include "test_f103_unit.c"
#undef main
#include <stdlib.h>

static unsigned passed;
static void Check(int condition, const char *name)
{
    if (!condition) { fprintf(stderr, "FAIL %s state=%u detail=%u mask=%lx\n", name, control.state, control.detail, (unsigned long)control.gate.mask); exit(1); }
    ++passed;
}
static void Refresh(uint32_t now)
{
    unsigned axis;
    protocol.snapshot.heartbeat_at = now;
    protocol.snapshot.basic[5].timestamp = now;
    protocol.snapshot.records[2].timestamp = now;
    for (axis = 0U; axis < 2U; ++axis) protocol.snapshot.basic[axis + 6U].timestamp = now;
}
static void Begin(uint32_t now)
{
    UpperControl_Tick(&control, &protocol, now);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, now);
}
static void TestReasons(void)
{
    UpperAuthGate_t gate;
    unsigned reason;
    for (reason = 201U; reason <= 211U; ++reason)
    {
        Setup(1000U);
        switch (reason)
        {
            case 201U: protocol.snapshot.state = UPPER_WAIT_HEARTBEAT; break;
            case 202U: protocol.capability_published = 0U; break;
            case 203U: Put32(protocol.snapshot.records[2].payload + 12U, 4U); break;
            case 204U: protocol.snapshot.records[2].timestamp = 500U; break;
            case 205U: protocol.snapshot.control_source = 3U; break;
            case 206U: protocol.snapshot.gates = 30U; break;
            case 207U: Put32(protocol.snapshot.records[2].payload + 12U, 1540U); break;
            case 208U: protocol.snapshot.basic[6].data[0] &= (uint8_t)~2U; break;
            case 209U: protocol.snapshot.basic[7].data[0] &= (uint8_t)~2U; break;
            case 210U: protocol.snapshot.records[1].valid = 0U; break;
            case 211U: Put16(protocol.snapshot.records[1].payload + 24U, 0U); break;
            default: break;
        }
        UpperControl_CheckAuthorization(&protocol, 1000U, 0U, 0U, &gate);
        Check((gate.mask & (1UL << (reason - 201U))) != 0U, UpperControl_DetailName((uint16_t)reason));
    }
    Setup(1000U); UpperControl_CheckAuthorization(&protocol, 1000U, 0U, 0U, &gate);
    Check(gate.mask == 0U && gate.reason == 0U, "all gates healthy");
}
static void TestPrepare(void)
{
    unsigned age;
    for (age = 499U; age <= 501U; ++age)
    {
        Setup(1000U); protocol.snapshot.records[2].timestamp = 1000U - age;
        Begin(1000U); Check(control.state == SIM_AUTH_PREPARING && requests == 0U, "499/500/501 wait new control");
        UpperControl_Intent(&control, &protocol, UPPER_INTENT_AUTHORIZE, 0, 1001U);
        Check(control.prepare_at == 1000U, "repeated intent fixed deadline");
        Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
        Check(control.state == SIM_AUTH_REQUESTED && requests == 1U && control.request_at == 1010U, "new fresh record starts grant deadline");
    }
    Setup(1000U); Begin(1000U); UpperControl_Intent(&control, &protocol, UPPER_INTENT_STOP, 0, 1001U);
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_IDLE && requests == 0U, "K3 cancels without stop transaction");
    Setup(1000U); Begin(1000U); UpperControl_Intent(&control, &protocol, UPPER_INTENT_RELEASE, 0, 1001U);
    Check(control.state == SIM_IDLE && requests == 0U, "K1 release cancels preparation");
    Setup(1000U); protocol.capability_published = 0U; Begin(1000U);
    Refresh(6000U); UpperControl_Tick(&control, &protocol, 6000U);
    Check(control.state == SIM_IDLE && control.detail == 212U && control.prepare_last_reason == 202U, "fixed five second deadline");
    protocol.capability_published = 1U; Refresh(6010U); UpperControl_Tick(&control, &protocol, 6010U);
    Check(requests == 0U, "timeout never auto reauthorizes");
    Setup(1000U); Begin(1000U); protocol.snapshot.lower_boot_id++;
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_IDLE && control.detail == 201U, "boot change cancels");
    Setup(1000U); Begin(1000U); protocol.snapshot.link_token++;
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_IDLE && control.detail == 201U, "token change cancels");
    Setup(1000U); Begin(1000U); protocol.snapshot.control_source = 1U;
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_IDLE && control.detail == 205U, "new owner cancels");
    Setup(1000U); Begin(1000U); Put32(protocol.snapshot.records[2].payload + 12U, 1540U);
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_IDLE && control.detail == 207U, "safety lock cancels");
    Setup(1000U); Begin(1000U); Put16(protocol.snapshot.basic[6].data + 2U, 1U);
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_IDLE && control.detail == 208U, "nonzero left cancels");
    Setup(1000U); Begin(1000U); Put16(protocol.snapshot.basic[7].data + 4U, 1U);
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_IDLE && control.detail == 209U, "right fault cancels");
    Setup(1000U); Begin(1000U); protocol.snapshot.basic[6].data[0] &= (uint8_t)~2U;
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_IDLE && control.detail == 208U, "fresh disabled feedback cancels");
    Setup(1000U); Begin(1000U); protocol.snapshot.basic[7].data[0] |= 32U;
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_IDLE && control.detail == 209U, "fresh stop latch cancels");
    Setup(1000U); protocol.snapshot.basic[6].timestamp = 700U; Begin(1000U);
    Check(control.state == SIM_AUTH_PREPARING, "stale axis waits not releases gate");
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_AUTH_REQUESTED, "fresh axis recovers within deadline");
    Setup(1000U); protocol.snapshot.basic[7].timestamp = 699U; Begin(1000U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, 100, 1001U);
    Check(nonzero == 0U && control.target_mm_s == 0, "no target while preparing");
    { UpperUiView_t view; char text[40];
      UpperUi_Publish(&control, &protocol, 1001U, &view); view.page = 2U;
      UpperUi_Format(&view, 1U, text, sizeof(text)); Check(strcmp(text, "AUTH_PREP") == 0, "OLED preparation state");
      view.detail = 209U; UpperUi_Format(&view, 6U, text, sizeof(text));
      Check(strstr(text, "RIGHT AXIS") != NULL, "OLED concrete reason"); }
}
static void TestConfig(void)
{
    uint8_t response[8] = {0};
    unsigned attempt;
    Setup(1000U); protocol.snapshot.records[1].valid = 0U; Begin(1000U);
    Check(control.state == SIM_AUTH_PREPARING && sent_id == 0x110U && sent[1] == 8U && Upper_ReadU16(sent + 2U) == 16U, "missing config queries existing command");
    response[0] = control.request_sequence; response[1] = 8U; Put16(response + 4U, 100U);
    UpperControl_Receive(&control, &protocol, 0x120U, response, 1001U, 1001U);
    response[2] = 3U; UpperControl_Receive(&control, &protocol, 0x121U, response, 1002U, 1002U);
    protocol.snapshot.records[1].valid = 1U; Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_AUTH_REQUESTED && control.request_sequence == (uint8_t)(response[0] + 1U), "query complete allocates new authorization sequence");
    UpperControl_Receive(&control, &protocol, 0x121U, response, 1011U, 1011U);
    Check(!control.accepted && !control.completed && !control.motion_token, "late query cannot grant motion");
    Setup(1000U); Put32(protocol.snapshot.records[1].payload, 8U); Begin(1000U);
    Check(control.command == 8U && (control.gate.mask & (1UL << 9U)), "revision mismatch queries");
    Setup(1000U); Put16(protocol.snapshot.records[1].payload + 24U, 0U); Begin(1000U);
    Check(control.state == SIM_IDLE && control.detail == 211U && requests == 0U, "invalid config never queries or grants");
    Setup(1000U); protocol.snapshot.records[1].valid = 0U; Begin(1000U);
    for (attempt = 1U; attempt <= 5U; ++attempt)
    { Refresh(1000U + attempt * 1000U); UpperControl_Tick(&control, &protocol, 1000U + attempt * 1000U); }
    Check(control.query_attempts == 3U && requests == 3U && control.detail == 212U, "config retries bounded by count and deadline");
    Setup(1000U); protocol.snapshot.records[1].valid = 0U; control.request_sequence = 255U; Begin(1000U);
    Check(control.request_sequence == 0U, "shared sequence wraps");
    response[0] = 0U; response[1] = 8U; response[2] = 3U; response[3] = 0U; Put16(response + 4U, 99U);
    UpperControl_Receive(&control, &protocol, 0x121U, response, 1001U, 1001U);
    Check(!control.completed, "wrong query token rejected");
    Put16(response + 4U, 100U); response[0] = 1U;
    UpperControl_Receive(&control, &protocol, 0x121U, response, 1001U, 1001U);
    Check(!control.completed, "wrong query sequence rejected");
    response[0] = 0U; UpperControl_Receive(&control, &protocol, 0x121U, response, 999U, 1001U);
    Check(!control.completed, "pre-request query response rejected");
    response[2] = 4U; response[3] = 8U; Put16(response + 6U, 13U);
    UpperControl_Receive(&control, &protocol, 0x121U, response, 1001U, 1001U);
    Check(control.query_result == 8U && control.query_detail == 13U && !control.request_active, "query failure preserves F4 result");
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.request_sequence == 1U && control.query_attempts == 2U, "retry allocates new sequence after wrap");
}
static void TestCapability(void)
{
    uint8_t result[8] = {0};
    unsigned round;
    Setup(1000U); protocol.capability_published = 0U;
    for (round = 1U; round <= 3U; ++round)
    {
        protocol.capability_active = protocol.capability_waiting = 1U;
        protocol.capability_attempts = (uint8_t)round; protocol.capability_step = 9U;
        protocol.capability_sequence = (uint16_t)round; Put16(result, (uint16_t)round);
        Upper_TxResult(&protocol, 0x312U, result, (uint8_t)(round == 1U), 1000U + round);
    }
    Check(protocol.capability_published == 1U, "later capability failures retain first success");
    Upper_ResetSession(&protocol, 1010U);
    Check(!protocol.capability_published && !protocol.capability_confirmed, "session resets capability confirmations");
    Setup(1000U); protocol.capability_published = 0U; protocol.capability_attempts = 3U;
    Upper_Tick(&protocol, 1000U, 1U); Check(!protocol.capability_active, "exhausted attempts do not loop");
    Begin(1000U); Upper_Tick(&protocol, 1001U, 1U);
    Check(protocol.capability_active && protocol.capability_attempts == 1U, "new user intent restarts bounded budget");
    Setup(1000U); Put32(protocol.snapshot.records[2].payload + 12U, 4U); Begin(1000U);
    Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_AUTH_PREPARING && requests == 0U && control.detail == 203U, "transport complete is not business acceptance");
    Setup(1000U); protocol.capability_active = protocol.capability_waiting = 1U;
    protocol.capability_attempts = 3U; protocol.capability_sequence = 8U; protocol.capability_step = 9U;
    Put16(result, 8U); Upper_TxResult(&protocol, 0x312U, result, 0U, 1001U);
    Begin(1001U); Refresh(1010U); UpperControl_Tick(&control, &protocol, 1010U);
    Check(control.state == SIM_AUTH_REQUESTED, "final redundant failure does not lock grant");
    Setup(1000U); protocol.capability_confirmed = 1U; protocol.snapshot.records[2].timestamp = 400U;
    Upper_Tick(&protocol, 1000U, 1U);
    Check(!protocol.capability_confirmed, "old strategy snapshot not confirmation");
}
int main(void)
{
    TestReasons(); TestPrepare(); TestConfig(); TestCapability();
    printf("PASS AUTHFIX %u assertions: diagnostics, freshness, prepare, query, cancellation and capabilities\n", passed);
    return 0;
}
