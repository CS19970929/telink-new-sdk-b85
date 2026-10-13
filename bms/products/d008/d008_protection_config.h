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
/* 2.1 电压阈值：只修改实际电池类型对应分支。总压阈值按单串 mV 折算。 */
#if BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_LFP
#define D008_SW_CELL_OVP_FIRST_MV                         3750u
#define D008_SW_CELL_OVP_SECOND_MV                        3750u
#define D008_SW_CELL_OVP_THIRD_MV                         3750u
#define D008_SW_CELL_OVP_RECOVER_MV                       3500u
#define D008_SW_CELL_UVP_THIRD_MV                         2800u
#define D008_SW_PACK_OVP_FIRST_CELL_MV                    3500u
#define D008_SW_PACK_OVP_SECOND_CELL_MV                   3600u
#define D008_SW_PACK_OVP_THIRD_CELL_MV                    3650u
#define D008_SW_PACK_OVP_RECOVER_CELL_MV                  3500u
#define D008_SW_PACK_UVP_THIRD_CELL_MV                    2900u
#define D008_SW_PACK_UVP_RECOVER_CELL_MV                  3000u
#elif BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_NMC
#define D008_SW_CELL_OVP_FIRST_MV                         4200u
#define D008_SW_CELL_OVP_SECOND_MV                        4200u
#define D008_SW_CELL_OVP_THIRD_MV                         4200u
#define D008_SW_CELL_OVP_RECOVER_MV                       4100u
#define D008_SW_CELL_UVP_THIRD_MV                         3000u
#define D008_SW_PACK_OVP_FIRST_CELL_MV                    4200u
#define D008_SW_PACK_OVP_SECOND_CELL_MV                   4200u
#define D008_SW_PACK_OVP_THIRD_CELL_MV                    4200u
#define D008_SW_PACK_OVP_RECOVER_CELL_MV                  4100u
#define D008_SW_PACK_UVP_THIRD_CELL_MV                    3000u
#define D008_SW_PACK_UVP_RECOVER_CELL_MV                  3100u
#else
#error "D008 protection requires LFP or NMC chemistry"
#endif

/* 电压保护延时、共用欠压阈值；延时单位为 10ms。 */
#define D008_SW_CELL_OVP_FILTER_10MS                      100u
#define D008_SW_CELL_UVP_FIRST_MV                         3000u
#define D008_SW_CELL_UVP_SECOND_MV                        3000u
#define D008_SW_CELL_UVP_RECOVER_MV                       3100u
#define D008_SW_CELL_UVP_FILTER_10MS                      100u
#define D008_SW_PACK_OVP_FILTER_10MS                      100u
#define D008_SW_PACK_UVP_FIRST_CELL_MV                    3000u
#define D008_SW_PACK_UVP_SECOND_CELL_MV                   3000u
#define D008_SW_PACK_UVP_FILTER_10MS                      100u

/* 2.2 充电过流：A10=0.1A；当前 100/150/200 对应 10/15/20A。 */
#define D008_SW_CHARGE_OCP_FIRST_A10                      (100)
#define D008_SW_CHARGE_OCP_SECOND_A10                     (150)
#define D008_SW_CHARGE_OCP_THIRD_A10                      (200)
#define D008_SW_CHARGE_OCP_RECOVER_A10                    (100)
#define D008_SW_CHARGE_OCP_FILTER_10MS                    10u

/* 2.3 放电过流：A10=0.1A；软件恢复仍要求原物理恢复证据。 */
#define D008_SW_DISCHARGE_OCP_FIRST_A10                   (100)
#define D008_SW_DISCHARGE_OCP_SECOND_A10                  (150)
#define D008_SW_DISCHARGE_OCP_THIRD_A10                   (200)
#define D008_SW_DISCHARGE_OCP_RECOVER_A10                 (100)
#define D008_SW_DISCHARGE_OCP_FILTER_10MS                 10u

/* 2.4 充电高温：温度编码为 (摄氏度+40)*10。 */
#define D008_SW_CHARGE_OTP_FIRST_X10                      ((40 + 40) * 10)
#define D008_SW_CHARGE_OTP_SECOND_X10                     ((50 + 40) * 10)
#define D008_SW_CHARGE_OTP_THIRD_X10                      ((55 + 40) * 10)
#define D008_SW_CHARGE_OTP_RECOVER_X10                    ((50 + 40) * 10)
#define D008_SW_CHARGE_OTP_FILTER_10MS                    100u

/* 2.5 充电低温。 */
#define D008_SW_CHARGE_UTP_FIRST_X10                      ((5 + 40) * 10)
#define D008_SW_CHARGE_UTP_SECOND_X10                     ((3 + 40) * 10)
#define D008_SW_CHARGE_UTP_THIRD_X10                      ((0 + 40) * 10)
#define D008_SW_CHARGE_UTP_RECOVER_X10                    ((3 + 40) * 10)
#define D008_SW_CHARGE_UTP_FILTER_10MS                    100u

/* 2.6 放电高温。 */
#define D008_SW_DISCHARGE_OTP_FIRST_X10                   ((50 + 40) * 10)
#define D008_SW_DISCHARGE_OTP_SECOND_X10                  ((50 + 40) * 10)
#define D008_SW_DISCHARGE_OTP_THIRD_X10                   ((60 + 40) * 10)
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
#if BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_LFP
#define D008_AFE_COV_MV                                   3750u
#define D008_AFE_COV_RECOVER_MV                           3500u
#define D008_AFE_CUV_MV                                   2800u
#define D008_AFE_CUV_RECOVER_MV                           3100u
#elif BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_NMC
#define D008_AFE_COV_MV                                   4200u
#define D008_AFE_COV_RECOVER_MV                           4100u
#define D008_AFE_CUV_MV                                   3000u
#define D008_AFE_CUV_RECOVER_MV                           3100u
#endif
#define D008_AFE_COV_DELAY_MS                             1000u
#define D008_AFE_COV_RECOVER_MS                           1000u
#define D008_AFE_CUV_DELAY_MS                             1000u
#define D008_AFE_CUV_RECOVER_MS                           1000u

/* 3.3 放电过流：A10=0.1A，延时为 ms。
 * 原默认恢复种子 100 保留：归一化后须低于最小启用触发值100，得到99（9.9A）。
 * 如需修改恢复值，请填写小于最低启用过流阈值的数值。
 */
#define D008_AFE_OCD1_A10                                 100u
#define D008_AFE_OCD1_DELAY_MS                            100u
#define D008_AFE_OCD2_A10                                 150u
#define D008_AFE_OCD2_DELAY_MS                            100u
#define D008_AFE_OCD_RECOVER_A10                          100u

/* 3.4 充电过流：单位及默认恢复值归一化规则同放电过流。 */
#define D008_AFE_OCC1_A10                                 100u
#define D008_AFE_OCC1_DELAY_MS                            100u
#define D008_AFE_OCC2_A10                                 150u
#define D008_AFE_OCC2_DELAY_MS                            100u
#define D008_AFE_OCC_RECOVER_A10                          100u

/* 3.5 放电短路：阈值是分流电阻两端的 mV；分流电阻在 dvc1124_product_defaults.h。
 * 200uOhm 下 40mV 对应200A。请求256us沿用原编码，名义249.92us、整数读回250us。
 * 不提供无作用的温度/自动恢复时间设置：本项目 DVC profile 不支持温度项；温度保护及恢复资格保持原策略。
 */
#define D008_AFE_SC_THRESHOLD_MV                          40u
#define D008_AFE_SC_DELAY_US                              256u

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
