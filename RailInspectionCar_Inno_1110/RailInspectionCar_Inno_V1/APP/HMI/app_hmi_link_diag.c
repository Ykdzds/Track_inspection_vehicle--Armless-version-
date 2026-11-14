#include "app_hmi_link_diag.h"
#include "app_link_diag.h"

#if APP_LINK_DIAG_ACTIVE
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app_boot_identity.h"
#include "app_hmi_control.h"
#include "app_hmi_snapshot.h"
#if APP_CHASSIS_LCD_ENABLED
#include "app_lcd_show.h"
#include "lcd_port.h"
#endif
#include "app_tune_diagnostics.h"
#include "app_runtime_metrics.h"
#include "bsp_hmi_uart.h"
#include "bsp_system.h"
#include "debug_uart.h"

#if !defined(__CC_ARM) && !defined(__arm__)
size_t xPortGetFreeHeapSize(void);
size_t xPortGetMinimumEverFreeHeapSize(void);
#endif
#define DIAG_BUDGET 6000U
#define DIAG_UART_RESERVE 512U
typedef struct {
    uint32_t at, number, data[6];
    uint16_t kind;
} link_event_t;

/* All format/capture scratch storage is static and CLI-owned. */
static link_event_t events[APP_HMI_LINK_DIAG_FIFO_SIZE];
static uint16_t read_at, write_at, count;
static uint32_t boot, serial, overflow, output_dropped, budget_at;
static uint16_t budget_used, normal_used;
static uint8_t priority_output, source_cursor, first_cursor, compact_phase;
static uint32_t compact_at, health_at, sampled;
static uint32_t hmi_sample_at[3];
static uint8_t hmi_sample_seen;
static uint32_t can_sample_at[3];
static uint8_t can_sample_seen, queued_by_source[APP_LINK_CHANNELS];
typedef struct { uint32_t id; uint8_t part, source, phase; } projection_cursor_t;
static projection_cursor_t projection_cursor[2];
static uint8_t health_source, health_page, state_page;
static uint32_t stop_merged;
typedef struct {
    uint32_t at, number, words[16], merged;
    uint16_t kind;
    uint8_t length, page;
} route_first_t;
static route_first_t route_first[4];
/* One immutable boot record.  It remains available after successful output;
 * subsequent stop paths may add only the merged count, never replace cause. */
typedef struct {
    uint32_t at, number, words[24], merged;
    uint8_t length, page, delivered;
} startup_first_t;
static startup_first_t startup_first;
typedef char startup_first_budget[(sizeof(startup_first_t) <= 128U) ? 1 : -1];
typedef char first_budget[(sizeof(route_first_t) == 80U) ? 1 : -1];
static const uint8_t enabled_sources[APP_LINK_CHANNELS] = {
    !!APP_HMI_LINK_DIAG_ENABLED, !!APP_NRF_DIAGNOSTIC_ENABLED,
    !!APP_CAN_SERVO_DIAG_ENABLED, !!APP_CAN_ARM_DIAG_ENABLED,
    !!APP_SERVO_RS485_DIAG_ENABLED, !!APP_CAN_DEBUG_ENABLED,
    !!APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED
};
static uint8_t enabled(uint8_t source) { return source < APP_LINK_CHANNELS && enabled_sources[source]; }
static unsigned source_count(void) {
    unsigned i,n=0U; for(i=0U;i<5U;++i) n+=enabled_sources[i]; return n;
}
static uint32_t state_period(void) { return source_count()>1U ? 250U : 100U; }
static uint32_t health_period(void) { return source_count()>1U ? 2000U : 1000U; }
static const char *source_name(uint8_t source) {
    static const char *const names[]={"HMI","RF","CS","CA","S485","CANRAW","SYNC"};
    return source<APP_LINK_CHANNELS ? names[source] : "UNKNOWN";
}
static uint32_t status_at, summary_at;
static uint32_t snapshot_summary_at;
static uint8_t boot_phase, summary_phase;
static app_control_stop_t first_stop;
static uint32_t first_revision, first_printed, first_copy_revision;
static char line[256];
static uint16_t screen[2][APP_HMI_LINK_DIAG_WORDS];

static uint32_t screen_id;
static uint8_t screen_mask, screen_dirty;
static uint32_t screen_conflict, screen_replaced, screen_reused;
#if APP_CHASSIS_LCD_ENABLED
typedef struct {
    app_lcd_event_state_t ui;
    lcd_port_stats_t port;
} lcd_diag_t;
/* CLI-owned pending group is immutable until its last line is accepted. */
static lcd_diag_t lcd_pending;
static uint32_t lcd_seen_revision, lcd_seen_errors, lcd_delivered_revision, lcd_delivered_errors;
static uint32_t lcd_merged, lcd_group_merged, lcd_line_at;
static uint8_t lcd_phase, lcd_line_sent;
#endif
/* No member is live across a CLI yield/output retry. Persistent pending
 * records stay outside this union. Each summary page samples independently. */
static union {
    app_hmi_control_status_t control;
    app_tune_diag_system_t system_diag;
    hmi_uart_diag_t uart_diag;
    hmi_uart_stats_t uart_stats;
    app_runtime_metrics_t runtime;
    bsp_system_fault_record_t fault;
    app_control_stop_t first_copy;
#if APP_CHASSIS_LCD_ENABLED
    lcd_diag_t lcd_probe;
#endif
    app_hmi_snapshot_diag_t snapshot_diag;
    link_event_t work;
    uint16_t screen_copy[APP_HMI_LINK_DIAG_WORDS];
    app_link_diag_snapshot_t compact;
} scratch;
#if defined(__CC_ARM) || defined(__arm__)
typedef char scratch_budget[(sizeof(scratch) <= 448U) ? 1 : -1];
#else
/* Host enums use int; ARMCC packs the production enum fields. */
typedef char scratch_host_budget[(sizeof(scratch) <= 512U) ? 1 : -1];
#endif
#define control scratch.control
#define system_diag scratch.system_diag
#define uart_diag scratch.uart_diag
#define uart_stats scratch.uart_stats
#define runtime scratch.runtime
#define fault scratch.fault
#define first_copy scratch.first_copy
#if APP_CHASSIS_LCD_ENABLED
#define lcd_probe scratch.lcd_probe
#endif
#define snapshot_diag scratch.snapshot_diag
#define work scratch.work
#define screen_copy scratch.screen_copy
/* Producer publishes once, then the record is immutable through both output
 * pages. Only CLI acknowledges it; repeated overruns change counters only. */
