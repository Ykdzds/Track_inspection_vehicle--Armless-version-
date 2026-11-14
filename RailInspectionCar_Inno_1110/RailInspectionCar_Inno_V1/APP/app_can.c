#include "app_can.h"
#include "app_can_config.h"
#include "app_can_status.h"
#include "app_snapshot_ports.h"
#include "app_config.h"
#include "app_link_diag.h"
#include "app_health.h"
#include "app_nrf_remote.h"
#include "app_rtos_create.h"
#include "bsp_can_identity.h"
#include "app_boot_identity.h"
#include "app_can_control.h"
#include "app_arm3.h"
#include "app_arm3_port.h"
#include "app_arm3_diagnostics.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static bool parse_arm_joint_command(const char *line, uint8_t *arm_id,
                                    uint8_t *joint_id)
{
    unsigned int arm, joint;
    char tail;
    if (line == NULL || arm_id == NULL || joint_id == NULL) return false;
    if (sscanf(line, "can arm joint %u %u %c", &arm, &joint, &tail) != 2)
        return false;
    if (arm < 1U || arm > 2U || joint >= 32U) return false;
    *arm_id = (uint8_t)arm;
    *joint_id = (uint8_t)joint;
    return true;
}

static app_can_status_t s_public;
static can_upper_cache_t s_upper;
static can_arm_cache_t s_arm;

#if APP_CAN_ENABLED
static can_link_t s_link;
static can_identity_t s_identity;
/* 大快照与记录缓存均使用静态空间，避免占用CAN任务栈。 */
static app_can_status_input_t s_input;
static can_upper_snapshot_t s_clock;
static uint64_t s_monotonic_ms, s_clock_received_us;
static uint32_t s_clock_tick, s_transport_generation;
static uint32_t s_basic_due[8], s_record_due[8];
static uint32_t s_collect_ms, s_ready_generation;
static uint32_t s_arm_safety_due;
static uint32_t s_snapshot_due, s_servo_diagnostic_due, s_recovery_diagnostic_due;
static uint32_t s_stale_rx;
static bool s_collected, s_clock_pending, s_snapshot_supported;
static uint8_t s_heartbeat_sequence;
static uint8_t s_change_basic[2][8], s_change_owner[36];
static uint32_t s_position_sequence[2], s_position_generation[2];
static bool s_change_basic_seen[2], s_change_owner_seen, s_position_seen[2];
static can_protocol_record_t s_change_record, s_arm_tx, s_binding_reply, s_arm_urgent;
static bool s_arm_tx_pending, s_arm_urgent_pending;
static uint8_t s_arm_urgent_priority;
static uint32_t s_arm_urgent_built, s_arm_link_errors;
static uint8_t s_arm_tx_priority;
static uint32_t s_arm_tx_built;

#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
/* Observer scalars only; wire timestamps are captured by BSP, not task time. */
static struct {
    uint32_t service_at, service_gap_max, tx_processed_at, tx_delay_max;
    uint32_t segment_ready_at, segment_delay_max, record_duration_max;
    uint32_t critical_duration_max, critical_gap_max[4], last_failure_at;
    uint32_t failures, failure_reason, failure_processed_at;
    bool service_seen, segment_ready;
} s_timing;
#endif

#if APP_LINK_DIAG_ACTIVE && APP_CAN_DEBUG_ENABLED
static uint8_t s_log_lines;
static uint32_t s_log_window, s_log_skipped;
#endif

static uint64_t monotonic(uint32_t now)
{
    /* Accept forward progress and normal wrap, never an older sample. */
    if ((int32_t)(now - s_clock_tick) > 0)
    {
        s_monotonic_ms += (uint32_t)(now - s_clock_tick);
        s_clock_tick = now;
    }
    return s_monotonic_ms * 1000U;
}

static void log_frame(const bsp_can_frame_t *frame, uint8_t direction)
{
#if APP_LINK_DIAG_ACTIVE && APP_CAN_DEBUG_ENABLED
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_log_window) >= 1000U)
    {
        s_log_window = now;
        s_log_lines = 0U;
    }
    taskENTER_CRITICAL();
    if (s_log_lines >= 20U)
        ++s_log_skipped;
    else
    {
        ++s_log_lines;
        app_link_diag_record(APP_LINK_CAN_RAW,0x300U,frame->timestamp_ms,
            frame->id | ((uint32_t)frame->dlc<<16U) | ((uint32_t)direction<<24U),
            can_protocol_read_u32(frame->data),can_protocol_read_u32(frame->data+4U),
            s_link.generation,s_link.token);
    }
    taskEXIT_CRITICAL();
#else
    (void)frame;
    (void)direction;
#endif
}

static bool send_frame(void *context, uint16_t id, const uint8_t data[8],
                       uint32_t cookie, uint32_t deadline)
{
    bsp_can_frame_t frame;
    (void)context;
    frame.id = id;
    frame.dlc = 8U;
    frame.timestamp_ms = HAL_GetTick();
    memcpy(frame.data, data, 8U);
    if (!bsp_can_send(&frame, cookie, deadline)) return false;
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
    if (id >= 0x300U && id <= 0x302U && s_timing.segment_ready) {
        /* A cancelled record may leave a ready marker. A new START is not
         * the continuation of that old record. */
        if(id!=0x300U){
            uint32_t delay = frame.timestamp_ms - s_timing.segment_ready_at;
            if (delay > s_timing.segment_delay_max) s_timing.segment_delay_max = delay;
        }
        s_timing.segment_ready = false;
    }
#endif
    log_frame(&frame, 3U);
    return true;
}

static void cancel_frame(void *context, uint32_t cookie)
{
    (void)context;
    bsp_can_cancel(cookie);
}

static void invalidate_upper(void *context)
{
    (void)context;
    s_arm_tx_pending=s_arm_urgent_pending=false;
    taskENTER_CRITICAL();
    can_upper_init(&s_upper);
    can_arm_invalidate(&s_arm);
    taskEXIT_CRITICAL();
    s_clock_pending = false;
}

