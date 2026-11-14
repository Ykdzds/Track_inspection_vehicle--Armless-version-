#ifndef __KEY_H
#define __KEY_H

#include "main.h"      

typedef struct
{
    uint8_t key_judge;
    uint8_t key_sta;
    uint8_t key_times;
    uint8_t key_flag;
    uint8_t long_flag;
    uint8_t affirm_longflag;
}KEY;

extern KEY keys[];

void key_scan(void);
void key_handle(void);

#endif