static app_hmi_link_transaction_t slow_first;
static uint32_t slow_warnings, slow_deadlines, slow_merged;
static uint8_t slow_pending, slow_page;

#define U(v) ((unsigned long)(v))
static uint8_t emit(int length)
{
    uint32_t now = HAL_GetTick();
    if (length <= 0 || (unsigned)length >= sizeof(line)) {
        ++output_dropped;
        return 0U;
    }
    if ((uint32_t)(now - budget_at) >= 1000U) {
        budget_at = now;
        budget_used = normal_used = 0U;
    }
    if ((uint32_t)budget_used + (unsigned)length > DIAG_BUDGET ||
        (!priority_output && (uint32_t)normal_used + (unsigned)length > 4000U)) return 0U;
    if (!debug_uart_try_write_frame((const uint8_t *)line, (uint16_t)length,
                                    DIAG_UART_RESERVE)) {
        ++output_dropped;
        return 0U;
    }
    budget_used = (uint16_t)(budget_used + length);
    if (!priority_output) normal_used = (uint16_t)(normal_used + length);
    return 1U;
}

void app_hmi_link_diag_init(void)
{
    taskENTER_CRITICAL();
    memset(events, 0, sizeof(events));
    memset(&first_stop, 0, sizeof(first_stop));
    memset(screen, 0, sizeof(screen));
    read_at = write_at = count = 0U;
    serial = overflow = output_dropped = 0U;
    first_revision = first_printed = 0U;
    screen_id = screen_conflict = screen_replaced = screen_reused = 0U;
    screen_mask = screen_dirty = 0U;
    boot = app_boot_identity_id();
    budget_at = status_at = summary_at = HAL_GetTick();
    snapshot_summary_at = summary_at;
    budget_used = normal_used = 0U; boot_phase = 1U; summary_phase = 0U;
    priority_output=source_cursor=first_cursor=compact_phase=0U;
    compact_at=health_at=HAL_GetTick(); sampled=0U;
    memset(route_first,0,sizeof(route_first));
    memset(&startup_first,0,sizeof(startup_first));
    memset(hmi_sample_at,0,sizeof(hmi_sample_at)); hmi_sample_seen=0U;
    memset(can_sample_at,0,sizeof(can_sample_at));can_sample_seen=0U;
    memset(queued_by_source,0,sizeof(queued_by_source));
    memset(projection_cursor,0,sizeof(projection_cursor));
    health_source=health_page=state_page=0U;stop_merged=0U;
#if APP_CHASSIS_LCD_ENABLED
    memset(&lcd_pending, 0, sizeof(lcd_pending));
    memset(&lcd_probe, 0, sizeof(lcd_probe));
    lcd_seen_revision = lcd_seen_errors = lcd_delivered_revision = lcd_delivered_errors = 0U;
    lcd_merged = lcd_group_merged = lcd_line_at = 0U;
    lcd_phase = lcd_line_sent = 0U;
#endif
    memset(&slow_first, 0, sizeof(slow_first));
    slow_warnings = slow_deadlines = slow_merged = 0U;
    slow_pending = slow_page = 0U;
    taskEXIT_CRITICAL();
}

#if defined(APP_HMI_LINK_DIAG_TEST) || defined(APP_LINK_DIAG_TEST)
void app_hmi_link_diag_test_seed_serial(uint32_t value) { serial = value; }
#endif

void app_link_diag_startup_first(const uint32_t *words, uint8_t length)
{
    if(words==NULL || length==0U)return;
    if(length>24U)length=24U;
    taskENTER_CRITICAL();
    if(startup_first.length)++startup_first.merged;
    else {
        if(++serial==0U)++serial;
        startup_first.at=HAL_GetTick();startup_first.number=serial;
        memcpy(startup_first.words,words,(size_t)length*sizeof(words[0]));
        startup_first.page=startup_first.delivered=0U;
        startup_first.length=length;
    }
    taskEXIT_CRITICAL();
}

void app_hmi_link_diag_complete(const app_hmi_link_transaction_t *transaction)
{
    uint32_t elapsed, hz = bsp_hmi_uart_clock_hz();
    if (transaction == NULL || transaction->id == 0U || hz == 0U) return;
    elapsed = transaction->completed - transaction->capture;
    if (elapsed < hz / 25U) return;
    taskENTER_CRITICAL();
    ++slow_warnings;
    if (elapsed >= hz / 20U)
    {
        ++slow_deadlines;
        if (slow_pending) ++slow_merged;
        else
        {
            slow_first = *transaction;
            slow_page = 0U;
            slow_pending = 1U;
        }
    }
    taskEXIT_CRITICAL();
    app_hmi_link_diag_record(APP_HMI_LINK_SLOW_WARNING, transaction->id,
        transaction->start, transaction->quantity, elapsed,
        transaction->capture, transaction->completed);
}

static uint8_t print_slow(void)
{
    const app_hmi_link_transaction_t *t = &slow_first;
    int n;
    uint8_t pending;
    taskENTER_CRITICAL(); pending = slow_pending; taskEXIT_CRITICAL();
    if (!pending) return 0U;
    if (!slow_page)
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX %lX SLOW p=0 at=%lX req=%X/%X fc=%X len=%X gen=%lX flags=%X cap=%lX boundary=%lX service=%lX parse=%lX submit=%lX tc=%lX\r\n",
            U(HAL_GetTick()), U(boot), U(t->id), U(t->at_ms), t->start,
            t->quantity, t->function, t->length, U(t->rx_generation), t->flags,
            U(t->capture), U(t->boundary), U(t->service), U(t->parsed),
            U(t->submitted), U(t->completed));
    else
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX %lX SLOW p=1 src=%lX/%lX copy=%lX encode=%lX total=%lX hz=%lX warn=%lX deadline=%lX merged=%lX\r\n",
            U(HAL_GetTick()), U(boot), U(t->id), U(t->source_ms), U(t->source_sequence),
            U(t->copy_cycles), U(t->encode_cycles), U(t->completed - t->capture),
            U(bsp_hmi_uart_clock_hz()), U(slow_warnings), U(slow_deadlines), U(slow_merged));
    if (emit(n))
    {
        if (!slow_page) slow_page = 1U;
        else
        {
            taskENTER_CRITICAL(); slow_pending = slow_page = 0U; taskEXIT_CRITICAL();
        }
    }
    return 1U;
}

