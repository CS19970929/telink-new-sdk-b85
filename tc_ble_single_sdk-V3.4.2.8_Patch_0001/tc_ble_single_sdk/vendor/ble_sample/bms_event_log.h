#pragma once

#include "tl_common.h"

#ifdef __cplusplus
extern "C" {
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
    u8 sleep, balance, heat, cool;
    u8 vcell_ovp, vbus_ovp, chg_ocp, vcell_uvp, vbus_uvp, dsg_ocp;
    u8 chg_utp, dsg_utp, chg_otp, dsg_otp, vdelta_op;
    u8 afe2_err, eeprom_err, cbc_err;
} bms_event_log_sample_t;

int bms_event_log_init(void);
void bms_event_log_note_startup(void);
int bms_event_log_note_sleep(void);
void bms_event_log_poll_1s(const bms_event_log_sample_t *sample);
u16 bms_event_log_read_reg(u16 reg);
int bms_event_log_factory_reset(void);

#ifdef __cplusplus
}
#endif
