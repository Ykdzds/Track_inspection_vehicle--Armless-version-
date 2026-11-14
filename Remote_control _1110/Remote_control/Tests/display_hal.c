#include "i2c.h"
#include <assert.h>
I2C_HandleTypeDef hi2c1 = {HAL_I2C_STATE_READY,0U};
uint32_t HAL_GetTick(void){return 0U;}
void HAL_Delay(uint32_t ms){(void)ms;}
GPIO_PinState HAL_GPIO_ReadPin(void *p,uint16_t pin){(void)p;(void)pin;return GPIO_PIN_SET;}
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *i){return i->State;}
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i){return i->ErrorCode;}
uint32_t mock_i2c_flag(void){return 0U;}
void mock_i2c_reset(uint8_t f){(void)f;}
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *i){i->State=HAL_I2C_STATE_READY;return HAL_OK;}
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *i){i->State=HAL_I2C_STATE_RESET;return HAL_OK;}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *i,uint16_t a,uint16_t c,uint16_t z,uint8_t *d,uint16_t n,uint32_t t){(void)i;(void)a;(void)c;(void)z;(void)d;(void)n;(void)t;return HAL_OK;}
