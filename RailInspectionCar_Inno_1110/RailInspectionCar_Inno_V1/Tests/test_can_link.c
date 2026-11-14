#include "can_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static can_link_t link;
static uint16_t sent_id;
static uint8_t sent_data[8];
static uint32_t sent_cookie, cancelled_cookie, sent_count, invalidated;
static bool busy;
static uint8_t failure_reason;
static uint32_t failure_at,failure_cookie;
static uint16_t failure_index;
static void failed_record(void *context,uint8_t reason,uint32_t at)
{(void)context;failure_reason=reason;failure_at=at;failure_cookie=link.record_cookie;failure_index=link.record_index;}


static bool send_frame(void *context, uint16_t id, const uint8_t data[8],
                       uint32_t cookie, uint32_t deadline)
{
    (void)context;
    (void)deadline;
    if (busy) return false;
    busy = true;
    sent_id = id;
    sent_cookie = cookie;
    memcpy(sent_data, data, 8U);
    ++sent_count;
    return true;
}

static void cancel_frame(void *context, uint32_t cookie)
{
    (void)context;
    cancelled_cookie = cookie;
}

static bool build_record(void *context, uint8_t type,
                         can_protocol_record_t *record, uint32_t now)
{
    (void)context;
    (void)now;
    if (type != 2U && type != 0x10U && type != 0x11U && type != 0x24U)
        return false;
    record->length = type == 0x10U ? 80U : type == 0x11U ? 40U : 32U;
    memset(record->payload, type, record->length);
    return true;
}

static void invalidate_upper(void *context)
{
    (void)context;
    ++invalidated;
}

static void init(uint32_t now)
{
    can_link_callbacks_t callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.send = send_frame;
    callbacks.cancel = cancel_frame;
    callbacks.build_record = build_record;
    callbacks.invalidate_upper = invalidate_upper;
    callbacks.record_failure = failed_record;
    busy = false;failure_reason=0U;
    sent_count = invalidated = cancelled_cookie = 0U;
    can_link_init(&link, &callbacks, 123U, 100U, 131U, 1U, 7U, 500U, now);
}

static void step(uint32_t now)
{
    can_link_service(&link, now);
    if (busy)
    {
        busy = false;
        can_link_tx_result(&link, sent_cookie, true, now);
    }
}

static void invite(uint32_t now)
{
    uint8_t data[8] = {0U};
    can_protocol_write_u32(data, 456U);
    data[4] = 1U;
    data[6] = 7U;
    can_link_receive(&link, 0x102U, data, now);
    can_protocol_write_u32(data, 123U);
    can_protocol_write_u32(data + 4U, 456U);
    can_link_receive(&link, 0x104U, data, now);
    assert(link.phase == CAN_LINK_WAIT_CONFIRM && link.token == 100U);
}

static void ready(uint32_t now)
{
    uint32_t i;
    uint8_t data[8] = {0U};
    invite(now);
    for (i = 0U; i < 30U && !link.invite_delivered; ++i) step(now + i);
    assert(link.invite_delivered);
    can_protocol_write_u16(data, link.token);
    can_protocol_write_u16(data + 2U, link.invite_sequence);
    can_protocol_write_u32(data + 4U, 456U);
    can_link_receive(&link, 0x105U, data, now + 30U);
    for (i = 0U; i < 8U && !link.confirm_delivered; ++i) step(now + 30U);
    assert(link.confirm_delivered);
    memset(data, 0, sizeof(data));
    data[0] = data[2] = 1U;
    can_protocol_write_u16(data + 4U, link.token);
    for (i = 0U; i < 3U; ++i)
    {
        data[1] = (uint8_t)(254U + i);
        can_link_receive(&link, 0x100U, data, now + 31U + i);
        if (i < 2U) assert(link.phase != CAN_LINK_READY);
    }
    assert(link.phase == CAN_LINK_READY);
}

static void test_handshake(void)
{
    uint8_t data[8] = {0U};
    uint32_t old_time;
    init(0U);
    ready(0U);
    old_time = link.last_heartbeat_ms;
    data[0] = data[2] = 1U;
    can_protocol_write_u16(data + 4U, link.token);
    can_link_receive(&link, 0x100U, data, 100U);
    assert(link.last_heartbeat_ms == old_time);
    can_protocol_write_u32(data, 123U);
    can_protocol_write_u32(data + 4U, 456U);
    can_link_receive(&link, 0x104U, data, 101U);
    assert(link.token == 100U && link.next_token == 101U);
    step(533U);
    assert(link.phase == CAN_LINK_WAIT_PEER && link.token == 0U);
    assert(invalidated >= 3U);
}

