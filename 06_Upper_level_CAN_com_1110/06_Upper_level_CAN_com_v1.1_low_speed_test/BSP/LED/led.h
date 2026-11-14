#ifndef __LED_H
#define __LED_H

#include "main.h"  

#define LED1       1
#define LED2       2
#define LED3       3
#define LED4       4

#define LED_ON        0
#define LED_OFF       1

void set_led(uint8_t led_label,uint8_t level);
void set_led_tog(uint8_t led_label);

#endif
