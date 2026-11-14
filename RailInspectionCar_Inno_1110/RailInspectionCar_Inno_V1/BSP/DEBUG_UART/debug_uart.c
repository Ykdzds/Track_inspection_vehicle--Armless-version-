#include "debug_uart.h"
#include "app_config.h"
#include "usart.h"
#include <string.h>

#define TX_SEGMENT_MAX (2047U)
#define UART_BASEPRI (6U << (8U - __NVIC_PRIO_BITS))
#define RX_RETRY_MS (10U)
#define RX_ABORT_MS (100U)
#define TX_ABORT_MS (100U)
#if APP_UART_RX_ACTIVE
static uint8_t s_rx_buffer[DEBUG_UART_RX_BUFFER_SIZE];
#endif
static uint8_t s_tx_buffer[DEBUG_UART_TX_BUFFER_SIZE];
static volatile uint16_t s_tx_head, s_tx_tail, s_tx_length;
static volatile uint8_t s_tx_lock, s_tx_busy, s_tx_done, s_tx_recovery, s_tx_aborting, s_tx_aborted;
static volatile uint32_t s_tx_started, s_tx_abort_at;
#if APP_UART_RX_ACTIVE
static volatile uint32_t s_rx_turns, s_rx_epoch;
static uint32_t s_rx_consumed, s_rx_last_poll, s_rx_last_wraps;
static volatile uint8_t s_rx_running, s_rx_recovery, s_rx_aborting, s_rx_aborted;
static volatile uint32_t s_rx_abort_at, s_rx_next_retry;
static volatile uint32_t s_rx_seen_sr, s_rx_seen_hal;
static uint32_t s_rx_total_base;
#endif
static volatile debug_uart_stats_t s_stats;
static uint32_t s_guard_cycles;