static bool publish_upper(void *context, const can_protocol_record_t *record,
                          uint32_t sample_ms)
{
    bool accepted;
    (void)context;
    if(record->type==0x5EU && record->length==48U && record->payload[8]==10U){
        if(!s_link.arm3_profile || !app_can_control_stop_binding(record,&s_binding_reply,HAL_GetTick()))return false;
        return can_link_enqueue_payload(&s_link,&s_binding_reply,2U,HAL_GetTick(),HAL_GetTick()+200U);
    }
    if(record->type==0x5CU || record->type==0x5EU || record->type==0x5FU ||
       record->type==0x47U || record->type==0x48U || record->type==0x55U){
        bool arm_accepted=app_arm3_receive(record,record->received_start_ms,record->received_commit_ms,HAL_GetTick());
        if(record->type!=0x47U && record->type!=0x48U && record->type!=0x55U)return arm_accepted;
    }
    taskENTER_CRITICAL();
    if (record->type == 0x47U || record->type == 0x48U ||
        record->type == 0x55U)
        accepted = can_arm_publish(&s_arm, record, sample_ms);
    else
        accepted = can_upper_publish(&s_upper, record, sample_ms,
                                     s_link.lower_boot_id, s_link.upper_boot_id);
    taskEXIT_CRITICAL();
    if (accepted && record->type == 0x45U)
    {
        uint32_t now = HAL_GetTick();
        (void)can_upper_get(&s_upper, 0x45U, sample_ms, &s_clock);
        s_clock_received_us = monotonic(now) -
            (uint64_t)(uint32_t)(now - sample_ms) * 1000U;
        s_clock_pending = true;
        (void)can_link_request_record(&s_link, 0x46U);
    }
    return accepted;
}

static bool build_record(void *context, uint8_t type,
                         can_protocol_record_t *record, uint32_t now)
{
    (void)context;
    s_input.now_ms = now;
    s_input.token = s_link.token;
    s_input.upper_boot_id = s_link.upper_boot_id;
    s_input.can_link_ready = s_link.phase == CAN_LINK_READY;
    if(type==0x53U && s_link.arm3_profile){app_arm3_build_safety(record,now);return true;}
    if (type == 0x46U)
    {
        if (!s_clock_pending) return false;
        memset(record, 0, sizeof(*record));
        record->length = 32U;
        memcpy(record->payload, s_clock.payload, 4U);
        can_protocol_write_u32(record->payload + 4U, 1U);
        memcpy(record->payload + 8U, s_clock.payload + 8U, 8U);
        can_protocol_write_u64(record->payload + 16U, s_clock_received_us);
        can_protocol_write_u64(record->payload + 24U, monotonic(now));
        s_clock_pending = false;
        return true;
    }
    return app_can_status_record(&s_input, type, record);
}

static void describe(void *context, uint8_t *work, uint8_t *owner)
{
    (void)context;
    *work = s_input.work;
    *owner = s_input.owner;
}



static void control_link_update(void)
{
    static can_upper_snapshot_t capability;
    bool strategy;
    taskENTER_CRITICAL();
    strategy = can_upper_get(&s_upper, 0x02U, HAL_GetTick(), &capability) &&
        capability.valid && (can_protocol_read_u32(capability.payload + 8U) & (1UL << 29U)) != 0U;
    s_snapshot_supported = capability.present && capability.valid &&
        (can_protocol_read_u32(capability.payload + 8U) & (1UL << 30U)) != 0U;
    taskEXIT_CRITICAL();
    app_arm3_link(s_link.lower_boot_id,s_link.upper_boot_id,s_link.token,s_link.phase==CAN_LINK_READY,
        capability.valid?can_protocol_read_u32(capability.payload+8U):0U,HAL_GetTick());
    {bool negotiated=app_arm3_negotiated();
     can_link_arm3_profile(&s_link,negotiated);app_can_control_arm3_revision(negotiated);}
    app_can_control_link(s_link.token, s_link.generation, s_link.upper_boot_id,
        s_link.last_heartbeat_ms, s_link.phase == CAN_LINK_READY, strategy);
}

#if APP_CAN_CONTROL_ENABLED
static uint8_t submit_control(void *context, const uint8_t data[8], uint32_t time, uint32_t generation)
{ (void)context; control_link_update(); return app_can_control_submit(data, time, generation); }
static bool submit_speed(void *context, const uint8_t data[8], uint32_t time)
{ (void)context; control_link_update(); return app_can_control_speed(data, time); }
static bool submit_stop(void *context, const uint8_t data[8], uint32_t time)
{ bool ok; (void)context; control_link_update(); ok=app_can_control_stop(data,time); if(ok)app_arm3_port_stop(); return ok; }
#endif

static void collect(uint32_t now)
{
    bool valid;
    valid = app_system_snapshot_read_can(&s_input);
    if (!valid) s_input.source_valid = false;
    s_input.now_ms = now;
    s_input.boot_id = s_link.lower_boot_id;
    s_input.upper_boot_id = s_link.upper_boot_id;
    s_input.config_revision = s_link.config_revision;
    s_input.token = s_link.token;
    s_input.can_link_ready = s_link.phase == CAN_LINK_READY;
    s_input.rejected = s_link.rejected;
    s_input.telemetry_dropped = s_link.queue_dropped;
    s_input.reassembly_failed = s_link.upper_invalid;

    app_can_status_describe(&s_input);
    s_collect_ms = now;
    s_collected = true;
}

