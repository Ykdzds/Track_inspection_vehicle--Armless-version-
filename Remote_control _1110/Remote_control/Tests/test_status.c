#include "remote_status_model.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void finish(uint8_t p[32])
{
    uint16_t crc = remote_status_crc16(p, 28U);
    p[28] = (uint8_t)(crc >> 8U); p[29] = (uint8_t)crc;
}

static void make(uint8_t p[32], uint8_t type, uint8_t seq, uint8_t session)
{
    memset(p, 0, 32U);
    p[0] = 0x5AU; p[1] = 0xA5U; p[2] = 1U; p[3] = type;
    p[4] = seq; p[5] = 22U; p[6] = session; p[7] = 7U; p[8] = 0x7FU;
    p[30] = 13U; p[31] = 10U;
    finish(p);
}

static void test_decode(void)
{
    uint8_t p[32], type;
    remote_status_frame_t f;
    make(p, 0x8AU, 1U, 1U);
    p[15] = 7U; p[20] = 5U; p[21] = 1U; finish(p);
    assert(remote_status_decode(p, sizeof(p), &f) == REMOTE_PARSE_OK);
    assert(f.data.metadata.sequence == 7U && f.data.metadata.owner == 5U);
    p[20] = 6U; finish(p);
    assert(remote_status_decode(p, sizeof(p), &f) == REMOTE_PARSE_FORMAT);
    assert(remote_status_crc16((const uint8_t *)"123456789", 9U) == 0x29B1U);
    for (type = 0x81U; type <= 0x89U; type++) {
        make(p, type, 255U, 17U);
        assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_OK);
        assert(f.header.type == type && f.header.sequence == 255U);
    }
    make(p, 0x81U, 1U, 1U);
    p[16] = 0x80U; p[17] = 0U;
    p[18] = 0x7FU; p[19] = 0xFFU;
    p[20] = 0xFFU; p[21] = 0xFFU; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_OK);
    assert(f.data.fast.target == INT16_MIN && f.data.fast.base == INT16_MAX);
    assert(f.data.fast.command[0] == -1 && f.data.fast.speed[0] == 0);
    make(p, 0x83U, 1U, 1U);
    p[12] = 0x80U;
    p[16] = 0x7FU; p[17] = p[18] = p[19] = 0xFFU;
    p[22] = 0x80U; p[24] = p[25] = p[26] = p[27] = 0xFFU; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_OK);
    assert(f.data.position.raw[0] == INT32_MIN && f.data.position.raw[1] == INT32_MAX);
    assert(f.data.position.net_dm == INT16_MIN && f.data.position.boot_dm == 65535U);
    assert(f.data.position.trip_dm == 65535U);
    make(p, 0x87U, 1U, 1U);
    memset(p + 20U, 0xFF, 8U); finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_OK);
    assert(f.data.alarm.tick == UINT32_MAX && f.data.alarm.detail == UINT32_MAX);
    make(p, 0x82U, 1U, 1U);
    p[14] = p[15] = p[16] = p[17] = 0xFFU;
    p[18] = p[20] = 0x7FU; p[19] = p[21] = 0xFFU; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_OK);
    assert(f.data.servo.fault[0] == 65535U && f.data.servo.speed[1] == 32767);
    assert(remote_status_decode(p, 31U, &f) == REMOTE_PARSE_LENGTH);
    assert(remote_status_decode(p, 33U, &f) == REMOTE_PARSE_LENGTH);
    assert(remote_status_decode(NULL, 32U, &f) == REMOTE_PARSE_LENGTH);
    p[16] ^= 1U;
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_CRC);
    p[10] = 1U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    p[10] = 0U; p[3] = 0x8AU; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    make(p, 0x89U, 1U, 1U);
    p[10] = 16U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    p[10] = 1U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_OK);
    /* 合法CRC但超手册量程的有效字段不得进入末次缓存。 */
    p[12] = 0x8BU; p[13] = 0xB9U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    make(p, 0x81U, 1U, 1U);
    p[3] = 0x81U; p[14] = 0x80U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    p[14] = 0U; p[7] = 0x80U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    make(p, 0x84U, 1U, 1U); p[12] = 0x10U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    make(p, 0x87U, 1U, 1U); p[13] = 3U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    make(p, 0x81U, 1U, 1U); p[2] = 2U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    make(p, 0x81U, 1U, 1U); p[5] = 21U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    make(p, 0x81U, 1U, 1U); p[31] = 0U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    make(p, 0x85U, 1U, 1U); p[27] = 0x20U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
    make(p, 0x84U, 1U, 1U); p[14] = 2U; finish(p);
    assert(remote_status_decode(p, 32U, &f) == REMOTE_PARSE_FORMAT);
}

