#include "tb6612.h"

/*
*设置左边电机的PWM
*/
void PWM_SetCCR_L(uint16_t Compare)
{
	__HAL_TIM_SetCompare(&htim2,TIM_CHANNEL_1,Compare);
}

/*
*设置右边电机的PWM
*/
void PWM_SetCCR_R(uint16_t Compare)
{
	__HAL_TIM_SetCompare(&htim2,TIM_CHANNEL_2,Compare);
}

void MotorSpeed(uint8_t Motor_Channel,int16_t Speed)
{        
    switch(Motor_Channel)
    {
        case 0://左边电机
        {
            if (Speed >= 0)
            {
                HAL_GPIO_WritePin(AIN1_GPIO_Port,AIN1_Pin,GPIO_PIN_SET);
                HAL_GPIO_WritePin(AIN2_GPIO_Port,AIN2_Pin,GPIO_PIN_RESET);         
                PWM_SetCCR_L(Speed);
            }
            else
            {
                HAL_GPIO_WritePin(AIN1_GPIO_Port,AIN1_Pin,GPIO_PIN_RESET);
                HAL_GPIO_WritePin(AIN2_GPIO_Port,AIN2_Pin,GPIO_PIN_SET);        
                PWM_SetCCR_L(-Speed);
            }
        }
        break;
        case 1://右边电机
        {
            if (Speed >= 0)
            {
                HAL_GPIO_WritePin(BIN1_GPIO_Port,BIN1_Pin,GPIO_PIN_SET);
                HAL_GPIO_WritePin(BIN2_GPIO_Port,BIN2_Pin,GPIO_PIN_RESET); 
                PWM_SetCCR_R(Speed);
            }
            else
            {
                HAL_GPIO_WritePin(BIN1_GPIO_Port,BIN1_Pin,GPIO_PIN_SET);
                HAL_GPIO_WritePin(BIN2_GPIO_Port,BIN2_Pin,GPIO_PIN_RESET);    
                PWM_SetCCR_R(-Speed);
            }
        }
        break;             
        default:break;
    }
}

void MotorAll_Stop(void)
{
    MotorSpeed(MOTOR_L,0);
    MotorSpeed(MOTOR_R,0);  
}

void Motors_LSpeed(int16_t Speed)
{
    MotorSpeed(MOTOR_L,Speed);
}

void Motors_RSpeed(int16_t Speed)
{
    MotorSpeed(MOTOR_R,Speed); 
}
