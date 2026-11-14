#include "can_link.h"
#include <string.h>

#define LINK_RESULT_UNSUPPORTED 1U
#define LINK_RESULT_FORMAT 2U
#define LINK_RESULT_BUSY 5U
#define LINK_RESULT_LOST_DUPLICATE 6U
#define LINK_RESULT_SESSION 7U
#define LINK_RESULT_TIMEOUT 10U
#define LINK_RESULT_INTERNAL 12U

static bool reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/* Codes preserve the producer branch; no deadline or retry policy changes. */
static void record_failure(can_link_t *link, uint8_t reason, uint32_t now_ms)
{
    if (link->callbacks.record_failure != NULL)
        link->callbacks.record_failure(link->callbacks.context, reason, now_ms);
}

static bool newer8(uint8_t value, uint8_t previous)
{
    uint8_t delta = (uint8_t)(value - previous);
    return delta != 0U && delta < 128U;
}

bool can_link_offer_basic(can_link_t *link, uint16_t id,
                          const uint8_t data[8], uint32_t deadline_ms)
{
    uint32_t i;
    can_link_basic_t *slot = NULL;
    /* 为启动公告、心跳和周期状态保留容量，重复查询不能填满全部槽位。 */
    if (id == 0x120U || id == 0x121U)
    {
        uint32_t response_slots = 0U;
        for (i = 0U; i < CAN_LINK_BASIC_SLOTS; ++i)
            if (link->basic[i].used &&
                (link->basic[i].id == 0x120U || link->basic[i].id == 0x121U))
                ++response_slots;
        bool control_response = data[1] >= 4U && data[1] <= 7U;
        if (response_slots >= (control_response ? CAN_LINK_BASIC_SLOTS - 12U : CAN_LINK_BASIC_SLOTS / 2U))
        {
            ++link->queue_dropped;
            return false;
        }
    }
    for (i = 0U; i < CAN_LINK_BASIC_SLOTS; ++i)
    {
        /* 周期遥测只替换尚未提交的同ID快照，业务应答不可互相覆盖。 */
        if (id != 0x120U && id != 0x121U &&
            link->basic[i].used && !link->basic[i].pending &&
            link->basic[i].id == id)
        {
            slot = &link->basic[i];
            break;
        }
        if (!link->basic[i].used && slot == NULL) slot = &link->basic[i];
    }
    if (slot == NULL)
    {
        ++link->queue_dropped;
        return false;
    }
    memset(slot, 0, sizeof(*slot));
    slot->id = id;
    memcpy(slot->data, data, 8U);
    slot->deadline_ms = deadline_ms;
    slot->used = true;
    return true;
}

bool can_link_request_record(can_link_t *link, uint8_t type)
{
    if (type >= 96U || type == 0x01U) return false;
    link->record_requested[type / 32U] |= 1UL << (type % 32U);
    return true;
}

bool can_link_request_record_priority(can_link_t *link, uint8_t type)
{
    if (!can_link_request_record(link, type)) return false;
    link->priority_type = type;
    link->priority_pending = true;
    link->prefer_record = true;
    return true;
}


static bool arm_motion(const can_protocol_record_t *r)
{return r->type==0x57U || r->type==0x56U || (r->type==0x54U && r->payload[9]!=3U);}
void can_link_cancel_arm_motion(can_link_t *link)
{
 unsigned i;
 for(i=0U;i<CAN_LINK_PAYLOAD_SLOTS+CAN_LINK_URGENT_SLOTS;++i)
  if(link->payload_queue[i].used && arm_motion(&link->payload_queue[i].record))link->payload_queue[i].used=false;
 if(link->record_active && arm_motion(&link->record)){
  if(link->record_pending){link->record_cancelled=true;
    if(link->callbacks.cancel)link->callbacks.cancel(link->callbacks.context,link->record_cookie);
  }else link->record_active=false;
 }
}
void can_link_arm3_profile(can_link_t *link, bool enabled)
{ link->arm3_profile = enabled; }

bool can_link_enqueue_payload(can_link_t *link, const can_protocol_record_t *record,
                              uint8_t priority, uint32_t now_ms, uint32_t deadline_ms)
{
    unsigned i, first = priority == 0U ? CAN_LINK_PAYLOAD_SLOTS : 0U;
    unsigned end = priority == 0U ? CAN_LINK_PAYLOAD_SLOTS + CAN_LINK_URGENT_SLOTS : CAN_LINK_PAYLOAD_SLOTS;
    if (record == NULL || !record->length || record->length > CAN_PROTOCOL_RECORD_MAX ||
        link->phase != CAN_LINK_READY || priority > 3U) return false;
    /* Keep the newest immutable permit snapshot. A stale grant must never
     * follow a revoke already accepted into a higher priority slot. */
    if (record->type==0x53U) {
        for(i=0U;i<CAN_LINK_PAYLOAD_SLOTS+CAN_LINK_URGENT_SLOTS;++i)
            if(link->payload_queue[i].used && link->payload_queue[i].record.type==0x53U)
                link->payload_queue[i].used=false;
    }
    /* Only unsent pulses for the same operation may be coalesced. */
    for (i = first; i < end; ++i) {
        can_link_payload_t *s = &link->payload_queue[i];
        if (s->used && record->type == 0x56U && record->length == 64U &&
            record->payload[9] == 4U && s->record.type == record->type &&
            can_protocol_read_u32(s->record.payload) == can_protocol_read_u32(record->payload)) {
            s->record = *record; s->created_ms = now_ms; s->deadline_ms = deadline_ms; return true;
        }
    }
    for (i = first; i < end; ++i) {
        can_link_payload_t *s = &link->payload_queue[i];
        if (s->used && reached(now_ms, s->deadline_ms)) { s->used = false; ++link->payload_dropped; }
        if (!s->used) { s->record = *record; s->created_ms = now_ms;
            s->deadline_ms = deadline_ms; s->priority = priority; s->used = true; return true; }
    }
    ++link->payload_dropped; return false;
}

