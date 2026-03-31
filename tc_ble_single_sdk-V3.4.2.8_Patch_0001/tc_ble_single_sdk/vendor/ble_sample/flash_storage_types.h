#ifndef FLASH_STORAGE_TYPES_H_
#define FLASH_STORAGE_TYPES_H_

#include "tl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLASH_STORAGE_LAYOUT_VERSION            1u

#define FLASH_CFG_MAGIC                         0x43464731u  /* CFG1 */
#define FLASH_RUNTIME_MAGIC                     0x52554E31u  /* RUN1 */
#define FLASH_EVENT_LOG_MAGIC                   0x4C4F4731u  /* LOG1 */

#define FLASH_BTNAME_SUFFIX_MAX_LEN             22u
#define FLASH_EVENT_RECORD_DATA_WORDS           2u

typedef struct
{
    u16 layout_version;
    u16 payload_size;
    u32 seq;
} flash_store_hdr_t;

typedef struct
{
    u16 layout_version;
    u16 param_size;
    u8  bt_name_len;
    u8  reserved0;
    u16 reserved1;
} flash_config_meta_t;

typedef struct
{
    u32 runtime_min;
    u16 cycle;
    u16 soh;
    u16 soc;
    u16 dsg_int;
    u8  bms_mode;
    u8  factory_expired;
    u8  shutdown_reason;
    u8  reserved0;
    u16 fault_summary;
    u16 reserved1;
    u32 update_counter;
} flash_runtime_state_t;

typedef struct
{
    u32 seq;
    u32 runtime_min;
    u16 event_id;
    u8  level;
    u8  source;
    u32 data[FLASH_EVENT_RECORD_DATA_WORDS];
    u16 crc;
    u16 reserved;
} flash_event_record_t;

#ifdef __cplusplus
}
#endif

#endif