#if APP_CHASSIS_LCD_ENABLED
static void capture_lcd(void)
{
    app_lcd_get_event_state(&lcd_probe.ui);
    lcd_port_get_stats(&lcd_probe.port);
    if (lcd_probe.ui.revision != lcd_seen_revision || lcd_probe.port.errors != lcd_seen_errors) {
        if (lcd_phase) ++lcd_merged;
        lcd_seen_revision = lcd_probe.ui.revision;
        lcd_seen_errors = lcd_probe.port.errors;
    }
    if (!lcd_phase && (lcd_probe.ui.revision != lcd_delivered_revision ||
                       lcd_probe.port.errors != lcd_delivered_errors)) {
        lcd_pending = lcd_probe;
        lcd_group_merged = lcd_merged;
        lcd_phase = 1U;
    }
}

static void print_lcd(uint32_t now)
{
    const lcd_failure_t *failure;
    int n;
    if (!lcd_phase || (lcd_line_sent && (uint32_t)(now - lcd_line_at) < 1000U)) return;
    if (lcd_phase == 1U) {
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX %lX LCD ui at=%lu ready=%u stage=%u page=%u rebuilding=%u start=%lu end=%lu rebuild=%lu gen=%lu clear=%lu reason=%u clear_at=%lu retry=%lu merged=%lu\r\n",
            U(now), U(boot), U(lcd_pending.ui.revision), U(lcd_pending.ui.at_ms),
            lcd_pending.ui.ready, lcd_pending.ui.stage, lcd_pending.ui.page, lcd_pending.ui.rebuilding,
            U(lcd_pending.ui.rebuild_started_ms), U(lcd_pending.ui.rebuild_finished_ms),
            U(lcd_pending.ui.rebuilds), U(lcd_pending.ui.generation), U(lcd_pending.ui.clear_count),
            lcd_pending.ui.clear_reason, U(lcd_pending.ui.clear_at_ms), U(lcd_pending.ui.retry_at_ms),
            U(lcd_group_merged));
    } else if (lcd_phase == 2U || lcd_phase == 3U) {
        failure = lcd_phase == 2U ? &lcd_pending.port.first : &lcd_pending.port.last;
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX %lX LCD %s at=%lu elapsed=%lu reason=%u hal=%u stage=%u len=%u left=%u sr=%lX cr1=%lX cr2=%lX dma=%lX flags=%lX err=%lX\r\n",
            U(now), U(boot), U(lcd_pending.ui.revision), lcd_phase == 2U ? "first" : "last",
            U(failure->timestamp_ms), U(failure->elapsed_ms), failure->reason, failure->hal_status,
            failure->stage, failure->length, failure->remaining, U(failure->spi_sr),
            U(failure->spi_cr1), U(failure->spi_cr2), U(failure->dma_cr), U(failure->dma_flags),
            U(failure->dma_error));
    } else {
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX %lX LCD count errors=%lu dma=%lu/%lu poll=%lu max_ms=%lu init_clear=%lu merged=%lu\r\n",
            U(now), U(boot), U(lcd_pending.ui.revision), U(lcd_pending.port.errors),
            U(lcd_pending.port.dma_started), U(lcd_pending.port.dma_completed),
            U(lcd_pending.port.polling), U(lcd_pending.port.max_transfer_ms),
            U(lcd_pending.port.init_clears), U(lcd_group_merged));
    }
    if (!emit(n)) return;
    lcd_line_at = now; lcd_line_sent = 1U;
    if (lcd_phase == 4U) {
        lcd_delivered_revision = lcd_pending.ui.revision;
        lcd_delivered_errors = lcd_pending.port.errors;
        lcd_phase = 0U;
    } else ++lcd_phase;
}

#else
#define capture_lcd() ((void)0)
#define print_lcd(now) ((void)(now))
#endif

void app_link_diag_record(uint8_t source, uint16_t kind, uint32_t a, uint32_t b,
    uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    link_event_t *slot;
    uint8_t sample_index=255U,index;
    uint32_t now=HAL_GetTick();
    if(!APP_HMI_LINK_DIAG_ENABLED && source==APP_LINK_HMI && kind==APP_HMI_LINK_STOP_UPGRADE) {
        for(source=1U;source<APP_LINK_CHANNELS;++source)if(enabled(source))break;
        kind=(uint16_t)(0x3E0U+kind);
    }
    if (!enabled(source)) return;
    if(source==APP_LINK_HMI) {
        if(kind==APP_HMI_LINK_RX_REQUEST && b==3U)sample_index=3U;
        if(kind==APP_HMI_LINK_TX_SUBMIT && c && b>5U)sample_index=4U;
        if(kind==APP_HMI_LINK_TX_COMPLETE)sample_index=5U;
    }
    if(source==APP_LINK_CAN_SERVO && kind==0x101U && (e==0U || e==14U))sample_index=0U;
    if(source==APP_LINK_CAN_SERVO && kind==0x103U && e==0U)sample_index=1U;
    /* Only a successful, held ALIVE on the expected page is routine. */
    if(source==APP_LINK_CAN_ARM && kind==0x200U && ((b>>8U)&255U)==2U &&
       (b&0x03000000UL)==0x03000000UL && f==0U)sample_index=2U;
    taskENTER_CRITICAL();
    /* Driver target evidence uses the existing write sequence for paired
     * sampling. Zero and either health-clamp change always remain visible. */
    if(source==APP_LINK_RS485 && (kind==0x20U || kind==0x21U) &&
       (uint16_t)b!=0U && (uint16_t)(b>>16U)==(uint16_t)b &&
       (((kind==0x20U?a:f)&7U)!=1U)) {
        ++sampled;taskEXIT_CRITICAL();return;
    }
    if(sample_index>=3U && sample_index<6U) {
        index=(uint8_t)(sample_index-3U);
        if((hmi_sample_seen&(1U<<index)) && (uint32_t)(now-hmi_sample_at[index])<200U) {
            ++sampled;taskEXIT_CRITICAL();return;
        }
        hmi_sample_seen|=(uint8_t)(1U<<index);hmi_sample_at[index]=now;
    }
    if(sample_index<3U) {
        if((can_sample_seen&(1U<<sample_index)) &&
           (uint32_t)(now-can_sample_at[sample_index])<200U) {
            ++sampled;taskEXIT_CRITICAL();return;
        }
        can_sample_seen|=(uint8_t)(1U<<sample_index);can_sample_at[sample_index]=now;
    }
    if (++serial == 0U) ++serial;
    /* Reserve FIFO space for every enabled producer. FIFO order is retained;
     * a high-rate source cannot consume all 32 shared descriptors. */
    if (count == APP_HMI_LINK_DIAG_FIFO_SIZE || queued_by_source[source]>=APP_HMI_LINK_DIAG_FIFO_SIZE/
       (!!APP_HMI_LINK_DIAG_ENABLED+!!APP_NRF_DIAGNOSTIC_ENABLED+!!APP_CAN_SERVO_DIAG_ENABLED+
        !!APP_CAN_ARM_DIAG_ENABLED+!!APP_SERVO_RS485_DIAG_ENABLED+!!APP_CAN_DEBUG_ENABLED+
        !!APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED)) {
        ++overflow; taskEXIT_CRITICAL(); return;
    }
    slot=&events[write_at];
    slot->at=HAL_GetTick(); slot->number=serial;
    slot->kind=(uint16_t)(((uint16_t)source<<12U)|(kind&0x0FFFU));
    slot->data[0]=a;slot->data[1]=b;slot->data[2]=c;
    slot->data[3]=d;slot->data[4]=e;slot->data[5]=f;
    write_at=(uint16_t)((write_at+1U)%APP_HMI_LINK_DIAG_FIFO_SIZE);++count;++queued_by_source[source];
    taskEXIT_CRITICAL();
}
#if !defined(__CC_ARM) && !defined(__arm__)
void app_hmi_link_diag_record(uint16_t kind, uint32_t a, uint32_t b,
    uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    /* Sampling and standalone common-stop routing live in one producer. */
    app_link_diag_record(APP_LINK_HMI,kind,a,b,c,d,e,f);
}
#endif

