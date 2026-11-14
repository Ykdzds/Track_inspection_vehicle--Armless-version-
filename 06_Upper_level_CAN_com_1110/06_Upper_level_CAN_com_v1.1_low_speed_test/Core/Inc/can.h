/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.h
  * @brief   This file contains all the function prototypes for
  *          the can.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_H__
#define __CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "can_upper_tx.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern CAN_HandleTypeDef hcan;

#define CAN_BUS_RX_QUEUE_LENGTH 32U

typedef struct
{
  uint16_t id;
  uint8_t dlc;
  uint8_t data[8];
  uint32_t timestamp;
} CanBusFrame_t;

typedef struct
{
  CanBusFrame_t frame;
  uint32_t generation, mailbox, age, tsr, esr;
  UpperTxOutcome_t outcome;
} CanBusTxResult_t;

typedef struct
{
  uint32_t rx_frames;
  uint32_t tx_frames;
  uint32_t rx_queue_overruns;
  uint32_t tx_errors;
  uint32_t hal_errors;
  uint32_t bus_off_count;
  uint32_t invalid_frames;
  uint32_t mailbox_full;
  uint32_t tx_dropped;
  uint32_t last_error;
  uint32_t tx_cancelled, tx_replaced, tx_expired, tx_timeouts, tx_retried;
  uint32_t result_overruns, abort_timeouts, recovery_count;
} CanBusStats_t;

void MX_CAN_Init(void);

HAL_StatusTypeDef CanBus_Start(void);
HAL_StatusTypeDef CanBus_Send(uint16_t id, const uint8_t data[8]);
uint8_t CanBus_Receive(CanBusFrame_t *frame);
void CanBus_GetStats(CanBusStats_t *stats);
uint8_t CanBus_IsBusOff(void);
void CanBus_SetRxTask(void *task);
void CanBus_Service(uint32_t now);
void CanBus_ClearPending(void);
void CanBus_DropMotion(void);
uint8_t CanBus_IsStarted(void);
uint8_t CanBus_GetTxResult(CanBusFrame_t *frame, uint8_t *success);
uint8_t CanBus_GetTxResultEx(CanBusTxResult_t *result);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_H__ */

