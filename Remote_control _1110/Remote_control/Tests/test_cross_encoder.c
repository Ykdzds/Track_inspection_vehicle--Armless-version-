#include "nrf_status_telemetry.h"
#include "remote_status_protocol.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    nrf_status_input_t in;
    remote_status_frame_t out;
    uint8_t frame[32];
    uint8_t axis, type;
    memset(&in, 0, sizeof(in));
    in.snapshot_sequence = 0x12345678UL;
    in.snapshot_ms = 0x87654321UL;
    in.control_owner = 5U;
    assert(nrf_status_encode(frame, 0x8AU, 1U, 2U, &in));
    assert(remote_status_decode(frame, sizeof(frame), &out) == REMOTE_PARSE_OK);
    assert(out.data.metadata.sequence == in.snapshot_sequence);
    assert(out.data.metadata.sample_ms == in.snapshot_ms && out.data.metadata.owner == 5U);
    in.axis_flags = 7U; in.valid_flags = 0xBFU; in.alarm_level = 3U;
    in.system_state = 7U; in.mode = 4U; in.authorization_flags = 127U;
    in.remote.target_rpm = -100; in.remote.commanded_rpm = 0;
    in.remote.left_commanded_rpm = -32768; in.remote.right_commanded_rpm = 32766;
    in.position.axis[0].raw_position = INT32_MIN;
    in.position.axis[1].raw_position = INT32_MAX;
    in.position.difference_mm = -123.0;
    in.position.vehicle_boot_net_mm = -123400.0;
    in.position.vehicle_boot_path_mm = 6553500.0;
    in.position.vehicle_trip_path_mm = 1200.0;
    in.remote.sync.enabled = 1U; in.remote.sync.active = 1U;
    in.remote.sync.pair_valid = 1U; in.remote.sync.integer_correction = -5;
    in.remote.sync.effective_error = -2.0;
    in.remote.sync.age_ms[0] = 10U; in.remote.sync.age_ms[1] = 20U;
    in.alarm_sequence = 255U; in.alarm_code = 2U; in.alarm_source = 3U;
    in.alarm_tick_ms = UINT32_MAX; in.alarm_detail = UINT32_MAX;
    for (axis = 0U; axis < 2U; axis++) {
        in.axis[axis].online = in.axis[axis].speed_valid = in.axis[axis].fault_valid = 1U;
        in.axis[axis].enabled = in.axis[axis].parameter_verified = 1U;
        in.axis[axis].actual_speed_rpm = (int16_t)(-99 + axis);
        in.axis[axis].fault_code = (uint16_t)(0xABCDU + axis);
        in.axis[axis].speed_age_ms = 10U;
        in.axis[axis].diagnostic_valid_mask = 15U;
        in.axis[axis].diagnostic_stale_mask = 4U;
        in.axis[axis].realtime_torque_tenths_percent = -123;
        in.axis[axis].load_ratio_tenths_percent = 456U;
        in.axis[axis].phase_current_deciamp = -123;
        in.axis[axis].bus_voltage_decivolt = 3250U;
    }
    in.axis[0].actual_velocity_mps = -0.32f;
    in.axis[1].actual_velocity_mps = -0.30f;
    for (type = 0x81U; type <= 0x89U; type++) {
        assert(nrf_status_encode(frame, type, 255U, 0x39U, &in));
        assert(remote_status_decode(frame, sizeof(frame), &out) == REMOTE_PARSE_OK);
        assert(out.header.sequence == 255U && out.header.session == 0x39U);
        switch (type) {
        case 0x89U:
            for (axis = 0U; axis < 2U; axis++) {
                assert(out.data.diagnostic.valid_mask[axis] == 15U);
                assert(out.data.diagnostic.stale_mask[axis] == 4U);
                assert(out.data.diagnostic.torque[axis] == -123);
                assert(out.data.diagnostic.load[axis] == 456U);
                assert(out.data.diagnostic.current[axis] == -123);
                assert(out.data.diagnostic.voltage[axis] == 3250U);
            }
            break;
        case 0x81U:
            assert(out.data.fast.target == -100 && out.data.fast.base == 0);
            assert(out.data.fast.command[0] == INT16_MIN && out.data.fast.command[1] == 32766);
            assert(out.data.fast.speed[0] == -99 && out.data.fast.speed[1] == -98); break;
        case 0x82U:
            assert(out.data.servo.fault[0] == 0xABCDU && out.data.servo.fault[1] == 0xABCEU);
            assert(out.data.servo.age[0] == 10U); break;
        case 0x83U:
            assert(out.data.position.raw[0] == INT32_MIN && out.data.position.raw[1] == INT32_MAX);
            assert(out.data.position.difference_mm == -123 && out.data.position.net_dm == -1234);
            assert(out.data.position.boot_dm == 65535U && out.data.position.trip_dm == 12U); break;
        case 0x84U:
            assert(out.data.sync.difference == -1 && out.data.sync.correction == -5);
            assert(out.data.sync.error == -2 && out.data.sync.age[1] == 20U); break;
        case 0x85U:
            assert(out.data.extension.battery == 0U && out.data.extension.charge == 255U);
            assert(!(out.header.valid_flags & 64U)); break;
        case 0x86U:
            assert(out.data.parameter.speed_unit == 1U && out.data.parameter.position_unit == 1U);
            assert(out.data.parameter.wheel_mm[0] > 0U && out.data.parameter.ratio_milli[0] > 0U);
            break;
        case 0x87U:
            assert(out.data.alarm.sequence == 255U && out.data.alarm.code == 2U);
            assert(out.data.alarm.tick == UINT32_MAX && out.data.alarm.detail == UINT32_MAX);
            break;
        case 0x88U:
            assert(out.data.vehicle_speed.target_mm_s < 0);
            assert(out.data.vehicle_speed.command_mm_s == 0);
            assert(out.data.vehicle_speed.front_mm_s == -320);
            assert(out.data.vehicle_speed.rear_mm_s == -300);
            assert(out.data.vehicle_speed.average_mm_s == -310);
            assert(out.data.vehicle_speed.flags == 0x1FU);
            assert(out.data.vehicle_speed.front_age_ms == 10U);
            break;
        default: assert(0);
        }
    }
    in.control_owner = 2U; in.remote.speed_mode = REMOTE_SPEED_KEYS;
    in.remote.common_target_centi_kmh = -2000;
    in.remote.key_exit_pending = in.remote.key_exit_ready = 1U;
    assert(nrf_status_encode(frame, 0x88U, 0U, 0x39U, &in));
    assert(remote_status_decode(frame, 32U, &out) == REMOTE_PARSE_OK);
    assert(out.data.vehicle_speed.control_extended && out.data.vehicle_speed.control_source == 1U);
    assert(out.data.vehicle_speed.common_centi_kmh == -2000 && out.data.vehicle_speed.exit_ready);
    in.control_owner = 5U; in.control_authorized = 1U; in.can_target_mm_s = 1000;
    assert(nrf_status_encode(frame, 0x88U, 0U, 0x39U, &in));
    assert(remote_status_decode(frame, 32U, &out) == REMOTE_PARSE_OK);
    assert(out.data.vehicle_speed.control_source == 3U && out.data.vehicle_speed.common_centi_kmh == 360);
    in.control_owner = 4U;
    assert(nrf_status_encode(frame, 0x88U, 0U, 0x39U, &in));
    assert(remote_status_decode(frame, 32U, &out) == REMOTE_PARSE_OK);
    assert(out.data.vehicle_speed.control_source == 4U && out.data.vehicle_speed.common_centi_kmh == 32767);
    in.axis_flags = 1U; in.axis[0].speed_valid = in.axis[0].fault_valid = 0U;
    assert(nrf_status_encode(frame, 0x82U, 0U, 0x39U, &in));
    assert(remote_status_decode(frame, sizeof(frame), &out) == REMOTE_PARSE_OK);
    assert(out.data.servo.speed[0] == 32767 && out.data.servo.fault[0] == 65535U);
    assert(out.data.servo.speed[1] == 32767 && out.data.servo.state[1] == 0U);
    assert(nrf_status_encode(frame, 0x88U, 0U, 0x39U, &in));
    assert(remote_status_decode(frame, sizeof(frame), &out) == REMOTE_PARSE_OK);
    assert(out.data.vehicle_speed.target_mm_s < 0);
    assert(out.data.vehicle_speed.command_mm_s == 0);
    assert(out.data.vehicle_speed.flags == 3U);
    assert(out.data.vehicle_speed.front_age_ms == UINT16_MAX);
    assert(out.data.vehicle_speed.rear_age_ms == UINT16_MAX);
    in.axis_flags = 2U;
    in.axis[0].speed_valid = in.axis[0].fault_valid = 0U;
    in.axis[1].speed_valid = in.axis[1].fault_valid = 1U;
    in.axis[1].actual_velocity_mps = -0.30f;
    assert(nrf_status_encode(frame, 0x88U, 1U, 0x39U, &in));
    assert(remote_status_decode(frame, sizeof(frame), &out) == REMOTE_PARSE_OK);
    assert(out.data.vehicle_speed.flags == 0x0BU);
    assert(out.data.vehicle_speed.front_mm_s == INT16_MAX);
    assert(out.data.vehicle_speed.rear_mm_s == -300);
    assert(out.data.vehicle_speed.average_mm_s == INT16_MAX);
    assert(out.data.vehicle_speed.front_age_ms == UINT16_MAX);
    assert(out.data.vehicle_speed.rear_age_ms == 10U);
    frame[14] ^= 1U;
    assert(remote_status_decode(frame, sizeof(frame), &out) != REMOTE_PARSE_OK);
    in.axis_flags = 1U;
    in.axis[0].phase_current_deciamp = INT16_MIN;
    in.axis[0].bus_voltage_decivolt = UINT16_MAX;
    assert(nrf_status_encode(frame, 0x89U, 1U, 0x39U, &in));
    assert(remote_status_decode(frame, sizeof(frame), &out) == REMOTE_PARSE_OK);
    assert(out.data.diagnostic.valid_mask[0] == 3U);
    assert(out.data.diagnostic.valid_mask[1] == 0U);
    frame[16] ^= 1U;
    assert(remote_status_decode(frame, sizeof(frame), &out) == REMOTE_PARSE_CRC);
    puts("PASS dual firmware: main encoder -> remote decoder, all 9 types, sentinels and CRC");
    return 0;
}
