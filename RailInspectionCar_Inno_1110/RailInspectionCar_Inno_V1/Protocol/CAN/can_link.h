#ifndef CAN_LINK_H
#define CAN_LINK_H

#include "can_protocol.h"

#define CAN_LINK_BASIC_SLOTS 32U
#define CAN_LINK_REQUEST_SLOTS 32U
#define CAN_LINK_PAYLOAD_SLOTS 8U
#define CAN_LINK_URGENT_SLOTS 2U

typedef struct {
    can_protocol_record_t record;
    uint32_t created_ms, deadline_ms;
    uint8_t priority;
    bool used;
} can_link_payload_t;

typedef enum
{
    CAN_LINK_IDENTITY_FAILED,
    CAN_LINK_WAIT_PEER,
    CAN_LINK_WAIT_CONFIRM,
    CAN_LINK_WAIT_HEARTBEAT,
    CAN_LINK_READY
} can_link_phase_t;

typedef struct
{
    /* 提交邮箱不等于发送成功，结果必须通过can_link_tx_result回送。 */
    bool (*send)(void *context, uint16_t id, const uint8_t data[8],
                 uint32_t cookie, uint32_t deadline_ms);
    /* 取消已提交事务仍须等待发送结果，不能立即复用扩展记录通道。 */
    void (*cancel)(void *context, uint32_t cookie);
    bool (*build_record)(void *context, uint8_t type,
                         can_protocol_record_t *record, uint32_t now_ms);
    bool (*publish_upper)(void *context, const can_protocol_record_t *record,
                          uint32_t sample_ms);
    void (*invalidate_upper)(void *context);
    bool (*prepare_payload)(void *context,can_protocol_record_t *record,uint32_t built_ms,uint32_t now_ms);
    void (*record_complete)(void *context,const can_protocol_record_t *record,uint32_t completed_ms);
    /* Observation only: called before failed-record state is cleared. */
    void (*record_failure)(void *context, uint8_t reason, uint32_t observed_ms);
    void (*describe)(void *context, uint8_t *work_mode, uint8_t *owner);
    uint8_t (*submit_control)(void *context, const uint8_t data[8], uint32_t sample_ms, uint32_t generation);
    bool (*submit_speed)(void *context, const uint8_t data[8], uint32_t sample_ms);
    bool (*submit_stop)(void *context, const uint8_t data[8], uint32_t sample_ms);
    void *context;
} can_link_callbacks_t;

typedef struct
{
    uint16_t id;
    uint8_t data[8];
    uint32_t cookie, deadline_ms;
    uint8_t retries;
    bool used, pending;
} can_link_basic_t;

typedef struct
{
    uint8_t request[32];
    uint8_t length;
    uint32_t created_ms;
    uint8_t result, phase;
    uint16_t detail;
    uint16_t record_sequence;
    bool record_seen;
    bool used;
} can_link_request_t;

typedef struct
{
    can_link_phase_t phase;
    uint32_t lower_boot_id, upper_boot_id, config_revision, capabilities;
    uint32_t generation, last_heartbeat_ms, confirmed_ms, next_boot_ms;
    uint32_t rejected, queue_dropped, record_failed, upper_invalid;
    uint32_t requests_completed, cookie_counter, heartbeat_timeout_ms;
    uint32_t rate_started_ms, query_deadline_ms, invite_created_ms;
    uint32_t response_started_ms;
    uint16_t token, next_token, token_limit, record_sequence, invite_sequence;
    uint16_t stop_sequence;
    uint8_t healthy_count, heartbeat_sequence, last_request_sequence;
    uint8_t rate_count, query_index, query_remaining, query_type;
    uint8_t response_count, control_response_count, record_cursor, basic_cursor;
    bool heartbeat_seen, request_seen, invite_delivered, invite_requested;
    bool confirm_requested, query_active, query_record, prefer_record;
    bool confirm_delivered;
    bool stop_seen, record_cancelled, transport_draining;
    uint32_t record_requested[3];
    can_link_callbacks_t callbacks;
    can_link_basic_t basic[CAN_LINK_BASIC_SLOTS];
    can_link_request_t requests[CAN_LINK_REQUEST_SLOTS];
    can_protocol_record_t record;
    can_protocol_reassembly_t reassembly;
    can_link_payload_t payload_queue[CAN_LINK_PAYLOAD_SLOTS + CAN_LINK_URGENT_SLOTS];
    uint32_t record_cookie, record_started_ms, record_built_ms, record_start_tx_ms;
    uint32_t submitted_count, record_last_tx_ms;
    uint32_t record_created_ms, record_deadline_ms;
    uint8_t record_priority;
    uint32_t payload_dropped, start_budget_failed;
    bool arm3_profile;
    bool record_has_payload;
    uint16_t record_index;
    uint8_t record_retries;
    bool record_active, record_pending, record_started;
    uint32_t critical_completed_ms[4], diagnostic_started_ms, last_record_duration_ms;
    uint8_t priority_type;
    bool priority_pending, priority_served;
    uint16_t upper_sequence;
    bool upper_sequence_seen;
} can_link_t;

void can_link_init(can_link_t *link, const can_link_callbacks_t *callbacks,
                   uint32_t boot_id, uint16_t first_token, uint16_t token_limit,
                   uint32_t config_revision, uint32_t capabilities,
                   uint32_t heartbeat_timeout_ms, uint32_t now_ms);
void can_link_transport_reset(can_link_t *link, uint32_t now_ms);
void can_link_receive(can_link_t *link, uint16_t id, const uint8_t data[8],
                      uint32_t sample_ms);
void can_link_service(can_link_t *link, uint32_t now_ms);
void can_link_tx_result(can_link_t *link, uint32_t cookie, bool success,
                        uint32_t now_ms);
bool can_link_offer_basic(can_link_t *link, uint16_t id,
                          const uint8_t data[8], uint32_t deadline_ms);
bool can_link_request_record(can_link_t *link, uint8_t type);
/* Copies the complete immutable payload. priority0=stop/revoke,1=lease,2=result/action,3=config/query. */
bool can_link_enqueue_payload(can_link_t *link, const can_protocol_record_t *record,
                              uint8_t priority, uint32_t now_ms, uint32_t deadline_ms);
void can_link_arm3_profile(can_link_t *link, bool enabled);
void can_link_cancel_arm_motion(can_link_t *link);
bool can_link_request_record_priority(can_link_t *link, uint8_t type);
void can_link_control_result(can_link_t *link, uint8_t sequence, uint8_t command,
                             uint8_t result, uint8_t phase, uint16_t detail, uint32_t now_ms);

#endif
