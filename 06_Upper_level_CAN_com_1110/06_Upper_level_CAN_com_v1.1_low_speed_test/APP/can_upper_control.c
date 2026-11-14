#include "can_upper_control.h"
#include <string.h>

#if UPPER_USART1_DEBUG_ENABLE
static UpperStopFirst_t stop_first;
typedef char StopFirstBudget[(sizeof(stop_first) <= 80U) ? 1 : -1];
const UpperStopFirst_t *UpperControl_StopFirst(void) { return &stop_first; }
void UpperControl_StopFirstSent(uint8_t page)
{
    if (!stop_first.pending || page != stop_first.page) return;
    if (++stop_first.page >= 3U) {
        stop_first.pending = 0U;
        if(stop_first.armed==2U)stop_first.armed=1U;
    }
}
static void StopFirstArm(void)
{
    /* A new grant cannot erase an unprinted decision. */
    if (!stop_first.pending) { memset(&stop_first, 0, sizeof(stop_first)); stop_first.armed = 1U; }
    else stop_first.armed=2U; /* Arm the new authorization after old output completes. */
}
void UpperControl_CaptureStop(UpperControl_t *control, const UpperProtocol_t *protocol,
    UpperStopSource_t source, uint16_t detail, uint8_t key_event, uint8_t key_mask,
    uint32_t event_at, uint32_t now)
{
    const UpperSnapshot_t *s = &protocol->snapshot;
    const UpperRecord_t *r = UpperControl_Record(s, 0x11U);
    unsigned axis;
    if (stop_first.armed!=1U) { if (stop_first.merged != 0xFFFFFFFFUL) ++stop_first.merged; return; }
    stop_first.at = now; stop_first.event_at = event_at;
    stop_first.merged=0U;
    stop_first.lower_boot = s->lower_boot_id; stop_first.upper_boot = s->upper_boot_id;
    stop_first.control_age = r ? now-r->timestamp : 0xFFFFFFFFUL;
    stop_first.heartbeat_age = s->heartbeat_seen ? now-s->heartbeat_at : 0xFFFFFFFFUL;
    stop_first.flags = r ? Upper_ReadU32(r->payload+12U) : 0U;
    stop_first.control_generation = r ? Upper_ReadU32(r->payload) : 0U;
    stop_first.link_token = s->link_token; stop_first.motion_token = control->motion_token;
    stop_first.control_sequence = r ? r->sequence : 0U;
    stop_first.speed_sequence = control->speed_sequence; stop_first.detail = detail;
    stop_first.stop_request=source==UPPER_STOP_AUTO_UNSAFE ? 0xFFFFU :
        source==UPPER_STOP_UI_QUICK ? (uint16_t)(control->stop_sequence+1U) :
        (uint16_t)(uint8_t)(control->request_sequence+1U);
    stop_first.target_rpm = control->target_rpm; stop_first.target_mm_s = control->target_mm_s;
    stop_first.source = (uint8_t)source;
    stop_first.recovery = source==UPPER_STOP_AUTO_CONTROL ? 7U : source==UPPER_STOP_AUTO_AXIS ? 8U : source==UPPER_STOP_AUTO_UNSAFE ? 9U : 0U;
    stop_first.state = (uint8_t)control->state; stop_first.owner = s->control_source;
    stop_first.life = s->lifecycle; stop_first.axes = s->axes;
    stop_first.key_event = key_event; stop_first.key_mask = key_mask;
    stop_first.validity = (uint8_t)((r && r->valid ? 1U : 0U) |
        (r && Upper_RecordFresh(s,r,now) && (uint32_t)(now-r->timestamp)<UPPER_CONTROL_MAX_AGE_MS ? 2U : 0U) |
        (s->heartbeat_seen ? 64U : 0U));
    for (axis=0U;axis<2U;++axis) {
        const UpperBasic_t *b=&s->basic[6U+axis];
        stop_first.axis_age[axis]=b->valid ? now-b->timestamp : 0xFFFFFFFFUL;
        stop_first.axis_flags[axis]=b->data[0]; stop_first.axis_mode[axis]=b->data[1];
        stop_first.axis_fault[axis]=Upper_ReadU16(b->data+4U);
        if(b->valid)stop_first.validity|=(uint8_t)(4U<<axis);
        if(Upper_BasicFresh(s,(uint8_t)(6U+axis),now))stop_first.validity|=(uint8_t)(16U<<axis);
    }
    stop_first.page=0U; stop_first.armed=0U;
    stop_first.pending=1U; /* Publish after the immutable payload is complete. */
}
#endif

#if UPPER_CAN_MOTION_ENABLED
static void Put16(uint8_t *data, uint16_t value) { data[0] = (uint8_t)value; data[1] = (uint8_t)(value >> 8); }

#endif
const UpperRecord_t *UpperControl_Record(const UpperSnapshot_t *snapshot, uint8_t type)
{
    unsigned index;
    for (index = 0U; index < UPPER_RECORD_COUNT; ++index)
        if (snapshot->records[index].valid && snapshot->records[index].type == type) return &snapshot->records[index];
    return 0;
}

#if UPPER_CAN_MOTION_ENABLED
static uint8_t Fresh(const UpperSnapshot_t *snapshot, const UpperRecord_t *record, uint32_t now)
{
    return (uint8_t)(record != 0 && Upper_RecordFresh(snapshot, record, now) && (uint32_t)(now - record->timestamp) < UPPER_CONTROL_MAX_AGE_MS);
}
static uint8_t AxesSafe(const UpperSnapshot_t *snapshot, uint32_t now, uint8_t require_zero, uint8_t *enabled)
{
    unsigned axis;
    *enabled = 0U;
    if (snapshot->axes == 0U || snapshot->axes > 3U) return 0U;
    for (axis = 0U; axis < 2U; ++axis)
    {
        const uint8_t *data = snapshot->basic[6U + axis].data;
        if (!(snapshot->axes & (1U << axis))) continue;
        if (!Upper_BasicFresh(snapshot, (uint8_t)(6U + axis), now) || (data[0] & 31U) != 31U ||
            (data[0] & 32U) || data[1] != 2U || Upper_ReadU16(data + 4U) != 0U ||
            (require_zero && Upper_ReadU16(data + 2U) != 0U)) return 0U;
        *enabled |= (uint8_t)(1U << axis);
    }
    return 1U;
}

