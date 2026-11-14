#define K5_RECOVERY_MAIN previous_k5_main
#include "test_can_k5_recovery.c"
#include "can_upper_protocol.h"
#include "can_upper_control.h"
#include "can_upper_ui.h"

static UpperProtocol_t upper;
static UpperControl_t upper_control;
static uint16_t record_sequence;
static uint8_t heartbeat_sequence, pending_command[8], command_pending;

static uint8_t upper_send(void *context, uint16_t id, const uint8_t data[8])
{
    (void)context;
    if (id == 0x110U) { assert(!command_pending); memcpy(pending_command, data, 8U); command_pending = 1U; }
    else if (id == 0x111U)
    {
        assert(app_can_control_speed(data, g_tick));
        UpperControl_TxResult(&upper_control, id, data, 1U, g_tick);
    }
    return 1U;
}

static void send_record(uint8_t type)
{
    uint16_t index, id;
    uint8_t data[8];
    assert(app_can_status_record(&telemetry, type, &can_record));
    can_record.token = 42U; can_record.sequence = ++record_sequence;
    for (index = 0U; can_protocol_encode_record_frame(&can_record, index, &id, data); ++index)
        Upper_Receive(&upper, id, 8U, data, g_tick, g_tick);
}

static void joint_cycle(uint8_t presses)
{
    static const uint16_t ids[] = {0x185U, 0x180U, 0x186U, 0x187U, 0x188U};
    app_can_control_result_t result;
    uint8_t data[8];
    unsigned index;
    if (command_pending)
    {
        uint8_t accepted = pending_command[1] == 8U ? 0U : app_can_control_submit(pending_command, g_tick, 1U);
        memcpy(data, pending_command, 2U); data[2] = accepted; data[3] = 0U;
        can_protocol_write_u16(data + 4U, 42U); data[6] = data[7] = 0U;
        UpperControl_Receive(&upper_control, &upper, 0x120U, data, g_tick, g_tick);
        if (pending_command[1] == 8U)
        {
            data[2] = 3U; data[3] = 0U;
            UpperControl_Receive(&upper_control, &upper, 0x121U, data, g_tick, g_tick);
        }
        command_pending = 0U;
    }
    production_step(presses);
    telemetry.token = 42U; telemetry.boot_id = 1U; telemetry.upper_boot_id = 7U;
    telemetry.config_revision = 7U;
    while (app_can_control_take_result(&result))
    {
        data[0] = result.sequence; data[1] = result.command; data[2] = result.phase; data[3] = result.result;
        can_protocol_write_u16(data + 4U, result.link_token); can_protocol_write_u16(data + 6U, result.detail);
        UpperControl_Receive(&upper_control, &upper, 0x121U, data, g_tick, g_tick);
    }
    for (index = 0U; index < sizeof(ids) / sizeof(ids[0]); ++index)
    {
        assert(app_can_status_basic(&telemetry, ids[index], heartbeat_sequence++, data));
        Upper_Receive(&upper, ids[index], 8U, data, g_tick, g_tick);
    }
    send_record(0x11U);
    if (telemetry.control.stop_generation) send_record(0x13U);
    UpperControl_Tick(&upper_control, &upper, g_tick);
}

static void button(uint8_t mask, uint32_t duration)
{
    uint32_t end = g_tick + duration;
    while (g_tick < end)
    {
        UpperUi_ScanKeys(g_tick, mask, &upper_control, &upper);
        joint_cycle(0U);
    }
}

int main(void)
{
    can_identity_t identity;
    unsigned round, index;
    uint16_t previous_token = 0U;
    reset_fixture(); memset(&remote_input, 0, sizeof(remote_input)); memset(&joint_hold, 0, sizeof(joint_hold));
    remote_input.link_up = 1U; remote_input.left_vertical = 1000U;
    joint_hold_progress = g_feedback_progress = 1U;
    app_remote_servo_init(); can_joint_init();
    memset(&identity, 0, sizeof(identity)); identity.result = CAN_IDENTITY_OK;
    identity.motion_first = 100U; identity.motion_last = 1000U;
    app_can_control_init(&identity);
    Upper_Init(&upper, 7U, upper_send, 0, 0); UpperControl_Init(&upper_control, upper_send, 0); UpperUi_Init();
    upper.snapshot.state = UPPER_READY; upper.snapshot.lower_boot_id = 1U; upper.snapshot.link_token = 42U;
    upper.snapshot.config_revision = 7U; upper.capability_published = 1U;
    joint_cycle(0U); send_record(0x10U);
    button(1U, 80U); button(0U, 80U); button(1U, 80U); button(0U, 80U);
    for (round = 0U; round < 100U; ++round)
    {
        button(1U, UPPER_KEY_LONG_MS + UPPER_KEY_DEBOUNCE_MS + 30U); button(0U, 80U);
        for (index = 0U; index < 100U && upper_control.state != SIM_AUTHORIZED_ZERO; ++index) joint_cycle(0U);
        if (upper_control.state != SIM_AUTHORIZED_ZERO)
            fprintf(stderr, "round=%u upper=%s err=%u gate=%lu owner=%u life=%u\n", round, UpperControl_StateName(upper_control.state), upper_control.detail, (unsigned long)upper_control.gate.mask, telemetry.owner, telemetry.life);
        assert(upper_control.state == SIM_AUTHORIZED_ZERO && upper_control.motion_token != previous_token);
        previous_token = upper_control.motion_token;
        UpperControl_Intent(&upper_control, &upper, UPPER_INTENT_APPLY, round & 1U ? -100 : 100, g_tick);
        for (index = 0U; index < 100U; ++index) joint_cycle(0U);
        assert(upper_control.state == SIM_RUNNING);
        joint_cycle(1U);
        for (index = 0U; index < 1000U && upper_control.state != SIM_STOPPED_RELEASED; ++index)
            joint_cycle(index % 23U == 0U && telemetry.owner == 4U ? 1U : 0U);
        assert(upper_control.state == SIM_STOPPED_RELEASED && !upper_control.detail && !app_remote_servo_k1_latched());
        assert(telemetry.owner == 0U && telemetry.life == 1U && !upper_control.motion_token && !upper_control.target_rpm);
    }
    assert(!g_emergency_calls);
    puts("PASS 100 cycles: real debounced K1 -> grant -> first zero -> +/-100rpm -> real remote K5/repeat -> CAN executor -> encoded CRC records -> F103 release -> new token");
    return 0;
}
