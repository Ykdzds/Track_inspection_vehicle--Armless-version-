#include "FreeRTOS.h"
#include "task.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

/* 直接编译生产APP，替换RTOS及硬件依赖，不复制调度算法。 */
#define APP_RTOS_CREATE_H
TaskHandle_t motion_task_handle, servo_comm_task_handle, health_task_handle;
TaskHandle_t key_task_handle, status_task_handle, cli_task_handle;
TaskHandle_t lcd_task_handle, iwdg_task_handle, nrf_task_handle, can_task_handle;
#include "../APP/app_can.c"
#include "link_diag_capture.h"

static uint32_t tick, flash_calls, create_calls;
static can_identity_t test_identity;
const can_identity_t *app_boot_identity_get(void) { return &test_identity; }
uint32_t app_boot_identity_id(void){return test_identity.boot_id;}
/* Transport fixture: board port is covered separately and by the real Keil build. */
void app_arm3_port_init(uint32_t boot){app_arm3_init(NULL);(void)boot;}
void app_arm3_port_service(uint32_t now){app_arm3_service(now);}
void app_arm3_port_display_service(uint32_t now,void (*progress)(void)){(void)now;if(progress)progress();}
void app_arm3_port_stop(void){}
bool app_arm3_port_get_status(app_arm3_port_status_t *status,uint32_t now)
{ (void)status; (void)now; return false; }
uint8_t app_arm3_port_diag_words(uint32_t *words,uint8_t phase,uint32_t *at)
{ (void)words;(void)phase;*at=tick;return 0U; }
static bool create_success;
test_core_debug_t test_core_debug;
test_dwt_t test_dwt;
uint32_t SystemCoreClock = 168000000U;
uint32_t HAL_GetTick(void) { return tick; }
TickType_t xTaskGetTickCount(void) { return tick; }

bool bsp_can_identity_startup(uint32_t fingerprint, can_identity_t *identity)
{
    (void)fingerprint;
    memset(identity, 0, sizeof(*identity));
    identity->result = CAN_IDENTITY_WRITE_FAILED;
    ++flash_calls;
    return false;
}
BaseType_t xTaskCreate(void (*function)(void *), const char *name,
    unsigned stack, void *argument, unsigned priority, TaskHandle_t *task)
{
    assert(function != NULL && strcmp(name, "can") == 0 && argument == NULL);
    assert(stack == 768U && priority == 3U);
    ++create_calls;
    *task = create_success ? (TaskHandle_t)1 : NULL;
    return create_success ? pdPASS : 0;
}

#if APP_CAN_ENABLED
static jmp_buf task_boundary;
static bool stop_at_wait;
static unsigned rx_remaining, rx_taken, tx_remaining, tx_taken;
static unsigned waits, delays;
static uint32_t rx_age, fast_mock_cost;
static app_hmi_control_status_t fake_hmi;
typedef struct {bsp_can_frame_t frame;uint32_t cookie,due;bool used;} async_mailbox_t;
static async_mailbox_t async_boxes[3];
static bool async_mode;
static unsigned async_submitted,async_completed,async_record_pending,async_record_high;
static void (*async_deliver)(const bsp_can_frame_t *frame);

