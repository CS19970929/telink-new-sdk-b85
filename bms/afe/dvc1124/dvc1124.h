/* 文件功能：DVC1124 寄存器通信、采样、保护编码与驱动接口；为条件编译的 DVC backend 保留。
 * bms/afe/dvc1124/dvc1124.h；实际编译归属见各产品 sources.txt。
 */
#ifndef DVC1124_H_
#define DVC1124_H_

#include <stdint.h>
#include "dvc1124_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DVC1124-2 public driver interface.
 *
 * Register truth source: DVC1124-2 Reference Manual V1.2.
 * Board truth source: HS-D008 schematic/BOM.
 * DVC11XX DemoCode V1.3 is secondary reference only and must not override the
 * V1.2 register definition when the two differ.
 *
 * Telink B85 i2c_master_init()/i2c_set_id() use the 8-bit transfer address,
 * including the R/W bit position. Addresses stored by this driver are WRITE
 * transfer addresses (LSB=0). READ address = write address | 1.
 */
typedef enum
{
    DVC1124_MODEL_22 = 22,
    DVC1124_MODEL_24 = 24
} dvc1124_model_t;

typedef enum
{
    DVC1124_ADDR_FIXED = 0,      /* normal HS-D008: 0x40(W) / 0x41(R) */
    DVC1124_ADDR_HARDWIRED = 1,  /* cascade address selected by AFE hardware pins */
    DVC1124_ADDR_EXPLICIT = 2    /* MCU target override; hardware must already match */
} dvc1124_addr_mode_t;

typedef struct
{
    dvc1124_model_t model;
    dvc1124_addr_mode_t addr_mode;
    uint8_t hardwire_code;
    uint8_t explicit_write_addr;
    uint8_t cell_count;          /* DVC1124-2 V1.2 valid range: 4..24 */
    uint32_t shunt_uohm;
    uint8_t battery_ntc_gp;
    uint8_t mos_ntc_gp;
} dvc1124_config_t;

typedef struct
{
    uint8_t valid;
    uint8_t alarm;
    uint8_t status;
    uint8_t chip_version;
    uint8_t write_addr;
    uint8_t cell_count;
    uint32_t sample_tick_32k;    /* acquisition time, modulo 32-bit SDK 32k clock */
    int32_t raw_current_ma;     /* Before software factory calibration. */
    int32_t current_ma;          /* positive = discharge, negative = charge */
    uint32_t vtop_mv;
    uint32_t pack_mv;
    uint32_t load_mv;
    uint16_t cell_mv[24];
    uint16_t gp_code[6];
    uint32_t ntc_res_ohm[6];
    int16_t die_temp_x10;        /* signed degree C x 10 */
    uint16_t rpu_ohm;
} dvc1124_snapshot_t;

typedef enum
{
    DVC1124_BOOT_ZERO_NOT_ATTEMPTED = 0u,
    DVC1124_BOOT_ZERO_IN_PROGRESS = 1u,
    DVC1124_BOOT_ZERO_VALID = 2u,
    DVC1124_BOOT_ZERO_DISABLED = 3u,
    DVC1124_BOOT_ZERO_FET_IO_ERROR = 4u,
    DVC1124_BOOT_ZERO_FET_ACTIVE = 5u,
    DVC1124_BOOT_ZERO_CAMZ_ERROR = 6u,
    DVC1124_BOOT_ZERO_SAMPLE_IO_ERROR = 7u,
    DVC1124_BOOT_ZERO_OUT_OF_RANGE = 8u,
    DVC1124_BOOT_ZERO_UNSTABLE = 9u
} dvc1124_boot_zero_status_t;

typedef struct
{
    dvc1124_boot_zero_status_t status;
    uint8_t sample_count;
    int32_t learned_offset_ma;      /* residual after persistent offset/gain */
    int32_t raw_sample1_ma;         /* before persistent factory calibration */
    int32_t raw_sample2_ma;
    int32_t calibrated_sample1_ma;  /* persistent offset/gain applied */
    int32_t calibrated_sample2_ma;
    uint16_t spread_ma;
} dvc1124_boot_zero_diag_t;

