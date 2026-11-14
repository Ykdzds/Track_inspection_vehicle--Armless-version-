/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* Keep the ISR declaration independent of the LCD rendering headers. */
extern void lcd_port_dma_tx_isr(void);
#include "app_debug_cli.h"
#include "bsp_rs485.h"
#include "debug_uart.h"
#if defined(__CC_ARM) || defined(__arm__)
#include "bsp_system.h"
#endif
#include "servo_pulse.h"
#include "NRF24L01.h"
#include "app_config.h"
#include "bsp_hmi_uart.h"
#if APP_CAN_ENABLED
#include "bsp_can.h"
#endif
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void DMA2_Stream2_IRQHandler(void)
{
  extern DMA_HandleTypeDef hdma_usart1_rx;
  HAL_DMA_IRQHandler(&hdma_usart1_rx);
}

#if APP_CHASSIS_LCD_ENABLED
void DMA2_Stream3_IRQHandler(void)
{
  lcd_port_dma_tx_isr();
}
#endif

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern CAN_HandleTypeDef hcan1;
extern DMA_HandleTypeDef hdma_spi2_rx;
extern DMA_HandleTypeDef hdma_spi2_tx;
extern TIM_HandleTypeDef htim2;
#if APP_SERVO_PULSE_ENABLED
extern TIM_HandleTypeDef htim9;
extern TIM_HandleTypeDef htim11;
#endif
extern DMA_HandleTypeDef hdma_uart5_rx;
extern DMA_HandleTypeDef hdma_uart5_tx;
extern DMA_HandleTypeDef hdma_usart1_tx;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart2_rx;
extern DMA_HandleTypeDef hdma_usart2_tx;
extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart5;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart6;
extern TIM_HandleTypeDef htim1;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
/* Preserve ORIGINAL exception SP, then use an independent emergency stack. */
#if defined(__CC_ARM)
__declspec(noreturn) void bsp_fault_entry(uint32_t *stack,uint32_t exc_return,uint32_t kind)
{
    servo_pulse_stop_all();
    bsp_system_capture_exception(stack,exc_return,kind);
    for (;;) { /* Hardware IWDG completes the existing fault response. */ }
}
__asm void HardFault_Handler(void)
{
    IMPORT bsp_fault_entry
    IMPORT bsp_system_exception_stack
    TST lr, #4
    ITE EQ
    MRSEQ r0, MSP
    MRSNE r0, PSP
    MOV r1, lr
    MOVS r2, #1
    CPSID i
    LDR r3, =bsp_system_exception_stack
    ADD r3, r3, #512
    MSR MSP, r3
    B bsp_fault_entry
    NOP
}
__asm void MemManage_Handler(void)
{
    IMPORT bsp_fault_entry
    IMPORT bsp_system_exception_stack
    TST lr, #4
    ITE EQ
    MRSEQ r0, MSP
    MRSNE r0, PSP
    MOV r1, lr
    MOVS r2, #2
    CPSID i
    LDR r3, =bsp_system_exception_stack
    ADD r3, r3, #512
    MSR MSP, r3
    B bsp_fault_entry
    NOP
}
__asm void BusFault_Handler(void)
{
    IMPORT bsp_fault_entry
    IMPORT bsp_system_exception_stack
    TST lr, #4
    ITE EQ
    MRSEQ r0, MSP
    MRSNE r0, PSP
    MOV r1, lr
    MOVS r2, #3
    CPSID i
    LDR r3, =bsp_system_exception_stack
    ADD r3, r3, #512
    MSR MSP, r3
    B bsp_fault_entry
    NOP
}
__asm void UsageFault_Handler(void)
{
    IMPORT bsp_fault_entry
    IMPORT bsp_system_exception_stack
    TST lr, #4
    ITE EQ
    MRSEQ r0, MSP
    MRSNE r0, PSP
    MOV r1, lr
    MOVS r2, #4
    CPSID i
    LDR r3, =bsp_system_exception_stack
    ADD r3, r3, #512
    MSR MSP, r3
    B bsp_fault_entry
    NOP
}
#else
/* Native-host builds never execute Cortex fault vectors. */
void HardFault_Handler(void) { servo_pulse_stop_all();for(;;){} }
void MemManage_Handler(void) { servo_pulse_stop_all();for(;;){} }
void BusFault_Handler(void) { servo_pulse_stop_all();for(;;){} }
void UsageFault_Handler(void) { servo_pulse_stop_all();for(;;){} }
#endif