static void test_invitation_replay(void)
{
    uint32_t i, crc, sequence;
    uint8_t data[8];
    init(0U);
    invite(10U);
    for (i = 0U; i < 30U && !link.invite_delivered; ++i) step(11U + i);
    crc = can_protocol_record_crc32(&link.record);
    sequence = link.invite_sequence;
    can_protocol_write_u32(data, 123U);
    can_protocol_write_u32(data + 4U, 456U);
    can_link_receive(&link, 0x104U, data, 100U);
    for (i = 0U; i < 30U; ++i) step(100U + i);
    assert(link.invite_sequence == sequence);
    assert(can_protocol_record_crc32(&link.record) == crc);
}

static void test_requests(void)
{
    uint8_t data[8] = {1U, 3U, 0U};
    uint32_t i, completed;
    init(0U);
    ready(0U);
    can_protocol_write_u16(data + 6U, link.token);
    can_link_receive(&link, 0x110U, data, 40U);
    assert(link.requests[0].result == 1U && !link.query_active);
    data[1] = 0x0AU;
    can_link_receive(&link, 0x110U, data, 41U);
    assert(!link.query_active);
    data[0] = 2U;
    can_link_receive(&link, 0x110U, data, 42U);
    assert(link.query_active && link.requests[1].phase == 1U);
    completed = link.requests_completed;
    for (i = 0U; i < 160U && link.query_active; ++i) step(43U + i);
    assert(!link.query_active && link.requests_completed == completed + 1U);
    can_link_receive(&link, 0x110U, data, 210U);
    assert(!link.query_active);
    data[0] = 3U;
    data[1] = 0x01U;
    for (i = 0U; i < 1000U; ++i)
        can_link_receive(&link, 0x110U, data, 211U);
    assert(link.queue_dropped != 0U);
    memset(data, 0, sizeof(data));
    assert(can_link_offer_basic(&link, 0x185U, data, 400U));
}

static void test_cancel_and_late_result(void)
{
    uint32_t i, cookie, before;
    init(0U);
    invite(0U);
    for (i = 0U; i < 10U; ++i)
    {
        can_link_service(&link, i);
        if (busy && sent_id == 0x300U) break;
        if (busy)
        {
            busy = false;
            can_link_tx_result(&link, sent_cookie, true, i);
        }
    }
    assert(busy && sent_id == 0x300U);
    cookie = sent_cookie;
    before = sent_count;
    can_link_service(&link, 210U);
    assert(cancelled_cookie == cookie && link.record_pending);
    can_link_service(&link, 211U);
    assert(sent_count == before);
    busy = false;
    can_link_tx_result(&link, cookie, false, 212U);
    assert(!link.record_active);
    can_link_transport_reset(&link, 213U);
    can_link_tx_result(&link, cookie, true, 214U);
    assert(!link.invite_delivered && link.token == 0U);
}

static void test_wrap_and_token_exhaustion(void)
{
    uint8_t data[8];
    init(0xFFFFFFD0UL);
    ready(0xFFFFFFD0UL);
    step(0xFFFFFFD0UL + 533U);
    assert(link.phase == CAN_LINK_WAIT_PEER);
    link.next_token = 132U;
    can_protocol_write_u32(data, 123U);
    can_protocol_write_u32(data + 4U, 456U);
    can_link_receive(&link, 0x104U, data, 600U);
    assert(link.token == 0U && link.phase == CAN_LINK_WAIT_PEER);
}

static void receive_extended(can_protocol_record_t *record, uint32_t now)
{
    uint16_t frame, id;
    uint8_t data[8];
    for (frame = 0U; frame < (record->length + 3U) / 4U + 2U; ++frame)
    {
        assert(can_protocol_encode_record_frame(record, frame, &id, data));
        can_link_receive(&link, (uint16_t)(id + 0x10U), data, now + frame);
    }
}

