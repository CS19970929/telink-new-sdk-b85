#ifndef DVC1124_H_
#define DVC1124_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DVC1124-2 I2C address model.
 *
 * Telink's i2c_master_init()/i2c_set_id() use the 8-bit transfer address,
 * including the R/W bit position. Therefore all addresses in this driver are
 * the WRITE transfer address (LSB = 0). The READ transfer address is addr | 1.
 */
typedef enum
{
    DVC1124_MODEL_22 = 22,
    DVC1124_MODEL_24 = 24
} dvc1124_model_t;

typedef enum
{
    DVC1124_ADDR_FIXED = 0,      /* normal application: 0x40(W) / 0x41(R) */
    DVC1124_ADDR_HARDWIRED = 1,  /* cascade application: 0xC0 | strap_code << 1 */
    DVC1124_ADDR_EXPLICIT = 2    /* MCU target address override; hardware must match */
} dvc1124_addr_mode_t;

typedef struct
{
    dvc1124_model_t model;
    dvc1124_addr_mode_t addr_mode;
    uint8_t hardwire_code;
    uint8_t explicit_write_addr;
    uint8_t cell_count;
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
    int32_t current_ma;          /* positive = discharge, negative = charge */
    uint32_t vtop_mv;
    uint32_t pack_mv;
    uint32_t load_mv;
    uint16_t cell_mv[24];
    uint16_t gp_code[6];
    uint32_t ntc_res_ohm[6];
    int16_t die_temp_x10;        /* signed, degree C x 10 */
    uint16_t rpu_ohm;
} dvc1124_snapshot_t;

#define DVC1124_FIXED_WRITE_ADDR             0x40u
#define DVC1124_HARDWIRE_BASE_WRITE_ADDR     0xC0u
#define DVC1124_MAX_REGISTER                 0x90u
#define DVC1124_MAX_CELLS                    24u
#define DVC1124_MAX_GP                       6u

#ifndef DVC1124_DEFAULT_CELL_COUNT
#define DVC1124_DEFAULT_CELL_COUNT           24u
#endif

#ifndef DVC1124_DEFAULT_SHUNT_UOHM
/* HS-D008: ten 2 mOhm current shunts in parallel => 0.2 mOhm = 200 uOhm. */
#define DVC1124_DEFAULT_SHUNT_UOHM            200u
#endif

#ifndef DVC1124_DEFAULT_BATTERY_NTC_GP
/* HS-D008 schematic: NTC1 is connected to GP4. */
#define DVC1124_DEFAULT_BATTERY_NTC_GP        4u
#endif

#ifndef DVC1124_DEFAULT_MOS_NTC_GP
/* HS-D008 schematic: NTC2 is connected to GP1. */
#define DVC1124_DEFAULT_MOS_NTC_GP            1u
#endif

#ifndef DVC1124_HW_SCD_THRESHOLD_MV
/* No project-specific short-circuit threshold was provided. 0 keeps SCD disabled. */
#define DVC1124_HW_SCD_THRESHOLD_MV           0u
#endif

#ifndef DVC1124_HW_SCD_DELAY_US
#define DVC1124_HW_SCD_DELAY_US               0u
#endif

/*
 * Virtual pins used only by the legacy app.c compatibility layer. They must
 * never reach the Telink GPIO register helpers. This prevents the old SH367309
 * board mapping from driving real HS-D008 pins such as PA1(MCC-EN-HT).
 */
#define DVC1124_VPIN_AFE_CTL                  0x7F010001u
#define DVC1124_VPIN_LEGACY_MCC               0x7F010002u
#define DVC1124_VPIN_ADC_BAT                  0x7F010003u
#define DVC1124_VPIN_ADC_PACK                 0x7F010004u
#define DVC1124_VPIN_ADC_MOS                  0x7F010005u
#define DVC1124_VPIN_NOOP0                    0x7F010006u
#define DVC1124_VPIN_NOOP1                    0x7F010007u

uint8_t DVC1124_ResolveWriteAddress(dvc1124_model_t model,
                                    dvc1124_addr_mode_t mode,
                                    uint8_t hardwire_code,
                                    uint8_t explicit_write_addr,
                                    uint8_t *write_addr);
uint8_t DVC1124_SetAddressConfig(dvc1124_model_t model,
                                 dvc1124_addr_mode_t mode,
                                 uint8_t hardwire_code,
                                 uint8_t explicit_write_addr);
uint8_t DVC1124_SetCellCount(uint8_t cell_count);
uint8_t DVC1124_SetShuntUohm(uint32_t shunt_uohm);
void DVC1124_GetConfig(dvc1124_config_t *config);
void DVC1124_GetSnapshot(dvc1124_snapshot_t *snapshot);
uint8_t DVC1124_GetWriteAddress(void);

uint8_t DVC1124_ReadRegisters(uint8_t reg, uint8_t *data, uint8_t len);
uint8_t DVC1124_WriteRegisters(uint8_t reg, const uint8_t *data, uint8_t len);
uint8_t DVC1124_SetMosState(uint8_t charge_on, uint8_t discharge_on);
uint8_t DVC1124_SetBalanceMask(uint32_t cell_mask);
uint8_t DVC1124_StartOpenWireCheck(void);
uint8_t DVC1124_SetShortCircuitProtection(uint16_t threshold_mv, uint16_t delay_us);

/* Compatibility entry points used by app_config.h without rewriting app.c. */
void DVC1124_App_AFEGet(void);
void DVC1124_AFE_Reset(void);
uint8_t DVC1124_AFE_IsReady(void);
void DVC1124_AFE_Sleep(void);
void DVC1124_UpdataAfeConfig(void);
uint8_t DVC1124_CompatMTPWrite(uint8_t wr_addr, uint8_t length, const uint8_t *wr_buf);

void DVC1124_CompatAdcBaseInit(unsigned int pin);
unsigned int DVC1124_CompatAdcSample(void);
void DVC1124_CompatGpioSetFunc(unsigned int pin, unsigned int func);
void DVC1124_CompatGpioSetInputEn(unsigned int pin, unsigned int value);
void DVC1124_CompatGpioSetOutputEn(unsigned int pin, unsigned int value);
void DVC1124_CompatGpioWrite(unsigned int pin, unsigned int value);

#ifdef __cplusplus
}
#endif

#endif /* DVC1124_H_ */