/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles DMA1 stream0 global interrupt.
  */
void DMA1_Stream0_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream0_IRQn 0 */
  bsp_hmi_uart_dma_irq_isr(0U);
  /* USER CODE END DMA1_Stream0_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_uart5_rx);
  /* USER CODE BEGIN DMA1_Stream0_IRQn 1 */

  /* USER CODE END DMA1_Stream0_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream3 global interrupt.
  */
void DMA1_Stream3_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream3_IRQn 0 */
  NRF24L01_DmaRxIsr();

  /* 专用驱动已处理事件，不再交给HAL重复消费；保留生成区以兼容CubeMX。 */
#if 0
  /* USER CODE END DMA1_Stream3_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_spi2_rx);
  /* USER CODE BEGIN DMA1_Stream3_IRQn 1 */
#endif

  /* USER CODE END DMA1_Stream3_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream4 global interrupt.
  */
void DMA1_Stream4_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream4_IRQn 0 */
  NRF24L01_DmaTxIsr();

  /* 专用驱动已处理事件，不再交给HAL重复消费；保留生成区以兼容CubeMX。 */
#if 0
  /* USER CODE END DMA1_Stream4_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_spi2_tx);
  /* USER CODE BEGIN DMA1_Stream4_IRQn 1 */
#endif

  /* USER CODE END DMA1_Stream4_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream5 global interrupt.
  */
void DMA1_Stream5_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream5_IRQn 0 */

  /* USER CODE END DMA1_Stream5_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart2_rx);
  /* USER CODE BEGIN DMA1_Stream5_IRQn 1 */

  /* USER CODE END DMA1_Stream5_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream6 global interrupt.
  */
void DMA1_Stream6_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream6_IRQn 0 */

  /* USER CODE END DMA1_Stream6_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart2_tx);
  /* USER CODE BEGIN DMA1_Stream6_IRQn 1 */

  /* USER CODE END DMA1_Stream6_IRQn 1 */
}

/**
  * @brief This function handles CAN1 TX interrupts.
  */
void CAN1_TX_IRQHandler(void)
{
  /* USER CODE BEGIN CAN1_TX_IRQn 0 */
#if APP_CAN_ENABLED
  bsp_can_tx_isr();
#endif
  /* 专用驱动已处理事件，不再交给HAL重复消费；保留生成区以兼容CubeMX。 */
#if 0
  /* USER CODE END CAN1_TX_IRQn 0 */
  HAL_CAN_IRQHandler(&hcan1);
  /* USER CODE BEGIN CAN1_TX_IRQn 1 */
#endif

  /* USER CODE END CAN1_TX_IRQn 1 */
}

/**
  * @brief This function handles CAN1 RX0 interrupts.
  */
void CAN1_RX0_IRQHandler(void)
{
  /* USER CODE BEGIN CAN1_RX0_IRQn 0 */
#if APP_CAN_ENABLED
  bsp_can_rx_isr(0U);
#endif
  /* 专用驱动已处理事件，不再交给HAL重复消费；保留生成区以兼容CubeMX。 */
#if 0
  /* USER CODE END CAN1_RX0_IRQn 0 */
  HAL_CAN_IRQHandler(&hcan1);
  /* USER CODE BEGIN CAN1_RX0_IRQn 1 */
#endif

  /* USER CODE END CAN1_RX0_IRQn 1 */
}

/**
  * @brief This function handles CAN1 RX1 interrupt.
  */
void CAN1_RX1_IRQHandler(void)
{
  /* USER CODE BEGIN CAN1_RX1_IRQn 0 */
#if APP_CAN_ENABLED
  bsp_can_rx_isr(1U);
#endif
  /* 专用驱动已处理事件，不再交给HAL重复消费；保留生成区以兼容CubeMX。 */
#if 0
  /* USER CODE END CAN1_RX1_IRQn 0 */
  HAL_CAN_IRQHandler(&hcan1);
  /* USER CODE BEGIN CAN1_RX1_IRQn 1 */
#endif

  /* USER CODE END CAN1_RX1_IRQn 1 */
}

/**
  * @brief This function handles CAN1 SCE interrupt.
  */