/* Restart an interrupted action with the same immutable intent and deadline.
 * A newer queued pulse for that operation already subsumes the old pulse. */
static bool retain_action(can_link_t *link, uint32_t now_ms)
{
    unsigned i;
    can_link_payload_t *slot = NULL;
    for (i=0U;i<CAN_LINK_PAYLOAD_SLOTS;++i) {
        can_link_payload_t *item=&link->payload_queue[i];
        if (item->used && reached(now_ms,item->deadline_ms)) item->used=false;
        if (item->used && item->record.type==link->record.type &&
            can_protocol_read_u32(item->record.payload)==can_protocol_read_u32(link->record.payload)) {
            /* A queued pulse was built later; do not replace it with an old one. */
            return true;
        }
        if (!item->used && !slot) slot=item;
    }
    if (!slot || reached(now_ms,link->record_deadline_ms)) {
        record_failure(link, 1U, now_ms);
        ++link->record_failed; return false;
    }
    slot->record=link->record;slot->created_ms=link->record_created_ms;
    slot->deadline_ms=link->record_deadline_ms;slot->priority=link->record_priority;slot->used=true;
    return true;
}

static int payload_candidate(can_link_t *link, uint32_t now_ms)
{
    unsigned i; int best = -1, first_action = -1;
    for (i = 0U; i < CAN_LINK_PAYLOAD_SLOTS + CAN_LINK_URGENT_SLOTS; ++i) {
        can_link_payload_t *s = &link->payload_queue[i];
        if (s->used && reached(now_ms, s->deadline_ms)) { s->used = false; ++link->payload_dropped; }
        if (s->used && s->priority!=0U && arm_motion(&s->record) &&
            (first_action<0 || can_protocol_read_u32(s->record.payload)<
                can_protocol_read_u32(link->payload_queue[first_action].record.payload))) first_action=(int)i;
        if (s->used && (best < 0 || s->priority < link->payload_queue[best].priority ||
            (s->priority == link->payload_queue[best].priority &&
             ((s->priority==1U && s->record.type==0x53U && link->payload_queue[best].record.type!=0x53U) ||
              (!(s->priority==1U && link->payload_queue[best].record.type==0x53U && s->record.type!=0x53U) &&
               (int32_t)(s->created_ms - link->payload_queue[best].created_ms) < 0))))) best = (int)i;
    }
    /* New operation IDs may not overtake an older, interrupted intent. */
    if (best>=0 && first_action>=0 && link->payload_queue[best].priority!=0U &&
        arm_motion(&link->payload_queue[best].record)) best=first_action;
    return best;
}

static bool timed_arm_record(uint8_t type)
{ return type == 0x53U || type == 0x47U || type == 0x48U || type == 0x55U || type == 0x5FU; }

static void response(can_link_t *link, const uint8_t request[8],
                     uint8_t result, uint8_t phase, uint16_t detail,
                     bool cached, uint32_t now_ms)
{
    uint8_t data[8] = {0U};
    uint8_t mode = 255U, owner = 255U;
    bool control_response = request[1] >= 4U && request[1] <= 7U;
    if ((uint32_t)(now_ms - link->response_started_ms) >= 1000U)
    {
        link->response_started_ms = now_ms;
        link->response_count = link->control_response_count = 0U;
    }
    if ((control_response ? link->control_response_count : link->response_count) >= 40U)
    {
        ++link->queue_dropped;
        return;
    }
    if (control_response) ++link->control_response_count;
    else ++link->response_count;
    if (link->callbacks.describe != NULL)
        link->callbacks.describe(link->callbacks.context, &mode, &owner);
    data[0] = request[0];
    data[1] = request[1];
    data[2] = result;
    data[3] = mode;
    can_protocol_write_u16(data + 4U, link->token);
    data[6] = owner;
    data[7] = (uint8_t)((cached ? 1U : 0U) |
              (phase != 0U ? 2U : 0U) | (result != 0U ? 8U : 0U));
    (void)can_link_offer_basic(link, 0x120U, data, now_ms + 500U);
    if (phase != 0U)
    {
        data[2] = phase;
        data[3] = result;
        can_protocol_write_u16(data + 6U, detail);
        (void)can_link_offer_basic(link, 0x121U, data, now_ms + 500U);
    }
}

static void end_query(can_link_t *link, uint8_t result, uint32_t now_ms)
{
    can_link_request_t *entry;
    if (!link->query_active) return;
    entry = &link->requests[link->query_index];
    entry->result = result;
    entry->phase = result == 0U ? 3U : 4U;
    entry->detail = result == 0U ? 0U : 13U;
    /* 去重结果至少保留至完成后两秒，而不是申请后的两秒。 */
    entry->created_ms = now_ms;
    response(link, entry->request, entry->result, entry->phase,
             entry->detail, false, now_ms);
    if (result == 0U) ++link->requests_completed;
    link->query_active = false;
}

