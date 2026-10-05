/* 文件功能：D008 固定板级接线、AFE 工作模式及故障安全配置。 */
#ifndef DVC1124_PROJECT_CONFIG_H_
#define DVC1124_PROJECT_CONFIG_H_

#include "d008_product_profile.h"

/*
 * HS-D008 / DVC1124-2 固定板级及故障安全配置。依据优先级：
 * DVC1124-2 Reference Manual V1.2（寄存器/编码）、HS-D008 原理图/BOM（接线/装配）、
 * D008 产品配置（串数/化学体系）、本文件（固定策略）、运行 Flash 参数（仅保护阈值）
 * 、DVC11XX DemoCode V1.3（辅助时序/示例）。重要所有权规则：本文件值由固件拥有，
 * 每次 AFE 复位后应用，禁止从历史 DVC 运行配置 Flash 恢复；0x2800 窗口仅诊断。
 * 持久保护独立由 g_tParam.protect（软件）和 bms_afe_hw_profile_t（DVC 硬件阈值/延时
 * ）拥有。
 */

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
#define DVC1124_DEFAULT_EXPLICIT_WRITE_ADDR  0x40u
#endif

/* 物理串数来自显式选择的 D008 装配。 */
#ifndef DVC1124_DEFAULT_CELL_COUNT
#define DVC1124_DEFAULT_CELL_COUNT           D008_PRODUCT_CELL_COUNT
#endif

/* HS-D008：10 个 2mOhm 分流电阻并联，等效 0.2mOhm = 200uOhm。 */
#ifndef DVC1124_DEFAULT_SHUNT_UOHM
#define DVC1124_DEFAULT_SHUNT_UOHM           200u
#endif

#ifndef BMS_PRODUCTION_BUILD
#define BMS_PRODUCTION_BUILD                 0u
#endif
#if (BMS_PRODUCTION_BUILD > 1u)
#error "BMS_PRODUCTION_BUILD must be 0 or 1"
#endif

/*
 * 保护隔离：1/1 为量产软件+DVC 硬件保护；1/0 为仅软件台架测试，
 * 有意关闭自主硬件保护/故障安全源；0/1 为硬件保护且关闭软件电压/电流，温度独立；
 * 0/0 隔离电压/电流阈值，温度默认开启。
 * DVC1124_SW_TEMP_PROTECT_ENABLE 控制全部软件温度保护。
 * 各模式保留测量、I2C 和 CHGF/DSGF 采样；HW=0 仍保存请求硬件配置，
 * 只禁止台架模式下应用到 DVC。
 */
#ifndef DVC1124_SW_PROTECT_ENABLE
#define DVC1124_SW_PROTECT_ENABLE            1u
#endif
/* 电池 OTP/UTP、MOS OTP 和必要 NTC 有效性独立于电压/电流隔离；无外部温度硬件后备。 */
#ifndef DVC1124_SW_TEMP_PROTECT_ENABLE
#define DVC1124_SW_TEMP_PROTECT_ENABLE       1u
#endif
#ifndef DVC1124_HW_PROTECT_ENABLE
#define DVC1124_HW_PROTECT_ENABLE            1u
#endif
#if ((DVC1124_SW_PROTECT_ENABLE > 1u) || (DVC1124_HW_PROTECT_ENABLE > 1u) || (DVC1124_SW_TEMP_PROTECT_ENABLE > 1u))
#error "DVC1124 protection enable macros must be 0 or 1"
#endif
#if BMS_PRODUCTION_BUILD && ((DVC1124_SW_PROTECT_ENABLE != 1u) || \
                             (DVC1124_HW_PROTECT_ENABLE != 1u) || \
                             (DVC1124_SW_TEMP_PROTECT_ENABLE != 1u))
#error "Production build requires software, hardware and temperature protection enabled"
#endif

