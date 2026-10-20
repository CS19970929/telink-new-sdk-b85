/* UART/RS485升级：只由主循环调用，Flash和SDK状态由Telink端口持有。 */
#pragma once
#include <stdint.h>

#define BMS_SERIAL_OTA_FUNCTION 0x46u
#define BMS_SERIAL_OTA_SLOT_BYTES (124u * 1024u)
#define BMS_SERIAL_OTA_BLOCK_BYTES 128u
#define BMS_SERIAL_OTA_REPLY_BYTES 30u
typedef enum {
    BMS_SERIAL_OTA_IDLE, BMS_SERIAL_OTA_ERASING, BMS_SERIAL_OTA_RECEIVING,
    BMS_SERIAL_OTA_VERIFYING, BMS_SERIAL_OTA_READY, BMS_SERIAL_OTA_COMMITTED,
    BMS_SERIAL_OTA_ERROR
} bms_serial_ota_state_t;

/* 返回1表示本模块消费该帧；CRC/长度错误不响应。现有Modbus地址不变。 */
int bms_serial_ota_on_frame(const uint8_t *request, uint32_t length,
                          uint8_t *response, uint32_t *response_length);
void bms_serial_ota_poll(void);
uint8_t bms_serial_ota_busy(void);

/* 唯一硬件端口；host故障模型使用同一core与可替换端口。 */
uint32_t bms_serial_ota_time_ms(void);
int bms_serial_ota_flash_open(uint32_t *candidate_base);
void bms_serial_ota_flash_close(void);
int bms_serial_ota_flash_erase(uint32_t address);
int bms_serial_ota_flash_write(uint32_t address, const uint8_t *data, uint32_t length);
void bms_serial_ota_flash_read(uint32_t address, uint8_t *data, uint32_t length);
void bms_serial_ota_running_image(uint32_t *build_id, uint32_t *image_crc);
int bms_serial_ota_flash_commit(uint32_t candidate_base);
void bms_serial_ota_reboot(void);