void can_link_control_result(can_link_t *link, uint8_t sequence, uint8_t command,
                             uint8_t result, uint8_t phase, uint16_t detail, uint32_t now_ms)
{
    unsigned i;
    for (i = 0U; i < CAN_LINK_REQUEST_SLOTS; ++i)
    {
        can_link_request_t *r = &link->requests[i];
        if (!r->used || r->request[0] != sequence || r->request[1] != command) continue;
        r->result = result; r->phase = phase; r->detail = detail;
        if (phase >= 3U) { r->created_ms = now_ms; if (result == 0U) ++link->requests_completed; }
        response(link, r->request, result, phase, detail, false, now_ms);
        (void)can_link_request_record(link, 0x11U);
        return;
    }
}

static void invalidate(can_link_t *link, uint32_t now_ms)
{
    uint32_t i;
    /* 会话更换时先取消旧邮箱，收齐结果后才允许新代次提交。 */
    for (i = 0U; i < CAN_LINK_BASIC_SLOTS; ++i)
    {
        if (link->basic[i].pending && link->callbacks.cancel != NULL)
            link->callbacks.cancel(link->callbacks.context, link->basic[i].cookie);
        if (!link->basic[i].pending) link->basic[i].used = false;
    }
    if (link->record_pending && link->callbacks.cancel != NULL)
        link->callbacks.cancel(link->callbacks.context, link->record_cookie);
    link->transport_draining = link->record_pending;
    for (i = 0U; i < CAN_LINK_BASIC_SLOTS; ++i)
        if (link->basic[i].pending) link->transport_draining = true;
    link->token = 0U;
    link->phase = link->lower_boot_id != 0U ?
        CAN_LINK_WAIT_PEER : CAN_LINK_IDENTITY_FAILED;
    link->heartbeat_seen = false;
    link->healthy_count = 0U;
    link->invite_delivered = link->invite_requested = false;
    link->confirm_requested = false;
    link->confirm_delivered = false;
    link->record_active = link->record_pending;
    link->record_cancelled = link->record_pending;
    link->query_active = link->query_record = false;
    link->upper_sequence_seen = false;
    link->request_seen = false;
    link->stop_seen = false;
    link->stop_sequence = 0xFFFFU;
    link->rate_started_ms = now_ms;
    link->rate_count = 0U;
    memset(link->requests, 0, sizeof(link->requests));
    memset(link->record_requested, 0, sizeof(link->record_requested));
    memset(link->payload_queue, 0, sizeof(link->payload_queue));
    link->arm3_profile = false;
    link->priority_pending = link->priority_served = false;
    can_protocol_reassembly_reset(&link->reassembly);
    ++link->generation;
    if (link->callbacks.invalidate_upper != NULL)
        link->callbacks.invalidate_upper(link->callbacks.context);
}

void can_link_init(can_link_t *link, const can_link_callbacks_t *callbacks,
                   uint32_t boot_id, uint16_t first_token, uint16_t token_limit,
                   uint32_t config_revision, uint32_t capabilities,
                   uint32_t heartbeat_timeout_ms, uint32_t now_ms)
{
    memset(link, 0, sizeof(*link));
    link->callbacks = *callbacks;
    link->lower_boot_id = boot_id;
    link->next_token = first_token;
    link->token_limit = token_limit;
    link->config_revision = config_revision;
    link->capabilities = capabilities;
    link->heartbeat_timeout_ms = heartbeat_timeout_ms;
    link->next_boot_ms = now_ms;
    link->stop_sequence = 0xFFFFU;
    invalidate(link, now_ms);
}

void can_link_transport_reset(can_link_t *link, uint32_t now_ms)
{
    invalidate(link, now_ms);
    link->next_boot_ms = now_ms;
}

static bool allocate_invite(can_link_t *link, uint32_t now_ms)
{
    if (link->next_token == 0U || link->next_token == 0xFFFFU ||
        link->next_token > link->token_limit) return false;
    link->token = link->next_token++;
    link->invite_sequence = ++link->record_sequence;
    link->phase = CAN_LINK_WAIT_CONFIRM;
    link->confirmed_ms = now_ms;
    link->invite_created_ms = now_ms;
    link->invite_requested = true;
    return true;
}

