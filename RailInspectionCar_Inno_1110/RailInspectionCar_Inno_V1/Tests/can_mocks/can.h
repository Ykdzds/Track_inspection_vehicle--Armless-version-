#ifndef TEST_CAN_H
#define TEST_CAN_H
#include "stm32f407xx.h"
#undef CAN1
extern CAN_TypeDef test_can_registers;
#define CAN1 (&test_can_registers)
uint32_t HAL_GetTick(void);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);
void HAL_NVIC_DisableIRQ(IRQn_Type irq);
#endif