static void test_type_ages(void)
{
    static const uint32_t limits[] = {500U, 500U, 2500U, 500U, 2500U, 15000U};
    remote_status_model_t m;
    uint8_t p[32], type, sequence;
    uint32_t now, limit;
    for (type = 0x81U; type <= 0x86U; type++) {
        remote_status_init(&m);
        make(p, type, 0U, 1U);
        assert(remote_status_accept(&m, p, sizeof(p), 0U));
        limit = limits[type - 0x81U];
        sequence = 0U;
        /* 用其他帧保持全局链路在线，验证每种缓存独立计龄。 */
        for (now = 200U; now < limit; now += 200U) {
            make(p, type == 0x81U ? 0x82U : 0x81U, ++sequence, 1U);
            assert(remote_status_accept(&m, p, sizeof(p), now));
        }
        make(p, type == 0x81U ? 0x82U : 0x81U, ++sequence, 1U);
        assert(remote_status_accept(&m, p, sizeof(p), limit));
        assert(remote_status_fresh(&m, type, limit));
        assert(!remote_status_fresh(&m, type, limit + 1U));
    }
}

static void test_model(void)
{
    remote_status_model_t m;
    uint8_t p[32];
    uint32_t now;
    remote_status_init(&m);
    assert(remote_status_link(&m, 1000U) == REMOTE_RETURN_WAIT);
    make(p, 0x81U, 255U, 1U);
    assert(remote_status_accept(&m, p, 32U, 0U));
    assert(remote_status_link(&m, 300U) == REMOTE_RETURN_ONLINE);
    assert(remote_status_link(&m, 301U) == REMOTE_RETURN_LOST);
    assert(!remote_status_accept(&m, p, 32U, 100U));
    assert(m.stats.duplicates == 1U && m.last_received_ms == 0U);
    make(p, 0x82U, 0U, 1U);
    assert(remote_status_accept(&m, p, 32U, 200U));
    assert(m.stats.sequence_gaps == 0U);
    make(p, 0x84U, 2U, 1U);
    assert(remote_status_accept(&m, p, 32U, 400U));
    assert(m.stats.sequence_gaps == 1U);
    assert(remote_status_fresh(&m, 0x81U, 500U));
    assert(!remote_status_fresh(&m, 0x81U, 501U));
    assert(remote_status_sample_fresh(&m, 0x82U, 199U, 500U));
    assert(!remote_status_sample_fresh(&m, 0x82U, 200U, 500U));
    make(p, 0x81U, 1U, 1U);
    assert(!remote_status_accept(&m, p, 32U, 450U) && m.stats.old_frames == 1U);
    remote_status_tick(&m, 701U); remote_status_tick(&m, 900U);
    assert(m.stats.link_losses == 1U);
    make(p, 0x83U, 200U, 1U);
    assert(!remote_status_accept(&m, p, 32U, 900U));
    assert(m.stats.recovery_candidates == 1U && m.slot[0].received);
    make(p, 0x83U, 201U, 1U);
    assert(remote_status_accept(&m, p, 32U, 910U));
    assert(m.stats.recoveries == 1U && !m.slot[0].received);
    assert(remote_status_valid(&m, 0x83U, 4U, 910U));
    make(p, 0x81U, 202U, 1U); p[8] = 1U; finish(p);
    assert(remote_status_accept(&m, p, 32U, 1000U));
    assert(!remote_status_valid(&m, 0x83U, 4U, 1000U));
    make(p, 0x87U, 203U, 1U); p[9] = p[13] = 3U; p[14] = 2U; finish(p);
    assert(remote_status_accept(&m, p, 32U, 1010U));
    assert(remote_status_alarm_detail(&m, 1010U) && m.stats.alarm_events == 1U);
    p[4]++; finish(p);
    assert(remote_status_accept(&m, p, 32U, 1020U));
    assert(m.stats.alarm_events == 1U);
    make(p, 0x81U, 205U, 1U);
    assert(remote_status_accept(&m, p, 32U, 1030U));
    assert(!remote_status_alarm_detail(&m, 1030U) && m.slot[6].received);
    make(p, 0x81U, 1U, 2U); p[7] = 1U; finish(p);
    assert(remote_status_accept(&m, p, 32U, 1040U));
    assert(m.stats.session_changes == 1U && m.stats.sequence_gaps == 1U);
    assert(!m.slot[2].received && !m.slot[6].received);
    assert(remote_status_axis(&m, 0U) && !remote_status_axis(&m, 1U));
    p[12] ^= 1U;
    assert(!remote_status_accept(&m, p, 32U, 1050U));
    assert(m.stats.crc_errors == 1U && m.last_received_ms == 1040U);
    remote_status_init(&m); now = UINT32_MAX - 100U;
    make(p, 0x81U, 1U, 1U); assert(remote_status_accept(&m, p, 32U, now));
    assert(remote_status_link(&m, 199U) == REMOTE_RETURN_ONLINE);
    assert(remote_status_link(&m, 200U) == REMOTE_RETURN_LOST);
    /* 低频帧年龄不能因快速帧持续到达而被刷新。 */
    remote_status_init(&m);
    make(p, 0x83U, 0U, 1U); assert(remote_status_accept(&m, p, 32U, 0U));
    for (now = 200U; now <= 2600U; now += 200U) {
        make(p, 0x81U, (uint8_t)(now / 200U), 1U);
        assert(remote_status_accept(&m, p, 32U, now));
    }
    assert(!remote_status_fresh(&m, 0x83U, 2600U));
    assert(remote_status_fresh(&m, 0x81U, 2600U));
    remote_status_init(&m);
    make(p, 0x81U, 0U, 1U); assert(remote_status_accept(&m, p, 32U, 0U));
    make(p, 0x81U, 127U, 1U); assert(remote_status_accept(&m, p, 32U, 10U));
    assert(m.stats.sequence_gaps == 126U);
    make(p, 0x81U, 255U, 1U); assert(!remote_status_accept(&m, p, 32U, 20U));
    assert(m.stats.old_frames == 1U && m.last_received_ms == 10U);
}

