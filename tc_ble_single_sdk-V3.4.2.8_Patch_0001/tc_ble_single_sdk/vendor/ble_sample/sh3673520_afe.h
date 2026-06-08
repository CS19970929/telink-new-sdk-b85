#pragma once

#include "conf.h"
#include "sci_upper.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH3673520_MODEL_WORD              0x3520u
#define SH3673520_MAX_CELL_COUNT          20u
#define SH3673520_PARAM_REG_BASE          0x2200u
#define SH3673520_PARAM_WORD_COUNT        31u
#define SH3673520_PARAM_STATUS_WORD_COUNT (SH3673520_PARAM_WORD_COUNT + 1u)

#define SH3673520_PARAM_MODEL             0u
#define SH3673520_PARAM_SERIES_COUNT      1u
#define SH3673520_PARAM_RSENSE_UOHM       2u
#define SH3673520_PARAM_LAST_STATUS       3u
#define SH3673520_PARAM_FLAG1             4u
#define SH3673520_PARAM_FLAG2             5u
#define SH3673520_PARAM_FLAG3             6u
#define SH3673520_PARAM_RESERVED          7u

#define SH3673520_CFG_REG_START           0x41u
#define SH3673520_CFG_REG_END             0x57u
#define SH3673520_CFG_REG_COUNT           (SH3673520_CFG_REG_END - SH3673520_CFG_REG_START + 1u)
#define SH3673520_PARAM_CFG_BASE          8u
#define SH3673520_PARAM_APPLY_STATUS      SH3673520_PARAM_WORD_COUNT

#define SH3673520_STATUS_OK               0x0000u
#define SH3673520_STATUS_FLASH_FAIL       0x0001u
#define SH3673520_STATUS_SPI_FAIL         0x0002u
#define SH3673520_STATUS_INVALID_WRITE    0x0004u
#define SH3673520_STATUS_READ_FAIL        0x0008u
#define SH3673520_STATUS_CONTEXT_SHIFT    8u

#define SH3673520_BIT(n)                  ((u8)(1u << (n)))

#define SH3673520_REG_SCONF1              0x40u
#define SH3673520_REG_SCONF2              0x41u
#define SH3673520_REG_SCONF3              0x42u
#define SH3673520_REG_SCONF4              0x43u
#define SH3673520_REG_SCONF5              0x44u
#define SH3673520_REG_SCONF6              0x45u
#define SH3673520_REG_SCONF7              0x46u
#define SH3673520_REG_OWV_ALARMH          0x47u
#define SH3673520_REG_ALARML              0x48u
#define SH3673520_REG_OVT_OVH             0x49u
#define SH3673520_REG_OVL                 0x4Au
#define SH3673520_REG_UVT_UVH             0x4Bu
#define SH3673520_REG_UVL                 0x4Cu
#define SH3673520_REG_OCD1                0x4Du
#define SH3673520_REG_OCD2                0x4Eu
#define SH3673520_REG_SCV_SCT             0x4Fu
#define SH3673520_REG_OCC                 0x50u
#define SH3673520_REG_OTC                 0x51u
#define SH3673520_REG_OTD                 0x52u
#define SH3673520_REG_UTC                 0x53u
#define SH3673520_REG_UTD                 0x54u
#define SH3673520_REG_BALANCEH            0x55u
#define SH3673520_REG_BALANCEM            0x56u
#define SH3673520_REG_BALANCEL            0x57u
#define SH3673520_REG_FLAG1               0x58u
#define SH3673520_REG_FLAG2               0x59u
#define SH3673520_REG_FLAG3               0x5Au
#define SH3673520_REG_BSTATUS1            0x5Bu
#define SH3673520_REG_BSTATUS2            0x5Cu
#define SH3673520_REG_TEMP1               0x5Du
#define SH3673520_REG_TEMPI               0x65u
#define SH3673520_REG_CUR                 0x67u
#define SH3673520_REG_CELL1               0x69u
#define SH3673520_REG_CADCD               0x91u
#define SH3673520_REG_VTOP                0x93u
#define SH3673520_REG_CPLUS               0x95u
#define SH3673520_REG_OWD                 0x97u

#define SH3673520_SCONF2_LTCLR            SH3673520_BIT(7)
#define SH3673520_SCONF2_PD_EN            SH3673520_BIT(6)
#define SH3673520_SCONF2_PD_CTL           SH3673520_BIT(5)
#define SH3673520_SCONF2_PUMP_EN          SH3673520_BIT(4)
#define SH3673520_SCONF2_PDSG_CTL         SH3673520_BIT(3)
#define SH3673520_SCONF2_PDSGMOS          SH3673520_BIT(2)
#define SH3673520_SCONF2_DSGMOS           SH3673520_BIT(1)
#define SH3673520_SCONF2_CHGMOS           SH3673520_BIT(0)

#define SH3673520_SCONF3_CGR_WK           SH3673520_BIT(6)
#define SH3673520_SCONF3_OWD_EN           SH3673520_BIT(1)
#define SH3673520_SCONF3_OWD_TRG          SH3673520_BIT(0)

