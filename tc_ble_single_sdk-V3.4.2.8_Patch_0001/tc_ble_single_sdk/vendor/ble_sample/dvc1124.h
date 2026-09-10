#ifndef DVC1124_H_
#define DVC1124_H_

#include <stdint.h>

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

#define DVC1124_FIXED_WRITE_ADDR             0x40u
#define DVC1124_HARDWIRE_BASE_WRITE_ADDR     0xC0u
/* V1.2 explicitly lists offset 0x90 as a read-only register. */
#define DVC1124_MAX_REGISTER                 0x90u
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
#define DVC1124_DEFAULT_SHUNT_UOHM            200u
#endif
#ifndef DVC1124_DEFAULT_BATTERY_NTC_GP
#define DVC1124_DEFAULT_BATTERY_NTC_GP        4u
#endif
#ifndef DVC1124_DEFAULT_MOS_NTC_GP
#define DVC1124_DEFAULT_MOS_NTC_GP            1u
#endif

/*
 * Virtual pins exist only for the legacy SH367309 application compatibility
 * layer. They must never reach Telink GPIO register helpers.
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

/* Legacy application compatibility entry points. */
void DVC1124_App_AFEGet(void);
void DVC1124_BmsApp_AFEGet(void);
void DVC1124_AFE_Reset(void);
uint8_t DVC1124_AFE_IsReady(void); /* legacy convention: 0 = ready */
void DVC1124_AFE_Sleep(void);
void DVC1124_UpdataAfeConfig(void);
uint8_t DVC1124_CompatMTPWrite(uint8_t wr_addr, uint8_t length, const uint8_t *wr_buf);
uint8_t DVC1124_BmsCompatMTPWrite(uint8_t wr_addr, uint8_t length, const uint8_t *wr_buf);

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
