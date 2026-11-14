#ifndef BUS_TEST_MAIN_H
#define BUS_TEST_MAIN_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1
#define HAL_BUSY 2
#define ENABLE 1
typedef struct { uint32_t TSR, ESR; } CAN_TypeDef;
typedef struct { CAN_TypeDef *Instance; uint32_t ErrorCode; } CAN_HandleTypeDef;
typedef struct { uint32_t StdId, IDE, RTR, DLC; } CAN_TxHeaderTypeDef;
typedef CAN_TxHeaderTypeDef CAN_RxHeaderTypeDef;
typedef struct { uint32_t FilterMode, FilterScale, FilterFIFOAssignment, FilterActivation, FilterBank, FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow; } CAN_FilterTypeDef;
#define CAN_TX_MAILBOX0 1U
#define CAN_TX_MAILBOX1 2U
#define CAN_TX_MAILBOX2 4U
#define CAN_ID_STD 0U
#define CAN_RTR_DATA 0U
#define CAN_RX_FIFO0 0U
#define CAN_FILTERMODE_IDLIST 0U
#define CAN_FILTERSCALE_16BIT 0U
#define CAN_FILTER_FIFO0 0U
#define CAN_IT_RX_FIFO0_MSG_PENDING 1U
#define CAN_IT_RX_FIFO0_OVERRUN 2U
#define CAN_IT_TX_MAILBOX_EMPTY 4U
#define CAN_IT_ERROR 8U
#define CAN_IT_BUSOFF 16U
#define CAN_IT_ERROR_WARNING 32U
#define CAN_IT_ERROR_PASSIVE 64U
#define CAN_IT_LAST_ERROR_CODE 128U
#define HAL_CAN_ERROR_RX_FOV0 1U
#define HAL_CAN_ERROR_BOF 2U
#define HAL_CAN_ERROR_TX_ALST0 4U
#define HAL_CAN_ERROR_TX_TERR0 8U
#define HAL_CAN_ERROR_TX_ALST1 16U
#define HAL_CAN_ERROR_TX_TERR1 32U
#define HAL_CAN_ERROR_TX_ALST2 64U
#define HAL_CAN_ERROR_TX_TERR2 128U
#define CAN_TSR_RQCP0 1U
#define CAN_TSR_TXOK0 2U
#define CAN_TSR_ALST0 4U
#define CAN_TSR_TERR0 8U
#define CAN_FLAG_RQCP0 0U
#define CAN_FLAG_RQCP1 1U
#define CAN_FLAG_RQCP2 2U
#define __HAL_CAN_CLEAR_FLAG(handle, flag) ((handle)->Instance->TSR &= ~(15UL << ((flag) * 8U)))
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *, CAN_FilterTypeDef *);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_DeInit(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_ResetError(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *, CAN_TxHeaderTypeDef *, uint8_t *, uint32_t *);
uint32_t HAL_CAN_IsTxMessagePending(CAN_HandleTypeDef *, uint32_t);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *);
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *, uint32_t, CAN_RxHeaderTypeDef *, uint8_t *);
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *);
#endif