static void test_extended_sequence(void)
{
    can_protocol_record_t record;
    uint32_t rejected, invalid;
    init(0U);
    ready(0U);
    memset(&record, 0, sizeof(record));
    record.type = 0x30U;
    record.length = 32U;
    record.token = link.token;
    record.sequence = 65535U;
    record.payload[0] = 1U;
    record.payload[1] = 0x20U;
    can_protocol_write_u16(record.payload + 4U, link.token);
    receive_extended(&record, 40U);
    assert(link.upper_sequence_seen && link.upper_sequence == 65535U);
    assert(link.requests[0].result == 1U && !link.query_active);
    rejected = link.rejected;
    receive_extended(&record, 60U);
    assert(link.rejected == rejected);
    invalid = link.upper_invalid;
    record.payload[12] = 1U;
    receive_extended(&record, 80U);
    assert(link.upper_invalid == invalid + 1U);
    record.sequence = 0U;
    record.payload[0] = 2U;
    receive_extended(&record, 100U);
    assert(link.upper_sequence == 0U);
    record.sequence = 65534U;
    record.payload[0] = 3U;
    invalid = link.upper_invalid;
    receive_extended(&record, 120U);
    assert(link.upper_invalid == invalid + 1U && link.upper_sequence == 0U);
}

static unsigned controls, speeds, stops;
static uint8_t control_submit(void *context, const uint8_t data[8], uint32_t now, uint32_t generation)
{ (void)context; (void)data; (void)now; assert(generation == link.generation); ++controls; return 0U; }
static bool speed_submit(void *context, const uint8_t data[8], uint32_t now)
{ (void)context; (void)data; (void)now; ++speeds; return true; }
static bool stop_submit(void *context, const uint8_t data[8], uint32_t now)
{ (void)context; (void)data; (void)now; ++stops; return true; }
static void test_control_callbacks(void)
{
    uint8_t data[8] = {1U,4U,1U,0U,0U,0U,0U,0U};
    init(0U); ready(0U); controls = speeds = stops = 0U;
    link.callbacks.submit_control = control_submit; link.callbacks.submit_speed = speed_submit;
    link.callbacks.submit_stop = stop_submit;
    can_protocol_write_u16(data + 6U, link.token);
    can_link_receive(&link, 0x110U, data, 40U);
    can_link_receive(&link, 0x110U, data, 41U); assert(controls == 1U);
    can_link_control_result(&link, 1U,4U,0U,3U,0U,42U);
    assert(link.requests[0].phase == 3U);
    can_link_receive(&link, 0x111U, data, 43U); assert(speeds == 1U);
    memset(data, 0, sizeof(data)); can_protocol_write_u16(data + 2U, link.token);
    link.phase = CAN_LINK_WAIT_HEARTBEAT; can_link_receive(&link, 0x080U, data, 44U);
    assert(stops == 1U);
    puts("PASS CAN control callback routing: async completion, duplicate, speed and unhealthy-session stop");
}
static void test_stop_under_query_flood(void)
{
    uint8_t data[8] = {1U,4U,1U,0U,0U,0U,0U,0U};
    unsigned i;
    init(0U); ready(0U); controls = 0U;
    link.callbacks.submit_control = control_submit;
    can_protocol_write_u16(data + 6U, link.token);
    can_link_receive(&link, 0x110U, data, 40U);
    for (i = 2U; i <= 10U; ++i)
    {
        data[0] = (uint8_t)i; data[1] = 0x08U;
        can_protocol_write_u16(data + 2U, 0x11U);
        can_link_receive(&link, 0x110U, data, 40U + i);
    }
    assert(link.rate_count == 10U && link.requests[0].phase == 1U);
    data[0] = 11U; data[1] = 5U; can_protocol_write_u16(data + 2U, 0U);
    link.response_count = 40U; /* Exhaust diagnostic replies, not control replies. */
    can_link_receive(&link, 0x110U, data, 60U);
    assert(link.control_response_count >= 2U);
    assert(controls == 2U);
    /* 活动事务即使超过缓存年龄也不能被洪泛查询淘汰。 */
    for (i = 12U; i < 80U; ++i)
    {
        data[0] = (uint8_t)i; data[1] = 8U;
        can_protocol_write_u16(data + 2U, 0x11U);
        can_link_receive(&link, 0x110U, data, 3000U + i);
    }
    assert(link.requests[0].used && link.requests[0].request[0] == 1U && link.requests[0].phase == 1U);
    puts("PASS query flood: stop bypasses rate limit; incomplete results cannot be evicted");
}