/* Observability only: this does not replace or weaken any release predicate. */
static void CaptureStopVerify(UpperControl_t *control, const UpperSnapshot_t *snapshot, uint32_t now)
{
    const UpperRecord_t *r = UpperControl_Record(snapshot,0x11U);
    const UpperRecord_t *s = UpperControl_Record(snapshot,0x13U);
    const UpperRecord_t *d = UpperControl_Record(snapshot,0x25U);
    uint32_t mask = 0U, flags = r ? Upper_ReadU32(r->payload+12U) : 0U;
    uint8_t enabled = 0U;
    unsigned axis;
#if UPPER_USART1_DEBUG_ENABLE
    if (control->stop_failure_state) return;
#endif
    if (control->state == SIM_FAULT) return; /* Preserve the pre-timeout scene. */
    if (!Fresh(snapshot,r,now)) mask |= 1U;
    if (!Fresh(snapshot,s,now)) mask |= 2U;
    if (!Upper_BasicFresh(snapshot,5U,now)) mask |= 4U;
    if (snapshot->control_source || snapshot->lifecycle != 1U ||
        (r && (r->payload[6] || r->payload[7] != 1U))) mask |= 8U;
    if (r && Upper_ReadU16(r->payload+4U)) mask |= 16U;
    if (flags & (8U|512U)) mask |= 32U;
    if (!s || !Upper_ReadU32(s->payload) ||
        (control->stop_event && Upper_ReadU32(s->payload) != control->stop_event)) mask |= 64U;
    if (!r || !s || r->payload[34] != 4U || s->payload[5] != 4U) mask |= 128U;
    if (r && s && (r->payload[35] || s->payload[7])) mask |= 256U;
    if (!s || !control->axes || snapshot->axes != control->axes ||
        (s->payload[6] & control->axes) != control->axes) mask |= 512U;
    if (!AxesSafe(snapshot,now,1U,&enabled) || (enabled & control->axes) != control->axes) mask |= 1024U;
    if (control->state == SIM_CROSS_SOURCE_STOP || control->state == SIM_RECOVERY_VERIFY || control->quick_stop) {
        if (control->quick_stop ? !control->stop_event : !control->cross_associated) mask |= 2048U;
        if (control->cross_zero_observations < 2U) mask |= 4096U;
    }
    if (snapshot->state != UPPER_READY || snapshot->link_token != control->link_token ||
        snapshot->lower_boot_id != control->lower_boot || snapshot->upper_boot_id != control->upper_boot ||
        !snapshot->heartbeat_seen || (uint32_t)(now-snapshot->heartbeat_at) >= UPPER_LINK_TIMEOUT_MS) mask |= 8192U;
    control->stop_verify.at = now; control->stop_verify.mask = mask;
    control->stop_verify.flags = flags;
    control->stop_verify.event = s ? Upper_ReadU32(s->payload) : 0U;
    control->stop_verify.control_age = r ? now-r->timestamp : 0xFFFFFFFFUL;
    control->stop_verify.stop_age = s ? now-s->timestamp : 0xFFFFFFFFUL;
    for (axis=0U;axis<2U;++axis) control->stop_verify.axis_age[axis] =
        snapshot->basic[6U+axis].valid ? now-snapshot->basic[6U+axis].timestamp : 0xFFFFFFFFUL;
    control->stop_verify.diagnostic = d && Upper_RecordFresh(snapshot,d,now) ? Upper_ReadU32(d->payload+84U) : 0U;
    control->stop_verify.token = r ? Upper_ReadU16(r->payload+4U) : 0U;
    control->stop_verify.owner = snapshot->control_source; control->stop_verify.life = snapshot->lifecycle;
    control->stop_verify.phase = s ? s->payload[5] : 255U;
    control->stop_verify.result = r ? r->payload[35] : 255U;
}

/* Reuse the existing verify scene for identity failure. Bit31 identifies this
 * layout: age slots hold expected/observed boots, diagnostic holds both tokens. */
static void CaptureLinkFailure(UpperControl_t *control, const UpperSnapshot_t *snapshot, uint32_t now)
{
#if UPPER_USART1_DEBUG_ENABLE
    uint32_t cause=0U;
    if(control->stop_failure_state)return;
    CaptureStopVerify(control,snapshot,now);
    if(snapshot->state!=UPPER_READY)cause|=1U;
    if(snapshot->link_token!=control->link_token)cause|=2U;
    if(snapshot->lower_boot_id!=control->lower_boot)cause|=4U;
    if(snapshot->upper_boot_id!=control->upper_boot)cause|=8U;
    if(!snapshot->heartbeat_seen || (uint32_t)(now-snapshot->heartbeat_at)>=UPPER_LINK_TIMEOUT_MS)cause|=16U;
    control->stop_verify.at=now;
    control->stop_verify.mask|=0x80000000UL|(cause<<16U);
    control->stop_verify.control_age=control->lower_boot;
    control->stop_verify.stop_age=snapshot->lower_boot_id;
    control->stop_verify.axis_age[0]=control->upper_boot;
    control->stop_verify.axis_age[1]=snapshot->upper_boot_id;
    control->stop_verify.diagnostic=((uint32_t)control->link_token<<16U)|snapshot->link_token;
    control->stop_verify.token=control->stop_sequence;
#else
    (void)control;(void)snapshot;(void)now;
#endif
}

static void ResetStopVerify(UpperControl_t *control)
{
#if UPPER_USART1_DEBUG_ENABLE
    if(control->stop_failure_state==1U)return;
    control->stop_failure_state=control->stop_failure_detail=0U;
#endif
    memset(&control->stop_verify,0,sizeof(control->stop_verify));
}

#endif
uint8_t UpperControl_Convert(const UpperSnapshot_t *snapshot, int16_t rpm, int16_t *mm_s)
{
    const UpperRecord_t *record = UpperControl_Record(snapshot, 0x10U);
    double selected = 0.0, maximum = 0.0;
    unsigned axis, selected_axis = snapshot->axes == 2U ? 1U : 0U;
    uint16_t limit;
    if (record == 0 || record->length != 80U || snapshot->axes == 0U || snapshot->axes > 3U ||
        rpm > UPPER_LOW_SPEED_MAX_RPM || rpm < -UPPER_LOW_SPEED_MAX_RPM) return 0U;
    limit = Upper_ReadU16(record->payload + 34U);
    if (limit == 0U || limit == 65535U || !(Upper_ReadU16(record->payload + 78U) & 512U)) return 0U;
    if (rpm < 0 && !(Upper_ReadU16(record->payload + 78U) & 256U)) return 0U;
    for (axis = 0U; axis < 2U; ++axis)
    {
        uint32_t diameter = Upper_ReadU32(record->payload + 8U + axis * 4U);
        uint16_t numerator = Upper_ReadU16(record->payload + 24U + axis * 4U);
        uint16_t denominator = Upper_ReadU16(record->payload + 26U + axis * 4U);
        double value;
        if (!(snapshot->axes & (1U << axis))) continue;
        if (diameter == 0U || diameter == 0xFFFFFFFFUL || numerator == 0U || numerator == 65535U || denominator == 0U || denominator == 65535U) return 0U;
        value = (double)rpm * 3.14159265358979323846 * diameter * denominator / (60000.0 * numerator);
        if (axis == selected_axis) selected = value;
        if (rpm != 0)
        {
            double axis_limit = (double)UPPER_LOW_SPEED_MAX_RPM * 3.14159265358979323846 * diameter * denominator / (60000.0 * numerator);
            if (maximum == 0.0 || axis_limit < maximum) maximum = axis_limit;
        }
    }
    if (selected > limit || selected < -(double)limit || selected > 32766.0 || selected < -32766.0) return 0U;
    if (rpm != 0 && (selected > maximum + 0.5 || selected < -maximum - 0.5)) return 0U;
    *mm_s = (int16_t)(selected >= 0.0 ? selected + 0.5 : selected - 0.5);
    return 1U;
}

static void Block(UpperAuthGate_t *gate, uint16_t reason)
{
    gate->mask |= 1UL << (reason - 201U);
    if (!gate->reason) gate->reason = reason;
}

