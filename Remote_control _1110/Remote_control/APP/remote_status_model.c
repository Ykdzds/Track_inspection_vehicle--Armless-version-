#include "remote_status_model.h"
#include <string.h>

static const uint32_t type_timeout[REMOTE_STATUS_TYPES] = {
    500U, 500U, 2500U, 500U, 2500U, 15000U, 1000U, 500U, 3000U, 2500U
};

uint8_t remote_history_axis(remote_history_field_t field)
{
    if (field == RH_CMD1 || field == RH_FAST1 || field == RH_STATE1 ||
        field == RH_ENABLE1 || field == RH_ZERO1 || field == RH_FAULT1 ||
        field == RH_RPM1 || field == RH_AGE1 || field == RH_FAIL1 ||
        field == RH_RAW1 || field == RH_SYNC_AGE1 || field == RH_COUNTS1 ||
        field == RH_WHEEL1 || field == RH_RATIO1 || field == RH_DIRECTION1 ||
        field == RH_FRONT || field == RH_FRONT_AGE || field == RH_TORQUE1 ||
        field == RH_LOAD1 || field == RH_CURRENT1 || field == RH_VOLTAGE1 ||
        field == RH_TARGET_KMH1) return 1U;
    if (field == RH_CMD2 || field == RH_FAST2 || field == RH_STATE2 ||
        field == RH_ENABLE2 || field == RH_ZERO2 || field == RH_FAULT2 ||
        field == RH_RPM2 || field == RH_AGE2 || field == RH_FAIL2 ||
        field == RH_RAW2 || field == RH_SYNC_AGE2 || field == RH_COUNTS2 ||
        field == RH_WHEEL2 || field == RH_RATIO2 || field == RH_DIRECTION2 ||
        field == RH_REAR || field == RH_REAR_AGE || field == RH_TORQUE2 ||
        field == RH_LOAD2 || field == RH_CURRENT2 || field == RH_VOLTAGE2 ||
        field == RH_TARGET_KMH2) return 2U;
    return 0U;
}

static uint8_t history_type(remote_history_field_t field)
{
    if (field <= RH_FAST2) return 0x81U;
    if (field <= RH_FAIL2) return 0x82U;
    if (field <= RH_INCOMPLETE) return 0x83U;
    if (field <= RH_SYNC_AGE2) return 0x84U;
    if (field <= RH_EXT_FAULTS) return 0x85U;
    if (field <= RH_POSITION_UNIT) return 0x86U;
    if (field <= RH_ALARM_SEQUENCE) return 0x87U;
    if (field <= RH_REAR_AGE) return 0x88U;
    if (field <= RH_VOLTAGE2) return 0x89U;
    return 0x81U;
}

uint8_t remote_history_fresh(const remote_status_model_t *m,
                             remote_history_field_t field, uint32_t now)
{
    const remote_history_value_t *value = &m->history[field];
    uint8_t type = history_type(field);
    uint32_t elapsed = now - value->updated_ms;
    uint8_t required = 0U;
    if (type == 0x81U) required = field >= RH_FAST1 && field <= RH_FAST2 ? 3U : 1U;
    else if ((field >= RH_ENABLE1 && field <= RH_RPM2)) required = 2U;
    else if (field == RH_RAW1 || field == RH_RAW2) required = 4U;
    else if (field == RH_DIFF) required = 12U;
    else if (field >= RH_NET && field <= RH_INCOMPLETE) required = 16U;
    else if (field >= RH_DELTA && field <= RH_CORRECTION) required = 34U;
    else if (type == 0x85U) required = 64U;
    if (required && !remote_status_valid(m, type, required, now)) return 0U;
    if (!value->received || value->stale ||
        !remote_status_fresh(m, type, now) || elapsed > type_timeout[type - 0x81U])
        return 0U;
    if ((field >= RH_RPM1 && field <= RH_RPM2) || (field >= RH_ZERO1 && field <= RH_ZERO2) ||
        (field >= RH_FRONT && field <= RH_AVERAGE) ||
        (field >= RH_DELTA && field <= RH_CORRECTION)) {
        if (value->sample_age >= REMOTE_SAMPLE_TIMEOUT_MS ||
            elapsed >= REMOTE_SAMPLE_TIMEOUT_MS - value->sample_age) return 0U;
    }
    if (field >= RH_TARGET_KMH1)
        return (uint8_t)(remote_history_fresh(m, (remote_history_field_t)
            (RH_CMD1 + field - RH_TARGET_KMH1), now) &&
            remote_history_fresh(m, (remote_history_field_t)
            (RH_WHEEL1 + field - RH_TARGET_KMH1), now) &&
            remote_history_fresh(m, (remote_history_field_t)
            (RH_RATIO1 + field - RH_TARGET_KMH1), now) &&
            remote_history_fresh(m, RH_SPEED_UNIT, now) &&
            m->history[RH_SPEED_UNIT].value == 1);
    if ((field >= RH_TARGET && field <= RH_FAST2) ||
        (field >= RH_RPM1 && field <= RH_RPM2) ||
        (field >= RH_DELTA && field <= RH_CORRECTION)) {
        if (m->history[RH_SPEED_UNIT].received &&
            (!remote_history_fresh(m, RH_SPEED_UNIT, now) ||
             m->history[RH_SPEED_UNIT].value != 1)) return 0U;
    }
    if (field >= RH_RAW1 && field <= RH_TRIP &&
        m->history[RH_POSITION_UNIT].received &&
        (!remote_history_fresh(m, RH_POSITION_UNIT, now) ||
         m->history[RH_POSITION_UNIT].value != 1)) return 0U;
    return 1U;
}