/*
 * HS-D008 温度角色：GP1 加热 MOS/电路、GP2 电池 #1、GP3 电池 #2、GP4 功率 MOS。
 * battery_ntc_gp 保留为遗留诊断主通道，产品保护同时使用 GP2/GP3。
 */
#ifndef DVC1124_DEFAULT_HEATER_NTC_GP
#define DVC1124_DEFAULT_HEATER_NTC_GP        1u
#endif
#ifndef DVC1124_DEFAULT_BATTERY_NTC_GP
#define DVC1124_DEFAULT_BATTERY_NTC_GP       2u
#endif
#ifndef DVC1124_DEFAULT_BATTERY_NTC2_GP
#define DVC1124_DEFAULT_BATTERY_NTC2_GP      3u
#endif
#ifndef DVC1124_DEFAULT_MOS_NTC_GP
#define DVC1124_DEFAULT_MOS_NTC_GP           4u
#endif

/*
 * 独立不可逆加热电路故障处置：温度编码 (degC + 40) * 10，95C 为 1350；
 * 软件关闭加热后 GP1 仍 >=95C 持续 10s，视为卡住/异常并触发 PD4/MCC-EN-RF，
 * 独立于功率 MOS OTP 参数。
 */
#ifndef DVC1124_HEATER_OFF_FAULT_TEMP_X10
#define DVC1124_HEATER_OFF_FAULT_TEMP_X10    1350u
#endif
#ifndef DVC1124_HEATER_OFF_FAULT_CONFIRM_MS
#define DVC1124_HEATER_OFF_FAULT_CONFIRM_MS  10000u
#endif

/* GP 模式属于固件板级路由；GP1..GP4 为按上述角色实装的 NTC，GP5/GP6 控制低侧 FET。 */
#ifndef DVC1124_GP1_DEFAULT_MODE
#define DVC1124_GP1_DEFAULT_MODE             DVC1124_GP14_NTC
#endif
#ifndef DVC1124_GP2_DEFAULT_MODE
#define DVC1124_GP2_DEFAULT_MODE             DVC1124_GP236_NTC
#endif
#ifndef DVC1124_GP3_DEFAULT_MODE
#define DVC1124_GP3_DEFAULT_MODE             DVC1124_GP236_NTC
#endif
#ifndef DVC1124_GP4_DEFAULT_MODE
#define DVC1124_GP4_DEFAULT_MODE             DVC1124_GP14_NTC
#endif
#ifndef DVC1124_GP5_DEFAULT_MODE
#define DVC1124_GP5_DEFAULT_MODE             DVC1124_GP5_LOW_CHG
#endif
#ifndef DVC1124_GP6_DEFAULT_MODE
#define DVC1124_GP6_DEFAULT_MODE             DVC1124_GP6_LOW_DSG
#endif

#ifndef DVC1124_GP123_MODE_VALUE
#define DVC1124_GP123_MODE_VALUE \
    DVC1124_GP123_ENCODE(DVC1124_GP1_DEFAULT_MODE, \
                         DVC1124_GP2_DEFAULT_MODE, \
                         DVC1124_GP3_DEFAULT_MODE)
#endif
#ifndef DVC1124_GP456_MODE_VALUE
#define DVC1124_GP456_MODE_VALUE \
    DVC1124_GP456_ENCODE(DVC1124_GP4_DEFAULT_MODE, \
                         DVC1124_GP5_DEFAULT_MODE, \
                         DVC1124_GP6_DEFAULT_MODE)
#endif

/* D008 用 GP5/GP6 控制低侧 CHG/DSG，屏蔽未用高侧驱动。 */
#ifndef DVC1124_DEFAULT_HIGH_SIDE_FET_MASK
#define DVC1124_DEFAULT_HIGH_SIDE_FET_MASK       1u
#endif
#ifndef DVC1124_DEFAULT_CADC_WORK_ENABLE
#define DVC1124_DEFAULT_CADC_WORK_ENABLE         1u
#endif

