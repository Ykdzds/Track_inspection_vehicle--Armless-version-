#include "beep.h"  

void set_beep(uint8_t level)
{
    level ? HAL_GPIO_WritePin(GPIOD,GPIO_PIN_14,GPIO_PIN_SET)
         : HAL_GPIO_WritePin(GPIOD,GPIO_PIN_14,GPIO_PIN_RESET);
}
