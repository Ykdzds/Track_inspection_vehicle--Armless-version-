#ifndef __KEY_H
#define __KEY_H

#include "main.h"      

/* 停车键使用物理K5，协议位编号不变。 */
#define REMOTE_STOP_AUTH_KEY_INDEX (4U)
#define REMOTE_STOP_AUTH_KEY_MASK (1U << REMOTE_STOP_AUTH_KEY_INDEX)

#define DBL_CLICK_TIMEOUT  30   // 双击窗口超时 (x10ms)，25 = 250ms

typedef struct
{
    uint8_t key_judge;
    uint8_t key_sta;
    uint8_t key_times;
    uint8_t key_flag;
    uint8_t long_flag;
    uint8_t affirm_longflag;
    uint8_t double_flag;    // 双击事件标志
    uint8_t double_state;   // 双击状态机：0=空闲，1=等待第二次点击
    uint8_t double_times;  // 双击窗口计数器
}KEY;

extern KEY keys[];

/* Atomic cumulative input snapshot; bit0/1 are K1/K2. */
void key_get_speed_input(uint8_t *held, uint16_t steps[2], uint16_t *k9_clicks);
uint16_t key_get_held_mask(void);
void key_scan(void);
void key_handle(void);
void key_get_and_clear_events(uint16_t *short_events,
                              uint16_t *long_events,
                              uint16_t *double_events);

#endif
