#ifndef SH3673520_REG_H
#define SH3673520_REG_H

/*
 * SH36735xx family register/wire-protocol single source of truth.
 * Verified against SH36735XX CV1.0A and SH3673520 STM32 demo V1.3.
 * HS-D011 uses the SH3673510 member; addresses/protocol below are common to
 * the documented family, while the configured cell count is limited by the
 * product profile.
 */

/* SPI protocol */
#define SH3673520_SPI_CMD_WRITE              0x01u
#define SH3673520_SPI_CMD_READ               0x02u
#define SH3673520_SPI_CMD_RESET              0x0Bu
#define SH3673520_SPI_RESET_KEY1             0xBBu
#define SH3673520_SPI_RESET_KEY2             0xCCu
#define SH3673520_SPI_RESPONSE_IDLE          0xFFu
#define SH3673520_SPI_ACK                    0xA5u
#define SH3673520_SPI_NACK                   0xFFu
#define SH3673520_CRC8_POLY                  0x07u
#define SH3673520_CRC8_INIT                  0x00u
#define SH3673520_SPI_MAX_CLOCK_HZ           1000000UL

#define SH3673520_REG_WRITE_MIN              0x40u
#define SH3673520_REG_WRITE_MAX              0x59u
#define SH3673520_REG_READ_MIN               0x40u
#define SH3673520_REG_READ_MAX               0x99u

/* Configuration / protection / status */
#define SH3673520_REG_SCONF1                 0x40u
#define SH3673520_REG_SCONF2                 0x41u
#define SH3673520_REG_SCONF3                 0x42u
#define SH3673520_REG_SCONF4                 0x43u
#define SH3673520_REG_SCONF5                 0x44u
#define SH3673520_REG_SCONF6                 0x45u
#define SH3673520_REG_SCONF7                 0x46u
#define SH3673520_REG_OWV_ALARMH             0x47u
#define SH3673520_REG_ALARML                 0x48u
#define SH3673520_REG_OVT_OVH                0x49u
#define SH3673520_REG_OVL                    0x4Au
#define SH3673520_REG_UVT_UVH                0x4Bu
#define SH3673520_REG_UVL                    0x4Cu
#define SH3673520_REG_OCD1V_OCD1T            0x4Du
#define SH3673520_REG_OCD2V_OCD2T            0x4Eu
#define SH3673520_REG_SCV_SCT                0x4Fu
#define SH3673520_REG_OCCV_OCCT              0x50u
#define SH3673520_REG_OTC                    0x51u
#define SH3673520_REG_OTD                    0x52u
#define SH3673520_REG_UTC                    0x53u
#define SH3673520_REG_UTD                    0x54u
#define SH3673520_REG_BALANCEH               0x55u
#define SH3673520_REG_BALANCEM               0x56u
#define SH3673520_REG_BALANCEL               0x57u
#define SH3673520_REG_FLAG1                  0x58u
#define SH3673520_REG_FLAG2                  0x59u
#define SH3673520_REG_FLAG3                  0x5Au
#define SH3673520_REG_BSTATUS1               0x5Bu
#define SH3673520_REG_BSTATUS2               0x5Cu

