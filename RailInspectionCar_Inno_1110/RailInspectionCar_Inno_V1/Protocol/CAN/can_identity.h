#ifndef CAN_IDENTITY_H
#define CAN_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

#define CAN_IDENTITY_SLOT_BYTES 64U
#define CAN_IDENTITY_BLOCK_TOKENS 32U
#define CAN_IDENTITY_COMMIT 0x43414E31UL

typedef enum
{
    CAN_IDENTITY_OK,
    CAN_IDENTITY_CORRUPT,
    CAN_IDENTITY_FULL,
    CAN_IDENTITY_EXHAUSTED,
    CAN_IDENTITY_WRITE_FAILED
} can_identity_result_t;

typedef struct
{
    uint32_t slot, boot_id, fingerprint, revision;
    uint16_t first_token, last_token;
    uint16_t motion_first, motion_last;
    can_identity_result_t result;
} can_identity_t;

/* 扫描只读日志；任何非空但未提交的槽都消耗其固定编号块。 */
can_identity_result_t can_identity_prepare(const uint8_t *area, uint32_t bytes,
    uint32_t fingerprint, can_identity_t *identity, uint8_t slot[64]);

#endif
