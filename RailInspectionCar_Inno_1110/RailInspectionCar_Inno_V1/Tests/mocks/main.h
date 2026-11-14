#ifndef TEST_MAIN_H
#define TEST_MAIN_H
#include <stdint.h>
typedef struct {
    uint32_t BSRR, ODR, MODER, OTYPER, PUPDR;
} GPIO_TypeDef;
extern GPIO_TypeDef test_gpio_b, test_gpio_c, test_gpio_e;
#define GPIOB (&test_gpio_b)
#define GPIOC (&test_gpio_c)
#define GPIOE (&test_gpio_e)
#define GPIO_PIN_0 1U
#define GPIO_PIN_1 2U
#define GPIO_PIN_5 32U
#define GPIO_PIN_8 256U
#define GPIO_PIN_9 512U
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct {
    uint32_t Pin, Mode, Pull, Speed, Alternate;
} GPIO_InitTypeDef;
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_AF3_TIM9 3U
#define GPIO_AF3_TIM11 3U
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
uint32_t HAL_GetTick(void);
#endif
