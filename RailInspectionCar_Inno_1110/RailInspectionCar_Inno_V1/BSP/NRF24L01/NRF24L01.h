#ifndef NRF24L01_H
#define NRF24L01_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "NRF24L01_Define.h"
#include "app_link_diag.h"

extern uint8_t NRF24L01_RxAddress[5];
extern uint8_t NRF24L01_RxPacket[32];

typedef enum
{
    NRF_FAILURE_NONE = 0,
    NRF_FAILURE_SPI,
    NRF_FAILURE_DMA_TIMEOUT,
    NRF_FAILURE_CONFIG,
    NRF_FAILURE_FEATURE,
    NRF_FAILURE_DYNPD,
    NRF_FAILURE_FIFO,
    NRF_FAILURE_WIDTH,
    NRF_FAILURE_FIRST_ACK,
    NRF_FAILURE_ACK_STALLED,
    NRF_FAILURE_CONFIG_CHECK
} nrf_failure_t;

/* Last failure evidence, captured before recovery clears DMA/SPI state. */
typedef struct
{
    uint32_t tick_ms, elapsed_ms;
    uint32_t spi_cr1, spi_cr2, spi_sr;
    uint32_t rx_remaining, tx_remaining, rx_cr, tx_cr;
    uint32_t rx_flags, tx_flags, rx_error, tx_error;
    uint8_t valid, state, command, length, active, done, error;
} NRF24L01_FailureSnapshot;

typedef struct
{
    uint32_t spi_errors;
    uint32_t fifo_errors;
    uint32_t recoveries;
    uint32_t rx_packets;
    uint32_t fifo_full_events;
    uint32_t dma_started;
    uint32_t dma_completed;
    uint32_t dma_timeouts;
    uint32_t rx_dropped;
    uint32_t invalid_widths;
    uint32_t ack_queued;
    uint32_t ack_loaded;
    uint32_t ack_replaced;
    uint32_t ack_flushed;
    uint32_t ack_failures;
    uint32_t recovery_generation;
    uint32_t verify_failures;
    uint32_t ack_load_timeouts;
    uint32_t config_check_mismatches;
    uint32_t config_check_count;
    uint32_t ack_max_wait_ms;
    uint32_t ack_pending_age_ms;
    uint32_t ack_fifo_age_ms;
    uint32_t last_failure;
    uint32_t ack_clear_count;
    uint32_t ack_clear_by_reason[5];
    uint32_t event_overwrites;
    NRF24L01_FailureSnapshot failure_snapshot;
    uint8_t ack_pending;
    uint8_t ack_in_fifo;
    uint8_t ack_clear_reason;
    uint8_t initialized;
    uint8_t radio_ready;
    uint8_t config;
    uint8_t status;
    uint8_t fifo_status;
    uint8_t ack_supported;
    uint8_t feature;
    uint8_t dynpd;
    uint8_t dma_busy;
} NRF24L01_Stats;

typedef enum
{
    NRF24L01_ACK_CLEAR_NONE = 0,
    NRF24L01_ACK_CLEAR_MAX_RT = 1,
    NRF24L01_ACK_CLEAR_TIMEOUT = 2,
    NRF24L01_ACK_CLEAR_TX_FULL = 3,
    NRF24L01_ACK_CLEAR_RECOVERY = 4
} NRF24L01_AckClearReason;

typedef struct
{
    uint32_t timestamp_ms;
    uint8_t kind, type, session, sequence;
} NRF24L01_Event;

bool NRF24L01_PopEvent(NRF24L01_Event *event);

/* 初始化只建立状态；硬件配置、恢复与FIFO搬运均由任务服务异步完成。 */
void NRF24L01_Init(void);
void NRF24L01_BindTask(TaskHandle_t task);
void NRF24L01_Service(void);
/* SPI2专用短中断入口；仅通知任务，不等待DMA关闭或SPI空闲。 */
void NRF24L01_DmaRxIsr(void);
void NRF24L01_DmaTxIsr(void);
bool NRF24L01_IsInitialized(void);
bool NRF24L01_IsRadioReady(void);
void NRF24L01_ReportControlFrame(void);
void NRF24L01_GetStats(NRF24L01_Stats *stats);
#if APP_LINK_DIAG_ACTIVE && APP_NRF_DIAGNOSTIC_ENABLED
/* Cached scalars only: health=12, ACK stages=16, failure evidence=16 words. */
uint8_t NRF24L01_DiagnosticWords(uint32_t *words, uint8_t phase);
#endif
/* 仅搬运已完成的接收包，不访问SPI，不等待无线或DMA。 */
uint8_t NRF24L01_Receive(void);
bool NRF24L01_CanQueueAck(void);
/* 软件中只保留最新一帧，不代表装载成功，更不代表遥控器已收到。 */
bool NRF24L01_QueueAckPayload(const uint8_t payload[32]);

#endif