uint32_t debug_uart_cycles(void) { return DWT->CYCCNT; }
uint32_t debug_uart_parse_limit_cycles(void) { return (SystemCoreClock / 1000000U) * DEBUG_UART_PARSE_US; }
/* BASEPRI_MAX preserves a caller's stronger mask. USART2 priority 5 remains live. */
static uint32_t guard(void) {
    uint32_t old = __get_BASEPRI();
    __set_BASEPRI_MAX(UART_BASEPRI); __DSB(); __ISB();
    s_guard_cycles = debug_uart_cycles(); return old;
}
static void unguard(uint32_t old) {
    uint32_t cycles = debug_uart_cycles() - s_guard_cycles;
    if (cycles > s_stats.tx_critical_max_cycles) s_stats.tx_critical_max_cycles = cycles;
    __set_BASEPRI(old);
}
static uint16_t used(void) {
    uint16_t h = s_tx_head, t = s_tx_tail;
    return h >= t ? (uint16_t)(h-t) : (uint16_t)(DEBUG_UART_TX_BUFFER_SIZE-t+h);
}
static void tx_depth(void) {
    uint16_t depth = used();
    s_stats.buffer_used = depth;
    s_stats.buffer_free = (uint16_t)(DEBUG_UART_TX_BUFFER_SIZE-1U-depth);
    if (depth > s_stats.buffer_high_water) s_stats.buffer_high_water = depth;
    s_stats.tx_busy = s_tx_busy;
}
static uint8_t enqueue(const uint8_t *data, uint16_t length, uint16_t reserve) {
    uint32_t mask;
    uint16_t head, free_bytes, i;
    if (__get_IPSR() != 0U) { s_stats.tx_isr_drops++; return 0U; }
    mask = guard();
    if (s_tx_lock) { s_stats.tx_lock_drops++; unguard(mask); return 0U; }
    s_tx_lock = 1U; head = s_tx_head;
    free_bytes = (uint16_t)(DEBUG_UART_TX_BUFFER_SIZE-1U-used());
    unguard(mask);
    if ((uint32_t)length + reserve > free_bytes) {
        mask = guard(); s_tx_lock = 0U; unguard(mask); return 0U;
    }
    /* Only the producer lock is held here: DMA and RS485 can preempt the copy. */
    for (i=0U; i<length; ++i) {
        s_tx_buffer[head++] = data[i];
        if (head == DEBUG_UART_TX_BUFFER_SIZE) head=0U;
    }
    __DMB(); mask = guard();
    s_tx_head = head; s_stats.queued_bytes += length; s_tx_lock=0U;
    tx_depth(); unguard(mask); return 1U;
}
void debug_uart_write_char(uint8_t data) {
    if (!enqueue(&data,1U,0U)) { uint32_t mask=guard(); s_stats.dropped_bytes++; unguard(mask); }
}
uint8_t debug_uart_try_write_frame(const uint8_t *data, uint16_t length, uint16_t reserve) {
    if (!data || !length || length>256U) return 0U;
    return enqueue(data,length,reserve);
}
uint8_t debug_uart_try_write(const uint8_t *data, uint16_t length, uint16_t reserve) {
    return length<=128U ? debug_uart_try_write_frame(data,length,reserve) : 0U;
}
#if APP_UART_RX_ACTIVE
static uint8_t rx_start(void) {
    HAL_StatusTypeDef result;
    uint32_t mask = guard();
    s_rx_running=0U; s_rx_turns=0U; s_rx_consumed=0U; s_rx_last_wraps=0U;
    __HAL_DMA_CLEAR_FLAG(huart1.hdmarx, __HAL_DMA_GET_TC_FLAG_INDEX(huart1.hdmarx));
    __HAL_DMA_CLEAR_FLAG(huart1.hdmarx, __HAL_DMA_GET_HT_FLAG_INDEX(huart1.hdmarx));
    unguard(mask);
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    /* HAL entry is never called with PRIMASK/BASEPRI raised by this module. */
    s_rx_total_base=s_stats.rx_bytes;
    s_rx_seen_sr=s_rx_seen_hal=0U;
    s_rx_recovery=0U; s_rx_running=1U;
    result=HAL_UARTEx_ReceiveToIdle_DMA(&huart1,s_rx_buffer,DEBUG_UART_RX_BUFFER_SIZE);
    if (result!=HAL_OK || s_rx_recovery) {
        s_rx_running=0U; s_rx_recovery=1U; s_stats.rx_start_failures++;
        s_rx_next_retry=HAL_GetTick()+RX_RETRY_MS; return 0U;
    }
    s_rx_last_poll=HAL_GetTick(); return 1U;
}
#endif
void debug_uart_init(void) {
    memset((void *)&s_stats,0,sizeof(s_stats));
    s_tx_head=s_tx_tail=s_tx_length=0U;
    s_tx_lock=s_tx_busy=s_tx_done=s_tx_recovery=s_tx_aborting=s_tx_aborted=0U;
#if APP_UART_RX_ACTIVE
    s_rx_turns=s_rx_consumed=s_rx_epoch=0U;
    s_rx_running=s_rx_recovery=s_rx_aborting=s_rx_aborted=0U;
    s_rx_abort_at=s_rx_next_retry=0U;
#endif
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    tx_depth();
#if APP_UART_RX_ACTIVE
    if (!huart1.hdmarx) { s_stats.rx_start_failures++; s_rx_recovery=1U; return; }
    (void)rx_start();
#endif
}
void debug_uart_tx_complete_isr(void) {
    if (s_tx_busy && !s_tx_recovery && !s_tx_aborting) s_tx_done=1U;
}
void debug_uart_tx_error_isr(void) {
    /* HAL UART ErrorCode is shared: only the TX DMA handle can identify a TX DMA error. */
    if (s_tx_busy && huart1.hdmatx && huart1.hdmatx->ErrorCode!=HAL_DMA_ERROR_NONE && !s_tx_recovery) {
        s_tx_recovery=1U; s_stats.dma_error_count++;
    }
}
void debug_uart_tx_abort_isr(void) { if (s_tx_aborting) s_tx_aborted=1U; }
void debug_uart_service(void) {
    HAL_StatusTypeDef result;
    uint16_t length, tail;
    uint32_t now=HAL_GetTick(), mask;
    mask=guard();
    if (s_tx_done && !s_tx_recovery) {
        s_tx_tail=(uint16_t)((s_tx_tail+s_tx_length)%DEBUG_UART_TX_BUFFER_SIZE);
        s_stats.sent_bytes+=s_tx_length; s_stats.dma_complete_count++;
        s_tx_length=0U; s_tx_busy=s_tx_done=0U;
    }
    if (s_tx_busy && !s_tx_recovery && (uint32_t)(now-s_tx_started)>500U) {
        s_tx_recovery=1U; s_stats.dma_error_count++;
    }
    tx_depth(); unguard(mask);
    if (s_tx_recovery) {
        if (!s_tx_aborting) {
            s_tx_aborting=1U; s_tx_aborted=0U; s_tx_abort_at=now;
            result=HAL_UART_AbortTransmit_IT(&huart1);
            if (result!=HAL_OK) { s_tx_aborting=0U; s_stats.dma_start_failures++; return; }
        }
        if (!s_tx_aborted) {
            if ((uint32_t)(now-s_tx_abort_at)>TX_ABORT_MS) {
                /* An unconfirmed abort never frees/reuses DMA-owned bytes. */
                s_stats.dma_start_failures++; s_tx_abort_at=now;
            }
            return;
        }
        mask=guard(); s_tx_recovery=s_tx_aborting=s_tx_aborted=s_tx_busy=s_tx_done=0U;
        s_tx_length=0U; tx_depth(); unguard(mask);
    }
    if (s_tx_busy) return;
    mask=guard(); tail=s_tx_tail;
    length=s_tx_head>=tail ? (uint16_t)(s_tx_head-tail) : (uint16_t)(DEBUG_UART_TX_BUFFER_SIZE-tail);
    if (length>TX_SEGMENT_MAX) length=TX_SEGMENT_MAX;
    if (!length) { unguard(mask); return; }
    s_tx_length=length; s_tx_busy=1U; s_tx_done=0U; s_tx_started=now; tx_depth(); unguard(mask);
    result=HAL_UART_Transmit_DMA(&huart1,&s_tx_buffer[tail],length);
    if (result!=HAL_OK) { mask=guard(); s_tx_busy=0U; s_tx_length=0U; s_stats.dma_start_failures++; tx_depth(); unguard(mask); }
    else s_stats.dma_start_count++;
}
#if APP_UART_RX_ACTIVE
void debug_uart_rx_event_isr(uint16_t position, uint32_t kind) {
    if (!s_rx_running || s_rx_recovery) return;
    if (position>DEBUG_UART_RX_BUFFER_SIZE) { debug_uart_rx_error_isr(0U,HAL_UART_ERROR_DMA); return; }
    if (kind==HAL_UART_RXEVENT_TC) { s_rx_turns++; s_stats.rx_full_events++; }
    else if (kind==HAL_UART_RXEVENT_HT) s_stats.rx_half_events++;
    else s_stats.rx_idle_events++;
}
void debug_uart_rx_error_isr(uint32_t sr, uint32_t hal_error) {
    uint32_t fresh, hal_fresh;
    /* ErrorCode is UART-wide. A pure TX DMA error must not restart healthy RX. */
    if ((hal_error&HAL_UART_ERROR_DMA) && (!huart1.hdmarx || huart1.hdmarx->ErrorCode==HAL_DMA_ERROR_NONE)) hal_error &= ~HAL_UART_ERROR_DMA;
    fresh=sr & (UART_FLAG_ORE|UART_FLAG_FE|UART_FLAG_NE|UART_FLAG_PE) & ~s_rx_seen_sr;
    hal_fresh=hal_error & ~s_rx_seen_hal;
    if (!fresh && !hal_fresh && s_rx_recovery) return;
    if (!(sr & 15U) && !hal_error) return;
    s_rx_seen_sr |= sr & 15U; s_rx_seen_hal |= hal_error;
    if (fresh&UART_FLAG_ORE) s_stats.rx_ore++;
    if (fresh&UART_FLAG_FE) s_stats.rx_fe++;
    if (fresh&UART_FLAG_NE) s_stats.rx_ne++;
    if (fresh&UART_FLAG_PE) s_stats.rx_pe++;
    if ((hal_fresh&HAL_UART_ERROR_ORE) && !(s_rx_seen_sr&UART_FLAG_ORE)) {s_stats.rx_ore++;s_rx_seen_sr|=UART_FLAG_ORE;}
    if ((hal_fresh&HAL_UART_ERROR_FE) && !(s_rx_seen_sr&UART_FLAG_FE)) {s_stats.rx_fe++;s_rx_seen_sr|=UART_FLAG_FE;}
    if ((hal_fresh&HAL_UART_ERROR_NE) && !(s_rx_seen_sr&UART_FLAG_NE)) {s_stats.rx_ne++;s_rx_seen_sr|=UART_FLAG_NE;}
    if ((hal_fresh&HAL_UART_ERROR_PE) && !(s_rx_seen_sr&UART_FLAG_PE)) {s_stats.rx_pe++;s_rx_seen_sr|=UART_FLAG_PE;}
    if ((hal_fresh&HAL_UART_ERROR_DMA) && huart1.hdmarx && huart1.hdmarx->ErrorCode!=HAL_DMA_ERROR_NONE) s_stats.rx_dma_errors++;
    if (!s_rx_recovery) { s_stats.rx_uart_errors++; s_rx_epoch++; }
    s_rx_recovery=1U;
}
void debug_uart_rx_abort_isr(void) { if (s_rx_aborting) s_rx_aborted=1U; }
void debug_uart_rx_service(void) {
    uint32_t now=HAL_GetTick(); HAL_StatusTypeDef result;
    if (!s_rx_recovery) return;
    if (!huart1.hdmarx || (int32_t)(now-s_rx_next_retry)<0) return;
    if (!s_rx_aborting) {
        s_rx_aborting=1U; s_rx_aborted=0U; s_rx_abort_at=now;
        result=HAL_UART_AbortReceive_IT(&huart1);
        if (result!=HAL_OK) {s_rx_aborting=0U;s_stats.rx_abort_failures++;s_rx_next_retry=now+RX_RETRY_MS;return;}
    }
    if (!s_rx_aborted) {
        if ((uint32_t)(now-s_rx_abort_at)>RX_ABORT_MS) {s_stats.rx_abort_failures++;s_rx_abort_at=now;}
        return;
    }
    s_rx_aborting=s_rx_aborted=0U; s_stats.rx_restarts++;
    (void)rx_start();
}
/* Snapshot hardware position without stopping DMA. A pending TC counts as a wrap
 * even before HAL has dispatched its callback. The double flag read closes the
 * NDTR-reload race; normal snapshots hold BASEPRI for only a few register reads. */