void app_link_diag_freeze(uint8_t source,uint16_t kind,const uint32_t *words,uint8_t length)
{
    route_first_t *p;
    if(source<APP_LINK_REMOTE || source>APP_LINK_RS485 || !enabled(source) || !words) return;
    if(length>16U) length=16U;
    taskENTER_CRITICAL();
    p=&route_first[source-1U];
    if(p->length) ++p->merged;
    else {
        if(++serial==0U)++serial;
        p->at=HAL_GetTick();p->number=serial;p->kind=kind;p->page=0U;
        memcpy(p->words,words,length*sizeof(uint32_t));p->length=length;
    }
    taskEXIT_CRITICAL();
}

void app_link_diag_freeze6(uint8_t source,uint16_t kind,uint32_t a,uint32_t b,
    uint32_t c,uint32_t d,uint32_t e,uint32_t f)
{
    route_first_t *p;
    if(source<APP_LINK_REMOTE || source>APP_LINK_RS485 || !enabled(source))return;
    taskENTER_CRITICAL();p=&route_first[source-1U];
    if(p->length)++p->merged;
    else {
        if(++serial==0U)++serial;
        p->at=HAL_GetTick();p->number=serial;p->kind=kind;p->page=0U;
        p->words[0]=a;p->words[1]=b;p->words[2]=c;
        p->words[3]=d;p->words[4]=e;p->words[5]=f;p->length=6U;
    }
    taskEXIT_CRITICAL();
}

void app_hmi_link_diag_stop(const app_control_stop_t *stop, uint16_t kind)
{
    if (!stop || !stop->event) return;
    taskENTER_CRITICAL();
    if (kind == APP_HMI_LINK_STOP_FIRST && first_stop.event != stop->event && first_revision == first_printed) {
        first_stop = *stop;
        if (++first_revision == 0U) ++first_revision;
    } else if(kind==APP_HMI_LINK_STOP_FIRST && first_stop.event!=stop->event) ++stop_merged;
    taskEXIT_CRITICAL();
    { uint8_t source=APP_LINK_HMI,i;
      if (!enabled(source)) {
          source=stop->owner==APP_CONTROL_OWNER_REMOTE ? APP_LINK_REMOTE :
                 stop->owner==APP_CONTROL_OWNER_CAN ? APP_LINK_CAN_SERVO : APP_LINK_RS485;
          if(!enabled(source)) for(i=0U;i<APP_LINK_CHANNELS;++i) if(enabled(i)){source=i;break;}
      }
    if(source!=APP_LINK_HMI)kind=(uint16_t)(0x3E0U+kind);
    app_link_diag_record(source, kind, stop->event, stop->reason,
        ((uint32_t)stop->owner << 16U) | (uint16_t)stop->requester,
        stop->request_sequence, ((uint32_t)stop->command << 16U) | stop->deceleration_mm_s2,
        ((uint32_t)stop->result << 16U) | stop->error);
    }
}

/* Four register arguments avoid growing the arbiter's frame for the rare
 * upgrade path. First event ownership/history remains untouched. */
void app_hmi_link_diag_upgrade(const app_control_stop_t *stop,uint16_t requester,
    uint32_t sequence,uint16_t command)
{
    if(!stop)return;
    app_link_diag_record(APP_LINK_HMI,APP_HMI_LINK_STOP_UPGRADE,stop->event,stop->reason,
        ((uint32_t)stop->owner<<16U)|requester,sequence,
        ((uint32_t)command<<16U)|stop->deceleration_mm_s2,
        ((uint32_t)stop->result<<16U)|stop->error);
}

