#ifndef BMS_STORAGE_PLATFORM_H_
#define BMS_STORAGE_PLATFORM_H_

#include "storage_port.h"

#define BMS_STORAGE_RETRY_INTERVAL_MS 5000u

typedef enum {
    BMS_STORAGE_DOMAIN_CONFIG,
    BMS_STORAGE_DOMAIN_STATE,
    BMS_STORAGE_DOMAIN_EVENT,
    BMS_STORAGE_DOMAIN_COUNT
} bms_storage_domain_t;

const storage_port_t *bms_storage_platform_port(void);
int bms_storage_platform_region(bms_storage_domain_t domain, storage_region_t *region);
int bms_storage_platform_erase_factory_legacy(void);

#endif
