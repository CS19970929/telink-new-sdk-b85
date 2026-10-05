/* 文件功能：AFE 硬件保护协议的寄存器与功能码定义；requested/effective 与软件保护表保持独立。
 * bms/core/bms_afe_hw_modbus.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_AFE_HW_MODBUS_H_
#define BMS_AFE_HW_MODBUS_H_

#include "tl_common.h"
#include "bms_afe_hw_profile.h"

/* 0x2500..0x2522: requested/persisted 35-word profile. */
#define BMS_AFE_HW_REQUESTED_REG_BASE           0x2500u

/* 0x2523..0x252B: read-only metadata. */
#define BMS_AFE_HW_META_CAPABILITIES             0x2523u
#define BMS_AFE_HW_META_VALID                    0x2524u
#define BMS_AFE_HW_META_SHUNT_UOHM               0x2525u
#define BMS_AFE_HW_META_CELL_COUNT               0x2526u
#define BMS_AFE_HW_META_WDT_SECONDS              0x2527u
#define BMS_AFE_HW_META_ACCESS_ACTIVE            0x2528u
#define BMS_AFE_HW_META_APPLY_STATE              0x2529u
#define BMS_AFE_HW_META_LAST_ERROR               0x252Au
#define BMS_AFE_HW_META_INTERFACE_VERSION        0x252Bu
#define BMS_AFE_HW_INTERFACE_VERSION             0x0002u
#define BMS_AFE_HW_REQUESTED_REG_COUNT           44u

/* 0x2540..0x2562: read-only values actually represented by the AFE. */
#define BMS_AFE_HW_EFFECTIVE_REG_BASE            0x2540u
#define BMS_AFE_HW_EFFECTIVE_REG_COUNT           BMS_AFE_HW_PROFILE_WORD_COUNT

#endif /* BMS_AFE_HW_MODBUS_H_ */
