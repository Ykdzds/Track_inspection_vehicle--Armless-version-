#ifndef __LED_H
#define __LED_H

#include "main.h"  

#define RGB_RED       0
#define RGB_GREEN     1
#define RGB_BLUE      2
#define LED_D12       3
#define LED_D13       4

#define LED_ON        0
#define LED_OFF       1

void set_led(uint8_t led_label,uint8_t level);
void set_led_tog(uint8_t led_label);

#endif