static void schedule(uint32_t now)
{
    static const uint16_t ids[8] =
        {0x180U, 0x181U, 0x182U, 0x183U, 0x185U, 0x186U, 0x187U, 0x188U};
    static const uint16_t periods[8] = {100U, 500U, 1000U, 500U, 100U, 100U, 100U, 100U};
    static const uint8_t types[8] = {0x11U, 0x12U, 0x13U, 0x20U, 0x21U, 0x22U, 0x23U, 0x24U};
    static const uint16_t record_periods[8] = {50U, 500U, 200U, 150U, 150U, 150U, 50U, 1000U};
    static const uint16_t legacy_periods[8] = {500U, 1000U, 200U, 1000U, 1000U, 1000U, 200U, 1000U};
    uint8_t i, data[8];
    bool ready = s_link.phase == CAN_LINK_READY;
    if (ready && s_ready_generation != s_link.generation)
    {
        memset(s_basic_due, 0, sizeof(s_basic_due));
        memset(s_record_due, 0, sizeof(s_record_due));
        s_arm_safety_due = 0U;
        s_snapshot_due = s_servo_diagnostic_due = s_recovery_diagnostic_due = 0U;
        memset(s_change_basic_seen, 0, sizeof(s_change_basic_seen));
        memset(s_position_seen, 0, sizeof(s_position_seen));
        s_change_owner_seen = false;
        s_ready_generation = s_link.generation;
    }
    for (i = 0U; i < 8U; ++i)
    {
        uint32_t period = i == 4U ? APP_CAN_HEARTBEAT_PERIOD_MS : periods[i];
        /* 确认后即发送下层心跳，避免双方等待READY造成握手死锁。 */
        if (!ready && !(i == 4U && s_link.phase == CAN_LINK_WAIT_HEARTBEAT)) continue;
        {
            uint8_t change_index = i == 2U ? 0U : 1U;
            bool change_frame = i == 2U || i == 7U;
            bool encoded = app_can_status_basic(&s_input, ids[i],
                s_heartbeat_sequence, data);
            bool changed = change_frame && encoded &&
                (!s_change_basic_seen[change_index] ||
                 memcmp(data, s_change_basic[change_index], 8U) != 0);
            bool due = s_basic_due[i] == 0U ||
                (uint32_t)(now - s_basic_due[i]) >= period ||
                (changed && (uint32_t)(now - s_basic_due[i]) >= 50U);
            /* 变化帧至多20Hz；发送槽忙时保留变化，下一轮继续申请最新快照。 */
            if (due && encoded)
            {
                if (i == 4U)
                {
                    ++s_heartbeat_sequence;
                    data[1] = s_heartbeat_sequence;
                }
                if (can_link_offer_basic(&s_link, ids[i], data, now + period))
                {
                    s_basic_due[i] = now;
                    if (change_frame)
                    {
                        memcpy(s_change_basic[change_index], data, 8U);
                        s_change_basic_seen[change_index] = true;
                    }
                }
            }
        }
        {
            bool changed = false;
            if (ready && i == 0U &&
                app_can_status_record(&s_input, 0x11U, &s_change_record))
            {
                /* 排除持续变化的剩余期限、年龄和采样时间，只比较控制状态字段。 */
                changed = !s_change_owner_seen ||
                    memcmp(s_change_record.payload, s_change_owner, 16U) != 0 ||
                    memcmp(s_change_record.payload + 24U, s_change_owner + 24U, 12U) != 0;
            }
            if (ready && (i == 3U || i == 4U))
            {
                uint8_t axis = (uint8_t)(i - 3U);
                changed = !s_position_seen[axis] ||
                    s_position_sequence[axis] != s_input.axis[axis].position_sequence ||
                    s_position_generation[axis] != s_input.axis[axis].position_generation;
            }
            if (ready && (s_record_due[i] == 0U ||
                (uint32_t)(now - s_record_due[i]) >=
                    (i == 0U && (s_input.life == 2U || s_input.life == 3U ||
                     s_input.life == 4U || (s_input.runtime & 135U) != 0U) ? (s_snapshot_supported ? 50U : 100U) :
                     (i == 2U && s_input.life == 4U ? 100U : (s_snapshot_supported ? record_periods[i] : legacy_periods[i]))) ||
                (changed && (uint32_t)(now - s_record_due[i]) >= 50U)))
            {
                if ((types[i] != 0x13U || s_input.life == 4U ||
                    s_input.stop.event != 0U ||
                    s_input.remote.controlled_stop_state != REMOTE_CONTROLLED_STOP_IDLE || s_input.control.stop_phase != 0U) &&
                    can_link_request_record(&s_link, types[i]))
                {
                    s_record_due[i] = now;
                    if (i == 0U)
                    {
                        memcpy(s_change_owner, s_change_record.payload, 36U);
                        s_change_owner_seen = true;
                    }
                    if (i == 3U || i == 4U)
                    {
                        uint8_t axis = (uint8_t)(i - 3U);
                        s_position_sequence[axis] = s_input.axis[axis].position_sequence;
                        s_position_generation[axis] = s_input.axis[axis].position_generation;
                        s_position_seen[axis] = true;
                    }
                }
            }
        }
    }
    /* 联锁快照独立于普通扩展记录调度，始终保持无许可状态并优先发送。 */
    if (ready && s_snapshot_supported && (s_snapshot_due == 0U || (uint32_t)(now - s_snapshot_due) >= 50U) &&
        can_link_request_record(&s_link, 0x25U)) s_snapshot_due = now;
    if (ready && s_snapshot_supported && (s_servo_diagnostic_due == 0U ||
        (uint32_t)(now - s_servo_diagnostic_due) >= 400U) &&
        can_link_request_record(&s_link, 0x26U)) s_servo_diagnostic_due = now;
    if (ready && s_snapshot_supported && (s_recovery_diagnostic_due == 0U ||
        (uint32_t)(now - s_recovery_diagnostic_due) >= 400U) &&
        can_link_request_record(&s_link, 0x27U))
        s_recovery_diagnostic_due = now;
    if (ready && !s_link.arm3_profile && (s_arm_safety_due == 0U ||
        (uint32_t)(now - s_arm_safety_due) >= 100U) &&
        can_link_request_record_priority(&s_link, 0x53U))
        s_arm_safety_due = now;
}

