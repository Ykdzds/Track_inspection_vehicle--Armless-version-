#include "remote_status_protocol.h"
#include <string.h>

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8U | p[1]);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24U | (uint32_t)p[1] << 16U |
           (uint32_t)p[2] << 8U | p[3];
}

/* 不依赖编译器将超范围无符号数转换为有符号数的实现行为。 */
static int16_t signed16(const uint8_t *p)
{
    uint16_t u = get16(p);
    return u <= 32767U ? (int16_t)u : (int16_t)((int32_t)u - 65536L);
}

static int32_t signed32(const uint8_t *p)
{
    uint32_t u = get32(p);
    return u <= 0x7FFFFFFFUL ? (int32_t)u :
           -1L - (int32_t)(0xFFFFFFFFUL - u);
}

uint16_t remote_status_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    size_t i;
    uint8_t bit;
    for (i = 0U; i < length; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (bit = 0U; bit < 8U; bit++)
            crc = (crc & 0x8000U) ?
                  (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
    }
    return crc;
}

remote_parse_result_t remote_status_decode(const uint8_t *p, size_t length,
                                           remote_status_frame_t *out)
{
    uint8_t a;
    remote_status_frame_t f;
    if (p == NULL || length != REMOTE_STATUS_SIZE) return REMOTE_PARSE_LENGTH;
    if (out == NULL || p[0] != 0x5AU || p[1] != 0xA5U || p[2] != 1U ||
        p[3] < 0x81U || p[3] > 0x8AU || p[5] != 22U ||
        (p[3] != 0x89U && p[3] != 0x88U && (p[10] != 0U || p[11] != 0U)) ||
        (p[3] == 0x89U && ((p[10] | p[11]) & 0xF0U)) ||
        (p[7] & 0xC0U) != 0U ||
        p[30] != 13U || p[31] != 10U) return REMOTE_PARSE_FORMAT;
    if (remote_status_crc16(p, 28U) != get16(p + 28U)) return REMOTE_PARSE_CRC;
    if ((p[3] == 0x81U && (p[14] & 0x80U)) ||
        (p[3] == 0x84U && ((p[12] & 0xF0U) || p[14] > 1U)) ||
        (p[3] == 0x85U && (p[27] & 0xE0U)))
        return REMOTE_PARSE_FORMAT;
    memset(&f, 0, sizeof(f));
    f.header.type = p[3]; f.header.sequence = p[4]; f.header.session = p[6];
    f.header.axis_flags = p[7]; f.header.valid_flags = p[8];
    f.header.alarm_level = p[9];
    switch (p[3]) {
    case 0x8AU:
        f.data.metadata.sequence = get32(p + 12U);
        f.data.metadata.sample_ms = get32(p + 16U);
        f.data.metadata.owner = p[20];
        f.data.metadata.version = p[21];
        f.data.metadata.capabilities = p[21] == 2U ? p[22] : 0U;
        f.data.metadata.recovery_state = p[23];
        f.data.metadata.recovery_eligible = p[24];
        f.data.metadata.recovery_failure = p[25];
        f.data.metadata.recovery_healthy_ms = (uint16_t)p[26] | ((uint16_t)p[27] << 8U);
        if (p[20] > 5U || (p[21] != 1U && p[21] != 2U) || f.data.metadata.sequence == 0U)
            return REMOTE_PARSE_FORMAT;
        break;
    case 0x89U:
        for (a = 0U; a < 2U; a++) {
            uint8_t field;
            remote_servo_diagnostic_t *v = &f.data.diagnostic;
            v->stale_mask[a] = p[10U + a];
            for (field = 0U; field < 4U; field++) {
                uint16_t raw = get16(p + 12U + a * 8U + field * 2U);
                uint16_t magnitude = (uint16_t)(raw & 0x7FFFU);
                int16_t signed_value = magnitude < 0x4000U ? (int16_t)magnitude :
                    (int16_t)((int32_t)magnitude - 32768L);
                if (raw & 0x8000U) v->valid_mask[a] |= (uint8_t)(1U << field);
                if (field == 0U) v->torque[a] = signed_value;
                else if (field == 1U) v->load[a] = magnitude;
                else if (field == 2U) v->current[a] = signed_value;
                else v->voltage[a] = magnitude;
                /* 有效字段必须符合手册量程，避免损坏数据被当成真实值显示。 */
                if ((raw & 0x8000U) &&
                    ((field == 0U && (signed_value < -3000 || signed_value > 3000)) ||
                     (field == 1U && magnitude > 5000U) ||
                     (field == 2U && (signed_value < -12000 || signed_value > 12000)) ||
                     (field == 3U && magnitude > 9999U))) return REMOTE_PARSE_FORMAT;
            }
        }
        break;
    case 0x81U:
        f.data.fast.system = p[12]; f.data.fast.mode = p[13];
        f.data.fast.authorization = p[14]; f.data.fast.communication = p[15];
        f.data.fast.target = signed16(p + 16); f.data.fast.base = signed16(p + 18);
        for (a = 0U; a < 2U; a++) {
            f.data.fast.command[a] = signed16(p + 20U + a * 4U);
            f.data.fast.speed[a] = signed16(p + 22U + a * 4U);
        }
        break;
    case 0x82U:
        for (a = 0U; a < 2U; a++) {
            f.data.servo.state[a] = p[12U + a];
            f.data.servo.fault[a] = get16(p + 14U + a * 2U);
            f.data.servo.speed[a] = signed16(p + 18U + a * 2U);
            f.data.servo.age[a] = get16(p + 22U + a * 2U);
            f.data.servo.failures[a] = p[26U + a];
        }
        break;
    case 0x83U:
        f.data.position.raw[0] = signed32(p + 12);
        f.data.position.raw[1] = signed32(p + 16);
        f.data.position.difference_mm = signed16(p + 20);
        f.data.position.net_dm = signed16(p + 22);
        f.data.position.boot_dm = get16(p + 24);
        f.data.position.trip_dm = get16(p + 26);
        break;
    case 0x84U:
        f.data.sync.flags = p[12]; f.data.sync.state = p[13];
        f.data.sync.pair = p[14]; f.data.sync.reject = p[15];
        f.data.sync.difference = signed16(p + 16);
        f.data.sync.error = signed16(p + 18);
        f.data.sync.correction = signed16(p + 20);
        f.data.sync.skew = get16(p + 22);
        f.data.sync.age[0] = get16(p + 24);
        f.data.sync.age[1] = get16(p + 26);
        break;
    case 0x85U:
        f.data.extension.battery = p[12]; f.data.extension.charge = p[13];
        f.data.extension.location = p[14]; f.data.extension.accuracy = p[15];
        f.data.extension.coordinate[0] = signed32(p + 16);
        f.data.extension.coordinate[1] = signed32(p + 20);
        f.data.extension.arm = p[24]; f.data.extension.light = p[25];
        f.data.extension.acquisition = p[26]; f.data.extension.faults = p[27];
        break;
    case 0x86U:
        for (a = 0U; a < 2U; a++) {
            f.data.parameter.counts[a] = get16(p + 12U + a * 2U);
            f.data.parameter.wheel_mm[a] = get16(p + 16U + a * 2U);
            f.data.parameter.direction[a] = p[20U + a];
            f.data.parameter.ratio_milli[a] = get16(p + 22U + a * 2U);
        }
        f.data.parameter.speed_unit = p[26]; f.data.parameter.position_unit = p[27];
        break;
    case 0x87U:
        f.data.alarm.sequence = p[12]; f.data.alarm.level = p[13];
        f.data.alarm.code = p[14]; f.data.alarm.source = p[15];
        f.data.alarm.fault[0] = get16(p + 16);
        f.data.alarm.fault[1] = get16(p + 18);
        f.data.alarm.tick = get32(p + 20); f.data.alarm.detail = get32(p + 24);
        if (p[13] != p[9]) return REMOTE_PARSE_FORMAT;
        break;
    case 0x88U:
        f.data.vehicle_speed.control_extended = (uint8_t)((p[23] & 0xE0U) == 0xA0U);
        if (f.data.vehicle_speed.control_extended) {
            if ((p[23] & 7U) > 4U || ((p[23] & 16U) && !(p[23] & 8U))) return REMOTE_PARSE_FORMAT;
            f.data.vehicle_speed.control_source = (uint8_t)(p[23] & 7U);
            f.data.vehicle_speed.exit_pending = (uint8_t)((p[23] >> 3) & 1U);
            f.data.vehicle_speed.exit_ready = (uint8_t)((p[23] >> 4) & 1U);
            f.data.vehicle_speed.common_centi_kmh = signed16(p + 10U);
        } else if (p[10] || p[11] || p[23]) return REMOTE_PARSE_FORMAT;
        f.data.vehicle_speed.target_mm_s = signed16(p + 12);
        f.data.vehicle_speed.command_mm_s = signed16(p + 14);
        f.data.vehicle_speed.front_mm_s = signed16(p + 16);
        f.data.vehicle_speed.rear_mm_s = signed16(p + 18);
        f.data.vehicle_speed.average_mm_s = signed16(p + 20);
        f.data.vehicle_speed.flags = p[22];
        f.data.vehicle_speed.reserved = p[23];
        f.data.vehicle_speed.front_age_ms = get16(p + 24);
        f.data.vehicle_speed.rear_age_ms = get16(p + 26);
        if ((p[22] & 0xC0U) != 0U || (!f.data.vehicle_speed.control_extended && p[23] != 0U))
            return REMOTE_PARSE_FORMAT;
        break;
    default:
        return REMOTE_PARSE_FORMAT;
    }
    *out = f;
    return REMOTE_PARSE_OK;
}
