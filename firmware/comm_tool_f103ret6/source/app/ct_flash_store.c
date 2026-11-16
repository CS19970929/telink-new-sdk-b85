#include "ct_flash_store.h"
#include "ct_config.h"
#include "ct_crc16.h"
#include "ct_watchdog.h"
#include "stm32f10x_flash.h"
#include <string.h>

#define CT_FW_MAGIC                    0x43544657u
#define CT_FW_VALID                    0xA55A5AA5u

static CtFirmwareInfo s_fw_info;

static uint32_t align_up_page(uint32_t value)
{
    return (value + CT_SELF_FLASH_PAGE_SIZE - 1u) & ~(CT_SELF_FLASH_PAGE_SIZE - 1u);
}

static int range_in_cache(uint32_t offset, uint32_t length)
{
    if (offset > CT_FW_CACHE_SIZE)
    {
        return 0;
    }
    if (length > (CT_FW_CACHE_SIZE - offset))
    {
        return 0;
    }
    return 1;
}

static int app_addr_supported(uint32_t app_addr, uint32_t size)
{
    if ((app_addr == CT_BMS_APP_BASE_ADDR) &&
        (size <= (CT_BMS_APP_LIMIT_ADDR - CT_BMS_APP_BASE_ADDR)))
    {
        return 1;
    }

    if ((app_addr == CT_SELF_APP_BASE) &&
        (size <= CT_SELF_APP_SIZE))
    {
        return 1;
    }

    return 0;
}

static int erase_pages(uint32_t start, uint32_t length)
{
    uint32_t addr;
    uint32_t end;
    FLASH_Status status;

    end = align_up_page(start + length);
    FLASH_Unlock();
    for (addr = start; addr < end; addr += CT_SELF_FLASH_PAGE_SIZE)
    {
        CtWatchdog_Feed();
        status = FLASH_ErasePage(addr);
        CtWatchdog_Feed();
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return 0;
        }
    }
    FLASH_Lock();
    return 1;
}

static int program_bytes(uint32_t addr, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint16_t halfword;
    FLASH_Status status;

    if (data == 0)
    {
        return 0;
    }

    FLASH_Unlock();
    for (i = 0u; i < length; i += 2u)
    {
        halfword = data[i];
        if ((i + 1u) < length)
        {
            halfword |= (uint16_t)data[i + 1u] << 8;
        }
        else
        {
            halfword |= 0xFF00u;
        }

        status = FLASH_ProgramHalfWord(addr + i, halfword);
        CtWatchdog_Feed();
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return 0;
        }
        if (*(volatile uint16_t *)(addr + i) != halfword)
        {
            FLASH_Lock();
            return 0;
        }
    }
    FLASH_Lock();
    return 1;
}

static int save_info(const CtFirmwareInfo *info)
{
    if (!erase_pages(CT_FW_META_PAGE, CT_SELF_FLASH_PAGE_SIZE))
    {
        return 0;
    }
    return program_bytes(CT_FW_META_PAGE, (const uint8_t *)info, (uint32_t)sizeof(*info));
}

void CtFlash_Init(void)
{
    const CtFirmwareInfo *stored;

    stored = (const CtFirmwareInfo *)CT_FW_META_PAGE;
    if ((stored->magic == CT_FW_MAGIC) && (stored->valid == CT_FW_VALID))
    {
        memcpy(&s_fw_info, stored, sizeof(s_fw_info));
    }
    else
    {
        memset(&s_fw_info, 0, sizeof(s_fw_info));
    }
}

const CtFirmwareInfo *CtFlash_GetInfo(void)
{
    return &s_fw_info;
}

int CtFlash_Begin(uint32_t app_addr, uint32_t size, uint16_t crc16, uint32_t crc32)
{
    uint32_t erase_len;

    if ((size == 0u) || (size > CT_FW_CACHE_SIZE) || !app_addr_supported(app_addr, size))
    {
        return 0;
    }

    erase_len = align_up_page(size);
    if (!erase_pages(CT_FW_CACHE_BASE, erase_len))
    {
        return 0;
    }
    if (!erase_pages(CT_FW_META_PAGE, CT_SELF_FLASH_PAGE_SIZE))
    {
        return 0;
    }

    memset(&s_fw_info, 0, sizeof(s_fw_info));
    s_fw_info.magic = CT_FW_MAGIC;
    s_fw_info.app_addr = app_addr;
    s_fw_info.size = size;
    s_fw_info.crc16 = crc16;
    s_fw_info.crc32 = crc32;
    return 1;
}

int CtFlash_Write(uint32_t offset, const uint8_t *data, uint16_t length)
{
    if ((length == 0u) || !range_in_cache(offset, length))
    {
        return 0;
    }
    return program_bytes(CT_FW_CACHE_BASE + offset, data, length);
}

int CtFlash_End(uint32_t size, uint16_t crc16, uint32_t crc32)
{
    uint16_t actual_crc;

    if ((s_fw_info.magic != CT_FW_MAGIC) || (s_fw_info.size != size) ||
        (s_fw_info.crc16 != crc16) || (s_fw_info.crc32 != crc32))
    {
        return 0;
    }

    actual_crc = CtCrc16_Calc((const uint8_t *)CT_FW_CACHE_BASE, size);
    if (actual_crc != crc16)
    {
        return 0;
    }

    s_fw_info.valid = CT_FW_VALID;
    return save_info(&s_fw_info);
}

int CtFlash_Read(uint32_t offset, uint8_t *data, uint16_t length)
{
    if ((data == 0) || (length == 0u) || !range_in_cache(offset, length))
    {
        return 0;
    }
    memcpy(data, (const void *)(CT_FW_CACHE_BASE + offset), length);
    return 1;
}