uint8_t app_hmi_link_diag_screen(const uint16_t *words, uint16_t words_count)
{
    uint32_t id;
    uint8_t page, bit;
    if (!words || words_count != APP_HMI_LINK_DIAG_WORDS || words[0] != 0x4844U ||
        (words[1] != 0x0100U && words[1] != 0x0101U)) return 0U;
    id = ((uint32_t)words[2] << 16U) | words[3];
    if (!id) return 0U;
    page = (uint8_t)(words[1] & 1U); bit = (uint8_t)(1U << page);
    taskENTER_CRITICAL();
    if (id != screen_id) {
        /* Never acknowledge data that would overwrite a still-unprinted page. */
        if (screen_dirty) { ++screen_conflict; taskEXIT_CRITICAL(); return 0U; }
        if (screen_mask && screen_mask != 3U) ++screen_replaced;
        screen_id = id; screen_mask = 0U;
    }
    if (screen_mask & bit) {
        uint8_t same = memcmp(screen[page], words, sizeof(screen[page])) == 0;
        if (same) { taskEXIT_CRITICAL(); return 1U; }
        /* A screen-only reboot restarts its event counter. Page 0 may open a
           new generation only once the previous pair is fully printed. */
        if (page == 0U && screen_mask == 3U && !screen_dirty) {
            screen_mask = 0U; ++screen_reused;
        } else {
            ++screen_conflict;
            taskEXIT_CRITICAL();
            return 0U;
        }
    }
    memcpy(screen[page], words, sizeof(screen[page]));
    screen_mask |= bit; screen_dirty |= bit;
    taskEXIT_CRITICAL();
    return 1U;
}

static uint8_t print_boot(void)
{
    int n;
    if (boot_phase == 1U) {
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 BOOT build=%s_%s diag=1 lease=%u ttl=%u reset=%08lX impl=%s diag_rev=3 mask=%X/%X/%X/%X/%X units=hex\r\n",
            U(HAL_GetTick()), U(boot), __DATE__, __TIME__,
            APP_HMI_SESSION_LEASE_MS, APP_HMI_SPEED_HOLD_MAX_MS,
            U(bsp_system_get_reset_reason()), APP_LINK_DIAG_IMPLEMENTATION,
            APP_HMI_LINK_DIAG_ENABLED,APP_NRF_DIAGNOSTIC_ENABLED,
            APP_CAN_SERVO_DIAG_ENABLED,APP_CAN_ARM_DIAG_ENABLED,APP_SERVO_RS485_DIAG_ENABLED);
    } else if (boot_phase == 2U || boot_phase == 3U) {
        uint8_t valid = boot_phase == 2U ? bsp_system_get_previous_fault(&fault) :
                                         bsp_system_get_previous_exception(&fault);
        if (!valid) memset(&fault, 0, sizeof(fault));
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 PREV type=%u valid=%u boot=%lu at=%lu code=%08lX detail=%lu task=%lu pc=%08lX lr=%08lX cfsr=%08lX hfsr=%08lX\r\n",
            U(HAL_GetTick()), U(boot), (unsigned)(boot_phase - 2U), valid,
            U(fault.boot), U(fault.ms), U(fault.fault_code), U(fault.detail), U(fault.task_id),
            U(fault.pc), U(fault.lr), U(fault.cfsr), U(fault.hfsr));
    }
    else {
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 CONFIG memory=%s lcd=%u cli=%u tune=%u servo_cli=%u pulse=%u\r\n",
            U(HAL_GetTick()), U(boot), APP_MEMORY_PROFILE_ID, APP_CHASSIS_LCD_ENABLED,
            APP_DEBUG_CLI_ENABLED, APP_TUNE_UART_ENABLED, APP_SERVO_CLI_DEBUG_ENABLED,
            APP_SERVO_PULSE_ENABLED);
    }
    if (!emit(n)) return 0U;
    boot_phase = boot_phase == 4U ? 0U : (uint8_t)(boot_phase + 1U);
    return 1U;
}

static uint8_t print_first(void)
{
    int n;
    taskENTER_CRITICAL();
    first_copy = first_stop; first_copy_revision = first_revision;
    taskEXIT_CRITICAL();
    if (!first_copy_revision || first_copy_revision == first_printed) return 0U;
    n = snprintf(line, sizeof(line),
        "HLD %lX %lX %lX FIRST at=%lu owner=%u requester=%u reason=%u seq=%lu cmd=%u decel=%u gen=%lu merged=%lu\r\n",
        U(HAL_GetTick()), U(boot), U(first_copy.event), U(first_copy.started_ms),
        (unsigned)first_copy.owner, (unsigned)first_copy.requester, first_copy.reason,
        U(first_copy.request_sequence), first_copy.command, first_copy.deceleration_mm_s2,
        U(first_copy.generation),U(stop_merged));
    if (emit(n)) first_printed = first_copy_revision;
    return 1U;
}

static uint8_t print_screen(void)
{
    uint8_t page, mask;
    uint32_t id;
    unsigned i;
    int n;
    taskENTER_CRITICAL();
    if (!screen_dirty) { taskEXIT_CRITICAL(); return 0U; }
    page = (screen_dirty & 1U) ? 0U : 1U;
    memcpy(screen_copy, screen[page], sizeof(screen_copy));
    id = screen_id; mask = screen_mask;
    taskEXIT_CRITICAL();
    n = snprintf(line, sizeof(line), "HLD %lX %lX %lX SCREEN page=%u mask=%u incomplete=%u words=",
        U(HAL_GetTick()), U(boot), U(id), page, mask, mask != 3U);
    for (i = 0U; i < APP_HMI_LINK_DIAG_WORDS && n > 0 && (unsigned)n < sizeof(line); ++i)
        n += snprintf(line + n, sizeof(line) - (unsigned)n, "%s%04X", i ? "," : "", screen_copy[i]);
    if (n > 0 && (unsigned)n < sizeof(line)) n += snprintf(line + n, sizeof(line) - (unsigned)n, "\r\n");
    if (emit(n)) {
        taskENTER_CRITICAL();
        if (screen_id == id) screen_dirty &= (uint8_t)~(1U << page);
        taskEXIT_CRITICAL();
    }
    return 1U;
}

static uint8_t print_snapshot_reject(void)
{
    const app_hmi_snapshot_reject_t *r;
    int n;
    app_hmi_snapshot_diag_read(&snapshot_diag);
    if (!snapshot_diag.reject_pending) return 0U;
    r = &snapshot_diag.reject;
    /* All diagnostic times/ages/cycles on this compact line are hexadecimal. */
    n = snprintf(line, sizeof(line),
        "HLD %lX %lX %lX SNAP_REJECT at=%lX req=%X/%X why=%u ex=%u cache=%lX source=%lX seq=%lX/%lX full=%lX critical=%lX age=%lX/%lX cycles=%lX/%lX\r\n",
        U(HAL_GetTick()), U(boot), U(r->id), U(r->at_ms), r->start, r->quantity,
        r->reason, r->exception, U(r->cache_age_ms), U(r->source_age_ms),
        U(r->cache_sequence), U(r->source_sequence), U(r->full_sequence), U(r->critical_sequence), U(r->age_before_ms),
        U(r->age_after_ms), U(r->copy_cycles), U(r->encode_cycles));
    if (emit(n)) app_hmi_snapshot_diag_ack_reject(r->id);
    return 1U;
}

