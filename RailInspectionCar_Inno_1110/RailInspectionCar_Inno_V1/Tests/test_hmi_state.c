#include "app_hmi_state.h"
#include "app_control_arbiter.h"
#include "app_health.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static hmi_state_input_t in;
static hmi_alarm_state_t alarm;
static uint16_t r[HMI_STATE_REGISTER_COUNT];
static void ready(void)
{
    unsigned i;
    memset(&in, 0, sizeof(in)); memset(&alarm, 0, sizeof(alarm));
    in.identity_valid = in.health_valid = in.owner_valid = in.k1_valid = 1U;
    in.axes_valid = in.position_valid = in.remote_valid = in.cli_valid = in.debug_valid = in.startup_valid = 1U;
    in.boot_id = 0x12345678U; in.sequence = 0x87654321U;
    in.now_tick = in.now_ms = 1000U; in.selected = 3U;
    in.init_complete = in.safety_ready = 1U;
    in.remote.link_valid = 1U; in.failed_task = APP_HEARTBEAT_COUNT;
    for (i = 0U; i < 2U; i++)
    {
        in.axis[i].online = in.axis[i].speed_valid = in.axis[i].fault_valid = in.axis[i].position_valid = 1U;
        in.axis[i].parameter_verified = 1U;
        in.axis[i].actual_position = i ? -123 : 123;
        in.axis[i].speed_samples[0].valid = 1U;
        in.axis[i].speed_samples[0].sample_tick = 990U;
    }
}
static void build(void) { app_hmi_state_build(&in, &alarm, r); }
int main(void)
{
    uint32_t event;
    ready(); in.hmi_control.stop.result = 3U; in.hmi_control.stop.zero_mask = 3U;
    in.hmi_control.session = 42U; in.hmi_control.zero_mask = 0U;
    build(); assert(r[0x47C] == 0U);
    in.hmi_control.session = 0U; build(); assert(r[0x47C] == 3U);
    ready(); build();
    assert(r[0x48A] == 500 && r[0x48B] == 600);
    assert(r[0x18] == 0x1234 && r[0x19] == 0x5678 && r[0x204] == r[0x18]);
    assert(r[0x1A] == 0x8765 && r[0x1B] == 0x4321 && r[0x206] == r[0x1A]);
    assert(r[0x1C] == 1 && r[0x9D] == 1 && r[0x91] == 0 && r[0xCD] == 0);
    assert(r[0x9A] == 0xFFFF && r[0xB3] == 0 && r[0xB0] == 0xFFFF);
    assert(r[0xCC] == 0 && r[0x202] == (APP_HMI_CONTROL_ENABLED ? 255U : 1U));
    assert(r[0x20E] == 0 && r[0x214] == (APP_HMI_CONTROL_ENABLED ? in.selected : 0U));
    assert(r[0x460] == 0x534D && r[0x461] == 2 && r[0x486] == (uint16_t)-1600 && r[0x487] == 1600);
    assert(r[0x72] == 0xFFFF && r[0x73] == (uint16_t)-123);
    assert(r[0x86] == 0x7FFF && r[0x80] % 8 < 4);
    in.axis[0].actual_speed_rpm = 500; in.axis[1].actual_speed_rpm = 500;
    in.axis[0].speed_samples[0].actual_speed_rpm = 100;
    in.axis[1].speed_samples[0].actual_speed_rpm = 100;
    build(); assert(r[0x34] < r[0x22]);
    in.axis[0].speed_samples[0].actual_speed_rpm = 160;
    build(); assert(r[0x80] & 4U); assert(r[0x84] == 60);
    in.remote.authorized = in.remote.sync.active = in.remote.sync.pair_valid = 1U;
    in.remote.sync.raw_error = 7; in.remote.sync.effective_error = 7;
    in.remote.sync.integer_correction = 2;
    build(); assert(r[0x84] == 7 && r[0x85] == 7 && r[0x86] == 2);
    in.remote.sync.state = REMOTE_SYNC_FAULT; build(); assert(r[0x81] == 4);
    ready(); in.remote.estop_latched = 1; in.remote.stop_reason = REMOTE_STOP_K1;
    build(); assert(r[0x93] == 5 && r[0x9A] == 0xFFFF && r[0xCD] == 4);
    ready(); in.axis[0].fault_code = 900; build(); assert(r[0x93] == 2);
    ready(); in.axis[1].fault_code = 900; build(); assert(r[0x93] == 3);
    ready(); in.diagnostic = APP_DIAG_HEARTBEAT_TIMEOUT; build(); assert(r[0x93] == 6);
    ready(); in.position.axis[0].incomplete = 1; build(); assert(r[0x92] == 0x0601 && r[0x93] == 1);
    ready(); in.remote.authorized = 1; in.remote.link_valid = 0;
    build(); assert(r[0x92] == 0x0303 && r[0x93] == 4);
    ready(); in.remote.link_valid = 0; build(); assert(r[0x91] == 0);
    in.remote.estop_latched = 1; in.remote.stop_reason = REMOTE_STOP_LINK;
    build(); assert(r[0x93] == 4 && r[0x92] == 0x0303);
    ready(); in.boot_zero[0].state = SERVO_BOOT_ZERO_FAILED; build(); assert(r[0x92] == 0x0402);
    ready(); build(); event = alarm.sequence;
    in.init_complete = 0; in.axis[0].fault_valid = 0; build();
    assert(r[0x9D] == 0 && r[0x91] == 0xFFFF && r[0xCD] == 0xFFFF && alarm.sequence != event);
    in.axis[1].fault_code = 900; build(); assert(r[0x9D] == 0 && r[0x91] == 3);
    ready(); in.reset_reason = 1UL << 29; build(); assert(r[7] == 4);
    in.reset_reason = 1UL << 28; build(); assert(r[7] == 3);
    in.reset_reason = 1UL << 26; build(); assert(r[7] == 2);
    in.reset_reason = 1UL << 27; build(); assert(r[7] == 1);
    ready(); in.identity_valid = 0; build(); assert(r[0x1C] == 0 && r[0x18] == 0xFFFF && r[0x204] == 0xFFFF);
    ready(); in.axis[0].online = 0; build();
    assert(r[0]==6 && r[8]==0 && r[0x22]==0x7FFF && r[0x9D]==0);
    ready(); in.axes_valid = 0; build(); assert(r[0x8E] == 0 && r[0xCE] == 0);
    ready(); in.debug_valid = 0; build(); assert(r[0xCF] == 0);
    ready(); in.axis[0].enabled = 1; build(); assert(r[0x29] == 0xFFFF);
    in.axis[0].enable_feedback_valid = in.axis[0].enable_feedback = 1;
    build(); assert(r[0x29] == 1);
    in.axis[0].enable_feedback_age_ms = APP_SERVO_STATUS_MAX_AGE_MS;
    build(); assert(r[0x29] == 0xFFFF);
    ready(); in.selected = 1; build(); assert(r[0x8E] == 1 && r[0x81] == 0 && r[0x84] == 0x7FFF);
    ready(); in.axis[0].position_age_ms = APP_POSITION_MAX_AGE_MS + 1; build();
    assert(r[0x8C] == 0 && r[0x70] == 0x7FFF && r[0x71] == 0xFFFF);
    ready(); in.can_valid = 1; in.can.enabled = in.can.task_created = 1;
    in.can.phase = CAN_LINK_READY; in.can.bus.phase = BSP_CAN_READY;
    in.upper[0].present = in.upper[0].valid = 1; in.upper[0].length = 32;
    in.upper[0].payload[10] = in.upper[0].payload[14] = 6;
    build(); assert(r[0xB3] == 0);
    in.upper[3].present = in.upper[3].valid = 1; in.upper[3].length = 32;
    in.upper[5].present = in.upper[5].valid = 1; in.upper[5].length = 32;
    in.upper[5].payload[28] = 1;
    build(); assert(r[0xB3] == 1);
    in.upper[1].present = in.upper[1].valid = 1; in.upper[1].length = 32;
    in.upper[0].payload[10] |= 8; in.upper[0].payload[14] |= 8;
    in.upper[1].payload[28] = 16; in.upper[1].payload[20] = 7;
    build(); assert(r[0x92] == 0x0701 && r[0x93] == 1);
    in.upper[1].valid = 0; build(); assert(r[0x9D] == 0);
    in.upper[5].valid = 0; build(); assert(r[0xB3] == 0 && r[0xB0] == 0xFFFF);
    in.upper[0].payload[14] = 0; build();
    assert((r[0xC2] & (1U << 8)) && r[0xB0] == 0xFFFF && r[0xB3] == 0);
    ready(); in.can_valid = 1; in.can.enabled = in.can.task_created = 1;
    in.can.phase = CAN_LINK_READY; in.can.bus.phase = BSP_CAN_READY;
    in.upper[0].present = in.upper[0].valid = 1; in.upper[0].length = 32;
    in.upper[0].payload[11] = 0x02; in.upper[0].payload[15] = 0x02;
    in.arm_valid[0] = in.arm_valid[1] = 1U;
    in.arm[0].valid = in.arm[1].valid = 1U;
    in.arm[0].sample_age_ms = in.arm[1].sample_age_ms = 10U;
    in.arm[0].state = in.arm[1].state = 1U;
    in.arm[0].flags = in.arm[1].flags = 3U;
    build(); assert(r[0xA8] == 1U && r[0xA9] == 0U && (r[0xC2] & (1U << 5)));
    in.arm[1].flags = 1U << 2; build(); assert(r[0xA8] == 2U && r[0xA9] == 0U);
    in.arm[0].flags = 1U << 4; build(); assert(r[0xA8] == 3U && r[0xA9] == 0U);
    in.arm_valid[1] = 0U; build(); assert(r[0xA8] == 0xFFFFU && r[0xA9] == 0U);
    in.arm_valid[1] = 1U; in.arm[1].valid = 0U; build(); assert(r[0xA8] == 0xFFFFU);
    in.upper[0].payload[15] = 0U; build(); assert(r[0xA8] == 0U && r[0xA9] == 0U);
    ready(); in.now_tick = 5; in.axis[0].speed_samples[0].sample_tick = UINT32_MAX - 5;
    in.axis[1].speed_samples[0].sample_tick = UINT32_MAX - 5; build(); assert(r[0x82] == 1);
    ready(); in.position.enabled = in.position.vehicle_valid = 1;
    in.position.vehicle_boot_path_mm = -1; build(); assert(r[0x76] == 0x7FFF);
    in.position.vehicle_boot_path_mm = 2147483647.0; build(); assert(r[0x76] == 0x7FFF);
    ready(); in.owner = APP_CONTROL_OWNER_CLI; in.cli.authorized = 1;
    in.debug.mode = SERVO_DEBUG_MODE_RS485_POSITION; in.cli.mode = SERVO_DEBUG_MODE_RS485_POSITION;
    build(); assert(r[0xCB] == 4 && r[0xCC] == 0 && r[0x36] == 2);
    in.debug.mode = SERVO_DEBUG_MODE_PULSE; in.cli.mode = SERVO_DEBUG_MODE_PULSE;
    build(); assert(r[0x36] == 2);
    ready(); in.remote.hold_stopping = 1; build(); assert(r[0] == 4 && r[0x9B] == 1);
    ready(); in.owner = APP_CONTROL_OWNER_AUTO; in.motion = MOTION_RUNNING;
    build(); assert(r[0] == 3 && r[0xCB] == 3 && r[0xCC] == 0);
    ready(); in.owner = APP_CONTROL_OWNER_CAN; in.can_control.authorized = true;
    in.can_control.commanded_rpm[0] = 10; in.can_control.commanded_rpm[1] = 10;
    build(); assert(r[0xCB] == 3 && r[0xCC] == 0);
    in.can_control.authorized = false; in.can_control.stop_phase = 2U;
    build(); assert(r[0] == 4 && r[0x9B] == 1);
    in.can_control.stop_phase = 4U; build(); assert(r[0x9B] == 2);
    ready(); in.failed_task = APP_HEARTBEAT_NRF; build(); assert(r[0x5E] == 3);
    ready(); in.sequence = UINT32_MAX; build(); assert(r[0x1A] == 0xFFFF && r[0x1B] == 0xFFFF);
    ready(); in.owner = APP_CONTROL_OWNER_HMI;
    in.hmi_control.flags = 128U | 4U; in.hmi_control.state = 4U;
    in.hmi_control.target = 100; in.hmi_control.output[0] = 90;
    in.hmi_control.output_mask = 1U; in.hmi_control.stop.event = 0x12345678UL;
    in.hmi_control.stop.result = 2U; in.hmi_control.stop.owner = APP_CONTROL_OWNER_HMI;
    in.hmi_control.stop.deceleration_mm_s2 = 400U;
    build(); assert(r[0xCC] == 0 && r[0x17] == 1 && r[0] == 4);
    assert(r[0x472] == 100 && r[0x475] == 90 && r[0x476] == 0x7FFF);
    assert(r[0x490] == 0x1234 && r[0x491] == 0x5678 && r[0x495] == 2);
    assert(r[0x47F] == 400 && r[0x499] == 1);
    assert(r[0x49A] == 0U && r[0x49B] == 1U);
    assert(r[0x49F] == APP_SYSTEM_SNAPSHOT_PERIOD_MS);
    ready(); in.owner = APP_CONTROL_OWNER_CAN; in.can_control.authorized = true;
    in.can_control.target_mm_s = 123; build(); assert(r[0x49A] == 123U);
    in.lock_reason = APP_CONTROL_LOCK_SAFETY; build(); assert(r[0x49A] == 0U);
    ready(); in.safety_stop_class = MOTION_STOP_CLASS_CONTROLLED_SAFETY;
    in.safety_fault_reason = MOTION_FAULT_REASON_FEEDBACK_STALE;
    build();
    assert(alarm.code == 0x0404U && alarm.source == 1U && alarm.level == 4U);
    assert(alarm.parameter == ((MOTION_STOP_CLASS_CONTROLLED_SAFETY << 16U) |
                              MOTION_FAULT_REASON_FEEDBACK_STALE));
    /* The request encoder must clear validity masks even when its reused
     * window previously contained valid values, and match the full dictionary. */
    {
        uint16_t control[HMI_CONTROL_REGISTER_COUNT];
        ready(); build();
        memset(control, 0xFF, sizeof(control));
        app_hmi_state_build_control(&in, control);
        assert(memcmp(control, &r[HMI_CONTROL_REGISTER_START], sizeof(control)) == 0);
        in.axes_valid = 0U; in.selected = 0U; build();
        app_hmi_state_build_control(&in, control);
        assert(memcmp(control, &r[HMI_CONTROL_REGISTER_START], sizeof(control)) == 0);
        assert(control[0x19U] == 0U && control[0x1AU] == 0U && control[0x1BU] == 0U);
    }
    puts("PASS HMI state production: six regions, validity, sources, paired velocity, sync, unknowns and Tick wrap");
    return 0;
}
