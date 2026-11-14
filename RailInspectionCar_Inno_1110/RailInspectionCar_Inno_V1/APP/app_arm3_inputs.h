#ifndef APP_ARM3_INPUTS_H
#define APP_ARM3_INPUTS_H
#include <stdint.h>
#include <stdbool.h>
void app_arm3_inputs_init(uint32_t lower_boot);
void app_arm3_inputs_service(uint32_t now_ms);
/* True means recognized ARM frame (including a rejected malformed frame). */
bool app_arm3_remote_receive(const uint8_t *payload,uint8_t length,uint32_t now_ms);
bool app_arm3_remote_ack(uint8_t type,uint8_t out[32],uint32_t now_ms);
bool app_arm3_remote_active(void);
#endif