bool bsp_can_send(const bsp_can_frame_t *frame, uint32_t cookie, uint32_t deadline)
{
    unsigned i;
    test_dwt.CYCCNT+=fast_mock_cost;
    if(!async_mode){(void)frame;(void)cookie;(void)deadline;return true;}
    for(i=0U;i<3U;++i)if(!async_boxes[i].used){
        assert((int32_t)(deadline-tick)>0);async_boxes[i].used=true;async_boxes[i].frame=*frame;
        async_boxes[i].cookie=cookie;async_boxes[i].due=tick+1U;++async_submitted;
        if(frame->id>=0x300U && frame->id<=0x302U){
            ++async_record_pending;if(async_record_pending>async_record_high)async_record_high=async_record_pending;
        }
        return true;
    }
    return false;
}
void bsp_can_cancel(uint32_t cookie) {
    unsigned i;
    if(async_mode)for(i=0U;i<3U;++i)if(async_boxes[i].used && async_boxes[i].cookie==cookie){
        async_boxes[i].due=tick;/* Result still required; timeout tests inspect cancellation separately. */
    }
}
void bsp_can_init(TaskHandle_t task, uint32_t now) { (void)task; (void)now; }
void bsp_can_service(uint32_t now) { (void)now; }
bool bsp_can_ready(void) { return true; }
bool bsp_can_receive(bsp_can_frame_t *frame)
{
    if (rx_remaining == 0U) return false;
    --rx_remaining;
    ++rx_taken;
    memset(frame, 0, sizeof(*frame));
    frame->id = 0x100U;
    frame->dlc = 8U;
    frame->timestamp_ms = tick - rx_age;
    return true;
}
bool bsp_can_take_tx_result(bsp_can_tx_result_t *result)
{
    test_dwt.CYCCNT+=fast_mock_cost;
    if(async_mode){
        unsigned i;
        for(i=0U;i<3U;++i)if(async_boxes[i].used && (int32_t)(tick-async_boxes[i].due)>=0){
            result->frame=async_boxes[i].frame;result->frame.timestamp_ms=async_boxes[i].due;
            result->cookie=async_boxes[i].cookie;result->success=true;async_boxes[i].used=false;++async_completed;
            if(result->frame.id>=0x300U && result->frame.id<=0x302U)--async_record_pending;
            if(async_deliver)async_deliver(&result->frame);
            return true;
        }
        return false;
    }
    if (tx_remaining == 0U) return false;
    --tx_remaining;
    ++tx_taken;
    memset(result, 0, sizeof(*result));
    return true;
}
void bsp_can_get_stats(bsp_can_stats_t *stats) { memset(stats, 0, sizeof(*stats)); }
void aimtor_get_recovery_status(servo_recovery_status_t *out) { memset(out, 0, sizeof(*out)); }
servo_result_t aimtor_request_recovery(uint8_t source) { (void)source; return SERVO_NOT_READY; }
bool aimtor_get_snapshot_pair(servo_snapshot_t *left, servo_snapshot_t *right)
{ memset(left, 0, sizeof(*left)); memset(right, 0, sizeof(*right)); return true; }
bool app_remote_servo_get_status(remote_servo_status_t *status)
{ memset(status, 0, sizeof(*status)); return true; }
bool app_cli_servo_session_get_status(cli_servo_session_status_t *status)
{ memset(status, 0, sizeof(*status)); return true; }
bool app_position_monitor_get_status(position_monitor_status_t *status)
{ memset(status, 0, sizeof(*status)); return true; }
void app_hmi_control_get_status(app_hmi_control_status_t *status)
{ *status = fake_hmi; }
uint8_t app_system_snapshot_read_can(app_can_status_input_t *input)
{
    (void)aimtor_get_snapshot_pair(&input->axis[0], &input->axis[1]);
    (void)app_remote_servo_get_status(&input->remote);
    (void)app_cli_servo_session_get_status(&input->cli);
    (void)app_position_monitor_get_status(&input->position);
    (void)app_can_control_get_status(&input->control);
    input->hmi = fake_hmi;
    input->hmi_remaining_ms = fake_hmi.session ? 900U : 0U;
    input->arbiter_owner = app_control_arbiter_owner();
    input->arbiter_generation = app_control_arbiter_generation();
    input->arbiter_axes = app_control_arbiter_axis_mask();
    input->lock_reason = app_control_arbiter_lock_reason();
    app_control_arbiter_get_stop(&input->stop);
    input->source_valid = true;
    input->snapshot_ms = tick;
    input->sample_tick = tick;
    input->selected = app_config_servo_control_axis_mask();
    input->gates = 31U;
    input->minimum_stack = 768U;
    return 1U;
}
uint16_t app_hmi_control_remaining_ms(void) { return fake_hmi.session ? 900U : 0U; }
#if !APP_CAN_CONTROL_ENABLED
app_control_owner_t app_control_arbiter_owner(void) { return APP_CONTROL_OWNER_NONE; }
uint32_t app_control_arbiter_generation(void) { return 0U; }
uint8_t app_control_arbiter_axis_mask(void) { return 0U; }
app_control_lock_reason_t app_control_arbiter_lock_reason(void) { return APP_CONTROL_LOCK_NONE; }
void app_control_arbiter_get_stop(app_control_stop_t *stop) { memset(stop, 0, sizeof(*stop)); }
uint8_t app_control_arbiter_arm_begin(void){return 0U;}
void app_control_arbiter_arm_lock(uint8_t v){(void)v;}
uint8_t app_control_arbiter_motion_pending(void){return 0U;}
uint8_t app_control_arbiter_parameter_locked(void){return 0U;}
uint8_t app_control_arbiter_preparing(void){return 0U;}
uint8_t app_control_arbiter_stopping(void){return 0U;}
uint8_t app_control_arbiter_submit_global_stop(app_control_owner_t o,uint16_t r,uint16_t d,uint32_t q,uint16_t c){(void)o;(void)r;(void)d;(void)q;(void)c;return 0U;}
#endif
bool aimtor_watchdog_recovery_get_status(servo_axis_t axis, servo_wdg_recovery_status_t *status)
{ (void)axis; memset(status, 0, sizeof(*status)); return true; }
uint8_t app_config_servo_control_axis_mask(void) { return 3U; }
uint32_t app_health_get_diagnostic_faults(void) { return 0U; }
uint32_t app_health_get_reset_reason(void) { return 0U; }
TickType_t app_health_get_heartbeat_age(app_heartbeat_t heartbeat) { (void)heartbeat; return 0U; }
app_heartbeat_t app_health_get_failed_heartbeat(void) { return APP_HEARTBEAT_COUNT; }
bool app_nrf_remote_get_snapshot(nrf_remote_snapshot_t *snapshot)
{ memset(snapshot, 0, sizeof(*snapshot)); return true; }
void debug_uart_get_stats(debug_uart_stats_t *stats) { memset(stats, 0, sizeof(*stats)); }
void modbus_get_stats(modbus_stats_t *stats) { memset(stats, 0, sizeof(*stats)); }
bool aimtor_startup_stop_ready(void) { return true; }
bool aimtor_watchdog_recovery_ready(void) { return true; }
bool aimtor_boot_zero_ready(void) { return true; }
bool aimtor_startup_hold_ready(void) { return true; }
#if APP_CAN_CONTROL_ENABLED
bool app_health_watchdog_test_requested(void) { return false; }
bool app_remote_servo_k1_latched(void) { return false; }
void app_remote_servo_can_k5_stop_begin(uint32_t event, uint8_t axes) { (void)event; (void)axes; }
bool app_remote_servo_can_k5_stop_complete(uint32_t event, uint8_t axes) { (void)event; (void)axes; return false; }
uint8_t app_config_motion_hardware_ready(void) { return 1U; }
uint8_t app_config_servo_bus_ready(void) { return 1U; }
bool aimtor_get_snapshot(servo_axis_t axis, servo_snapshot_t *out) { (void)axis; memset(out, 0, sizeof(*out)); return true; }
void aimtor_can_revoke(uint32_t generation) { (void)generation; }
void aimtor_set_can_control(bool enable, uint8_t mask) { (void)enable; (void)mask; }
bool aimtor_safety_stop_active(void) { return false; }
bool aimtor_recovery_blocks_motion(void) { return false; }
servo_result_t aimtor_remote_prepare_mode(servo_axis_t axis)
{ (void)axis; assert(0); return SERVO_NOT_READY; }
servo_result_t aimtor_remote_prepare_enable(servo_axis_t axis)
{ (void)axis; assert(0); return SERVO_NOT_READY; }
void aimtor_request_controlled_safety_stop(uint8_t offline) { (void)offline; }
void aimtor_emergency_stop(void) {}
servo_result_t aimtor_can_velocity(servo_axis_t axis, int16_t rpm, uint32_t generation, uint32_t batch, bool stop)
{ (void)axis; (void)rpm; (void)generation; (void)batch; (void)stop; assert(0); return SERVO_NOT_READY; }
void app_remote_servo_request_estop(void) {}
servo_result_t app_cli_servo_session_off(void) { return SERVO_OK; }
void app_hmi_control_request_stop(void) {}
servo_result_t aimtor_k1_hold_request(uint8_t mask) { (void)mask; return SERVO_NOT_READY; }
bool aimtor_k1_hold_get_status(servo_k1_hold_status_t *out) { memset(out, 0, sizeof(*out)); return true; }
servo_result_t aimtor_k1_hold_release(void) { return SERVO_NOT_READY; }
#endif
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 768U; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return NULL; }
void vTaskDelay(TickType_t ticks)
{
    assert(ticks == 1U);
    ++delays;
    tick += ticks;
    if (stop_at_wait) longjmp(task_boundary, 1);
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{
    assert(clear == pdTRUE && ticks == 2U);
    ++waits;
    tick += ticks;
    if (stop_at_wait) longjmp(task_boundary, 1);
    return 0U;
}
#if APP_LINK_DIAG_ACTIVE && APP_CAN_DEBUG_ENABLED
static void test_logs(void)
{
    bsp_can_frame_t frame = {1000U, 0x100U, 8U, {1,2,3,4,5,6,7,8}};
    unsigned i;
    tick = 1000U;
    diag_capture_reset();s_log_lines = 0U;
    s_log_window = s_log_skipped = 0U;
    for (i = 0U; i < 100U; ++i) log_frame(&frame, (uint8_t)(i % 4U));
    assert(diag_capture_count[0x300U] == 20U && s_log_skipped == 80U);
    assert(diag_capture_last[0]==1000U && diag_capture_last[2]==0x04030201U && diag_capture_last[3]==0x08070605U);
    app_can_cli_service();
    assert(diag_capture_count[0x300U]==20U); /* Writer ownership moved; no second consumer. */
    tick = 2000U;
    log_frame(&frame, 0U);
    assert(diag_capture_count[0x300U]==21U && s_log_skipped==80U);
}
#endif

static void run_task_iteration(void)
{
    stop_at_wait = true;
    if (setjmp(task_boundary) == 0) can_task(NULL);
    stop_at_wait = false;
}

static void test_task_boundaries(void)
{
    unsigned previous_delays, previous_waits;
    app_can_init();
    assert(flash_calls == 0U && s_public.enabled &&
        s_public.phase == CAN_LINK_IDENTITY_FAILED);
    app_can_task_create();
    assert(create_calls == 1U && !s_public.task_created);
    create_success = true;
    app_can_task_create();
    assert(create_calls == 2U && s_public.task_created);
    /* 直接执行生产任务一轮，在RTOS阻塞边界返回测试，不复制服务循环。 */
    tick = 5000U;
    rx_remaining = 64U;
    tx_remaining = 8U;
    rx_taken = tx_taken = 0U;
    previous_delays = delays;
    run_task_iteration();
    assert(rx_taken == 8U && tx_taken == 3U && delays == previous_delays + 1U);
    rx_remaining = 1U;
    tx_remaining = 0U;
    rx_age = APP_CAN_LINK_TIMEOUT_MS + 1U;
    s_stale_rx = 0U;
    previous_waits = waits;
    run_task_iteration();
    assert(s_stale_rx == 1U && waits == previous_waits + 1U);
    rx_age = 0U;
}

static void test_fast_quotas(void)
{
    can_fast_budget_t quota={0U,0U,0U};
    memset(&s_link,0,sizeof(s_link));s_link.callbacks.send=send_frame;
    s_link.lower_boot_id=0U;s_link.phase=CAN_LINK_READY;s_link.heartbeat_timeout_ms=1000U;
    s_link.heartbeat_seen=true;s_link.last_heartbeat_ms=tick;
    tx_remaining=10U;tx_taken=0U;test_dwt.CYCCNT=0U;
    fast_mock_cost=SystemCoreClock/5000U;
    fast_service(&quota,SystemCoreClock/5000U);
    assert(quota.results==1U && tx_taken==1U && !quota.submitted);
    fast_service(&quota,SystemCoreClock/5000U);assert(tx_taken==1U);
    fast_mock_cost=0U;quota.spent_cycles=0U;
    fast_service(&quota,SystemCoreClock/5000U);
    assert(quota.results==3U && tx_taken==3U);
    {
        uint8_t frame[8]={0U};unsigned i;
        quota.spent_cycles=0U;quota.results=quota.submitted=0U;tx_remaining=0U;
        memset(s_link.basic,0,sizeof(s_link.basic));
        for(i=0U;i<4U;++i)assert(can_link_offer_basic(&s_link,(uint16_t)(0x180U+i),frame,tick+100U));
        fast_service(&quota,SystemCoreClock/5000U);fast_service(&quota,SystemCoreClock/5000U);
        assert(quota.submitted==3U && s_link.submitted_count==3U);
    }
    puts("PASS repeated fast-service calls share 3-result/3-submission quota and 200us cycle budget");
}

#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
static void test_segment_timing_identity(void)
{
    uint8_t data[8]={0U};
    memset(&s_timing,0,sizeof(s_timing));tick=1000U;
    s_timing.segment_ready=true;s_timing.segment_ready_at=800U;
    assert(send_frame(NULL,0x300U,data,1U,1200U));
    assert(!s_timing.segment_ready && !s_timing.segment_delay_max);
    s_timing.segment_ready=true;s_timing.segment_ready_at=998U;
    assert(send_frame(NULL,0x301U,data,2U,1200U));
    assert(!s_timing.segment_ready && s_timing.segment_delay_max==2U);
    puts("PASS segment timing excludes a new record START after cancellation");
}
#endif

static bool has_basic(uint16_t id)
{
    unsigned i;
    for (i = 0U; i < CAN_LINK_BASIC_SLOTS; ++i)
        if (s_link.basic[i].used && s_link.basic[i].id == id) return true;
    return false;
}
static bool has_record(uint8_t type)
{
    return (s_link.record_requested[type / 32U] & (1UL << (type % 32U))) != 0U;
}
static void clear_offers(void)
{
    memset(s_link.basic, 0, sizeof(s_link.basic));
    memset(s_link.record_requested, 0, sizeof(s_link.record_requested));
}
static void test_schedule(void)
{
    memset(&s_link, 0, sizeof(s_link));
    memset(&s_input, 0, sizeof(s_input));
    memset(s_basic_due, 0, sizeof(s_basic_due));
    memset(s_record_due, 0, sizeof(s_record_due));
    s_link.phase = CAN_LINK_READY;
    s_snapshot_supported = true;
    s_link.generation = 1U;
    s_input.source_valid = true;
    tick = 1000U;
    s_input.now_ms = s_input.snapshot_ms = s_input.sample_tick = tick;
    schedule(tick);
    assert(has_basic(0x182U) && has_basic(0x188U));
    assert(has_record(0x11U) && has_record(0x20U) && has_record(0x21U));
    clear_offers();
    s_input.diagnostic = APP_DIAG_HEARTBEAT_TIMEOUT;
    s_input.owner = 2U;
    ++s_input.cli.generation;
    ++s_input.axis[0].position_sequence;
    schedule(1049U);
    assert(!has_basic(0x182U) && !has_record(0x11U) && !has_record(0x20U));
    schedule(1050U);
    assert(has_basic(0x182U) && has_record(0x11U) && has_record(0x20U));
    assert(!has_record(0x21U));
    clear_offers();
    schedule(1100U);
    assert(!has_basic(0x182U) && has_record(0x11U) && !has_record(0x20U));
    s_input.cli.remaining_ms = 2000U;
    s_input.now_ms = 1150U;
    schedule(1150U);
    assert(has_record(0x11U));
    ++s_link.generation;
    schedule(1151U);
    assert(has_record(0x11U) && has_basic(0x182U));
    assert(has_record(0x25U) && has_record(0x26U));
    s_snapshot_supported = false;
    ++s_link.generation;
    clear_offers();
    schedule(1152U);
    assert(!has_record(0x25U) && !has_record(0x26U));
    clear_offers();
    s_link.phase = CAN_LINK_WAIT_HEARTBEAT;
    schedule(1400U);
    assert(has_basic(0x185U) && !has_basic(0x180U) && !has_record(0x11U));
    clear_offers();
    s_link.phase = CAN_LINK_READY;
    s_basic_due[2] = UINT32_MAX - 20U;
    s_input.diagnostic = 0U;
    schedule(29U);
    assert(has_basic(0x182U));
}

static void test_hmi_collection_and_schedule(void)
{
#if APP_CAN_CONTROL_ENABLED
    uint32_t ticket;
    app_control_arbiter_init();
    ticket = app_control_arbiter_submit(APP_CONTROL_OWNER_HMI, 3U);
    assert(ticket);
    app_control_arbiter_service();
    assert(app_control_arbiter_request(APP_CONTROL_OWNER_HMI, 3U, NULL));
    fake_hmi.session = 31U; fake_hmi.state = 3U; fake_hmi.flags = 2U;
    collect(tick);
    assert(s_input.owner == 3U && s_input.hmi.session == 31U);
    assert(s_input.arbiter_axes == 3U && s_input.hmi_remaining_ms == 900U);
#endif
    memset(&s_input, 0, sizeof(s_input));
    memset(&s_link, 0, sizeof(s_link));
    memset(s_record_due, 0, sizeof(s_record_due));
    s_input.source_valid = true; s_input.life = 1U; s_input.runtime = 128U;
    s_input.now_ms = s_input.snapshot_ms = tick = 2000U;
    s_link.phase = CAN_LINK_READY; s_link.generation = 21U;
    s_snapshot_supported = true;
    schedule(tick);
    clear_offers();
    schedule(tick + 49U); assert(!has_record(0x11U));
    schedule(tick + 50U); assert(has_record(0x11U));
    clear_offers();
    s_input.runtime = 0U; s_input.stop.event = 7U;
    schedule(tick + 200U); assert(has_record(0x13U));
    puts("PASS CAN HMI collection and active 50ms / idle stop-history scheduling");
}
#endif

int main(void)
{
#if APP_CAN_ENABLED
    test_schedule();
    assert(flash_calls == 0U && create_calls == 0U);
    test_task_boundaries();
    test_fast_quotas();
    test_hmi_collection_and_schedule();
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
    test_segment_timing_identity();
#endif
#if APP_LINK_DIAG_ACTIVE && APP_CAN_DEBUG_ENABLED
    test_logs();
    puts("PASS CAN raw diagnostics: 20 events/s, shared writer ownership, packed payload and throttle count");
#endif
    puts("PASS CAN APP production: scheduling, reconnect, Tick wrap, task failure, 8 RX/3 TX budget, flood yielding and stale rejection");
#else
    app_can_init();
    app_can_task_create();
    assert(flash_calls == 0U && create_calls == 0U);
    assert(!s_public.enabled && !s_public.task_created);
#if APP_LINK_DIAG_ACTIVE && (APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED)
    {app_link_diag_snapshot_t diag;memset(&diag,0xFF,sizeof(diag));
     app_can_diag_snapshot(&diag,APP_LINK_CAN_SERVO,3U);assert(!diag.count);}
#endif
    puts("PASS CAN disabled: no Flash identity write and no CAN task creation");
#endif
    return 0;
}
