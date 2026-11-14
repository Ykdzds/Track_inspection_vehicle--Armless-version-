#define main existing_unit_main
#include "test_f103_unit.c"
#undef main

static unsigned checks;
#define VERIFY(expression) do { ++checks; assert(expression); } while (0)
static uint32_t key_time;

static void Refresh(void)
{
    unsigned axis;
    protocol.snapshot.heartbeat_at = key_time;
    protocol.snapshot.basic[5].timestamp = key_time;
    protocol.snapshot.records[2].timestamp = key_time;
    for (axis = 0U; axis < 2U; ++axis) protocol.snapshot.basic[6U + axis].timestamp = key_time;
}

static void Key(uint8_t mask, uint8_t hold)
{
    UpperUi_ScanKeys(key_time, mask, &control, &protocol);
    key_time += UPPER_KEY_DEBOUNCE_MS + 1U;
    Refresh();
    UpperUi_ScanKeys(key_time, mask, &control, &protocol);
    key_time += hold ? UPPER_KEY_LONG_MS : 30U;
    Refresh();
    UpperUi_ScanKeys(key_time, mask, &control, &protocol);
    UpperUi_ScanKeys(key_time, 0U, &control, &protocol);
    key_time += UPPER_KEY_DEBOUNCE_MS + 1U;
    Refresh();
    UpperUi_ScanKeys(key_time, 0U, &control, &protocol);
    ++key_time;
}

static void ControlPage(UpperControlState_t state)
{
    Setup(1000U);
    key_time = 1000U;
    control.state = state;
    Put16(protocol.snapshot.records[2].payload + 24U, 5811U);
    Key(1U, 0U);
    Key(1U, 0U);
}

static void VerifyConversion(void)
{
    static const int16_t values[] = {0,100,200,300,400};
    static const int16_t converted[] = {0,363,726,1090,1453};
    unsigned axes, index;
    int16_t speed;
    for (axes = 1U; axes <= 3U; ++axes)
    {
        Setup(1000U);
        protocol.snapshot.axes = (uint8_t)axes;
        for (index = 0U; index < sizeof(values) / sizeof(values[0]); ++index)
        {
            VERIFY(UpperControl_Convert(&protocol.snapshot, values[index], &speed) && speed == converted[index]);
            VERIFY(UpperControl_Convert(&protocol.snapshot, (int16_t)-values[index], &speed) && speed == -converted[index]);
        }
        VERIFY(!UpperControl_Convert(&protocol.snapshot, 401, &speed));
        VERIFY(!UpperControl_Convert(&protocol.snapshot, -401, &speed));
        Put16(protocol.snapshot.records[1].payload + 34U, 1000U);
        VERIFY(!UpperControl_Convert(&protocol.snapshot, 300, &speed));
        VERIFY(!UpperControl_Convert(&protocol.snapshot, -400, &speed));
        VERIFY(UpperControl_Convert(&protocol.snapshot, 200, &speed));
    }
    Setup(1000U);
    Put32(protocol.snapshot.records[1].payload + 12U, 92500U);
    VERIFY(!UpperControl_Convert(&protocol.snapshot, 400, &speed));
    VERIFY(!UpperControl_Convert(&protocol.snapshot, -400, &speed));
    VERIFY(UpperControl_Convert(&protocol.snapshot, 200, &speed) && speed == 726);
    protocol.snapshot.axes = 2U;
    VERIFY(UpperControl_Convert(&protocol.snapshot, 400, &speed) && speed == 726);
    Put16(protocol.snapshot.records[1].payload + 28U, 0U);
    VERIFY(!UpperControl_Convert(&protocol.snapshot, 100, &speed));
    Setup(1000U);
    Put16(protocol.snapshot.records[1].payload + 78U, 512U);
    VERIFY(!UpperControl_Convert(&protocol.snapshot, -100, &speed));
    VERIFY(UpperControl_Convert(&protocol.snapshot, 400, &speed));
    puts("PASS SPEED400 conversion: +/-100/200/300/400, +/-401 rejected, axis modes, asymmetric mechanics and lower limits");
}

