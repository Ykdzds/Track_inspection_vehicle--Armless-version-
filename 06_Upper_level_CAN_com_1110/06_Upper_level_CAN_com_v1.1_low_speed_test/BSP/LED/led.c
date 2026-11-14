#include "led.h"  

void set_led(uint8_t led_label,uint8_t level)
{
    switch(led_label)
    {
        case LED1:
        {
            level ? HAL_GPIO_WritePin(LED1_GPIO_Port,LED1_Pin,GPIO_PIN_SET)
                  : HAL_GPIO_WritePin(LED1_GPIO_Port,LED1_Pin,GPIO_PIN_RESET);
        }
        break;
        case LED2:
        {
            level ? HAL_GPIO_WritePin(LED2_GPIO_Port,LED2_Pin,GPIO_PIN_SET)
                  : HAL_GPIO_WritePin(LED2_GPIO_Port,LED2_Pin,GPIO_PIN_RESET);
        }
        break;
        case LED3:
        {
            level ? HAL_GPIO_WritePin(LED3_GPIO_Port,LED3_Pin,GPIO_PIN_SET)
                  : HAL_GPIO_WritePin(LED3_GPIO_Port,LED3_Pin,GPIO_PIN_RESET);
        }
        break;
        case LED4:
        {
            level ? HAL_GPIO_WritePin(LED4_GPIO_Port,LED4_Pin,GPIO_PIN_SET)
                  : HAL_GPIO_WritePin(LED4_GPIO_Port,LED4_Pin,GPIO_PIN_RESET);
        }
        break;
        default:break;        
    }       
}

void set_led_tog(uint8_t led_label)
{
    switch(led_label)
    {
        case LED1:
        {
            if(HAL_GPIO_ReadPin(LED1_GPIO_Port,LED1_Pin) == RESET)
                HAL_GPIO_WritePin(LED1_GPIO_Port,LED1_Pin,GPIO_PIN_SET);
            else HAL_GPIO_WritePin(LED1_GPIO_Port,LED1_Pin,GPIO_PIN_RESET);
        }
        break;
        case LED2:
        {
            if(HAL_GPIO_ReadPin(LED2_GPIO_Port,LED2_Pin) == RESET)
                HAL_GPIO_WritePin(LED2_GPIO_Port,LED2_Pin,GPIO_PIN_SET);
            else HAL_GPIO_WritePin(LED2_GPIO_Port,LED2_Pin,GPIO_PIN_RESET);
        }
        break;
        case LED3:
        {
            if(HAL_GPIO_ReadPin(LED3_GPIO_Port,LED3_Pin) == RESET)
                HAL_GPIO_WritePin(LED3_GPIO_Port,LED3_Pin,GPIO_PIN_SET);
            else HAL_GPIO_WritePin(LED3_GPIO_Port,LED3_Pin,GPIO_PIN_RESET);
        }
        break;
        case LED4:
        {
            if(HAL_GPIO_ReadPin(LED4_GPIO_Port,LED4_Pin) == RESET)
                HAL_GPIO_WritePin(LED4_GPIO_Port,LED4_Pin,GPIO_PIN_SET);
            else HAL_GPIO_WritePin(LED4_GPIO_Port,LED4_Pin,GPIO_PIN_RESET);
        }
        break;
        default:break;
    }       
}
