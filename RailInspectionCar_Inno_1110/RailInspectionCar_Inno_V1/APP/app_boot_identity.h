#ifndef APP_BOOT_IDENTITY_H
#define APP_BOOT_IDENTITY_H

#include "can_identity.h"

/* 同一次启动只追加一次；CAN关闭时HMI仍使用启动日志。 */
void app_boot_identity_init(void);
const can_identity_t *app_boot_identity_get(void);
uint32_t app_boot_identity_id(void);
bool app_boot_identity_valid(void);

#endif