/* CWT=0 关闭电流唤醒，CAES 必须与固定策略一致。 */
#ifndef DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE
#define DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE 0u
#endif
#ifndef DVC1124_DEFAULT_CC1_WORK_TIME
#define DVC1124_DEFAULT_CC1_WORK_TIME            DVC1124_CC1_WORK_4MS
#endif
#ifndef DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME
#define DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME      DVC1124_CC1_SLEEP_WAKE_32MS
#endif

/* DVC1124-2 0x6D CPVS=101 表示 10V。 */
#ifndef DVC1124_CHARGE_PUMP_VOLTAGE_CODE
#define DVC1124_CHARGE_PUMP_VOLTAGE_CODE         DVC1124_CPVS_10V
#endif

#ifndef DVC1124_DEFAULT_CELL_MEASUREMENT_MASK
#define DVC1124_DEFAULT_CELL_MEASUREMENT_MASK    0u
#endif
#ifndef DVC1124_DEFAULT_CELL_VOLTAGE_SIGNED
#define DVC1124_DEFAULT_CELL_VOLTAGE_SIGNED      0u
#endif

#ifndef DVC1124_DEFAULT_VADC_ENABLE
#define DVC1124_DEFAULT_VADC_ENABLE              1u
#endif
#ifndef DVC1124_DEFAULT_VADC_SYNC_WITH_CC2
#define DVC1124_DEFAULT_VADC_SYNC_WITH_CC2       1u
#endif
#ifndef DVC1124_DEFAULT_VADC_PERIOD
#define DVC1124_DEFAULT_VADC_PERIOD              DVC1124_VADC_EVERY_1_CC2
#endif
#ifndef DVC1124_DEFAULT_VADC_TIME
#define DVC1124_DEFAULT_VADC_TIME                DVC1124_VADC_TIME_1P54MS
#endif

#ifndef DVC1124_DEFAULT_V3P3_SLEEP_ENABLE
#define DVC1124_DEFAULT_V3P3_SLEEP_ENABLE        1u
#endif
#ifndef DVC1124_DEFAULT_V3P3_WORK_ENABLE
#define DVC1124_DEFAULT_V3P3_WORK_ENABLE         1u
#endif
#ifndef DVC1124_DEFAULT_V3P3_TIMEOUT_RESTART
#define DVC1124_DEFAULT_V3P3_TIMEOUT_RESTART     0u
#endif
#ifndef DVC1124_DEFAULT_TIMED_WAKE
#define DVC1124_DEFAULT_TIMED_WAKE               DVC1124_TIMED_WAKE_OFF
#endif

/* D008 不使用 DVC GP 中断，屏蔽全部源。 */
#ifndef DVC1124_DEFAULT_INTERRUPT_MASK
#define DVC1124_DEFAULT_INTERRUPT_MASK           0xFFu
#endif

/* R82 DPC 复位默认 16。 */
#ifndef DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH
#define DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH    16u
#endif

/*
 * DVC 0x53/0x54 为屏蔽寄存器。HS-D008 共口，
 * 清除 DBDM/CBDM 以使 R81 AUTO_DIODE (10b)在电流反转后重开受保护 FET；
 * DWM/CWM 由下方编译期 I2C 超时关闭策略覆盖。0 允许源生效，1 屏蔽源。
 */
#ifndef DVC1124_DEFAULT_DSG_MASK_POLICY
#define DVC1124_DEFAULT_DSG_MASK_POLICY \
    ((uint8_t)(DVC1124_DSG_MASK_RESET & (uint8_t)~DVC1124_DSGMASK_DBDM_MASK))
#endif
#ifndef DVC1124_DEFAULT_CHG_MASK_POLICY
#define DVC1124_DEFAULT_CHG_MASK_POLICY \
    ((uint8_t)(DVC1124_CHG_MASK_RESET & (uint8_t)~DVC1124_CHGMASK_CBDM_MASK))