void CAN1_SCE_IRQHandler(void)
{
  /* USER CODE BEGIN CAN1_SCE_IRQn 0 */
#if APP_CAN_ENABLED
  bsp_can_error_isr();
#endif
  /* 专用驱动已处理事件，不再交给HAL重复消费；保留生成区以兼容CubeMX。 */
#if 0
  /* USER CODE END CAN1_SCE_IRQn 0 */
  HAL_CAN_IRQHandler(&hcan1);
  /* USER CODE BEGIN CAN1_SCE_IRQn 1 */
#endif

  /* USER CODE END CAN1_SCE_IRQn 1 */
}

/**
  * @brief This function handles TIM1 break interrupt and TIM9 global interrupt.
  */
#if APP_SERVO_PULSE_ENABLED
void TIM1_BRK_TIM9_IRQHandler(void)
{
  /* USER CODE BEGIN TIM1_BRK_TIM9_IRQn 0 */
  servo_pulse_update_tim9_isr();
  /* 专用驱动已处理事件，不再交给HAL重复消费；保留生成区以兼容CubeMX。 */
#if 0
  /* USER CODE END TIM1_BRK_TIM9_IRQn 0 */
  HAL_TIM_IRQHandler(&htim9);
  /* USER CODE BEGIN TIM1_BRK_TIM9_IRQn 1 */
#endif
  /* USER CODE END TIM1_BRK_TIM9_IRQn 1 */
}
#endif

/**
  * @brief This function handles TIM1 update interrupt and TIM10 global interrupt.
  */
void TIM1_UP_TIM10_IRQHandler(void)
{
  /* USER CODE BEGIN TIM1_UP_TIM10_IRQn 0 */

  /* USER CODE END TIM1_UP_TIM10_IRQn 0 */
  HAL_TIM_IRQHandler(&htim1);
  /* USER CODE BEGIN TIM1_UP_TIM10_IRQn 1 */

  /* USER CODE END TIM1_UP_TIM10_IRQn 1 */
}

/**
  * @brief This function handles TIM1 trigger and commutation interrupts and TIM11 global interrupt.
  */
#if APP_SERVO_PULSE_ENABLED
void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
  /* USER CODE BEGIN TIM1_TRG_COM_TIM11_IRQn 0 */
  servo_pulse_update_tim11_isr();
  /* 专用驱动已处理事件，不再交给HAL重复消费；保留生成区以兼容CubeMX。 */
#if 0
  /* USER CODE END TIM1_TRG_COM_TIM11_IRQn 0 */
  HAL_TIM_IRQHandler(&htim11);
  /* USER CODE BEGIN TIM1_TRG_COM_TIM11_IRQn 1 */
#endif
  /* USER CODE END TIM1_TRG_COM_TIM11_IRQn 1 */
}
#endif

/**
  * @brief This function handles TIM2 global interrupt.
  */
void TIM2_IRQHandler(void)
{
  /* USER CODE BEGIN TIM2_IRQn 0 */

  /* USER CODE END TIM2_IRQn 0 */
  HAL_TIM_IRQHandler(&htim2);
  /* USER CODE BEGIN TIM2_IRQn 1 */

  /* USER CODE END TIM2_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */
  uint32_t saved_sr = huart1.Instance->SR;
  if (saved_sr & (UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE))
    debug_uart_rx_error_isr(saved_sr, huart1.ErrorCode);

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles USART2 global interrupt.
  */
void USART2_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_IRQn 0 */
  uint8_t idle_event;

  rs485_uart_irq_entry_isr();
  idle_event = (uint8_t)((__HAL_UART_GET_FLAG(&huart2, UART_FLAG_IDLE) != RESET) &&
                         (__HAL_UART_GET_IT_SOURCE(&huart2, UART_IT_IDLE) != RESET));
  if (idle_event != 0U)
  {
    /* 清IDLE会同时清接收错误，必须在读DR之前保存并请求任务恢复。 */
    if ((huart2.Instance->SR &
         (UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE)) != 0U)
    {
      rs485_uart_error_isr();
    }
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);
    rs485_uart_idle_isr();
  }
  /* USER CODE END USART2_IRQn 0 */
  HAL_UART_IRQHandler(&huart2);
  /* USER CODE BEGIN USART2_IRQn 1 */

  /* USER CODE END USART2_IRQn 1 */
}

