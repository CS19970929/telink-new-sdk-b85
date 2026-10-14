/*
 * D008 保护参数唯一编辑入口；仅由本产品 bms_product.h 引入。
 * 软件保护与 AFE 硬件保护独立配置，修改任一区域不会联动另一区域。
 * 空白板首次启动使用这些默认值；已有有效 Flash 配置按原 SW/AFE 分组更新规则处理。
 * 单位：_MV=mV，_A10=0.1A，_FILTER_10MS=10ms，_MS=ms，_US=us；
 * 温度 _X10=(摄氏度+40)*10。例：200 A10=20A，100 FILTER_10MS=1s，950 X10=55℃。
 * 总压按单串折算的 _CELL_MV 编辑，存储值仍为 (_CELL_MV/10)*有效串数。
 * 本次只迁移既有默认值；不代表电芯/板级保护参数已通过实板验收。
 */
#pragma once

/* OTA_SOURCE_POLICY_BEGIN：本包更新选择和固定标识，保存后一起提交。 */
#define BMS_OTA_UPDATE_SW        1u
#define BMS_OTA_UPDATE_AFE       1u
#define BMS_OTA_UPDATE_BUSINESS  1u
#define BMS_OTA_UPDATE_SOC       1u
#define BMS_OTA_UPDATE_SOC_STATE 1u
#ifndef BMS_OTA_UPDATE_ID_0
#define BMS_OTA_UPDATE_ID_0 0x0000u
#define BMS_OTA_UPDATE_ID_1 0x2516u
#define BMS_OTA_UPDATE_ID_2 0xfdfeu
#define BMS_OTA_UPDATE_ID_3 0x96a6u
#define BMS_OTA_UPDATE_ID_4 0x3b85u
#define BMS_OTA_UPDATE_ID_5 0x2971u
#endif
#define BMS_OTA_UPDATE_CONTENT_SHA256 "4d43287d0696785d080ff519adf863e409af2c0b70a64525c1c542cef089a21f"
/* OTA_SOURCE_POLICY_END */


#if BMS_PRODUCT_ID != 8u
#error "d008_protection_config.h is only for D008"
#endif

/* 一、保护总开关。保持原台架覆盖和生产构建必须全部开启的约束。 */
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

/* 二、软件三级保护：First/Second 为告警，Third 参与输出保护；零值含义由原校验器决定。 */
/* 2.1 电压保护：*_FOR(c) 为 LFP / NMC；总压仍按每串折算。 */

/* 单体过压 / mV：一级、二级、三级、恢复、滤波。 */
#define D008_SW_CELL_OVP_FIRST_MV_FOR(c)                       ((c) == BMS_SOC_CHEMISTRY_LFP ? 3750u : 4200u)
#define D008_SW_CELL_OVP_FIRST_MV                              D008_SW_CELL_OVP_FIRST_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_CELL_OVP_SECOND_MV_FOR(c)                      ((c) == BMS_SOC_CHEMISTRY_LFP ? 3750u : 4200u)
#define D008_SW_CELL_OVP_SECOND_MV                             D008_SW_CELL_OVP_SECOND_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_CELL_OVP_THIRD_MV_FOR(c)                       ((c) == BMS_SOC_CHEMISTRY_LFP ? 3750u : 4200u)
#define D008_SW_CELL_OVP_THIRD_MV                              D008_SW_CELL_OVP_THIRD_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_CELL_OVP_RECOVER_MV_FOR(c)                     ((c) == BMS_SOC_CHEMISTRY_LFP ? 3500u : 4100u)
#define D008_SW_CELL_OVP_RECOVER_MV                            D008_SW_CELL_OVP_RECOVER_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_CELL_OVP_FILTER_10MS                           100u

/* 单体欠压 / mV：一级、二级、三级、恢复、滤波。 */
#define D008_SW_CELL_UVP_FIRST_MV_FOR(c)                       ((c) == BMS_SOC_CHEMISTRY_LFP ? 3200u : 3200u)
#define D008_SW_CELL_UVP_FIRST_MV                              D008_SW_CELL_UVP_FIRST_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_CELL_UVP_SECOND_MV_FOR(c)                      ((c) == BMS_SOC_CHEMISTRY_LFP ? 3100u : 3100u)
#define D008_SW_CELL_UVP_SECOND_MV                             D008_SW_CELL_UVP_SECOND_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_CELL_UVP_THIRD_MV_FOR(c)                       ((c) == BMS_SOC_CHEMISTRY_LFP ? 2600u : 3000u)
#define D008_SW_CELL_UVP_THIRD_MV                              D008_SW_CELL_UVP_THIRD_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_CELL_UVP_RECOVER_MV_FOR(c)                     ((c) == BMS_SOC_CHEMISTRY_LFP ? 3100u : 3100u)
#define D008_SW_CELL_UVP_RECOVER_MV                            D008_SW_CELL_UVP_RECOVER_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_CELL_UVP_FILTER_10MS                           100u