static void test_recovery_freshness(void)
{
    remote_status_model_t model;
    remote_status_event_t event;
    uint8_t payload[32];
    uint32_t now;
    remote_status_init(&model);
    make(payload, 0x81U, 5U, 1U);
    assert(remote_status_accept(&model, payload, 32U, 0U));
    for (now = 100U; now <= 2000U; now += 100U) {
        remote_status_note_payload(&model, now);
        assert(!remote_status_accept(&model, payload, 32U, now));
    }
    assert(remote_status_link(&model, now) == REMOTE_RETURN_LOST);
    assert(model.stats.accepted == 1U && model.stats.recoveries == 0U);
    assert(model.payloads_received == 20U && model.max_payload_gap_ms == 100U);
    assert(model.event_count == REMOTE_STATUS_EVENT_CAPACITY && model.event_overwrites > 0U);
    assert(remote_status_pop_event(&model, &event) && event.type == 0x81U);
    make(payload, 0x81U, 6U, 1U);
    assert(remote_status_accept(&model, payload, 32U, 2100U));
    assert(model.max_accept_gap_ms == 2100U);
    remote_status_init(&model);
    make(payload, 0x81U, 100U, 1U);
    assert(remote_status_accept(&model, payload, 32U, 100U));
    make(payload, 0x81U, 1U, 1U);
    assert(!remote_status_accept(&model, payload, 32U, 500U));
    assert(!remote_status_accept(&model, payload, 32U, 600U));
    assert(model.stats.recoveries == 0U);
    make(payload, 0x81U, 2U, 1U);
    assert(!remote_status_accept(&model, payload, 32U, 801U));
    assert(model.stats.recovery_candidates == 2U);
    make(payload, 0x81U, 3U, 1U);
    assert(remote_status_accept(&model, payload, 32U, 1101U));
    assert(model.stats.recoveries == 1U);
    make(payload, 0x81U, 3U, 2U);
    assert(remote_status_accept(&model, payload, 32U, 1500U));
    assert(model.stats.session_changes == 1U && !model.recovery_candidate_valid);
    remote_status_init(&model);
    make(payload, 0x81U, 255U, 1U);
    assert(remote_status_accept(&model, payload, 32U, UINT32_MAX - 100U));
    make(payload, 0x81U, 0U, 1U);
    assert(remote_status_accept(&model, payload, 32U, 300U));
    assert(model.stats.recoveries == 1U && model.latest.sequence == 0U);
}

int main(void)
{
    test_decode();
    test_model();
    test_type_ages();
    test_recovery_freshness();
    puts("PASS remote protocol/model: layouts, CRC, invalid, session, sequence, timeouts");
    return 0;
}