static void VerifyKeys(void)
{
    static const int16_t cycle[] = {0,100,200,300,400,0,-100,-200,-300,-400};
    static const UpperControlState_t blocked[] = {SIM_IDLE,SIM_AUTH_PREPARING,SIM_AUTH_REQUESTED,SIM_AUTHORIZED_WAIT_ZERO,SIM_STOP_REQUESTED,SIM_STOPPING};
    static const UpperControlState_t reset[] = {SIM_LINK_NOT_READY,SIM_STOPPED_RELEASED,SIM_FAULT};
    UpperUiView_t view;
    unsigned index;
    ControlPage(SIM_IDLE);
    for (index = 0U; index < 21U; ++index)
    {
        UpperUi_Publish(&control, &protocol, key_time, &view);
        VERIFY(view.selected_rpm == cycle[index % 10U]);
        VERIFY(control.target_rpm == 0 && frames == 0U && requests == 0U && nonzero == 0U);
        Key(2U, 0U);
    }
    for (index = 0U; index < sizeof(blocked) / sizeof(blocked[0]); ++index)
    {
        ControlPage(blocked[index]);
        Key(2U, 0U);
        Key(2U, 1U);
        VERIFY(control.target_rpm == 0 && nonzero == 0U && requests == 0U);
    }
    ControlPage(SIM_AUTHORIZED_ZERO);
    protocol.snapshot.records[2].payload[6] = 4U;
    Put32(protocol.snapshot.records[2].payload + 12U, 1036U);
    for (index = 1U; index <= 4U; ++index)
    {
        Key(2U, 0U);
        VERIFY(control.target_rpm == (int16_t)(index * 100U) && control.detail == 0U);
        UpperUi_Publish(&control, &protocol, key_time, &view);
        VERIFY(view.selected_rpm == control.target_rpm);
    }
    Key(2U, 0U);
    VERIFY(control.target_rpm == 0);
    Put16(protocol.snapshot.basic[6].data + 2U, 5U);
    Key(2U, 0U);
    VERIFY(control.target_rpm == -100 && control.detail == 0U);
    Put16(protocol.snapshot.basic[6].data + 2U, 0U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, 100, key_time);
    VERIFY(control.target_rpm == 100 && control.detail == 0U);
    Key(4U, 0U);
    UpperUi_Publish(&control, &protocol, key_time, &view);
    VERIFY(control.state == SIM_STOP_REQUESTED && view.selected_rpm == 0 && control.target_rpm == 0);
    for (index = 0U; index < sizeof(reset) / sizeof(reset[0]); ++index)
    {
        ControlPage(SIM_IDLE);
        Key(2U, 0U);
        control.state = reset[index];
        Refresh();
        UpperUi_ScanKeys(key_time, 0U, &control, &protocol);
        UpperUi_Publish(&control, &protocol, key_time, &view);
        VERIFY(view.selected_rpm == 0);
    }
    ControlPage(SIM_IDLE);
    Key(2U, 0U);
    Key(1U, 1U);
    UpperUi_Publish(&control, &protocol, key_time, &view);
    VERIFY(view.selected_rpm == 0 && control.state == SIM_AUTH_PREPARING);
    Key(4U, 0U);
    VERIFY(control.state == SIM_STOP_REQUESTED && control.quick_stop && control.target_rpm == 0 && requests == 0U);
    Key(2U, 0U);
    for (index = 0U; index < UPPER_UI_PAGE_COUNT; ++index) Key(1U, 0U);
    UpperUi_Publish(&control, &protocol, key_time, &view);
    VERIFY(view.page == 2U && view.selected_rpm == 0);
    ControlPage(SIM_RUNNING);
    Key(2U, 1U);
    VERIFY(control.state == SIM_STOP_REQUESTED && !control.quick_stop && control.target_rpm == 0);
    ControlPage(SIM_IDLE);
    for (index = 0U; index < 25U; ++index)
    {
        UpperUi_ScanKeysOnly(key_time, 1U); key_time += 21U;
        UpperUi_ScanKeysOnly(key_time, 1U); key_time += 30U;
        UpperUi_ScanKeysOnly(key_time, 0U); key_time += 21U;
        UpperUi_ScanKeysOnly(key_time, 0U); ++key_time;
    }
    UpperUi_ScanKeysOnly(key_time, 4U); key_time += 21U;
    UpperUi_ScanKeysOnly(key_time, 4U);
    Refresh(); UpperUi_ConsumeEvents(&control, &protocol);
    VERIFY(control.state == SIM_STOP_REQUESTED && control.quick_stop && !control.target_rpm);
    puts("PASS SPEED400 keys: immediate short-apply, long normal-stop, global K3 quick-stop, blocked states, direct reversal, reset");
}

static void VerifySessionLimit(void)
{
    ControlPage(SIM_AUTHORIZED_ZERO);
    Put16(protocol.snapshot.records[2].payload + 24U, 1000U);
    protocol.snapshot.records[2].payload[6] = 4U;
    Put32(protocol.snapshot.records[2].payload + 12U, 1036U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, 300, key_time);
    VERIFY(control.target_rpm == 0 && control.detail == 102U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, -400, key_time);
    VERIFY(control.target_rpm == 0 && control.detail == 102U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, 200, key_time);
    VERIFY(control.target_rpm == 200 && control.detail == 0U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, 401, key_time);
    VERIFY(control.target_rpm == 200 && control.detail == 102U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, -401, key_time);
    VERIFY(control.target_rpm == 200 && control.detail == 102U);
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, 0, key_time);
    protocol.snapshot.records[2].timestamp = key_time - UPPER_CONTROL_MAX_AGE_MS;
    UpperControl_Intent(&control, &protocol, UPPER_INTENT_APPLY, 100, key_time);
    VERIFY(control.target_rpm == 0 && control.detail == 102U);
    puts("PASS SPEED400 session limit: fresh current record required, lower session cap preserved, rejected targets never applied");
}

int main(void)
{
    VerifyConversion();
    VerifyKeys();
    VerifySessionLimit();
    printf("PASS SPEED400 %u assertions; software only, physical 400rpm acceptance pending\n", checks);
    return 0;
}