static bool arm_prepare(void *context,can_protocol_record_t *record,uint32_t built,uint32_t now)
{(void)context;return app_arm3_prepare_record(record,built,now);}
static void arm_record_complete(void *context,const can_protocol_record_t *record,uint32_t now)
{
    (void)context;
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
    {
        static const uint8_t critical[4] = {0x11U,0x25U,0x13U,0x53U};
        uint32_t duration = now-s_link.record_started_ms; unsigned i;
        if(duration>s_timing.record_duration_max)s_timing.record_duration_max=duration;
        for(i=0U;i<4U;++i)if(record->type==critical[i]){
            uint32_t gap=now-s_link.critical_completed_ms[i];
            if(duration>s_timing.critical_duration_max)s_timing.critical_duration_max=duration;
            if(s_link.critical_completed_ms[i] && gap>s_timing.critical_gap_max[i])s_timing.critical_gap_max[i]=gap;
        }
        s_timing.segment_ready=false;
    }
#endif
#if APP_LINK_DIAG_ACTIVE && APP_CAN_ARM_DIAG_ENABLED
    if(record->type==0x54U || record->type==0x56U || record->type==0x57U || record->type==0x5FU)
        app_link_diag_record(APP_LINK_CAN_ARM,0x208U,now,record->type,record->sequence,
            record->length,record->length>=4U?can_protocol_read_u32(record->payload):0U,
            record->length>=8U?can_protocol_read_u32(record->payload+4U):0U);
#endif
    app_arm3_record_transmitted(record,now);
}
static void record_failed(void *context,uint8_t reason,uint32_t now)
{
    (void)context;
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
    uint32_t w[16];uint8_t source;
    s_timing.last_failure_at=now;++s_timing.failures;s_timing.failure_reason=reason;
    s_timing.failure_processed_at=HAL_GetTick();s_timing.segment_ready=false;
    w[0]=now;w[1]=reason | ((uint32_t)s_link.record.type<<8U) | ((uint32_t)s_link.record_index<<16U);
    w[2]=s_link.record.sequence | ((uint32_t)s_link.record.length<<16U);w[3]=s_link.record_cookie;
    w[4]=s_link.record_started_ms;w[5]=s_link.record_built_ms;w[6]=s_link.record_start_tx_ms;w[7]=s_link.record_last_tx_ms;
    w[8]=s_timing.tx_processed_at;w[9]=HAL_GetTick();w[10]=s_link.generation;w[11]=s_link.token;
    w[12]=s_link.record_active | ((uint32_t)s_link.record_pending<<1U) | ((uint32_t)s_link.record_cancelled<<2U) |
        ((uint32_t)s_link.record_has_payload<<3U) | ((uint32_t)s_link.record_retries<<8U);
    w[13]=s_link.record_deadline_ms;w[14]=s_timing.service_gap_max;w[15]=s_timing.tx_delay_max;
    if(reason==2U){ /* Payload preparation precedes allocation of a TX identity. */
        w[1]=reason | ((uint32_t)s_link.record.type<<8U) | 0xFFFF0000UL;
        w[3]=w[4]=w[6]=w[7]=UINT32_MAX;w[12]=0U;
    }
    for(source=APP_LINK_CAN_SERVO;source<=APP_LINK_CAN_ARM;++source){
        if((source==APP_LINK_CAN_SERVO && !APP_CAN_SERVO_DIAG_ENABLED) ||
           (source==APP_LINK_CAN_ARM && !APP_CAN_ARM_DIAG_ENABLED))continue;
        app_link_diag_record(source,0x111U,w[0],w[1],w[2],w[3],w[4],w[7]);
        app_link_diag_freeze(source,0x111U,w,16U);
    }
#else
    (void)reason;(void)now;
#endif
}

static void arm_service(uint32_t now)
{
    unsigned i;
    uint32_t errors=s_link.start_budget_failed+s_link.record_failed;
    if(errors!=s_arm_link_errors){s_arm_link_errors=errors;app_arm3_transport_fault(now);}
    app_arm3_port_service(now);
    if(s_link.phase!=CAN_LINK_READY){s_arm_tx_pending=s_arm_urgent_pending=false;return;}
    /* Poll independent urgent slots before retrying a full normal queue. */
    for(i=0U;i<2U;++i){
        if(!s_arm_urgent_pending){
            if(!app_arm3_take_urgent(&s_arm_urgent,&s_arm_urgent_priority,&s_arm_urgent_built))break;
            s_arm_urgent_pending=true;
        }
        if(s_arm_urgent.type==0x54U || (s_arm_urgent.type==0x53U && !s_arm_urgent.payload[8])){
            can_link_cancel_arm_motion(&s_link);
            if(s_arm_tx_pending && (s_arm_tx.type==0x54U || s_arm_tx.type==0x56U || s_arm_tx.type==0x57U || s_arm_tx.type==0x53U))s_arm_tx_pending=false;
        }
        if(!can_link_enqueue_payload(&s_link,&s_arm_urgent,0U,s_arm_urgent_built,s_arm_urgent_built+200U))break;
        s_arm_urgent_pending=false;
    }
    for(i=0U;i<4U;++i){
        if(!s_arm_tx_pending){
            if(!app_arm3_take_record(&s_arm_tx,&s_arm_tx_priority,&s_arm_tx_built))break;
            s_arm_tx_pending=true;
        }
        if(s_link.phase!=CAN_LINK_READY){s_arm_tx_pending=false;break;}
        if(s_arm_tx.type==0x54U && s_arm_tx.payload[9]==3U)can_link_cancel_arm_motion(&s_link);
        if(!can_link_enqueue_payload(&s_link,&s_arm_tx,s_arm_tx_priority,s_arm_tx_built,s_arm_tx_built+200U))break;
        s_arm_tx_pending=false;
    }
}
static void publish_status(uint32_t now)
{
    app_can_status_t status;
    bsp_can_get_stats(&status.bus);
    status.enabled = true;
    status.task_created = true;
    status.boot_id = s_link.lower_boot_id;
    status.upper_boot_id = s_link.upper_boot_id;
    status.revision = s_link.config_revision;
    status.generation = s_link.generation;
    status.token = s_link.token;
    status.phase = s_link.phase;
    status.identity_result = s_identity.result;
    status.rejected = s_link.rejected;
    status.queue_dropped = s_link.queue_dropped;
    status.record_failed = s_link.record_failed;
    status.upper_invalid = s_link.upper_invalid;
    status.stale_rx = s_stale_rx;
    status.last_service_ms = now;
    status.log_skipped = 0U;
#if APP_LINK_DIAG_ACTIVE && APP_CAN_DEBUG_ENABLED
    status.log_skipped = s_log_skipped;
#endif
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED)
    if(status.phase!=s_public.phase || status.generation!=s_public.generation ||
       status.bus.bus_off_count!=s_public.bus.bus_off_count || status.bus.rx_dropped!=s_public.bus.rx_dropped ||
       status.record_failed!=s_public.record_failed || status.upper_invalid!=s_public.upper_invalid)
    {
        uint32_t w[16];uint8_t source;
        bool fault=(s_public.phase==CAN_LINK_READY && status.phase!=CAN_LINK_READY) ||
            status.bus.bus_off_count!=s_public.bus.bus_off_count || status.bus.rx_dropped!=s_public.bus.rx_dropped ||
            status.record_failed!=s_public.record_failed;
        w[0]=now;w[1]=(uint32_t)s_public.phase | ((uint32_t)status.phase<<8U) | ((uint32_t)status.bus.phase<<16U);
        w[2]=status.generation;w[3]=status.token;w[4]=status.bus.bus_off_count;w[5]=status.bus.last_esr;
        w[6]=status.bus.rx_dropped;w[7]=status.record_failed;w[8]=status.upper_invalid;
        w[9]=status.boot_id;w[10]=status.upper_boot_id;w[11]=status.rejected;
        w[12]=status.queue_dropped;w[13]=status.stale_rx;w[14]=status.bus.recovery_count;w[15]=s_public.generation;
        for(source=APP_LINK_CAN_SERVO;source<=APP_LINK_CAN_ARM;++source){
            if((source==APP_LINK_CAN_SERVO && !APP_CAN_SERVO_DIAG_ENABLED) ||
               (source==APP_LINK_CAN_ARM && !APP_CAN_ARM_DIAG_ENABLED))continue;
            app_link_diag_record(source,0x110U,w[0],w[1],w[2],w[3],w[4],w[5]);
            if(fault)app_link_diag_freeze(source,0x110U,w,16U);
        }
    }
