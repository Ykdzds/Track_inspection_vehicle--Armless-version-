#ifndef TEST_BOARD_KEY_MAIN_H
#define TEST_BOARD_KEY_MAIN_H
#include "../../Tests/mocks/main.h"
#include "task.h"
extern GPIO_TypeDef test_gpio_d;
extern GPIO_TypeDef test_gpio_c;
#define GPIOD (&test_gpio_d)
#define GPIOC (&test_gpio_c)
#define GPIO_PIN_10 (1U << 10U)
#define GPIO_PIN_11 (1U << 11U)
#define GPIO_PIN_13 (1U << 13U)
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
#endif