static void receive_request(can_link_t *link, const uint8_t *request,
                            uint8_t length, uint32_t now_ms)
{
    uint32_t i;
    uint8_t result = 0U;
    can_link_request_t *slot = NULL;
    if (link->phase != CAN_LINK_READY ||
        can_protocol_read_u16(request + (length == 8U ? 6U : 4U)) != link->token)
    {
        ++link->rejected;
        response(link, request, LINK_RESULT_SESSION, 0U, 0U, false, now_ms);
        return;
    }
    for (i = 0U; i < CAN_LINK_REQUEST_SLOTS; ++i)
    {
        can_link_request_t *entry = &link->requests[i];
        if (entry->used && entry->request[0] == request[0])
        {
            if (entry->length != length ||
                memcmp(entry->request, request, length) != 0)
            {
                ++link->rejected;
                response(link, request, LINK_RESULT_FORMAT, 0U, 14U,
                         false, now_ms);
            }
            else response(link, request, entry->result, entry->phase,
                          entry->detail, true, now_ms);
            return;
        }
        if (slot == NULL && (!entry->used ||
            ((uint32_t)(now_ms - entry->created_ms) >= 2000U &&
             (entry->phase == 0U || entry->phase >= 3U) &&
             (!link->query_active || i != link->query_index)))) slot = entry;
    }
    if (link->request_seen &&
        !newer8(request[0], link->last_request_sequence))
    {
        ++link->rejected;
        response(link, request, LINK_RESULT_LOST_DUPLICATE, 0U, 0U,
                 false, now_ms);
        return;
    }
    if ((uint32_t)(now_ms - link->rate_started_ms) >= 1000U)
    {
        link->rate_started_ms = now_ms;
        link->rate_count = 0U;
    }
    if (slot == NULL || (link->rate_count >= 10U &&
        !(length == 8U && (request[1] == 5U ||
          (request[1] == 4U && can_protocol_read_u16(request + 2U) == 0U)))))
    {
        ++link->rejected;
        response(link, request, LINK_RESULT_BUSY, 0U, 0U, false, now_ms);
        return;
    }
    if (link->rate_count < 255U) ++link->rate_count;
    link->request_seen = true;
    link->last_request_sequence = request[0];
    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    slot->created_ms = now_ms;
    slot->length = length;
    memcpy(slot->request, request, length);
    if (length == 8U && request[1] >= 4U && request[1] <= 7U &&
        link->callbacks.submit_control != NULL)
    {
        result = link->callbacks.submit_control(link->callbacks.context, request, now_ms, link->generation);
        slot->result = result; slot->phase = result == 0U ? 1U : 4U;
        response(link, request, result, slot->phase, 0U, false, now_ms);
        return;
    }
    if (length != 8U || (request[1] != 0x08U && request[1] != 0x0AU))
        result = LINK_RESULT_UNSUPPORTED;
    else if (can_protocol_read_u16(request + 4U) != 0U ||
             (request[1] == 0x0AU &&
              can_protocol_read_u16(request + 2U) != 0U) ||
             (request[1] == 0x08U &&
              can_protocol_read_u16(request + 2U) > 0xFFU))
        result = LINK_RESULT_FORMAT;
    else if (link->query_active) result = LINK_RESULT_BUSY;
    slot->result = result;
    if (result != 0U)
    {
        ++link->rejected;
        slot->detail = result == LINK_RESULT_UNSUPPORTED ? 13U : 0U;
        response(link, request, result, 0U, slot->detail, false, now_ms);
        return;
    }
    slot->phase = 1U;
    link->query_active = true;
    link->query_index = (uint8_t)(slot - link->requests);
    link->query_type = request[1] == 0x0AU ? 0x02U : request[2];
    link->query_remaining = request[1] == 0x0AU ? 2U : 1U;
    link->query_deadline_ms = now_ms + 1000U;
    response(link, request, 0U, 1U, 0U, false, now_ms);
}

static void receive_heartbeat(can_link_t *link, const uint8_t data[8],
                              uint32_t now_ms)
{
    if ((link->phase != CAN_LINK_WAIT_HEARTBEAT &&
         link->phase != CAN_LINK_READY) || data[0] != 1U ||
        (data[3] > 6U && data[3] != 255U) ||
        can_protocol_read_u16(data + 4U) != link->token ||
        (link->heartbeat_seen &&
         !newer8(data[1], link->heartbeat_sequence))) return;
    link->heartbeat_sequence = data[1];
    link->heartbeat_seen = true;
    link->last_heartbeat_ms = now_ms;
    if ((data[2] & 0x41U) == 1U)
    {
        if (link->healthy_count < 3U) ++link->healthy_count;
        if (link->healthy_count == 3U && link->confirm_delivered &&
            link->phase != CAN_LINK_READY)
        {
            link->phase = CAN_LINK_READY;
            (void)can_link_request_record(link, 0x02U);
            (void)can_link_request_record(link, 0x10U);
        }
    }
    else
    {
        link->healthy_count = 0U;
        link->phase = CAN_LINK_WAIT_HEARTBEAT;
        if (link->callbacks.invalidate_upper != NULL)
            link->callbacks.invalidate_upper(link->callbacks.context);
    }
}

