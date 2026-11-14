#include "bsp_can_identity.h"
#include "can_protocol.h"
#include "bsp_system.h"
#include "main.h"

#define FLASH_ERROR_BITS (FLASH_SR_OPERR | FLASH_SR_WRPERR | \
                          FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)

static bool program_word(uint32_t address, uint32_t word)
{
    uint32_t polls, started;
    if ((FLASH->SR & FLASH_SR_BSY) != 0U ||
        *(volatile uint32_t *)address != 0xFFFFFFFFUL) return false;
    FLASH->SR = FLASH_ERROR_BITS | FLASH_SR_EOP;
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SER |
                 FLASH_CR_MER | FLASH_CR_SNB)) | FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    *(volatile uint32_t *)address = word;
    started = HAL_GetTick();
    /* 启动阶段有限等待单个字编程；同时限制循环数，避免Tick异常无限等待。 */
    for (polls = 0U; polls < 300000U; ++polls)
    {
        if ((FLASH->SR & FLASH_SR_BSY) == 0U) break;
        if ((uint32_t)(HAL_GetTick() - started) >= 2U) break;
    }
    if ((FLASH->SR & FLASH_SR_BSY) != 0U) return false;
    FLASH->CR &= ~FLASH_CR_PG;
    return (FLASH->SR & FLASH_ERROR_BITS) == 0U &&
           *(volatile uint32_t *)address == word;
}

bool bsp_can_identity_startup(uint32_t fingerprint, can_identity_t *identity)
{
    uint8_t slot[64];
    uint32_t offset, address;
    bool success = true;
    if (can_identity_prepare((const uint8_t *)BSP_CAN_IDENTITY_ADDRESS,
        BSP_CAN_IDENTITY_BYTES, fingerprint, identity, slot) != CAN_IDENTITY_OK)
        return false;
    if ((FLASH->SR & FLASH_SR_BSY) != 0U)
        success = false;
    else
    {
        FLASH->KEYR = 0x45670123UL;
        FLASH->KEYR = 0xCDEF89ABUL;
        if ((FLASH->CR & FLASH_CR_LOCK) != 0U) success = false;
    }
    address = BSP_CAN_IDENTITY_ADDRESS + identity->slot * 64U;
    /* 提交标记位于最后一个字；任何失败都不能发布身份或复用该槽编号。 */
    for (offset = 0U; success && offset < 64U; offset += 4U)
    {
        success = program_word(address + offset, can_protocol_read_u32(slot + offset));
        bsp_system_watchdog_refresh();
    }
    if ((FLASH->SR & FLASH_SR_BSY) == 0U)
    {
        FLASH->CR &= ~FLASH_CR_PG;
        FLASH->CR |= FLASH_CR_LOCK;
    }
    if (!success) identity->result = CAN_IDENTITY_WRITE_FAILED;
    return success;
}