#define SH3673520_SCONF5_MOS_EN           SH3673520_BIT(5)
#define SH3673520_SCONF5_OCC_EN           SH3673520_BIT(4)
#define SH3673520_SCONF5_CADC_EN          SH3673520_BIT(3)
#define SH3673520_SCONF5_WDT_EN           SH3673520_BIT(2)

#define SH3673520_SCONF6_TS4_EN           SH3673520_BIT(7)
#define SH3673520_SCONF6_TS3_EN           SH3673520_BIT(6)
#define SH3673520_SCONF6_TS2_EN           SH3673520_BIT(5)
#define SH3673520_SCONF6_TS1_EN           SH3673520_BIT(4)
#define SH3673520_SCONF6_SC_EN            SH3673520_BIT(3)
#define SH3673520_SCONF6_OCD_EN           SH3673520_BIT(2)
#define SH3673520_SCONF6_UV_EN            SH3673520_BIT(1)
#define SH3673520_SCONF6_OV_EN            SH3673520_BIT(0)

#define SH3673520_FLAG1_RST1              SH3673520_BIT(7)
#define SH3673520_FLAG1_WK                SH3673520_BIT(6)
#define SH3673520_FLAG1_OCC               SH3673520_BIT(5)
#define SH3673520_FLAG1_SC                SH3673520_BIT(4)
#define SH3673520_FLAG1_OCD2              SH3673520_BIT(3)
#define SH3673520_FLAG1_OCD1              SH3673520_BIT(2)
#define SH3673520_FLAG1_UV                SH3673520_BIT(1)
#define SH3673520_FLAG1_OV                SH3673520_BIT(0)

#define SH3673520_FLAG2_OTD               SH3673520_BIT(7)
#define SH3673520_FLAG2_UTD               SH3673520_BIT(6)
#define SH3673520_FLAG2_OTC               SH3673520_BIT(5)
#define SH3673520_FLAG2_UTC               SH3673520_BIT(4)
#define SH3673520_FLAG2_RST2              SH3673520_BIT(3)
#define SH3673520_FLAG2_WDT               SH3673520_BIT(2)
#define SH3673520_FLAG2_VADC              SH3673520_BIT(1)
#define SH3673520_FLAG2_CADC              SH3673520_BIT(0)

#define SH3673520_FLAG3_OWD_IND           SH3673520_BIT(1)
#define SH3673520_FLAG3_OWD               SH3673520_BIT(0)

#define SH3673520_DEFAULT_SCONF2          0x50u
#define SH3673520_DEFAULT_SCONF3          0x00u
#define SH3673520_DEFAULT_SCONF4          ((u8)((0x03u << 5) | (SeriesNum & 0x1Fu)))
#define SH3673520_DEFAULT_SCONF5          0x38u
#define SH3673520_DEFAULT_SCONF6_BOARD    (SH3673520_SCONF6_TS2_EN | SH3673520_SCONF6_TS1_EN | SH3673520_SCONF6_SC_EN | SH3673520_SCONF6_OCD_EN | SH3673520_SCONF6_UV_EN | SH3673520_SCONF6_OV_EN)
#define SH3673520_DEFAULT_SCONF7          0x04u
#define SH3673520_DEFAULT_OWV_ALARMH      0x57u
#define SH3673520_DEFAULT_ALARML          0xFFu
#define SH3673520_DEFAULT_OVT_OVH         0x33u
#define SH3673520_DEFAULT_OVL             0x48u
#define SH3673520_DEFAULT_UVT_UVH         0x22u
#define SH3673520_DEFAULT_UVL             0x1Cu
#define SH3673520_DEFAULT_OCD1            0x39u
#define SH3673520_DEFAULT_OCD2            0x39u
#define SH3673520_DEFAULT_SCV_SCT         0x07u
#define SH3673520_DEFAULT_OCC             0x6Fu
#define SH3673520_DEFAULT_OTC             0x96u
#define SH3673520_DEFAULT_OTD             0x76u
#define SH3673520_DEFAULT_UTC             0x76u
#define SH3673520_DEFAULT_UTD             0x9Eu

typedef struct {
    u16 cell_mv[SH3673520_MAX_CELL_COUNT];
    u16 temp_01c_offset[4];
    u32 pack_mv;
    u16 cplus_mv;
    u16 current_raw;
    u16 charge_ma;
    u16 discharge_ma;
    u8 status1;
    u8 status2;
    u8 flag1;
    u8 flag2;
    u8 flag3;
} sh3673520_sample_t;

void sh3673520_bus_init(void);
void sh3673520_param_load(void);
u8 sh3673520_reset(void);
u8 sh3673520_is_ready(void);
u8 sh3673520_apply_params(void);
void sh3673520_sleep(void);
u8 sh3673520_read_sample(sh3673520_sample_t *sample);
void sh3673520_publish_to_cell_info(const sh3673520_sample_t *sample, struct stCell_Info *report);

u16 sh3673520_param_read_word(u16 index);
int sh3673520_param_write_word(u16 index, u16 value);
int sh3673520_param_commit(void);
int sh3673520_param_is_writable(u16 index);
u16 sh3673520_get_apply_status(void);
u32 sh3673520_get_last_pack_mv(void);
int sh3673520_get_last_current_ma(void);

#ifdef __cplusplus
}
#endif