void can_link_receive(can_link_t *link, uint16_t id, const uint8_t data[8],
                      uint32_t sample_ms)
{
    uint32_t peer;
    if (id == 0x102U)
    {
        peer = can_protocol_read_u32(data);
        if (peer == 0U || data[4] != 1U || data[5] != 0U) return;
        if (link->upper_boot_id != peer)
        {
            invalidate(link, sample_ms);
            link->upper_boot_id = peer;
        }
    }
    else if (id == 0x104U)
    {
        if (link->lower_boot_id == 0U ||
            can_protocol_read_u32(data) != link->lower_boot_id ||
            link->upper_boot_id == 0U ||
            can_protocol_read_u32(data + 4U) != link->upper_boot_id) return;
        if (link->phase == CAN_LINK_WAIT_PEER)
            (void)allocate_invite(link, sample_ms);
        else if (link->phase == CAN_LINK_WAIT_CONFIRM)
            link->invite_requested = true;
        else link->confirm_requested = true;
    }
    else if (id == 0x105U)
    {
        if (link->token == 0U || !link->invite_delivered ||
            can_protocol_read_u16(data) != link->token ||
            can_protocol_read_u16(data + 2U) != link->invite_sequence ||
            can_protocol_read_u32(data + 4U) != link->upper_boot_id) return;
        link->confirm_requested = true;
        if (link->phase == CAN_LINK_WAIT_CONFIRM)
        {
            link->phase = CAN_LINK_WAIT_HEARTBEAT;
            link->confirmed_ms = sample_ms;
        }
    }
    else if (id == 0x100U) receive_heartbeat(link, data, sample_ms);
    else if (id == 0x110U) receive_request(link, data, 8U, sample_ms);
    else if (id == 0x080U)
    {
        if (link->token != 0U && can_protocol_read_u16(data + 2U) == link->token &&
            link->callbacks.submit_stop != NULL)
        {
            if (!link->callbacks.submit_stop(link->callbacks.context, data, sample_ms)) ++link->rejected;
            else (void)can_link_request_record(link, 0x11U);
            return;
        }
        ++link->rejected;
        if (link->phase == CAN_LINK_READY &&
            can_protocol_read_u16(data + 2U) == link->token &&
            data[4] <= 1U && data[5] <= 3U)
        {
            uint16_t sequence = can_protocol_read_u16(data);
            if (!link->stop_seen || sequence == link->stop_sequence ||
                can_protocol_sequence_newer(sequence, link->stop_sequence))
            {
                link->stop_seen = true;
                link->stop_sequence = sequence;
                (void)can_link_request_record(link, 0x11U);
            }
        }
    }
    else if (id == 0x111U)
    {
        if (link->phase != CAN_LINK_READY || link->callbacks.submit_speed == NULL ||
            !link->callbacks.submit_speed(link->callbacks.context, data, sample_ms)) ++link->rejected;
    }
    else if (id == 0x124U) ++link->rejected;
    else if (id >= 0x310U && id <= 0x312U)
    {
        can_protocol_result_t result;
        if (link->phase != CAN_LINK_READY) return;
        result = can_protocol_receive_record(&link->reassembly, id, data,
                                             link->token, sample_ms);
        if (result == CAN_PROTOCOL_COMPLETE)
        {
            /* 扩展请求和普通请求共用去重域，完整保存内容，不以哈希替代比较。 */
            if (link->reassembly.record.type == 0x30U &&
                link->reassembly.record.length == 32U)
            {
                uint32_t i;
                bool duplicate = false;
                can_protocol_record_t *record = &link->reassembly.record;
                /* 只允许完整同内容重传命中缓存；新请求仍须遵守扩展记录序列域。 */
                for (i = 0U; i < CAN_LINK_REQUEST_SLOTS; ++i)
                {
                    const can_link_request_t *entry = &link->requests[i];
                    if (entry->used && entry->record_seen &&
                        entry->record_sequence == record->sequence &&
                        entry->length == 32U &&
                        memcmp(entry->request, record->payload, 32U) == 0)
                        duplicate = true;
                }
                if (!duplicate && link->upper_sequence_seen &&
                    !can_protocol_sequence_newer(record->sequence, link->upper_sequence))
                {
                    ++link->upper_invalid;
                    return;
                }
                receive_request(link, link->reassembly.record.payload,
                                32U, sample_ms);
                if (!duplicate)
                {
                    link->upper_sequence = record->sequence;
                    link->upper_sequence_seen = true;
                    for (i = 0U; i < CAN_LINK_REQUEST_SLOTS; ++i)
                    {
                        can_link_request_t *entry = &link->requests[i];
                        if (entry->used && entry->length == 32U &&
                            memcmp(entry->request, record->payload, 32U) == 0)
                        {
                            entry->record_sequence = record->sequence;
                            entry->record_seen = true;
                        }
                    }
                }
                return;
            }
            if ((!link->upper_sequence_seen ||
                 can_protocol_sequence_newer(link->reassembly.record.sequence,
                                              link->upper_sequence)) &&
                link->callbacks.publish_upper != NULL &&
                link->callbacks.publish_upper(link->callbacks.context,
                    &link->reassembly.record, sample_ms))
            {
                link->upper_sequence = link->reassembly.record.sequence;
                link->upper_sequence_seen = true;
            }
            else ++link->upper_invalid;
        }
        else if (result == CAN_PROTOCOL_INVALID || result == CAN_PROTOCOL_EXPIRED)
            ++link->upper_invalid;
    }
}