typedef enum
{
    DVC1124_OPENWIRE_IDLE = 0u,
    DVC1124_OPENWIRE_WAITING = 1u,
    DVC1124_OPENWIRE_READY = 2u,
    DVC1124_OPENWIRE_ERROR = 3u,
} dvc1124_openwire_state_t;

typedef struct
{
    dvc1124_openwire_state_t state;
    uint8_t valid;
    uint8_t cell_count;
    uint16_t cell_mv[24]; /* DVC1124 physical maximum; macro is declared below this public type. */
    uint32_t pack_mv;
} dvc1124_openwire_result_t;

/*
 * Named operating configuration. This is transport-neutral and intentionally
 * uses semantic fields instead of raw magic register bytes.
 *
 * Protection thresholds remain in the BMS parameter layer (g_tParam) for now;
 * this structure covers DVC operating/configuration fields that used to be
 * hidden in hard-coded register values.
 */
typedef struct
{
    uint8_t high_side_fet_mask;          /* 0 allow high-side outputs, 1 mask */
    uint8_t cadc_work_enable;            /* CAEW */
    uint8_t current_wake_enable;         /* CAES */
    dvc1124_cc1_work_time_t cc1_work_time;
    dvc1124_cc1_sleep_wake_time_t cc1_sleep_wake_time;

    dvc1124_cp_voltage_t charge_pump_voltage;
    uint8_t cell_measurement_mask;       /* CMM */
    uint8_t cell_voltage_signed;         /* CVS: 0 unsigned/100uV, 1 signed/200uV */

    uint8_t vadc_enable;
    uint8_t vadc_sync_with_cc2;
    dvc1124_vadc_period_t vadc_period;
    dvc1124_vadc_time_t vadc_time;

    dvc1124_gp14_mode_t gp1_mode;
    dvc1124_gp236_mode_t gp2_mode;
    dvc1124_gp236_mode_t gp3_mode;
    dvc1124_gp14_mode_t gp4_mode;
    dvc1124_gp236_mode_t gp5_mode;
    dvc1124_gp236_mode_t gp6_mode;

    uint8_t v3p3_sleep_enable;
    uint8_t v3p3_work_enable;
    uint8_t v3p3_timeout_restart;
    dvc1124_i2c_wdt_code_t i2c_watchdog;
    dvc1124_timed_wake_t timed_wake;
    uint8_t interrupt_mask;
} dvc1124_operating_config_t;

typedef enum
{
    DVC1124_REG_READ_SAFE = 0u,
    DVC1124_REG_READ_CLEAR = 1u
} dvc1124_reg_read_effect_t;

#define DVC1124_FIXED_WRITE_ADDR             0x40u
#define DVC1124_HARDWIRE_BASE_WRITE_ADDR     0xC0u
#define DVC1124_MAX_REGISTER                 DVC1124_REG_MAX
#define DVC1124_MIN_CELLS                    4u
#define DVC1124_MAX_CELLS                    24u
#define DVC1124_MAX_GP                       6u

/* Product/board defaults live in one project-owned file. */
#include "dvc1124_project_config.h"

#ifndef DVC1124_DEFAULT_MODEL
#define DVC1124_DEFAULT_MODEL                DVC1124_MODEL_22
#endif
#ifndef DVC1124_DEFAULT_ADDR_MODE
#define DVC1124_DEFAULT_ADDR_MODE            DVC1124_ADDR_FIXED
#endif
#ifndef DVC1124_DEFAULT_HARDWIRE_CODE
#define DVC1124_DEFAULT_HARDWIRE_CODE        0u
#endif
#ifndef DVC1124_DEFAULT_EXPLICIT_WRITE_ADDR
#define DVC1124_DEFAULT_EXPLICIT_WRITE_ADDR  DVC1124_FIXED_WRITE_ADDR
#endif
#ifndef DVC1124_DEFAULT_CELL_COUNT
#define DVC1124_DEFAULT_CELL_COUNT           24u
#endif
#ifndef DVC1124_DEFAULT_SHUNT_UOHM
#define DVC1124_DEFAULT_SHUNT_UOHM           200u
#endif
#ifndef DVC1124_DEFAULT_BATTERY_NTC_GP
#define DVC1124_DEFAULT_BATTERY_NTC_GP       4u
#endif
#ifndef DVC1124_DEFAULT_MOS_NTC_GP
#define DVC1124_DEFAULT_MOS_NTC_GP           1u
#endif

