#include "app_config.h"

#if APP_CHASSIS_LCD_ENABLED
#include "main.h"
#include "lcd.h"
#include "lcd_port.h"
#include "FreeRTOS.h"
#include "task.h"
#include "spi.h"
#include <string.h>

#define LCD_SPI_TIMEOUT_MS  (20U)
#define LCD_DMA_TIMEOUT_MS  (50U)
#define LCD_DMA_CHUNK       (640U)

static volatile uint32_t s_spi_errors;
static volatile lcd_init_stage_t s_init_stage;
static lcd_port_stats_t s_stats;
/* Internal SRAM, never the caller's stack/flash/CCM buffer. */
static uint8_t s_dma_buffer[LCD_DMA_CHUNK];
static TaskHandle_t s_waiter;
static volatile uint8_t s_dma_done, s_dma_active;
static volatile uint32_t s_dma_error, s_dma_flags;

static uint32_t lcd_dma_all_flags(DMA_HandleTypeDef *dma)
{
    return __HAL_DMA_GET_TC_FLAG_INDEX(dma) | __HAL_DMA_GET_HT_FLAG_INDEX(dma) |
        __HAL_DMA_GET_TE_FLAG_INDEX(dma) | __HAL_DMA_GET_DME_FLAG_INDEX(dma) |
        __HAL_DMA_GET_FE_FLAG_INDEX(dma);
}

void lcd_port_dma_tx_isr(void)
{
    DMA_HandleTypeDef *dma = hspi1.hdmatx;
    uint32_t flags, error = 0U;
    BaseType_t higher = pdFALSE;
    if (dma == NULL) return;
    flags = __HAL_DMA_GET_FLAG(dma, lcd_dma_all_flags(dma));
    if (flags == 0U) return;
    /* Clear sampled flags only; a later TC must not be lost to an HT-only IRQ. */
    __HAL_DMA_CLEAR_FLAG(dma, flags);
    if (!s_dma_active) return;
    if (flags & __HAL_DMA_GET_TE_FLAG_INDEX(dma)) error |= HAL_DMA_ERROR_TE;
    if (flags & __HAL_DMA_GET_DME_FLAG_INDEX(dma)) error |= HAL_DMA_ERROR_DME;
    if (flags & __HAL_DMA_GET_FE_FLAG_INDEX(dma)) error |= HAL_DMA_ERROR_FE;
    s_dma_flags |= flags;
    if (error || (flags & __HAL_DMA_GET_TC_FLAG_INDEX(dma))) {
        CLEAR_BIT(hspi1.Instance->CR2, SPI_CR2_TXDMAEN);
        __HAL_DMA_DISABLE_IT(dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE | DMA_IT_DME);
        __HAL_DMA_DISABLE_IT(dma, DMA_IT_FE);
        s_dma_error |= error;
        s_dma_done = 1U;
        if (s_waiter != NULL) {
            vTaskNotifyGiveFromISR(s_waiter, &higher);
            portYIELD_FROM_ISR(higher);
        }
    }
}

static void lcd_failure(SPI_HandleTypeDef *spi, lcd_error_t reason,
                        HAL_StatusTypeDef status, uint16_t length, uint32_t start)
{
    lcd_failure_t f;
    uint8_t running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    memset(&f, 0, sizeof(f));
    f.timestamp_ms = HAL_GetTick(); f.elapsed_ms = f.timestamp_ms - start;
    f.length = length; f.reason = (uint8_t)reason; f.hal_status = (uint8_t)status;
    f.stage = (uint8_t)s_init_stage;
    if (spi != NULL) {
        f.spi_sr = spi->Instance->SR; f.spi_cr1 = spi->Instance->CR1; f.spi_cr2 = spi->Instance->CR2;
        f.remaining = spi->TxXferCount;
        if (s_dma_active && spi->hdmatx != NULL) {
            f.remaining = (uint16_t)spi->hdmatx->Instance->NDTR;
            f.dma_cr = spi->hdmatx->Instance->CR;
            f.dma_flags = s_dma_flags | __HAL_DMA_GET_FLAG(spi->hdmatx, lcd_dma_all_flags(spi->hdmatx));
            f.dma_error = s_dma_error | spi->hdmatx->ErrorCode;
        }
    }
    if (running) taskENTER_CRITICAL();
    ++s_spi_errors; s_stats.errors = s_spi_errors;
    if (s_spi_errors == 1U) s_stats.first = f;
    s_stats.last = f; s_init_stage = LCD_INIT_STAGE_SPI_ERROR;
    if (running) taskEXIT_CRITICAL();
}

