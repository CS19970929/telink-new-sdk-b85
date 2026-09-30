#ifndef STORAGE_RECORD_H_
#define STORAGE_RECORD_H_

#include <stdint.h>
#include "storage_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_RECORD_FORMAT_VERSION 1u
#define STORAGE_RECORD_COMMIT0        0x434D4954u /* CMIT */
#define STORAGE_RECORD_COMMIT1        0xA55AA55Au
#define STORAGE_RECORD_HEADER_SIZE    32u
#define STORAGE_RECORD_MAX_PROGRAM_SIZE 8u

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
} storage_record_store_t;

int storage_record_open(storage_record_store_t *store,
                        const storage_port_t *port,
                        storage_region_t region,
                        uint32_t magic,
                        uint16_t schema_version,
                        uint16_t payload_size);

int storage_record_load(storage_record_store_t *store, uint8_t *payload_out);
int storage_record_save(storage_record_store_t *store, const uint8_t *payload);
int storage_record_format(storage_record_store_t *store);

uint32_t storage_record_crc32(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_RECORD_H_ */
