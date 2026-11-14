#ifndef OLED_TEST_MAIN_H
#define OLED_TEST_MAIN_H
#include "../mocks/main.h"
#undef OLED_6X8
#include "OLED.h"
typedef enum { HAL_I2C_STATE_RESET, HAL_I2C_STATE_READY, HAL_I2C_STATE_BUSY_TX } HAL_I2C_StateTypeDef;
typedef struct { HAL_I2C_StateTypeDef State; uint32_t ErrorCode; } I2C_HandleTypeDef;
#define GPIOB ((void *)2)
#define GPIO_PIN_8 0x100U
#define GPIO_PIN_9 0x200U
#define RESET 0U
#define I2C_FLAG_BUSY 1U
#define I2C_MEMADD_SIZE_8BIT 1U
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin);
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *i2c);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c);
uint32_t mock_i2c_flag(void);
void mock_i2c_reset(uint8_t force);
#define __HAL_I2C_GET_FLAG(i2c, flag) mock_i2c_flag()
#define __HAL_RCC_I2C1_FORCE_RESET() mock_i2c_reset(1U)
#define __HAL_RCC_I2C1_RELEASE_RESET() mock_i2c_reset(0U)
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *i2c, uint16_t address,
    uint16_t control, uint16_t address_size, uint8_t *data, uint16_t size, uint32_t timeout);
#endif