/* Measurement registers: high byte first. */
#define SH3673520_REG_TEMP1H                 0x5Du
#define SH3673520_REG_TEMP1L                 0x5Eu
#define SH3673520_REG_TEMP2H                 0x5Fu
#define SH3673520_REG_TEMP2L                 0x60u
#define SH3673520_REG_TEMP3H                 0x61u
#define SH3673520_REG_TEMP3L                 0x62u
#define SH3673520_REG_TEMP4H                 0x63u
#define SH3673520_REG_TEMP4L                 0x64u
#define SH3673520_REG_TEMPIH                 0x65u
#define SH3673520_REG_TEMPIL                 0x66u
#define SH3673520_REG_CURH                   0x67u
#define SH3673520_REG_CURL                   0x68u
#define SH3673520_REG_CELL1H                 0x69u
#define SH3673520_REG_CELL1L                 0x6Au
#define SH3673520_REG_CELL2H                 0x6Bu
#define SH3673520_REG_CELL2L                 0x6Cu
#define SH3673520_REG_CELL3H                 0x6Du
#define SH3673520_REG_CELL3L                 0x6Eu
#define SH3673520_REG_CELL4H                 0x6Fu
#define SH3673520_REG_CELL4L                 0x70u
#define SH3673520_REG_CELL5H                 0x71u
#define SH3673520_REG_CELL5L                 0x72u
#define SH3673520_REG_CELL6H                 0x73u
#define SH3673520_REG_CELL6L                 0x74u
#define SH3673520_REG_CELL7H                 0x75u
#define SH3673520_REG_CELL7L                 0x76u
#define SH3673520_REG_CELL8H                 0x77u
#define SH3673520_REG_CELL8L                 0x78u
#define SH3673520_REG_CELL9H                 0x79u
#define SH3673520_REG_CELL9L                 0x7Au
#define SH3673520_REG_CELL10H                0x7Bu
#define SH3673520_REG_CELL10L                0x7Cu
#define SH3673520_REG_CELL11H                0x7Du
#define SH3673520_REG_CELL11L                0x7Eu
#define SH3673520_REG_CELL12H                0x7Fu
#define SH3673520_REG_CELL12L                0x80u
#define SH3673520_REG_CELL13H                0x81u
#define SH3673520_REG_CELL13L                0x82u
#define SH3673520_REG_CELL14H                0x83u
#define SH3673520_REG_CELL14L                0x84u
#define SH3673520_REG_CELL15H                0x85u
#define SH3673520_REG_CELL15L                0x86u
#define SH3673520_REG_CELL16H                0x87u
#define SH3673520_REG_CELL16L                0x88u
#define SH3673520_REG_CELL17H                0x89u
#define SH3673520_REG_CELL17L                0x8Au
#define SH3673520_REG_CELL18H                0x8Bu
#define SH3673520_REG_CELL18L                0x8Cu
#define SH3673520_REG_CELL19H                0x8Du
#define SH3673520_REG_CELL19L                0x8Eu
#define SH3673520_REG_CELL20H                0x8Fu
#define SH3673520_REG_CELL20L                0x90u
#define SH3673520_REG_CADCDH                 0x91u
#define SH3673520_REG_CADCDL                 0x92u
#define SH3673520_REG_VTOPH                  0x93u
#define SH3673520_REG_VTOPL                  0x94u
#define SH3673520_REG_VCHGRH                 0x95u
#define SH3673520_REG_VCHGRL                 0x96u
#define SH3673520_REG_OWDH                   0x97u
#define SH3673520_REG_OWDM                   0x98u
#define SH3673520_REG_OWDL                   0x99u

/* SCONF1 mode commands. */
#define SH3673520_SCONF1_NORMAL              0x00u
#define SH3673520_SCONF1_IDLE                0x55u
#define SH3673520_SCONF1_SLEEP               0xAAu
#define SH3673520_SCONF1_POWERDOWN           0x33u

/* SCONF2 */
#define SH3673520_SCONF2_LTCLR_MASK          0x80u
#define SH3673520_SCONF2_PD_EN_MASK          0x40u
#define SH3673520_SCONF2_PD_CTL_MASK         0x20u
#define SH3673520_SCONF2_PUMP_EN_MASK        0x10u
#define SH3673520_SCONF2_PDSG_CTL_MASK       0x08u
#define SH3673520_SCONF2_PDSGMOS_MASK        0x04u
#define SH3673520_SCONF2_DSGMOS_MASK         0x02u
#define SH3673520_SCONF2_CHGMOS_MASK         0x01u
#define SH3673520_SCONF2_FET_MASK            0x03u

/* SCONF3 */
#define SH3673520_SCONF3_CGR_WK_MASK         0x40u
#define SH3673520_SCONF3_LD_WK_MASK          0x30u
#define SH3673520_SCONF3_CRLD_EN_MASK        0x0Cu
#define SH3673520_SCONF3_CRLD_CPLUS          0x04u
#define SH3673520_SCONF3_CRLD_LOAD           0x08u
#define SH3673520_SCONF3_OWD_EN_MASK         0x02u
#define SH3673520_SCONF3_OWD_TRG_MASK        0x01u

/* SCONF4 */
#define SH3673520_SCONF4_PDSGT_MASK          0xE0u
#define SH3673520_SCONF4_CELL_COUNT_MASK     0x1Fu

