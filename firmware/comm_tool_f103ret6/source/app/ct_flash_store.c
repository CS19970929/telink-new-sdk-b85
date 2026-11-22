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

    if (app_addr == CT_C073_APP_BASE)
    {
        return size >= 1024u && size <= CT_C073_APP_LIMIT - CT_C073_APP_BASE && (size % 1024u) == 0u;
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
    /* 描述来自持久Flash，必须先验证范围；不擦除旧缓存或改变布局。 */
    if ((stored->magic == CT_FW_MAGIC) && (stored->valid == CT_FW_VALID) &&
        (stored->size > 0u) && (stored->size <= CT_FW_CACHE_SIZE) &&
        app_addr_supported(stored->app_addr, stored->size))
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

    memset(&s_fw_info, 0, sizeof(s_fw_info));
    erase_len = align_up_page(size);
    if (!erase_pages(CT_FW_META_PAGE, CT_SELF_FLASH_PAGE_SIZE))
    {
        return 0;
    }
    if (!erase_pages(CT_FW_CACHE_BASE, erase_len))
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
    if ((s_fw_info.magic != CT_FW_MAGIC) || s_fw_info.valid || !data || !length || (offset & 1u) ||
        offset > s_fw_info.size || length > s_fw_info.size - offset || !range_in_cache(offset, length))
    {
        return 0;
    }
    return program_bytes(CT_FW_CACHE_BASE + offset, data, length);
}

static uint32_t c073_image_capacity(uint32_t board, uint32_t version)
{
    if (board == CT_C073_GUARDED_BOARD)
        return (version >> 16) == 2u ? CT_C073_COMPACT_CAPACITY : 0u;
    if (board != CT_C073_BOARD) return 0u;
    switch (version >> 16) {
    case 0u:
    case 1u: return CT_C073_ORIGINAL_CAPACITY;
    case 2u: return CT_C073_COMPACT_CAPACITY;
    default: return 0u;
    }
}

uint32_t CtFlash_C073Capacity(uint32_t *board)
{
    const uint32_t *words = (const uint32_t *)CT_FW_CACHE_BASE;
    uint32_t capacity;
    if (s_fw_info.valid != CT_FW_VALID || s_fw_info.app_addr != CT_C073_APP_BASE ||
        s_fw_info.size < 1024u || words[0x108u/4u] != CT_C073_APP_BASE) return 0u;
    capacity = c073_image_capacity(words[0x104u/4u], words[0x10Cu/4u]);
    if (s_fw_info.size > capacity) return 0u;
    *board = words[0x104u/4u];
    return capacity;
}

static int c073_image_valid(uint32_t size, uint32_t expected_crc)
{
    const uint32_t *words = (const uint32_t *)CT_FW_CACHE_BASE;
    const uint8_t *data = (const uint8_t *)words;
    uint32_t stack = words[0], entry = words[1], actual = words[0x110u / 4u];
    uint32_t crc = 0xFFFFFFFFu, i, bit;
    if (words[0x108u/4u] != CT_C073_APP_BASE ||
        size > c073_image_capacity(words[0x104u/4u], words[0x10Cu/4u]) ||
        actual <= 0x114u || actual > size || ((actual + 1023u) & ~1023u) != size ||
        stack < 0x200000D0u || stack > 0x20005000u || (stack & 7u) || !(entry & 1u) ||
        (entry & ~1u) < CT_C073_APP_BASE + 0x114u || (entry & ~1u) >= CT_C073_APP_BASE + actual)
        return 0;
    for (i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        if ((i & 255u) == 0u) CtWatchdog_Feed();
    }
    return ~crc == expected_crc;
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
    if (actual_crc != crc16 || (s_fw_info.app_addr == CT_C073_APP_BASE && !c073_image_valid(size, crc32)))
    {
        return 0;
    }

    s_fw_info.valid = CT_FW_VALID;
    if (save_info(&s_fw_info)) return 1;
    s_fw_info.valid = 0u;
    return 0;
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