void UpperControl_CheckAuthorization(const UpperProtocol_t *protocol, uint32_t now, uint32_t after, uint8_t require_new, UpperAuthGate_t *gate)
{
    const UpperSnapshot_t *snapshot = &protocol->snapshot;
    const UpperRecord_t *record = UpperControl_Record(snapshot, 0x11U);
    const UpperRecord_t *config = UpperControl_Record(snapshot, 0x10U);
    uint32_t flags = record && record->length == 40U ? Upper_ReadU32(record->payload + 12U) : 0U;
    uint8_t control_fresh = (uint8_t)(record && record->length == 40U && Fresh(snapshot, record, now));
    unsigned axis;
    int16_t converted;
    memset(gate, 0, sizeof(*gate));
    gate->owner = snapshot->control_source; gate->life = snapshot->lifecycle;
    gate->gates = snapshot->gates; gate->axes = snapshot->axes;
    gate->control_age = record ? now - record->timestamp : 0xFFFFFFFFUL;
    gate->heartbeat_age = snapshot->heartbeat_seen ? now - snapshot->heartbeat_at : 0xFFFFFFFFUL;
    gate->capability_sent = protocol->capability_published;
    gate->capability_confirmed = (uint8_t)(control_fresh && (flags & 1024U) != 0U);
    if (snapshot->state != UPPER_READY || !snapshot->link_token || !snapshot->heartbeat_seen ||
        !Upper_BasicFresh(snapshot, 5U, now) || gate->heartbeat_age >= UPPER_LINK_TIMEOUT_MS) Block(gate, 201U);
    if (!gate->capability_sent) Block(gate, 202U);
    if (!control_fresh || (require_new && (int32_t)(record->timestamp - after) <= 0)) Block(gate, 204U);
    if (!gate->capability_confirmed) Block(gate, 203U);
    if (snapshot->control_source != 0U || snapshot->lifecycle != 1U ||
        (control_fresh && (record->payload[6] != 0U || record->payload[7] != 1U ||
         (flags & (8U | 32U | 64U | 128U | 256U))))) Block(gate, 205U);
    if ((snapshot->gates & 31U) != 31U || (control_fresh && (record->payload[11] & 31U) != 31U)) Block(gate, 206U);
    if (control_fresh && (flags & 512U)) Block(gate, 207U);
    for (axis = 0U; axis < 2U; ++axis)
    {
        const UpperBasic_t *basic = &snapshot->basic[6U + axis];
        UpperAxisGate_t *check = &gate->axis[axis];
        check->age = basic->valid ? now - basic->timestamp : 0xFFFFFFFFUL;
        check->fresh = Upper_BasicFresh(snapshot, (uint8_t)(6U + axis), now);
        check->flags = basic->data[0]; check->mode = basic->data[1];
        check->rpm = (int16_t)Upper_ReadU16(basic->data + 2U); check->fault = Upper_ReadU16(basic->data + 4U);
        if (snapshot->axes == 0U || snapshot->axes > 3U || ((snapshot->axes & (1U << axis)) &&
            (!check->fresh || (check->flags & 159U) != 159U || (check->flags & 32U) ||
             check->mode != 2U || check->rpm != 0 || check->fault != 0U))) Block(gate, (uint16_t)(208U + axis));
    }
    if (!config || config->length != 80U || Upper_ReadU32(config->payload) != snapshot->config_revision) Block(gate, 210U);
    else if (config->payload[4] != snapshot->axes || !UpperControl_Convert(snapshot, 0, &converted)) Block(gate, 211U);
}

const char *UpperControl_DetailName(uint16_t detail)
{
    static const char *names[] = {"LINK/HB", "CAP TX", "CAP ACK", "CTRL AGE", "OWNER/LIFE", "GATES", "SAFETY LOCK", "LEFT AXIS", "RIGHT AXIS", "CONFIG WAIT", "CONFIG BAD", "PREP TIMEOUT"};
    if (!detail) return "OK";
    if (detail == 110U) return "TX FINAL";
    if (detail == 115U) return "STOP VERIFY";
    if (detail == 116U) return "FEEDBACK WAIT";
    return detail >= 201U && detail <= 212U ? names[detail - 201U] : "RESULT";
}

static void Fail(UpperControl_t *control, uint16_t detail)
{
#if UPPER_USART1_DEBUG_ENABLE
    if(!control->stop_failure_state && control->stop_verify.at &&
       (detail==111U || detail==114U || detail==115U || detail==113U)) {
        control->stop_failure_detail=(uint8_t)detail;
        control->stop_failure_sequence=control->quick_stop ? control->stop_sequence : control->request_sequence;
        control->stop_failure_state=1U; /* Publish immutable scene before state clear. */
    }
#endif
    control->state = SIM_FAULT;
    control->detail = detail;
    control->operation_detail = 0U;
    control->target_rpm = control->target_mm_s = 0;
    control->motion_token = 0U;
    control->request_active = 0U;
    control->tx_retry_pending = 0U;
}
void UpperControl_Init(UpperControl_t *control, UpperSend_t send, void *user)
{
    memset(control, 0, sizeof(*control));
    control->send = send; control->user = user;
    control->state = SIM_LINK_NOT_READY;
    control->stop_result = 255U;
#if UPPER_USART1_DEBUG_ENABLE
    memset(&stop_first,0,sizeof(stop_first)); stop_first.armed=1U;
#endif
}

#if UPPER_CAN_MOTION_ENABLED
static void StartCommand(UpperControl_t *control, uint8_t command, int16_t parameter, uint32_t now)
{
    ++control->request_sequence;
    control->command = command; control->request_parameter = parameter;
    control->accepted = control->completed = control->attempts = 0U;
    control->request_active = 1U;
    control->request_at = control->retry_at = now;
}
static uint8_t SendCommand(UpperControl_t *control, uint32_t now)
{
    uint8_t data[8] = {0};
    data[0] = control->request_sequence; data[1] = control->command;
    Put16(data + 2U, (uint16_t)control->request_parameter);
    Put16(data + 6U, control->link_token);
    ++control->attempts; control->retry_at = now + 300U;
    if (control->send == 0 || !control->send(control->user, 0x110U, data)) return 0U;
    return 1U;
}

static void EndPreparation(UpperControl_t *control, uint16_t detail)
{
    control->state = SIM_IDLE; control->detail = detail;
    control->request_active = 0U; control->motion_token = 0U;
    control->target_rpm = control->target_mm_s = 0;
}

static uint16_t FatalPreparation(const UpperControl_t *control, const UpperSnapshot_t *snapshot, uint32_t now)
{
    unsigned axis;
    const UpperRecord_t *record = UpperControl_Record(snapshot, 0x11U);
    if (snapshot->state != UPPER_READY || snapshot->link_token != control->link_token ||
        snapshot->lower_boot_id != control->lower_boot || snapshot->upper_boot_id != control->upper_boot ||
        !Upper_BasicFresh(snapshot, 5U, now)) return 201U;
    if (Fresh(snapshot, record, now) && (Upper_ReadU32(record->payload + 12U) & 512U)) return 207U;
    if (Upper_BasicFresh(snapshot, 5U, now) && (snapshot->control_source != 0U || snapshot->lifecycle != 1U)) return 205U;
    if (Fresh(snapshot, record, now) && (control->gate.mask & (1UL << 4U))) return 205U;
    for (axis = 0U; axis < 2U; ++axis)
    {
        const UpperAxisGate_t *check = &control->gate.axis[axis];
        if (!(snapshot->axes & (1U << axis)) || !check->fresh) continue;
        if (((check->flags & 4U) && check->rpm != 0) || ((check->flags & 8U) && check->fault != 0U) ||
            ((check->flags & 9U) == 9U && !(check->flags & 2U)) || (check->flags & 32U)) return (uint16_t)(208U + axis);
    }
    return (control->gate.mask & (1UL << 10U)) ? 211U : 0U;
}

