#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bms_storage_platform.h"

typedef uint8_t u8;
typedef uint32_t u32;
#define FLASH_SIZE_512K 0x13u
#define FLASH_READ_STATUS_CMD_LOWBYTE 0x05u
#define MULTI_BOOT_ADDR_0x20000 0x20000u
#define FLASH_STORE_VERIFY_CHUNK 64u
#define MODULE_WATCHDOG_ENABLE 1

static uint8_t memory[0x80000u], expected[sizeof(memory)];
static uint32_t blc_flash_capacity = FLASH_SIZE_512K;
static uint32_t blc_flash_mid = 0x1360C8u;
static uint32_t boot_offset = MULTI_BOOT_ADDR_0x20000, ota_program_offset = 0x20000u;
static uint32_t fail_erase_address = UINT32_MAX;
static uint8_t factory_reset, ota_is_working;
static struct { uint8_t init_err; } blc_flashProt;
static unsigned erases, watchdog_clears;
static int unlocked, missing_witness;

static uint32_t blc_ota_getCurrentUsedMultipleBootAddress(void) { return boot_offset; }
static uint32_t flash_read_mid(void) { return 0x1360C8u; }
static int flash_read_status(int cmd) { assert(cmd == FLASH_READ_STATUS_CMD_LOWBYTE); return 0; }
static uint32_t bms_time_ms(void) { return 10000u; }
static int app_flash_lock_restore_enabled(void) { return 1; }
static void flash_store_begin_modify(void) { assert(!unlocked); unlocked = 1; }
static void flash_store_end_modify(void) { assert(unlocked); unlocked = 0; }
static uint8_t bms_ota_factory_reset_requested(void) { return factory_reset; }
static void wd_clear(void) { ++watchdog_clears; }

static void flash_read_page(uint32_t addr, int size, uint8_t *buf)
{
    assert(addr + (uint32_t)size <= sizeof(memory));
    if (missing_witness && addr < 0x40000u) memset(buf, 0xFF, (unsigned)size);
    else memcpy(buf, memory + addr, (unsigned)size);
}

static void flash_write_page(uint32_t addr, int size, uint8_t *buf)
{
    (void)addr; (void)size; (void)buf;
    assert(0); /* 本场景只容许擦除已退役的扇区。 */
}

static void flash_erase_sector(uint32_t addr)
{
    assert(unlocked && addr % 4096u == 0u);
    assert((addr >= 0x42000u && addr < 0x44000u) ||
           (addr >= 0x46000u && addr < 0x4C000u) ||
           (addr >= 0x4E000u && addr < 0x60000u));
    if (addr == fail_erase_address) return;
    memset(memory + addr, 0xFF, 4096u);
    ++erases;
}

/* PRODUCTION */

int main(void)
{
    const uint8_t signature[] = {0x4Bu, 0x4Eu, 0x4Cu, 0x54u};
    const storage_port_t *port;
    uint8_t data = 0u;

    memset(memory, 0xA5, sizeof(memory));
    memcpy(memory + 8u, signature, sizeof(signature));
    memcpy(memory + 0x20008u, signature, sizeof(signature));
    memcpy(expected, memory, sizeof(expected));

    assert(!bms_storage_platform_erase_factory_legacy() && erases == 0u);
    factory_reset = 1u;
    blc_flash_capacity = 0x14u;
    assert(!bms_storage_platform_erase_factory_legacy() && erases == 0u);
    blc_flash_capacity = FLASH_SIZE_512K;
    boot_offset = 0x40000u;
    assert(!bms_storage_platform_erase_factory_legacy() && erases == 0u);
    boot_offset = MULTI_BOOT_ADDR_0x20000;
    missing_witness = 1;
    assert(!bms_storage_platform_erase_factory_legacy() && erases == 0u);
    missing_witness = 0;
    ota_is_working = 1u;
    assert(!bms_storage_platform_erase_factory_legacy() && erases == 0u);
    ota_is_working = 0u;

    port = bms_storage_platform_port();
    assert(port && !port->read(0, 0x42000u, &data, 1u));
    assert(!port->program(0, 0x46000u, &data, 1u));
    assert(!port->erase(0, 0x4E000u, 4096u));
    assert(!memcmp(memory, expected, sizeof(memory)));

    fail_erase_address = 0x47000u;
    assert(!bms_storage_platform_erase_factory_legacy() && erases == 3u && !unlocked);
    fail_erase_address = UINT32_MAX;
    s_failed = 0u; /* 冷启动后丢失 RAM，仅依已擦空的物理扇区续做。 */
    ota_program_offset = 0u;
    assert(bms_storage_platform_erase_factory_legacy() && erases == 26u && !unlocked);
    memset(expected + 0x42000u, 0xFF, 0x02000u);
    memset(expected + 0x46000u, 0xFF, 0x06000u);
    memset(expected + 0x4E000u, 0xFF, 0x12000u);
    assert(!memcmp(memory, expected, sizeof(memory)));
    assert(bms_storage_platform_erase_factory_legacy() && erases == 26u);
    assert(watchdog_clears >= 26u);
    puts("PASS legacy sector whitelist, resumed erase, APP/OTA/SDK address sentinels");
    return 0;
}
