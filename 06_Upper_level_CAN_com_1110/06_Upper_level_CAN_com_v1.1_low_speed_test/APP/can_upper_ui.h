#ifndef CAN_UPPER_UI_H
#define CAN_UPPER_UI_H
#include "can_upper_control.h"
#define UPPER_UI_PAGE_COUNT 10U
typedef struct
{
    UpperLinkState_t link;
    UpperControlState_t state;
    uint32_t boot, hb_age, last_rx_age, errors, records, stop_event, flags;
    uint16_t link_token, motion_token, last_id, detail, sequence, lease, last_reason;
    int16_t actual, left, right, target_rpm, target_mm_s, selected_rpm;
    uint16_t left_fault, right_fault;
    uint8_t page, owner, life, axes, gates, authorized, stop_phase, stop_result;
    uint8_t reason, zero_mask, enable_mask, fresh;
    uint8_t tx_failure, recovery_required, feedback_valid;
    uint16_t operation_detail, first_error;
    uint8_t quick_stop, recovery_reason;
    uint32_t snapshot_sequence, snapshot_age, snapshot_ms;
    int32_t position[2], net_mm;
    uint32_t path_mm, trip_mm, lost_intervals, diagnostic, bus_drops;
    int16_t global_target, sync_error, correction;
    uint16_t skew, torque[2], current[2], voltage[2], load[2];
    uint8_t snapshot_fresh, position_fresh, sync_fresh, diagnostic_fresh;
    uint8_t servo_valid[2], servo_stale[2];
    uint8_t global_owner;
    uint32_t stop_verify_mask;
} UpperUiView_t;
typedef enum { UPPER_UI_EVENT_PAGE, UPPER_UI_EVENT_SELECT, UPPER_UI_EVENT_STOP, UPPER_UI_EVENT_K1_LONG, UPPER_UI_EVENT_APPLY, UPPER_UI_EVENT_QUICK_STOP } UpperUiEventType_t;
/* ARMCC uses a one-byte enum here: keep key_mask in its alignment gap. */
typedef struct { UpperUiEventType_t type; uint8_t key_mask; int16_t rpm; uint32_t at; } UpperUiEvent_t;
void UpperUi_Init(void);
void UpperUi_ScanKeys(uint32_t now, uint8_t pressed_mask, UpperControl_t *control, UpperProtocol_t *protocol);
void UpperUi_ScanKeysOnly(uint32_t now, uint8_t pressed_mask);
uint8_t UpperUi_GetEvent(UpperUiEvent_t *event);
void UpperUi_ConsumeEvents(UpperControl_t *control, UpperProtocol_t *protocol);
void UpperUi_ConsumeEventsAt(UpperControl_t *control, UpperProtocol_t *protocol, uint32_t now);
void UpperUi_NotifyState(UpperControlState_t state);
void UpperUi_Publish(const UpperControl_t *control, const UpperProtocol_t *protocol, uint32_t now, UpperUiView_t *view);
void UpperUi_Format(const UpperUiView_t *view, uint8_t line, char *text, uint32_t capacity);
#endif
