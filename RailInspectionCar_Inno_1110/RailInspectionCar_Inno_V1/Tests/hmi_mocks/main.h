#ifndef TEST_HMI_MAIN_H
#define TEST_HMI_MAIN_H
#include "../can_mocks/main.h"
uint32_t __get_PRIMASK(void);
uint32_t __get_IPSR(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
#endif