static bool begin_record(can_link_t *link, uint32_t now_ms)
{
    uint32_t i;
    uint8_t type = 0U;
    bool found = false;
    int queued;
    bool chassis_due;
    link->record_has_payload = false;
    queued = payload_candidate(link, now_ms);
    chassis_due=(link->record_requested[0x11U/32U] & ((1UL<<(0x11U%32U))|(1UL<<(0x13U%32U))))!=0U ||
        (link->record_requested[0x25U/32U] & (1UL<<(0x25U%32U)))!=0U;
    if (link->phase == CAN_LINK_READY && queued >= 0 &&
        (link->payload_queue[queued].priority <= 1U || (!chassis_due &&
         !(link->record_requested[0x53U / 32U] & (1UL << (0x53U % 32U)))))) {
        link->record = link->payload_queue[queued].record;
        link->record_built_ms = link->payload_queue[queued].created_ms;
        link->record_created_ms = link->payload_queue[queued].created_ms;
        link->record_deadline_ms = link->payload_queue[queued].deadline_ms;
        link->record_priority = link->payload_queue[queued].priority;
        link->payload_queue[queued].used = false;
        if(link->callbacks.prepare_payload && !link->callbacks.prepare_payload(link->callbacks.context,&link->record,link->record_built_ms,now_ms)){
            record_failure(link, 2U, now_ms);
            if(link->record.type==0x53U)++link->start_budget_failed;
            else ++link->payload_dropped;
            return false;
        }
        /* prepare_payload already accounted queue age in non-permit snapshots.
         * Their 20ms driver uncertainty starts at this immutable encoding;
         * permit53 retains its stricter original construction timestamp. */
        if(link->record.type!=0x53U)link->record_built_ms=now_ms;
        link->record.sequence = ++link->record_sequence;
        link->record.token = link->token;
        link->query_record = false; link->record_has_payload = true;
        goto selected_payload;
    }
    if (link->invite_requested)
    {
        /* 同一boot对重发原邀请，序号和令牌不得重新分配。 */
        memset(&link->record, 0, sizeof(link->record));
        link->record.type = 0x01U;
        link->record.length = 24U;
        link->record.sequence = link->invite_sequence;
        can_protocol_write_u32(link->record.payload, link->lower_boot_id);
        can_protocol_write_u32(link->record.payload + 4U, link->upper_boot_id);
        can_protocol_write_u16(link->record.payload + 8U, link->token);
        link->record.payload[10] = 1U;
        can_protocol_write_u32(link->record.payload + 12U, link->capabilities);
        can_protocol_write_u32(link->record.payload + 16U, link->config_revision);
        can_protocol_write_u32(link->record.payload + 20U, link->invite_created_ms);
        link->invite_requested = false;
        link->query_record = false;
    }
    else
    {
        if (link->phase != CAN_LINK_READY) return false;
        /* Whole-record EDF: reserve bandwidth for consumers before diagnostics. */
        {
            static const uint8_t critical[4] = {0x11U, 0x25U, 0x13U, 0x53U};
            uint32_t oldest = 0U;
            int selected = -1;
            for (i = 0U; i < 4U; ++i)
            {
                uint8_t candidate = critical[i];
                uint32_t age = now_ms - link->critical_completed_ms[i];
                if (!link->arm3_profile && i == 3U && link->last_record_duration_ms > 20U && age < 220U) continue;
                if ((link->record_requested[candidate / 32U] & (1UL << (candidate % 32U))) &&
                    (selected < 0 || age > oldest))
                { selected = (int)i; oldest = age; }
            }
            link->query_record = false;
            if (selected >= 0 && oldest >= (link->last_record_duration_ms > 20U ? 60U : 80U))
            { type = critical[selected]; found = true; }
            else if (now_ms - link->diagnostic_started_ms >= (link->last_record_duration_ms > 20U ? 150U : 0U))
            {
                if (link->query_active)
                { type = link->query_type; link->query_record = true; found = true; }
                else for (i = 0U; i < 96U; ++i)
                {
                    uint32_t index = (link->record_cursor + i) % 96U;
                    if (index == 0x11U || index == 0x25U || index == 0x13U || index == 0x53U) continue;
                    if (link->record_requested[index / 32U] & (1UL << (index % 32U)))
                    { type = (uint8_t)index; link->record_cursor = (uint8_t)((index + 1U) % 96U); found = true; break; }
                }
                if (found) link->diagnostic_started_ms = now_ms;
            }
            if (!found && selected >= 0)
            { type = critical[selected]; found = true; }
            if (found && link->query_active && type == link->query_type) link->query_record = true;
            if (found)
            {
                link->record_requested[type / 32U] &= ~(1UL << (type % 32U));
                if (type == link->priority_type) link->priority_pending = false;
            }
        }
        if (!found) return false;
        memset(&link->record, 0, sizeof(link->record));
        if (link->callbacks.build_record == NULL ||
            !link->callbacks.build_record(link->callbacks.context, type,
                                           &link->record, now_ms))
        {
            if (link->query_record) end_query(link, LINK_RESULT_UNSUPPORTED, now_ms);
            return false;
        }
        if (link->record.length == 0U ||
            link->record.length > CAN_PROTOCOL_RECORD_MAX)
        {
            if (link->query_record) end_query(link, LINK_RESULT_INTERNAL, now_ms);
            return false;
        }
        link->record.type = type;
        link->record.sequence = ++link->record_sequence;
        link->record.token = link->token;
        if (type == 0x11U && link->record.length == 40U &&
            link->callbacks.submit_stop == NULL)
        {
            can_protocol_write_u16(link->record.payload + 32U, link->stop_sequence);
            link->record.payload[34] = 0U;
            link->record.payload[35] = link->stop_sequence == 0xFFFFU ?
                                       255U : LINK_RESULT_UNSUPPORTED;
        }
    }
    link->record_built_ms = now_ms;
selected_payload:
    link->record_start_tx_ms = 0U;
    link->record_last_tx_ms = 0U;
    link->record_active = true;
    link->record_cancelled = false;
    link->record_pending = link->record_started = false;
    link->record_index = 0U;
    link->record_retries = 0U;
    return true;
}