/* 总压过压 / 每串 mV：一级、二级、三级、恢复、滤波。 */
#define D008_SW_PACK_OVP_FIRST_CELL_MV_FOR(c)                  ((c) == BMS_SOC_CHEMISTRY_LFP ? 3500u : 4200u)
#define D008_SW_PACK_OVP_FIRST_CELL_MV                         D008_SW_PACK_OVP_FIRST_CELL_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_PACK_OVP_SECOND_CELL_MV_FOR(c)                 ((c) == BMS_SOC_CHEMISTRY_LFP ? 3600u : 4200u)
#define D008_SW_PACK_OVP_SECOND_CELL_MV                        D008_SW_PACK_OVP_SECOND_CELL_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_PACK_OVP_THIRD_CELL_MV_FOR(c)                  ((c) == BMS_SOC_CHEMISTRY_LFP ? 3650u : 4200u)
#define D008_SW_PACK_OVP_THIRD_CELL_MV                         D008_SW_PACK_OVP_THIRD_CELL_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_PACK_OVP_RECOVER_CELL_MV_FOR(c)                ((c) == BMS_SOC_CHEMISTRY_LFP ? 3500u : 4100u)
#define D008_SW_PACK_OVP_RECOVER_CELL_MV                       D008_SW_PACK_OVP_RECOVER_CELL_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_PACK_OVP_FILTER_10MS                           100u

/* 总压欠压 / 每串 mV：一级、二级、三级、恢复、滤波。 */
#define D008_SW_PACK_UVP_FIRST_CELL_MV_FOR(c)                  ((c) == BMS_SOC_CHEMISTRY_LFP ? 3000u : 3000u)
#define D008_SW_PACK_UVP_FIRST_CELL_MV                         D008_SW_PACK_UVP_FIRST_CELL_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_PACK_UVP_SECOND_CELL_MV_FOR(c)                 ((c) == BMS_SOC_CHEMISTRY_LFP ? 3000u : 3000u)
#define D008_SW_PACK_UVP_SECOND_CELL_MV                        D008_SW_PACK_UVP_SECOND_CELL_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_PACK_UVP_THIRD_CELL_MV_FOR(c)                  ((c) == BMS_SOC_CHEMISTRY_LFP ? 2800u : 3000u)
#define D008_SW_PACK_UVP_THIRD_CELL_MV                         D008_SW_PACK_UVP_THIRD_CELL_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_PACK_UVP_RECOVER_CELL_MV_FOR(c)                ((c) == BMS_SOC_CHEMISTRY_LFP ? 3000u : 3100u)
#define D008_SW_PACK_UVP_RECOVER_CELL_MV                       D008_SW_PACK_UVP_RECOVER_CELL_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_SW_PACK_UVP_FILTER_10MS                           100u

/* 2.2 充电过流：A10=0.1A；当前 300 对应 30A。 */
#define D008_SW_CHARGE_OCP_FIRST_A10                      (300)
#define D008_SW_CHARGE_OCP_SECOND_A10                     (300)
#define D008_SW_CHARGE_OCP_THIRD_A10                      (300)
#define D008_SW_CHARGE_OCP_RECOVER_A10                    (10)
#define D008_SW_CHARGE_OCP_FILTER_10MS                    10u

/* 2.3 放电过流：A10=0.1A；软件恢复仍要求原物理恢复证据。 */
#define D008_SW_DISCHARGE_OCP_FIRST_A10                   (300)
#define D008_SW_DISCHARGE_OCP_SECOND_A10                  (400)
#define D008_SW_DISCHARGE_OCP_THIRD_A10                   (500)
#define D008_SW_DISCHARGE_OCP_RECOVER_A10                 (10)
#define D008_SW_DISCHARGE_OCP_FILTER_10MS                 10u

/* 2.4 充电高温：温度编码为 (摄氏度+40)*10。 */
#define D008_SW_CHARGE_OTP_FIRST_X10                      ((40 + 40) * 10)
#define D008_SW_CHARGE_OTP_SECOND_X10                     ((50 + 40) * 10)
#define D008_SW_CHARGE_OTP_THIRD_X10                      ((50 + 40) * 10)
#define D008_SW_CHARGE_OTP_RECOVER_X10                    ((40 + 40) * 10)
#define D008_SW_CHARGE_OTP_FILTER_10MS                    100u

