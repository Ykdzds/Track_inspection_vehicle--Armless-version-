#include "FreeRTOS.h"
#include "task.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lcd.h"
#include "bsp_lcd.h"
#include <stdarg.h>
static lcd_init_stage_t stage;
lcd_init_stage_t bsp_lcd_get_init_stage(void) { return stage; }
/* Exact production formatter extracted by the runner, with its cache. */
#include "rail_lcd_cache_under_test.inc"

static unsigned writes;
static uint16_t last_x, last_y;
static char last_text[41];
static uint8_t fail_write;
void lcd_show_string(lcd *display, uint16_t x, uint16_t y, const uint8_t *text)
{
    (void)display; ++writes; last_x = x; last_y = y;
    assert(strlen((const char *)text) <= 40U);
    strcpy(last_text, (const char *)text);
    if (fail_write) stage = LCD_INIT_STAGE_SPI_ERROR;
}
int main(void)
{
    lcd display;
    unsigned i;
    static const uint16_t rows16[] = {0,16,32,48,64,80,96,112,128,144};
    static const uint16_t rows20[] = {0,20,40,60,80,100,120,140};
    memset(&display,0,sizeof(display)); display.font.width=8U;
    for (unsigned page=0U; page<2U; ++page)
    {
        const uint16_t *rows=page ? rows20 : rows16;
        unsigned count=page ? 8U : 10U;
        memset(s_line_valid,0,sizeof(s_line_valid)); writes=0U;
        for(i=0U;i<count;++i) app_lcd_line(&display,0U,rows[i],"row %u",i);
        assert(writes==count);
        for(i=0U;i<count;++i) app_lcd_line(&display,0U,rows[i],"row %u",i);
        assert(writes==count);
        app_lcd_line(&display,0U,rows[count-1U],"row X");
        assert(writes==count+1U && last_x==32U && last_y==rows[count-1U]);
        assert(strcmp(last_text,"X")==0);
        app_lcd_line(&display,0U,rows[count-1U],"r");
        assert(writes==count+2U && last_x==8U && strcmp(last_text,"    ")==0);
    }
    memset(s_line_valid,0,sizeof(s_line_valid)); stage=LCD_INIT_STAGE_READY;
    fail_write=1U; app_lcd_line(&display,0U,0U,"new content");
    assert(!s_line_valid[0]);
    { unsigned before=writes; app_lcd_line(&display,0U,0U,"more"); assert(writes==before); }
    stage=LCD_INIT_STAGE_READY;fail_write=0U;
    app_lcd_line(&display,0U,0U,"new content");
    assert(s_line_valid[0] && last_x==0U && strlen(last_text)==40U);
    puts("PASS LCD cached production formatter: independent 16/20px rows, unchanged rows suppressed, changed span and old-text erasure");
    return 0;
}
