#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../APP/can_upper_task.c"

static uint32_t test_now;
static char uart_output[65536];
static size_t uart_length;
static int uart_result;
static unsigned can_frames;
TestRegisters test_registers;
int huart1, hi2c1, hiwdg;
TaskHandle_t xTaskGetIdleTaskHandle(void) { return &hiwdg; }
TaskHandle_t xTimerGetTimerDaemonTaskHandle(void) { return &hi2c1; }
HAL_StatusTypeDef HAL_I2C_DeInit(int *bus) { (void)bus; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Init(int *bus) { (void)bus; return HAL_OK; }
uint32_t HAL_I2C_GetError(int *bus) { (void)bus; return 0U; }
uint32_t HAL_I2C_GetState(int *bus) { (void)bus; return 0U; }
void HAL_GPIO_WritePin(int port, int pins, int level) { (void)port; (void)pins; (void)level; }
void HAL_GPIO_Init(int port, GPIO_InitTypeDef *pins) { (void)port; (void)pins; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 80U; }
void OLED_ShowStringClipped(uint8_t left, uint8_t top, const char *text, uint16_t length, uint8_t width, uint8_t font)
{ (void)left; (void)top; (void)text; (void)length; (void)width; (void)font; }
uint8_t OLED_DisplayBuf[8][128];

uint32_t HAL_GetTick(void) { return test_now; }
HAL_StatusTypeDef HAL_UART_Transmit(int *handle, uint8_t *text, uint16_t length, uint32_t timeout)
{
    (void)handle; (void)timeout;
    if (uart_result != HAL_OK) return uart_result;
    assert(uart_length + length < sizeof(uart_output));
    memcpy(uart_output + uart_length, text, length);
    uart_length += length;
    uart_output[uart_length] = 0;
    return HAL_OK;
}
QueueHandle_t xQueueCreateStatic(unsigned length, unsigned size, uint8_t *storage, StaticQueue_t *queue)
{
    queue->count = 0U; queue->size = size; queue->length = length; queue->head = 0U; queue->storage = storage;
    return queue;
}
int xQueueSend(QueueHandle_t queue, const void *message, unsigned timeout)
{
    unsigned tail;
    (void)timeout;
    if (queue->count == queue->length) return 0;
    tail = (queue->head + queue->count) % queue->length;
    memcpy(queue->storage + tail * queue->size, message, queue->size);
    ++queue->count;
    return pdPASS;
}
int xQueueReceive(QueueHandle_t queue, void *message, unsigned timeout)
{
    (void)timeout;
    if (!queue->count) return 0;
    memcpy(message, queue->storage + queue->head * queue->size, queue->size);
    queue->head = (queue->head + 1U) % queue->length;
    --queue->count;
    return pdPASS;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) { return queue->count; }
HAL_StatusTypeDef CanBus_Send(uint16_t id, const uint8_t *data)
{
    (void)id; (void)data; ++can_frames; return HAL_OK;
}

uint32_t UpperBoot_Allocate(void) { return 123U; }
HAL_StatusTypeDef HAL_I2C_Mem_Write(int *bus, unsigned address, unsigned reg, unsigned mode, uint8_t *data, unsigned length, unsigned timeout)
{ (void)bus; (void)address; (void)reg; (void)mode; (void)data; (void)length; (void)timeout; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(int *bus, unsigned address, unsigned trials, unsigned timeout)
{ (void)bus; (void)address; (void)trials; (void)timeout; return HAL_OK; }
void HAL_IWDG_Refresh(int *handle) { (void)handle; }
int HAL_GPIO_ReadPin(int port, int pin) { (void)port; (void)pin; return 1; }
void Error_Handler(void) { assert(0); }
void OLED_Clear(void) { }
void OLED_ShowString(unsigned left, unsigned top, char *text, unsigned font)
{ (void)left; (void)top; (void)text; (void)font; }
uint32_t ulTaskNotifyTake(int clear, unsigned delay) { (void)clear; (void)delay; return 0U; }
TickType_t xTaskGetTickCount(void) { return test_now; }
void xTaskNotifyGive(TaskHandle_t handle) { (void)handle; }
void vTaskDelayUntil(TickType_t *wake, unsigned period) { *wake += period; }
void vTaskDelay(unsigned period) { test_now += period; }
TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name, unsigned size, void *argument, unsigned priority, StackType_t *stack, StaticTask_t *task)
{ (void)entry; (void)name; (void)size; (void)argument; (void)priority; (void)stack; return task; }
HAL_StatusTypeDef CanBus_Start(void) { return HAL_OK; }
uint8_t CanBus_Receive(CanBusFrame_t *frame) { (void)frame; return 0U; }
void CanBus_GetStats(CanBusStats_t *stats) { memset(stats, 0, sizeof(*stats)); }
uint8_t CanBus_IsBusOff(void) { return 0U; }
void CanBus_SetRxTask(void *task) { (void)task; }
void CanBus_Service(uint32_t now) { (void)now; }
void CanBus_ClearPending(void) { }
void CanBus_DropMotion(void) { }
uint8_t CanBus_IsStarted(void) { return 1U; }
uint8_t CanBus_GetTxResult(CanBusFrame_t *frame, uint8_t *success) { (void)frame; (void)success; return 0U; }
uint8_t CanBus_GetTxResultEx(CanBusTxResult_t *result) { (void)result; return 0U; }

static void Reset(void)
{
    test_now = 0U; uart_length = 0U; uart_output[0] = 0; uart_result = HAL_OK; can_frames = 0U;
    log_queue = xQueueCreateStatic(LOG_QUEUE_LENGTH, sizeof(UpperLog_t), log_storage, &log_control);
    memset(log_errors, 0, sizeof(log_errors)); memset(log_record_seen, 0, sizeof(log_record_seen));
    memset(log_record_at, 0, sizeof(log_record_at));
    log_error_next = 0U; log_dropped = 0U; log_event_dropped = 0U;
    log_frame_suppressed = 0U; log_record_suppressed = 0U; log_error_suppressed = 0U;
    log_uart_bytes = 0U; uart_errors = 0U; log_started_at = 0U; log_target_seen = 0U; log_observed = 0U;
    memset(log_gate_at, 0, sizeof(log_gate_at)); log_gate_seen = log_gate_mask = 0U;
    log_gate_detail = log_gate_query_detail = 0U; log_gate_query_attempt = log_gate_query_result = 0U;
    Upper_Init(&protocol, 123U, ProtocolSend, LogEvent, NULL);
    UpperControl_Init(&control, ProtocolSend, NULL);
    protocol.snapshot.state = UPPER_READY; protocol.snapshot.link_token = 17U;
}
static unsigned Drain(void)
{
    UpperLog_t message;
    unsigned count = 0U;
    while (xQueueReceive(log_queue, &message, 0U) == pdPASS) { LogMessage(&message); ++count; }
    return count;
}
static void TestFrames(void)
{
    CanBusFrame_t frame = {0};
    unsigned index;
    Reset(); frame.dlc = 8U;
    for (index = 0U; index < 200U; ++index)
    {
        test_now = index * 10U; frame.timestamp = test_now;
        frame.id = (uint16_t)(0x180U + index % 9U); LogFrame(&frame, 0U, 1U);
        frame.id = (uint16_t)(0x300U + index % 3U); LogFrame(&frame, 0U, 1U);
    }
    if (UPPER_LOG_LEVEL == UPPER_LOG_LEVEL_RAW) assert(uxQueueMessagesWaiting(log_queue) > 0U);
    else assert(uxQueueMessagesWaiting(log_queue) == 0U && log_frame_suppressed == 400U);
    Drain();
    test_now = UPPER_LOG_RAW_DURATION_MS; frame.id = UPPER_LOG_RAW_ID;
    LogFrame(&frame, 0U, 1U); assert(uxQueueMessagesWaiting(log_queue) == 0U);
    LogEvent(NULL, UPPER_EVENT_LINK, UPPER_READY); assert(Drain() == 1U); assert(strstr(uart_output, "LINK=READY"));
    LogObserve(test_now); Drain();
    protocol.snapshot.link_token = 18U; LogObserve(++test_now); assert(Drain() == 1U);
    assert(strstr(uart_output, "token=0012"));
    LogEvent(NULL, UPPER_EVENT_REJECT, 60U); assert(Drain() == 1U);
    LogEvent(NULL, UPPER_EVENT_REJECT, 61U); assert(Drain() == 1U);
    LogEvent(NULL, UPPER_EVENT_REJECT, 61U); assert(Drain() == 0U && log_error_suppressed == 1U);
    puts("PASS logs: basic/fragment suppression, READY/token events, Bus-off/overflow first event and repeat gate");
}
static void TestRecords(void)
{
    UpperRecord_t *record;
    Reset(); record = &protocol.snapshot.records[14];
    record->valid = 1U; record->type = 0x53U; record->length = 32U; record->sequence = 8U;
    LogEvent(NULL, UPPER_EVENT_RECORD, 0x53U);
    if (UPPER_LOG_LEVEL == UPPER_LOG_LEVEL_QUIET) assert(Drain() == 0U);
    else
    {
        assert(Drain() == 1U); assert(strstr(uart_output, "type=53"));
        assert(strstr(uart_output, "crc=OK")); assert(strstr(uart_output, "fresh=YES"));
        if (UPPER_LOG_LEVEL == UPPER_LOG_LEVEL_RAW || UPPER_LOG_RAW_RECORD) assert(strstr(uart_output, "+000:"));
        else assert(strstr(uart_output, "+000:") == NULL);
    }
    test_now = 1U; LogEvent(NULL, UPPER_EVENT_RECORD, 0x53U); assert(Drain() == 0U);
    test_now = UPPER_LOG_RECORD_PERIOD_MS; record->timestamp = test_now;
    LogEvent(NULL, UPPER_EVENT_RECORD, 0x53U);
    assert(Drain() == (UPPER_LOG_LEVEL == UPPER_LOG_LEVEL_QUIET ? 0U : 1U));
    puts("PASS logs: 0x53 summary, no default payload expansion, first-at-zero and 5000ms record gate");
}
static void TestPriority(void)
{
    UpperLog_t low = {0}, high = {0}, message;
    unsigned index;
    Reset(); low.kind = 2U; high.kind = 3U;
    for (index = 0U; index < LOG_QUEUE_LENGTH; ++index) LogPush(&low);
    LogPush(&high); assert(log_dropped == 1U && log_event_dropped == 0U);
    assert(xQueueReceive(log_queue, &message, 0U) == pdPASS && message.kind == 3U);
    while (xQueueReceive(log_queue, &message, 0U) == pdPASS) { }
    for (index = 0U; index < LOG_QUEUE_LENGTH; ++index) LogPush(&high);
    LogPush(&low); assert(log_dropped == 2U && log_event_dropped == 0U);
    LogPush(&high); assert(log_dropped == 3U && log_event_dropped == 1U);
    while (xQueueReceive(log_queue, &message, 0U) == pdPASS) assert(message.kind == 3U);
    Reset();
    for (index = 0U; index < LOG_QUEUE_LENGTH - 1U; ++index) { low.sequence = (uint16_t)index; LogPush(&low); }
    LogPush(&high);
    assert(xQueueReceive(log_queue, &message, 0U) == pdPASS && message.kind == 3U);
    for (index = 0U; index < LOG_QUEUE_LENGTH - 1U; ++index)
        assert(xQueueReceive(log_queue, &message, 0U) == pdPASS && message.sequence == index);
    puts("PASS logs: bounded static queue, priority eviction/order, FIFO peers and event-drop accounting");
}
static void TestTargets(void)
{
    CanBusFrame_t frame = {0};
    unsigned index, logged = 0U;
    Reset(); frame.id = 0x111U; frame.dlc = 8U; frame.data[4] = 23U; frame.data[6] = 44U; frame.data[7] = 1U;
    for (index = 0U; index < 20U; ++index)
    {
        test_now = index * 100U; frame.timestamp = test_now; frame.data[0] = (uint8_t)index;
        LogFrame(&frame, 1U, 1U); logged += Drain();
    }
    assert(logged == (UPPER_LOG_LEVEL == UPPER_LOG_LEVEL_QUIET ? 0U : 2U));
    if (UPPER_LOG_LEVEL != UPPER_LOG_LEVEL_QUIET)
    {
        frame.data[2] = 182U; LogFrame(&frame, 1U, 1U); assert(Drain() == 1U);
        assert(strstr(uart_output, "TARGET seq=") && strstr(uart_output, "lease=300"));
    }
    control.state = SIM_AUTH_REQUESTED; LogObserve(test_now); Drain();
    control.state = SIM_AUTHORIZED_WAIT_ZERO; control.motion_token = 23U; LogObserve(++test_now); Drain();
    control.state = SIM_AUTHORIZED_ZERO; LogObserve(++test_now); Drain();
    control.state = SIM_STOP_REQUESTED; LogObserve(++test_now); Drain();
    control.state = SIM_STOPPED_RELEASED; control.motion_token = 0U; LogObserve(++test_now); Drain();
    control.state = SIM_FAULT; LogObserve(++test_now); Drain();
    assert(strstr(uart_output, "old_target_cleared=YES"));
    SerialWrite("abc"); assert(log_uart_bytes == uart_length);
    uart_result = HAL_ERROR; SerialWrite("failed"); assert(uart_errors == 1U && log_uart_bytes == uart_length);
    puts("PASS logs: 100ms target traffic limited to 1000ms summaries; control transitions and UART byte/error counts");
}
static void TestTiming(void)
{
    unsigned index, heartbeat_count = 0U, boot_count = 0U;
    CanBusFrame_t frame = {0};
    Reset(); protocol.snapshot.link_token = 0U; protocol.snapshot.state = UPPER_WAIT_BOOT;
    for (index = 0U; index <= 200U; ++index)
    {
        unsigned before = can_frames;
        test_now = index * 10U;
        Upper_Tick(&protocol, test_now, 1U);
        if (index % 10U == 0U) ++heartbeat_count;
        if (index % 50U == 0U) ++boot_count;
        assert(can_frames - before == (index % 10U == 0U ? 1U : 0U) + (index % 50U == 0U ? 1U : 0U));
        frame.id = 0x185U; frame.dlc = 8U; frame.timestamp = test_now;
        LogFrame(&frame, 0U, 1U); Drain();
    }
    assert(can_frames == heartbeat_count + boot_count);
    assert(UPPER_TARGET_PERIOD_MS == 100U && UPPER_TARGET_LEASE_MS == 300U);
    puts("PASS logs: unchanged heartbeat/boot counts and 100ms/300ms target configuration across log levels");
}

static void TestSummaryVolume(void)
{
    unsigned index, slot;
    CanBusFrame_t frame = {0};
    Reset(); frame.dlc = 8U;
    for (index = 0U; index < 1000U; ++index)
    {
        test_now = 30000U + index * 10U;
        frame.timestamp = test_now; frame.id = (uint16_t)(0x180U + index % 9U); LogFrame(&frame, 0U, 1U);
        frame.id = (uint16_t)(0x300U + index % 3U); LogFrame(&frame, 0U, 1U);
        if (index % 50U == 0U) { frame.id = 0x102U; LogFrame(&frame, 1U, 1U); }
        if (index % 500U == 0U)
        {
            for (slot = 0U; slot < UPPER_RECORD_COUNT; ++slot)
            {
                UpperRecord_t *record = &protocol.snapshot.records[slot];
                record->valid = 1U; record->type = (uint8_t)(slot == 14U ? 0x53U : slot + 0x60U);
                record->length = 32U; record->timestamp = test_now;
                LogEvent(NULL, UPPER_EVENT_RECORD, record->type); Drain();
            }
        }
        if (index % 100U == 0U) SerialWrite("STAT representative counters reserved: rx tx errors drops suppressed uart bytes: 300 bytes budget per second                                                                                                                  ");
        Drain();
    }
    assert(uart_length / 10U < 2000U);
    assert(log_dropped == 0U && log_event_dropped == 0U);
    printf("PASS synthetic steady log volume=%u bytes/s (not hardware throughput)\n", (unsigned)(uart_length / 10U));
}

static void TestAuthDiagnostics(void)
{
    unsigned index;
    Reset();
    control.state = SIM_AUTH_PREPARING; control.detail = 208U;
    control.gate.reason = 208U; control.gate.mask = 128U;
    control.gate.axis[0].age = 301U; control.gate.axis[0].flags = 157U;
    LogObserve(0U); Drain();
    assert(strstr(uart_output, "AUTH_GATE err=208 reason=LEFT AXIS") != NULL);
    assert(strstr(uart_output, "AUTH_AXIS axis=0 age=301") != NULL);
    uart_length = 0U; uart_output[0] = 0;
    for (index = 1U; index < 100U; ++index)
    {
        control.gate.mask = index & 1U ? 128U : 256U;
        test_now = index * 10U; LogObserve(test_now); Drain();
    }
    assert(uart_length == 0U);
    test_now = 1000U; control.gate.mask = 256U; LogObserve(test_now); Drain();
    assert(strstr(uart_output, "AUTH_GATE") != NULL);
    control.state = SIM_IDLE; control.detail = 212U; control.prepare_last_reason = 208U;
    test_now = 1010U; LogObserve(test_now); Drain();
    assert(strstr(uart_output, "err=212 reason=PREP TIMEOUT last=208") != NULL);
    assert(uart_length < 2000U);
    puts("PASS AUTHFIX logs: first diagnostic, complete axis snapshot, same-cause 1s gate and final timeout retained");
}

static void FirstScene(uint32_t now, uint32_t control_age, uint32_t axis_age)
{
    UpperRecord_t *record=&protocol.snapshot.records[2];
    unsigned axis;
    test_now=now;
    protocol.snapshot.state=UPPER_READY; protocol.snapshot.link_token=17U;
    protocol.snapshot.lower_boot_id=456U; protocol.snapshot.upper_boot_id=123U;
    protocol.snapshot.axes=3U; protocol.snapshot.control_source=4U; protocol.snapshot.lifecycle=3U;
    protocol.snapshot.heartbeat_seen=1U; protocol.snapshot.heartbeat_at=now-9U;
    record->valid=1U; record->type=0x11U; record->length=40U; record->sequence=77U;
    record->timestamp=now-control_age; record->payload[6]=4U; record->payload[10]=3U;
    record->payload[4]=23U; record->payload[12]=8U;
    for(axis=0U;axis<2U;++axis){
        UpperBasic_t *b=&protocol.snapshot.basic[6U+axis];
        b->valid=1U;b->timestamp=now-axis_age;b->data[0]=31U;b->data[1]=2U;
    }
    control.state=SIM_RUNNING;control.link_token=17U;control.motion_token=23U;
    control.lower_boot=456U;control.upper_boot=123U;control.axes=3U;
    control.target_rpm=-400;control.target_mm_s=-1453;control.speed_at=now;
}
static void TestStopFirst(void)
{
    char text[256];
    UpperLog_t message={0};
    const UpperStopFirst_t *f;
    unsigned i;
    Reset(); FirstScene(1000U,499U,0U);
    UpperControl_Tick(&control,&protocol,test_now);
    assert(control.state==SIM_RUNNING && !UpperControl_StopFirst()->pending);
    FirstScene(1001U,500U,0U); UpperControl_Tick(&control,&protocol,test_now);
    f=UpperControl_StopFirst();
    assert(f->pending && f->source==UPPER_STOP_AUTO_CONTROL && f->recovery==7U);
    assert(f->control_age==500U && f->axis_age[0]==0U && f->motion_token==23U);
    assert(f->target_rpm==-400 && f->target_mm_s==-1453 && !control.motion_token);
    assert(f->control_sequence==77U && f->detail==116U && f->validity==125U);
    for(i=0U;i<LOG_QUEUE_LENGTH;++i)LogPush(&message);
    uart_result=HAL_ERROR; assert(LogStopFirst(text,sizeof(text)) && f->page==0U && f->pending);
    UpperControl_CaptureStop(&control,&protocol,UPPER_STOP_UI_QUICK,0U,5U,4U,1100U,1100U);
    assert(f->at==1001U && f->source==UPPER_STOP_AUTO_CONTROL && f->merged==1U);
    uart_result=HAL_OK;
    assert(LogStopFirst(text,sizeof(text)) && f->page==1U);
    assert(LogStopFirst(text,sizeof(text)) && f->page==2U);
    assert(LogStopFirst(text,sizeof(text)) && !f->pending);
    assert(f->at==1001U && f->target_rpm==-400 && uxQueueMessagesWaiting(log_queue)==LOG_QUEUE_LENGTH);
    assert(strstr(uart_output,"STOP_FIRST v=1 at=1001 part=0 source=1 recovery=7"));
    assert(strstr(uart_output,"ctrl_age=500 axis_age=0/0"));
    assert(strstr(uart_output,"part=2 axis_flags=1F/1F mode=2/2 fault=0/0 stop_req=1 merged=1"));
    assert(f->stop_request==control.request_sequence);

    /* A grant while the old first is pending cannot clear it; completion
     * arms the next event without requiring a second grant. */
    Reset();FirstScene(1000U,500U,0U);UpperControl_Tick(&control,&protocol,test_now);
    control.state=SIM_IDLE;
    UpperControl_Intent(&control,&protocol,UPPER_INTENT_AUTHORIZE,0,1002U);
    f=UpperControl_StopFirst();assert(f->pending && f->at==1000U);
    for(i=0U;i<3U;++i)assert(LogStopFirst(text,sizeof(text)));
    FirstScene(2000U,0U,300U);UpperControl_Tick(&control,&protocol,test_now);
    assert(f->pending && f->at==2000U && f->source==UPPER_STOP_AUTO_AXIS);

    Reset(); FirstScene(1000U,0U,299U); UpperControl_Tick(&control,&protocol,test_now);
    assert(control.state==SIM_RUNNING && !UpperControl_StopFirst()->pending);
    FirstScene(1001U,0U,300U); UpperControl_Tick(&control,&protocol,test_now);
    f=UpperControl_StopFirst(); assert(f->source==UPPER_STOP_AUTO_AXIS && f->recovery==8U);
    assert(f->axis_age[0]==300U && f->validity==79U && f->control_age==0U);

    Reset(); UpperUi_Init(); FirstScene(1000U,0U,0U);
    UpperUi_ScanKeysOnly(1000U,4U); UpperUi_ScanKeysOnly(1020U,4U);
    UpperUi_ConsumeEventsAt(&control,&protocol,1030U); f=UpperControl_StopFirst();
    assert(f->source==UPPER_STOP_UI_QUICK && f->key_mask==4U && f->key_event==UPPER_UI_EVENT_QUICK_STOP);
    assert(f->event_at==1020U && f->at==1030U && f->motion_token==23U);
    assert(f->stop_request==control.stop_sequence);

    Reset(); UpperUi_Init();
    UpperUi_ScanKeysOnly(0U,1U);UpperUi_ScanKeysOnly(20U,1U);
    UpperUi_ScanKeysOnly(30U,0U);UpperUi_ScanKeysOnly(50U,0U);
    UpperUi_ScanKeysOnly(60U,1U);UpperUi_ScanKeysOnly(80U,1U);
    UpperUi_ScanKeysOnly(90U,0U);UpperUi_ScanKeysOnly(110U,0U);
    FirstScene(1000U,0U,0U);
    UpperUi_ScanKeysOnly(1000U,1U); UpperUi_ScanKeysOnly(1020U,1U);
    UpperUi_ScanKeysOnly(1820U,1U);
    UpperUi_ConsumeEventsAt(&control,&protocol,1830U);f=UpperControl_StopFirst();
    assert(f->source==UPPER_STOP_UI_RELEASE && f->key_mask==1U && f->event_at==1820U);

    Reset(); UpperUi_Init();
    UpperUi_ScanKeysOnly(0U,1U);UpperUi_ScanKeysOnly(20U,1U);
    UpperUi_ScanKeysOnly(30U,0U);UpperUi_ScanKeysOnly(50U,0U);
    UpperUi_ScanKeysOnly(60U,1U);UpperUi_ScanKeysOnly(80U,1U);
    UpperUi_ScanKeysOnly(90U,0U);UpperUi_ScanKeysOnly(110U,0U);
    FirstScene(1000U,0U,0U);
    UpperUi_ScanKeysOnly(1000U,2U);UpperUi_ScanKeysOnly(1020U,2U);
    UpperUi_ScanKeysOnly(1820U,2U);UpperUi_ConsumeEventsAt(&control,&protocol,1830U);
    f=UpperControl_StopFirst();assert(f->source==UPPER_STOP_UI_NORMAL && f->key_mask==2U && f->key_event==2U);

    Reset(); FirstScene(1000U,0U,0U);
    protocol.snapshot.basic[6].data[0]=63U;
    UpperControl_Tick(&control,&protocol,test_now);f=UpperControl_StopFirst();
    assert(f->source==UPPER_STOP_AUTO_UNSAFE && f->recovery==9U && f->axis_flags[0]==63U);
    assert(f->stop_request==0xFFFFU); /* Fail branch creates no STOP request. */

    Reset(); FirstScene(0xFFFFFFF0UL,500U,0U); UpperControl_Tick(&control,&protocol,test_now);
    assert(UpperControl_StopFirst()->control_age==500U && UpperControl_StopFirst()->source==UPPER_STOP_AUTO_CONTROL);
    printf("PASS STOP_FIRST: %u bytes, production 499/500 and 299/300 gates, wrap, key sample, queue pressure and UART retry\n",(unsigned)sizeof(UpperStopFirst_t));
}

static void TestStopFailure(void)
{
    char text[320];
    UpperStopVerify_t frozen;
    UpperLog_t entry={0};
    unsigned i;
    for(i=0U;i<5U;++i) {
        uint32_t local=0xFFFFFFF0UL;
        Reset();FirstScene(local,0U,0U);
        UpperControl_Intent(&control,&protocol,UPPER_INTENT_QUICK_STOP,0,local);
        if(i==0U)protocol.snapshot.state=UPPER_WAIT_BOOT;
        else if(i==1U)++protocol.snapshot.link_token;
        else if(i==2U)++protocol.snapshot.lower_boot_id;
        else if(i==3U)++protocol.snapshot.upper_boot_id;
        else protocol.snapshot.heartbeat_at=local+1U-UPPER_LINK_TIMEOUT_MS;
        UpperControl_Tick(&control,&protocol,local+1U);
        assert(control.detail==111U && control.stop_failure_state==1U);
        assert((control.stop_verify.mask&0x001F0000UL)==(1UL<<(16U+i)));
    }
    Reset();FirstScene(1000U,0U,0U);
    UpperControl_Intent(&control,&protocol,UPPER_INTENT_QUICK_STOP,0,1000U);
    protocol.snapshot.lower_boot_id=457U;
    protocol.snapshot.link_token=18U;
    UpperControl_Tick(&control,&protocol,1001U);
    assert(control.state==SIM_FAULT && control.detail==111U);
    assert(control.stop_failure_state==1U && control.stop_failure_detail==111U);
    assert((control.stop_verify.mask&0x801F0000UL)==0x80060000UL);
    assert(control.stop_verify.control_age==456U && control.stop_verify.stop_age==457U);
    assert(control.stop_verify.axis_age[0]==123U && control.stop_verify.axis_age[1]==123U);
    assert(control.stop_verify.diagnostic==0x00110012UL);
    frozen=control.stop_verify;
    entry.kind=3U;
    for(i=0U;i<LOG_QUEUE_LENGTH;++i)assert(LogPush(&entry));
    log_observed=0U;LogObserve(1002U);assert(!log_observed);
    uart_result=HAL_ERROR;assert(LogStopFailure(text,sizeof(text)) && control.stop_failure_state==1U);
    UpperControl_Intent(&control,&protocol,UPPER_INTENT_QUICK_STOP,0,1003U);
    UpperControl_Tick(&control,&protocol,1004U);
    assert(!memcmp(&frozen,&control.stop_verify,sizeof(frozen)));
    uart_result=HAL_OK;assert(LogStopFailure(text,sizeof(text)) && control.stop_failure_state==2U);
    assert(strstr(uart_output,"STOP_FAIL v=1 at=1001 err=111"));
    assert(strstr(uart_output,"lower=000001C8/000001C9 upper=0000007B/0000007B token=17/18"));
    assert(!LogStopFailure(text,sizeof(text)));
    UpperControl_Intent(&control,&protocol,UPPER_INTENT_QUICK_STOP,0,1005U);
    /* A duplicate active K3 cannot reset a still-running stop or its deadline. */
    assert(control.stop_at==1003U);
    Reset();FirstScene(1000U,0U,0U);
    UpperControl_Intent(&control,&protocol,UPPER_INTENT_QUICK_STOP,0,1000U);
    /* Keep link identity alive while the required released proof is absent. */
    protocol.snapshot.heartbeat_at=30999U;
    UpperControl_Tick(&control,&protocol,30999U);
    assert(control.state==SIM_STOPPING && control.stop_failure_state==0U);
    protocol.snapshot.heartbeat_at=31000U;
    UpperControl_Tick(&control,&protocol,31000U);
    assert(control.state==SIM_FAULT && control.detail==114U && control.stop_at==1000U);
    assert(control.stop_failure_state==1U && control.stop_failure_detail==114U);
    assert(!(control.stop_verify.mask&0x80000000UL));
    frozen=control.stop_verify;
    for(i=0U;i<LOG_QUEUE_LENGTH;++i)assert(LogPush(&entry));
    uart_result=HAL_ERROR;assert(LogStopFailure(text,sizeof(text)) && control.stop_failure_state==1U);
    UpperControl_Intent(&control,&protocol,UPPER_INTENT_QUICK_STOP,0,31001U);
    protocol.snapshot.lower_boot_id++;
    UpperControl_Tick(&control,&protocol,31002U);
    assert(!memcmp(&frozen,&control.stop_verify,sizeof(frozen)) && control.stop_failure_detail==114U);
    uart_result=HAL_OK;assert(LogStopFailure(text,sizeof(text)) && control.stop_failure_state==2U);
    assert(strstr(uart_output,"STOP_FAIL v=1 at=31000 err=114"));
    assert(!LogStopFailure(text,sizeof(text)));
    puts("PASS STOP_FAIL: exact111/114 scenes, 30s boundary, independent queue/UART retry, pending immutable and no deadline reset");
}

int main(void)
{
    TestStopFirst();
    TestStopFailure();
    {
        Reset();
        control.stop_verify.at=123U; control.stop_verify.mask=8U;
        control.stop_verify.life=5U; control.stop_verify.event=88U;
        control.stop_verify.phase=4U; control.stop_verify.axis_age[0]=17U;
        control.stop_verify.axis_age[1]=18U; control.stop_verify.diagnostic=12U;
        LogObserve(123U); Drain();
        assert(strstr(uart_output,"STOP_VERIFY t=123 mask=008 owner=0 life=5 token=0 event=88 phase=4 result=0"));
        assert(strstr(uart_output,"axis=17/18 diag=0000000C"));
        puts("PASS bounded STOP_VERIFY log contains retained predicate and full scene");
    }
    TestFrames(); TestRecords(); TestPriority();
    {
        UpperLog_t entry;
        Reset(); memset(&entry, 0, sizeof(entry));
        entry.type=0x22U; entry.dlc=104U; entry.copied=55U;
        LogRecord(&entry); assert(strstr(uart_output,"prefix invalid"));
        uart_length=0U; uart_output[0]=0;
        entry.copied=56U; LogRecord(&entry); assert(strstr(uart_output,"RECORD type=22"));
        uart_length=0U; uart_output[0]=0;
        entry.type=0x53U; entry.dlc=entry.copied=3U;
        entry.data[0]=0xA1U;entry.data[1]=0xB2U;entry.data[2]=0xC3U;
        LogRecord(&entry);
        if(UPPER_LOG_LEVEL==UPPER_LOG_LEVEL_RAW || UPPER_LOG_RAW_RECORD)assert(strstr(uart_output,"+000: A1 B2 C3"));
        uart_length=0U; uart_output[0]=0;
        entry.dlc=2U; entry.copied=3U;LogRecord(&entry);assert(strstr(uart_output,"prefix invalid"));
        puts("PASS compact record prefix/truncation/partial RAW bounds");
    }
    TestTargets(); TestTiming(); TestSummaryVolume(); TestAuthDiagnostics();
    {
        UpperLog_t entry;
        CanBusTxResult_t result;
        unsigned index;
        Reset(); memset(&entry, 0, sizeof(entry)); memset(&result, 0, sizeof(result));
        entry.kind = 10U; result.frame.id = 0x111U; result.outcome = UPPER_TX_TIMEOUT;
        result.age = 110U; result.mailbox = 1U; result.generation = 7U;
        memcpy(entry.data, &result, sizeof(result)); LogPush(&entry); Drain();
        assert(strstr(uart_output, "TX_FINAL id=111") && strstr(uart_output, "age=110"));
        Reset(); control.state = SIM_RUNNING; control.target_rpm = 100;
        control.operation_detail = control.detail = 103U; control.rejected_rpm = -100; control.operation_count = 1U;
        LogObserve(0U); Drain(); assert(strstr(uart_output, "OP_REJECT err=103 requested=-100 target=100"));
        uart_length = 0U; uart_output[0] = 0;
        for (index = 1U; index < 100U; ++index)
        { control.operation_count++; test_now = index * 10U; LogObserve(test_now); Drain(); }
        assert(uart_length == 0U);
        puts("PASS TXFIX logs: final transaction evidence and operation rejection 1s rate limit");
    }
    printf("PASS F103 actual log task mode=%u motion=%u; software only\n", UPPER_LOG_LEVEL, UPPER_CAN_MOTION_ENABLED);
    return 0;
}