static void test_control_priority(void)
{
    uint8_t data[8] = {0U};
    unsigned i;
    init(100U);
    link.next_boot_ms = 1000U;
    for (i = 0U; i < CAN_LINK_BASIC_SLOTS / 2U; ++i)
    {
        data[0] = (uint8_t)i; data[1] = 8U;
        assert(can_link_offer_basic(&link, 0x120U, data, 500U));
    }
    assert(!can_link_offer_basic(&link, 0x120U, data, 500U));
    data[1] = 5U; data[2] = 3U;
    assert(can_link_offer_basic(&link, 0x121U, data, 500U));
    assert(can_link_offer_basic(&link, 0x185U, data, 200U));
    step(100U); assert(sent_id == 0x185U);
    step(101U); assert(sent_id == 0x121U && sent_data[1] == 5U);
    step(102U); assert(sent_id == 0x120U && sent_data[1] == 8U);
    puts("PASS query saturation reserves execution-result slots and prioritizes heartbeat/results");
}


static void test_arm3_payload_queue(void)
{
    can_protocol_record_t r; unsigned i;
    init(0U); ready(0U); link.next_boot_ms=10000U;link.last_heartbeat_ms=100U;
    memset(link.basic,0,sizeof(link.basic));memset(link.record_requested,0,sizeof(link.record_requested));
    link.record_active=link.record_pending=false;busy=false;can_link_arm3_profile(&link,true);
    memset(&r,0,sizeof(r));r.type=0x54U;r.length=32U;r.payload[9]=1U;
    for(i=0U;i<8U;++i){can_protocol_write_u32(r.payload,i+1U);assert(can_link_enqueue_payload(&link,&r,2U,100U,400U));}
    assert(!can_link_enqueue_payload(&link,&r,2U,100U,400U));
    r.payload[9]=3U;can_protocol_write_u32(r.payload,20U);
    assert(can_link_enqueue_payload(&link,&r,0U,100U,400U));
    step(100U);assert(link.record.type==0x54U&&link.record.payload[9]==3U);
    assert(can_protocol_read_u32(link.record.payload)==20U);
    /* The caller mutating its buffer cannot alter the in-flight record. */
    r.payload[0]=99U;assert(can_protocol_read_u32(link.record.payload)==20U);
    init(0U);ready(0U);memset(link.basic,0,sizeof(link.basic));
    memset(link.record_requested,0,sizeof(link.record_requested));link.record_active=false;busy=false;
    link.next_boot_ms=10000U;link.last_heartbeat_ms=100U;can_link_arm3_profile(&link,true);
    memset(&r,0,sizeof(r));r.type=0x5FU;r.length=64U;
    assert(can_link_enqueue_payload(&link,&r,2U,100U,400U));
    can_link_service(&link,100U);assert(busy&&sent_id==0x300U);
    busy=false;can_link_tx_result(&link,sent_cookie,true,121U);
    assert(!link.record_active&&link.start_budget_failed==1U);
    /* A stop invalidates queued motion before a later COMMIT can resurrect it. */
    memset(&r,0,sizeof(r));r.type=0x56U;r.length=64U;
    assert(can_link_enqueue_payload(&link,&r,3U,100U,300U));
    can_link_cancel_arm_motion(&link);
    for(unsigned k=0;k<CAN_LINK_PAYLOAD_SLOTS;++k)assert(!link.payload_queue[k].used || link.payload_queue[k].record.type!=0x56U);
    /* Revocation coalesces all queued permits, never followed by an old grant. */
    init(0U);ready(0U);memset(link.basic,0,sizeof(link.basic));memset(link.record_requested,0,sizeof(link.record_requested));
    link.record_active=link.record_pending=false;busy=false;link.next_boot_ms=10000U;link.last_heartbeat_ms=100U;can_link_arm3_profile(&link,true);
    memset(&r,0,sizeof(r));r.type=0x53U;r.length=32U;r.payload[8]=3U;
    assert(can_link_enqueue_payload(&link,&r,1U,100U,300U));r.payload[8]=0U;
    assert(can_link_enqueue_payload(&link,&r,0U,101U,301U));
    {unsigned permits=0U;for(i=0U;i<CAN_LINK_PAYLOAD_SLOTS+CAN_LINK_URGENT_SLOTS;++i)if(link.payload_queue[i].used && link.payload_queue[i].record.type==0x53U){++permits;assert(!link.payload_queue[i].record.payload[8]);}assert(permits==1U);}
    step(101U);assert(link.record.type==0x53U && !link.record.payload[8]);
    /* Optional ARM configuration cannot starve a requested chassis owner record. */
    init(0U);ready(0U);memset(link.basic,0,sizeof(link.basic));memset(link.record_requested,0,sizeof(link.record_requested));
    link.record_active=link.record_pending=false;busy=false;link.next_boot_ms=10000U;link.last_heartbeat_ms=100U;can_link_arm3_profile(&link,true);
    memset(&r,0,sizeof(r));r.type=0x5FU;r.length=64U;assert(can_link_enqueue_payload(&link,&r,3U,100U,300U));
    assert(can_link_request_record(&link,0x11U));step(100U);assert(link.record.type==0x11U);
    /* Periodic permit interrupts a lower-priority record only after TX completion. */
    init(0U);ready(0U);memset(link.basic,0,sizeof(link.basic));memset(link.record_requested,0,sizeof(link.record_requested));
    link.record_active=link.record_pending=false;busy=false;link.next_boot_ms=10000U;link.last_heartbeat_ms=100U;can_link_arm3_profile(&link,true);
    memset(&r,0,sizeof(r));r.type=0x5FU;r.length=144U;assert(can_link_enqueue_payload(&link,&r,3U,100U,300U));step(100U);
    r.type=0x53U;r.length=32U;assert(can_link_enqueue_payload(&link,&r,1U,101U,301U));step(101U);assert(link.record.type==0x53U);
    puts("PASS ARM3 immutable queue, stop/revoke reserve, chassis fairness and real TX_OK start budget");
}

