/* D011/D013/D014 共同的 SH 板级接线与独立 AFE 默认值。
 * 产品文件先明确串数、Rsense 和功能能力；本文件不提供能力回退值。
 * 寄存器组合在 sh3673510_project_config.h。电压默认来自电池类型表，
 * 运行时软件保护与 AFE 参数仍独立保存。 */
#pragma once

#ifndef BMS_BOARD_DEBUG_LED_ENABLE
#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD
#define BMS_BOARD_DEBUG_LED_ENABLE                   0u
#else
#define BMS_BOARD_DEBUG_LED_ENABLE                   1u
#endif
#endif
#define SH3673510_BOARD_NTC_NOMINAL_OHM 10000UL
#define SH3673510_BOARD_SPI_GROUP               SH3673520_SPI_GROUP_B6_B7_D2_D7
#define SH3673510_BOARD_BAT_NTC1_INDEX           0u  /* TS1, 10K-3435 */
#define SH3673510_BOARD_BAT_NTC2_INDEX           1u  /* TS2, 10K-3435 */
#define SH3673510_BOARD_HEATER_NTC_INDEX         2u  /*
 * TS3 加热传感器角色，仅支持时使用。
 */
#define SH3673510_BOARD_MOS_NTC_INDEX            3u  /*
 * TS4 MOS 传感器角色，仅支持时使用。
 */
#define SH3673510_BOARD_SCONF1_BOOT_VALUE         SH3673520_SCONF1_NORMAL
#define SH3673510_BOARD_LTCLR                      0u /* 仅运行时标志清除门控。 */
#define SH3673510_BOARD_PD_EN                      SH3673510_HW_PROTECT_ENABLE /*
 * 低电压自主 Powerdown 属于硬件保护。
 */
#define SH3673510_BOARD_PD_CTL                     0u /*
 * 不立即发出 MCU Powerdown 命令。
 */
#define SH3673510_BOARD_PUMP_EN                    1u
#define SH3673510_BOARD_PDSG_CTL                   0u
#define SH3673510_BOARD_PDSGMOS                    0u /*
 * 预放电由 MCU 强制控制，静态关闭。
 */
#define SH3673510_BOARD_DSGMOS_BOOT                0u /* 有效样本后运行时控制。 */
#define SH3673510_BOARD_CHGMOS_BOOT                0u /* 有效样本后运行时控制。 */
#define SH3673510_BOARD_CGR_WK                     1u
#define SH3673510_BOARD_LD_WK_CODE                 SH3673520_SCONF3_LD_WK_OFF
#define SH3673510_BOARD_CRLD_EN_CODE               SH3673520_SCONF3_CRLD_CPLUS_CODE
#define SH3673510_BOARD_OWD_EN                     0u
#define SH3673510_BOARD_OWD_TRG                    0u /*
 * 触发位具有命令语义，静态保持零。
 */
#define SH3673510_BOARD_PDSGT_CODE                 SH3673520_SCONF4_PDSGT_490MS
#define SH3673510_BOARD_MOS_EN                     SH3673510_HW_PROTECT_ENABLE /*
 * 硬件测试隔离 AFE 自主 FET 恢复。
 */
#define SH3673510_BOARD_OCC_EN                     SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_CADC_EN                    1u /* 所有测试模式保留电流采集。 */
#define SH3673510_BOARD_WDT_EN                     SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_WDT_CODE                   SH3673520_SCONF5_WDT_32S_CODE
#define SH3673510_BOARD_TS4_HW_PROTECT_EN           0u /*
 * AFE TS4 共用电池 OTC/UTC 阈值，MOS 独立 OTP 用软件策略。
 */
#define SH3673510_BOARD_TS3_HW_PROTECT_EN           0u /*
 * 独立加热策略，不用公共电池阈值。
 */
#define SH3673510_BOARD_TS2_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_TS1_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_SC_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_OCD_HW_PROTECT_EN           SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_UV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_OV_HW_PROTECT_EN            SH3673510_HW_PROTECT_ENABLE
#define SH3673510_BOARD_RLD                        0u /* 60uA 负载检测上拉。 */
#define SH3673510_BOARD_CADCT_CODE                 SH3673520_SCONF7_CADCT_4S_CODE
#define SH3673510_BOARD_CDV_CODE                   4u /*
 * 手册复位值：742.5uV 状态检测阈值。
 */
