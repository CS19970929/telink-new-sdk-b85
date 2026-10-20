/* TLSR8251普通Flash端口：复用SDK选槽与Flash锁，业务区不参与升级。 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app.h"
#include "bms_diag.h"
#include "bms_time.h"
#include "bms_serial_ota.h"
#include "modbus_uart.h"
#include "flash_store_cfg.h"

static uint32_t candidate;

uint32_t bms_serial_ota_time_ms(void) { return bms_time_ms(); }

int bms_serial_ota_flash_open(uint32_t *base)
{
    uint8_t old_mark;
    if (ota_is_working || !app_flash_lock_restore_enabled() || blc_flashProt.init_err ||
        ota_program_bootAddr != MULTI_BOOT_ADDR_0x20000 ||
        (ota_program_offset != 0u && ota_program_offset != 0x20000u) ||
        ota_firmware_max_size < BMS_SERIAL_OTA_SLOT_BYTES || !flash_store_cfg_layout_supported()) return 0;
    candidate = ota_program_offset;
    flash_read_page((candidate ? 0u : 0x20000u) + 8u, 1u, &old_mark);
    if (old_mark != 0x4Bu) return 0;
    ota_is_working = 1u;
    app_flash_protection_operation(FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_BEGIN,
                                 candidate, candidate + BMS_SERIAL_OTA_SLOT_BYTES);
    *base = candidate;
    return 1;
}
void bms_serial_ota_flash_close(void)
{
    app_flash_protection_operation(FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_END, 0u, 0u);
    ota_is_working = 0u;
}
static int candidate_range(uint32_t address, uint32_t length)
{
    return address >= candidate && length <= BMS_SERIAL_OTA_SLOT_BYTES &&
           address - candidate <= BMS_SERIAL_OTA_SLOT_BYTES - length;
}
void bms_serial_ota_flash_read(uint32_t address, uint8_t *data, uint32_t length)
{
    flash_read_page(address, length, data);
}
int bms_serial_ota_flash_erase(uint32_t address)
{
    uint8_t buffer[128];
    uint32_t offset, i;
    if (!candidate_range(address, 4096u) || (address & 4095u)) return 0;
    flash_erase_sector(address);
    for (offset = 0u; offset < 4096u; offset += sizeof(buffer)) {
        flash_read_page(address + offset, sizeof(buffer), buffer);
        for (i = 0u; i < sizeof(buffer); ++i) if (buffer[i] != 0xFFu) return 0;
    }
    return 1;
}
int bms_serial_ota_flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint8_t verify[BMS_SERIAL_OTA_BLOCK_BYTES];
    uint32_t count;
    if (!candidate_range(address, length)) return 0;
    while (length) {
        count = 256u - (address & 255u);
        if (count > length) count = length;
        if (count > sizeof(verify)) count = sizeof(verify);
        flash_write_page(address, count, (uint8_t *)data);
        flash_read_page(address, count, verify);
        if (memcmp(verify, data, count)) return 0;
        address += count; data += count; length -= count;
    }
    return 1;
}
void bms_serial_ota_running_image(uint32_t *build, uint32_t *crc)
{
    uint32_t size, running = ota_program_offset ? 0u : ota_program_bootAddr;
    *build = BMS_DIAG_BUILD_ID;
    *crc = 0u;
    flash_read_page(running + 0x18u, 4u, (uint8_t *)&size);
    if (size >= 64u && size <= BMS_SERIAL_OTA_SLOT_BYTES)
        flash_read_page(running + size - 4u, 4u, (uint8_t *)crc);
}
int bms_serial_ota_flash_commit(uint32_t base)
{
    uint8_t marker = 0x4Bu;
    uint8_t invalid = 0u, old_mark;
    if (base != candidate || !ota_is_working) return 0;
    if (!bms_serial_ota_flash_write(base + 8u, &marker, 1u)) return 0;
    /* 仅校验并激活新区后，才允许使当前运行区失效；与SDK提交次序一致。 */
    flash_write_page((base ? 0u : 0x20000u) + 8u, 1u, &invalid);
    flash_read_page((base ? 0u : 0x20000u) + 8u, 1u, &old_mark);
    return old_mark != 0x4Bu;
}
void bms_serial_ota_reboot(void)
{
    /* 给30字节应答及RS485 DE释放留1s，仍须检查硬件发送状态。 */
    if (modbus_uart_tx_active()) return;
    bms_serial_ota_flash_close();
    start_reboot();
}
