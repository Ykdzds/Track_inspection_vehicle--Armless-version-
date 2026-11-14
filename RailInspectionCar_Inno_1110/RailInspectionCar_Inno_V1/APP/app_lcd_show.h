#ifndef APP_LCD_SHOW_H
#define APP_LCD_SHOW_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

typedef enum
{
    LCD_PAGE_SERVO = 0,
    LCD_PAGE_REMOTE = 1,
    LCD_PAGE_POSITION = 2
} lcd_page_t;

/* LCD-owned state changes, including pre-scheduler init; no per-frame events. */
typedef struct
{
    uint32_t revision, at_ms;
    uint32_t rebuild_started_ms, rebuild_finished_ms;
    uint32_t rebuilds, generation, clear_count, clear_at_ms, retry_at_ms;
    uint8_t ready, stage, page, clear_reason, rebuilding;
} app_lcd_event_state_t;

void lcd_init(void);
bool app_lcd_is_ready(void);
void app_lcd_refresh(void);
void app_lcd_toggle_page(void);
lcd_page_t app_lcd_get_page(void);
TickType_t app_lcd_get_last_refresh_ticks(void);
TickType_t app_lcd_get_max_refresh_ticks(void);
uint8_t app_lcd_get_init_stage(void);
uint32_t app_lcd_get_spi_errors(void);
uint32_t app_lcd_get_rebuild_count(void);
/* Task/pre-scheduler snapshot; page is the rendered page, revision 0 is empty. */
void app_lcd_get_event_state(app_lcd_event_state_t *state);
void app_lcd_print_status(void);

#endif
