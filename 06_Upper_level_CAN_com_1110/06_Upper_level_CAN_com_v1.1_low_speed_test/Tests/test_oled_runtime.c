#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "../APP/can_upper_task.c"
static uint32_t test_now, stop_at;
static jmp_buf exit_task;
static unsigned can_frames, writes, probes, watchdog_feeds, pulses, motion_drops;
static unsigned page_writes[8];
static uint32_t wire_hash;
static CanBusStats_t injected_stats;
static int i2c_failure, scl_low, sda_release_after, key_mask, fail_data_only;
static uint8_t last_page, pending_result;
static CanBusTxResult_t injected_result;
TestRegisters test_registers;
int huart1, hi2c1, hiwdg;
TaskHandle_t xTaskGetIdleTaskHandle(void) { return &hiwdg; }
TaskHandle_t xTimerGetTimerDaemonTaskHandle(void) { return &hi2c1; }
uint8_t OLED_DisplayBuf[8][128];
uint32_t HAL_GetTick(void) { return test_now; }
HAL_StatusTypeDef HAL_UART_Transmit(int *handle,uint8_t *data,uint16_t length,uint32_t timeout)
{ (void)handle;(void)data;(void)length;(void)timeout; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_DeInit(int *bus) { (void)bus; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Init(int *bus) { (void)bus; return HAL_OK; }
uint32_t HAL_I2C_GetError(int *bus) { (void)bus; return 0x1234U; }
uint32_t HAL_I2C_GetState(int *bus) { (void)bus; return 0x24U; }
void HAL_GPIO_WritePin(int port,int pins,int level)
{ if(port==GPIOB && pins==GPIO_PIN_8 && level==GPIO_PIN_SET) ++pulses; }
void HAL_GPIO_Init(int port,GPIO_InitTypeDef *pins) { (void)port; assert(pins->Mode==GPIO_MODE_OUTPUT_OD); }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { assert(task); return 80U; }
void OLED_ShowStringClipped(uint8_t left,uint8_t top,const char *text,uint16_t length,uint8_t width,uint8_t font)
{ assert(left==0U && top<=56U && strlen(text)<=21U && length==21U && width==128U && font==6U); }
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
    unsigned byte;
    wire_hash=(wire_hash ^ id)*16777619U;
    for(byte=0U;byte<8U;++byte)wire_hash=(wire_hash ^ data[byte])*16777619U;
    ++can_frames; return HAL_OK;
}

uint32_t UpperBoot_Allocate(void) { return 123U; }
HAL_StatusTypeDef HAL_I2C_Mem_Write(int *bus, unsigned address, unsigned reg, unsigned mode, uint8_t *data, unsigned length, unsigned timeout)
{
    (void)bus; (void)address; (void)mode; (void)timeout;
    ++writes;
    if (reg == 0x00U && length == 3U) { last_page = data[0] & 7U; assert(last_page < 8U); }
    if (reg == 0x40U) { assert(length == 128U); ++page_writes[last_page]; }
    if (i2c_failure && (!fail_data_only || reg==0x40U)) { test_now += 25U; return i2c_failure; }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(int *bus, unsigned address, unsigned trials, unsigned timeout)
{ (void)bus; (void)address; (void)trials; (void)timeout; ++probes; return i2c_failure; }
void HAL_IWDG_Refresh(int *handle) { (void)handle; ++watchdog_feeds; }
int HAL_GPIO_ReadPin(int port, int pin)
{
    if (port == GPIOB) return pin == GPIO_PIN_8 ? !scl_low : (sda_release_after >= 0 && pulses >= (unsigned)sda_release_after);
    return !(key_mask & (1U << (pin - 1)));
}
void Error_Handler(void) { assert(0); }
void OLED_Clear(void) { }
void OLED_ShowString(unsigned left, unsigned top, char *text, unsigned font)
{ (void)left; (void)top; (void)text; (void)font; }
uint32_t ulTaskNotifyTake(int clear, unsigned delay)
{ (void)clear; test_now += delay; if (stop_at && test_now >= stop_at) longjmp(exit_task,1); return 0U; }
TickType_t xTaskGetTickCount(void) { return test_now; }
void xTaskNotifyGive(TaskHandle_t handle) { (void)handle; }
void vTaskDelayUntil(TickType_t *wake, unsigned period) { *wake += period; }
void vTaskDelay(unsigned period) { test_now += period; if (stop_at && test_now >= stop_at) longjmp(exit_task, 1); }
TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name, unsigned size, void *argument, unsigned priority, StackType_t *stack, StaticTask_t *task)
{ (void)entry; (void)name; (void)size; (void)argument; (void)priority; (void)stack; return task; }
HAL_StatusTypeDef CanBus_Start(void) { return HAL_OK; }
uint8_t CanBus_Receive(CanBusFrame_t *frame) { (void)frame; return 0U; }
void CanBus_GetStats(CanBusStats_t *stats) { *stats=injected_stats; }
uint8_t CanBus_IsBusOff(void) { return 0U; }
void CanBus_SetRxTask(void *task) { (void)task; }
void CanBus_Service(uint32_t now) { (void)now; }
void CanBus_ClearPending(void) { }
void CanBus_DropMotion(void) { ++motion_drops; }
uint8_t CanBus_IsStarted(void) { return 1U; }
uint8_t CanBus_GetTxResult(CanBusFrame_t *frame, uint8_t *success) { (void)frame; (void)success; return 0U; }
uint8_t CanBus_GetTxResultEx(CanBusTxResult_t *result)
{ if (!pending_result) return 0U; pending_result=0U; *result=injected_result; return 1U; }


static void Run(void (*task)(void *), uint32_t duration)
{ stop_at=test_now+duration; if(!setjmp(exit_task)) task(NULL); stop_at=0U; }
static void Reset(void)
{
    test_now=0U; stop_at=0U; can_frames=writes=probes=watchdog_feeds=pulses=motion_drops=0U;
    i2c_failure=scl_low=key_mask=fail_data_only=0; sda_release_after=0; pending_result=0U;
    wire_hash=2166136261U;memset(&injected_stats,0,sizeof(injected_stats));
    oled_present=0U; memset((void *)&upper_diagnostics,0,sizeof(upper_diagnostics));
    memset(page_writes,0,sizeof(page_writes)); memset(&view,0,sizeof(view));
    UpperApp_Init(); UpperApp_Start();
}
int main(void)
{
    uint32_t before, hash=2166136261U; unsigned index;
    UpperUiEvent_t event;
    Reset(); assert(TASK_COUNT==(UPPER_USART1_DEBUG_ENABLE?6U:5U));
    assert(sizeof(UpperDiagnostics_t)+sizeof(task_handles)<=128U || sizeof(void*)>4U);
    assert(!OledHardwareRefreshPage(8U) && writes==0U);
    assert(!OledHardwareRefreshPage(255U) && writes==0U);
    sda_release_after=4; before=test_now; assert(OledRecover()); assert(pulses==5U && test_now-before<=100U);
    Reset(); scl_low=1; before=test_now; assert(!OledRecover()); assert(pulses==0U && probes==0U && test_now-before<=100U);
    Reset(); sda_release_after=-1; before=test_now; assert(!OledRecover()); assert(pulses==10U && probes==0U && test_now-before<=100U);
    for(index=1U;index<=3U;++index){
        Reset(); i2c_failure=(int)index; before=test_now; assert(!OledRecover());
        assert(test_now-before<=100U && upper_diagnostics.oled_fail_count==1U);
        assert(upper_diagnostics.oled_last_error==0x1234U && upper_diagnostics.oled_last_state==0x24U);
    }
    Reset(); oled_present=1U; i2c_failure=3; Run(OledTask,500U);
    assert(upper_diagnostics.oled_fail_count==3U && upper_diagnostics.oled_state==2U);
    assert(page_writes[1]==0U && writes==3U);
    i2c_failure=0; Run(OledTask,2500U);
    assert(upper_diagnostics.oled_recovery_count>=1U && upper_diagnostics.oled_refresh_count>=8U);
    for(index=0U;index<8U;++index) assert(page_writes[index]>0U);
    assert(upper_diagnostics.oled_frame_generation>=2U);
    Reset(); oled_present=1U; i2c_failure=HAL_TIMEOUT; fail_data_only=1;
    Run(OledTask,500U); assert(page_writes[0]==3U && page_writes[1]==0U);
    assert(upper_diagnostics.oled_state==2U);
    Reset(); key_mask=4; Run(KeyTask,60U); assert(UpperUi_GetEvent(&event) && event.type==UPPER_UI_EVENT_QUICK_STOP);
    Run(HealthTask,100U); assert(watchdog_feeds==1U);
    key_mask=0; Run(KeyTask,40U); key_mask=1; Run(KeyTask,60U); key_mask=0; Run(KeyTask,60U);
    UpperUi_Publish(&control,&protocol,test_now,&view); assert(view.page==1U);
    test_now+=1000U; Run(HealthTask,100U); assert(watchdog_feeds==1U);
    Reset(); pending_result=1U; memset(&injected_result,0,sizeof(injected_result));
    injected_result.frame.id=0x111U; injected_result.outcome=UPPER_TX_TIMEOUT;
    control.state=SIM_RUNNING;control.motion_token=23U;control.target_rpm=100;control.target_mm_s=363;
    injected_result.frame.data[4]=23U;
    Run(ProtocolTask,20U); assert(motion_drops>=1U);
    assert(control.first_error==110U && control.motion_token==0U && control.target_rpm==0 && control.recovery_required);
    printf("SAFETY detail=%u target=%d token=%u recovery=%u drops=%u\n",control.detail,control.target_rpm,control.motion_token,control.recovery_required,motion_drops);
    for(index=0U;index<4U;++index){
        Reset(); protocol.snapshot.link_token=100U;protocol.snapshot.state=UPPER_READY;
        if(index==0U)injected_stats.bus_off_count=1U;
        if(index==1U)injected_stats.rx_queue_overruns=1U;
        if(index==2U)injected_stats.recovery_count=1U;
        if(index==3U)injected_stats.result_overruns=1U;
        Run(ProtocolTask,20U);assert(protocol.snapshot.link_token==0U);
    }
    Reset();
    for(index=0U;index<1000U;++index){
        test_now=index*10U; Upper_Tick(&protocol,test_now,1U);
        UpperControl_Tick(&control,&protocol,test_now);
        hash=(hash^(uint32_t)control.state)*16777619U;
        hash=(hash^protocol.snapshot.tx_heartbeat_sequence)*16777619U;
    }
    printf("EQUIVALENCE state=%u frames=%u hash=%08X wire=%08X drops=%u\n",control.state,can_frames,hash,wire_hash,motion_drops);
    puts("PASS runtime: bounded recovery/9 clocks/SCL-low/BUSY-NACK-timeout/backoff/same-page/full redraw/K3/health/final-failure safety");
    return 0;
}