/* SCONF5 */
#define SH3673520_SCONF5_MOS_EN_MASK         0x20u
#define SH3673520_SCONF5_OCC_EN_MASK         0x10u
#define SH3673520_SCONF5_CADC_EN_MASK        0x08u
#define SH3673520_SCONF5_WDT_EN_MASK         0x04u
#define SH3673520_SCONF5_WDT_MASK            0x03u

/* SCONF6 */
#define SH3673520_SCONF6_TS4_EN_MASK         0x80u
#define SH3673520_SCONF6_TS3_EN_MASK         0x40u
#define SH3673520_SCONF6_TS2_EN_MASK         0x20u
#define SH3673520_SCONF6_TS1_EN_MASK         0x10u
#define SH3673520_SCONF6_SC_EN_MASK          0x08u
#define SH3673520_SCONF6_OCD_EN_MASK         0x04u
#define SH3673520_SCONF6_UV_EN_MASK          0x02u
#define SH3673520_SCONF6_OV_EN_MASK          0x01u
#define SH3673520_SCONF6_ALL_PROTECT_MASK    0x0Fu

/* Protection register fields. */
#define SH3673520_OVUV_DELAY_MASK            0x70u
#define SH3673520_OVUV_HI_MASK               0x03u
#define SH3673520_OCD1_DELAY_MASK            0x70u
#define SH3673520_OCD1_VALUE_MASK            0x0Fu
#define SH3673520_OCD2_DELAY_MASK            0xF0u
#define SH3673520_OCD2_VALUE_MASK            0x0Fu
#define SH3673520_SC_VALUE_MASK              0x30u
#define SH3673520_SC_DELAY_MASK              0x0Fu
#define SH3673520_OCC_DELAY_MASK             0xE0u
#define SH3673520_OCC_VALUE_MASK             0x1Fu

/* FLAG1: W0C when LTCLR=1. */
#define SH3673520_FLAG1_RST1_MASK            0x80u
#define SH3673520_FLAG1_WK_MASK              0x40u
#define SH3673520_FLAG1_OCC_MASK             0x20u
#define SH3673520_FLAG1_SC_MASK              0x10u
#define SH3673520_FLAG1_OCD2_MASK            0x08u
#define SH3673520_FLAG1_OCD1_MASK            0x04u
#define SH3673520_FLAG1_UV_MASK              0x02u
#define SH3673520_FLAG1_OV_MASK              0x01u

/* FLAG2: VADC_FLG/CADC_FLG are read-clear. */
#define SH3673520_FLAG2_OTD_MASK             0x80u
#define SH3673520_FLAG2_UTD_MASK             0x40u
#define SH3673520_FLAG2_OTC_MASK             0x20u
#define SH3673520_FLAG2_UTC_MASK             0x10u
#define SH3673520_FLAG2_RST2_MASK            0x08u
#define SH3673520_FLAG2_WDT_MASK             0x04u
#define SH3673520_FLAG2_VADC_MASK            0x02u
#define SH3673520_FLAG2_CADC_MASK            0x01u

/* BSTATUS1 */
#define SH3673520_BSTATUS1_E2P_ERR_MASK      0x40u
#define SH3673520_BSTATUS1_HDSG_FET_MASK     0x20u
#define SH3673520_BSTATUS1_HCHG_FET_MASK     0x10u
#define SH3673520_BSTATUS1_PDSG_FET_MASK     0x04u
#define SH3673520_BSTATUS1_DSG_FET_MASK      0x02u
#define SH3673520_BSTATUS1_CHG_FET_MASK      0x01u

/* BSTATUS2 */
#define SH3673520_BSTATUS2_CHGING_MASK       0x80u
#define SH3673520_BSTATUS2_DSGING_MASK       0x40u
#define SH3673520_BSTATUS2_SLEEP_MASK        0x20u
#define SH3673520_BSTATUS2_IDLE_MASK         0x10u
#define SH3673520_BSTATUS2_BAL_MASK          0x08u
#define SH3673520_BSTATUS2_LOADON_MASK       0x02u
#define SH3673520_BSTATUS2_LOADOFF_MASK      0x01u

#define SH3673520_MAX_CELLS                  20u
#define SH3673520_MIN_CELLS                  4u
#define SH3673520_EXTERNAL_TEMP_COUNT        4u
#define SH3673510_MAX_CELLS                  10u

#endif /* SH3673520_REG_H */
