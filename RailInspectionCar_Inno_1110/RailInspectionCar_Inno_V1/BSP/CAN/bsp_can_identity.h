#ifndef BSP_CAN_IDENTITY_H
#define BSP_CAN_IDENTITY_H

#include "can_identity.h"

#define BSP_CAN_IDENTITY_ADDRESS 0x080E0000UL
#define BSP_CAN_IDENTITY_BYTES 0x20000UL

/* 仅允许在创建任务之前调用，不提供运行期擦除或重新编号接口。 */
bool bsp_can_identity_startup(uint32_t fingerprint, can_identity_t *identity);

#endif