static uint32_t rx_produced(uint32_t *wraps, uint32_t *pending) {
    uint32_t turns, before, after, remaining, mask=guard();
    turns=s_rx_turns;
    before=__HAL_DMA_GET_FLAG(huart1.hdmarx,__HAL_DMA_GET_TC_FLAG_INDEX(huart1.hdmarx))?1U:0U;
    remaining=__HAL_DMA_GET_COUNTER(huart1.hdmarx);
    after=__HAL_DMA_GET_FLAG(huart1.hdmarx,__HAL_DMA_GET_TC_FLAG_INDEX(huart1.hdmarx))?1U:0U;
    /* UART1 DMA IRQ is masked, so TC can only become set, never clear.
     * A single extra NDTR read resolves reload without an unbounded loop. */
    if (before!=after) remaining=__HAL_DMA_GET_COUNTER(huart1.hdmarx);
    if (remaining>DEBUG_UART_RX_BUFFER_SIZE) remaining=DEBUG_UART_RX_BUFFER_SIZE;
    /* NDTR can transiently reach zero before TC is visible/reload occurs. */
    if (remaining==0U) { remaining=DEBUG_UART_RX_BUFFER_SIZE; after=1U; }
    *wraps=turns+after; *pending=after;
    unguard(mask);
    return (turns+after)*DEBUG_UART_RX_BUFFER_SIZE + DEBUG_UART_RX_BUFFER_SIZE-remaining;
}
uint16_t debug_uart_rx_read(uint8_t *data, uint16_t length) {
    uint32_t produced, depth, epoch, now, i, wraps, pending, copy_started;
    if (!data || !length || !s_rx_running || s_rx_recovery || !huart1.hdmarx) return 0U;
    epoch=s_rx_epoch; produced=rx_produced(&wraps,&pending); depth=produced-s_rx_consumed; now=HAL_GetTick();
    s_stats.rx_bytes=s_rx_total_base+produced;
    /* A task stalled for a complete DMA revolution cannot disambiguate coalesced
     * hardware TC flags. Conservatively discard any pending region. */
    if (depth>=DEBUG_UART_RX_BUFFER_SIZE ||
        ((uint32_t)(now-s_rx_last_poll)>=40U && (pending || wraps!=s_rx_last_wraps))) {
        s_stats.rx_overflows++; s_stats.rx_dropped_bytes+=depth;
        s_rx_consumed=produced; s_rx_epoch++; s_rx_last_poll=now; s_rx_last_wraps=wraps; return 0U;
    }
    s_rx_last_poll=now; s_rx_last_wraps=wraps; copy_started=now;
    if (depth>s_stats.rx_high_water) s_stats.rx_high_water=(uint16_t)depth;
    s_stats.rx_buffer_used=(uint16_t)depth;
    if (length>depth) length=(uint16_t)depth;
    for(i=0U;i<length;++i) data[i]=s_rx_buffer[(s_rx_consumed+i)%DEBUG_UART_RX_BUFFER_SIZE];
    __DMB();
    if (epoch!=s_rx_epoch || s_rx_recovery) return 0U;
    /* If DMA overwrote the region during a preemption, none of this copy is valid. */
    produced=rx_produced(&wraps,&pending);
    if (produced-s_rx_consumed>=DEBUG_UART_RX_BUFFER_SIZE ||
        ((uint32_t)(HAL_GetTick()-copy_started)>=40U && (pending || wraps!=s_rx_last_wraps))) {
        s_stats.rx_overflows++;s_stats.rx_dropped_bytes+=produced-s_rx_consumed;
        s_rx_consumed=produced;s_rx_epoch++;s_rx_last_poll=HAL_GetTick();s_rx_last_wraps=wraps;return 0U;
    }
    s_rx_consumed+=length; s_stats.rx_consumed+=length;
    s_stats.rx_bytes=s_rx_total_base+produced; s_stats.rx_buffer_used=(uint16_t)(produced-s_rx_consumed);
    return length;
}
uint32_t debug_uart_rx_epoch(void) { return s_rx_epoch; }
void debug_uart_rx_note_parse(uint32_t cycles) { if (cycles>s_stats.rx_parse_max_cycles) s_stats.rx_parse_max_cycles=cycles; }
void debug_uart_rx_note_line_drop(uint8_t queue_full) { if(queue_full) s_stats.rx_queue_drops++; else s_stats.rx_line_overflows++; }
#else
/* IRQ bridges stay link-compatible. RX DMA, recovery and its 512 B storage do
 * not exist in the production logging-only profile. */
