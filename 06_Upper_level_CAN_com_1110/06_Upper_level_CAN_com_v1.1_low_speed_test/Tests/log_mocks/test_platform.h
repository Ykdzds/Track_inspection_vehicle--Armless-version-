#ifndef F103_LOG_TEST_PLATFORM_H
#define F103_LOG_TEST_PLATFORM_H
#include <stdint.h>
#include <stddef.h>
#include "can_upper_tx.h"
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef struct { unsigned count, size, length, head; uint8_t *storage; } StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
typedef struct { int unused; } StaticTask_t;
typedef void *TaskHandle_t;
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1
#define HAL_BUSY 2
#define HAL_TIMEOUT 3
#define GPIO_PIN_RESET 0
#define KEY1_GPIO_Port 0
#define KEY2_GPIO_Port 0
#define KEY3_GPIO_Port 0
#define KEY1_Pin 1
#define KEY2_Pin 2
#define KEY3_Pin 3
#define I2C_MEMADD_SIZE_8BIT 1
#define OLED_6X8 6
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(value) (value)
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
typedef struct { unsigned Pin, Mode, Speed; } GPIO_InitTypeDef;
typedef struct { uint32_t CR, CSR, DR1, DR2, DR3; } TestRegisters;
extern TestRegisters test_registers;
#define RCC (&test_registers)
#define BKP (&test_registers)
#define PWR (&test_registers)
#define PWR_CR_DBP 1U
#define RCC_CSR_IWDGRSTF 2U
#define RCC_CSR_SFTRSTF 4U
#define GPIOB 1
#define GPIO_PIN_8 256
#define GPIO_PIN_9 512
#define GPIO_PIN_SET 1
#define GPIO_MODE_OUTPUT_OD 1
#define GPIO_SPEED_FREQ_HIGH 1
#define taskDISABLE_INTERRUPTS() ((void)0)
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_RCC_BKP_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_CLEAR_RESET_FLAGS() ((void)0)
#define __HAL_RCC_I2C1_FORCE_RESET() ((void)0)
#define __HAL_RCC_I2C1_RELEASE_RESET() ((void)0)
HAL_StatusTypeDef HAL_I2C_DeInit(int *);
HAL_StatusTypeDef HAL_I2C_Init(int *);
uint32_t HAL_I2C_GetError(int *);
uint32_t HAL_I2C_GetState(int *);
void HAL_GPIO_WritePin(int, int, int);
void HAL_GPIO_Init(int, GPIO_InitTypeDef *);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);
TaskHandle_t xTaskGetIdleTaskHandle(void);
TaskHandle_t xTimerGetTimerDaemonTaskHandle(void);
void OLED_ShowStringClipped(uint8_t, uint8_t, const char *, uint16_t, uint8_t, uint8_t);
extern int huart1, hi2c1, hiwdg;
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Transmit(int *, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(int *, unsigned, unsigned, unsigned, uint8_t *, unsigned, unsigned);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(int *, unsigned, unsigned, unsigned);
void HAL_IWDG_Refresh(int *);
int HAL_GPIO_ReadPin(int, int);
void Error_Handler(void);
void OLED_Clear(void);
void OLED_ShowString(unsigned, unsigned, char *, unsigned);
QueueHandle_t xQueueCreateStatic(unsigned, unsigned, uint8_t *, StaticQueue_t *);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueReceive(QueueHandle_t, void *, unsigned);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t);
uint32_t ulTaskNotifyTake(int, unsigned);
TickType_t xTaskGetTickCount(void);
void xTaskNotifyGive(TaskHandle_t);
void vTaskDelayUntil(TickType_t *, unsigned);
void vTaskDelay(unsigned);
TaskHandle_t xTaskCreateStatic(void (*)(void *), const char *, unsigned, void *, unsigned, StackType_t *, StaticTask_t *);
typedef struct { uint16_t id; uint8_t dlc; uint8_t data[8]; uint32_t timestamp; } CanBusFrame_t;
typedef struct { uint32_t rx_frames, tx_frames, rx_queue_overruns, tx_errors, hal_errors, bus_off_count, invalid_frames, mailbox_full, tx_dropped, last_error; uint32_t tx_cancelled, tx_replaced, tx_expired, tx_timeouts, result_overruns, abort_timeouts, recovery_count; } CanBusStats_t;
typedef struct { CanBusFrame_t frame; uint32_t generation, mailbox, age, tsr, esr; UpperTxOutcome_t outcome; } CanBusTxResult_t;
uint8_t CanBus_GetTxResultEx(CanBusTxResult_t *);
HAL_StatusTypeDef CanBus_Start(void);
HAL_StatusTypeDef CanBus_Send(uint16_t, const uint8_t *);
uint8_t CanBus_Receive(CanBusFrame_t *);
void CanBus_GetStats(CanBusStats_t *);
uint8_t CanBus_IsBusOff(void);
void CanBus_SetRxTask(void *);
void CanBus_Service(uint32_t);
void CanBus_ClearPending(void);
void CanBus_DropMotion(void);
uint8_t CanBus_IsStarted(void);
uint8_t CanBus_GetTxResult(CanBusFrame_t *, uint8_t *);
#endif
