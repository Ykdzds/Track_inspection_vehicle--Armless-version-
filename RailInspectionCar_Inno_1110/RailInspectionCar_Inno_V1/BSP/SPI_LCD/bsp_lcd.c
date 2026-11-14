#include "app_config.h"
#include <stddef.h>

#if APP_CHASSIS_LCD_ENABLED
#include "bsp_lcd.h"

#include "main.h"
#include "spi.h"

static uint16_t s_line_buffer[320];
static uint8_t s_lcd_ready;

static lcd_io s_lcd_io = {
    .spi = &hspi1,
    .rst = {LCD_RST_GPIO_Port, LCD_RST_Pin, 0},
    .bl  = {LCD_PWR_GPIO_Port, LCD_PWR_Pin, 1},
    .cs  = {LCD_CS_GPIO_Port, LCD_CS_Pin, 0},
    .dc  = {LCD_DC_GPIO_Port, LCD_DC_Pin, 0},
    .te  = {0}
};

static lcd s_lcd = {
    .io = &s_lcd_io,
    .line_buffer = s_line_buffer
};

lcd *bsp_lcd_init(void)
{
    s_lcd_ready = 0U;
    if (!lcd_port_recover()) return NULL;
    lcd_port_reset_stats();

    /* SPI1仅连接LCD，片选按成功例程在整个初始化和刷新期间保持有效。 */
    lcd_io_cs(&s_lcd_io, false);
    lcd_io_dc(&s_lcd_io, false);
    lcd_io_rst(&s_lcd_io, false);
    lcd_init_dev(&s_lcd, LCD_1_47_INCH, LCD_ROTATE_90);

    if (lcd_port_get_init_stage() == LCD_INIT_STAGE_SPI_ERROR)
    {
        return NULL;
    }
    lcd_port_set_init_stage(LCD_INIT_STAGE_READY);
    s_lcd_ready = 1U;
    return &s_lcd;
}

uint8_t bsp_lcd_is_ready(void)
{
    return s_lcd_ready;
}

uint32_t bsp_lcd_get_spi_errors(void)
{
    return lcd_port_get_spi_errors();
}

lcd_init_stage_t bsp_lcd_get_init_stage(void)
{
    return lcd_port_get_init_stage();
}

#else
#include "bsp_lcd.h"
lcd *bsp_lcd_init(void) { return NULL; }
uint8_t bsp_lcd_is_ready(void) { return 0U; }
uint32_t bsp_lcd_get_spi_errors(void) { return 0U; }
lcd_init_stage_t bsp_lcd_get_init_stage(void) { return LCD_INIT_STAGE_IDLE; }

#endif
