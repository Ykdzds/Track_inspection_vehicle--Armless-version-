#ifndef APP_HMI_ARM3_H
#define APP_HMI_ARM3_H
#include <stdint.h>
#include <stdbool.h>
void app_hmi_arm3_init(uint32_t boot);
void app_hmi_arm3_service(uint32_t now_ms);
/* CAN task only: origin shared by the six cached display windows. */
uint32_t app_hmi_arm3_snapshot_time(void);
bool app_hmi_arm3_write(uint16_t start,uint16_t count,const uint16_t *words,uint32_t now_ms);
bool app_hmi_arm3_read(uint16_t start,uint16_t count,uint16_t *words,uint32_t now_ms);
#endif
