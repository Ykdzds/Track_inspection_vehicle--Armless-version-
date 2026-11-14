#ifndef __LCD_PORT_H
#define __LCD_PORT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct __gpio_io {
    void* port;
    uint16_t pin;
    bool invert;
} gpio_io;

typedef struct __lcd_io {
    void* spi;
    gpio_io rst;
    gpio_io bl;
    gpio_io cs;
    gpio_io dc;
    gpio_io te;
} lcd_io;

typedef enum {
    LCD_INIT_STAGE_IDLE = 0,
    LCD_INIT_STAGE_RESET_ASSERTED,
    LCD_INIT_STAGE_RESET_RELEASED,
    LCD_INIT_STAGE_SLEEP_OUT,
    LCD_INIT_STAGE_DISPLAY_ON,
    LCD_INIT_STAGE_FRAME_WRITE,
    LCD_INIT_STAGE_READY,
    LCD_INIT_STAGE_SPI_ERROR = 0xFF
} lcd_init_stage_t;

typedef enum {
    LCD_ERROR_NONE, LCD_ERROR_ARGUMENT, LCD_ERROR_BUSY, LCD_ERROR_POLL,
    LCD_ERROR_DMA_START, LCD_ERROR_DMA, LCD_ERROR_TIMEOUT, LCD_ERROR_DMA_STOP,
    LCD_ERROR_REINIT
} lcd_error_t;
typedef struct {
    uint32_t timestamp_ms, elapsed_ms, spi_sr, spi_cr1, spi_cr2, dma_cr, dma_flags;
    uint32_t dma_error;
    uint16_t length, remaining;
    uint8_t reason, hal_status, stage;
} lcd_failure_t;
typedef struct {
    uint32_t errors, dma_started, dma_completed, polling, max_transfer_ms, init_clears;
    lcd_failure_t first, last;
} lcd_port_stats_t;

/* SPI1-only ISR: no HAL SPI tail polling and no printing in interrupt context. */
void lcd_port_dma_tx_isr(void);
void lcd_port_get_stats(lcd_port_stats_t *stats);
bool lcd_port_recover(void);

void lcd_delay(uint32_t delay);
void lcd_io_rst(lcd_io* lcdio, bool flag);
void lcd_io_bl(lcd_io* lcdio, bool flag);
void lcd_io_cs(lcd_io* lcdio, bool flag);
void lcd_io_dc(lcd_io* lcdio, bool flag);
/****** 底层接口 SPI  ******/
void lcd_write_byte(lcd_io* lcdio, uint8_t data);
void lcd_write_halfword(lcd_io* lcdio, uint16_t data);
void lcd_write_bulk(lcd_io* lcdio, uint8_t* data, uint32_t len);
void lcd_write_reg(lcd_io* lcdio, uint8_t data);
void lcd_port_reset_stats(void);
uint32_t lcd_port_get_spi_errors(void);
void lcd_port_set_init_stage(lcd_init_stage_t stage);
lcd_init_stage_t lcd_port_get_init_stage(void);

#endif