#endif
    taskENTER_CRITICAL();
    s_public = status;
    taskEXIT_CRITICAL();
}

typedef struct {
    uint32_t spent_cycles;
    uint8_t results, submitted;
} can_fast_budget_t;

/* Independent from maintenance wall time. No waiting and at most three actual
 * submissions/results per task pass, even when called between display steps. */
static void fast_service_run(can_fast_budget_t *quota,uint32_t cycle_budget,bool allow_send)
{
    bsp_can_tx_result_t result;
    uint32_t started=DWT->CYCCNT;
    unsigned steps;
    for(steps=0U;steps<6U;++steps){
        uint32_t before;bool progress=false;
        if(quota->spent_cycles+(uint32_t)(DWT->CYCCNT-started)>=cycle_budget)break;
        if(quota->results<3U && bsp_can_take_tx_result(&result)){
            uint32_t processed=HAL_GetTick();
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
            uint32_t old_index=s_link.record_index;
            bool record_match=s_link.record_active && s_link.record_pending && s_link.record_cookie==result.cookie;
            uint32_t delay=processed-result.frame.timestamp_ms;
            s_timing.tx_processed_at=processed;
            if(delay>s_timing.tx_delay_max)s_timing.tx_delay_max=delay;
#endif
            can_link_tx_result(&s_link,result.cookie,result.success,result.frame.timestamp_ms);
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
            if(record_match && result.success && s_link.record_active && s_link.record_index>old_index){
                s_timing.segment_ready_at=result.frame.timestamp_ms;s_timing.segment_ready=true;
            }
#endif
            log_frame(&result.frame,result.success?1U:2U);
            ++quota->results;progress=true;
            (void)processed;
        }
        if(quota->spent_cycles+(uint32_t)(DWT->CYCCNT-started)>=cycle_budget)break;
        if(allow_send && quota->submitted<3U && bsp_can_ready()){
            before=s_link.submitted_count;
            can_link_service(&s_link,HAL_GetTick());
            if(before!=s_link.submitted_count){++quota->submitted;progress=true;}
        }
        if(!progress)break;
    }
    quota->spent_cycles+=(uint32_t)(DWT->CYCCNT-started);
}

static void fast_service(can_fast_budget_t *quota,uint32_t cycle_budget)
{ fast_service_run(quota,cycle_budget,true); }

/* Non-reentrant CAN-task-only adapter; the pointer is valid only while the
 * presentation service runs, and never escapes to an ISR or another task. */
static can_fast_budget_t *s_display_quota;
static void display_progress(void)
{
    if(s_display_quota)fast_service(s_display_quota,SystemCoreClock/5000U);
}

static void can_task(void *argument)
{
    bsp_can_frame_t frame;
    bsp_can_stats_t bus;
    uint32_t now,start_cycles,budget,count,result_count,last_dropped=0U;
    app_can_control_result_t control_result;
    (void)argument;
    bsp_can_init(xTaskGetCurrentTaskHandle(),HAL_GetTick());
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    budget=SystemCoreClock/5000U;
    for(;;){
        can_fast_budget_t quota={0U,0U,0U};
        now=HAL_GetTick();(void)monotonic(now);
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
        if(s_timing.service_seen){uint32_t gap=now-s_timing.service_at;
            if(gap>s_timing.service_gap_max)s_timing.service_gap_max=gap;}
        s_timing.service_seen=true;s_timing.service_at=now;
#endif
        bsp_can_service(now);bsp_can_get_stats(&bus);
        if(bus.rx_dropped!=last_dropped){app_can_control_safety();app_arm3_transport_fault(now);last_dropped=bus.rx_dropped;}
        if(s_transport_generation!=bus.generation){
            app_arm3_transport_fault(now);s_transport_generation=bus.generation;
            can_link_transport_reset(&s_link,now);s_ready_generation=0U;
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
            s_timing.segment_ready=false;
#endif
        }
        /* Consume queued heartbeat/STOP frames before checking protocol
         * deadlines, preserving the receive-before-timeout boundary. */
        fast_service_run(&quota,budget,false);
        start_cycles=DWT->CYCCNT;
        for(count=0U;count<8U && bsp_can_receive(&frame);++count){
            uint32_t maximum_age=frame.id>=0x310U?200U:APP_CAN_LINK_TIMEOUT_MS;
            now=HAL_GetTick();
            if((uint32_t)(now-frame.timestamp_ms)>maximum_age)++s_stale_rx;
            else{log_frame(&frame,0U);can_link_receive(&s_link,frame.id,frame.data,frame.timestamp_ms);}
            if((uint32_t)(DWT->CYCCNT-start_cycles)>=budget){++count;break;}
        }
        /* Each maintenance step is bracketed by the same bounded TX budget. */
        fast_service(&quota,budget);
        now=HAL_GetTick();
        if(!s_collected || (uint32_t)(now-s_collect_ms)>=10U)collect(now);
        fast_service(&quota,budget);
        control_link_update();
        arm_service(HAL_GetTick());
        fast_service(&quota,budget);
        for(result_count=0U;result_count<4U && app_can_control_take_result(&control_result);++result_count)
            if(control_result.link_generation==s_link.generation && control_result.link_token==s_link.token)
                can_link_control_result(&s_link,control_result.sequence,control_result.command,
                    control_result.result,control_result.phase,control_result.detail,HAL_GetTick());
        now=HAL_GetTick();
        if(bsp_can_ready())schedule(now);
        fast_service(&quota,budget);
        /* Presentation is incremental; safety and input maintenance above is
         * never throttled by the 20 ms publication cadence. */
        s_display_quota=&quota;
        app_arm3_port_display_service(HAL_GetTick(),display_progress);
        s_display_quota=NULL;
        publish_status(HAL_GetTick());
        if(count>=8U)vTaskDelay(1U);
        else(void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(2U)!=0U?pdMS_TO_TICKS(2U):1U);
    }
}
#endif