static void Prepare(UpperControl_t *control, UpperProtocol_t *protocol, uint32_t now)
{
    const UpperSnapshot_t *snapshot = &protocol->snapshot;
    const UpperRecord_t *stop = UpperControl_Record(snapshot, 0x13U);
    uint16_t fatal;
    UpperControl_CheckAuthorization(protocol, now, control->prepare_at, 1U, &control->gate);
    control->detail = control->gate.reason;
    control->prepare_last_reason = control->gate.reason;
    fatal = FatalPreparation(control, snapshot, now);
    if (fatal) { EndPreparation(control, fatal); return; }
    if ((uint32_t)(now - control->prepare_at) >= UPPER_AUTH_PREPARE_TIMEOUT_MS)
    { EndPreparation(control, 212U); return; }
    if (control->request_active && control->command == 8U)
    {
        if (control->accepted && control->completed) control->request_active = 0U;
        else if ((uint32_t)(now - control->request_at) >= UPPER_CONFIG_QUERY_TIMEOUT_MS)
        { control->request_active = 0U; control->query_detail = 1000U; }
        else
        {
            if (control->attempts < 4U && (int32_t)(now - control->retry_at) >= 0) (void)SendCommand(control, now);
            return;
        }
    }
    if ((control->gate.mask & (1UL << 9U)) && control->query_attempts < UPPER_CONFIG_QUERY_ATTEMPTS)
    {
        ++control->query_attempts;
        StartCommand(control, 8U, 16, now);
        (void)SendCommand(control, now);
        return;
    }
    if (control->gate.mask) return;
    control->axes = snapshot->axes;
    control->zero_observations = control->zero_sent = control->tx_seen = 0U;
    control->last_zero_record = control->last_control_record = 0U;
    control->stop_baseline = stop ? Upper_ReadU32(stop->payload) : 0U;
    control->stop_event = 0U; control->stop_phase = 0U; control->stop_result = 255U;
    control->detail = 0U;
    StartCommand(control, 4U, 1, now); control->state = SIM_AUTH_REQUESTED;
}

static void BeginPreparation(UpperControl_t *control, UpperProtocol_t *protocol, uint32_t now)
{
    const UpperSnapshot_t *snapshot = &protocol->snapshot;
#if UPPER_USART1_DEBUG_ENABLE
    StopFirstArm();
#endif
    ResetStopVerify(control);
    control->link_token = snapshot->link_token; control->lower_boot = snapshot->lower_boot_id; control->upper_boot = snapshot->upper_boot_id;
    control->prepare_at = now; control->motion_token = 0U; control->request_active = 0U;
    control->query_attempts = control->query_result = control->query_phase = 0U; control->query_detail = 0U;
    control->target_rpm = control->target_mm_s = 0;
    control->state = SIM_AUTH_PREPARING; control->quick_stop = 0U;
    Upper_RetryCapabilities(protocol, now);
}

#endif
void UpperControl_Intent(UpperControl_t *control, UpperProtocol_t *protocol, UpperIntent_t intent, int16_t rpm, uint32_t now)
{
#if UPPER_CAN_MOTION_ENABLED
    const UpperSnapshot_t *snapshot = &protocol->snapshot;
    const UpperRecord_t *stop = UpperControl_Record(snapshot, 0x13U);
    uint8_t enabled;
    int16_t converted;
    if (intent == UPPER_INTENT_QUICK_STOP)
    {
        control->target_rpm = control->target_mm_s = 0;
        control->motion_token = 0U; control->request_active = 0U;
        control->operation_detail = 0U; control->tx_retry_pending = 0U;
        if (control->quick_stop && (control->state == SIM_STOP_REQUESTED || control->state == SIM_STOPPING)) return;
        if (snapshot->state != UPPER_READY || snapshot->link_token == 0U)
        { Fail(control, 100U); control->recovery_required = 1U; return; }
        control->link_token = snapshot->link_token; control->lower_boot = snapshot->lower_boot_id;
        control->upper_boot = snapshot->upper_boot_id;
        if (control->state != SIM_STOP_REQUESTED && control->state != SIM_STOPPING && control->state != SIM_CROSS_SOURCE_STOP)
        { control->stop_at = now; control->stop_baseline = stop ? Upper_ReadU32(stop->payload) : 0U; }
        else if (control->state == SIM_CROSS_SOURCE_STOP) control->stop_at = control->cross_stop_at;
        control->quick_stop = 1U; control->quick_attempts = control->quick_ack = 0U;
        ++control->stop_sequence; control->quick_retry_at = now;
        control->axes = snapshot->axes; control->stop_event = 0U;
        ResetStopVerify(control);
        control->stop_phase = 0U; control->stop_result = 255U;
        control->cross_zero_observations = 0U; control->last_zero_record = 0U;
        control->cross_axis_at[0] = control->cross_axis_at[1] = now;
        control->detail = 0U; control->state = SIM_STOP_REQUESTED;
        return;
    }
    if (intent == UPPER_INTENT_STOP || intent == UPPER_INTENT_RELEASE)
    {
        if (control->state == SIM_RECOVERY_VERIFY)
        { control->state = SIM_FAULT; control->detail = 115U; control->request_active = 0U; return; }
        if (control->state == SIM_FAULT && intent == UPPER_INTENT_RELEASE)
        { control->recovery_required = 1U; return; }
        control->operation_detail = 0U;
        if (control->state == SIM_AUTH_PREPARING) { EndPreparation(control, 0U); return; }
        if (control->state == SIM_STOP_REQUESTED || control->state == SIM_STOPPING) return;
        if (control->state == SIM_STOPPED_RELEASED || control->state == SIM_IDLE) return;
        ResetStopVerify(control);
        control->quick_stop = 0U;
        control->target_rpm = control->target_mm_s = 0;
        control->motion_token = 0U;
        if (snapshot->state != UPPER_READY || snapshot->link_token == 0U) { Fail(control, 100U); return; }
        if (control->state != SIM_AUTH_REQUESTED && control->state != SIM_AUTHORIZED_WAIT_ZERO &&
            control->state != SIM_AUTHORIZED_ZERO && control->state != SIM_RUNNING && control->state != SIM_FAULT) return;
        control->link_token = snapshot->link_token;
        control->lower_boot = snapshot->lower_boot_id;
        control->upper_boot = snapshot->upper_boot_id;
        control->stop_baseline = stop ? Upper_ReadU32(stop->payload) : 0U;
        control->stop_event = 0U; control->stop_at = now; control->stop_sent = 0U; control->axes = snapshot->axes;
        StartCommand(control, control->state == SIM_AUTH_REQUESTED || control->state == SIM_FAULT ? 4U : 5U, 0, now);
        control->state = SIM_STOP_REQUESTED;
        return;
    }
    if (intent == UPPER_INTENT_AUTHORIZE)
    {
        if (control->state == SIM_CROSS_SOURCE_STOP) { control->recovery_reason = 1U; return; }
        if (control->state == SIM_FAULT && control->detail == 115U && !control->recovery_hard)
        {
            const UpperRecord_t *record = UpperControl_Record(snapshot, 0x11U);
            unsigned axis;
            if (Fresh(snapshot, record, now))
            {
                if (record->payload[34] >= 1U && record->payload[34] <= 3U)
                { control->recovery_reason = 1U; return; }
                if (Upper_ReadU32(record->payload + 12U) & 512U)
                { control->recovery_reason = 4U; return; }
                if (record->payload[6] != 0U || record->payload[7] != 1U)
                { control->recovery_reason = 3U; return; }
            }
            for (axis = 0U; axis < 2U; ++axis)
            {
                const uint8_t *data = snapshot->basic[6U + axis].data;
                if ((control->axes & (1U << axis)) && Upper_BasicFresh(snapshot, (uint8_t)(6U + axis), now) &&
                    ((data[0] & 31U) != 31U || (data[0] & 32U) || data[1] != 2U ||
                     Upper_ReadU16(data + 2U) || Upper_ReadU16(data + 4U)))
                { control->recovery_reason = 5U; return; }
            }
            control->state = SIM_RECOVERY_VERIFY; control->prepare_at = now;
            control->query_attempts = 0U; control->request_active = 0U;
            control->cross_zero_observations = 0U;
            return;
        }
        if (control->state == SIM_FAULT) { control->recovery_required = 1U; return; }
        if (control->state != SIM_IDLE && control->state != SIM_STOPPED_RELEASED) return;
        BeginPreparation(control, protocol, now);
        Prepare(control, protocol, now);
        return;
    }
    if (intent == UPPER_INTENT_APPLY && (control->state == SIM_AUTHORIZED_ZERO || control->state == SIM_RUNNING))
    {
        const UpperRecord_t *record = UpperControl_Record(snapshot, 0x11U);
        uint16_t session_limit;
        if (!UpperControl_Convert(snapshot, rpm, &converted)) { control->detail = 102U; return; }
        if (rpm != 0)
        {
            if (!record || record->length != 40U || !Fresh(snapshot, record, now))
            { control->detail = 102U; return; }
            session_limit = Upper_ReadU16(record->payload + 24U);
            if (!session_limit || session_limit == 65535U || converted > (int32_t)session_limit || converted < -(int32_t)session_limit)
            { control->detail = 102U; return; }
        }
        if (!record || !Fresh(snapshot, record, now) || record->payload[6] != 4U ||
            !(Upper_ReadU32(record->payload + 12U) & 8U) || (Upper_ReadU32(record->payload + 12U) & 512U) ||
            Upper_ReadU16(record->payload + 4U) != control->motion_token ||
            !AxesSafe(snapshot, now, 0U, &enabled))
        { control->detail = control->operation_detail = 104U; control->rejected_rpm = rpm;
          control->operation_at = now; control->operation_count++; return; }
        control->target_rpm = rpm; control->target_mm_s = converted; control->detail = 0U;
        control->operation_detail = 0U;
        control->state = rpm == 0 ? SIM_AUTHORIZED_ZERO : SIM_RUNNING;
    }
#else
    (void)control; (void)protocol; (void)intent; (void)rpm; (void)now;
#endif
}