/**
  * @brief This function handles USART3 global interrupt.
  */
void USART3_IRQHandler(void)
{
  /* USER CODE BEGIN USART3_IRQn 0 */

  /* USER CODE END USART3_IRQn 0 */
  HAL_UART_IRQHandler(&huart3);
  /* USER CODE BEGIN USART3_IRQn 1 */

  /* USER CODE END USART3_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream7 global interrupt.
  */
void DMA1_Stream7_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream7_IRQn 0 */
  bsp_hmi_uart_dma_irq_isr(1U);
  /* USER CODE END DMA1_Stream7_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_uart5_tx);
  /* USER CODE BEGIN DMA1_Stream7_IRQn 1 */

  /* USER CODE END DMA1_Stream7_IRQn 1 */
}

/**
  * @brief This function handles UART4 global interrupt.
  */
void UART4_IRQHandler(void)
{
  /* USER CODE BEGIN UART4_IRQn 0 */

  /* USER CODE END UART4_IRQn 0 */
  HAL_UART_IRQHandler(&huart4);
  /* USER CODE BEGIN UART4_IRQn 1 */

  /* USER CODE END UART4_IRQn 1 */
}

/**
  * @brief This function handles UART5 global interrupt.
  */
void UART5_IRQHandler(void)
{
  /* USER CODE BEGIN UART5_IRQn 0 */
  bsp_hmi_uart_irq_entry_isr();
  /* USER CODE END UART5_IRQn 0 */
  HAL_UART_IRQHandler(&huart5);
  /* USER CODE BEGIN UART5_IRQn 1 */

  /* USER CODE END UART5_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream7 global interrupt.
  */
void DMA2_Stream7_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream7_IRQn 0 */

  /* USER CODE END DMA2_Stream7_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
  /* USER CODE BEGIN DMA2_Stream7_IRQn 1 */

  /* USER CODE END DMA2_Stream7_IRQn 1 */
}

/**
  * @brief This function handles USART6 global interrupt.
  */
void USART6_IRQHandler(void)
{
  /* USER CODE BEGIN USART6_IRQn 0 */

  /* USER CODE END USART6_IRQn 0 */
  HAL_UART_IRQHandler(&huart6);
  /* USER CODE BEGIN USART6_IRQn 1 */

  /* USER CODE END USART6_IRQn 1 */
}

/* USER CODE BEGIN 1 */
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART5)
  {
    bsp_hmi_uart_rx_event_isr(APP_HMI_DMA_RX_SIZE / 2U, HAL_UART_RXEVENT_HT);
  }
  else if (huart->Instance == USART2)
  {
    rs485_uart_rx_half_isr();
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART5)
  {
    bsp_hmi_uart_rx_event_isr(0U, HAL_UART_RXEVENT_TC);
  }
  else if (huart->Instance == USART2)
  {
    rs485_uart_rx_complete_isr();
  }
  else if (huart->Instance == USART1)
  {
    debug_uart_rx_event_isr(0U, HAL_UART_RXEVENT_TC);
  }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t position)
{
  if (huart->Instance == UART5)
    bsp_hmi_uart_rx_event_isr(position, HAL_UARTEx_GetRxEventType(huart));
  else if (huart->Instance == USART1)
    debug_uart_rx_event_isr(position, HAL_UARTEx_GetRxEventType(huart));
}

void HAL_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART5) bsp_hmi_uart_rx_abort_isr();
  else if (huart->Instance == USART1) debug_uart_rx_abort_isr();
}

void HAL_UART_AbortTransmitCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART5) bsp_hmi_uart_tx_abort_isr();
  else if (huart->Instance == USART1) debug_uart_tx_abort_isr();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    debug_uart_tx_complete_isr();
  }
  else if (huart->Instance == USART2)
  {
    rs485_uart_tx_complete_isr();
  }
  else if (huart->Instance == UART5)
  {
    bsp_hmi_uart_tx_complete_isr();
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    rs485_uart_error_isr();
  }
  else if (huart->Instance == USART1)
  {
    debug_uart_rx_error_isr(huart->Instance->SR, huart->ErrorCode);
    debug_uart_tx_error_isr();
  }
  else if (huart->Instance == UART5)
  {
    bsp_hmi_uart_error_isr();
  }
}

/* USER CODE END 1 */
