#ifndef REMOTE_ARM3_H
#define REMOTE_ARM3_H
#include <stdint.h>
#include "remote_display.h"
void remote_arm3_init(void);
void remote_arm3_keys(uint16_t short_keys,uint16_t long_keys,uint16_t double_keys,uint16_t held,uint16_t k9_clicks,uint32_t now);
uint8_t remote_arm3_ack(const uint8_t *data,uint8_t length,uint32_t now);
uint8_t remote_arm3_active(void);
uint8_t remote_arm3_frame(uint8_t out[32],uint8_t frame_seq,uint32_t now);
void remote_arm3_display(remote_display_t *display,uint32_t now);
#endif