/* At most 1 ms of task-context polling; never reset a DMA controller shared by UARTs. */
static bool lcd_dma_stop(void)
{
    DMA_HandleTypeDef *dma = hspi1.hdmatx;
    uint32_t start;
    uint8_t running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (dma == NULL) return true;
    CLEAR_BIT(hspi1.Instance->CR2, SPI_CR2_TXDMAEN);
    if (running) taskENTER_CRITICAL();
    s_dma_active = 0U; s_waiter = NULL;
    __HAL_DMA_DISABLE_IT(dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE | DMA_IT_DME);
    __HAL_DMA_DISABLE_IT(dma, DMA_IT_FE);
    __HAL_DMA_DISABLE(dma);
    if (running) taskEXIT_CRITICAL();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    start = DWT->CYCCNT;
    while ((dma->Instance->CR & DMA_SxCR_EN) != 0U) {
        if ((uint32_t)(DWT->CYCCNT - start) >= SystemCoreClock / 1000U) return false;
        __NOP();
    }
    __HAL_DMA_CLEAR_FLAG(dma, lcd_dma_all_flags(dma));
    dma->State = HAL_DMA_STATE_READY; dma->Lock = HAL_UNLOCKED;
    return true;
}

bool lcd_port_recover(void)
{
    uint32_t start = HAL_GetTick();
    HAL_StatusTypeDef status;
    if (!lcd_dma_stop()) {
        lcd_failure(&hspi1, LCD_ERROR_DMA_STOP, HAL_ERROR, 0U, start);
        return false;
    }
    __HAL_SPI_DISABLE(&hspi1);
    __HAL_RCC_SPI1_FORCE_RESET();
    __HAL_RCC_SPI1_RELEASE_RESET();
    hspi1.State = HAL_SPI_STATE_RESET; hspi1.Lock = HAL_UNLOCKED;
    status = HAL_SPI_Init(&hspi1);
    if (status != HAL_OK || hspi1.hdmatx == NULL ||
        hspi1.hdmatx->State != HAL_DMA_STATE_READY || hspi1.hdmatx->ErrorCode != 0U) {
        lcd_failure(&hspi1, LCD_ERROR_REINIT, status, 0U, start);
        return false;
    }
    return true;
}

void lcd_port_get_stats(lcd_port_stats_t *stats)
{
    uint8_t running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
    if (stats == NULL) return;
    if (running) taskENTER_CRITICAL();
    *stats = s_stats;
    if (running) taskEXIT_CRITICAL();
}

