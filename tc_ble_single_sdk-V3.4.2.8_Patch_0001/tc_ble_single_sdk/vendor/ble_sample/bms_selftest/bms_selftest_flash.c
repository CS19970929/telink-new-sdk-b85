#include "bms_selftest_internal.h"
#include "bms_fault_inject.h"
#include "bms_selftest_port.h"

#include "drivers.h"

#define BMS_MANIFEST_MAGIC       0x464d5342u /* "BSMF" in little endian */
#define BMS_MANIFEST_VERSION     1u
#define BMS_MANIFEST_BYTES       64u
#define BMS_MANIFEST_CRC_OFFSET  48u
#define BMS_IMAGE_MAX_BYTES      0x1f000u
#define BMS_CRC32_POLYNOMIAL     0xedb88320u

typedef struct {
    u32 magic;
    u16 version;
    u16 header_size;
    u32 flags;
    u32 image_start;
    u32 image_size;
    u32 range0_start;
    u32 range0_length;
    u32 range1_start;
    u32 range1_length;
    u32 image_crc32;
    u32 polynomial;
    u32 build_id;
    u32 manifest_crc32;
    u32 reserved[3];
} bms_image_manifest_t;

__attribute__((section(".bms_manifest"), used))
const bms_image_manifest_t g_bms_image_manifest = {
    0xffffffffu, 0xffffu, 0xffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    {0xffffffffu, 0xffffffffu, 0xffffffffu}
};

static bms_image_manifest_t g_bms_manifest_ram;
static u8 g_bms_flash_buffer[BMS_SELFTEST_FLASH_BLOCK_BYTES];
static u32 g_bms_flash_base;
static u32 g_bms_flash_range_index;
static u32 g_bms_flash_range_offset;
static u32 g_bms_flash_crc_state;
static u8 g_bms_flash_manifest_valid;

static u32 bms_crc32_update(u32 state, const u8 *data, u32 length)
{
    u32 i;
    u8 bit;

    for (i = 0u; i < length; ++i)
    {
        state ^= data[i];
        for (bit = 0u; bit < 8u; ++bit)
        {
            state = (state >> 1) ^ ((state & 1u) ? BMS_CRC32_POLYNOMIAL : 0u);
        }
    }
    return state;
}

static u32 bms_manifest_crc(const bms_image_manifest_t *manifest)
{
    const u8 *bytes = (const u8 *)manifest;
    u32 state = 0xffffffffu;
    u32 i;
    u8 value;

    for (i = 0u; i < BMS_MANIFEST_BYTES; ++i)
    {
        value = ((i >= BMS_MANIFEST_CRC_OFFSET) && (i < BMS_MANIFEST_CRC_OFFSET + 4u)) ? 0u : bytes[i];
        state = bms_crc32_update(state, &value, 1u);
    }
    return ~state;
}

static u8 bms_flash_range_valid(u32 start, u32 length, u32 image_size)
{
    if ((start > image_size) || (length > image_size))
    {
        return 0u;
    }
    return (start + length <= image_size) ? 1u : 0u;
}

static u8 bms_flash_load_manifest(void)
{
    u32 manifest_offset = (u32)&g_bms_image_manifest;

    g_bms_flash_base = BMS_Port_GetRunningImageBase();
    flash_read_page(g_bms_flash_base + manifest_offset,
                    (u32)sizeof(g_bms_manifest_ram), (u8 *)&g_bms_manifest_ram);

    if ((sizeof(g_bms_manifest_ram) != BMS_MANIFEST_BYTES) ||
        (g_bms_manifest_ram.magic != BMS_MANIFEST_MAGIC) ||
        (g_bms_manifest_ram.version != BMS_MANIFEST_VERSION) ||
        (g_bms_manifest_ram.header_size != BMS_MANIFEST_BYTES) ||
        (g_bms_manifest_ram.image_start != 0u) ||
        (g_bms_manifest_ram.image_size < 0x20u) ||
        (g_bms_manifest_ram.image_size > BMS_IMAGE_MAX_BYTES) ||
        (g_bms_manifest_ram.polynomial != BMS_CRC32_POLYNOMIAL) ||
        !bms_flash_range_valid(g_bms_manifest_ram.range0_start, g_bms_manifest_ram.range0_length,
                               g_bms_manifest_ram.image_size) ||
        !bms_flash_range_valid(g_bms_manifest_ram.range1_start, g_bms_manifest_ram.range1_length,
                               g_bms_manifest_ram.image_size) ||
        (g_bms_manifest_ram.range0_start + g_bms_manifest_ram.range0_length > manifest_offset) ||
        (g_bms_manifest_ram.range1_start < manifest_offset + BMS_MANIFEST_BYTES) ||
        (bms_manifest_crc(&g_bms_manifest_ram) != g_bms_manifest_ram.manifest_crc32))
    {
        g_bms_flash_manifest_valid = 0u;
        return 0u;
    }
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_FLASH_MANIFEST))
    {
        g_bms_flash_manifest_valid = 0u;
        return 0u;
    }

    g_bms_flash_manifest_valid = 1u;
    return 1u;
}