static void history_put(remote_status_model_t *m, remote_history_field_t field,
                         int32_t value, uint8_t valid, uint8_t stale,
                         uint16_t age, uint32_t now)
{
    remote_history_value_t *cached = &m->history[field];
    if (valid) {
        cached->value = value;
        cached->received = 1U;
        cached->updated_ms = now;
        cached->sample_age = age;
    }
    cached->stale = (uint8_t)(!valid || stale);
}

static void history_update(remote_status_model_t *m,
                            const remote_status_frame_t *frame, uint32_t now)
{
    uint8_t axis, selected, valid;
    const remote_status_header_t *header = &frame->header;
    uint8_t speed_unit = (uint8_t)(!m->history[RH_SPEED_UNIT].received ||
        m->history[RH_SPEED_UNIT].value == 1);
    uint8_t position_unit = (uint8_t)(!m->history[RH_POSITION_UNIT].received ||
        m->history[RH_POSITION_UNIT].value == 1);
    for (axis = 0U; axis < 2U; axis++) {
        if (!(header->axis_flags & (1U << axis))) {
            unsigned field;
            for (field = 0U; field < RH_COUNT; field++)
                if (remote_history_axis((remote_history_field_t)field) == axis + 1U)
                    memset(&m->history[field], 0, sizeof(m->history[field]));
        }
    }
    if ((header->axis_flags & 3U) != 3U) {
        memset(&m->history[RH_DIFF], 0, sizeof(m->history[RH_DIFF]));
        memset(&m->history[RH_AVERAGE], 0, sizeof(m->history[RH_AVERAGE]));
        memset(&m->history[RH_PAIR], 0, sizeof(m->history[RH_PAIR]));
        memset(&m->history[RH_DELTA], 0, sizeof(m->history[RH_DELTA]));
        memset(&m->history[RH_ERROR], 0, sizeof(m->history[RH_ERROR]));
        memset(&m->history[RH_CORRECTION], 0, sizeof(m->history[RH_CORRECTION]));
    }
    switch (header->type) {
    case 0x81U: {
        const remote_fast_t *value = &frame->data.fast;
        valid = (uint8_t)(header->valid_flags & 1U);
        history_put(m, RH_SYSTEM, value->system, valid, 0U, 0U, now);
        history_put(m, RH_MODE, value->mode, valid, 0U, 0U, now);
        history_put(m, RH_AUTH, value->authorization, valid, 0U, 0U, now);
        history_put(m, RH_COMM, value->communication, valid, 0U, 0U, now);
        history_put(m, RH_TARGET, value->target, valid && speed_unit && value->target != 32767, 0U, 0U, now);
        history_put(m, RH_BASE, value->base, valid && speed_unit && value->base != 32767, 0U, 0U, now);
        for (axis = 0U; axis < 2U; axis++) {
            selected = (uint8_t)(header->axis_flags & (1U << axis));
            history_put(m, (remote_history_field_t)(RH_CMD1 + axis), value->command[axis],
                selected && valid && speed_unit && value->command[axis] != 32767, 0U, 0U, now);
            history_put(m, (remote_history_field_t)(RH_FAST1 + axis), value->speed[axis],
                selected && speed_unit && (header->valid_flags & 3U) == 3U && value->speed[axis] != 32767,
                0U, 0U, now);
        }
        break;
    }
    case 0x82U: {
        const remote_servo_t *value = &frame->data.servo;
        for (axis = 0U; axis < 2U; axis++) {
            selected = (uint8_t)(header->axis_flags & (1U << axis));
            valid = (uint8_t)(selected && (header->valid_flags & 2U));
            {
                uint8_t speed_ok = (uint8_t)(valid && (value->state[axis] & 9U) == 9U &&
                    value->speed[axis] != 32767 && value->age[axis] < REMOTE_SAMPLE_TIMEOUT_MS);
                uint8_t fault_ok = (uint8_t)(valid && (value->state[axis] & 17U) == 17U &&
                    value->fault[axis] != 65535U);
                history_put(m, (remote_history_field_t)(RH_STATE1 + axis), value->state[axis],
                    selected, 0U, 0U, now);
                history_put(m, (remote_history_field_t)(RH_ENABLE1 + axis),
                    (value->state[axis] & 2U) != 0U, fault_ok, 0U, 0U, now);
                history_put(m, (remote_history_field_t)(RH_ZERO1 + axis),
                    (value->state[axis] & 32U) != 0U, speed_ok, 0U, value->age[axis], now);
            }
            history_put(m, (remote_history_field_t)(RH_FAULT1 + axis), value->fault[axis],
                valid && (value->state[axis] & 17U) == 17U && value->fault[axis] != 65535U,
                0U, 0U, now);
            history_put(m, (remote_history_field_t)(RH_RPM1 + axis), value->speed[axis],
                valid && speed_unit && (value->state[axis] & 9U) == 9U && value->speed[axis] != 32767 &&
                value->age[axis] < REMOTE_SAMPLE_TIMEOUT_MS, 0U, value->age[axis], now);
            history_put(m, (remote_history_field_t)(RH_AGE1 + axis), value->age[axis],
                selected && value->age[axis] != 65535U, 0U, 0U, now);
            history_put(m, (remote_history_field_t)(RH_FAIL1 + axis), value->failures[axis],
                selected, 0U, 0U, now);
        }
        break;
    }
    case 0x83U: {
        const remote_position_t *value = &frame->data.position;
        valid = (uint8_t)((header->valid_flags & 4U) && position_unit);
        for (axis = 0U; axis < 2U; axis++)
            history_put(m, (remote_history_field_t)(RH_RAW1 + axis), value->raw[axis],
                valid && (header->axis_flags & (1U << axis)), 0U, 0U, now);
        history_put(m, RH_DIFF, value->difference_mm,
            valid && (header->valid_flags & 8U) && (header->axis_flags & 3U) == 3U,
            0U, 0U, now);
        valid = (uint8_t)((header->valid_flags & 16U) && position_unit);
        history_put(m, RH_NET, value->net_dm, valid, 0U, 0U, now);
        history_put(m, RH_BOOT, value->boot_dm, valid, 0U, 0U, now);
        history_put(m, RH_TRIP, value->trip_dm, valid, 0U, 0U, now);
        history_put(m, RH_INCOMPLETE, (header->valid_flags & 128U) != 0U,
            valid, 0U, 0U, now);
        break;
    }
    case 0x84U: {
        const remote_sync_t *value = &frame->data.sync;
        uint16_t age = value->age[0] > value->age[1] ? value->age[0] : value->age[1];
        history_put(m, RH_SYNC_FLAGS, value->flags, 1U, 0U, 0U, now);
        history_put(m, RH_SYNC_STATE, value->state, 1U, 0U, 0U, now);
        history_put(m, RH_PAIR, value->pair, (header->axis_flags & 3U) == 3U, 0U, 0U, now);
        history_put(m, RH_REJECT, value->reject, 1U, 0U, 0U, now);
        valid = (uint8_t)(speed_unit && (header->valid_flags & 34U) == 34U && value->pair == 1U &&
            (header->axis_flags & 3U) == 3U && age < REMOTE_SAMPLE_TIMEOUT_MS);
        history_put(m, RH_DELTA, value->difference, valid && value->difference != 32767,
            0U, age, now);
        history_put(m, RH_ERROR, value->error, valid && value->error != 32767, 0U, age, now);
        history_put(m, RH_CORRECTION, value->correction,
            valid && value->correction != 32767, 0U, age, now);
        history_put(m, RH_SKEW, value->skew, 1U, 0U, 0U, now);
        for (axis = 0U; axis < 2U; axis++)
            history_put(m, (remote_history_field_t)(RH_SYNC_AGE1 + axis), value->age[axis],
                (header->axis_flags & (1U << axis)) && value->age[axis] != 65535U,
                0U, 0U, now);
        break;
    }
    case 0x85U: {
        const remote_extension_t *value = &frame->data.extension;
        valid = (uint8_t)(header->valid_flags & 64U);
        history_put(m, RH_BATTERY, value->battery, valid, 0U, 0U, now);
        history_put(m, RH_CHARGE, value->charge,
            valid && value->battery >= 1U && value->battery <= 4U && value->charge <= 100U,
            0U, 0U, now);
        history_put(m, RH_LOCATION, value->location, valid, 0U, 0U, now);
        history_put(m, RH_ACCURACY, value->accuracy,
            valid && value->location >= 1U && value->location <= 3U &&
            value->accuracy >= 1U && value->accuracy <= 3U, 0U, 0U, now);
        history_put(m, RH_ARM, value->arm, valid, 0U, 0U, now);
        history_put(m, RH_LIGHT, value->light, valid, 0U, 0U, now);
        history_put(m, RH_ACQUISITION, value->acquisition, valid, 0U, 0U, now);
        history_put(m, RH_EXT_FAULTS, value->faults, valid, 0U, 0U, now);
        break;
    }
    case 0x86U: {
        const remote_parameter_t *value = &frame->data.parameter;
        for (axis = 0U; axis < 2U; axis++) {
            selected = (uint8_t)(header->axis_flags & (1U << axis));
            history_put(m, (remote_history_field_t)(RH_COUNTS1 + axis), value->counts[axis],
                selected && value->counts[axis] != 0U, 0U, 0U, now);
            history_put(m, (remote_history_field_t)(RH_WHEEL1 + axis), value->wheel_mm[axis],
                selected && value->wheel_mm[axis] != 0U, 0U, 0U, now);
            history_put(m, (remote_history_field_t)(RH_RATIO1 + axis), value->ratio_milli[axis],
                selected && value->ratio_milli[axis] != 0U, 0U, 0U, now);
            history_put(m, (remote_history_field_t)(RH_DIRECTION1 + axis), value->direction[axis],
                selected, 0U, 0U, now);
        }
        history_put(m, RH_SPEED_UNIT, value->speed_unit, 1U, 0U, 0U, now);
        history_put(m, RH_POSITION_UNIT, value->position_unit, 1U, 0U, 0U, now);
        break;
    }
    case 0x87U: {
        const remote_alarm_t *value = &frame->data.alarm;
        history_put(m, RH_ALARM_CODE, value->code, 1U, 0U, 0U, now);
        history_put(m, RH_ALARM_SOURCE, value->source, 1U, 0U, 0U, now);
        history_put(m, RH_ALARM_SEQUENCE, value->sequence, 1U, 0U, 0U, now);
        break;
    }
    case 0x88U: {
        const remote_vehicle_speed_t *value = &frame->data.vehicle_speed;
        uint8_t front = (uint8_t)((header->axis_flags & 1U) && (value->flags & 4U) &&
            value->front_mm_s != 32767 && value->front_mm_s != -32768 &&
            value->front_age_ms < REMOTE_SAMPLE_TIMEOUT_MS);
        uint8_t rear = (uint8_t)((header->axis_flags & 2U) && (value->flags & 8U) &&
            value->rear_mm_s != 32767 && value->rear_mm_s != -32768 &&
            value->rear_age_ms < REMOTE_SAMPLE_TIMEOUT_MS);
        history_put(m, RH_VTARGET, value->target_mm_s,
            (value->flags & 1U) && value->target_mm_s != 32767 && value->target_mm_s != -32768,
            0U, 0U, now);
        history_put(m, RH_VCOMMAND, value->command_mm_s,
            (value->flags & 2U) && value->command_mm_s != 32767 && value->command_mm_s != -32768,
            0U, 0U, now);
        history_put(m, RH_FRONT, value->front_mm_s, front, 0U, value->front_age_ms, now);
        history_put(m, RH_REAR, value->rear_mm_s, rear, 0U, value->rear_age_ms, now);
        history_put(m, RH_AVERAGE, value->average_mm_s,
            front && rear && (value->flags & 16U) && value->average_mm_s != 32767 &&
            value->average_mm_s != -32768, 0U,
            value->front_age_ms > value->rear_age_ms ? value->front_age_ms : value->rear_age_ms, now);
        history_put(m, RH_FRONT_AGE, value->front_age_ms,
            (header->axis_flags & 1U) && value->front_age_ms != 65535U, 0U, 0U, now);
        history_put(m, RH_REAR_AGE, value->rear_age_ms,
            (header->axis_flags & 2U) && value->rear_age_ms != 65535U, 0U, 0U, now);
        break;
    }
    case 0x89U: {
        const remote_servo_diagnostic_t *value = &frame->data.diagnostic;
        for (axis = 0U; axis < 2U; axis++) {
            selected = (uint8_t)(header->axis_flags & (1U << axis));
            valid = selected ? value->valid_mask[axis] : 0U;
            history_put(m, (remote_history_field_t)(RH_TORQUE1 + axis), value->torque[axis],
                valid & 1U, value->stale_mask[axis] & 1U, 0U, now);
            history_put(m, (remote_history_field_t)(RH_LOAD1 + axis), value->load[axis],
                valid & 2U, value->stale_mask[axis] & 2U, 0U, now);
            history_put(m, (remote_history_field_t)(RH_CURRENT1 + axis), value->current[axis],
                valid & 4U, value->stale_mask[axis] & 4U, 0U, now);
            history_put(m, (remote_history_field_t)(RH_VOLTAGE1 + axis), value->voltage[axis],
                valid & 8U, value->stale_mask[axis] & 8U, 0U, now);
        }
        break;
    }
    default: break;
    }
    for (axis = 0U; axis < 2U; axis++) {
        remote_history_value_t *command = &m->history[RH_CMD1 + axis];
        remote_history_value_t *wheel = &m->history[RH_WHEEL1 + axis];
        remote_history_value_t *ratio = &m->history[RH_RATIO1 + axis];
        if ((header->type == 0x81U || header->type == 0x86U) &&
            (header->axis_flags & (1U << axis)) && command->received &&
            !command->stale && now - command->updated_ms <= 500U &&
            wheel->received && !wheel->stale && now - wheel->updated_ms <= 15000U &&
            ratio->received && !ratio->stale && now - ratio->updated_ms <= 15000U &&
            m->history[RH_SPEED_UNIT].value == 1 && m->history[RH_SPEED_UNIT].received) {
            int64_t numerator = (int64_t)command->value * wheel->value * 3141593LL * 3LL;
            int64_t denominator = (int64_t)ratio->value * 5000000LL;
            int64_t magnitude = numerator < 0 ? -numerator : numerator;
            int64_t tenths = (magnitude + denominator / 2) / denominator;
            history_put(m, (remote_history_field_t)(RH_TARGET_KMH1 + axis),
                tenths > 2147483647LL ? 0 : (int32_t)(numerator < 0 ? -tenths : tenths),
                tenths <= 2147483647LL, 0U, 0U, command->updated_ms);
        }
    }
}