static bool lcd_dma_transmit(SPI_HandleTypeDef *spi, uint8_t *data, uint16_t size)
{
    HAL_StatusTypeDef status;
    uint32_t start, tail_start, now, sr;
    lcd_error_t error = LCD_ERROR_NONE;
    if (spi != &hspi1 || spi->hdmatx == NULL || s_dma_active ||
        spi->State != HAL_SPI_STATE_READY || (spi->hdmatx->Instance->CR & DMA_SxCR_EN)) {
        lcd_failure(spi, LCD_ERROR_BUSY, HAL_BUSY, size, HAL_GetTick()); return false;
    }
    memcpy(s_dma_buffer, data, size);
    (void)ulTaskNotifyTake(pdTRUE, 0U);
    taskENTER_CRITICAL();
    s_waiter = xTaskGetCurrentTaskHandle(); s_dma_done = 0U;
    s_dma_error = s_dma_flags = 0U; s_dma_active = 1U;
    spi->State = HAL_SPI_STATE_BUSY_TX; spi->ErrorCode = HAL_SPI_ERROR_NONE;
    spi->TxXferSize = spi->TxXferCount = size;
    __HAL_SPI_DISABLE(spi); SPI_1LINE_TX(spi); __HAL_SPI_ENABLE(spi);
    status = HAL_DMA_Start_IT(spi->hdmatx, (uint32_t)(uintptr_t)s_dma_buffer,
                              (uint32_t)(uintptr_t)&spi->Instance->DR, size);
    if (status == HAL_OK) {
        __HAL_DMA_DISABLE_IT(spi->hdmatx, DMA_IT_HT);
        /* Only the actual transfer consumes the fixed 50 ms deadline. */
        start = HAL_GetTick();
        SET_BIT(spi->Instance->CR2, SPI_CR2_TXDMAEN);
    } else start = HAL_GetTick();
    taskEXIT_CRITICAL();
    if (status != HAL_OK) error = LCD_ERROR_DMA_START;
    else {
        ++s_stats.dma_started;
        for (;;) {
            uint8_t done;
            /* IRQ priority 6 is masked here. Re-sample completion after the
               clock, so a completed transfer wins over a late task wake. */
            taskENTER_CRITICAL();
            now = HAL_GetTick();
            done = s_dma_done;
            if (s_dma_error) error = LCD_ERROR_DMA;
            else if (!done && (uint32_t)(now - start) >= LCD_DMA_TIMEOUT_MS)
                error = LCD_ERROR_TIMEOUT;
            taskEXIT_CRITICAL();
            if (done || error != LCD_ERROR_NONE) break;
            (void)ulTaskNotifyTake(pdTRUE, 1U);
        }
    }
    /* DMA TC means DR loaded, not the final bit on the wire. Keep DC/buffer stable. */
    if (error == LCD_ERROR_NONE) {
        tail_start = DWT->CYCCNT;
        for (;;) {
            taskENTER_CRITICAL();
            now = HAL_GetTick();
            sr = spi->Instance->SR;
            if (s_dma_error || (sr & (SPI_SR_MODF | SPI_SR_FRE))) error = LCD_ERROR_DMA;
            else if (((sr & SPI_SR_BSY) || !(sr & SPI_SR_TXE)) &&
                     (uint32_t)(now - start) >= LCD_DMA_TIMEOUT_MS)
                error = LCD_ERROR_TIMEOUT;
            taskEXIT_CRITICAL();
            if (error != LCD_ERROR_NONE || (!(sr & SPI_SR_BSY) && (sr & SPI_SR_TXE))) break;
            if ((uint32_t)(DWT->CYCCNT - tail_start) >= SystemCoreClock / 100000U) vTaskDelay(1U);
        }
    }
    if (error != LCD_ERROR_NONE) lcd_failure(spi, error, status, size, start);
    if (!lcd_dma_stop()) {
        if (error == LCD_ERROR_NONE) lcd_failure(spi, LCD_ERROR_DMA_STOP, HAL_ERROR, size, start);
        return false; /* Buffer remains quarantined until a successful recovery. */
    }
    __HAL_SPI_DISABLE(spi);
    spi->State = HAL_SPI_STATE_READY; spi->Lock = HAL_UNLOCKED; spi->TxXferCount = 0U;
    if (error != LCD_ERROR_NONE) return false;
    ++s_stats.dma_completed;
    if ((uint32_t)(HAL_GetTick() - start) > s_stats.max_transfer_ms)
        s_stats.max_transfer_ms = HAL_GetTick() - start;
    return true;
}

/************ Hardware Port ************/
void lcd_delay(uint32_t delay_ms)
{
    uint32_t cycles_per_ms;
    uint32_t start;

    if (delay_ms == 0U)
    {
        return;
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(delay_ms) ? pdMS_TO_TICKS(delay_ms) : 1U);
        return;
    }

    /*
     * LCD会在RTOS启动前初始化，使用DWT周期计数器可避免HAL时基异常
     * 导致复位延时永久阻塞。
     */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    cycles_per_ms = SystemCoreClock / 1000U;
    while (delay_ms > 0U)
    {
        start = DWT->CYCCNT;
        while ((DWT->CYCCNT - start) < cycles_per_ms)
        {
            __NOP();
        }
        delay_ms--;
    }
}

static void lcd_io_ctrl(gpio_io* io, bool flag)
{
    if ((io != NULL) && (io->port != NULL))
    {
        HAL_GPIO_WritePin(io->port, io->pin,
                          (GPIO_PinState)(flag ^ io->invert));
    }
}

/*
 * SPI1为LCD专用总线，片选由初始化代码持续拉低。
 * 该方式与已验证成功例程一致，也保证命令及其参数属于同一连续事务。
 */
static void lcd_spi_transmit(lcd_io *lcdio, uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef status;
    uint16_t chunk;
    uint32_t start;

    if ((lcdio == NULL) || (lcdio->spi == NULL) ||
        (data == NULL) || (len == 0U))
    {
        lcd_failure(NULL, LCD_ERROR_ARGUMENT, HAL_ERROR, 0U, HAL_GetTick());
        return;
    }
    if (s_init_stage == LCD_INIT_STAGE_SPI_ERROR)
    {
        /* 首次SPI错误后终止本轮整屏写入，避免重复等待发送超时。 */
        return;
    }
    while (len > 0U)
    {
        chunk = (uint16_t)((len > LCD_DMA_CHUNK) ? LCD_DMA_CHUNK : len);
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
            if (!lcd_dma_transmit(lcdio->spi, data, chunk)) break;
            data += chunk; len -= chunk; continue;
        }
        ++s_stats.polling;
        start = HAL_GetTick();
        status = HAL_SPI_Transmit(lcdio->spi, data, chunk,
                                  LCD_SPI_TIMEOUT_MS);
        if (status != HAL_OK)
        {
            lcd_failure(lcdio->spi, LCD_ERROR_POLL, status, chunk, start);
            break;
        }
        data += chunk;
        len -= chunk;
    }
}

