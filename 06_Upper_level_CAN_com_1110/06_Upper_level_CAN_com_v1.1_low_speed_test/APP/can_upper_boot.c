#include "can_upper_boot.h"
#include "can_upper_protocol.h"
#include "main.h"
#include "iwdg.h"

#define BOOT_PAGE_FIRST 0x0800F800UL
#define BOOT_PAGE_SECOND 0x0800FC00UL
#define BOOT_PAGE_SIZE 1024UL
#define BOOT_MAGIC 0x55504231UL
#define BOOT_COMMIT 0x424F4F54UL

typedef struct
{
    uint32_t magic;
    uint32_t count;
    uint32_t crc;
    uint32_t commit;
} BootRecord_t;

static uint8_t EmptySlot(uint32_t address)
{
    const BootRecord_t *record = (const BootRecord_t *)address;
    return (uint8_t)(record->magic == 0xFFFFFFFFUL && record->count == 0xFFFFFFFFUL &&
                     record->crc == 0xFFFFFFFFUL && record->commit == 0xFFFFFFFFUL);
}

uint32_t UpperBoot_Allocate(void)
{
    uint32_t page, address, latest_address = 0U, latest_count = 0U;
    uint32_t target = 0U, erase_page, page_error, next_count, boot_id;
    uint32_t device_hash, uid[3];
    uint8_t dirty = 0U;
    BootRecord_t record;
    FLASH_EraseInitTypeDef erase = {0};
    const BootRecord_t *stored;
    HAL_StatusTypeDef result = HAL_OK;
    uid[0] = *(const uint32_t *)0x1FFFF7E8UL;
    uid[1] = *(const uint32_t *)0x1FFFF7ECUL;
    uid[2] = *(const uint32_t *)0x1FFFF7F0UL;
    device_hash = Upper_Crc32((const uint8_t *)uid, sizeof(uid));
    for (page = BOOT_PAGE_FIRST; page <= BOOT_PAGE_SECOND; page += BOOT_PAGE_SIZE)
    {
        for (address = page; address < page + BOOT_PAGE_SIZE; address += sizeof(BootRecord_t))
        {
            stored = (const BootRecord_t *)address;
            if (EmptySlot(address)) continue;
            dirty = 1U;
            if (stored->magic != BOOT_MAGIC || stored->commit != BOOT_COMMIT || stored->count == 0U ||
                stored->crc != Upper_Crc32((const uint8_t *)stored, 8U)) continue;
            if (stored->count > latest_count)
            {
                latest_count = stored->count;
                latest_address = address;
            }
        }
    }
    if ((dirty && latest_address == 0U) || latest_count == 0xFFFFFFFFUL) return 0U;
    next_count = latest_count + 1U;
    boot_id = next_count ^ device_hash;
    if (boot_id == 0U)
    {
        if (next_count == 0xFFFFFFFFUL) return 0U;
        next_count++;
        boot_id = next_count ^ device_hash;
    }
    page = latest_address ? (latest_address & ~(BOOT_PAGE_SIZE - 1U)) : BOOT_PAGE_FIRST;
    for (address = page; address < page + BOOT_PAGE_SIZE; address += sizeof(BootRecord_t))
    {
        if (EmptySlot(address)) { target = address; break; }
    }
    HAL_IWDG_Refresh(&hiwdg);
    if (HAL_FLASH_Unlock() != HAL_OK) return 0U;
    if (!target)
    {
        erase_page = page == BOOT_PAGE_FIRST ? BOOT_PAGE_SECOND : BOOT_PAGE_FIRST;
        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        erase.PageAddress = erase_page;
        erase.NbPages = 1U;
        result = HAL_FLASHEx_Erase(&erase, &page_error);
        target = erase_page;
    }
    record.magic = BOOT_MAGIC;
    record.count = next_count;
    record.crc = Upper_Crc32((const uint8_t *)&record, 8U);
    record.commit = BOOT_COMMIT;
    for (address = 0U; address < sizeof(record) && result == HAL_OK; address += 4U)
        result = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, target + address, *(const uint32_t *)((const uint8_t *)&record + address));
    HAL_FLASH_Lock();
    HAL_IWDG_Refresh(&hiwdg);
    if (result != HAL_OK || memcmp((const void *)target, &record, sizeof(record)) != 0) return 0U;
    return boot_id;
}
