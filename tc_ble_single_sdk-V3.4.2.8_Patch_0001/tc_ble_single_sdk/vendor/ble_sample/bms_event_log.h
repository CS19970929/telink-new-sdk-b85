#pragma once

#include "tl_common.h"
#include "flash_store_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BMS_EVENT_LOG_FLASH_BASE
#define BMS_EVENT_LOG_FLASH_BASE      FLASH_ADDR_LOG_BASE
#endif

#ifndef BMS_EVENT_LOG_FLASH_SECTORS
#define BMS_EVENT_LOG_FLASH_SECTORS   FLASH_ADDR_LOG_SECTORS
#endif

#ifndef BMS_EVENT_LOG_SECTOR_SIZE
#define BMS_EVENT_LOG_SECTOR_SIZE     FLASH_SECTOR_SIZE
#endif

#define BMS_EVENT_LOG_ENTRY_COUNT     100u

#define BMS_EVENT_LOG_REG_BASE        0xC008u
#define BMS_EVENT_LOG_REG_COUNT       BMS_EVENT_LOG_ENTRY_COUNT
#define BMS_EVENT_LOG_RESET_REG       0x1007u

typedef enum {
    BMS_EVENT_NULL1 = 0,
    BMS_START_UP,
    BMS_SLEEP,
    BALANCE_OPEN,
    HEAT_OPEN,
    COOL_OPEN,
    VCELL_OVP,
    VBUS_OVP,
    CHG_OCP,
    VCELL_UVP,
    VBUS_UVP,
    DSG_OCP,
    CHG_UTP,
    DSG_UTP,
    CHG_OTP,
    DSG_OTP,
    VDELTA_OP,
    CBC_ERR,
    AFE1_ERR,
    AFE2_ERR,
    EEPROM_ERR,
    EVENT_NUM
} bms_event_log_id_t;

typedef struct {
    u8 sleep;
    u8 balance;
    u8 heat;
    u8 cool;
    u8 vcell_ovp;
    u8 vbus_ovp;
    u8 chg_ocp;
    u8 vcell_uvp;
    u8 vbus_uvp;
    u8 dsg_ocp;
    u8 chg_utp;
    u8 dsg_utp;
    u8 chg_otp;
    u8 dsg_otp;
    u8 vdelta_op;
    u8 afe2_err;
    u8 eeprom_err;
    u8 cbc_err;
} bms_event_log_sample_t;

typedef struct {
    u32 last_seq;
    u16 current_slot;
    u16 slots_per_sector;
    u16 total_slots;
    u16 write_pos;
    u8 ready;
} bms_event_log_dbg_t;

int bms_event_log_init(void);
void bms_event_log_note_startup(void);
void bms_event_log_note_sleep(void);
void bms_event_log_poll_1s(const bms_event_log_sample_t *sample);
u16 bms_event_log_read_reg(u16 reg);
void bms_event_log_fill_protocol_bytes(u8 *buf, u16 len);
int bms_event_log_factory_reset(void);
bms_event_log_dbg_t bms_event_log_get_dbg(void);

#ifdef __cplusplus
}
#endif