/************ GPIO ************/
void lcd_io_rst(lcd_io* lcdio, bool flag)
{
    lcd_io_ctrl(&lcdio->rst, flag);
}

void lcd_io_bl(lcd_io* lcdio, bool flag)
{
    lcd_io_ctrl(&lcdio->bl, flag);
}

void lcd_io_cs(lcd_io* lcdio, bool flag)
{
    lcd_io_ctrl(&lcdio->cs, flag);
}

void lcd_io_dc(lcd_io* lcdio, bool flag)
{
    if (s_init_stage == LCD_INIT_STAGE_SPI_ERROR) return;
    lcd_io_ctrl(&lcdio->dc, flag);
}

/************ SPI ************/
void lcd_write_byte(lcd_io* lcdio, uint8_t data)
{
    lcd_io_dc(lcdio, true);
    lcd_spi_transmit(lcdio, &data, 1U);
}

void lcd_write_halfword(lcd_io* lcdio, uint16_t data)
{
    lcd_io_dc(lcdio, true);
    /* HAL按内存顺序发送，先交换字节以匹配LCD的高字节优先格式。 */
    data = (uint16_t)((data << 8U) | (data >> 8U));
    lcd_spi_transmit(lcdio, (uint8_t *)&data, 2U);
}

void lcd_write_bulk(lcd_io* lcdio, uint8_t* data, uint32_t len)
{
    lcd_io_dc(lcdio, true);
    lcd_spi_transmit(lcdio, data, len);
}

void lcd_write_reg(lcd_io* lcdio, uint8_t data)
{
    lcd_io_dc(lcdio, false);
    lcd_spi_transmit(lcdio, &data, 1U);
}

void lcd_port_reset_stats(void)
{
    /* Lifetime errors survive display reconstruction. */
    s_init_stage = LCD_INIT_STAGE_IDLE;
}

uint32_t lcd_port_get_spi_errors(void)
{
    return s_spi_errors;
}

void lcd_port_set_init_stage(lcd_init_stage_t stage)
{
    /* SPI错误阶段优先保留，避免后续初始化步骤覆盖真正故障位置。 */
    if (s_init_stage != LCD_INIT_STAGE_SPI_ERROR)
    {
        if (stage == LCD_INIT_STAGE_FRAME_WRITE) ++s_stats.init_clears;
        s_init_stage = stage;
    }
}

lcd_init_stage_t lcd_port_get_init_stage(void)
{
    return s_init_stage;
}

#else
#include "lcd_port.h"
#include <string.h>
void lcd_port_dma_tx_isr(void) {}
void lcd_port_get_stats(lcd_port_stats_t *stats)
{ if (stats) memset(stats,0,sizeof(*stats)); }
bool lcd_port_recover(void) { return false; }
void lcd_delay(uint32_t delay) { (void)delay; }
void lcd_io_rst(lcd_io *io,bool flag) { (void)io;(void)flag; }
void lcd_io_bl(lcd_io *io,bool flag) { (void)io;(void)flag; }
void lcd_io_cs(lcd_io *io,bool flag) { (void)io;(void)flag; }
void lcd_io_dc(lcd_io *io,bool flag) { (void)io;(void)flag; }
void lcd_write_byte(lcd_io *io,uint8_t value) { (void)io;(void)value; }
void lcd_write_halfword(lcd_io *io,uint16_t value) { (void)io;(void)value; }
void lcd_write_bulk(lcd_io *io,uint8_t *value,uint32_t n) { (void)io;(void)value;(void)n; }
void lcd_write_reg(lcd_io *io,uint8_t value) { (void)io;(void)value; }
void lcd_port_reset_stats(void) {}
uint32_t lcd_port_get_spi_errors(void) { return 0U; }
void lcd_port_set_init_stage(lcd_init_stage_t stage) { (void)stage; }
lcd_init_stage_t lcd_port_get_init_stage(void) { return LCD_INIT_STAGE_IDLE; }

#endif