static void print_status(uint32_t now)
{
    int n;
    app_hmi_control_get_status(&control);
    n = snprintf(line, sizeof(line),
        "HLD %lX %lX %lX S %X,%lX,%X,%X,%lX,%X,%lX,%X,%X,%X,%X,%X,%X,%X,%X,%X\r\n",
        U(now), U(boot), U(control.stop.event), (unsigned)app_control_arbiter_owner(),
        U(control.session), control.state, control.flags, U(control.target_age_ms),
        app_hmi_control_remaining_ms(), U(control.last_sequence), (uint16_t)control.target,
        (uint16_t)control.output[0], (uint16_t)control.output[1], control.stop.reason, control.phase,
        control.stop.result, control.stop.error, control.stop.zero_mask, control.stop.enabled_mask);
    (void)emit(n);
}

static void print_summary(void)
{
    int n = 0;
    unsigned i;
    uint32_t now = HAL_GetTick();
    if (summary_phase == 1U) {
        (void)app_system_snapshot_read_diagnostic(&system_diag);
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 MEM free=%lu min=%lu reset=%08lX diag=%08lX wdg=%lu failed=%lu fresh=%u\r\n",
            U(now), U(boot), U(xPortGetFreeHeapSize()), U(xPortGetMinimumEverFreeHeapSize()),
            U(system_diag.reset_flags), U(system_diag.diagnostic), U(system_diag.watchdog_decision),
            U(system_diag.failed_task), system_diag.snapshot_fresh);
    } else if (summary_phase == 2U) {
        (void)app_system_snapshot_read_diagnostic(&system_diag);
        n = snprintf(line, sizeof(line), "HLD %lX %lX 0 STACK bytes=", U(now), U(boot));
        for (i = 0U; i < 12U && n > 0 && (unsigned)n < sizeof(line); ++i) {
            uint32_t v = system_diag.stack_words[i];
            if (v == UINT32_MAX) n += snprintf(line + n, sizeof(line) - (unsigned)n, "%sNA", i ? "," : "");
            else n += snprintf(line + n, sizeof(line) - (unsigned)n, "%s%lu", i ? "," : "", U(v * 4U));
        }
        if (n > 0 && (unsigned)n < sizeof(line)) n += snprintf(line + n, sizeof(line) - (unsigned)n, "\r\n");
    } else if (summary_phase == 3U) {
        bsp_hmi_uart_get_stats(&uart_stats);
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 UART %lX,%lX,%lX,%lX,%lX,%lX,%lX,%lX,%lX\r\n",
            U(now), U(boot), U(uart_stats.requests), U(uart_stats.responses), U(uart_stats.crc_errors),
            U(uart_stats.frame_errors), U(uart_stats.partial_timeouts), U(uart_stats.rx_overflow),
            U(uart_stats.tx_busy), U(uart_stats.tx_errors), U(uart_stats.rx_errors));
    } else if (summary_phase == 4U) {
        bsp_hmi_uart_get_diag(&uart_diag);
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 RXERR %lX,%lX,%lX,%lX,%lX,%lX,%X,%X,%lX\r\n",
            U(now), U(boot), U(uart_diag.ore), U(uart_diag.fe), U(uart_diag.ne), U(uart_diag.pe),
            U(uart_diag.last_error), U(uart_diag.dma_rx_error), uart_diag.rx_armed,
            uart_diag.rx_pending, U(uart_diag.rx_reason));
    } else if (summary_phase == 5U) {
        bsp_hmi_uart_get_diag(&uart_diag);
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 DMA %lX,%lX,%lX,%lX,%lX,%lX,%lX,%lX\r\n",
            U(now), U(boot), U(uart_diag.rx_epoch), U(uart_diag.tx_epoch), U(uart_diag.rx_attempts),
            U(uart_diag.rx_success), U(uart_diag.rx_failures), U(uart_diag.tx_attempts), U(uart_diag.tx_success),
            U(uart_diag.tx_failures));
    } else if (summary_phase == 6U) {
        bsp_hmi_uart_get_diag(&uart_diag);
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 QUEUE %lX,%X,%lX,%lX,%lX,%lX,%lX,%lX\r\n",
            U(now), U(boot), U(uart_diag.queue_high_water), uart_diag.event_high_water,
            U(uart_diag.event_overflow), U(uart_diag.dropped_frames), U(uart_diag.tx_dropped),
            U(uart_diag.boundaries), U(uart_diag.rx_last_ms), U(uart_diag.tx_last_ms));
    } else if (summary_phase == 7U) {
        bsp_hmi_uart_get_diag(&uart_diag);
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 TIMING %lX,%lX,%lX,%lX,%lX tc=%lX/%lX slow=%lX/%lX/%lX\r\n",
            U(now), U(boot), U(uart_diag.service_max_ms), U(uart_diag.response_delay_max_cycles),
            U(uart_diag.parse_max_cycles), U(uart_diag.capture_max_cycles), U(bsp_hmi_uart_clock_hz()),
            U(uart_diag.response_complete_last_cycles), U(uart_diag.response_complete_max_cycles),
            U(slow_warnings), U(slow_deadlines), U(slow_merged));
    } else if (summary_phase == 8U) {
        app_runtime_read(&runtime);
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 GAPS us=%lu,%lu,%lu,%lu,%lu,%lu\r\n",
            U(now), U(boot), U(runtime.gap_max_us[0]), U(runtime.gap_max_us[1]),
            U(runtime.gap_max_us[2]), U(runtime.gap_max_us[3]), U(runtime.gap_max_us[4]),
            U(runtime.gap_max_us[5]));
    } else if (summary_phase == 9U) {
        bsp_hmi_uart_get_diag(&uart_diag);
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 QUIET %lX,%lX,%lX,%lX\r\n",
            U(now), U(boot), U(uart_diag.quiet_early_count), U(uart_diag.quiet_elapsed_min_cycles),
            U(uart_diag.raw_last_cycles), U(uart_diag.tx_complete_cycles));
    } else if (summary_phase == 10U) {
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 LOGGER fifo_lost=%lu uart_drop=%lu screen_conflict=%lu screen_replaced=%lu screen_reused=%lu sampled=%lu\r\n",
            U(now), U(boot), U(overflow), U(output_dropped), U(screen_conflict), U(screen_replaced), U(screen_reused), U(sampled));
    } else if (summary_phase == 11U) {
        app_hmi_snapshot_diag_read(&snapshot_diag);
        n = snprintf(line, sizeof(line),
            "HLD %lX %lX 0 SNAP seq=%lX/%lX src=%lX/%lX copy=%lX/%lX critical=%lX/%lX full=%lX/%lX age=%lX/%lX rejected=%lX merged=%lX ordinary=%lX/%lX full_off=1\r\n",
            U(now), U(boot), U(snapshot_diag.full_sequence), U(snapshot_diag.critical_sequence),
            U(snapshot_diag.full_source_ms), U(snapshot_diag.critical_source_ms),
            U(snapshot_diag.last_copy_cycles), U(snapshot_diag.max_copy_cycles),
            U(snapshot_diag.last_critical_cycles), U(snapshot_diag.max_critical_cycles),
            U(snapshot_diag.last_full_cycles), U(snapshot_diag.max_full_cycles),
            U(snapshot_diag.last_age_before_ms), U(snapshot_diag.last_age_after_ms),
            U(snapshot_diag.rejections), U(snapshot_diag.merged),
            U(snapshot_diag.last_ordinary_cycles), U(snapshot_diag.max_ordinary_cycles));
    }
    /* A congested UART must retry this page.  Advancing on a failed write hid
     * MEM/STACK/TIMING precisely when the startup event stream was busiest. */
    if(!emit(n))return;
    if (summary_phase == 10U) {
        if ((uint32_t)(now - snapshot_summary_at) >= 5000U) {
            snapshot_summary_at = now; summary_phase = 11U;
        } else summary_phase = 0U;
    } else summary_phase = summary_phase == 11U ? 0U : (uint8_t)(summary_phase + 1U);
}

