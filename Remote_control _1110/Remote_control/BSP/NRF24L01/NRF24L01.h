#ifndef __NRF24L01_H
#define __NRF24L01_H

#include <stdint.h>
#include "NRF24L01_Define.h"

/*外部可调用全局数组***********/

extern uint8_t NRF24L01_TxAddress[];
extern uint8_t NRF24L01_TxPacket[];

extern uint8_t NRF24L01_RxAddress[];
extern uint8_t NRF24L01_RxPacket[];

/***********外部可调用全局数组*/

typedef enum
{
    NRF24L01_TX_IDLE = 0,
    NRF24L01_TX_BUSY,
    NRF24L01_TX_SUCCESS,
    NRF24L01_TX_MAX_RT,
    NRF24L01_TX_ERROR
} NRF24L01_TxResult;

/*函数声明*********************/

/*指令实现*/
uint8_t NRF24L01_ReadReg(uint8_t RegAddress);
void NRF24L01_ReadRegs(uint8_t RegAddress, uint8_t *DataArray, uint8_t Count);
void NRF24L01_WriteReg(uint8_t RegAddress, uint8_t Data);
void NRF24L01_WriteRegs(uint8_t RegAddress, uint8_t *DataArray, uint8_t Count);
void NRF24L01_ReadRxPayload(uint8_t *DataArray, uint8_t Count);
void NRF24L01_WriteTxPayload(uint8_t *DataArray, uint8_t Count);
void NRF24L01_FlushTx(void);
void NRF24L01_FlushRx(void);
uint8_t NRF24L01_ReadStatus(void);

/*功能函数*/
void NRF24L01_PowerDown(void);
void NRF24L01_StandbyI(void);
void NRF24L01_Rx(void);
void NRF24L01_Tx(void);

void NRF24L01_Init(void);
uint8_t NRF24L01_Send(void);
uint8_t NRF24L01_Receive(void);
void NRF24L01_UpdateRxAddress(void);
uint8_t NRF24L01_StartSend(uint8_t *DataArray, uint8_t Count);
NRF24L01_TxResult NRF24L01_PollSend(void);
void NRF24L01_AbortSend(void);

typedef struct {
    uint32_t tx_success, max_rt, empty_ack, ack_payloads;
    uint32_t bad_width, queue_overflows, spi_errors, recoveries;
    uint32_t capability_failures;
} NRF24L01_Stats;

/* 仅由主循环调用，初始化和故障恢复均采用短步骤。 */
void NRF24L01_Service(void);
uint8_t NRF24L01_IsInitialized(void);
uint8_t NRF24L01_HasAckPayload(void);
uint8_t NRF24L01_PopAckPayload(uint8_t *data, uint8_t *length);
void NRF24L01_GetStats(NRF24L01_Stats *stats);

/*********************函数声明*/


#endif


/*****************江协科技|版权所有****************/
/*****************jiangxiekeji.com*****************/
