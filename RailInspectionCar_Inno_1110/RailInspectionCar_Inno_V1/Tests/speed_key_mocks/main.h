#include "../mocks/main.h"
#define K1_GPIO_Port ((void *)1)
#define K2_GPIO_Port ((void *)1)
#define K3_GPIO_Port ((void *)1)
#define K4_GPIO_Port ((void *)1)
#define K5_GPIO_Port ((void *)1)
#define K6_GPIO_Port ((void *)1)
#define K7_GPIO_Port ((void *)1)
#define K8_GPIO_Port ((void *)1)
#define K9_GPIO_Port ((void *)1)
#define K10_GPIO_Port ((void *)1)
#define K11_GPIO_Port ((void *)1)
#define K12_GPIO_Port ((void *)1)
#define K1_Pin 0U
#define K2_Pin 1U
#define K3_Pin 2U
#define K4_Pin 3U
#define K5_Pin 4U
#define K6_Pin 5U
#define K7_Pin 6U
#define K8_Pin 7U
#define K9_Pin 8U
#define K10_Pin 9U
#define K11_Pin 10U
#define K12_Pin 11U
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);