uint8_t DVC1124_ResolveWriteAddress(dvc1124_model_t model,
                                    dvc1124_addr_mode_t mode,
                                    uint8_t hardwire_code,
                                    uint8_t explicit_write_addr,
                                    uint8_t *write_addr);
uint8_t DVC1124_SetCellCount(uint8_t cell_count);
void DVC1124_GetConfig(dvc1124_config_t *config);
void DVC1124_GetSnapshot(dvc1124_snapshot_t *snapshot);
uint8_t DVC1124_GetWriteAddress(void);

uint8_t DVC1124_ReadRegisters(uint8_t reg, uint8_t *data, uint8_t len);
uint8_t DVC1124_WriteRegisters(uint8_t reg, const uint8_t *data, uint8_t len);
uint8_t DVC1124_SetMosState(uint8_t charge_on, uint8_t discharge_on);
void DVC1124_SetOutputEnabled(uint8_t enabled);
uint8_t DVC1124_SetBalanceMask(uint32_t cell_mask);
void DVC1124_BalanceService(uint8_t allow_refresh);
uint8_t DVC1124_StartOpenWireCheck(void);
uint8_t DVC1124_OpenWireBegin(void);
void DVC1124_OpenWirePoll(void);
void DVC1124_OpenWireGetResult(dvc1124_openwire_result_t *result);
void DVC1124_OpenWireReset(void);
uint8_t DVC1124_SetShortCircuitProtection(uint16_t threshold_mv, uint16_t delay_us);

/*
 * 0x76 mixes an RC event flag (COTF) with RW threshold bits. Generic RMW must
 * not be used because the mandatory read clears COTF. These APIs preserve a
 * software sticky copy of every COTF observed while reading/writing threshold.
 */
uint8_t DVC1124_ReadCoreOtThresholdCode(uint8_t *threshold_code);
uint8_t DVC1124_SetCoreOtThresholdCode(uint8_t threshold_code);
uint8_t DVC1124_GetCoreOtEventLatched(void);
void DVC1124_ClearCoreOtEventLatched(void);

/* W0C and self-clearing operations implemented by dvc1124_special.c. */
uint8_t DVC1124_ClearAlarmFlags(uint8_t flag_mask);
uint8_t DVC1124_StartCadcCalibration(void);
uint8_t DVC1124_BootCurrentZeroCalibrate(void);
void DVC1124_GetBootCurrentZeroDiag(dvc1124_boot_zero_diag_t *diag);

/*
 * Register read-side-effect metadata from Reference Manual V1.2.
 *
 * 0x01: VADF/CC1F/CC2F are RC, therefore reading the byte consumes flags.
 * 0x76: COTF is RC, therefore reading the byte consumes the hardware flag.
 */
static inline dvc1124_reg_read_effect_t DVC1124_RegReadEffect(uint8_t reg)
{
    if ((reg == DVC1124_REG_STATUS) || (reg == DVC1124_REG_CORE_OT))
        return DVC1124_REG_READ_CLEAR;
    return DVC1124_REG_READ_SAFE;
}

static inline uint8_t DVC1124_RegReadHasSideEffect(uint8_t reg)
{
    return (DVC1124_RegReadEffect(reg) != DVC1124_REG_READ_SAFE) ? 1u : 0u;
}

/* Alarm is W0C and STATUS contains commands; CORE_OT is RC+RW. */
static inline uint8_t DVC1124_RegGenericRmwAllowed(uint8_t reg)
{
    if ((reg == DVC1124_REG_ALARM) ||
        (reg == DVC1124_REG_STATUS) ||
        (reg == DVC1124_REG_CORE_OT))
        return 0u;
    return 1u;
}

/*
 * Safe field/register configuration helpers.
 *
 * They intentionally reject registers with destructive-read/special-write
 * semantics. Alarm W0C, STATUS commands, CORE_OT RC+RW and self-clearing
 * commands must use dedicated APIs.
 */
