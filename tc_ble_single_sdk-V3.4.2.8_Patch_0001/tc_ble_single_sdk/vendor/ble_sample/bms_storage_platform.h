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

/* Since-boot diagnostics; no persistent wear counter writes. Time uses SDK 32k. */
typedef struct {
    uint32_t program_calls;
    uint32_t erase_calls;
    uint32_t verify_failures;
    uint32_t deferred_writes;
    uint32_t max_program_ticks_32k;
    uint32_t max_erase_ticks_32k;
} bms_storage_diagnostics_t;
void bms_storage_platform_get_diagnostics(bms_storage_diagnostics_t *out);
const storage_port_t *bms_storage_platform_port(void);
int bms_storage_platform_region(bms_storage_domain_t domain, storage_region_t *region);

#ifdef __cplusplus
}
#endif

#endif /* BMS_STORAGE_PLATFORM_H_ */