void UpperControl_Receive(UpperControl_t *control, const UpperProtocol_t *protocol, uint16_t id, const uint8_t data[8], uint32_t received, uint32_t now)
{
    const UpperSnapshot_t *snapshot = &protocol->snapshot;
    if ((uint32_t)(now - received) > 200U) return;
    if (!UPPER_CAN_MOTION_ENABLED || (id != 0x120U && id != 0x121U) || !control->request_active ||
        snapshot->link_token != control->link_token || snapshot->lower_boot_id != control->lower_boot ||
        Upper_ReadU16(data + 4U) != control->link_token || data[0] != control->request_sequence ||
        data[1] != control->command || (int32_t)(received - control->request_at) < 0) return;
    if (control->command == 8U && (control->state == SIM_AUTH_PREPARING ||
        control->state == SIM_CROSS_SOURCE_STOP || control->state == SIM_RECOVERY_VERIFY ||
        (control->state == SIM_FAULT && control->detail == 115U)))
    {
        control->query_phase = id == 0x120U ? 1U : data[2];
        control->query_result = id == 0x120U ? data[2] : data[3];
        control->query_detail = id == 0x121U ? Upper_ReadU16(data + 6U) : 0U;
        if (id == 0x120U && !data[2]) control->accepted = 1U;
        if (id == 0x121U && data[2] == 3U && !data[3]) control->completed = 1U;
        if ((id == 0x120U && data[2]) || (id == 0x121U && data[2] >= 3U && (data[2] != 3U || data[3])))
            control->request_active = 0U;
        return;
    }
    if (id == 0x120U)
    {
        if (data[2] == 0U) control->accepted = 1U;
        else
        {
            uint8_t rejected_grant = (uint8_t)(control->command == 4U && control->request_parameter == 1);
            control->result_code = data[2]; Fail(control, (uint16_t)(300U + data[2]));
            if (rejected_grant) control->state = SIM_IDLE;
        }
    }
    else if (data[2] >= 3U)
    {
        control->result_code = data[3];
        if (data[2] == 3U && data[3] == 0U) control->completed = 1U;
        else
        {
            uint8_t rejected_grant = (uint8_t)(control->command == 4U && control->request_parameter == 1);
            Fail(control, (uint16_t)(320U + data[3]));
            if (rejected_grant) control->state = SIM_IDLE;
        }
    }
}
void UpperControl_TxResult(UpperControl_t *control, uint16_t id, const uint8_t data[8], uint8_t success, uint32_t now)
{
    UpperControl_TxOutcome(control, id, data, success ? UPPER_TX_OK : UPPER_TX_HARDWARE, now);
}

void UpperControl_TxOutcome(UpperControl_t *control, uint16_t id, const uint8_t data[8], UpperTxOutcome_t outcome, uint32_t now)
{
    if (id == 0x111U && Upper_ReadU16(data + 4U) == control->motion_token && control->motion_token != 0U)
    {
        if (outcome == UPPER_TX_OK) { control->tx_done_at = now; control->tx_seen = 1U; }
        else if (outcome != UPPER_TX_CANCELLED && outcome != UPPER_TX_REPLACED)
        {
            control->tx_failure = (uint8_t)outcome;
            control->recovery_required = 1U;
            if (!control->first_error) { control->first_error = 110U; control->first_error_at = now; }
            Fail(control, 110U);
            /* Observe the existing owner stop; never replay the failed target. */
            control->cross_stop_at = now; control->cross_control_at = now;
            control->cross_axis_at[0] = control->cross_axis_at[1] = now;
            control->cross_associated = control->cross_zero_observations = 0U;
            control->stop_event = 0U; control->query_attempts = 0U;
            control->recovery_hard = 0U; control->recovery_reason = 1U;
        }
    }
}

#if UPPER_CAN_MOTION_ENABLED
static uint8_t SendSpeed(UpperControl_t *control, uint32_t now)
{
    uint8_t data[8];
    uint16_t next = (uint16_t)(control->speed_sequence + 1U);
    Put16(data, next);
    Put16(data + 2U, (uint16_t)control->target_mm_s);
    Put16(data + 4U, control->motion_token);
    Put16(data + 6U, UPPER_TARGET_LEASE_MS);
    if (control->send == 0 || !control->send(control->user, 0x111U, data)) return 0U;
    control->speed_sequence = next; control->speed_at = now;
    if (!control->zero_sent)
    { control->first_zero_sequence = next; control->zero_sent = 1U; }
    return 1U;
}

static void BeginCrossSourceStop(UpperControl_t *control, uint32_t now)
{
    control->cross_stop_at = control->state == SIM_STOPPING || control->state == SIM_STOP_REQUESTED ? control->stop_at : now;
    control->state = SIM_CROSS_SOURCE_STOP;
    control->cross_zero_observations = 0U;
    control->last_zero_record = 0U;
    control->target_rpm = control->target_mm_s = 0;
    control->motion_token = 0U;
    control->request_active = 0U;
    control->detail = 115U;
    control->stop_phase = 0U;
    control->stop_result = 255U;
    control->zero_mask = 0U;
    control->stop_event = 0U; control->cross_associated = 0U;
    ResetStopVerify(control);
    control->cross_control_at = now; control->cross_axis_at[0] = control->cross_axis_at[1] = now;
    control->quick_stop = control->tx_retry_pending = 0U;
    control->recovery_hard = 0U; control->recovery_reason = 1U;
    control->recovery_required = 1U; control->query_attempts = 0U;
}