static inline uint8_t DVC1124_WriteRegisterSafe(uint8_t reg, uint8_t requested)
{
    uint8_t current;
    uint8_t target;
    uint8_t verify;
    uint8_t mask = DVC1124_RegDocumentedWriteMask(reg);

    if ((reg > DVC1124_MAX_REGISTER) || (mask == 0u)) return 0u;
    if (!DVC1124_RegGenericRmwAllowed(reg)) return 0u;
    if (DVC1124_RegReadHasSideEffect(reg)) return 0u;
    if (!DVC1124_ReadRegisters(reg, &current, 1u)) return 0u;

    target = (uint8_t)((current & (uint8_t)~mask) | (requested & mask));
    if (!DVC1124_WriteRegisters(reg, &target, 1u)) return 0u;
    if (!DVC1124_ReadRegisters(reg, &verify, 1u)) return 0u;

    return ((verify & mask) == (target & mask)) ? 1u : 0u;
}

static inline uint8_t DVC1124_ReadRegisterField(uint8_t reg,
                                                uint8_t mask,
                                                uint8_t shift,
                                                uint8_t *value)
{
    uint8_t raw;

    if ((value == 0) || (mask == 0u) || (reg > DVC1124_MAX_REGISTER)) return 0u;
    if (DVC1124_RegReadHasSideEffect(reg)) return 0u;
    if (!DVC1124_ReadRegisters(reg, &raw, 1u)) return 0u;
    *value = DVC1124_FIELD_GET(mask, shift, raw);
    return 1u;
}

static inline uint8_t DVC1124_WriteRegisterFieldSafe(uint8_t reg,
                                                      uint8_t mask,
                                                      uint8_t shift,
                                                      uint8_t value)
{
    uint8_t current;
    uint8_t target;
    uint8_t verify;
    uint8_t owned = DVC1124_RegDocumentedWriteMask(reg);
    uint8_t field_max;

    if ((reg > DVC1124_MAX_REGISTER) || (mask == 0u) || (shift >= 8u)) return 0u;
    if ((mask & owned) != mask) return 0u;
    if (!DVC1124_RegGenericRmwAllowed(reg)) return 0u;
    if (DVC1124_RegReadHasSideEffect(reg)) return 0u;

    /* FIELD_PREP masks the value, so validate before encoding to avoid silent truncation. */
    field_max = (uint8_t)(mask >> shift);
    if ((field_max == 0u) || (value > field_max)) return 0u;

    if (!DVC1124_ReadRegisters(reg, &current, 1u)) return 0u;

    target = (uint8_t)((current & (uint8_t)~mask) |
                       DVC1124_FIELD_PREP(mask, shift, value));
    if (!DVC1124_WriteRegisters(reg, &target, 1u)) return 0u;
    if (!DVC1124_ReadRegisters(reg, &verify, 1u)) return 0u;

    return ((verify & mask) == (target & mask)) ? 1u : 0u;
}

/*
 * Operating configuration helpers. They expose readable semantic fields for
 * board/application code and are designed to be reused by BLE/UART services.
 */

uint8_t DVC1124_ApplyOperatingConfig(const dvc1124_operating_config_t *cfg);

/* DVC adapter entry points; application code uses bms_afe.h. */
void DVC1124_App_AFEGet(void);
void DVC1124_BmsApp_AFEGet(void);
void DVC1124_AFE_Reset(void);
uint8_t DVC1124_AFE_IsReady(void); /* legacy convention: 0 = ready */
uint8_t DVC1124_AFE_Sleep(void);
void DVC1124_UpdataAfeConfig(void);
uint8_t DVC1124_ApplyProtectionConfig(void);

/* Shared exact encoders for the safe baseline and full project config phases. */
uint8_t DVC1124_EncodeCurrentWake(uint16_t threshold_uv, uint8_t *code);
uint8_t DVC1124_EncodeBodyDiode(uint16_t threshold_uv, uint8_t *code);
uint8_t DVC1124_EncodeI2cWatchdog(uint8_t seconds, dvc1124_i2c_wdt_code_t *code);

#ifdef __cplusplus
}
#endif

#endif /* DVC1124_H_ */
