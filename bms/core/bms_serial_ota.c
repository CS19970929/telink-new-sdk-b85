/* 分包升级只写备用槽；完整读回CRC通过后，独立COMMIT才改变启动标志。 */
#include "bms_serial_ota.h"
#include "bms_crc.h"
#include "bms_product.h"
#include "bms_diag.h"
#include <string.h>

#define OTA_SILENCE_MS 15000u
#define OTA_TOTAL_MS 600000u
#define OTA_REBOOT_MS 1000u
#define OTA_SECTOR_BYTES 4096u
enum { OTA_QUERY, OTA_BEGIN, OTA_DATA, OTA_FINISH, OTA_COMMIT, OTA_ABORT };
enum { OTA_OK, OTA_BAD_FRAME, OTA_BUSY, OTA_SESSION, OTA_SEQUENCE,
       OTA_IMAGE, OTA_FLASH, OTA_TIMEOUT };

/* 正式BIN内的只读描述，不改变启动头、持久化或BLE协议。客户端据此拒绝跨产品包。 */
static const uint8_t image_info[20] = {
    'B','M','S','U','8','2','5','1', 1u, BMS_PRODUCT_ID, 1u, 0u,
    (uint8_t)BMS_DIAG_BUILD_ID, (uint8_t)(BMS_DIAG_BUILD_ID >> 8),
    (uint8_t)(BMS_DIAG_BUILD_ID >> 16), (uint8_t)(BMS_DIAG_BUILD_ID >> 24),
    0u, 0xF0u, 1u, 0u
};
static struct {
    bms_serial_ota_state_t state;
    uint8_t error, opened;
    uint32_t session, base, size, next, erase, verify, crc;
    uint32_t build, descriptor, trailer, start_ms, last_ms, commit_ms;
} update;

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write32(uint8_t *p, uint32_t value)
{
    uint8_t i;
    for (i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
uint8_t bms_serial_ota_busy(void)
{
    return update.opened;
}
static void close_update(uint8_t error)
{
    if (update.opened) bms_serial_ota_flash_close();
    update.opened = 0u;
    update.error = error;
    update.state = error ? BMS_SERIAL_OTA_ERROR : BMS_SERIAL_OTA_IDLE;
}
static uint8_t begin_update(const uint8_t *p, uint32_t session)
{
    uint32_t size = read32(p + 1u), descriptor = read32(p + 9u);
    if (!session || p[0] != BMS_PRODUCT_ID || size < 64u ||
        size > BMS_SERIAL_OTA_SLOT_BYTES || descriptor > size - 20u)
        return OTA_IMAGE;
    if (update.opened) return OTA_BUSY;
    if (!bms_serial_ota_flash_open(&update.base)) return OTA_BUSY;
    update.opened = 1u;
    update.session = session;
    update.size = size;
    update.build = read32(p + 5u);
    update.descriptor = descriptor;
    update.trailer = read32(p + 13u);
    update.next = update.erase = update.verify = 0u;
    update.crc = 0xFFFFFFFFu;
    update.error = OTA_OK;
    update.start_ms = update.last_ms = bms_serial_ota_time_ms();
    update.state = BMS_SERIAL_OTA_ERASING;
    return OTA_OK;
}
static uint8_t accept_data(const uint8_t *p, uint32_t length)
{
    uint32_t offset, count, i;
    uint8_t buffer[BMS_SERIAL_OTA_BLOCK_BYTES];
    if (length <= 4u || length > 4u + sizeof(buffer)) return OTA_BAD_FRAME;
    offset = read32(p);
    count = length - 4u;
    if (update.state != BMS_SERIAL_OTA_RECEIVING || offset > update.size ||
        count > update.size - offset) return OTA_SEQUENCE;
    if (offset == 0u && (count < 32u || read32(p + 12u) != 0x544C4E4Bu ||
                         read32(p + 28u) != update.size)) return OTA_IMAGE;
    memcpy(buffer, p + 4u, count);
    /* 仅屏蔽硬件检查的+8字节；其余TLNK字节正常写入并参与读回CRC。 */
    for (i = 0u; i < count; ++i)
        if (offset + i == 8u) buffer[i] = 0xFFu;
    if (offset == update.next) {
        if (!bms_serial_ota_flash_write(update.base + offset, buffer, count)) {
            close_update(OTA_FLASH);
            return OTA_FLASH;
        }
        update.next += count;
    } else if (offset < update.next && count <= update.next - offset) {
        uint8_t saved[BMS_SERIAL_OTA_BLOCK_BYTES];
        bms_serial_ota_flash_read(update.base + offset, saved, count);
        if (memcmp(buffer, saved, count)) return OTA_SEQUENCE;
    } else return OTA_SEQUENCE;
    return OTA_OK;
}

int bms_serial_ota_on_frame(const uint8_t *req, uint32_t len, uint8_t *rsp, uint32_t *rsp_len)
{
    uint8_t command, error = OTA_OK;
    uint32_t session, payload, build, running_crc;
    uint16_t crc;
    if (!req || !rsp || !rsp_len || len < 2u || req[1] != BMS_SERIAL_OTA_FUNCTION) return 0;
    *rsp_len = 0u;
    /* 仅现有从站1；广播不会擦Flash或触发升级。 */
    if (len < 11u || len > 143u || req[0] != 1u ||
        mb_crc16(req, len - 2u) != ((uint16_t)req[len-2u] | ((uint16_t)req[len-1u] << 8))) return 1;
    command = req[2];
    session = read32(req + 5u);
    payload = len - 11u;
    if (command == OTA_QUERY && payload == 0u) {
        if (session && session != update.session) error = OTA_SESSION;
    } else if (command == OTA_BEGIN && payload == 17u) {
        error = begin_update(req + 9u, session);
    } else if (!update.opened || session != update.session) {
        error = OTA_SESSION;
    } else if (command == OTA_DATA) {
        error = accept_data(req + 9u, payload);
    } else if (command == OTA_FINISH && payload == 0u) {
        if (update.state == BMS_SERIAL_OTA_RECEIVING && update.next == update.size)
            update.state = BMS_SERIAL_OTA_VERIFYING;
        else if (update.state != BMS_SERIAL_OTA_VERIFYING && update.state != BMS_SERIAL_OTA_READY)
            error = OTA_SEQUENCE;
    } else if (command == OTA_COMMIT && payload == 0u) {
        if (update.state != BMS_SERIAL_OTA_READY) error = OTA_SEQUENCE;
        else if (!bms_serial_ota_flash_commit(update.base)) {
            close_update(OTA_FLASH);
            error = OTA_FLASH;
        } else {
            update.state = BMS_SERIAL_OTA_COMMITTED;
            update.commit_ms = bms_serial_ota_time_ms();
        }
    } else if (command == OTA_ABORT && payload == 0u && update.state != BMS_SERIAL_OTA_COMMITTED) {
        close_update(OTA_OK);
    } else error = OTA_BAD_FRAME;
    if (!error && update.opened && (!session || session == update.session))
        update.last_ms = bms_serial_ota_time_ms();
    bms_serial_ota_running_image(&build, &running_crc);
    memcpy(rsp, req, 5u);
    rsp[5] = (uint8_t)update.state;
    rsp[6] = error ? error : update.error;
    /* volatile读取保留描述段，防止LTO/常量折叠后被--gc-sections移除。 */
    rsp[7] = ((const volatile uint8_t *)image_info)[9];
    write32(rsp + 8u, update.session);
    write32(rsp + 12u, update.next);
    write32(rsp + 16u, update.size);
    write32(rsp + 20u, build);
    write32(rsp + 24u, running_crc);
    crc = mb_crc16(rsp, 28u);
    rsp[28] = (uint8_t)crc; rsp[29] = (uint8_t)(crc >> 8);
    *rsp_len = BMS_SERIAL_OTA_REPLY_BYTES;
    return 1;
}

void bms_serial_ota_poll(void)
{
    uint32_t now, count, i;
    uint8_t buffer[BMS_SERIAL_OTA_BLOCK_BYTES], bit;
    if (!update.opened) return;
    now = bms_serial_ota_time_ms();
    if (update.state == BMS_SERIAL_OTA_COMMITTED) {
        if ((uint32_t)(now - update.commit_ms) >= OTA_REBOOT_MS) bms_serial_ota_reboot();
        return;
    }
    if ((uint32_t)(now - update.last_ms) >= OTA_SILENCE_MS ||
        (uint32_t)(now - update.start_ms) >= OTA_TOTAL_MS) {
        close_update(OTA_TIMEOUT);
        return;
    }
    if (update.state == BMS_SERIAL_OTA_ERASING) {
        if (!bms_serial_ota_flash_erase(update.base + update.erase)) {
            close_update(OTA_FLASH); return;
        }
        update.erase += OTA_SECTOR_BYTES;
        if (update.erase == BMS_SERIAL_OTA_SLOT_BYTES) update.state = BMS_SERIAL_OTA_RECEIVING;
    } else if (update.state == BMS_SERIAL_OTA_VERIFYING) {
        count = update.size - update.verify;
        if (count > sizeof(buffer)) count = sizeof(buffer);
        bms_serial_ota_flash_read(update.base + update.verify, buffer, count);
        for (i = 0u; i < count; ++i) {
            uint32_t offset = update.verify + i;
            uint8_t value = buffer[i];
            if (offset == 8u) value = 0x4Bu;
            update.crc ^= value;
            for (bit = 0u; bit < 8u; ++bit)
                update.crc = (update.crc >> 1) ^ ((update.crc & 1u) ? 0xEDB88320u : 0u);
        }
        update.verify += count;
        if (update.verify == update.size) {
            bms_serial_ota_flash_read(update.base + update.descriptor, buffer, 20u);
            if (update.crc != 0u || memcmp(buffer, image_info, 12u) ||
                read32(buffer + 12u) != update.build || read32(buffer + 16u) != BMS_SERIAL_OTA_SLOT_BYTES) {
                close_update(OTA_IMAGE); return;
            }
            bms_serial_ota_flash_read(update.base + update.size - 4u, buffer, 4u);
            if (read32(buffer) != update.trailer) close_update(OTA_IMAGE);
            else update.state = BMS_SERIAL_OTA_READY;
        }
    }
}
