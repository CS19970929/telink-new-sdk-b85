#ifndef BMS_STORAGE_PLATFORM_H_
#define BMS_STORAGE_PLATFORM_H_

#include "storage_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BMS_STORAGE_DOMAIN_CONFIG = 0,
    BMS_STORAGE_DOMAIN_STATE,
    BMS_STORAGE_DOMAIN_FACTORY,
    BMS_STORAGE_DOMAIN_EVENT,
    BMS_STORAGE_DOMAIN_COUNT
} bms_storage_domain_t;

const storage_port_t *bms_storage_platform_port(void);
int bms_storage_platform_region(bms_storage_domain_t domain, storage_region_t *region);

#ifdef __cplusplus
}
#endif

#endif /* BMS_STORAGE_PLATFORM_H_ */