/* 2.5 充电低温。 */
#define D008_SW_CHARGE_UTP_FIRST_X10                      ((5 + 40) * 10)
#define D008_SW_CHARGE_UTP_SECOND_X10                     ((3 + 40) * 10)
#define D008_SW_CHARGE_UTP_THIRD_X10                      ((0 + 40) * 10)
#define D008_SW_CHARGE_UTP_RECOVER_X10                    ((3 + 40) * 10)
#define D008_SW_CHARGE_UTP_FILTER_10MS                    100u

/* 2.6 放电高温。 */
#define D008_SW_DISCHARGE_OTP_FIRST_X10                   ((50 + 40) * 10)
#define D008_SW_DISCHARGE_OTP_SECOND_X10                  ((60 + 40) * 10)
#define D008_SW_DISCHARGE_OTP_THIRD_X10                   ((70 + 40) * 10)
#define D008_SW_DISCHARGE_OTP_RECOVER_X10                 ((50 + 40) * 10)
#define D008_SW_DISCHARGE_OTP_FILTER_10MS                 100u

/* 2.7 放电低温。 */
#define D008_SW_DISCHARGE_UTP_FIRST_X10                   ((-10 + 40) * 10)
#define D008_SW_DISCHARGE_UTP_SECOND_X10                  ((-15 + 40) * 10)
#define D008_SW_DISCHARGE_UTP_THIRD_X10                   ((-20 + 40) * 10)
#define D008_SW_DISCHARGE_UTP_RECOVER_X10                 ((-10 + 40) * 10)
#define D008_SW_DISCHARGE_UTP_FILTER_10MS                 100u

/* 2.8 MOS 高温。 */
#define D008_SW_MOS_OTP_FIRST_X10                         ((75 + 40) * 10)
#define D008_SW_MOS_OTP_SECOND_X10                        ((85 + 40) * 10)
#define D008_SW_MOS_OTP_THIRD_X10                         ((95 + 40) * 10)
#define D008_SW_MOS_OTP_RECOVER_X10                       ((80 + 40) * 10)
#define D008_SW_MOS_OTP_FILTER_10MS                       100u

/* 2.9 单体压差保护；与均衡启动/停止压差独立。 */
#define D008_SW_CELL_DELTA_FIRST_MV                       600u
#define D008_SW_CELL_DELTA_SECOND_MV                      800u
#define D008_SW_CELL_DELTA_THIRD_MV                       1000u
#define D008_SW_CELL_DELTA_RECOVER_MV                     800u
#define D008_SW_CELL_DELTA_FILTER_10MS                    100u

/* 2.10 低 SOC 告警/保护，百分比。 */
#define D008_SW_SOC_LOW_FIRST_PERCENT                     20u
#define D008_SW_SOC_LOW_SECOND_PERCENT                    10u
#define D008_SW_SOC_LOW_THIRD_PERCENT                     5u
#define D008_SW_SOC_LOW_RECOVER_PERCENT                   11u
#define D008_SW_SOC_LOW_FILTER_10MS                       100u

/* 三、AFE 硬件保护：以下请求值独立于软件保护，不引用 D008_SW_*。
 * 阈值/延时仍由原 DVC 后端归一化、量化、应用和读回，实际值可能与请求值不同。
 * DVC1124-2 RM V1.2 原 PDF：p20 短路，p23-24 电压保护；寄存器编码未改。
 * 3.1 启用项：0=关闭，1=启用；保留原阈值为0时的关闭规则。
 */
#define D008_AFE_ENABLE_COV                               1u
#define D008_AFE_ENABLE_CUV                               1u
#define D008_AFE_ENABLE_OCD1                              1u
#define D008_AFE_ENABLE_OCD2                              1u
#define D008_AFE_ENABLE_OCC1                              1u
#define D008_AFE_ENABLE_OCC2                              1u
#define D008_AFE_ENABLE_SC                                1u

