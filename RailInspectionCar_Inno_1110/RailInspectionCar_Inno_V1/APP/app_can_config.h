#ifndef APP_CAN_CONFIG_H
#define APP_CAN_CONFIG_H

#include <stdint.h>

void app_can_config_encode(uint8_t payload[80], uint32_t revision);
uint32_t app_can_config_fingerprint(void);
uint32_t app_can_capabilities(void);
uint32_t app_can_data_sources(void);

#endif