static void arm_ready(void)
{
    init(0U);ready(0U);memset(link.basic,0,sizeof(link.basic));memset(link.record_requested,0,sizeof(link.record_requested));
    link.record_active=link.record_pending=false;busy=false;link.next_boot_ms=10000U;link.last_heartbeat_ms=100U;can_link_arm3_profile(&link,true);
}
static bool arm_motion_type(uint8_t type){return type==0x54U || type==0x56U || type==0x57U;}
static unsigned queued_operation(uint32_t operation)
{
    unsigned i,n=0U;
    for(i=0U;i<CAN_LINK_PAYLOAD_SLOTS;++i)if(link.payload_queue[i].used &&
        can_protocol_read_u32(link.payload_queue[i].record.payload)==operation && arm_motion_type(link.payload_queue[i].record.type))++n;
    return n;
}
static void test_arm3_permit_preserves_action(void)
{
    can_protocol_record_t action,permit;unsigned i;uint32_t old_cookie;
    arm_ready();memset(&action,0,sizeof(action));action.type=0x56U;action.length=64U;action.payload[9]=6U;
    can_protocol_write_u32(action.payload,21U);can_protocol_write_u32(action.payload+24U,100U);action.payload[63]=99U;
    assert(can_link_enqueue_payload(&link,&action,2U,100U,250U));
    can_link_service(&link,100U);assert(busy && link.record_pending);old_cookie=sent_cookie;
    memset(&permit,0,sizeof(permit));permit.type=0x53U;permit.length=32U;permit.payload[8]=3U;
    assert(can_link_enqueue_payload(&link,&permit,1U,101U,301U));can_link_service(&link,101U);
    assert(link.record_pending && link.record_cancelled && cancelled_cookie==old_cookie && queued_operation(21U)==1U);
    for(i=0U;i<CAN_LINK_PAYLOAD_SLOTS;++i)if(link.payload_queue[i].used && link.payload_queue[i].record.type==0x56U){
        assert(link.payload_queue[i].created_ms==100U && link.payload_queue[i].deadline_ms==250U);
        assert(!memcmp(link.payload_queue[i].record.payload,action.payload,64U));
    }
    /* The old cookie must drain before the permit's first START. */
    busy=false;can_link_tx_result(&link,old_cookie,true,102U);assert(!link.record_active);
    step(103U);assert(link.record.type==0x53U);
    for(i=104U;i<116U && (link.record.type!=0x56U || !link.record_active);++i)step(i);
    assert(link.record_active && link.record.type==0x56U && can_protocol_read_u32(link.record.payload)==21U);
    assert(link.record_deadline_ms==250U && link.record_created_ms==100U);
    assert(!memcmp(link.record.payload,action.payload,64U));
    /* Repeated preemption cannot gift a new 200ms window. */
    assert(can_link_enqueue_payload(&link,&permit,1U,120U,320U));step(120U);
    step(250U);assert(!queued_operation(21U));assert(link.record.type!=0x56U || !link.record_active);

    /* A true stop purges the retained action and must never resurrect it. */
    arm_ready();assert(can_link_enqueue_payload(&link,&action,2U,100U,250U));step(100U);
    assert(can_link_enqueue_payload(&link,&permit,1U,101U,301U));step(101U);assert(queued_operation(21U)==1U);
    can_link_cancel_arm_motion(&link);assert(!queued_operation(21U));
    action.type=0x54U;action.length=32U;action.payload[9]=3U;can_protocol_write_u32(action.payload,22U);
    assert(can_link_enqueue_payload(&link,&action,0U,102U,302U));step(102U);
    assert(link.record.type==0x54U && link.record.payload[9]==3U);assert(!queued_operation(21U));

    /* A retained low ID remains ahead of a new higher-priority action/pulse. */
    arm_ready();action.type=0x54U;action.length=32U;action.payload[9]=1U;can_protocol_write_u32(action.payload,21U);
    assert(can_link_enqueue_payload(&link,&action,2U,100U,300U));step(100U);
    assert(can_link_enqueue_payload(&link,&permit,1U,101U,301U));step(101U);
    action.type=0x56U;action.length=64U;action.payload[9]=4U;can_protocol_write_u32(action.payload,23U);
    assert(can_link_enqueue_payload(&link,&action,1U,102U,302U));
    for(i=102U;i<116U && (link.record.type==0x53U || !link.record_active);++i)step(i);
    assert(link.record_active && link.record.type==0x54U && can_protocol_read_u32(link.record.payload)==21U);
    /* A waiting higher-ID pulse cannot hide a newly due permit from preemption. */
    assert(can_link_enqueue_payload(&link,&permit,1U,120U,320U));step(120U);assert(link.record.type==0x53U);
    /* No requeue space is an observable transport failure, never silent loss. */
    arm_ready();action.type=0x56U;action.length=64U;action.payload[9]=6U;can_protocol_write_u32(action.payload,21U);
    assert(can_link_enqueue_payload(&link,&action,2U,100U,300U));step(100U);
    action.type=0x5FU;for(i=0U;i<CAN_LINK_PAYLOAD_SLOTS-1U;++i)assert(can_link_enqueue_payload(&link,&action,3U,101U,301U));
    assert(can_link_enqueue_payload(&link,&permit,1U,101U,301U));step(101U);assert(link.record_failed==1U);
    puts("PASS ARM3 periodic permit preserves immutable action/deadline/order and stop discards it after cookie drain");
}