static bool send_record(can_link_t *link, uint32_t now_ms)
{
    uint8_t data[8];
    uint16_t id;
    uint32_t cookie;
    if (link->record_pending) return false;
    if (link->record_active && link->record_has_payload && reached(now_ms,link->record_deadline_ms)) {
        record_failure(link, 3U, now_ms);
        link->record_active=false; ++link->record_failed;
    }
    if (link->record_active && !link->record_started && link->arm3_profile &&
        timed_arm_record(link->record.type) && (uint32_t)(now_ms-link->record_built_ms) > 20U) {
        record_failure(link, 4U, now_ms);
        link->record_active = false; ++link->start_budget_failed;
    }
    if (!link->record_active && !begin_record(link, now_ms)) return false;
    if (!can_protocol_encode_record_frame(&link->record, link->record_index,
                                          &id, data)) return false;
    cookie = ++link->cookie_counter;
    if (link->callbacks.send(link->callbacks.context, id, data, cookie,
        link->record_has_payload ? link->record_deadline_ms :
        link->record_started ? link->record_started_ms + 200U : now_ms + 200U))
    {
        ++link->submitted_count;
        link->record_cookie = cookie;
        link->record_pending = true;
        if (!link->record_started)
        {
            link->record_started = true;
            link->record_started_ms = now_ms;
        }
        return true;
    }
    return false;
}

static uint8_t basic_priority(const can_link_basic_t *slot)
{
    if (slot->id == 0x185U || slot->id == 0x106U) return 0U;
    if (slot->id == 0x186U || slot->id == 0x187U) return 1U;
    if ((slot->id == 0x120U || slot->id == 0x121U) &&
        slot->data[1] >= 4U && slot->data[1] <= 7U) return 1U;
    return 2U;
}

static bool send_basic(can_link_t *link, uint32_t now_ms)
{
    uint32_t i, cookie, selected = CAN_LINK_BASIC_SLOTS;
    uint8_t priority = 255U;
    can_link_basic_t *slot;
    /* Keep round-robin order within each class; diagnostics cannot delay
     * heartbeats or execution results. Extended segments still alternate. */
    for (i = 0U; i < CAN_LINK_BASIC_SLOTS; ++i)
    {
        uint32_t index = (link->basic_cursor + i) % CAN_LINK_BASIC_SLOTS;
        slot = &link->basic[index];
        if (!slot->used || slot->pending) continue;
        if (reached(now_ms, slot->deadline_ms))
        {
            slot->used = false;
            ++link->queue_dropped;
            continue;
        }
        if (basic_priority(slot) < priority)
        {
            selected = index;
            priority = basic_priority(slot);
        }
    }
    if (selected == CAN_LINK_BASIC_SLOTS) return false;
    slot = &link->basic[selected];
    cookie = ++link->cookie_counter;
    if (!link->callbacks.send(link->callbacks.context, slot->id, slot->data,
                              cookie, slot->deadline_ms)) return false;
    ++link->submitted_count;
    slot->cookie = cookie;
    slot->pending = true;
    link->basic_cursor = (uint8_t)((selected + 1U) % CAN_LINK_BASIC_SLOTS);
    return true;
}

