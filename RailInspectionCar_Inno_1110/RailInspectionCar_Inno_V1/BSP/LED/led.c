#include "led.h"  

void set_led(uint8_t led_label,uint8_t level)
{
    switch(led_label)
    {
        case RGB_RED:
        {
            level ? HAL_GPIO_WritePin(RED_GPIO_Port,RED_Pin,GPIO_PIN_SET)
                  : HAL_GPIO_WritePin(RED_GPIO_Port,RED_Pin,GPIO_PIN_RESET);
        }
        break;
        case RGB_GREEN:
        {
            level ? HAL_GPIO_WritePin(GREEN_GPIO_Port,GREEN_Pin,GPIO_PIN_SET)
                  : HAL_GPIO_WritePin(GREEN_GPIO_Port,GREEN_Pin,GPIO_PIN_RESET);
        }
        break;
        case RGB_BLUE:
        {
            level ? HAL_GPIO_WritePin(BLUE_GPIO_Port,BLUE_Pin,GPIO_PIN_SET)
                  : HAL_GPIO_WritePin(BLUE_GPIO_Port,BLUE_Pin,GPIO_PIN_RESET);
        }
        break;
        case LED_D12:
        {
            level ? HAL_GPIO_WritePin(GPIOD,GPIO_PIN_12,GPIO_PIN_SET)
                  : HAL_GPIO_WritePin(GPIOD,GPIO_PIN_12,GPIO_PIN_RESET);
        }
        break;
        case LED_D13:
        {
            level ? HAL_GPIO_WritePin(GPIOD,GPIO_PIN_13,GPIO_PIN_SET)
                  : HAL_GPIO_WritePin(GPIOD,GPIO_PIN_13,GPIO_PIN_RESET);
        }
        break;
    }       
}

void set_led_tog(uint8_t led_label)
{
    switch(led_label)
    {
        case RGB_RED:
        {
            if(HAL_GPIO_ReadPin(RED_GPIO_Port,RED_Pin) == RESET)
                HAL_GPIO_WritePin(RED_GPIO_Port,RED_Pin,GPIO_PIN_SET);
            else HAL_GPIO_WritePin(RED_GPIO_Port,RED_Pin,GPIO_PIN_RESET);
        }
        break;
        case RGB_GREEN:
        {
            if(HAL_GPIO_ReadPin(GREEN_GPIO_Port,GREEN_Pin) == RESET)
                HAL_GPIO_WritePin(GREEN_GPIO_Port,GREEN_Pin,GPIO_PIN_SET);
            else HAL_GPIO_WritePin(GREEN_GPIO_Port,GREEN_Pin,GPIO_PIN_RESET);
        }
        break;
        case RGB_BLUE:
        {
            if(HAL_GPIO_ReadPin(BLUE_GPIO_Port,BLUE_Pin) == RESET)
                HAL_GPIO_WritePin(BLUE_GPIO_Port,BLUE_Pin,GPIO_PIN_SET);
            else HAL_GPIO_WritePin(BLUE_GPIO_Port,BLUE_Pin,GPIO_PIN_RESET);
        }
        break;
        case LED_D12:
        {
            if(HAL_GPIO_ReadPin(GPIOD,GPIO_PIN_12) == RESET)
                HAL_GPIO_WritePin(GPIOD,GPIO_PIN_12,GPIO_PIN_SET);
            else HAL_GPIO_WritePin(GPIOD,GPIO_PIN_12,GPIO_PIN_RESET);
        }
        break;
        case LED_D13:
        {
            if(HAL_GPIO_ReadPin(GPIOD,GPIO_PIN_13) == RESET)
                HAL_GPIO_WritePin(GPIOD,GPIO_PIN_13,GPIO_PIN_SET);
            else HAL_GPIO_WritePin(GPIOD,GPIO_PIN_13,GPIO_PIN_RESET);
        }
        break;
    }       
}