/* 3.2 单体过压/欠压：mV；只修改实际电池类型对应分支。 */
#define D008_AFE_COV_MV_FOR(c)                                 ((c) == BMS_SOC_CHEMISTRY_LFP ? 3750u : 4200u)
#define D008_AFE_COV_MV                                        D008_AFE_COV_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_AFE_COV_RECOVER_MV_FOR(c)                         ((c) == BMS_SOC_CHEMISTRY_LFP ? 3500u : 4100u)
#define D008_AFE_COV_RECOVER_MV                                D008_AFE_COV_RECOVER_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_AFE_CUV_MV_FOR(c)                                 ((c) == BMS_SOC_CHEMISTRY_LFP ? 2500u : 3000u)
#define D008_AFE_CUV_MV                                        D008_AFE_CUV_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_AFE_CUV_RECOVER_MV_FOR(c)                         ((c) == BMS_SOC_CHEMISTRY_LFP ? 2800u : 3100u)
#define D008_AFE_CUV_RECOVER_MV                                D008_AFE_CUV_RECOVER_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#define D008_AFE_COV_DELAY_MS                             1000u
#define D008_AFE_COV_RECOVER_MS                           1000u
#define D008_AFE_CUV_DELAY_MS                             1000u
#define D008_AFE_CUV_RECOVER_MS                           1000u

/* 3.3 放电过流：A10=0.1A，延时为 ms。
 * 原默认恢复种子 100 保留：归一化后须低于最小启用触发值100，得到99（9.9A）。
 * 如需修改恢复值，请填写小于最低启用过流阈值的数值。
 */
#define D008_AFE_OCD1_A10                                 800u
#define D008_AFE_OCD1_DELAY_MS                            100u
#define D008_AFE_OCD2_A10                                 1000u
#define D008_AFE_OCD2_DELAY_MS                            100u
#define D008_AFE_OCD_RECOVER_A10                          100u

/* 3.4 充电过流：单位及默认恢复值归一化规则同放电过流。 */
#define D008_AFE_OCC1_A10                                 600u
#define D008_AFE_OCC1_DELAY_MS                            100u
#define D008_AFE_OCC2_A10                                 800u
#define D008_AFE_OCC2_DELAY_MS                            100u
#define D008_AFE_OCC_RECOVER_A10                          100u

/* 3.5 放电短路：阈值是分流电阻两端的 mV；分流电阻在 dvc1124_product_defaults.h。
 * 200uOhm 下 40mV 对应200A。请求256us沿用原编码，名义249.92us、整数读回250us。
 * 不提供无作用的温度/自动恢复时间设置：本项目 DVC profile 不支持温度项；温度保护及恢复资格保持原策略。
 */
#define D008_AFE_SC_THRESHOLD_MV                          40u
#define D008_AFE_SC_DELAY_US                              256u


/* SOC 调参；OCV 曲线随所选化学体系切换，数值沿用现有通用曲线。 */
#define BMS_SOC_CURRENT_DEADBAND_MA_DEFAULT     200u
#define BMS_SOC_OCV_REST_PREPARE_S_DEFAULT      600u
#define BMS_SOC_OCV_ERROR_BAND_PERCENT_DEFAULT  5u
/* 修改曲线点时递增对应版本，便于设备诊断追踪；不是 OTA 覆盖编号。 */
#define BMS_SOC_OCV_LFP_VERSION 2u
#define BMS_SOC_OCV_NMC_VERSION 2u
#define BMS_SOC_OCV_LFP_POINTS \
    {2800u,0u}, {3000u,2u}, {3100u,5u}, {3200u,10u}, \
    {3250u,15u}, {3280u,25u}, {3300u,35u}, {3315u,45u}, \
    {3330u,55u}, {3340u,65u}, {3350u,75u}, {3370u,85u}, \
    {3400u,95u}, {3450u,98u}, {3500u,100u}
#define BMS_SOC_OCV_NMC_POINTS \
    {3000u,0u}, {3300u,5u}, {3450u,10u}, {3550u,20u}, \
    {3650u,30u}, {3700u,40u}, {3750u,50u}, {3800u,60u}, \
    {3850u,70u}, {3900u,80u}, {4000u,90u}, {4100u,96u}, {4180u,100u}

/* 类型切换恢复这些业务阈值；持久均衡许可、容量和实测校准仍保留。 */
#if BMS_D008_BALANCE_TEST_ENABLE
#define BMS_BATTERY_BALANCE_START_MV_FOR(c) ((c) == BMS_SOC_CHEMISTRY_LFP ? 3300u : 4100u)
#define BMS_BATTERY_BALANCE_START_DELTA_MV 30u
#define BMS_BATTERY_BALANCE_STOP_DELTA_MV  15u
#else
#define BMS_BATTERY_BALANCE_START_MV_FOR(c) ((c) == BMS_SOC_CHEMISTRY_LFP ? 3400u : 4100u)
#define BMS_BATTERY_BALANCE_START_DELTA_MV 50u
#define BMS_BATTERY_BALANCE_STOP_DELTA_MV  30u
#endif


