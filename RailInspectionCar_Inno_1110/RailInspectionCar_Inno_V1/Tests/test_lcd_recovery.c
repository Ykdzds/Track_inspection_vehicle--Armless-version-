#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_lcd_show.h"
#include "app_health.h"
#include "bsp_lcd.h"
static uint32_t now, attempts, clears, diagnostic;
static uint8_t fail_init;
static uint8_t scheduler;
static unsigned critical_calls, critical_depth;
static lcd display;
static lcd_init_stage_t stage;
static lcd_port_stats_t stats;
#define taskSCHEDULER_RUNNING 2
#undef taskENTER_CRITICAL
#undef taskEXIT_CRITICAL
#define taskENTER_CRITICAL() do {assert(scheduler==taskSCHEDULER_RUNNING);++critical_calls;++critical_depth;} while(0)
#define taskEXIT_CRITICAL() do {assert(critical_depth);--critical_depth;} while(0)
int xTaskGetSchedulerState(void) {return scheduler;}
uint32_t HAL_GetTick(void) {return now;}
void lcd_port_get_stats(lcd_port_stats_t *out) {*out=stats;}
lcd *bsp_lcd_init(void) {
    app_lcd_event_state_t event;
    app_lcd_get_event_state(&event);
    assert(event.rebuilding && event.rebuild_started_ms==now && !event.rebuild_finished_ms);
    assert(!critical_depth); /* No I/O or rebuild waiting in a critical section. */
    ++attempts;
    if(fail_init) {stage=LCD_INIT_STAGE_SPI_ERROR;return NULL;}
    ++stats.init_clears; stage=LCD_INIT_STAGE_READY; return &display;
}
uint8_t bsp_lcd_is_ready(void) {return stage==LCD_INIT_STAGE_READY;}
lcd_init_stage_t bsp_lcd_get_init_stage(void) {return stage;}
void app_health_set_diagnostic_fault(uint32_t value) {diagnostic|=value;}
void lcd_clear(lcd *d,uint16_t color) {assert(d==&display && color==BLACK);++clears;}
#include "rail_lcd_recovery_under_test.inc"
int main(void)
{
    unsigned i;
    uint32_t revision;
    app_lcd_event_state_t event;
    const uint32_t delay[]={2000U,4000U,8000U,10000U,10000U};
    assert(sizeof(app_lcd_event_state_t)==44U);
    app_lcd_get_event_state(&event); assert(!event.revision && !critical_calls);
    now=100U;
    fail_init=1U;
    for(i=0U;i<5U;++i) {
        lcd_init(); assert(attempts==i+1U && !app_lcd_is_ready());
        assert(s_lcd_retry_at-now==delay[i] && !s_clear_count);
        app_lcd_get_event_state(&event);
        assert(event.revision==2U*(i+1U) && !event.rebuilding && !event.ready);
        assert(event.rebuild_started_ms==now && event.rebuild_finished_ms==now);
        assert(event.rebuilds==i+1U && event.retry_at_ms==s_lcd_retry_at);
        if(!i) {assert(!critical_calls);scheduler=taskSCHEDULER_RUNNING;}
        revision=event.revision;
        now=s_lcd_retry_at-1U; lcd_init(); assert(attempts==i+1U);
        app_lcd_get_event_state(&event);assert(event.revision==revision);
        ++now;
    }
    fail_init=0U; lcd_init(); assert(app_lcd_is_ready() && s_display_generation==1U);
    assert(s_clear_count==1U && s_clear_reason==2U);
    app_lcd_get_event_state(&event);
    assert(event.ready && event.generation==1U && event.clear_count==1U && event.clear_reason==2U);
    revision=event.revision;
    s_line_valid[0]=1U; now+=2000U; app_lcd_frame_begin(LCD_PAGE_SERVO);
    assert(!s_line_valid[0] && s_full_redraws==1U && !clears);
    app_lcd_frame_end(); assert(!s_retry_failures);
    app_lcd_get_event_state(&event);assert(event.revision==revision); /* No periodic flood. */
    s_line_valid[0]=1U; app_lcd_toggle_page(); app_lcd_frame_begin(app_lcd_get_page());
    assert(clears==1U && !s_line_valid[0] && s_clear_reason==3U);
    app_lcd_get_event_state(&event);
    assert(event.revision==revision+1U && event.clear_reason==3U && event.page==LCD_PAGE_REMOTE);
    assert(event.clear_at_ms==now && event.clear_count==2U);
    stage=LCD_INIT_STAGE_SPI_ERROR; s_line_valid[0]=1U; app_lcd_frame_end();
    assert(!app_lcd_is_ready() && !s_lcd && !s_line_valid[0]);
    assert(diagnostic==APP_DIAG_LCD_SPI_ERROR && s_lcd_retry_at-now==2000U);
    app_lcd_get_event_state(&event);
    assert(!event.ready && event.stage==LCD_INIT_STAGE_SPI_ERROR && !event.rebuilding);
    now=s_lcd_retry_at; lcd_init(); assert(app_lcd_is_ready() && s_display_generation==2U);
    /* Recovery that succeeds at init but fails while drawing still backs off. */
    stage=LCD_INIT_STAGE_SPI_ERROR; app_lcd_frame_end(); assert(s_lcd_retry_at-now==4000U);
    assert(critical_calls && !critical_depth);
    puts("PASS production LCD recovery: pre-scheduler safety, coherent start/finish events, no periodic flood, backoff 2/4/8/10s, page clear, failed frame/rebuild");
    return 0;
}