void can_link_service(can_link_t *link, uint32_t now_ms)
{
    int urgent = payload_candidate(link, now_ms);
    bool stopping = urgent>=0 && link->payload_queue[urgent].priority==0U;
    bool safety_due = link->arm3_profile &&
        (link->record_requested[0x53U/32U] & (1UL<<(0x53U%32U))) != 0U;
    if (link->arm3_profile && ((urgent >= 0 && (link->payload_queue[urgent].priority == 0U ||
          (link->payload_queue[urgent].priority == 1U && link->payload_queue[urgent].record.type==0x53U && link->record.type!=0x53U))) ||
        (safety_due && link->record.type != 0x53U && !link->record_has_payload)) &&
        link->record_active && !link->record_cancelled &&
        (!link->record_pending || link->callbacks.cancel!=NULL) &&
        !(link->record_has_payload && link->record.type == 0x54U && link->record.payload[9] == 3U)) {
        if (!stopping && link->record_has_payload && arm_motion(&link->record))
            (void)retain_action(link,now_ms);
        if (link->record_pending) {
            if (link->callbacks.cancel != NULL) {
                link->record_cancelled = true;
                link->callbacks.cancel(link->callbacks.context, link->record_cookie);
            }
        } else link->record_active = false;
    }
    uint8_t data[8] = {0U};
    uint32_t i;
    if (link->transport_draining)
    {
        bool pending = link->record_pending;
        for (i = 0U; i < CAN_LINK_BASIC_SLOTS; ++i)
            if (link->basic[i].pending) pending = true;
        if (pending) return;
        link->transport_draining = false;
    }
    if ((link->phase == CAN_LINK_WAIT_HEARTBEAT ||
         link->phase == CAN_LINK_READY) &&
        (int32_t)(now_ms - (link->heartbeat_seen ?
            link->last_heartbeat_ms : link->confirmed_ms)) >=
            (int32_t)link->heartbeat_timeout_ms) invalidate(link, now_ms);
    if (link->reassembly.active &&
        (uint32_t)(now_ms - link->reassembly.started_ms) > 200U)
    {
        link->reassembly.active = false;
        ++link->upper_invalid;
    }
    if (link->record_active && link->record_started &&
        !link->record_cancelled &&
        ((uint32_t)(now_ms - link->record_started_ms) >= 200U ||
         (link->record_has_payload && reached(now_ms,link->record_deadline_ms))))
    {
        record_failure(link, 5U, now_ms);
        link->record_cancelled = true;
        if (link->record_pending && link->callbacks.cancel != NULL)
            link->callbacks.cancel(link->callbacks.context, link->record_cookie);
        link->record_active = link->record_pending;
        ++link->record_failed;
        if (link->query_record) end_query(link, LINK_RESULT_TIMEOUT, now_ms);
    }
    if (link->query_active && reached(now_ms, link->query_deadline_ms))
    {
        if (link->record_active && link->query_record)
        {
            link->record_cancelled = true;
            if (link->record_pending && link->callbacks.cancel != NULL)
                link->callbacks.cancel(link->callbacks.context, link->record_cookie);
            link->record_active = link->record_pending;
        }
        end_query(link, LINK_RESULT_TIMEOUT, now_ms);
    }
    if (link->lower_boot_id != 0U && reached(now_ms, link->next_boot_ms))
    {
        can_protocol_write_u32(data, link->lower_boot_id);
        data[4] = 1U;
        can_protocol_write_u16(data + 6U, 7U);
        (void)can_link_offer_basic(link, 0x101U, data, now_ms + 500U);
        link->next_boot_ms = now_ms + 500U;
        if (link->phase == CAN_LINK_WAIT_CONFIRM) link->invite_requested = true;
    }
    if (link->confirm_requested)
    {
        memset(data, 0, sizeof(data));
        can_protocol_write_u16(data, link->token);
        can_protocol_write_u32(data + 4U, link->lower_boot_id);
        if (can_link_offer_basic(link, 0x106U, data, now_ms + 500U))
            link->confirm_requested = false;
    }
    if (link->callbacks.send == NULL) return;
    /* 基本帧与扩展段交替；单次服务最多提交一个邮箱。 */
    if (link->prefer_record)
    {
        if (send_record(link, now_ms)) link->prefer_record = false;
        else if (send_basic(link, now_ms)) link->prefer_record = true;
    }
    else
    {
        if (send_basic(link, now_ms)) link->prefer_record = true;
        else if (send_record(link, now_ms)) link->prefer_record = false;
    }
}

void can_link_tx_result(can_link_t *link, uint32_t cookie, bool success,
                        uint32_t now_ms)
{
    uint32_t i;
    if (link->record_active && link->record_pending &&
        link->record_cookie == cookie)
    {
        link->record_pending = false;
        if (link->record_cancelled)
        {
            link->record_active = false;
            return;
        }
        if (link->record_has_payload && reached(now_ms,link->record_deadline_ms)) {
            record_failure(link, 6U, now_ms);
            link->record_active=false; ++link->record_failed; return;
        }
        if (!success)
        {
            if (++link->record_retries > 2U)
            {
                record_failure(link, 7U, now_ms);
                link->record_active = false;
                ++link->record_failed;
                if (link->query_record) end_query(link, 9U, now_ms);
            }
            return;
        }
        link->record_last_tx_ms = now_ms;
        if (link->record_index == 0U) {
            link->record_start_tx_ms = now_ms;
            if (link->arm3_profile && timed_arm_record(link->record.type) &&
                (uint32_t)(now_ms - link->record_built_ms) > 20U) {
                record_failure(link, 8U, now_ms);
                link->record_active = false; ++link->start_budget_failed;
                if (!link->record_has_payload) (void)can_link_request_record(link, link->record.type);
                return;
            }
        }
        link->record_retries = 0U;
        if (++link->record_index >
            (uint16_t)((link->record.length + 3U) / 4U + 1U))
        {
            link->record_active = false;
            if(link->callbacks.record_complete) link->callbacks.record_complete(link->callbacks.context,&link->record,now_ms);
            link->last_record_duration_ms = now_ms - link->record_started_ms;
            {
                static const uint8_t critical[4] = {0x11U, 0x25U, 0x13U, 0x53U};
                for (i = 0U; i < 4U; ++i) if (link->record.type == critical[i]) link->critical_completed_ms[i] = now_ms;
            }
            if (link->record.type == 0x01U) link->invite_delivered = true;
            if (link->query_record && link->query_active)
            {
                if (--link->query_remaining == 0U) end_query(link, 0U, now_ms);
                else link->query_type = 0x10U;
            }
        }
        return;
    }
    for (i = 0U; i < CAN_LINK_BASIC_SLOTS; ++i)
    {
        can_link_basic_t *slot = &link->basic[i];
        if (slot->used && slot->pending && slot->cookie == cookie)
        {
            if (success && slot->id == 0x106U && !link->transport_draining)
            {
                link->confirm_delivered = true;
                if (link->healthy_count == 3U &&
                    link->phase == CAN_LINK_WAIT_HEARTBEAT)
                {
                    link->phase = CAN_LINK_READY;
                    (void)can_link_request_record(link, 0x02U);
                    (void)can_link_request_record(link, 0x10U);
                }
            }
            slot->pending = false;
            if (link->transport_draining || success || ++slot->retries > 2U ||
                reached(now_ms, slot->deadline_ms)) slot->used = false;
            return;
        }
    }
    /* 旧代次或已过期事务的迟到结果不得推进新记录。 */
}
