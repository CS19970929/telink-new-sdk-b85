#include "safety_manager.h"

#include "drivers.h"
#include <string.h>

#if SAFETY_FLASH_CRC_ENFORCE

#define SAFETY_FLASH_CRC_HEADER_BYTES 16u
#define SAFETY_FLASH_CRC_APP_START    0x00000u

typedef struct
{
    u32 magic;
    u32 version;
    u32 length;
    u32 crc;
} SafetyFlashCrcInfo;

static u32 safety_crc32_update(u32 crc, const u8 *data, u32 len)
{
    u32 i;
    u8 bit;

    for (i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8u; ++bit)
        {
            if ((crc & 1u) != 0u)
            {
                crc = (crc >> 1) ^ 0xEDB88320u;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static void safety_flash_read_crc_info(SafetyFlashCrcInfo *info)
{
    u8 raw[SAFETY_FLASH_CRC_HEADER_BYTES];

    flash_read_page(SAFETY_FLASH_CRC_INFO_ADDR, (int)sizeof(raw), raw);
    memcpy(info, raw, sizeof(*info));
}

static int safety_flash_crc_info_valid(const SafetyFlashCrcInfo *info)
{
    if (info->magic != SAFETY_FLASH_CRC_MAGIC)
    {
        return 0;
    }
    if (info->version != SAFETY_FLASH_CRC_VERSION)
    {
        return 0;
    }
    if ((info->length == 0u) || (info->length >= SAFETY_FLASH_CRC_INFO_ADDR))
    {
        return 0;
    }
    return 1;
}

static u32 safety_flash_crc_range(u32 start, u32 len)
{
    u8 buf[SAFETY_FLASH_PARTIAL_BYTES];
    u32 offset = 0u;
    u32 crc = 0xFFFFFFFFu;

    while (offset < len)
    {
        u32 chunk = len - offset;
        if (chunk > sizeof(buf))
        {
            chunk = sizeof(buf);
        }
        flash_read_page(start + offset, (int)chunk, buf);
        crc = safety_crc32_update(crc, buf, chunk);
        offset += chunk;
    }

    return ~crc;
}

#endif

int Safety_FlashStartupTest(void)
{
#if SAFETY_ENABLE
#if SAFETY_TEST_ENABLE && SAFETY_INJECT_FLASH_FAULT
    return 0;
#elif SAFETY_FLASH_CRC_ENFORCE
    SafetyFlashCrcInfo info;
    safety_flash_read_crc_info(&info);
    if (!safety_flash_crc_info_valid(&info))
    {
        return 0;
    }
    return safety_flash_crc_range(SAFETY_FLASH_CRC_APP_START, info.length) == info.crc;
#else
    /* 未配置量产 CRC 元数据时只保留框架，不阻断现有固件启动。 */
    return 1;
#endif
#else
    return 1;
#endif
}

int Safety_FlashRuntimeTask(void)
{
#if SAFETY_ENABLE
#if SAFETY_TEST_ENABLE && SAFETY_INJECT_FLASH_FAULT
    return 0;
#else
    /* Flash 分片 CRC 依赖同一份元数据，未启用强制校验时不增加 Flash 读负担。 */
    return Safety_FlashStartupTest();
#endif
#else
    return 1;
#endif
}
