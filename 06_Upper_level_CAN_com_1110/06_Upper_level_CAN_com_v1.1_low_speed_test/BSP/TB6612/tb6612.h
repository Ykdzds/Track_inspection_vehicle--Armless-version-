#ifndef __TB6612_H
#define __TB6612_H

#include "main.h"
#include "tim.h"

/*
*       0ºÅ        1ºÅ
*/

#define MOTOR_L  0
#define MOTOR_R  1

void MotorSpeed(uint8_t Motor_Channel,int16_t Speed);
void MotorAll_Stop(void);
void Motors_LSpeed(int16_t Speed);
void Motors_RSpeed(int16_t Speed);

#endif