void app_can_init(void)
{
    memset(&s_public, 0, sizeof(s_public));
#if APP_CAN_ENABLED
    s_snapshot_supported = false;
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
    memset(&s_timing,0,sizeof(s_timing));
#endif
#endif
    can_upper_init(&s_upper);
    can_arm_init(&s_arm);
    app_arm3_port_init(app_boot_identity_id());
    s_public.phase = CAN_LINK_IDENTITY_FAILED;
    s_public.identity_result = CAN_IDENTITY_WRITE_FAILED;
#if APP_CAN_ENABLED
    {
        can_link_callbacks_t callbacks;
        uint32_t now = HAL_GetTick();
        memset(&callbacks, 0, sizeof(callbacks));
        memset(&s_input, 0, sizeof(s_input));
        s_input.minimum_stack = 65535U;
        callbacks.send = send_frame;
        callbacks.cancel = cancel_frame;
        callbacks.build_record = build_record;
        callbacks.publish_upper = publish_upper;
        callbacks.invalidate_upper = invalidate_upper;
        callbacks.record_complete = arm_record_complete;
        callbacks.record_failure = record_failed;
        callbacks.prepare_payload = arm_prepare;
        callbacks.describe = describe;
#if APP_CAN_CONTROL_ENABLED
        callbacks.submit_control = submit_control;
        callbacks.submit_speed = submit_speed;
        callbacks.submit_stop = submit_stop;
#endif
        s_clock_tick = now;
        s_monotonic_ms = now;
        s_public.enabled = true;
        s_identity = *app_boot_identity_get();
        can_link_init(&s_link, &callbacks, s_identity.boot_id,
            s_identity.first_token, s_identity.last_token, s_identity.revision,
            app_can_capabilities(), APP_CAN_LINK_TIMEOUT_MS, now);
        s_public.identity_result = s_identity.result;
        s_public.boot_id = s_link.lower_boot_id;
        s_public.revision = s_link.config_revision;
        s_public.generation = s_link.generation;
        s_public.phase = s_link.phase;
    }
#endif
}

void app_can_task_create(void)
{
#if APP_CAN_ENABLED
    /* 可选通信任务失败不触发全局诊断或assert，更不能阻断伺服任务。 */
    s_public.task_created = xTaskCreate(can_task, "can", 768U, NULL,
                                      3U, &can_task_handle) == pdPASS;
#endif
}

bool app_can_get_status(app_can_status_t *status)
{
    if (status == NULL) return false;
    taskENTER_CRITICAL();
    *status = s_public;
    taskEXIT_CRITICAL();
    return true;
}

bool app_can_get_upper(uint8_t type, can_upper_snapshot_t *snapshot)
{
    bool result;
    if (snapshot == NULL) return false;
    taskENTER_CRITICAL();
    result = can_upper_get(&s_upper, type, HAL_GetTick(), snapshot);
    taskEXIT_CRITICAL();
    return result;
}

bool app_can_get_snapshot(app_can_status_t *status,
                          can_upper_snapshot_t upper[CAN_UPPER_SLOT_COUNT])
{
    static const uint8_t types[CAN_UPPER_SLOT_COUNT] =
        {0x02U, 0x40U, 0x41U, 0x42U, 0x43U, 0x44U, 0x45U};
    uint8_t i;
    uint32_t now;
    if (status == NULL || upper == NULL) return false;
    taskENTER_CRITICAL();
    now = HAL_GetTick();
    *status = s_public;
#if APP_CAN_ENABLED
    /* 链路刚切换而统计尚未发布时，也必须使用当前会话身份。 */
    status->phase = s_link.phase;
    status->generation = s_link.generation;
    status->upper_boot_id = s_link.upper_boot_id;
    status->token = s_link.token;
#endif
    for (i = 0U; i < CAN_UPPER_SLOT_COUNT; i++)
    {
        memset(&upper[i], 0, sizeof(upper[i]));
        (void)can_upper_get(&s_upper, types[i], now, &upper[i]);
    }
    taskEXIT_CRITICAL();
    return true;
}

bool app_can_get_arm_summary(uint8_t arm_id, can_arm_summary_t *summary)
{
    bool result;
    taskENTER_CRITICAL();
    result = can_arm_get_summary(&s_arm, arm_id, HAL_GetTick(), summary);
    taskEXIT_CRITICAL();
    return result;
}

bool app_can_get_arm_joint(uint8_t arm_id, uint8_t joint_id,
                           can_arm_joint_t *joint)
{
    bool result;
    taskENTER_CRITICAL();
    result = can_arm_get_joint(&s_arm, arm_id, joint_id, HAL_GetTick(), joint);
    taskEXIT_CRITICAL();
    return result;
}

bool app_can_get_arm_result(can_arm_result_t *result)
{
    bool valid;
    taskENTER_CRITICAL();
    valid = can_arm_get_result(&s_arm, HAL_GetTick(), result);
    taskEXIT_CRITICAL();
    return valid;
}

void app_can_get_arm_stats(can_arm_stats_t *stats)
{
    taskENTER_CRITICAL();
    can_arm_get_stats(&s_arm, stats);
    taskEXIT_CRITICAL();
}