#define SH3673510_BOARD_OWV_CODE                    5u /* 960mV 断线阈值。 */
#define SH3673510_BOARD_LOADON_INT                  0u
#define SH3673510_BOARD_LOADOFF_INT                 1u
#define SH3673510_BOARD_VADC_INT                    1u
#define SH3673510_BOARD_CADC_INT                    1u
#define SH3673510_BOARD_WK_INT                      1u
#define SH3673510_BOARD_WDT_INT                     1u
#define SH3673510_BOARD_OWD_INT                     1u
#define SH3673510_BOARD_TEMP_INT                    1u
#define SH3673510_BOARD_OCC_INT                     1u
#define SH3673510_BOARD_OCD_INT                     1u
#define SH3673510_BOARD_UV_INT                      1u
#define SH3673510_BOARD_OV_INT                      1u
#define SH3673510_BOARD_SC_MULTIPLIER_CODE           0u
#define SH3673510_BOARD_SC_DELAY_CODE                7u
#define SH3673510_HW_DEFAULT_COV_MV                BMS_DEFAULT_CELL_OVP_MV
#define SH3673510_HW_DEFAULT_COV_DELAY_MS          1000u
#define SH3673510_HW_DEFAULT_COV_RECOVER_MV        BMS_DEFAULT_CELL_OVP_RECOVER_MV
#define SH3673510_HW_DEFAULT_COV_RECOVER_MS        1000u
#define SH3673510_HW_DEFAULT_CUV_MV                BMS_DEFAULT_CUV3_MV
#define SH3673510_HW_DEFAULT_CUV_DELAY_MS          10000u
#define SH3673510_HW_DEFAULT_CUV_RECOVER_MV        BMS_DEFAULT_CELL_UVP_RECOVER_MV
#define SH3673510_HW_DEFAULT_CUV_RECOVER_MS        10000u
#define SH3673510_HW_DEFAULT_OCD1_A10              100u
#define SH3673510_HW_DEFAULT_OCD1_DELAY_MS         100u
#define SH3673510_HW_DEFAULT_OCD2_A10              150u
#define SH3673510_HW_DEFAULT_OCD2_DELAY_MS         100u
#define SH3673510_HW_DEFAULT_OCD_RECOVER_A10       100u
#define SH3673510_HW_DEFAULT_OCD_RECOVER_MS        2000u
#define SH3673510_HW_DEFAULT_OCC1_A10              100u
#define SH3673510_HW_DEFAULT_OCC1_DELAY_MS         100u
#define SH3673510_HW_DEFAULT_OCC_RECOVER_A10       100u
#define SH3673510_HW_DEFAULT_OCC_RECOVER_MS        100u
#define SH3673510_HW_DEFAULT_SC_A10                300u
#define SH3673510_HW_DEFAULT_SC_DELAY_US           256u
#define SH3673510_HW_DEFAULT_SC_RECOVER_MS         2000u
#define SH3673510_HW_DEFAULT_CHG_OT_X10            950u
#define SH3673510_HW_DEFAULT_CHG_OT_RECOVER_X10    900u
#define SH3673510_HW_DEFAULT_CHG_UT_X10            400u
#define SH3673510_HW_DEFAULT_CHG_UT_RECOVER_X10    430u
#define SH3673510_HW_DEFAULT_DSG_OT_X10            1000u
#define SH3673510_HW_DEFAULT_DSG_OT_RECOVER_X10    900u
#define SH3673510_HW_DEFAULT_DSG_UT_X10            200u
#define SH3673510_HW_DEFAULT_DSG_UT_RECOVER_X10    300u
#define SH3673510_HW_DEFAULT_TEMP_RECOVER_MS       1000u
#define BMS_BOARD_CMNT_EN_PIN                        GPIO_PD4
#define BMS_BOARD_AFE_SCLK_PIN                       GPIO_PD7
#define BMS_BOARD_SWITCH_PIN                         GPIO_PA0
#define BMS_BOARD_RS485_EN_PIN                       GPIO_PA1
#define BMS_BOARD_SWS_PIN                            GPIO_PA7
#define BMS_BOARD_INT_WK_MCU_PIN                     GPIO_PB1
#define BMS_BOARD_AFE_MISO_PIN                       GPIO_PB6
#define BMS_BOARD_AFE_MOSI_PIN                       GPIO_PB7
#define BMS_BOARD_AFE_ALARM_PIN                      GPIO_PC0
#define BMS_BOARD_AFE_RESET_OUT_PIN                  GPIO_PC1
#define BMS_BOARD_SCI1_TX_PIN                        GPIO_PC2
#define BMS_BOARD_SCI1_RX_PIN                        GPIO_PC3
#define BMS_BOARD_DEBUG_LED_PIN                      GPIO_PC4
#define BMS_BOARD_CMNT_WK_PIN                        GPIO_PD3
#define BMS_BOARD_AFE_CS_PIN                         GPIO_PD2

/* 硬件验证前保留固定 UART 低功耗门控。 */
#define SH3673510_FIXED_UART_BLOCKS_PM 1u