static u32 bms_flash_crc_range(u32 start, u32 length, u32 state)
{
    u32 chunk;

    while (length != 0u)
    {
        chunk = (length > BMS_SELFTEST_FLASH_BLOCK_BYTES) ? BMS_SELFTEST_FLASH_BLOCK_BYTES : length;
        flash_read_page(g_bms_flash_base + start, chunk, g_bms_flash_buffer);
        state = bms_crc32_update(state, g_bms_flash_buffer, chunk);
        start += chunk;
        length -= chunk;
    }
    return state;
}

u8 BMS_SelfTest_FlashStartup(void)
{
    u32 state;
    u32 actual;
    u32 magic;
    u32 telink_size;

    if (!bms_flash_load_manifest())
    {
        BMS_SelfTest_SetFlashDiag(0u, 0u, 0u, 0u);
        return 0u;
    }

    flash_read_page(g_bms_flash_base + 0x08u, 4u, (u8 *)&magic);
    flash_read_page(g_bms_flash_base + 0x18u, 4u, (u8 *)&telink_size);
    if ((magic != 0x544c4e4bu) || (telink_size < g_bms_manifest_ram.image_size) ||
        (telink_size > BMS_IMAGE_MAX_BYTES))
    {
        return 0u;
    }

    state = bms_flash_crc_range(g_bms_manifest_ram.range0_start,
                                g_bms_manifest_ram.range0_length, 0xffffffffu);
    state = bms_flash_crc_range(g_bms_manifest_ram.range1_start,
                                g_bms_manifest_ram.range1_length, state);
    actual = ~state;
    if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_FLASH_CRC))
    {
        actual ^= 1u;
    }
    BMS_SelfTest_SetFlashDiag(g_bms_manifest_ram.image_crc32, actual,
                              g_bms_manifest_ram.image_size, g_bms_manifest_ram.image_size);
    return (actual == g_bms_manifest_ram.image_crc32) ? 1u : 0u;
}

void BMS_SelfTest_FlashRuntimeReset(void)
{
    g_bms_flash_range_index = 0u;
    g_bms_flash_range_offset = 0u;
    g_bms_flash_crc_state = 0xffffffffu;
    BMS_SelfTest_SetFlashDiag(g_bms_manifest_ram.image_crc32, 0u, 0u,
                              g_bms_manifest_ram.range0_length + g_bms_manifest_ram.range1_length);
}

u8 BMS_SelfTest_FlashRuntimeStep(void)
{
    u32 start;
    u32 length;
    u32 remaining;
    u32 chunk;
    u32 progress;
    u32 actual;

    if (!g_bms_flash_manifest_valid)
    {
        return 0u;
    }
    if (g_bms_flash_range_index == 0u)
    {
        start = g_bms_manifest_ram.range0_start;
        length = g_bms_manifest_ram.range0_length;
    }
    else
    {
        start = g_bms_manifest_ram.range1_start;
        length = g_bms_manifest_ram.range1_length;
    }

    remaining = length - g_bms_flash_range_offset;
    chunk = (remaining > BMS_SELFTEST_FLASH_BLOCK_BYTES) ? BMS_SELFTEST_FLASH_BLOCK_BYTES : remaining;
    if (chunk != 0u)
    {
        flash_read_page(g_bms_flash_base + start + g_bms_flash_range_offset, chunk, g_bms_flash_buffer);
        g_bms_flash_crc_state = bms_crc32_update(g_bms_flash_crc_state, g_bms_flash_buffer, chunk);
        g_bms_flash_range_offset += chunk;
    }

    if (g_bms_flash_range_offset >= length)
    {
        if (g_bms_flash_range_index == 0u)
        {
            g_bms_flash_range_index = 1u;
            g_bms_flash_range_offset = 0u;
        }
        else
        {
            actual = ~g_bms_flash_crc_state;
            if (BMS_FaultInject_ShouldFail((u8)BMS_FAULT_FLASH_CRC)) actual ^= 1u;
            g_bms_flash_range_index = 0u;
            g_bms_flash_range_offset = 0u;
            g_bms_flash_crc_state = 0xffffffffu;
            BMS_SelfTest_SetFlashDiag(g_bms_manifest_ram.image_crc32, actual,
                                      g_bms_manifest_ram.range0_length + g_bms_manifest_ram.range1_length,
                                      g_bms_manifest_ram.range0_length + g_bms_manifest_ram.range1_length);
            return (actual == g_bms_manifest_ram.image_crc32) ? 1u : 0u;
        }
    }

    progress = (g_bms_flash_range_index == 0u) ? g_bms_flash_range_offset :
               (g_bms_manifest_ram.range0_length + g_bms_flash_range_offset);
    BMS_SelfTest_SetFlashDiag(g_bms_manifest_ram.image_crc32, 0u, progress,
                              g_bms_manifest_ram.range0_length + g_bms_manifest_ram.range1_length);
    return 1u;
}