static uint8_t print_startup_first(void)
{
    unsigned i,begin,end,parts;
    int n;
    if(!startup_first.length || startup_first.delivered)return 0U;
    begin=(unsigned)startup_first.page*8U;end=begin+8U;
    if(end>startup_first.length)end=startup_first.length;
    parts=((unsigned)startup_first.length+7U)/8U;
    n=snprintf(line,sizeof(line),
        "LD %lX %lX %lX STARTUP part=%X parts=%X count=%X merged=%lX w=",
        U(startup_first.at),U(boot),U(startup_first.number),startup_first.page,
        parts,startup_first.length,U(startup_first.merged));
    for(i=begin;i<end && n>0 && (unsigned)n<sizeof(line);++i)
        n+=snprintf(line+n,sizeof(line)-(unsigned)n,"%s%lX",i==begin?"":",",U(startup_first.words[i]));
    if(n>0 && (unsigned)n<sizeof(line))n+=snprintf(line+n,sizeof(line)-(unsigned)n,"\r\n");
    if(emit(n)) {
        taskENTER_CRITICAL();
        if(end==startup_first.length)startup_first.delivered=1U;
        else ++startup_first.page;
        taskEXIT_CRITICAL();
    }
    return 1U;
}


static uint8_t print_route_first(void)
{
    unsigned step,i,begin,end;
    uint8_t source;
    route_first_t *p;
    int n;
    for(step=0U;step<4U;++step) {
        source=(uint8_t)((first_cursor+step)%4U);
        p=&route_first[source];
        taskENTER_CRITICAL();
        if(!p->length) {taskEXIT_CRITICAL();continue;}
        /* Payload stays immutable until the final successful emit. */
        begin=(unsigned)p->page*8U;end=begin+8U;
        if(end>p->length)end=p->length;
        taskEXIT_CRITICAL();
        n=snprintf(line,sizeof(line),"LD %lX %lX %lX FIRST src=%s kind=%X p=%u merged=%lX w=",
            U(p->at),U(boot),U(p->number),source_name((uint8_t)(source+1U)),p->kind,p->page,U(p->merged));
        for(i=begin;i<end && n>0 && (unsigned)n<sizeof(line);++i)
            n+=snprintf(line+n,sizeof(line)-(unsigned)n,"%s%lX",i==begin?"":",",U(p->words[i]));
        if(n>0 && (unsigned)n<sizeof(line))n+=snprintf(line+n,sizeof(line)-(unsigned)n,"\r\n");
        if(emit(n)) {
            taskENTER_CRITICAL();
            if(end==p->length)p->length=p->page=0U;else ++p->page;
            taskEXIT_CRITICAL();
            first_cursor=(uint8_t)((source+1U)%4U);
        }
        return 1U;
    }
    return 0U;
}
static uint8_t project(uint8_t source,uint8_t phase)
{
    (void)phase;
    memset(&scratch.compact,0,sizeof(scratch.compact));
    switch(source) {
#if APP_NRF_DIAGNOSTIC_ENABLED || APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED
    case APP_LINK_REMOTE: app_remote_diag_snapshot(&scratch.compact,phase);break;
#if APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED
    case APP_LINK_SYNC:
        if(phase==0U)app_remote_diag_snapshot(&scratch.compact,0x80U);
        else app_position_diag_snapshot(&scratch.compact);
        break;
#endif
#endif
#if APP_CAN_SERVO_DIAG_ENABLED || APP_CAN_ARM_DIAG_ENABLED || APP_CAN_DEBUG_ENABLED
    case APP_LINK_CAN_SERVO:case APP_LINK_CAN_ARM:case APP_LINK_CAN_RAW:
        app_can_diag_snapshot(&scratch.compact,source,phase);break;
#endif
#if APP_SERVO_RS485_DIAG_ENABLED
    case APP_LINK_RS485:app_rs485_diag_snapshot(&scratch.compact,phase);break;
#endif
    default:break;
    }
    return scratch.compact.count!=0U;
}
static uint8_t print_projection(uint8_t source,uint8_t phase,uint8_t lane)
{
    int n;
    unsigned i,begin,end;
    projection_cursor_t *cursor=&projection_cursor[lane];
    if(cursor->source!=source || cursor->phase!=phase) {
        cursor->part=0U;cursor->source=source;cursor->phase=phase;
    }
    if(!project(source,phase)){cursor->part=0U;return 1U;}
    if(scratch.compact.count>24U)scratch.compact.count=24U;
    n=snprintf(line,sizeof(line),"LD %lX %lX 0 %s src=%s p=%X part=%u id=%lX sample=%lX w=",
        U(HAL_GetTick()),U(boot),lane?"HEALTH":"STATE",source_name(source),phase,cursor->part,U(cursor->id),U(scratch.compact.sampled_ms));
    begin=(unsigned)cursor->part*12U;end=begin+12U;
    if(end>scratch.compact.count)end=scratch.compact.count;
    for(i=begin;i<end && n>0 && (unsigned)n<sizeof(line);++i)
        n+=snprintf(line+n,sizeof(line)-(unsigned)n,"%s%lX",i==begin?"":",",U(scratch.compact.words[i]));
    if(n>0 && (unsigned)n<sizeof(line))n+=snprintf(line+n,sizeof(line)-(unsigned)n,"\r\n");
    if(!emit(n))return 0U;
    if(end<scratch.compact.count){cursor->part=1U;return 0U;}
    cursor->part=0U;
    if(source!=APP_LINK_SYNC || phase!=0U)++cursor->id;
    return 1U;
}
static uint8_t print_periodic(uint32_t now)
{
    uint8_t i,source;
    unsigned n=source_count();
    if(!n)n=1U;
    /* Stagger sources and retry only the current periodic projection on UART
     * backpressure. First/fault records always run before this path. */
    if((uint32_t)(now-compact_at)>=state_period()/n) {
        for(i=0U;i<APP_LINK_CHANNELS;++i) {
            source=(uint8_t)((source_cursor+i)%APP_LINK_CHANNELS);
            if(!enabled(source) || source==APP_LINK_HMI || source==APP_LINK_CAN_RAW)continue;
            if(print_projection(source,state_page,0U)) {
                if(source==APP_LINK_SYNC && state_page==0U)state_page=1U;
                else {state_page=0U;compact_at=now;source_cursor=(uint8_t)((source+1U)%APP_LINK_CHANNELS);}
            }
            return 1U;
        }
        compact_at=now;
    }
    if(!compact_phase && (uint32_t)(now-health_at)>=health_period()) {
        health_at=now;compact_phase=1U;health_source=1U;health_page=1U;
    }
    if(compact_phase) {
        while(health_source<APP_LINK_CHANNELS) {
            source=health_source;
            if(!enabled(source) || source==APP_LINK_SYNC) {++health_source;health_page=1U;continue;}
            /* CAN bus/timing are shared; emit them once with both routes. */
            if(source==APP_LINK_CAN_ARM && health_page>=2U && enabled(APP_LINK_CAN_SERVO)) {
                ++health_source;health_page=1U;continue;
            }
            if(source==APP_LINK_CAN_RAW &&
               (enabled(APP_LINK_CAN_SERVO) || enabled(APP_LINK_CAN_ARM))) {
                ++health_source;health_page=1U;continue;
            }
            if(source==APP_LINK_CAN_RAW && health_page==1U)health_page=2U;
            if(!print_projection(source,health_page,1U))return 1U;
            if(++health_page > (source==APP_LINK_REMOTE?5U:
                (source==APP_LINK_CAN_SERVO || source==APP_LINK_CAN_ARM || source==APP_LINK_CAN_RAW)?3U:2U)) {
                ++health_source;health_page=1U;
            }
            return 1U;
        }
        compact_phase=0U;
    }
    return 0U;
}
void app_hmi_link_diag_service(void)
{
    uint32_t now=HAL_GetTick();
    int n;
    if(APP_HMI_LINK_DIAG_ENABLED)capture_lcd();
    priority_output=1U;
    if(print_first())return;
    if(boot_phase){(void)print_boot();return;}
    if(print_startup_first())return;
    if(APP_HMI_LINK_DIAG_ENABLED && print_screen())return;
    if(print_route_first())return;
    if(APP_HMI_LINK_DIAG_ENABLED && print_slow())return;
    if(APP_HMI_LINK_DIAG_ENABLED && print_snapshot_reject())return;
    priority_output=0U;
    if(APP_HMI_LINK_DIAG_ENABLED && (uint32_t)(now-status_at)>=state_period()) {
        status_at=now;print_status(now);return;
    }
    if(!summary_phase && (uint32_t)(now-summary_at)>=health_period()) {
        summary_at=now;summary_phase=1U;
    }
    if(summary_phase) {
        if(!APP_HMI_LINK_DIAG_ENABLED && summary_phase>=3U && summary_phase<=7U)summary_phase=8U;
        if(!APP_HMI_LINK_DIAG_ENABLED && summary_phase==9U)summary_phase=10U;
        if(!APP_HMI_LINK_DIAG_ENABLED && summary_phase==11U)summary_phase=0U;
        if(summary_phase){print_summary();return;}
    }
    if(print_periodic(now))return;
    taskENTER_CRITICAL();
    if(!count){taskEXIT_CRITICAL();if(APP_HMI_LINK_DIAG_ENABLED)print_lcd(now);return;}
    work=events[read_at];
    taskEXIT_CRITICAL();
    if((work.kind>>12U)==APP_LINK_HMI)
        n=snprintf(line,sizeof(line),"HLD %lX %lX %lX E%X %lX,%lX,%lX,%lX,%lX,%lX\r\n",
            U(work.at),U(boot),U(work.number),work.kind&0x0FFFU,U(work.data[0]),U(work.data[1]),
            U(work.data[2]),U(work.data[3]),U(work.data[4]),U(work.data[5]));
    else
        n=snprintf(line,sizeof(line),"LD %lX %lX %lX EVENT src=%s kind=%X w=%lX,%lX,%lX,%lX,%lX,%lX\r\n",
            U(work.at),U(boot),U(work.number),source_name((uint8_t)(work.kind>>12U)),work.kind&0x0FFFU,
            U(work.data[0]),U(work.data[1]),U(work.data[2]),U(work.data[3]),U(work.data[4]),U(work.data[5]));
    if(emit(n)) {
        taskENTER_CRITICAL();--queued_by_source[(uint8_t)(work.kind>>12U)];
        read_at=(uint16_t)((read_at+1U)%APP_HMI_LINK_DIAG_FIFO_SIZE);--count;
        taskEXIT_CRITICAL();
    }
}
#endif