/* 容量单位 0.1 Ah；GPIO、Rsense、串数与能力仍在 bms_product.h。 */
#ifndef BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH 100u
#endif
#define BMS_HEATER_ENABLE_DEFAULT 1u
#ifndef BMS_HEATER_START_TEMP_X10
#define BMS_HEATER_START_TEMP_X10 400u
#endif
#ifndef BMS_HEATER_STOP_TEMP_X10
#define BMS_HEATER_STOP_TEMP_X10 450u
#endif
#ifndef BMS_BALANCE_ENABLE_DEFAULT
#define BMS_BALANCE_ENABLE_DEFAULT 1u
#endif
#ifndef BMS_BALANCE_START_VOLTAGE_MV_DEFAULT
/* 保留现有首次/OTA 均衡测试组；在线切换 NMC 使用正常 4100mV。 */
#if BMS_D008_BALANCE_TEST_ENABLE
#define BMS_BALANCE_START_VOLTAGE_MV_DEFAULT 3300u
#else
#define BMS_BALANCE_START_VOLTAGE_MV_DEFAULT BMS_BATTERY_BALANCE_START_MV_FOR(BMS_PRODUCT_CHEMISTRY)
#endif
#endif
#ifndef BMS_BALANCE_START_DELTA_MV_DEFAULT
#define BMS_BALANCE_START_DELTA_MV_DEFAULT BMS_BATTERY_BALANCE_START_DELTA_MV
#endif
#ifndef BMS_BALANCE_STOP_DELTA_MV_DEFAULT
#define BMS_BALANCE_STOP_DELTA_MV_DEFAULT BMS_BATTERY_BALANCE_STOP_DELTA_MV
#endif
/* 两档保护性休眠的首次默认；在线值属于独立持久域。 */
#define BMS_SLEEP_NORMAL_CELL_MV 3000u
#define BMS_SLEEP_NORMAL_SECONDS 86400u
#define BMS_SLEEP_LOW_CELL_MV 2800u
#define BMS_SLEEP_LOW_SECONDS 3600u

#if BMS_SOC_CURRENT_DEADBAND_MA_DEFAULT > 2000u || \
    BMS_SOC_OCV_REST_PREPARE_S_DEFAULT < 60u || BMS_SOC_OCV_REST_PREPARE_S_DEFAULT > 3600u || \
    BMS_SOC_OCV_ERROR_BAND_PERCENT_DEFAULT < 1u || BMS_SOC_OCV_ERROR_BAND_PERCENT_DEFAULT > 20u
#error "Invalid product SOC defaults"
#endif