void remote_status_init(remote_status_model_t *m)
{
    memset(m, 0, sizeof(*m));
}

static void remote_status_event(remote_status_model_t *model, uint32_t now,
                                uint8_t kind, const remote_status_header_t *header)
{
    uint8_t index = (uint8_t)((model->event_head + model->event_count) %
                             REMOTE_STATUS_EVENT_CAPACITY);
    remote_status_event_t *event = &model->events[index];
    event->timestamp_ms = now;
    event->kind = kind;
    event->type = header->type;
    event->session = header->session;
    event->sequence = header->sequence;
    if (model->event_count < REMOTE_STATUS_EVENT_CAPACITY) model->event_count++;
    else {
        model->event_head = (uint8_t)((model->event_head + 1U) % REMOTE_STATUS_EVENT_CAPACITY);
        model->event_overwrites++;
    }
}

uint8_t remote_status_pop_event(remote_status_model_t *model, remote_status_event_t *event)
{
    if (model == NULL || event == NULL || model->event_count == 0U) return 0U;
    *event = model->events[model->event_head];
    model->event_head = (uint8_t)((model->event_head + 1U) % REMOTE_STATUS_EVENT_CAPACITY);
    model->event_count--;
    return 1U;
}

void remote_status_note_payload(remote_status_model_t *m, uint32_t now)
{
    if (m == NULL) return;
    if (m->payloads_received != 0U &&
        now - m->last_payload_ms > m->max_payload_gap_ms)
        m->max_payload_gap_ms = now - m->last_payload_ms;
    m->last_payload_ms = now;
    m->payloads_received++;
}

