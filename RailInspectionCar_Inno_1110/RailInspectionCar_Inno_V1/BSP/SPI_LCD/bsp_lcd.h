#ifndef BSP_LCD_H
#define BSP_LCD_H

#include "lcd.h"

lcd *bsp_lcd_init(void);
uint8_t bsp_lcd_is_ready(void);
uint32_t bsp_lcd_get_spi_errors(void);
lcd_init_stage_t bsp_lcd_get_init_stage(void);

#endif