static uint8_t CrossSourceReleased(const UpperControl_t *control, const UpperSnapshot_t *snapshot,
    const UpperRecord_t *control_record, const UpperRecord_t *stop_record, uint32_t now, uint8_t *enabled)
{
    uint32_t flags;
    uint32_t event;
    if (control_record == 0 || stop_record == 0 || !Fresh(snapshot, control_record, now) || !Fresh(snapshot, stop_record, now)) return 0U;
    event = Upper_ReadU32(stop_record->payload);
    flags = Upper_ReadU32(control_record->payload + 12U);
    if ((control->quick_stop ? control->stop_event == 0U : !control->cross_associated) || event != control->stop_event || stop_record->payload[5] != 4U ||
        control_record->payload[34] != 4U || control_record->payload[35] != 0U || stop_record->payload[7] != 0U ||
        snapshot->control_source != 0U || snapshot->lifecycle != 1U || control_record->payload[6] != 0U || control_record->payload[7] != 1U ||
        !Upper_BasicFresh(snapshot, 5U, now) ||
        Upper_ReadU16(control_record->payload + 4U) != 0U || (flags & (8U | 512U)) != 0U ||
        snapshot->axes == 0U || snapshot->axes != control->axes || (control->zero_mask & snapshot->axes) != snapshot->axes ||
        !AxesSafe(snapshot, now, 1U, enabled)) return 0U;
    return 1U;
}

static uint8_t CrossSourceDetected(const UpperControl_t *control, const UpperSnapshot_t *snapshot,
    const UpperRecord_t *record, uint32_t now)
{
    uint32_t flags;
    if (record == 0 || !Fresh(snapshot, record, now)) return 0U;
    flags = Upper_ReadU32(record->payload + 12U);
    return (uint8_t)(record->payload[6] != 4U || !(flags & 8U) ||
        Upper_ReadU16(record->payload + 4U) != control->motion_token ||
        (record->payload[34] >= 1U && record->payload[34] <= 5U));
}

static void RecoveryQuery(UpperControl_t *control, uint32_t now)
{
    if (control->request_active)
    {
        if ((control->accepted && control->completed) || (uint32_t)(now - control->request_at) >= UPPER_CONFIG_QUERY_TIMEOUT_MS)
            control->request_active = 0U;
        else
        {
            if (control->attempts < 4U && (int32_t)(now - control->retry_at) >= 0) (void)SendCommand(control, now);
            return;
        }
    }
    if (control->query_attempts >= 6U) return;
    if (control->query_attempts && (uint32_t)(now - control->request_at) < UPPER_CONFIG_QUERY_TIMEOUT_MS) return;
    StartCommand(control, 8U, (control->query_attempts & 1U) ? 0x13 : 0x11, now);
    ++control->query_attempts;
    (void)SendCommand(control, now);
}

static void ObserveCrossStop(UpperControl_t *control, UpperProtocol_t *protocol, uint32_t now)
{
    const UpperSnapshot_t *snapshot = &protocol->snapshot;
    const UpperRecord_t *record = UpperControl_Record(snapshot, 0x11U);
    const UpperRecord_t *stop = UpperControl_Record(snapshot, 0x13U);
    uint8_t enabled = 0U, all_new = 1U;
    unsigned axis;
    uint32_t flags = record ? Upper_ReadU32(record->payload + 12U) : 0U;
    CaptureStopVerify(control,snapshot,now);
    if (snapshot->state != UPPER_READY || snapshot->link_token != control->link_token ||
        snapshot->lower_boot_id != control->lower_boot || snapshot->upper_boot_id != control->upper_boot ||
        !snapshot->heartbeat_seen || (uint32_t)(now - snapshot->heartbeat_at) >= UPPER_LINK_TIMEOUT_MS)
    { CaptureLinkFailure(control,snapshot,now); Fail(control, 111U); return; }
    if ((control->state == SIM_RECOVERY_VERIFY && (uint32_t)(now - control->prepare_at) >= UPPER_AUTH_PREPARE_TIMEOUT_MS) ||
        (control->state == SIM_CROSS_SOURCE_STOP && (uint32_t)(now - control->cross_stop_at) >= UPPER_STOP_TIMEOUT_MS))
    { control->recovery_reason = 14U; Fail(control, 115U); control->request_active = 0U; return; }
    control->recovery_reason = 2U;
    if (Fresh(snapshot, record, now) && Fresh(snapshot, stop, now) &&
        record->length == 40U && stop->length == 40U &&
        (int32_t)(record->timestamp - control->cross_control_at) >= 0 &&
        (int32_t)(stop->timestamp - control->cross_control_at) >= 0 &&
        Upper_ReadU32(record->payload) != control->motion_generation &&
        record->payload[34] == stop->payload[5] && record->payload[34] >= 1U && record->payload[34] <= 5U &&
        !(flags & 8U) && !Upper_ReadU16(record->payload + 4U))
    {
        uint32_t event = Upper_ReadU32(stop->payload);
        if (!event)
        { control->recovery_reason = 13U; RecoveryQuery(control, now); return; }
        if (control->cross_associated && control->stop_event != event)
        { control->recovery_hard = 1U; control->recovery_reason = 10U; Fail(control, 115U); return; }
        if (!control->cross_associated)
        {
            control->stop_event = event; control->cross_associated = 1U;
            control->cross_zero_observations = 0U;
        }
        control->stop_phase = stop->payload[5]; control->stop_reason = stop->payload[4];
        control->zero_mask = stop->payload[6]; control->stop_result = record->payload[35];
        if (control->stop_phase == 5U || control->stop_result || stop->payload[7])
        { control->recovery_hard = 1U; control->recovery_reason = 6U; Fail(control, 115U); return; }
        control->recovery_reason = (flags & 512U) ? 4U :
            snapshot->control_source || snapshot->lifecycle != 1U ? 3U :
            control->stop_phase != 4U ? 1U :
            !Fresh(snapshot, record, now) ? 7U :
            !AxesSafe(snapshot, now, 1U, &enabled) ? 8U :
            (enabled & control->axes) != control->axes ? 12U : 5U;
        if (CrossSourceReleased(control, snapshot, record, stop, now, &enabled))
        {
            for (axis = 0U; axis < 2U; ++axis)
                if ((control->axes & (1U << axis)) && (int32_t)(snapshot->basic[6U + axis].timestamp - control->cross_axis_at[axis]) <= 0) all_new = 0U;
            if (all_new)
            {
                for (axis = 0U; axis < 2U; ++axis) control->cross_axis_at[axis] = snapshot->basic[6U + axis].timestamp;
                if (++control->cross_zero_observations >= 2U)
                {
                    uint8_t authorize = control->state == SIM_RECOVERY_VERIFY;
                    control->state = SIM_STOPPED_RELEASED; control->detail = 0U;
                    control->recovery_required = control->recovery_reason = 0U;
                    control->enable_mask = enabled; control->request_active = 0U;
                    if (authorize) BeginPreparation(control, protocol, now);
                    return;
                }
            }
        }
        else control->cross_zero_observations = 0U;
    }
    else
    {
        control->cross_zero_observations = 0U;
        if (Fresh(snapshot, record, now) && Fresh(snapshot, stop, now) &&
            record->payload[34] != stop->payload[5]) control->recovery_reason = 11U;
    }
    if (control->state != SIM_FAULT) RecoveryQuery(control, now);
}

