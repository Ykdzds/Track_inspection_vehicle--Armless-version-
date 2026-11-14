#ifndef CAN_UPPER_CONTROL_H
#define CAN_UPPER_CONTROL_H
#include "can_upper_protocol.h"
#include "can_upper_tx.h"
typedef enum { SIM_IDLE, SIM_LINK_NOT_READY, SIM_AUTH_REQUESTED, SIM_AUTHORIZED_WAIT_ZERO, SIM_AUTHORIZED_ZERO, SIM_RUNNING, SIM_STOP_REQUESTED, SIM_STOPPING, SIM_STOPPED_RELEASED, SIM_FAULT, SIM_AUTH_PREPARING, SIM_CROSS_SOURCE_STOP, SIM_RECOVERY_VERIFY } UpperControlState_t;
typedef enum { UPPER_INTENT_AUTHORIZE, UPPER_INTENT_APPLY, UPPER_INTENT_STOP, UPPER_INTENT_RELEASE, UPPER_INTENT_QUICK_STOP } UpperIntent_t;

#if UPPER_USART1_DEBUG_ENABLE
/* One immutable scene, independent of the lossy ordinary log queue. Ages are
 * elapsed local milliseconds at the decision, not reconstructed at print time. */
typedef enum { UPPER_STOP_AUTO_CONTROL = 1, UPPER_STOP_AUTO_AXIS,
    UPPER_STOP_AUTO_UNSAFE, UPPER_STOP_UI_NORMAL, UPPER_STOP_UI_QUICK,
    UPPER_STOP_UI_RELEASE, UPPER_STOP_AUTH_TIMEOUT, UPPER_STOP_ZERO_TIMEOUT } UpperStopSource_t;
typedef struct
{
    uint32_t at, event_at, lower_boot, upper_boot;
    uint32_t control_age, axis_age[2], heartbeat_age, flags, control_generation;
    uint32_t merged;
    uint16_t link_token, motion_token, control_sequence, speed_sequence, detail;
    int16_t target_rpm, target_mm_s;
    uint16_t axis_fault[2], stop_request;
    uint8_t source, recovery, state, validity, owner, life, axes;
    uint8_t key_event, key_mask, axis_flags[2], axis_mode[2];
    volatile uint8_t pending, page, armed;
} UpperStopFirst_t;
const UpperStopFirst_t *UpperControl_StopFirst(void);
void UpperControl_StopFirstSent(uint8_t page);
#endif
typedef struct
{
    uint32_t age;
    int16_t rpm;
    uint16_t fault;
    uint8_t flags, mode, fresh;
} UpperAxisGate_t;
typedef struct
{
    uint32_t mask, control_age, heartbeat_age;
    uint16_t reason;
    uint8_t owner, life, gates, axes, capability_sent, capability_confirmed;
    UpperAxisGate_t axis[2];
} UpperAuthGate_t;
typedef struct {
    uint32_t at, mask, flags, event, control_age, stop_age, axis_age[2], diagnostic;
    uint16_t token;
    uint8_t owner, life, phase, result;
} UpperStopVerify_t;

typedef struct
{
    UpperControlState_t state;
    UpperSend_t send;
    void *user;
    uint32_t lower_boot, upper_boot, request_at, retry_at, speed_at, tx_done_at;
    uint32_t stop_at, stop_baseline, stop_event, last_zero_record, last_control_record;
    uint16_t link_token, motion_token, speed_sequence, first_zero_sequence, stop_sequence;
    uint16_t detail, limit_mm_s, last_motion_token;
    uint16_t first_error;
    uint32_t first_error_at;
    int16_t target_rpm, target_mm_s, request_parameter;
    uint8_t request_sequence, command, accepted, completed, request_active;
    uint8_t zero_observations, tx_seen, zero_sent, stop_sent, stop_intent;
    uint8_t axes, stop_phase, stop_result, stop_reason, zero_mask, enable_mask;
    uint8_t attempts, result_code;
    uint32_t prepare_at;
    uint16_t prepare_last_reason, query_detail;
    uint8_t query_attempts, query_result, query_phase;
    UpperAuthGate_t gate;
    uint16_t operation_detail;
    int16_t rejected_rpm;
    uint32_t operation_at, operation_count;
    uint8_t tx_failure, recovery_required;
    uint8_t tx_retry_count, tx_retry_pending, cross_zero_observations;
    uint32_t tx_retry_at, cross_stop_at;
    uint32_t quick_retry_at;
    uint8_t quick_stop, quick_attempts, quick_ack;
    uint32_t motion_generation, cross_control_at, cross_axis_at[2];
    uint8_t cross_associated, recovery_hard, recovery_reason;
    /* Last failed release predicate. Frozen on timeout; reset by the next stop intent. */
    UpperStopVerify_t stop_verify;
#if UPPER_USART1_DEBUG_ENABLE
    /* 0=unfrozen, 1=pending UART, 2=delivered; no second scene buffer. */
    volatile uint8_t stop_failure_state;
    uint8_t stop_failure_detail;
    uint16_t stop_failure_sequence;
#endif
} UpperControl_t;
#if UPPER_USART1_DEBUG_ENABLE
void UpperControl_CaptureStop(UpperControl_t *control, const UpperProtocol_t *protocol,
    UpperStopSource_t source, uint16_t detail, uint8_t key_event, uint8_t key_mask,
    uint32_t event_at, uint32_t now);
#endif
void UpperControl_Init(UpperControl_t *control, UpperSend_t send, void *user);
void UpperControl_Intent(UpperControl_t *control, UpperProtocol_t *protocol, UpperIntent_t intent, int16_t rpm, uint32_t now);
void UpperControl_Receive(UpperControl_t *control, const UpperProtocol_t *protocol, uint16_t id, const uint8_t data[8], uint32_t received, uint32_t now);
void UpperControl_TxResult(UpperControl_t *control, uint16_t id, const uint8_t data[8], uint8_t success, uint32_t now);
void UpperControl_TxOutcome(UpperControl_t *control, uint16_t id, const uint8_t data[8], UpperTxOutcome_t outcome, uint32_t now);
void UpperControl_Tick(UpperControl_t *control, UpperProtocol_t *protocol, uint32_t now);
const UpperRecord_t *UpperControl_Record(const UpperSnapshot_t *snapshot, uint8_t type);
const char *UpperControl_StateName(UpperControlState_t state);
uint8_t UpperControl_Convert(const UpperSnapshot_t *snapshot, int16_t rpm, int16_t *mm_s);
void UpperControl_CheckAuthorization(const UpperProtocol_t *protocol, uint32_t now, uint32_t after, uint8_t require_new, UpperAuthGate_t *gate);
const char *UpperControl_DetailName(uint16_t detail);
const char *UpperControl_RecoveryName(uint8_t reason);
#endif