static void test_failure_scene(void)
{
    uint32_t old;
    init(1000U);link.phase=CAN_LINK_READY;link.token=9U;link.heartbeat_seen=true;
    link.last_heartbeat_ms=1000U;link.next_boot_ms=2000U;
    assert(can_link_request_record(&link,0x11U));can_link_service(&link,1000U);
    assert(link.record_active && link.record_pending && busy);old=sent_cookie;
    can_link_service(&link,1200U);
    assert(failure_reason==5U && failure_at==1200U && failure_cookie==old && !failure_index);
    assert(link.record_failed==1U && link.record_cancelled && cancelled_cookie==old);
    busy=false;can_link_tx_result(&link,old,true,1201U);assert(!link.record_active);
    /* A late result for the retired cookie cannot advance the next record. */
    assert(can_link_request_record(&link,0x11U));can_link_service(&link,1202U);
    assert(link.record_pending && link.record_index==0U);can_link_tx_result(&link,old,true,1203U);
    assert(link.record_index==0U && link.record_pending);
    assert(link.submitted_count==2U);
    puts("PASS record failure callback freezes producer branch/cookie/index before cancel; late TX_OK cannot advance replacement");
}

int main(void)
{
    test_failure_scene();
    test_control_priority();
    test_arm3_payload_queue();
    test_arm3_permit_preserves_action();
    init(0U); ready(0U);
    link.last_heartbeat_ms = 101U; link.heartbeat_seen = true;
    can_link_service(&link, 100U); assert(link.token != 0U);
    can_link_service(&link, 1101U); assert(link.token == 0U);
    init(0U); ready(0U);
    link.last_heartbeat_ms = 0xFFFFFFF0U; link.heartbeat_seen = true;
    can_link_service(&link, 20U); assert(link.token != 0U);
    puts("PASS new heartbeat / older service sample, exact timeout and tick wrap");
    test_handshake();
    test_invitation_replay();
    test_requests();
    test_cancel_and_late_result();
    test_wrap_and_token_exhaustion();
    test_extended_sequence();
    test_control_callbacks();
    test_stop_under_query_flood();
    puts("PASS CAN link handshake, query completion, flood, cancellation and rollover");
    return 0;
}