#endif
void UpperControl_Tick(UpperControl_t *control, UpperProtocol_t *protocol, uint32_t now)
{
#if UPPER_CAN_MOTION_ENABLED
    const UpperSnapshot_t *snapshot = &protocol->snapshot;
    const UpperRecord_t *record = UpperControl_Record(snapshot, 0x11U);
    const UpperRecord_t *stop = UpperControl_Record(snapshot, 0x13U);
    uint8_t enabled = 0U;
    uint32_t flags = record ? Upper_ReadU32(record->payload + 12U) : 0U;
    if (control->quick_stop && (control->state == SIM_STOP_REQUESTED || control->state == SIM_STOPPING))
    {
        uint8_t data[8] = {0};
        CaptureStopVerify(control,snapshot,now);
        if (snapshot->state != UPPER_READY || snapshot->link_token != control->link_token ||
            snapshot->lower_boot_id != control->lower_boot || snapshot->upper_boot_id != control->upper_boot ||
            !snapshot->heartbeat_seen || (uint32_t)(now - snapshot->heartbeat_at) >= UPPER_LINK_TIMEOUT_MS)
        { CaptureLinkFailure(control,snapshot,now); Fail(control, 111U); control->recovery_required = 1U; return; }
        if ((uint32_t)(now - control->stop_at) >= UPPER_STOP_TIMEOUT_MS)
        { Fail(control, 114U); control->recovery_required = 1U; return; }
        if (Fresh(snapshot, record, now) && (flags & 512U)) control->recovery_required = 1U;
        if (Fresh(snapshot, record, now) && (int32_t)(record->timestamp - control->stop_at) >= 0 &&
            Upper_ReadU16(record->payload + 32U) == control->stop_sequence &&
            record->payload[34] >= 1U && record->payload[34] <= 5U) control->quick_ack = 1U;
        if (!control->quick_ack && control->quick_attempts < 4U && (int32_t)(now - control->quick_retry_at) >= 0)
        {
            Put16(data, control->stop_sequence); Put16(data + 2U, control->link_token); data[4] = 1U;
            ++control->quick_attempts; control->quick_retry_at = now + 100U;
            if (control->send) (void)control->send(control->user, 0x080U, data);
        }
        if (control->quick_ack && Fresh(snapshot, record, now) &&
            Upper_ReadU16(record->payload + 32U) == control->stop_sequence &&
            Fresh(snapshot, stop, now) && (int32_t)(stop->timestamp - control->stop_at) >= 0 &&
            Upper_ReadU32(stop->payload) != 0U &&
            record->payload[34] == stop->payload[5])
        {
            if (control->stop_event != 0U && control->stop_event != Upper_ReadU32(stop->payload))
            { control->cross_zero_observations = 0U; control->last_zero_record = 0U; }
            control->stop_event = Upper_ReadU32(stop->payload); control->stop_phase = stop->payload[5];
            control->zero_mask = stop->payload[6]; control->stop_result = record->payload[35];
            if (control->stop_phase == 5U || (flags & 512U))
            {
                if (control->stop_phase != 5U) { BeginCrossSourceStop(control, now); return; }
                control->recovery_hard = 1U; control->recovery_reason = 6U;
                Fail(control, 115U); control->recovery_required = 1U; return;
            }
            if (control->last_zero_record != stop->timestamp && CrossSourceReleased(control, snapshot, record, stop, now, &enabled))
            {
                unsigned axis;
                uint8_t all_new = 1U;
                for (axis = 0U; axis < 2U; ++axis)
                    if ((control->axes & (1U << axis)) &&
                        (int32_t)(snapshot->basic[6U + axis].timestamp - control->cross_axis_at[axis]) <= 0)
                        all_new = 0U;
                if (all_new)
                {
                    control->last_zero_record = stop->timestamp;
                    for (axis = 0U; axis < 2U; ++axis)
                        control->cross_axis_at[axis] = snapshot->basic[6U + axis].timestamp;
                    if (++control->cross_zero_observations >= 2U)
                    {
                        control->state = SIM_STOPPED_RELEASED; control->detail = 0U;
                        control->enable_mask = enabled;
                        control->recovery_required = control->recovery_reason = 0U;
                        control->request_active = control->tx_retry_pending = 0U;
                        control->motion_token = 0U; control->target_rpm = control->target_mm_s = 0;
                        return;
                    }
                }
            }
            else if (!CrossSourceReleased(control, snapshot, record, stop, now, &enabled)) control->cross_zero_observations = 0U;
        }
        else control->cross_zero_observations = 0U;
        control->state = SIM_STOPPING;
        return;
    }
    if (control->state == SIM_CROSS_SOURCE_STOP || control->state == SIM_RECOVERY_VERIFY ||
        (control->state == SIM_FAULT && (control->detail == 115U || control->detail == 110U) && control->recovery_required && !control->recovery_hard))
    {
        if (control->state == SIM_FAULT && control->detail == 110U) control->state = SIM_CROSS_SOURCE_STOP;
        ObserveCrossStop(control, protocol, now); return;
    }
    if (control->state == SIM_AUTH_PREPARING)
    {
        Prepare(control, protocol, now);
        if (control->state != SIM_AUTH_REQUESTED) return;
    }
    if (control->state == SIM_LINK_NOT_READY || control->state == SIM_IDLE || control->state == SIM_STOPPED_RELEASED)
    {
        if (snapshot->state == UPPER_READY)
        { if (control->state == SIM_LINK_NOT_READY) control->state = SIM_IDLE; }
        else { control->state = SIM_LINK_NOT_READY; control->target_rpm = control->target_mm_s = 0; }
        return;
    }
    if (control->state == SIM_FAULT && control->detail != 115U) return;
    if (snapshot->state != UPPER_READY || snapshot->link_token != control->link_token ||
        snapshot->lower_boot_id != control->lower_boot || snapshot->upper_boot_id != control->upper_boot)
    { CaptureLinkFailure(control,snapshot,now); Fail(control, 111U); return; }
    if (control->state == SIM_FAULT && control->detail == 115U) return;
    if ((control->state == SIM_AUTHORIZED_WAIT_ZERO || control->state == SIM_AUTHORIZED_ZERO || control->state == SIM_RUNNING) &&
        CrossSourceDetected(control, snapshot, record, now))
    {
        BeginCrossSourceStop(control, now);
        return;
    }
    if (control->request_active && !(control->accepted && control->completed) &&
        (int32_t)(now - control->retry_at) >= 0 && control->attempts < 4U)
        SendCommand(control, now);
    if (control->state == SIM_AUTH_REQUESTED)
    {
        if ((uint32_t)(now - control->request_at) >= UPPER_COMMAND_TIMEOUT_MS)
        {
#if UPPER_USART1_DEBUG_ENABLE
            UpperControl_CaptureStop(control,protocol,UPPER_STOP_AUTH_TIMEOUT,112U,0xFFU,0U,now,now);
#endif
            UpperControl_Intent(control, protocol, UPPER_INTENT_STOP, 0, now); control->detail = 112U; return;
        }
        if (control->accepted && control->completed && Fresh(snapshot, record, now) &&
            (int32_t)(record->timestamp - control->request_at) >= 0 && record->payload[6] == 4U &&
            (flags & 8U) && !(flags & 512U) && Upper_ReadU16(record->payload + 4U) != 0U &&
            Upper_ReadU16(record->payload + 4U) != control->last_motion_token)
        {
            control->motion_token = Upper_ReadU16(record->payload + 4U);
            control->motion_generation = Upper_ReadU32(record->payload);
            control->cross_associated = control->recovery_hard = control->recovery_required = control->recovery_reason = 0U;
            control->last_motion_token = control->motion_token;
            control->request_active = 0U;
            control->axes = record->payload[10];
            control->state = SIM_AUTHORIZED_WAIT_ZERO;
            control->request_at = now;
            control->speed_at = now - UPPER_TARGET_PERIOD_MS;
            control->target_rpm = control->target_mm_s = 0;
        }
        else return;
    }
    if (control->state == SIM_STOP_REQUESTED || control->state == SIM_STOPPING)
        CaptureStopVerify(control,snapshot,now);
    if (Fresh(snapshot, stop, now))
    {
        uint32_t event = Upper_ReadU32(stop->payload);
        if (event != 0U && (int32_t)(event - control->stop_baseline) > 0 &&
            (control->stop_event == 0U || control->stop_event == event))
        {
            control->stop_event = event; control->stop_phase = stop->payload[5];
            control->stop_reason = stop->payload[4]; control->zero_mask = stop->payload[6];
            if (stop->payload[5] == 5U) { Fail(control, 113U); return; }
        }
    }
    if (control->state == SIM_STOP_REQUESTED || control->state == SIM_STOPPING)
    {
        CaptureStopVerify(control,snapshot,now);
        if ((uint32_t)(now - control->stop_at) >= UPPER_STOP_TIMEOUT_MS) { Fail(control, 114U); return; }
        if (Fresh(snapshot, record, now))
        {
            control->stop_result = record->payload[35];
            if ((flags & 512U) && record->payload[34] >= 1U && record->payload[34] <= 4U)
            { BeginCrossSourceStop(control, now); return; }
            if (record->payload[34] == 5U || (flags & 512U))
            { control->recovery_hard = 1U; control->recovery_reason = 6U; Fail(control, 115U); control->recovery_required = 1U; return; }
            if (control->stop_event != 0U && control->stop_phase == 4U && control->stop_result == 0U &&
                (control->zero_mask & control->axes) == control->axes && control->axes != 0U &&
                record->payload[6] == 0U && Upper_ReadU16(record->payload + 4U) == 0U && !(flags & 8U) &&
                snapshot->control_source == 0U && snapshot->lifecycle == 1U &&
                AxesSafe(snapshot, now, 1U, &enabled))
            {
                control->enable_mask = enabled; control->state = SIM_STOPPED_RELEASED;
                control->request_active = 0U; control->detail = 0U;
                control->recovery_required = 0U;
                control->motion_token = 0U; control->target_rpm = control->target_mm_s = 0;
                return;
            }
            if (control->stop_event == 0U && control->accepted && control->completed &&
                record->payload[6] == 0U && (record->payload[34] == 0U ||
                 (record->payload[34] == 4U && record->payload[35] == 0U)) &&
                (int32_t)(record->timestamp - control->request_at) >= 0 && !(flags & 520U) &&
                snapshot->control_source == 0U && AxesSafe(snapshot, now, 1U, &enabled))
            {
                control->state = SIM_STOPPED_RELEASED; control->request_active = 0U;
                control->enable_mask = enabled; control->stop_phase = 0U; control->stop_result = 0U;
                control->detail = 0U; control->recovery_required = 0U;
                return;
            }
        }
        control->state = SIM_STOPPING;
        return;
    }
    if (!Fresh(snapshot, record, now) || !AxesSafe(snapshot, now, 0U, &enabled))
    {
        unsigned axis;
        for (axis = 0U; axis < 2U; ++axis)
        {
            const uint8_t *data = snapshot->basic[6U + axis].data;
            if ((snapshot->axes & (1U << axis)) && Upper_BasicFresh(snapshot, (uint8_t)(6U + axis), now) &&
                ((data[0] & 31U) != 31U || (data[0] & 32U) || data[1] != 2U || Upper_ReadU16(data + 4U)))
            {
#if UPPER_USART1_DEBUG_ENABLE
                UpperControl_CaptureStop(control,protocol,UPPER_STOP_AUTO_UNSAFE,116U,0xFFU,0U,now,now);
#endif
                Fail(control, 116U);
                control->recovery_hard = control->recovery_required = 1U;
                control->recovery_reason = 9U;
                return;
            }
        }
        control->recovery_reason = !Fresh(snapshot, record, now) ? 7U : 8U;
#if UPPER_USART1_DEBUG_ENABLE
        UpperControl_CaptureStop(control,protocol,control->recovery_reason==7U ?
            UPPER_STOP_AUTO_CONTROL : UPPER_STOP_AUTO_AXIS,116U,0xFFU,0U,now,now);
#endif
        UpperControl_Intent(control, protocol, UPPER_INTENT_STOP, 0, now);
        control->detail = 116U;
        control->recovery_required = 1U;
        return;
    }
    if (record->payload[34] >= 1U && record->payload[34] <= 4U && !(flags & 8U))
    {
        control->target_rpm = control->target_mm_s = 0; control->motion_token = 0U;
        control->request_active = 0U; control->state = SIM_STOPPING; control->stop_at = now;
        return;
    }
    if (record->payload[6] != 4U || !(flags & 8U) ||
        (flags & 512U) || Upper_ReadU16(record->payload + 4U) != control->motion_token)
    { Fail(control, 117U); return; }
    if (record->payload[10] != control->axes || snapshot->axes != control->axes) { Fail(control, 120U); return; }
    control->enable_mask = enabled;
    if (control->state == SIM_AUTHORIZED_WAIT_ZERO)
    {
        uint16_t seen_sequence = Upper_ReadU16(record->payload + 20U);
        const UpperBasic_t *runtime = &snapshot->basic[8];
        if ((uint32_t)(now - control->request_at) >= UPPER_FIRST_ZERO_TIMEOUT_MS)
        {
#if UPPER_USART1_DEBUG_ENABLE
            UpperControl_CaptureStop(control,protocol,UPPER_STOP_ZERO_TIMEOUT,118U,0xFFU,0U,now,now);
#endif
            UpperControl_Intent(control, protocol, UPPER_INTENT_STOP, 0, now); control->detail = 118U; return;
        }
        if (control->zero_sent && control->tx_seen && (uint16_t)(seen_sequence - control->first_zero_sequence) < 32768U &&
            Upper_ReadU16(record->payload + 22U) < UPPER_TARGET_LEASE_MS &&
            record->timestamp != control->last_zero_record &&
            (int32_t)(record->timestamp - control->request_at) >= 0 &&
            Upper_BasicFresh(snapshot, 8U, now) && runtime->data[2] == 4U &&
            (runtime->data[7] & 52U) == 4U && AxesSafe(snapshot, now, 1U, &enabled))
        {
            control->last_zero_record = record->timestamp;
            if (++control->zero_observations >= 2U) control->state = SIM_AUTHORIZED_ZERO;
        }
    }
    if (control->tx_seen && (uint32_t)(now - control->tx_done_at) >= UPPER_TARGET_LEASE_MS)
    { Fail(control, 119U); return; }
    if ((uint32_t)(now - control->speed_at) >= UPPER_TARGET_PERIOD_MS) SendSpeed(control, now);
#else
    (void)protocol; (void)now; control->state = SIM_IDLE;
#endif
}
const char *UpperControl_StateName(UpperControlState_t state)
{
    static const char *names[] = {"IDLE","NO_LINK","AUTH_WAIT","FIRST_ZERO","ZERO_READY","RUNNING","STOP_REQ","STOPPING","RELEASED","FAULT","AUTH_PREP","CROSS_STOP","VERIFY"};
    return state <= SIM_RECOVERY_VERIFY ? names[state] : "UNKNOWN";
}
const char *UpperControl_RecoveryName(uint8_t reason)
{
    static const char *names[] = {"K1 AUTH","WAIT STOP","STOP STALE","WAIT RELEASE","SAFETY LOCK","WAIT ZERO","STOP FAILED","CTRL STALE","AXIS STALE","AXIS UNSAFE","EVENT CHANGED","PHASE CONFLICT","ENABLE WAIT","NO STOP EVENT","VERIFY TIMEOUT"};
    return reason <= 14U ? names[reason] : "STOP STALE";
}
