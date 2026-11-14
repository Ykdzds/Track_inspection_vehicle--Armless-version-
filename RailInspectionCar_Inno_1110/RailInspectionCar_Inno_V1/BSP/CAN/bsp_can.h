#ifndef BSP_CAN_H
#define BSP_CAN_H

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#define BSP_CAN_RX_CAPACITY 64U

typedef struct
{
    uint32_t timestamp_ms;
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} bsp_can_frame_t;

typedef enum
{
    BSP_CAN_OFF,
    BSP_CAN_WAIT_INIT,
    BSP_CAN_WAIT_READY,
    BSP_CAN_READY,
    BSP_CAN_BACKOFF
} bsp_can_phase_t;

typedef struct
{
    bsp_can_frame_t frame;
    uint32_t cookie;
    bool success;
} bsp_can_tx_result_t;

typedef struct
{
    bsp_can_phase_t phase;
    uint32_t generation;
    uint32_t rx_count;
    uint32_t rx_dropped;
    uint32_t rx_invalid;
    uint32_t rx_fifo_overrun;
    uint32_t priority_replaced;
    uint32_t tx_submitted;
    uint32_t tx_success;
    uint32_t tx_failed;
    uint32_t tx_expired;
    uint32_t bus_off_count;
    uint32_t recovery_count;
    uint32_t init_timeouts;
    uint32_t last_esr;
    uint16_t rx_high_water;
    uint16_t rx_queued;
} bsp_can_stats_t;

void bsp_can_init(TaskHandle_t task, uint32_t now_ms);
void bsp_can_service(uint32_t now_ms);
bool bsp_can_ready(void);
bool bsp_can_receive(bsp_can_frame_t *frame);
bool bsp_can_send(const bsp_can_frame_t *frame, uint32_t cookie,
                  uint32_t deadline_ms);
bool bsp_can_take_tx_result(bsp_can_tx_result_t *result);
void bsp_can_cancel(uint32_t cookie);
void bsp_can_get_stats(bsp_can_stats_t *stats);
void bsp_can_tx_isr(void);
void bsp_can_rx_isr(uint32_t fifo);
void bsp_can_error_isr(void);

#endif
