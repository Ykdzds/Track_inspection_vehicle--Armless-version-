#include <assert.h>
#include <stdio.h>
#include "../BSP/SPI_LCD/lcd_port.c"
DWT_Type lcd_test_dwt;
CoreDebug_Type lcd_test_debug;
uint32_t SystemCoreClock=1000000U;
static SPI_TypeDef peripheral;
static DMA_Stream_TypeDef stream;
static DMA_HandleTypeDef dma={.Instance=&stream};
SPI_HandleTypeDef hspi1={.Instance=&peripheral,.hdmatx=&dma};
static uint32_t tick, notifications, transfers, polls, resets, wait_ticks, complete_after=1U;
static uint8_t mode, scheduler, stuck_disable, fail_init, injected, critical_depth;
enum {NORMAL,LATE_TASK,TIMEOUT,DMA_ERROR,START_BUSY,TAIL_BUSY,HT_THEN_TC,LOST_IRQ,
      PRELAUNCH_DELAY,DEADLINE_RACE,TAIL_DEADLINE_RACE,TAIL_DEADLINE_BUDGET,
      TC_AND_DMA_ERROR,DMA_DME_ERROR,DMA_FE_ERROR,SPI_MODF_ERROR,SPI_FRE_ERROR};
static void complete(void);
uint32_t HAL_GetTick(void)
{
    lcd_test_dwt.CYCCNT+=10U;
    if(mode==DEADLINE_RACE && !injected && s_dma_active && !s_dma_done &&
       (peripheral.CR2&SPI_CR2_TXDMAEN)) {
        /* IRQ completion between the old done check and its deadline read. */
        injected=1U;complete();tick+=60U;
    }
    if(mode==TAIL_DEADLINE_RACE && !injected && s_dma_done &&
       (peripheral.SR&SPI_SR_BSY)) {
        /* Hardware finishes between the old busy check and its deadline read. */
        injected=1U;peripheral.SR=SPI_SR_TXE;tick+=60U;
    }
    return tick;
}
int xTaskGetSchedulerState(void){return scheduler;}
void lcd_test_enter_critical(void){assert(scheduler==taskSCHEDULER_RUNNING && !critical_depth);critical_depth=1U;}
void lcd_test_exit_critical(void){assert(critical_depth);critical_depth=0U;}
TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void *)1;}
void vTaskNotifyGiveFromISR(TaskHandle_t task,BaseType_t *higher){assert(task==(void *)1);*higher=0;++notifications;}
static void complete(void){stream.NDTR=0U;stream.CR&=~DMA_SxCR_EN;dma.Flags=1U;lcd_port_dma_tx_isr();}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t wait)
{
    (void)clear;
    assert(!critical_depth); /* No blocking notification, even the zero-time drain, in a critical section. */
    if(!wait && mode==PRELAUNCH_DELAY && !injected){tick+=60U;injected=1U;}
    if(wait){++wait_ticks;++tick;
        if(mode==LATE_TASK){complete();tick+=100U;}
        else if(mode==DMA_ERROR){dma.Flags=4U;lcd_port_dma_tx_isr();}
        else if(mode==DMA_DME_ERROR){dma.Flags=8U;lcd_port_dma_tx_isr();}
        else if(mode==DMA_FE_ERROR){dma.Flags=16U;lcd_port_dma_tx_isr();}
        else if(mode==TC_AND_DMA_ERROR){stream.NDTR=0U;stream.CR&=~DMA_SxCR_EN;dma.Flags=5U;lcd_port_dma_tx_isr();}
        else if(mode==HT_THEN_TC && wait_ticks==1U){dma.Flags=2U;lcd_port_dma_tx_isr();assert(!s_dma_done);}
        else if(mode==LOST_IRQ){stream.NDTR=0U;stream.CR&=~DMA_SxCR_EN;dma.Flags=1U;}
        else if(mode!=TIMEOUT && mode!=LOST_IRQ && wait_ticks>=complete_after)complete();
    }
    {uint32_t value=notifications;notifications=0U;return value;}
}
void vTaskDelay(TickType_t n){assert(!critical_depth);tick+=n;}
void lcd_test_dma_disable(DMA_HandleTypeDef *h){lcd_test_dwt.CYCCNT+=1000U;if(!stuck_disable)h->Instance->CR&=~DMA_SxCR_EN;}
void lcd_test_spi_reset(void){++resets;memset(&peripheral,0,sizeof(peripheral));peripheral.SR=SPI_SR_TXE;}
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *h){assert(!critical_depth);if(fail_init==1U)return HAL_ERROR;h->hdmatx->State=HAL_DMA_STATE_READY;h->hdmatx->ErrorCode=fail_init==2U?1U:0U;h->State=HAL_SPI_STATE_READY;return HAL_OK;}
void HAL_GPIO_WritePin(void *p,uint32_t pin,GPIO_PinState state){(void)p;(void)pin;(void)state;assert(!s_dma_active);}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h,uint8_t *p,uint16_t n,uint32_t timeout)
{(void)h;(void)p;(void)n;assert(timeout==20U);++polls;return mode==TIMEOUT?HAL_TIMEOUT:HAL_OK;}
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *h,uint32_t src,uint32_t dst,uint32_t n)
{
    (void)src;(void)dst;assert(n<=640U);assert(h==&dma);assert(!s_dma_done && critical_depth);
    if(mode==START_BUSY)return HAL_BUSY;
    ++transfers;wait_ticks=0U;h->Instance->NDTR=n;h->Instance->CR=DMA_SxCR_EN|DMA_IT_TC|DMA_IT_TE;
    peripheral.SR=SPI_SR_TXE|
        ((mode==TAIL_BUSY || mode==TAIL_DEADLINE_RACE || mode==TAIL_DEADLINE_BUDGET)?SPI_SR_BSY:0U)|
        (mode==SPI_MODF_ERROR?SPI_SR_MODF:0U)|(mode==SPI_FRE_ERROR?SPI_SR_FRE:0U);
    return HAL_OK;
}
static void reset_case(void)
{
    stuck_disable=fail_init=injected=0U;mode=NORMAL;complete_after=1U;
    assert(lcd_port_recover());lcd_port_reset_stats();notifications=wait_ticks=0U;
}
int main(void)
{
    uint8_t payload[1300];lcd_io io={.spi=&hspi1};lcd_port_stats_t stats;uint32_t before,at;
    lcd_failure_t first;
    memset(payload,0xA5,sizeof(payload));reset_case();
    scheduler=0U;lcd_write_bulk(&io,payload,sizeof(payload));assert(polls==3U && !transfers);
    scheduler=taskSCHEDULER_RUNNING;
    lcd_write_bulk(&io,payload,sizeof(payload));assert(transfers==3U && !memcmp(s_dma_buffer,payload,20));
    before=transfers;mode=LATE_TASK;lcd_write_byte(&io,0x12U);assert(transfers==before+1U && s_init_stage!=LCD_INIT_STAGE_SPI_ERROR);
    reset_case();mode=HT_THEN_TC;complete_after=2U;lcd_write_byte(&io,1U);assert(!s_spi_errors);
    reset_case();mode=PRELAUNCH_DELAY;at=tick;before=s_stats.dma_completed;lcd_write_byte(&io,1U);
    assert(injected && !s_spi_errors && tick-at==61U && s_stats.dma_completed==before+1U);
    reset_case();mode=DEADLINE_RACE;at=tick;before=s_stats.dma_completed;lcd_write_byte(&io,1U);
    assert(injected && !s_spi_errors && !wait_ticks && tick-at==60U && s_stats.dma_completed==before+1U);
    reset_case();mode=TAIL_DEADLINE_RACE;at=tick;before=s_stats.dma_completed;lcd_write_byte(&io,1U);
    assert(injected && !s_spi_errors && tick-at==61U && s_stats.dma_completed==before+1U);
    reset_case();tick=UINT32_MAX-20U;complete_after=40U;lcd_write_byte(&io,1U);
    assert(!s_spi_errors && tick==19U);
    reset_case();mode=TIMEOUT;lcd_write_bulk(&io,payload,640U);
    lcd_port_get_stats(&stats);assert(stats.errors==1U && stats.last.reason==LCD_ERROR_TIMEOUT && stats.last.remaining==640U);
    first=stats.first;
    before=transfers;lcd_write_byte(&io,1U);assert(transfers==before);
    dma.Flags=1U;lcd_port_dma_tx_isr();assert(!s_dma_active); /* Late completion is drained. */
    reset_case();mode=TAIL_DEADLINE_BUDGET;complete_after=49U;lcd_write_byte(&io,1U);
    assert(s_stats.last.reason==LCD_ERROR_TIMEOUT && s_stats.last.elapsed_ms==50U && s_stats.last.remaining==0U);
    reset_case();mode=DMA_ERROR;lcd_write_byte(&io,1U);assert(s_stats.last.reason==LCD_ERROR_DMA);
    reset_case();mode=TC_AND_DMA_ERROR;lcd_write_byte(&io,1U);
    assert(s_stats.last.reason==LCD_ERROR_DMA && s_stats.last.dma_error==HAL_DMA_ERROR_TE);
    reset_case();mode=DMA_DME_ERROR;lcd_write_byte(&io,1U);
    assert(s_stats.last.reason==LCD_ERROR_DMA && s_stats.last.dma_error==HAL_DMA_ERROR_DME);
    reset_case();mode=DMA_FE_ERROR;lcd_write_byte(&io,1U);
    assert(s_stats.last.reason==LCD_ERROR_DMA && s_stats.last.dma_error==HAL_DMA_ERROR_FE);
    reset_case();mode=SPI_MODF_ERROR;lcd_write_byte(&io,1U);assert(s_stats.last.reason==LCD_ERROR_DMA);
    reset_case();mode=SPI_FRE_ERROR;lcd_write_byte(&io,1U);assert(s_stats.last.reason==LCD_ERROR_DMA);
    reset_case();mode=START_BUSY;lcd_write_byte(&io,1U);assert(s_stats.last.reason==LCD_ERROR_DMA_START);
    reset_case();mode=TAIL_BUSY;lcd_write_byte(&io,1U);assert(s_stats.last.reason==LCD_ERROR_TIMEOUT);
    reset_case();mode=LOST_IRQ;lcd_write_byte(&io,1U);assert(s_stats.last.reason==LCD_ERROR_TIMEOUT);
    reset_case();tick=UINT32_MAX-20U;mode=TIMEOUT;lcd_write_byte(&io,1U);
    assert(s_stats.last.reason==LCD_ERROR_TIMEOUT && s_stats.last.elapsed_ms==50U);
    reset_case();stuck_disable=1U;mode=TIMEOUT;lcd_write_byte(&io,1U);assert(!lcd_port_recover());
    assert(stream.CR&DMA_SxCR_EN);before=transfers;lcd_write_byte(&io,1U);assert(transfers==before);
    stuck_disable=0U;reset_case();
    fail_init=1U;assert(!lcd_port_recover());fail_init=0U;reset_case();
    fail_init=2U;assert(!lcd_port_recover() && s_stats.last.reason==LCD_ERROR_REINIT);reset_case();
    lcd_write_byte(&io,0x5AU);assert(s_init_stage!=LCD_INIT_STAGE_SPI_ERROR);
    lcd_port_get_stats(&stats);assert(stats.first.reason==LCD_ERROR_TIMEOUT && stats.errors>=7U);
    assert(!memcmp(&first,&stats.first,sizeof(first)) && !critical_depth && resets>0U);
    puts("PASS production LCD transport: prelaunch/DMA/tail races, one deadline, tick wrap, HT/TC/error priority, lost IRQ, quarantine/recovery, lifetime evidence");
    return 0;
}
