#include "can_upper_ui.h"
#include <stdio.h>
#include <string.h>
static uint8_t page, selected, raw_mask, stable_mask, long_mask;
static UpperControlState_t previous_state;
static uint32_t changed_at[3], pressed_at[3];
static const int16_t presets[] = {0,100,200,300,400,0,-100,-200,-300,-400};
static UpperUiEvent_t events[8];
static volatile uint8_t event_head, event_tail;
static volatile uint32_t quick_written, quick_read, normal_written, normal_read;
static uint32_t quick_at, normal_at;
static uint8_t quick_mask, normal_mask;
static void PushEvent(UpperUiEventType_t type, int16_t rpm, uint32_t now)
{
    uint8_t next = (uint8_t)((event_head + 1U) & 7U);
    if (type == UPPER_UI_EVENT_QUICK_STOP) { quick_at = now; quick_mask = stable_mask; ++quick_written; return; }
    if (type == UPPER_UI_EVENT_STOP) { normal_at = now; normal_mask = stable_mask; ++normal_written; return; }
    if (next == event_tail) return;
    events[event_head].type = type; events[event_head].rpm = rpm; events[event_head].key_mask = stable_mask;
    events[event_head].at = now; event_head = next;
}
void UpperUi_Init(void)
{
    page = selected = raw_mask = stable_mask = long_mask = 0U;
    previous_state = SIM_LINK_NOT_READY;
    memset(changed_at, 0, sizeof(changed_at)); memset(pressed_at, 0, sizeof(pressed_at));
    event_head = event_tail = 0U;
    quick_written = quick_read = normal_written = normal_read = 0U;
    quick_mask = normal_mask = 0U;
}
void UpperUi_NotifyState(UpperControlState_t state)
{
    if (state != previous_state)
    {
        if (state == SIM_LINK_NOT_READY || state == SIM_STOPPED_RELEASED || state == SIM_FAULT ||
            state == SIM_AUTH_PREPARING || state == SIM_STOP_REQUESTED || state == SIM_STOPPING || state == SIM_CROSS_SOURCE_STOP) selected = 0U;
        previous_state = state;
    }
}
void UpperUi_ScanKeysOnly(uint32_t now, uint8_t pressed_mask)
{
    unsigned key;
    for (key = 0U; key < 3U; ++key)
    {
        uint8_t bit = (uint8_t)(1U << key);
        if ((raw_mask & bit) != (pressed_mask & bit))
        { raw_mask ^= bit; changed_at[key] = now; }
        if ((uint32_t)(now - changed_at[key]) >= UPPER_KEY_DEBOUNCE_MS && ((stable_mask ^ raw_mask) & bit))
        {
            stable_mask ^= bit;
            if (stable_mask & bit)
            {
                pressed_at[key] = now; long_mask &= (uint8_t)~bit;
                if (key == 2U) { PushEvent(UPPER_UI_EVENT_QUICK_STOP, 0, now); selected = 0U; }
            }
            else if (!(long_mask & bit))
            {
                if (key == 0U) { page = (uint8_t)((page + 1U) % UPPER_UI_PAGE_COUNT); if (page == 2U) selected = 0U; PushEvent(UPPER_UI_EVENT_PAGE, 0, now); }
                else if (key == 1U && page == 2U) { selected = (uint8_t)((selected + 1U) % (sizeof(presets) / sizeof(presets[0]))); PushEvent(UPPER_UI_EVENT_APPLY, presets[selected], now); }
            }
        }
        if ((stable_mask & bit) && !(long_mask & bit) && (uint32_t)(now - pressed_at[key]) >= UPPER_KEY_LONG_MS)
        {
            long_mask |= bit;
            if (key == 0U && page == 2U)
            {
                PushEvent(UPPER_UI_EVENT_K1_LONG, 0, now);
                selected = 0U;
            }
            else if (key == 1U && page == 2U)
            { PushEvent(UPPER_UI_EVENT_STOP, 0, now); selected = 0U; }
        }
    }
}
uint8_t UpperUi_GetEvent(UpperUiEvent_t *event)
{
    if (quick_written != quick_read)
    {
        quick_read = quick_written; normal_read = normal_written; event_tail = event_head;
        event->type = UPPER_UI_EVENT_QUICK_STOP; event->rpm = 0; event->key_mask=quick_mask; event->at = quick_at; return 1U;
    }
    if (normal_written != normal_read)
    {
        normal_read = normal_written; event_tail = event_head;
        event->type = UPPER_UI_EVENT_STOP; event->rpm = 0; event->key_mask=normal_mask; event->at = normal_at; return 1U;
    }
    if (event_head == event_tail) return 0U;
    *event = events[event_tail]; event_tail = (uint8_t)((event_tail + 1U) & 7U); return 1U;
}
void UpperUi_ScanKeys(uint32_t now, uint8_t pressed_mask, UpperControl_t *control, UpperProtocol_t *protocol)
{
    UpperUi_NotifyState(control->state);
    UpperUi_ScanKeysOnly(now, pressed_mask);
    UpperUi_ConsumeEvents(control, protocol);
}
static void ConsumeEvents(UpperControl_t *control, UpperProtocol_t *protocol, uint32_t now, uint8_t sampled)
{
    UpperUiEvent_t event;
    uint8_t stopped = 0U;
    while (UpperUi_GetEvent(&event))
    {
#if UPPER_USART1_DEBUG_ENABLE
        uint32_t event_at=event.at;
#endif
        if (sampled)
        {
            if (event.type != UPPER_UI_EVENT_QUICK_STOP && event.type != UPPER_UI_EVENT_STOP &&
                ((int32_t)(now - event.at) < 0 || (uint32_t)(now - event.at) >= 300U)) continue;
            event.at = now;
        }
        if (event.type == UPPER_UI_EVENT_QUICK_STOP)
        {
#if UPPER_USART1_DEBUG_ENABLE
            UpperControl_CaptureStop(control,protocol,UPPER_STOP_UI_QUICK,0U,(uint8_t)event.type,event.key_mask,event_at,event.at);
#endif
            UpperControl_Intent(control, protocol, UPPER_INTENT_QUICK_STOP, 0, event.at); stopped = 1U;
        }
        else if (event.type == UPPER_UI_EVENT_STOP)
        {
#if UPPER_USART1_DEBUG_ENABLE
            UpperControl_CaptureStop(control,protocol,UPPER_STOP_UI_NORMAL,0U,(uint8_t)event.type,event.key_mask,event_at,event.at);
#endif
            UpperControl_Intent(control, protocol, UPPER_INTENT_STOP, 0, event.at); stopped = 1U;
        }
        else if (stopped) continue;
        else if (event.type == UPPER_UI_EVENT_K1_LONG)
        {
            if (control->state == SIM_IDLE || control->state == SIM_STOPPED_RELEASED || control->state == SIM_CROSS_SOURCE_STOP ||
                (control->state == SIM_FAULT && control->detail == 115U)) UpperControl_Intent(control, protocol, UPPER_INTENT_AUTHORIZE, 0, event.at);
            else {
#if UPPER_USART1_DEBUG_ENABLE
                UpperControl_CaptureStop(control,protocol,UPPER_STOP_UI_RELEASE,0U,(uint8_t)event.type,event.key_mask,event_at,event.at);
#endif
                UpperControl_Intent(control, protocol, UPPER_INTENT_RELEASE, 0, event.at);
            }
        }
        else if (event.type == UPPER_UI_EVENT_APPLY) UpperControl_Intent(control, protocol, UPPER_INTENT_APPLY, event.rpm, event.at);
    }
}
void UpperUi_ConsumeEvents(UpperControl_t *control, UpperProtocol_t *protocol)
{ ConsumeEvents(control, protocol, 0U, 0U); }
void UpperUi_ConsumeEventsAt(UpperControl_t *control, UpperProtocol_t *protocol, uint32_t now)
{ ConsumeEvents(control, protocol, now, 1U); }
void UpperUi_Publish(const UpperControl_t *control, const UpperProtocol_t *protocol, uint32_t now, UpperUiView_t *view)
{
    const UpperSnapshot_t *snapshot = &protocol->snapshot;
    const UpperRecord_t *record = UpperControl_Record(snapshot, 0x11U);
    const UpperRecord_t *stop = UpperControl_Record(snapshot, 0x13U);
    memset(view, 0, sizeof(*view));
    {
        const UpperRecord_t *unified = UpperControl_Record(snapshot, 0x25U);
        const UpperRecord_t *diagnostic = UpperControl_Record(snapshot, 0x26U);
        const UpperRecord_t *statistics = UpperControl_Record(snapshot, 0x24U);
        if (unified != 0 && Upper_RecordFresh(snapshot, unified, now))
        {
            const uint8_t *payload = unified->payload;
            view->snapshot_fresh = 1U;
            view->global_owner = payload[16];
            view->snapshot_sequence = Upper_ReadU32(payload);
            view->snapshot_ms = Upper_ReadU32(payload + 4U);
            view->snapshot_age = now - unified->timestamp + Upper_ReadU32(payload + 100U);
            view->global_target = (int16_t)Upper_ReadU16(payload + 24U);
            view->position[0] = (int32_t)Upper_ReadU32(payload + 32U);
            view->position[1] = (int32_t)Upper_ReadU32(payload + 36U);
            view->position_fresh = (uint8_t)((Upper_ReadU32(payload + 40U) & 1U) != 0U);
            {
                int64_t net = (int64_t)Upper_ReadU64(payload + 44U);
                uint64_t path = Upper_ReadU64(payload + 52U);
                uint64_t trip = Upper_ReadU64(payload + 60U);
                view->net_mm = net < INT32_MIN ? INT32_MIN : net > INT32_MAX ? INT32_MAX : (int32_t)net;
                view->path_mm = path > UINT32_MAX ? UINT32_MAX : (uint32_t)path;
                view->trip_mm = trip > UINT32_MAX ? UINT32_MAX : (uint32_t)trip;
                if (net < INT32_MIN || net > INT32_MAX || path > UINT32_MAX || trip > UINT32_MAX)
                    view->position_fresh = 0U;
            }
            view->sync_fresh = (uint8_t)((Upper_ReadU32(payload + 68U) & 1U) != 0U);
            view->sync_error = (int16_t)Upper_ReadU16(payload + 72U);
            view->correction = (int16_t)Upper_ReadU16(payload + 74U);
            view->skew = Upper_ReadU16(payload + 76U);
            view->diagnostic = Upper_ReadU32(payload + 84U);
            view->lost_intervals = Upper_ReadU32(payload + 96U);
        }
        if (diagnostic != 0 && Upper_RecordFresh(snapshot, diagnostic, now))
        {
            unsigned axis;
            view->diagnostic_fresh = 1U;
            for (axis = 0U; axis < 2U; ++axis)
            {
                const uint8_t *payload = diagnostic->payload + 8U + axis * 24U;
                view->servo_valid[axis] = payload[2] ? payload[0] : 0U;
                view->servo_stale[axis] = payload[1];
                {
                    unsigned field;
                    uint32_t transit = now - diagnostic->timestamp;
                    for (field = 0U; field < 4U; ++field)
                        if ((uint64_t)Upper_ReadU16(payload + 12U + field * 2U) + transit +
                            Upper_ReadU32(diagnostic->payload + 60U) > 3000U)
                            view->servo_stale[axis] |= (uint8_t)(1U << field);
                }
                view->torque[axis] = Upper_ReadU16(payload + 4U);
                view->load[axis] = Upper_ReadU16(payload + 6U);
                view->current[axis] = Upper_ReadU16(payload + 8U);
                view->voltage[axis] = Upper_ReadU16(payload + 10U);
            }
        }
        if (statistics != 0 && Upper_RecordFresh(snapshot, statistics, now))
            view->bus_drops = Upper_ReadU32(statistics->payload + 12U);
    }
    view->page = page; view->selected_rpm = presets[selected]; view->link = snapshot->state;
    view->quick_stop = control->quick_stop;
    view->recovery_reason = control->recovery_reason;
    view->stop_verify_mask = control->stop_verify.mask;
    view->boot = snapshot->lower_boot_id; view->link_token = snapshot->link_token;
    view->owner = snapshot->control_source; view->life = snapshot->lifecycle;
    view->axes = snapshot->axes; view->gates = snapshot->gates;
    view->hb_age = snapshot->heartbeat_seen ? now - snapshot->heartbeat_at : 0xFFFFFFFFUL;
    view->last_id = snapshot->last_rx_id; view->last_rx_age = now - snapshot->last_rx_at;
    view->errors = snapshot->rejected; view->records = snapshot->record_errors;
    view->actual = Upper_BasicFresh(snapshot, 0U, now) ? snapshot->actual_mm_s : (int16_t)-32768;
    view->left = Upper_BasicFresh(snapshot, 6U, now) ? snapshot->left_rpm : (int16_t)-32768;
    view->right = Upper_BasicFresh(snapshot, 7U, now) ? snapshot->right_rpm : (int16_t)-32768;
    view->left_fault = Upper_BasicFresh(snapshot, 6U, now) ? snapshot->left_fault : 65535U;
    view->right_fault = Upper_BasicFresh(snapshot, 7U, now) ? snapshot->right_fault : 65535U;
    view->state = control->state; view->target_rpm = control->target_rpm; view->target_mm_s = control->target_mm_s;
    view->operation_detail = control->operation_detail; view->first_error = control->first_error;
    view->tx_failure = control->tx_failure; view->recovery_required = control->recovery_required;
    view->feedback_valid = (uint8_t)(snapshot->axes != 0U && snapshot->axes <= 3U);
    {
        unsigned axis;
        for (axis = 0U; axis < 2U; ++axis)
        {
            const uint8_t *data = snapshot->basic[6U + axis].data;
            if ((snapshot->axes & (1U << axis)) && (!Upper_BasicFresh(snapshot, (uint8_t)(6U + axis), now) ||
                (data[0] & 31U) != 31U || (data[0] & 32U) || data[1] != 2U || Upper_ReadU16(data + 4U)))
                view->feedback_valid = 0U;
        }
    }
    view->detail = control->detail; view->sequence = control->speed_sequence;
    view->last_reason = control->prepare_last_reason;
    if (record != 0 && Upper_RecordFresh(snapshot, record, now))
    {
        view->fresh = 1U; view->motion_token = Upper_ReadU16(record->payload + 4U);
        view->flags = Upper_ReadU32(record->payload + 12U); view->authorized = (uint8_t)((view->flags & 8U) != 0U);
        view->stop_phase = record->payload[34]; view->stop_result = record->payload[35];
        view->lease = (uint16_t)(Upper_ReadU32(record->payload + 16U) < 65535U ? Upper_ReadU32(record->payload + 16U) : 65535U);
    }
    else { view->stop_phase = view->stop_result = 255U; }
    if (stop != 0 && Upper_RecordFresh(snapshot, stop, now))
    {
        view->stop_event = Upper_ReadU32(stop->payload); view->reason = stop->payload[4];
        view->zero_mask = stop->payload[6];
    }
    if (Upper_BasicFresh(snapshot, 6U, now) && (snapshot->basic[6].data[0] & 10U) == 10U) view->enable_mask |= 1U;
    if (Upper_BasicFresh(snapshot, 7U, now) && (snapshot->basic[7].data[0] & 10U) == 10U) view->enable_mask |= 2U;
}
static const char *Owner(uint8_t owner)
{
    static const char *names[] = {"NONE","REMOTE","CLI","HMI","CAN","SAFETY"};
    return owner <= 5U ? names[owner] : "UNKNOWN";
}
static unsigned long Count(uint32_t value)
{
    return (unsigned long)(value > 999999U ? 999999U : value);
}
static void Number(int16_t value, char *text, unsigned capacity)
{
    if (value == (int16_t)-32768) snprintf(text, capacity, "--");
    else snprintf(text, capacity, "%d", value);
}
static void FormatLine(const UpperUiView_t *view, uint8_t line, char *text, uint32_t capacity)
{
    char left[8], right[8];
    if (text == NULL || capacity == 0U) return;
    text[0] = 0;
    if (view == NULL || line > 7U || view->page > 9U) return;
    if (capacity > UPPER_OLED_LINE_CHARS + 1U) capacity = UPPER_OLED_LINE_CHARS + 1U;
    Number(view->left, left, sizeof(left)); Number(view->right, right, sizeof(right));
    if (line == 0U) { snprintf(text, capacity, "%s P%u/10", UPPER_CAN_MOTION_ENABLED ? "CAN LOW SPEED" : "CAN READONLY", view->page + 1U); return; }
    if (line == 7U) { snprintf(text, capacity, "K1:NEXT K3:QUICK"); return; }
    switch (view->page)
    {
        case 0U:
            if (line == 1U) snprintf(text, capacity, "CAN500K %s", Upper_StateName(view->link));
            if (line == 2U) snprintf(text, capacity, "BOOT:%08lX", (unsigned long)view->boot);
            if (line == 3U) snprintf(text, capacity, "LINK:%04X HB:%lu", view->link_token, Count(view->hb_age));
            if (line == 4U) snprintf(text, capacity, "OWNER:%s", Owner(view->owner));
            if (line == 5U) snprintf(text, capacity, "ID:%03X AGE:%lu", view->last_id, Count(view->last_rx_age));
            if (line == 6U) snprintf(text, capacity, "ERR:%lu REC:%lu", Count(view->errors), Count(view->records));
            break;
        case 1U:
            if (line == 1U) snprintf(text, capacity, "OWNER:%s LIFE:%u", Owner(view->owner), view->life);
            if (line == 2U) snprintf(text, capacity, "AUTH:%s %s", view->fresh ? (view->authorized ? "CAN-YES" : "CAN-NO") : "UNKNOWN", view->fresh ? "LIVE" : "STALE");
            if (line == 3U) snprintf(text, capacity, "MOTION:%04X", view->motion_token);
            if (line == 4U) snprintf(text, capacity, "AXIS:%u GATE:%02X", view->axes, view->gates);
            if (line == 5U) snprintf(text, capacity, "FLAGS:%08lX", (unsigned long)view->flags);
            if (line == 6U) snprintf(text, capacity, "LEASE:%u", view->lease);
            break;
        case 2U:
            if (line == 1U) snprintf(text, capacity, "%s", UpperControl_StateName(view->state));
            if (line == 2U) snprintf(text, capacity, "SELECT:%+d rpm", view->selected_rpm);
            if (line == 3U) snprintf(text, capacity, "TARGET:%+d rpm", view->target_rpm);
            if (line == 4U) snprintf(text, capacity, "WIRE:%+d mm/s", view->target_mm_s);
            if (line == 5U) snprintf(text, capacity, "SEQ:%u ERR:%u", view->sequence, view->detail);
            if (line == 6U)
            {
                if (view->state == SIM_STOPPED_RELEASED) snprintf(text, capacity, "STOPPED / K1 AUTH");
                else if (view->state == SIM_FAULT && view->detail == 111U)
                    snprintf(text, capacity, "LINK CHANGED / SAFE");
                else if (view->state == SIM_FAULT && (view->detail == 114U || view->detail == 115U))
                    snprintf(text, capacity, "VERIFY:%03lX", (unsigned long)view->stop_verify_mask);
                else if (view->state == SIM_CROSS_SOURCE_STOP || view->state == SIM_RECOVERY_VERIFY || view->detail == 115U)
                    snprintf(text, capacity, "%s", UpperControl_RecoveryName(view->recovery_reason));
                else if (view->state == SIM_FAULT && view->recovery_required)
                    snprintf(text, capacity, view->detail == 110U ? "TX FAIL:%u K3 STOP" : "FAULT:%u K3 STOP",
                        view->detail == 110U ? view->tx_failure : view->detail);
                else if (view->operation_detail == 103U) snprintf(text, capacity, "ZERO FIRST:APPLY 0");
                else if (view->operation_detail == 104U)
                    snprintf(text, capacity, "%s", view->feedback_valid ? "WAIT ZERO" : "FB INVALID");
                else if (view->detail >= 201U && view->detail <= 212U)
                    snprintf(text, capacity, "%s/%u", UpperControl_DetailName(view->detail == 212U ? view->last_reason : view->detail), view->detail);
                else if (view->state == SIM_STOPPING || view->state == SIM_STOP_REQUESTED)
                    snprintf(text, capacity, "%s", view->quick_stop ? "QUICK STOP 0.60" : "NORMAL STOP 0.50");
                else snprintf(text, capacity, "K2:TAP RUN/HOLD STOP");
            }
            break;
        case 3U:
            /* A signed 16-bit value needs at most six characters. Bound each
             * formatted field so the 21-column OLED line remains provably safe. */
            if (line == 1U) snprintf(text, capacity, "L:%.6s R:%.6s rpm", left, right);
            if (line == 2U) { Number(view->actual, left, sizeof(left)); snprintf(text, capacity, "ACTUAL:%s mm/s", left); }
            if (line == 3U) snprintf(text, capacity, "L-FAULT:%04X", view->left_fault);
            if (line == 4U) snprintf(text, capacity, "R-FAULT:%04X", view->right_fault);
            if (line == 5U) snprintf(text, capacity, "ENABLE:%u AX:%u", view->enable_mask, view->axes);
            if (line == 6U) snprintf(text, capacity, "FFFF/-- = UNKNOWN");
            break;
        case 4U:
            if (line == 1U) snprintf(text, capacity, "STOP PH:%u RES:%u", view->stop_phase, view->stop_result);
            if (line == 2U) snprintf(text, capacity, "EVENT:%lu", (unsigned long)view->stop_event);
            if (line == 3U) snprintf(text, capacity, "REASON:%u", view->reason);
            if (line == 4U) snprintf(text, capacity, "ZERO:%u EN-FB:%u", view->zero_mask, view->enable_mask);
            if (line == 5U) snprintf(text, capacity, "%s", UpperControl_StateName(view->state));
            if (line == 6U) snprintf(text, capacity, "DETAIL:%u", view->detail);
            break;
        case 6U:
            if (line == 1U) snprintf(text, capacity, "POSITION %s", view->position_fresh ? "LIVE" : "STALE");
            if (view->position_fresh) {
                if (line == 2U) { if (view->position[0] == INT32_MIN) snprintf(text, capacity, "L:--"); else snprintf(text, capacity, "L:%ld", (long)view->position[0]); }
                if (line == 3U) { if (view->position[1] == INT32_MIN) snprintf(text, capacity, "R:--"); else snprintf(text, capacity, "R:%ld", (long)view->position[1]); }
                if (line == 4U) snprintf(text, capacity, "NET:%ldmm", (long)view->net_mm);
                if (line == 5U) snprintf(text, capacity, "PATH:%lumm", (unsigned long)view->path_mm);
                if (line == 6U) snprintf(text, capacity, "TRIP:%lumm", (unsigned long)view->trip_mm);
            }
            break;
        case 7U:
            if (line == 1U) snprintf(text, capacity, "SYNC %s", view->sync_fresh ? "LIVE" : "STALE");
            if (view->sync_fresh) {
                if (line == 2U) snprintf(text, capacity, "ERROR:%d rpm", view->sync_error);
                if (line == 3U) snprintf(text, capacity, "CORRECT:%d rpm", view->correction);
                if (line == 4U) snprintf(text, capacity, "SKEW:%u ms", view->skew);
            }
            if (line == 5U) snprintf(text, capacity, "SEQ:%lu", (unsigned long)view->snapshot_sequence);
            if (line == 6U) snprintf(text, capacity, "AGE:%lu ms", (unsigned long)view->snapshot_age);
            break;
        case 8U:
            if (line == 1U) snprintf(text, capacity, "SERVO %s", view->diagnostic_fresh ? "LIVE" : "STALE");
            if (line >= 2U && line <= 5U) {
                unsigned field = line - 2U;
                const char *labels[] = {"TQ", "LOAD", "CUR", "VOLT"};
                const uint16_t *values = field == 0U ? view->torque : field == 1U ? view->load : field == 2U ? view->current : view->voltage;
                unsigned axis;
                for (axis = 0U; axis < 2U; ++axis) {
                    char *output = axis ? right : left;
                    if (!view->diagnostic_fresh || !(view->servo_valid[axis] & (1U << field)) ||
                        (view->servo_stale[axis] & (1U << field))) snprintf(output, 8U, "--");
                    else snprintf(output, 8U, "%d", (int16_t)values[axis]);
                }
                snprintf(text, capacity, "%s:%s/%s", labels[field], left, right);
            }
            break;
        case 9U:
            if (line == 1U) snprintf(text, capacity, "SNAP %s %s", view->snapshot_fresh ? "LIVE" : "STALE", view->snapshot_fresh ? Owner(view->global_owner) : "--");
            if (line == 2U) snprintf(text, capacity, "SEQ:%lu", (unsigned long)view->snapshot_sequence);
            if (line == 3U) snprintf(text, capacity, "AGE:%lums", (unsigned long)view->snapshot_age);
            if (line == 4U) snprintf(text, capacity, "DIAG:%08lX", (unsigned long)view->diagnostic);
            if (line == 5U) snprintf(text, capacity, "DROP:%lu", (unsigned long)view->bus_drops);
            if (line == 6U) snprintf(text, capacity, "LOST:%lu", (unsigned long)view->lost_intervals);
            if (line == 7U) snprintf(text, capacity, view->snapshot_fresh ? "TGT:%d mm/s" : "TGT:--", view->global_target);
            break;
        default:
            if (line == 1U) snprintf(text, capacity, "%s", UPPER_BUILD_ID);
            if (line == 2U) snprintf(text, capacity, "RECORD:%lu", Count(view->records));
            if (line == 3U) snprintf(text, capacity, "LAST:%03X", view->last_id);
            if (line == 4U) snprintf(text, capacity, "AGE:%lu", Count(view->last_rx_age));
            if (line == 5U) snprintf(text, capacity, "FIRST:%u ERR:%u", view->first_error, view->detail);
            if (line == 6U) snprintf(text, capacity, UPPER_USART1_DEBUG_ENABLE ? "UART:SUMMARY" : "UART:OFF");
            break;
    }
}

void UpperUi_Format(const UpperUiView_t *view, uint8_t line, char *text, uint32_t capacity)
{
    if (text == NULL || capacity == 0U) return;
    if (capacity > UPPER_OLED_LINE_CHARS + 1U) capacity = UPPER_OLED_LINE_CHARS + 1U;
    FormatLine(view, line, text, capacity);
    text[capacity - 1U] = '\0';
}