remote_return_state_t remote_status_link(const remote_status_model_t *m,
                                         uint32_t now)
{
    if (!m->received) return REMOTE_RETURN_WAIT;
    return now - m->last_received_ms > REMOTE_RETURN_TIMEOUT_MS ?
           REMOTE_RETURN_LOST : REMOTE_RETURN_ONLINE;
}

void remote_status_tick(remote_status_model_t *m, uint32_t now)
{
    if (remote_status_link(m, now) == REMOTE_RETURN_LOST && !m->loss_reported) {
        m->stats.link_losses++;
        m->loss_reported = 1U;
        remote_status_event(m, now, REMOTE_EVENT_LOST, &m->latest);
    }
}

uint8_t remote_status_accept(remote_status_model_t *m, const uint8_t *data,
                             size_t length, uint32_t now)
{
    remote_status_frame_t f;
    remote_parse_result_t result = remote_status_decode(data, length, &f);
    uint8_t delta;
    uint8_t recovering;
    uint8_t candidate_delta;
    remote_status_slot_t *slot;
    remote_status_tick(m, now);
    if (result != REMOTE_PARSE_OK) {
        remote_status_header_t raw_header;
        memset(&raw_header, 0, sizeof(raw_header));
        if (data != NULL && length >= 7U) {
            raw_header.type = data[3];
            raw_header.sequence = data[4];
            raw_header.session = data[6];
        }
        if (result == REMOTE_PARSE_LENGTH) m->stats.length_errors++;
        else if (result == REMOTE_PARSE_CRC) m->stats.crc_errors++;
        else m->stats.format_errors++;
        remote_status_event(m, now, result == REMOTE_PARSE_LENGTH ? REMOTE_EVENT_LENGTH :
            (result == REMOTE_PARSE_CRC ? REMOTE_EVENT_CRC : REMOTE_EVENT_FORMAT), &raw_header);
        return 0U;
    }
    recovering = (uint8_t)(remote_status_link(m, now) == REMOTE_RETURN_LOST);
    if (m->received && f.header.session != m->latest.session) {
        /* 会话变化仅清瞬时缓存，历史通信统计不清零。 */
        memset(m->slot, 0, sizeof(m->slot));
        memset(&m->diagnostic_cache, 0, sizeof(m->diagnostic_cache));
        memset(m->history, 0, sizeof(m->history));
        m->alarm_matches = 0U;
        m->recovery_candidate_valid = 0U;
        m->stats.session_changes++;
        remote_status_event(m, now, REMOTE_EVENT_SESSION, &f.header);
    } else if (m->received) {
        delta = (uint8_t)(f.header.sequence - m->latest.sequence);
        if (delta == 0U) {
            m->stats.duplicates++;
            remote_status_event(m, now, REMOTE_EVENT_DUPLICATE, &f.header);
            return 0U;
        }
        if (delta < 128U) {
            m->stats.sequence_gaps += (uint32_t)(delta - 1U);
            m->recovery_candidate_valid = 0U;
        } else if (!recovering) {
            m->stats.old_frames++;
            remote_status_event(m, now, REMOTE_EVENT_OLD, &f.header);
            return 0U;
        } else if (!m->recovery_candidate_valid ||
                   now - m->recovery_candidate_ms >
                       REMOTE_RECOVERY_CANDIDATE_TIMEOUT_MS) {
            m->recovery_candidate = f.header;
            m->recovery_candidate_ms = now;
            m->recovery_candidate_valid = 1U;
            m->stats.recovery_candidates++;
            remote_status_event(m, now, REMOTE_EVENT_CANDIDATE, &f.header);
            return 0U;
        } else {
            candidate_delta = (uint8_t)(f.header.sequence -
                m->recovery_candidate.sequence);
            if (candidate_delta == 0U) {
                m->stats.duplicates++;
                remote_status_event(m, now, REMOTE_EVENT_DUPLICATE, &f.header);
                return 0U;
            }
            if (candidate_delta >= 128U) {
                m->stats.old_frames++;
                remote_status_event(m, now, REMOTE_EVENT_OLD, &f.header);
                return 0U;
            }
            m->stats.sequence_gaps += (uint32_t)(candidate_delta - 1U);
        }
    }
    if (recovering) {
        memset(m->slot, 0, sizeof(m->slot));
        m->alarm_matches = 0U;
        m->stats.recoveries++;
        remote_status_event(m, now, REMOTE_EVENT_RECOVERED, &f.header);
        m->recovery_candidate_valid = 0U;
    }
    if (!m->received || f.header.alarm_level != m->latest.alarm_level ||
        f.header.alarm_level == 0U) m->alarm_matches = 0U;
    slot = &m->slot[f.header.type - 0x81U];
    if (f.header.type == 0x87U) {
        if (!slot->received ||
            f.data.alarm.sequence != slot->frame.data.alarm.sequence)
            m->stats.alarm_events++;
        m->alarm_matches = (uint8_t)(f.data.alarm.level != 0U);
    }
    slot->frame = f; slot->received_ms = now; slot->received = 1U;
    if (f.header.type == 0x89U) {
        uint8_t axis;
        remote_servo_diagnostic_t *cached = &m->diagnostic_cache.frame.data.diagnostic;
        const remote_servo_diagnostic_t *incoming = &f.data.diagnostic;
        for (axis = 0U; axis < 2U; axis++) {
            uint8_t valid = incoming->valid_mask[axis];
            if (!(f.header.axis_flags & (1U << axis))) {
                cached->valid_mask[axis] = 0U;
                cached->stale_mask[axis] = 0U;
                continue;
            }
            if (valid & 1U) cached->torque[axis] = incoming->torque[axis];
            if (valid & 2U) cached->load[axis] = incoming->load[axis];
            if (valid & 4U) cached->current[axis] = incoming->current[axis];
            if (valid & 8U) cached->voltage[axis] = incoming->voltage[axis];
            cached->stale_mask[axis] = (uint8_t)(incoming->stale_mask[axis] |
                (cached->valid_mask[axis] & (uint8_t)~valid));
            cached->valid_mask[axis] |= valid;
        }
        m->diagnostic_cache.frame.header = f.header;
        m->diagnostic_cache.received_ms = now;
        m->diagnostic_cache.received = 1U;
    }
    history_update(m, &f, now);
    if (m->received &&
        now - m->last_received_ms > m->max_accept_gap_ms)
        m->max_accept_gap_ms = now - m->last_received_ms;
    m->latest = f.header;
    m->last_received_ms = now; m->received = 1U; m->loss_reported = 0U;
    m->stats.accepted++;
    return 1U;
}

