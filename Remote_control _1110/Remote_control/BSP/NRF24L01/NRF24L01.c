/***************************************************************************************
  * NRF24L01无线通信模块驱动程序
  * 底层使用HAL库通过SPI2硬件外设通信
  ***************************************************************************************/

#include "main.h"
#include "spi.h"
#include "NRF24L01.h"
#include "NRF24L01_Define.h"
#include <string.h>

/* 全局变量 */
uint8_t NRF24L01_TxAddress[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
#define NRF24L01_TX_PACKET_WIDTH 32
uint8_t NRF24L01_TxPacket[NRF24L01_TX_PACKET_WIDTH];

uint8_t NRF24L01_RxAddress[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
#define NRF24L01_RX_PACKET_WIDTH 32
uint8_t NRF24L01_RxPacket[NRF24L01_RX_PACKET_WIDTH];

#define NRF24L01_SPI_TIMEOUT 2U
#define NRF24L01_MAX_TRANSFER_SIZE 33U
#define NRF24L01_TX_SETTLE_TIME 2U

typedef enum
{
    NRF24L01_TX_STATE_IDLE = 0,
    NRF24L01_TX_STATE_SETTLING,
    NRF24L01_TX_STATE_WAITING
} NRF24L01_TxState;

static NRF24L01_TxState NRF24L01_TxStateValue = NRF24L01_TX_STATE_IDLE;
static uint32_t NRF24L01_TxStateTick = 0U;
static uint8_t spi_fault;
static uint8_t initialized;
static uint8_t ack_capable;
static uint8_t init_step;
static uint8_t recovery_wait;
static uint32_t recovery_tick;
static uint8_t ack_queue[3][32];
static uint8_t ack_head;
static uint8_t ack_count;
static NRF24L01_Stats radio_stats;

static void NRF24L01_DrainAck(uint8_t clear_irq);

/* 控制CE引脚 */
static void NRF24L01_W_CE(uint8_t BitValue)
{
    HAL_GPIO_WritePin(CE_GPIO_Port, CE_Pin,
                      (BitValue != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* 控制CSN引脚 */
static void NRF24L01_W_CSN(uint8_t BitValue)
{
    HAL_GPIO_WritePin(CSN_GPIO_Port, CSN_Pin,
                      (BitValue != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/*
 * SPI连续事务，CSN在整条命令期间保持低电平。
 */
static HAL_StatusTypeDef NRF24L01_SPI_Transfer(uint8_t *txData,
                                                uint8_t *rxData,
                                                uint8_t Length)
{
    HAL_StatusTypeDef Status;

    if ((txData == NULL) || (rxData == NULL) || (Length == 0U) ||
        (Length > NRF24L01_MAX_TRANSFER_SIZE))
    {
        return HAL_ERROR;
    }
    if (spi_fault) return HAL_ERROR;

    NRF24L01_W_CSN(0U);
    Status = HAL_SPI_TransmitReceive(&hspi2, txData, rxData, Length,
                                     NRF24L01_SPI_TIMEOUT);
    NRF24L01_W_CSN(1U);
    if (Status != HAL_OK) {
        spi_fault = 1U;
        radio_stats.spi_errors++;
    }

    return Status;
}

/* 单字节命令事务 */
static HAL_StatusTypeDef NRF24L01_SPI_Command(uint8_t Command)
{
    uint8_t TxData;
    uint8_t RxData;

    TxData = Command;
    RxData = 0U;
    return NRF24L01_SPI_Transfer(&TxData, &RxData, 1U);
}

uint8_t NRF24L01_ReadReg(uint8_t RegAddress)
{
    uint8_t TxData[2];
    uint8_t RxData[2];

    TxData[0] = (uint8_t)(NRF24L01_R_REGISTER | (RegAddress & 0x1FU));
    TxData[1] = NRF24L01_NOP;
    RxData[0] = 0U;
    RxData[1] = 0xFFU;

    if (NRF24L01_SPI_Transfer(TxData, RxData, 2U) != HAL_OK)
    {
        return 0xFFU;
    }
    return RxData[1];
}

void NRF24L01_ReadRegs(uint8_t RegAddress, uint8_t *DataArray, uint8_t Count)
{
    uint8_t TxData[NRF24L01_MAX_TRANSFER_SIZE];
    uint8_t RxData[NRF24L01_MAX_TRANSFER_SIZE];
    uint8_t i;

    if ((DataArray == NULL) || (Count == 0U))
    {
        return;
    }
    if (Count > 5U)
    {
        Count = 5U;
    }

    TxData[0] = (uint8_t)(NRF24L01_R_REGISTER | (RegAddress & 0x1FU));
    RxData[0] = 0U;
    for (i = 0U; i < Count; i++)
    {
        TxData[i + 1U] = NRF24L01_NOP;
        RxData[i + 1U] = 0xFFU;
    }

    if (NRF24L01_SPI_Transfer(TxData, RxData, (uint8_t)(Count + 1U)) != HAL_OK)
    {
        for (i = 0U; i < Count; i++)
        {
            DataArray[i] = 0xFFU;
        }
        return;
    }
    for (i = 0U; i < Count; i++)
    {
        DataArray[i] = RxData[i + 1U];
    }
}

void NRF24L01_WriteReg(uint8_t RegAddress, uint8_t Data)
{
    uint8_t TxData[2];
    uint8_t RxData[2];

    TxData[0] = (uint8_t)(NRF24L01_W_REGISTER | (RegAddress & 0x1FU));
    TxData[1] = Data;
    RxData[0] = 0U;
    RxData[1] = 0U;
    (void)NRF24L01_SPI_Transfer(TxData, RxData, 2U);
}

void NRF24L01_WriteRegs(uint8_t RegAddress, uint8_t *DataArray, uint8_t Count)
{
    uint8_t TxData[NRF24L01_MAX_TRANSFER_SIZE];
    uint8_t RxData[NRF24L01_MAX_TRANSFER_SIZE];
    uint8_t i;

    if ((DataArray == NULL) || (Count == 0U))
    {
        return;
    }
    if (Count > 5U)
    {
        Count = 5U;
    }

    TxData[0] = (uint8_t)(NRF24L01_W_REGISTER | (RegAddress & 0x1FU));
    RxData[0] = 0U;
    for (i = 0U; i < Count; i++)
    {
        TxData[i + 1U] = DataArray[i];
        RxData[i + 1U] = 0U;
    }
    (void)NRF24L01_SPI_Transfer(TxData, RxData, (uint8_t)(Count + 1U));
}

void NRF24L01_ReadRxPayload(uint8_t *DataArray, uint8_t Count)
{
    uint8_t TxData[NRF24L01_MAX_TRANSFER_SIZE];
    uint8_t RxData[NRF24L01_MAX_TRANSFER_SIZE];
    uint8_t i;

    if ((DataArray == NULL) || (Count == 0U))
    {
        return;
    }
    if (Count > 32U)
    {
        Count = 32U;
    }

    TxData[0] = NRF24L01_R_RX_PAYLOAD;
    RxData[0] = 0U;
    for (i = 0U; i < Count; i++)
    {
        TxData[i + 1U] = NRF24L01_NOP;
        RxData[i + 1U] = 0xFFU;
    }

    if (NRF24L01_SPI_Transfer(TxData, RxData, (uint8_t)(Count + 1U)) != HAL_OK)
    {
        for (i = 0U; i < Count; i++)
        {
            DataArray[i] = 0xFFU;
        }
        return;
    }
    for (i = 0U; i < Count; i++)
    {
        DataArray[i] = RxData[i + 1U];
    }
}

void NRF24L01_WriteTxPayload(uint8_t *DataArray, uint8_t Count)
{
    uint8_t TxData[NRF24L01_MAX_TRANSFER_SIZE];
    uint8_t RxData[NRF24L01_MAX_TRANSFER_SIZE];
    uint8_t i;

    if ((DataArray == NULL) || (Count == 0U))
    {
        return;
    }
    if (Count > 32U)
    {
        Count = 32U;
    }

    TxData[0] = NRF24L01_W_TX_PAYLOAD;
    RxData[0] = 0U;
    for (i = 0U; i < Count; i++)
    {
        TxData[i + 1U] = DataArray[i];
        RxData[i + 1U] = 0U;
    }
    (void)NRF24L01_SPI_Transfer(TxData, RxData, (uint8_t)(Count + 1U));
}

void NRF24L01_FlushTx(void)
{
    (void)NRF24L01_SPI_Command(NRF24L01_FLUSH_TX);
}

void NRF24L01_FlushRx(void)
{
    (void)NRF24L01_SPI_Command(NRF24L01_FLUSH_RX);
}

uint8_t NRF24L01_ReadStatus(void)
{
    uint8_t TxData;
    uint8_t RxData;

    TxData = NRF24L01_NOP;
    RxData = 0xFFU;
    if (NRF24L01_SPI_Transfer(&TxData, &RxData, 1U) != HAL_OK)
    {
        return 0xFFU;
    }
    return RxData;
}

void NRF24L01_PowerDown(void)
{
    uint8_t Config;

    NRF24L01_W_CE(0U);
    Config = NRF24L01_ReadReg(NRF24L01_CONFIG);
    if (Config == 0xFFU)
    {
        return;
    }
    Config &= (uint8_t)(~0x02U);
    NRF24L01_WriteReg(NRF24L01_CONFIG, Config);
}

void NRF24L01_StandbyI(void)
{
    uint8_t Config;

    NRF24L01_W_CE(0U);
    Config = NRF24L01_ReadReg(NRF24L01_CONFIG);
    if (Config == 0xFFU)
    {
        return;
    }
    Config |= 0x02U;
    NRF24L01_WriteReg(NRF24L01_CONFIG, Config);
}

void NRF24L01_Rx(void)
{
    /* 保留旧接口名称，但遥控器固定PTX，不再切入主动接收。 */
    NRF24L01_StandbyI();
}

void NRF24L01_Tx(void)
{
    uint8_t Config;

    NRF24L01_W_CE(0U);
    Config = NRF24L01_ReadReg(NRF24L01_CONFIG);
    if (Config == 0xFFU)
    {
        return;
    }
    Config |= 0x02U;
    Config &= (uint8_t)(~0x01U);
    NRF24L01_WriteReg(NRF24L01_CONFIG, Config);
    HAL_Delay(2U);
    NRF24L01_W_CE(1U);
}

void NRF24L01_Init(void)
{
    memset(&radio_stats, 0, sizeof(radio_stats));
    ack_head = 0U; ack_count = 0U;
    initialized = 0U; ack_capable = 0U;
    spi_fault = 0U; init_step = 0U; recovery_wait = 0U;
    NRF24L01_TxStateValue = NRF24L01_TX_STATE_IDLE;
    NRF24L01_W_CE(0U);
    NRF24L01_W_CSN(1U);
}

static void NRF24L01_Fail(void)
{
    NRF24L01_W_CE(0U);
    initialized = 0U; ack_capable = 0U;
    NRF24L01_TxStateValue = NRF24L01_TX_STATE_IDLE;
    if (!recovery_wait) {
        recovery_wait = 1U;
        recovery_tick = HAL_GetTick();
    }
}

void NRF24L01_Service(void)
{
    uint8_t tx[2] = {0x50U, 0x73U};
    uint8_t rx[2];
    uint8_t feature, dynpd;
    if (spi_fault) NRF24L01_Fail();
    if (recovery_wait) {
        if (HAL_GetTick() - recovery_tick < 500U) return;
        recovery_wait = 0U; spi_fault = 0U; init_step = 0U;
        ack_head = 0U; ack_count = 0U;
        radio_stats.recoveries++;
    }
    if (initialized) {
        if (NRF24L01_TxStateValue == NRF24L01_TX_STATE_IDLE && ack_capable)
            NRF24L01_DrainAck(0U);
        if (spi_fault) NRF24L01_Fail();
        return;
    }
    /* 每次仅执行一个初始化阶段，避免配置失败占用整个10ms发送周期。 */
    switch (init_step) {
    case 0: NRF24L01_WriteReg(NRF24L01_CONFIG, 0x08U); break;
    case 1: NRF24L01_WriteReg(NRF24L01_EN_AA, 0x01U); break;
    case 2: NRF24L01_WriteReg(NRF24L01_EN_RXADDR, 0x01U); break;
    case 3: NRF24L01_WriteReg(NRF24L01_SETUP_AW, 0x03U); break;
    case 4: NRF24L01_WriteReg(NRF24L01_SETUP_RETR, 0x13U); break;
    case 5: NRF24L01_WriteReg(NRF24L01_RF_CH, 0x02U); break;
    case 6: NRF24L01_WriteReg(NRF24L01_RF_SETUP, 0x0EU); break;
    case 7: NRF24L01_WriteReg(NRF24L01_RX_PW_P0, 32U); break;
    case 8: NRF24L01_WriteRegs(NRF24L01_RX_ADDR_P0, NRF24L01_TxAddress, 5U); break;
    case 9: NRF24L01_WriteRegs(NRF24L01_TX_ADDR, NRF24L01_TxAddress, 5U); break;
    case 10: NRF24L01_WriteReg(NRF24L01_FEATURE, 0x06U); break;
    case 11: NRF24L01_WriteReg(NRF24L01_DYNPD, 0x01U); break;
    case 12:
        feature = NRF24L01_ReadReg(NRF24L01_FEATURE);
        dynpd = NRF24L01_ReadReg(NRF24L01_DYNPD);
        if (feature == 0x06U && dynpd == 0x01U) {
            ack_capable = 1U;
            init_step = 17U;
            return;
        }
        break;
    case 13: (void)NRF24L01_SPI_Transfer(tx, rx, 2U); break;
    case 14: NRF24L01_WriteReg(NRF24L01_FEATURE, 0x06U); break;
    case 15: NRF24L01_WriteReg(NRF24L01_DYNPD, 0x01U); break;
    case 16:
        feature = NRF24L01_ReadReg(NRF24L01_FEATURE);
        dynpd = NRF24L01_ReadReg(NRF24L01_DYNPD);
        ack_capable = (uint8_t)(feature == 0x06U && dynpd == 0x01U);
        if (!ack_capable && !spi_fault) radio_stats.capability_failures++;
        break;
    case 17: if (!ack_capable) NRF24L01_WriteReg(NRF24L01_FEATURE, 0U); break;
    case 18: if (!ack_capable) NRF24L01_WriteReg(NRF24L01_DYNPD, 0U); break;
    case 19: NRF24L01_FlushTx(); break;
    case 20: NRF24L01_FlushRx(); break;
    case 21: NRF24L01_WriteReg(NRF24L01_STATUS, 0x70U); break;
    case 22:
        NRF24L01_WriteReg(NRF24L01_CONFIG, 0x0AU);
        NRF24L01_TxStateTick = HAL_GetTick();
        break;
    case 23:
        if (HAL_GetTick() - NRF24L01_TxStateTick < 2U) return;
        if (NRF24L01_ReadReg(NRF24L01_CONFIG) != 0x0AU) {
            NRF24L01_Fail();
            return;
        }
        initialized = 1U;
        return;
    default: NRF24L01_Fail(); return;
    }
    if (spi_fault) { NRF24L01_Fail(); return; }
    init_step++;
}

static void NRF24L01_DrainAck(uint8_t clear_irq)
{
    uint8_t i, width, fifo;
    uint8_t tx[33], rx[33];
    for (i = 0U; i < 3U; i++) {
        fifo = NRF24L01_ReadReg(NRF24L01_FIFO_STATUS);
        if (spi_fault) return;
        if (fifo == 0xFFU) { NRF24L01_Fail(); return; }
        if (fifo & 1U) {
            if (i == 0U && !clear_irq) return;
            break;
        }
        tx[0] = NRF24L01_R_RX_PL_WID; tx[1] = NRF24L01_NOP;
        if (NRF24L01_SPI_Transfer(tx, rx, 2U) != HAL_OK) return;
        width = rx[1];
        if (width > 32U) {
            radio_stats.bad_width++;
            NRF24L01_FlushRx();
            break;
        }
        tx[0] = NRF24L01_R_RX_PAYLOAD;
        memset(tx + 1U, NRF24L01_NOP, width);
        if (NRF24L01_SPI_Transfer(tx, rx, (uint8_t)(width + 1U)) != HAL_OK) return;
        if (width != 32U) { radio_stats.bad_width++; continue; }
        if (ack_count == 3U) {
            ack_head = (uint8_t)((ack_head + 1U) % 3U);
            ack_count--; radio_stats.queue_overflows++;
        }
        memcpy(ack_queue[(ack_head + ack_count) % 3U], rx + 1U, 32U);
        ack_count++; radio_stats.ack_payloads++;
    }
    /* 只清RX_DR，不覆盖同一时刻到达的TX_DS/MAX_RT。 */
    NRF24L01_WriteReg(NRF24L01_STATUS, 0x40U);
}

uint8_t NRF24L01_IsInitialized(void) { return initialized; }
uint8_t NRF24L01_HasAckPayload(void) { return (uint8_t)(initialized && ack_capable); }

uint8_t NRF24L01_PopAckPayload(uint8_t *data, uint8_t *length)
{
    if (data == NULL || length == NULL || ack_count == 0U) return 0U;
    memcpy(data, ack_queue[ack_head], 32U);
    *length = 32U;
    ack_head = (uint8_t)((ack_head + 1U) % 3U); ack_count--;
    return 1U;
}

void NRF24L01_GetStats(NRF24L01_Stats *stats)
{
    if (stats != NULL) *stats = radio_stats;
}

uint8_t NRF24L01_StartSend(uint8_t *DataArray, uint8_t Count)
{
    uint8_t Config;

    if ((DataArray == NULL) || (Count != NRF24L01_TX_PACKET_WIDTH) ||
        (NRF24L01_TxStateValue != NRF24L01_TX_STATE_IDLE) || !initialized)
    {
        return 0U;
    }

    NRF24L01_W_CE(0U);
    NRF24L01_WriteReg(NRF24L01_STATUS, 0x30U);
    NRF24L01_FlushTx();
    NRF24L01_WriteRegs(NRF24L01_TX_ADDR, NRF24L01_TxAddress, 5U);
    NRF24L01_WriteRegs(NRF24L01_RX_ADDR_P0, NRF24L01_TxAddress, 5U);
    NRF24L01_WriteTxPayload(DataArray, Count);

    Config = NRF24L01_ReadReg(NRF24L01_CONFIG);
    if (Config == 0xFFU)
    {
        NRF24L01_FlushTx();
        NRF24L01_Fail();
        return 0U;
    }

    Config |= 0x02U;
    Config &= (uint8_t)(~0x01U);
    NRF24L01_WriteReg(NRF24L01_CONFIG, Config);
    if (spi_fault) { NRF24L01_Fail(); return 0U; }
    NRF24L01_TxStateTick = HAL_GetTick();
    NRF24L01_TxStateValue = NRF24L01_TX_STATE_SETTLING;
    return 1U;
}

NRF24L01_TxResult NRF24L01_PollSend(void)
{
    uint8_t Status;

    if (NRF24L01_TxStateValue == NRF24L01_TX_STATE_IDLE)
    {
        return NRF24L01_TX_IDLE;
    }

    if (NRF24L01_TxStateValue == NRF24L01_TX_STATE_SETTLING)
    {
        if ((HAL_GetTick() - NRF24L01_TxStateTick) < NRF24L01_TX_SETTLE_TIME)
        {
            return NRF24L01_TX_BUSY;
        }
        NRF24L01_W_CE(1U);
        NRF24L01_TxStateValue = NRF24L01_TX_STATE_WAITING;
        return NRF24L01_TX_BUSY;
    }

    Status = NRF24L01_ReadStatus();
    if (Status == 0xFFU)
    {
        NRF24L01_Fail();
        return NRF24L01_TX_ERROR;
    }
    if (ack_capable && (Status & 0x40U)) NRF24L01_DrainAck(1U);
    if ((Status & 0x30U) == 0x30U)
    {
        NRF24L01_AbortSend();
        return NRF24L01_TX_ERROR;
    }
    if ((Status & 0x10U) != 0U)
    {
        radio_stats.max_rt++;
        NRF24L01_AbortSend();
        return NRF24L01_TX_MAX_RT;
    }
    if ((Status & 0x20U) != 0U)
    {
        NRF24L01_W_CE(0U);
        NRF24L01_WriteReg(NRF24L01_STATUS, 0x20U);
        NRF24L01_FlushTx();
        NRF24L01_TxStateValue = NRF24L01_TX_STATE_IDLE;
        radio_stats.tx_success++;
        if (!(Status & 0x40U)) radio_stats.empty_ack++;
        if (spi_fault) NRF24L01_Fail();
        return NRF24L01_TX_SUCCESS;
    }
    if (spi_fault || !initialized) {
        NRF24L01_Fail();
        return NRF24L01_TX_ERROR;
    }

    return NRF24L01_TX_BUSY;
}

void NRF24L01_AbortSend(void)
{
    NRF24L01_W_CE(0U);
    NRF24L01_WriteReg(NRF24L01_STATUS, 0x30U);
    NRF24L01_FlushTx();
    NRF24L01_TxStateValue = NRF24L01_TX_STATE_IDLE;
    if (spi_fault) NRF24L01_Fail();
}

uint8_t NRF24L01_Send(void)
{
    uint8_t SendFlag;
    NRF24L01_TxResult Result;
    uint32_t StartTick;

    NRF24L01_AbortSend();
    if (NRF24L01_StartSend(NRF24L01_TxPacket,
                           NRF24L01_TX_PACKET_WIDTH) == 0U)
    {
        return 3U;
    }

    StartTick = HAL_GetTick();
    SendFlag = 4U;
    while ((HAL_GetTick() - StartTick) < 100U)
    {
        Result = NRF24L01_PollSend();
        if (Result == NRF24L01_TX_SUCCESS)
        {
            SendFlag = 1U;
            break;
        }
        if (Result == NRF24L01_TX_MAX_RT)
        {
            SendFlag = 2U;
            break;
        }
        if (Result == NRF24L01_TX_ERROR)
        {
            SendFlag = 3U;
            break;
        }
        HAL_Delay(1U);
    }

    if (SendFlag == 4U)
    {
        NRF24L01_AbortSend();
    }
    return SendFlag;
}

uint8_t NRF24L01_Receive(void)
{
    uint8_t length;
    NRF24L01_Service();
    return NRF24L01_PopAckPayload(NRF24L01_RxPacket, &length);
}

void NRF24L01_UpdateRxAddress(void)
{
    NRF24L01_WriteRegs(NRF24L01_RX_ADDR_P0, NRF24L01_RxAddress, 5U);
}
