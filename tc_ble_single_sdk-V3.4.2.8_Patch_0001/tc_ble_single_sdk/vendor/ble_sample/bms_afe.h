#pragma once

#include "conf.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BMS_AFE_TYPE_SH367309
#define BMS_AFE_TYPE_SH367309             367309u
#endif

#ifndef BMS_AFE_TYPE_SH3673520
#define BMS_AFE_TYPE_SH3673520            3673520u
#endif

#ifndef BMS_AFE_TYPE
#define BMS_AFE_TYPE                      BMS_AFE_TYPE_SH367309
#endif

void bms_afe_bus_init(void);
void bms_afe_reset(void);
u8 bms_afe_is_ready(void);
void bms_afe_apply_params(void);
void bms_afe_sleep(void);
void bms_afe_sample(void);

u16 bms_afe_param_read_word(u16 index);
int bms_afe_param_write_word(u16 index, u16 value);
int bms_afe_param_commit_and_apply(void);
int bms_afe_param_is_writable(u16 index);
u16 bms_afe_get_apply_status(void);
u32 bms_afe_get_pack_voltage_mv(void);
int bms_afe_get_signed_current_ma(void);

#ifdef __cplusplus
}
#endif