#endif

/* COTT=0 保持核心过温关断关闭。 */
#ifndef DVC1124_DEFAULT_CORE_OT_CODE
#define DVC1124_DEFAULT_CORE_OT_CODE             0u
#endif

/*
 * SCD 是运行 AFE 硬件保护参数，来自 bms_afe_hw_profile_t；
 * 遗留零默认只作保守编译回退，不拥有运行配置 Flash。
 */
#ifndef DVC1124_HW_SCD_THRESHOLD_MV
#define DVC1124_HW_SCD_THRESHOLD_MV          0u
#endif
#ifndef DVC1124_HW_SCD_DELAY_US
#define DVC1124_HW_SCD_DELAY_US              0u
#endif

/* 固定电流唤醒：0 关闭，否则 CWT * 10uV。 */
#ifndef DVC1124_CURRENT_WAKE_THRESHOLD_UV
#define DVC1124_CURRENT_WAKE_THRESHOLD_UV    0u
#endif

/*
 * 共口反向电流恢复阈值：厂商 FETControl 示例 80uV（BDPT=2），200uOhm 下名义 0.4A；
 * 属于拓扑/故障安全策略，不是用户保护设置。
 */
#ifndef DVC1124_BODY_DIODE_THRESHOLD_UV
#define DVC1124_BODY_DIODE_THRESHOLD_UV      80u
#endif

/*
 * DVC I2C 硬件看门狗：4s 为最短周期，超时启用 CHG/DSG 自主关闭源；值由固件拥有，
 * 禁止从 Flash 恢复。
 */
#ifndef DVC1124_I2C_WATCHDOG_SECONDS
#define DVC1124_I2C_WATCHDOG_SECONDS         4u
#endif
#ifndef DVC1124_I2C_TIMEOUT_CLOSE_CHG
#define DVC1124_I2C_TIMEOUT_CLOSE_CHG        1u
#endif
#ifndef DVC1124_I2C_TIMEOUT_CLOSE_DSG
#define DVC1124_I2C_TIMEOUT_CLOSE_DSG        1u
#endif

/* Telink 事务超时/重试独立于 DVC 硬件看门狗。 */
#ifndef DVC1124_I2C_CMD_TIMEOUT_US
#define DVC1124_I2C_CMD_TIMEOUT_US           5000u
#endif
#ifndef DVC1124_I2C_RETRY_COUNT
#define DVC1124_I2C_RETRY_COUNT              3u
#endif

/* 厂商示例复位 AFE 后等待 300ms 再访问。 */
#ifndef DVC1124_RESET_SETTLE_MS
#define DVC1124_RESET_SETTLE_MS              300u
#endif

/*
 * 仅启动残余零点校准：CC2 周期 256 ms，270 ms 保证每次新转换；
 * 不增加主循环学习/周期 Flash 写入。
 */
#ifndef DVC1124_BOOT_ZERO_ENABLE
#define DVC1124_BOOT_ZERO_ENABLE              1u
#endif
#ifndef DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS
#define DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS  270u
#endif
#ifndef DVC1124_BOOT_ZERO_MAX_ABS_MA
#define DVC1124_BOOT_ZERO_MAX_ABS_MA          1500u
#endif
#ifndef DVC1124_BOOT_ZERO_MAX_SPREAD_MA
#define DVC1124_BOOT_ZERO_MAX_SPREAD_MA       200u
#endif
#if (DVC1124_BOOT_ZERO_ENABLE > 1u)
#error "DVC1124_BOOT_ZERO_ENABLE must be 0 or 1"
#endif
#if DVC1124_BOOT_ZERO_ENABLE && (DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS < 256u)
#error "DVC1124 boot-zero interval must cover one complete CC2 conversion"
#endif

#endif /* 条件编译结束： DVC1124_PROJECT_CONFIG_H_ */