bool app_can_cli_command(const char *line)
{
    app_can_status_t status;
    uint8_t arm_id, joint_id;
    bool arm_joint = parse_arm_joint_command(line, &arm_id, &joint_id);
    if (strcmp(line, "can status") != 0 && strcmp(line, "can stat") != 0 &&
        strcmp(line, "can upper status") != 0 &&
        strcmp(line, "can control status") != 0 &&
        strcmp(line, "can arm status") != 0 &&
        strcmp(line, "can arm3 status") != 0 &&
        strcmp(line, "can arm result") != 0 && !arm_joint) return false;
    (void)app_can_get_status(&status);
    if (strcmp(line, "can control status") == 0)
    {
        static app_can_control_status_t c;
        (void)app_can_control_get_status(&c);
        printf("can control enabled=%u approved=%u state=%u authorized=%u token=%u generation=%lu batch=%lu\r\n",
            APP_CAN_CONTROL_ENABLED, APP_CAN_STOP_PARAMETERS_APPROVED, (unsigned)c.state,
            c.authorized, c.motion_token, (unsigned long)c.generation, (unsigned long)c.batch);
        printf("can control strategy=%u axis=%u target=%d actual=%d valid=%u lease=%lu age=%lu limit=%u reject=%lu detail=%u\r\n",
            c.strategy_ready, c.axis_mask, c.target_mm_s, c.actual_mm_s, c.actual_valid,
            (unsigned long)c.remaining_ms, (unsigned long)c.speed_age_ms, c.limit_mm_s,
            (unsigned long)c.rejected, c.detail);
        printf("can stop generation=%lu phase=%u result=%u reason=%u zero=%u failed=%u elapsed=%lu timeout=%lu\r\n",
            (unsigned long)c.stop_generation, c.stop_phase, c.stop_result, c.stop_reason,
            c.zero_mask, c.failed_mask, (unsigned long)c.stop_elapsed_ms, (unsigned long)c.stop_timeout_ms);
        printf("can axis pending=%u left=%d/%d seq=%lu right=%d/%d seq=%lu duplicates=%lu conflicts=%lu full=%lu grants=%lu\r\n",
            c.pending_mask, c.commanded_rpm[0], c.pending_rpm[0], (unsigned long)c.write_sequence[0],
            c.commanded_rpm[1], c.pending_rpm[1], (unsigned long)c.write_sequence[1],
            (unsigned long)c.duplicates, (unsigned long)c.conflicts, (unsigned long)c.queue_full, (unsigned long)c.grants);
    }
    else if (strcmp(line, "can arm status") == 0)
    {
        can_arm_stats_t stats;
        can_arm_summary_t summary;
        uint8_t arm;
        app_can_get_arm_stats(&stats);
        printf("can arm stats invalid=%lu replaced=%lu result=%lu config=%lu\r\n",
            (unsigned long)stats.invalid_records,
            (unsigned long)stats.joint_replaced,
            (unsigned long)stats.result_updates,
            (unsigned long)stats.config_changes);
        for (arm = 1U; arm <= 2U; ++arm)
        {
            bool present = app_can_get_arm_summary(arm, &summary);
            printf("can arm id=%u present=%u valid=%u state=%u flags=%04X age_ms=%lu joints=%u config=%lu\r\n",
                arm, present, present && summary.valid,
                present ? summary.state : 255U,
                present ? summary.flags : 0U,
                (unsigned long)(present ? summary.sample_age_ms : UINT32_MAX),
                present ? summary.joint_count : 65535U,
                (unsigned long)(present ? summary.config_version : 0U));
        }
    }
    else if (strcmp(line, "can arm3 status") == 0)
    {
        static app_arm3_port_status_t arm3;
        unsigned i;
        bool present=app_arm3_port_get_status(&arm3, HAL_GetTick());
        printf("can arm3 snapshot present=%u age_ms=%lu (state at capture)\r\n",
            present, (unsigned long)arm3.age_ms);
        if(!present)return true;
        printf("can arm3 link=%u negotiated=%u profile=%u safety=%u safety_ver=%lu "
               "owner=%u permit=%u work_lock=%u queue=%u stop_pending=%u reason=%lu\r\n",
            arm3.arm.link_ready, arm3.arm.negotiated,
            arm3.platform.profile_enabled, arm3.arm.safety_valid,
            (unsigned long)arm3.arm.safety_version, arm3.arm.owner,
            arm3.arm.permit_mask, arm3.arm.work_locked,
            arm3.input_queue_depth, arm3.stop_pending,
            (unsigned long)arm3.arm.reason);
        printf("can arm3 config_match=%u chassis_busy=%u parameter_lock=%u "
               "platform_flags=%08lX/%08lX/%08lX sample_age=%lu zero_stable=%u "
               "feedback_age=%u\r\n",
            arm3.arm.config_match, arm3.arm.chassis_busy,
            arm3.arm.parameter_locked,
            (unsigned long)arm3.platform.hardware_bitmap,
            (unsigned long)arm3.platform.mapped_input_mask,
            (unsigned long)arm3.platform.config_flags,
            (unsigned long)arm3.platform.sample_age_ms,
            arm3.platform.chassis_zero_stable_ms,
            arm3.platform.chassis_feedback_age_ms);
        for(i=0U;i<2U;++i)
        {
            const app_arm3_side_snapshot_t *side=&arm3.arm.side[i];
            printf("can arm3 side=%u online=%u active=%u stopping=%u "
                   "feedback=%u age=%lu op=%lu phase=%u result=%u "
                   "identity=%u config=%u transport=%u proof=%u\r\n",
                i+1U, side->online, side->active, side->stopping,
                side->feedback_real, (unsigned long)side->feedback_age_ms,
                (unsigned long)side->operation, side->phase, side->result,
                side->identity_valid, side->config_valid,
                side->transport_approved, side->proof_state);
        }
    }
    else if (strcmp(line, "can arm result") == 0)
    {
        can_arm_result_t result;
        bool present = app_can_get_arm_result(&result);
        printf("can arm result present=%u valid=%u op=%lu mask=%u phase=%u result=%u age_ms=%lu\r\n",
            present, present && result.valid,
            (unsigned long)(present ? result.operation_id : 0U),
            present ? result.arm_mask : 0U, present ? result.phase : 255U,
            present ? result.result : 255U,
            (unsigned long)(present ? result.sample_age_ms : UINT32_MAX));
    }
    else if (arm_joint)
    {
        can_arm_joint_t joint;
        bool present = app_can_get_arm_joint(arm_id, joint_id, &joint);
        printf("can arm joint arm=%u id=%u present=%u valid=%u age_ms=%lu "
            "flags=%02X actual=%ld target=%ld speed=%ld fault=%lu config=%lu\r\n",
            arm_id, joint_id, present, present && joint.valid,
            (unsigned long)(present ? joint.sample_age_ms : UINT32_MAX),
            present ? joint.flags : 0U,
            (long)(present ? joint.actual_angle : 0),
            (long)(present ? joint.target_angle : 0),
            (long)(present ? joint.actual_angular_speed : 0),
            (unsigned long)(present ? joint.fault_code : UINT32_MAX),
            (unsigned long)(present ? joint.config_version : 0U));
    }
    else if (strcmp(line, "can upper status") == 0)
    {
        static can_upper_snapshot_t upper;
        static const uint8_t types[7] = {0x02U, 0x40U, 0x41U, 0x42U, 0x43U, 0x44U, 0x45U};
        uint8_t i;
        for (i = 0U; i < 7U; ++i)
        {
            bool present = app_can_get_upper(types[i], &upper);
            printf("can upper type=%02X present=%u valid=%u age_ms=%lu seq=%u\r\n",
                types[i], present, present && upper.valid,
                (unsigned long)(present ? upper.age_ms : UINT32_MAX),
                present ? upper.sequence : 0U);
        }
    }
    else if (strcmp(line, "can stat") == 0)
    {
        printf("can rx=%lu tx_submit=%lu tx_ok=%lu tx_fail=%lu bus_off=%lu rx_drop=%lu\r\n",
            (unsigned long)status.bus.rx_count, (unsigned long)status.bus.tx_submitted,
            (unsigned long)status.bus.tx_success, (unsigned long)status.bus.tx_failed,
            (unsigned long)status.bus.bus_off_count, (unsigned long)status.bus.rx_dropped);
        printf("can reject=%lu queue_drop=%lu record_fail=%lu upper_invalid=%lu stale_rx=%lu log_skip=%lu esr=%08lX\r\n",
            (unsigned long)status.rejected, (unsigned long)status.queue_dropped,
            (unsigned long)status.record_failed, (unsigned long)status.upper_invalid,
            (unsigned long)status.stale_rx, (unsigned long)status.log_skipped,
            (unsigned long)status.bus.last_esr);
    }
    else
    {
        printf("can enabled=%u debug=%u task=%u bitrate=500000 bus=%u link=%u control=%u\r\n",
            status.enabled, APP_CAN_DEBUG_ENABLED, status.task_created,
            (unsigned int)status.bus.phase, (unsigned int)status.phase, APP_CAN_CONTROL_ENABLED);
        printf("can boot=%lu upper_boot=%lu token=%u revision=%lu generation=%lu identity=%u age_ms=%lu\r\n",
            (unsigned long)status.boot_id, (unsigned long)status.upper_boot_id,
            status.token, (unsigned long)status.revision, (unsigned long)status.generation,
            (unsigned int)status.identity_result,
            (unsigned long)(HAL_GetTick() - status.last_service_ms));
    }
    return true;
}