void debug_uart_rx_event_isr(uint16_t position, uint32_t kind) { (void)position; (void)kind; }
void debug_uart_rx_error_isr(uint32_t sr, uint32_t error) { (void)sr; (void)error; }
void debug_uart_rx_abort_isr(void) { }
void debug_uart_rx_service(void) { }
uint16_t debug_uart_rx_read(uint8_t *data, uint16_t length) { (void)data; (void)length; return 0U; }
uint32_t debug_uart_rx_epoch(void) { return 0U; }
void debug_uart_rx_note_parse(uint32_t cycles) { (void)cycles; }
void debug_uart_rx_note_line_drop(uint8_t queue_full) { (void)queue_full; }
#endif /* APP_UART_RX_ACTIVE */
void debug_uart_get_stats(debug_uart_stats_t *stats) {
    uint32_t mask;
    if(!stats) return;
    /* Only the scalar copy masks USART1; no full telemetry/system snapshot here. */
    mask=guard(); *stats=s_stats; stats->buffer_used=used();
    stats->buffer_free=(uint16_t)(DEBUG_UART_TX_BUFFER_SIZE-1U-stats->buffer_used);
    stats->tx_busy=s_tx_busy;
#if APP_UART_RX_ACTIVE
    stats->rx_epoch=s_rx_epoch;
    stats->rx_running=s_rx_running; stats->rx_recovery_pending=s_rx_recovery;
#else
    stats->rx_epoch=0U; stats->rx_running=0U; stats->rx_recovery_pending=0U;
#endif
    unguard(mask);
}
