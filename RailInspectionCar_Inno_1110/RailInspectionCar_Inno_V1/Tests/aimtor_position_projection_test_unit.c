/* Production driver, using the existing bus fixture. */
#include "aimtor_test_unit.c"

void aimtor_test_position_projection_seed(uint32_t now, unsigned variant)
{
    unsigned axis, sample;
    for (axis = 0U; axis < SERVO_AXIS_COUNT; ++axis)
    {
        aimtor_axis_state_t *state = &s_axes[axis];
        servo_snapshot_t *in = &state->snapshot;
        state->speed_tick = now - (variant == 2U ? 301U : 17U + axis);
        state->fault_tick = now - 13U;
        state->position_tick = now - (variant == 2U ? 401U : 23U + axis);
        state->position_sequence = 0xFFFFFFFEUL + axis;
        state->speed_valid = state->fault_valid = (uint8_t)(variant != 1U);
        in->speed_valid = in->fault_valid = in->position_valid = (uint8_t)(variant != 1U);
        in->parameter_verified = 1U;
        in->control_mode = SERVO_CONTROL_RS485_VELOCITY;
        in->actual_speed_rpm = axis == 0U ? -751 : 751;
        in->position_generation = 0xC001U + variant;
        in->fault = (uint8_t)(variant == 3U);
        in->busy = (uint8_t)(axis != 0U);
        for (sample = 0U; sample < SERVO_POSITION_SAMPLE_CAPACITY; ++sample)
        {
            memset(&in->position_samples[sample], (int)(sample + axis + variant + 1U),
                   sizeof(in->position_samples[sample]));
        }
    }
}

void aimtor_test_pulse_disabled_entries(void)
{
    uint8_t axis = app_config_servo_axis_selected(0U) ? 0U : 1U;
    aimtor_axis_state_t before;
    assert(APP_SERVO_PULSE_ENABLED == 0U);
    before = s_axes[axis];
    assert(aimtor_set_control_mode((servo_axis_t)axis, SERVO_CONTROL_PULSE_POSITION) == SERVO_UNSUPPORTED);
    assert(aimtor_zero_once((servo_axis_t)axis) == SERVO_UNSUPPORTED);
    assert(memcmp(&before, &s_axes[axis], sizeof(before)) == 0);
    /* Exercise verification with its normal boot-internal permission, keeping
     * every configuration precondition implemented in the production driver. */
    aimtor_init();
    s_boot_zero_internal = 1U;
    assert(aimtor_request_parameter_verify((servo_axis_t)axis) == SERVO_OK);
    assert(s_axes[axis].requested_mode == SERVO_CONTROL_RS485_VELOCITY);
    aimtor_init();
    s_boot_zero_internal = 1U;
    s_axes[axis].snapshot.control_mode = SERVO_CONTROL_PULSE_POSITION;
    before = s_axes[axis];
    assert(aimtor_request_parameter_verify((servo_axis_t)axis) == SERVO_UNSUPPORTED);
    assert(memcmp(&before, &s_axes[axis], sizeof(before)) == 0);
    s_boot_zero_internal = 0U;
}
