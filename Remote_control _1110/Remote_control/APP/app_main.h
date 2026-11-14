#ifndef __APP_MAIN_H
#define __APP_MAIN_H

#include "main.h"      
#include "tim.h"
#include "adc.h"
#include "iwdg.h"

void app_init(void);
void main_loop_task(void);
void state_display(void);
void iwdg_feeding(void);
void remote_radio_task(void);

#endif