uint8_t remote_status_fresh(const remote_status_model_t *m, uint8_t type,
                            uint32_t now)
{
    const remote_status_slot_t *s;
    if (type < 0x81U || type > 0x8AU ||
        remote_status_link(m, now) != REMOTE_RETURN_ONLINE) return 0U;
    s = &m->slot[type - 0x81U];
    return (uint8_t)(s->received && s->frame.header.session == m->latest.session &&
                    now - s->received_ms <= type_timeout[type - 0x81U]);
}

uint8_t remote_status_valid(const remote_status_model_t *m, uint8_t type,
                            uint8_t mask, uint32_t now)
{
    if (!remote_status_fresh(m, type, now)) return 0U;
    return (uint8_t)((m->slot[type - 0x81U].frame.header.valid_flags & mask) == mask &&
                    (m->latest.valid_flags & mask) == mask);
}

uint8_t remote_status_axis(const remote_status_model_t *m, uint8_t axis)
{
    /* 未收到配置时不判定为未选轴，由字段有效性显示未知。 */
    return (uint8_t)(axis < 2U && (!m->received ||
                    (m->latest.axis_flags & (1U << axis)) != 0U));
}

uint8_t remote_status_sample_fresh(const remote_status_model_t *m,
                                   uint8_t type, uint16_t sample_age,
                                   uint32_t now)
{
    uint32_t elapsed;
    if (!remote_status_fresh(m, type, now) || sample_age >= REMOTE_SAMPLE_TIMEOUT_MS)
        return 0U;
    elapsed = now - m->slot[type - 0x81U].received_ms;
    return (uint8_t)(elapsed < REMOTE_SAMPLE_TIMEOUT_MS - sample_age);
}

uint8_t remote_status_alarm_detail(const remote_status_model_t *m, uint32_t now)
{
    return (uint8_t)(m->alarm_matches && remote_status_fresh(m, 0x87U, now) &&
        m->slot[6].frame.data.alarm.level == m->latest.alarm_level);
}