/* 四、编译期检查：防止参数先截断成16位，再进入既有运行校验器。 */
#if (D008_SW_CELL_OVP_FIRST_MV > 65535u) || \
    (D008_SW_CELL_OVP_SECOND_MV > 65535u) || \
    (D008_SW_CELL_OVP_THIRD_MV > 65535u) || \
    (D008_SW_CELL_OVP_RECOVER_MV > 65535u) || \
    (D008_SW_CELL_OVP_FILTER_10MS > 65535u) || \
    (D008_SW_CELL_UVP_FIRST_MV > 65535u) || \
    (D008_SW_CELL_UVP_SECOND_MV > 65535u) || \
    (D008_SW_CELL_UVP_THIRD_MV > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if (D008_SW_CELL_UVP_RECOVER_MV > 65535u) || \
    (D008_SW_CELL_UVP_FILTER_10MS > 65535u) || \
    (D008_SW_PACK_OVP_FIRST_CELL_MV > 65535u) || \
    (D008_SW_PACK_OVP_SECOND_CELL_MV > 65535u) || \
    (D008_SW_PACK_OVP_THIRD_CELL_MV > 65535u) || \
    (D008_SW_PACK_OVP_RECOVER_CELL_MV > 65535u) || \
    (D008_SW_PACK_OVP_FILTER_10MS > 65535u) || \
    (D008_SW_PACK_UVP_FIRST_CELL_MV > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if (D008_SW_PACK_UVP_SECOND_CELL_MV > 65535u) || \
    (D008_SW_PACK_UVP_THIRD_CELL_MV > 65535u) || \
    (D008_SW_PACK_UVP_RECOVER_CELL_MV > 65535u) || \
    (D008_SW_PACK_UVP_FILTER_10MS > 65535u) || \
    (D008_SW_CHARGE_OCP_FIRST_A10 > 65535u) || \
    (D008_SW_CHARGE_OCP_SECOND_A10 > 65535u) || \
    (D008_SW_CHARGE_OCP_THIRD_A10 > 65535u) || \
    (D008_SW_CHARGE_OCP_RECOVER_A10 > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if (D008_SW_CHARGE_OCP_FILTER_10MS > 65535u) || \
    (D008_SW_DISCHARGE_OCP_FIRST_A10 > 65535u) || \
    (D008_SW_DISCHARGE_OCP_SECOND_A10 > 65535u) || \
    (D008_SW_DISCHARGE_OCP_THIRD_A10 > 65535u) || \
    (D008_SW_DISCHARGE_OCP_RECOVER_A10 > 65535u) || \
    (D008_SW_DISCHARGE_OCP_FILTER_10MS > 65535u) || \
    (D008_SW_CHARGE_OTP_FIRST_X10 > 65535u) || \
    (D008_SW_CHARGE_OTP_SECOND_X10 > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if (D008_SW_CHARGE_OTP_THIRD_X10 > 65535u) || \
    (D008_SW_CHARGE_OTP_RECOVER_X10 > 65535u) || \
    (D008_SW_CHARGE_OTP_FILTER_10MS > 65535u) || \
    (D008_SW_CHARGE_UTP_FIRST_X10 > 65535u) || \
    (D008_SW_CHARGE_UTP_SECOND_X10 > 65535u) || \
    (D008_SW_CHARGE_UTP_THIRD_X10 > 65535u) || \
    (D008_SW_CHARGE_UTP_RECOVER_X10 > 65535u) || \
    (D008_SW_CHARGE_UTP_FILTER_10MS > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if (D008_SW_DISCHARGE_OTP_FIRST_X10 > 65535u) || \
    (D008_SW_DISCHARGE_OTP_SECOND_X10 > 65535u) || \
    (D008_SW_DISCHARGE_OTP_THIRD_X10 > 65535u) || \
    (D008_SW_DISCHARGE_OTP_RECOVER_X10 > 65535u) || \
    (D008_SW_DISCHARGE_OTP_FILTER_10MS > 65535u) || \
    (D008_SW_DISCHARGE_UTP_FIRST_X10 > 65535u) || \
    (D008_SW_DISCHARGE_UTP_SECOND_X10 > 65535u) || \
    (D008_SW_DISCHARGE_UTP_THIRD_X10 > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if (D008_SW_DISCHARGE_UTP_RECOVER_X10 > 65535u) || \
    (D008_SW_DISCHARGE_UTP_FILTER_10MS > 65535u) || \
    (D008_SW_MOS_OTP_FIRST_X10 > 65535u) || \
    (D008_SW_MOS_OTP_SECOND_X10 > 65535u) || \
    (D008_SW_MOS_OTP_THIRD_X10 > 65535u) || \
    (D008_SW_MOS_OTP_RECOVER_X10 > 65535u) || \
    (D008_SW_MOS_OTP_FILTER_10MS > 65535u) || \
    (D008_SW_CELL_DELTA_FIRST_MV > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if (D008_SW_CELL_DELTA_SECOND_MV > 65535u) || \
    (D008_SW_CELL_DELTA_THIRD_MV > 65535u) || \
    (D008_SW_CELL_DELTA_RECOVER_MV > 65535u) || \
    (D008_SW_CELL_DELTA_FILTER_10MS > 65535u) || \
    (D008_SW_SOC_LOW_FIRST_PERCENT > 65535u) || \
    (D008_SW_SOC_LOW_SECOND_PERCENT > 65535u) || \
    (D008_SW_SOC_LOW_THIRD_PERCENT > 65535u) || \
    (D008_SW_SOC_LOW_RECOVER_PERCENT > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if (D008_SW_SOC_LOW_FILTER_10MS > 65535u) || \
    (D008_AFE_COV_MV > 65535u) || \
    (D008_AFE_COV_RECOVER_MV > 65535u) || \
    (D008_AFE_CUV_MV > 65535u) || \
    (D008_AFE_CUV_RECOVER_MV > 65535u) || \
    (D008_AFE_COV_DELAY_MS > 65535u) || \
    (D008_AFE_COV_RECOVER_MS > 65535u) || \
    (D008_AFE_CUV_DELAY_MS > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if (D008_AFE_CUV_RECOVER_MS > 65535u) || \
    (D008_AFE_OCD1_A10 > 65535u) || \
    (D008_AFE_OCD1_DELAY_MS > 65535u) || \
    (D008_AFE_OCD2_A10 > 65535u) || \
    (D008_AFE_OCD2_DELAY_MS > 65535u) || \
    (D008_AFE_OCD_RECOVER_A10 > 65535u) || \
    (D008_AFE_OCC1_A10 > 65535u) || \
    (D008_AFE_OCC1_DELAY_MS > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if (D008_AFE_OCC2_A10 > 65535u) || \
    (D008_AFE_OCC2_DELAY_MS > 65535u) || \
    (D008_AFE_OCC_RECOVER_A10 > 65535u) || \
    (D008_AFE_SC_THRESHOLD_MV > 65535u) || \
    (D008_AFE_SC_DELAY_US > 65535u)
#error "D008 protection value exceeds uint16 range"
#endif
#if ((D008_SW_PACK_OVP_FIRST_CELL_MV % 10u) != 0u) || ((D008_SW_PACK_OVP_FIRST_CELL_MV / 10u * D008_PRODUCT_CELL_COUNT) > 65535u)
#error "D008 pack protection must fit 10mV units and uint16 range"
#endif
#if ((D008_SW_PACK_OVP_SECOND_CELL_MV % 10u) != 0u) || ((D008_SW_PACK_OVP_SECOND_CELL_MV / 10u * D008_PRODUCT_CELL_COUNT) > 65535u)
#error "D008 pack protection must fit 10mV units and uint16 range"
#endif
#if ((D008_SW_PACK_OVP_THIRD_CELL_MV % 10u) != 0u) || ((D008_SW_PACK_OVP_THIRD_CELL_MV / 10u * D008_PRODUCT_CELL_COUNT) > 65535u)
#error "D008 pack protection must fit 10mV units and uint16 range"
#endif
#if ((D008_SW_PACK_OVP_RECOVER_CELL_MV % 10u) != 0u) || ((D008_SW_PACK_OVP_RECOVER_CELL_MV / 10u * D008_PRODUCT_CELL_COUNT) > 65535u)
#error "D008 pack protection must fit 10mV units and uint16 range"
#endif
#if ((D008_SW_PACK_UVP_FIRST_CELL_MV % 10u) != 0u) || ((D008_SW_PACK_UVP_FIRST_CELL_MV / 10u * D008_PRODUCT_CELL_COUNT) > 65535u)
#error "D008 pack protection must fit 10mV units and uint16 range"
#endif
#if ((D008_SW_PACK_UVP_SECOND_CELL_MV % 10u) != 0u) || ((D008_SW_PACK_UVP_SECOND_CELL_MV / 10u * D008_PRODUCT_CELL_COUNT) > 65535u)
#error "D008 pack protection must fit 10mV units and uint16 range"
#endif
#if ((D008_SW_PACK_UVP_THIRD_CELL_MV % 10u) != 0u) || ((D008_SW_PACK_UVP_THIRD_CELL_MV / 10u * D008_PRODUCT_CELL_COUNT) > 65535u)
#error "D008 pack protection must fit 10mV units and uint16 range"
#endif
#if ((D008_SW_PACK_UVP_RECOVER_CELL_MV % 10u) != 0u) || ((D008_SW_PACK_UVP_RECOVER_CELL_MV / 10u * D008_PRODUCT_CELL_COUNT) > 65535u)
#error "D008 pack protection must fit 10mV units and uint16 range"
#endif
#if (D008_SW_CELL_UVP_FIRST_MV < D008_SW_CELL_UVP_SECOND_MV) || \
    (D008_SW_CELL_UVP_SECOND_MV < D008_SW_CELL_UVP_THIRD_MV) || \
    ((D008_SW_CELL_UVP_THIRD_MV != 0u) && (D008_SW_CELL_UVP_RECOVER_MV <= D008_SW_CELL_UVP_THIRD_MV))
#error "D008 CUV defaults require First >= Second >= Third and Recover > Third"
#endif
#if DVC1124_DEFAULT_SHUNT_UOHM != 0u
#if ((D008_AFE_SC_THRESHOLD_MV * 10000u) / DVC1124_DEFAULT_SHUNT_UOHM) > 65535u
#error "D008 short circuit current exceeds uint16 A10 range"
#endif
#endif
#if (D008_AFE_ENABLE_COV > 1u) || \
    (D008_AFE_ENABLE_CUV > 1u) || \
    (D008_AFE_ENABLE_OCD1 > 1u) || \
    (D008_AFE_ENABLE_OCD2 > 1u) || \
    (D008_AFE_ENABLE_OCC1 > 1u) || \
    (D008_AFE_ENABLE_OCC2 > 1u) || \
    (D008_AFE_ENABLE_SC > 1u)
#error "D008 AFE protection enable must be 0 or 1"
#endif

/* 在线切换的另一种类型也必须在 uint16 范围内。 */
#if (D008_SW_CELL_OVP_FIRST_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_CELL_OVP_FIRST_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_CELL_OVP_FIRST_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_CELL_OVP_FIRST_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_CELL_OVP_SECOND_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_CELL_OVP_SECOND_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_CELL_OVP_SECOND_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_CELL_OVP_SECOND_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_CELL_OVP_THIRD_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_CELL_OVP_THIRD_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_CELL_OVP_THIRD_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_CELL_OVP_THIRD_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_CELL_OVP_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_CELL_OVP_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_CELL_OVP_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_CELL_OVP_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u)
#error "D008 chemistry default exceeds uint16 range"
#endif
#if (D008_SW_CELL_UVP_FIRST_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_CELL_UVP_FIRST_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_CELL_UVP_FIRST_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_CELL_UVP_FIRST_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_CELL_UVP_SECOND_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_CELL_UVP_SECOND_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_CELL_UVP_SECOND_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_CELL_UVP_SECOND_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_CELL_UVP_THIRD_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_CELL_UVP_THIRD_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_CELL_UVP_THIRD_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_CELL_UVP_THIRD_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_CELL_UVP_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_CELL_UVP_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_CELL_UVP_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_CELL_UVP_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u)
#error "D008 chemistry default exceeds uint16 range"
#endif
#if (D008_SW_PACK_OVP_FIRST_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_PACK_OVP_FIRST_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_PACK_OVP_FIRST_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_PACK_OVP_FIRST_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_PACK_OVP_SECOND_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_PACK_OVP_SECOND_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_PACK_OVP_SECOND_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_PACK_OVP_SECOND_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_PACK_OVP_THIRD_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_PACK_OVP_THIRD_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_PACK_OVP_THIRD_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_PACK_OVP_THIRD_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_PACK_OVP_RECOVER_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_PACK_OVP_RECOVER_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_PACK_OVP_RECOVER_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_PACK_OVP_RECOVER_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u)
#error "D008 chemistry default exceeds uint16 range"
#endif
#if (D008_SW_PACK_UVP_FIRST_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_PACK_UVP_FIRST_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_PACK_UVP_FIRST_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_PACK_UVP_FIRST_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_PACK_UVP_SECOND_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_PACK_UVP_SECOND_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_PACK_UVP_SECOND_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_PACK_UVP_SECOND_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_PACK_UVP_THIRD_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_PACK_UVP_THIRD_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_PACK_UVP_THIRD_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_PACK_UVP_THIRD_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_SW_PACK_UVP_RECOVER_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_SW_PACK_UVP_RECOVER_CELL_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_SW_PACK_UVP_RECOVER_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_SW_PACK_UVP_RECOVER_CELL_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u)
#error "D008 chemistry default exceeds uint16 range"
#endif
#if (D008_AFE_COV_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_AFE_COV_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_AFE_COV_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_AFE_COV_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_AFE_COV_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_AFE_COV_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_AFE_COV_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_AFE_COV_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_AFE_CUV_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_AFE_CUV_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_AFE_CUV_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_AFE_CUV_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u) || \
    (D008_AFE_CUV_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_LFP) < 0) || (D008_AFE_CUV_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_LFP) > 65535u) || \
    (D008_AFE_CUV_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_NMC) < 0) || (D008_AFE_CUV_RECOVER_MV_FOR(BMS_SOC_CHEMISTRY_NMC) > 65535u)
#error "D008 chemistry default exceeds uint16 range"
#endif

#if BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH < 1u || BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH > BMS_SOC_CAPACITY_MAX_0P1AH || \
    BMS_SOC_OCV_LFP_VERSION < 1u || BMS_SOC_OCV_LFP_VERSION > 65535u || \
    BMS_SOC_OCV_NMC_VERSION < 1u || BMS_SOC_OCV_NMC_VERSION > 65535u
#error "Invalid capacity or OCV profile version"
#endif
#if BMS_HEATER_ENABLE_DEFAULT > 1u || BMS_HEATER_START_TEMP_X10 >= BMS_HEATER_STOP_TEMP_X10 || \
    BMS_HEATER_STOP_TEMP_X10 > 1650u || BMS_BALANCE_ENABLE_DEFAULT > 1u || \
    BMS_BATTERY_BALANCE_START_DELTA_MV == 0u || BMS_BATTERY_BALANCE_START_DELTA_MV > 65535u || \
    BMS_BATTERY_BALANCE_STOP_DELTA_MV >= BMS_BATTERY_BALANCE_START_DELTA_MV
#error "Invalid heater or balance defaults"
#endif
