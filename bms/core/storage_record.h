/*
 * 文件功能：带版本、序号和 CRC 的 Flash 记录；处理有效记录选择、写入验证与掉电后旧记
 * 录恢复。
 * bms/core/storage_record.h；实际编译归属见各产品 sources.txt。
 */
#ifndef STORAGE_RECORD_H_
#define STORAGE_RECORD_H_

#include <stdint.h>
#include "storage_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_RECORD_FORMAT_VERSION 1u
#define STORAGE_RECORD_COMMIT0        0x434D4954u /* 提交标识：CMIT。 */
#define STORAGE_RECORD_COMMIT1        0xA55AA55Au
#define STORAGE_RECORD_HEADER_SIZE    32u
#define STORAGE_RECORD_MAX_PROGRAM_SIZE 8u
#define STORAGE_RECORD_MAX_WRITE_FAILURES 3u

typedef enum {
    STORAGE_RECORD_LOAD_EMPTY = 0,
    STORAGE_RECORD_LOAD_OK,
    STORAGE_RECORD_LOAD_INCOMPATIBLE,
    STORAGE_RECORD_LOAD_CORRUPT,
    STORAGE_RECORD_LOAD_IO_ERROR
} storage_record_load_status_t;

typedef struct {
    const storage_port_t *port;
    storage_region_t region;
    uint32_t magic;
    uint16_t schema_version;
    uint16_t payload_size;
    uint16_t slot_size;
    uint16_t slots_per_sector;
    uint16_t sector_count;
    uint32_t latest_addr;
    uint32_t next_sequence;
    uint8_t has_latest;
    uint8_t ready;
    uint32_t write_addr;
    uint8_t write_needs_erase;
    uint8_t write_failures;
    storage_record_load_status_t load_status;
} storage_record_store_t;

/* 校验分区和端口配置并建立记录访问状态。 */
int storage_record_open(storage_record_store_t *store,
                        const storage_port_t *port,
                        storage_region_t region,
                        uint32_t magic,
                        uint16_t schema_version,
                        uint16_t payload_size);

/* 返回 1 表示加载成功；失败原因见 load_status，IO_ERROR 不允许覆盖为默认值。 */
int storage_record_load(storage_record_store_t *store, uint8_t *payload_out);
/* 写入并验证新记录后发布提交标记，保留旧有效记录。 */
int storage_record_save(storage_record_store_t *store, const uint8_t *payload);


#ifdef __cplusplus
}
#endif

#endif /* 头文件保护：STORAGE_RECORD_H_。 */