void app_can_cli_service(void)
{
    /* Automatic output is owned by the shared CLI diagnostic writer. */
}

#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
void app_can_diag_snapshot(app_link_diag_snapshot_t *out,uint8_t source,uint8_t phase)
{
    const app_can_status_t *v=&s_public;
    uint32_t *w;
    if(!out)return;
    out->count=0U;w=out->words;
    if(phase==3U){
#if APP_CAN_ENABLED
        taskENTER_CRITICAL();out->sampled_ms=HAL_GetTick();
        w[0]=s_timing.service_gap_max;w[1]=s_timing.tx_delay_max;w[2]=s_timing.segment_delay_max;
        w[3]=s_timing.record_duration_max;w[4]=s_timing.critical_duration_max;
        memcpy(w+5U,s_timing.critical_gap_max,sizeof(s_timing.critical_gap_max));
        w[9]=s_timing.failures;w[10]=s_timing.failure_reason;w[11]=s_timing.last_failure_at;
        w[12]=s_timing.failure_processed_at;w[13]=s_timing.tx_processed_at;
        w[14]=s_link.record.type | ((uint32_t)s_link.record_index<<8U) |
            ((uint32_t)s_link.record_pending<<24U) | ((uint32_t)s_link.record_cancelled<<25U);
        w[15]=s_link.record.sequence | ((uint32_t)s_link.record.length<<16U);
        w[16]=s_link.record_started_ms;w[17]=s_link.record_last_tx_ms;w[18]=s_link.submitted_count;
        out->count=19U;taskEXIT_CRITICAL();return;
#else
        out->sampled_ms=HAL_GetTick();return;
#endif
    }
    if(phase==0U || phase==1U){
        if(source==APP_LINK_CAN_SERVO)out->count=app_can_control_diag_words(w,phase,&out->sampled_ms);
        else if(source==APP_LINK_CAN_ARM)out->count=app_arm3_port_diag_words(w,phase,&out->sampled_ms);
        return;
    }
    taskENTER_CRITICAL();out->sampled_ms=v->last_service_ms;
    w[0]=v->enabled | ((uint32_t)v->task_created<<1U) | ((uint32_t)v->bus.phase<<8U) | ((uint32_t)v->phase<<16U);
    w[1]=v->boot_id;w[2]=v->upper_boot_id;w[3]=v->token;w[4]=v->generation;
    w[5]=v->bus.rx_count;w[6]=v->bus.tx_submitted;w[7]=v->bus.tx_success;w[8]=v->bus.tx_failed;
    w[9]=v->bus.bus_off_count;w[10]=v->bus.recovery_count;w[11]=v->bus.last_esr;
    w[12]=v->bus.rx_dropped;w[13]=v->bus.rx_fifo_overrun;w[14]=v->bus.rx_high_water | ((uint32_t)v->bus.rx_queued<<16U);
    w[15]=v->rejected;w[16]=v->queue_dropped;w[17]=v->record_failed;w[18]=v->upper_invalid;w[19]=v->stale_rx;
    w[20]=v->log_skipped;w[21]=v->bus.tx_expired;w[22]=v->bus.init_timeouts;
    w[23]=v->identity_result;out->count=24U;
    taskEXIT_CRITICAL();
}
#endif
