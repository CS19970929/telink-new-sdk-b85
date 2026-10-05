/*
 * 文件功能：应用参数加载、保存和启动安全门禁；通过各产品独立更新编号决定参数组的保留
 * 或更新。
 * bms/core/param.h；实际编译归属见各产品 sources.txt。
 */
/*
 * **********************************************************************************
 * **********************模块名称：软件保护默认参数文件名称：param.h版本：
 * 开发版说明：四产品共用，
 * 产品差异由产品配置提供Copyright (C), 2012-2013, 瀹夊瘜鑾辩數瀛�
 * www.armfly.com********************************************************************************************************
 */

#ifndef __PARAM_H
#define __PARAM_H

#include "conf.h"
#include "bms_protection_params.h"

#define COV_1           3750
#define COV_2           3750
#define COV_3           3750
// #define COV_3           4250
// #define COV_recover     4150
#define COV_recover     3500
#define COV_filter3     100

#define CUV_1           3000
#define CUV_2           3000
// #define CUV_3           3000
#define CUV_3           BMS_DEFAULT_CUV3_MV
#define CUV_recover     3100
#define CUV_filter3     BMS_DEFAULT_CUV3_FILTER

/* 更新编号应用前先拒绝不安全编译默认值，避免因此阻断启动。 */
#if (CUV_1 < CUV_2) || (CUV_2 < CUV_3) || ((CUV_3 != 0) && (CUV_recover <= CUV_3))
#error "CUV defaults require First >= Second >= Third and Recover > Third (unless Third=0)"
#endif

#define BOV_1           (350 * SeriesNum)
#define BOV_2           (360 * SeriesNum)
#define BOV_3           (365 * SeriesNum)
#define BOV_recover     (350 * SeriesNum)
// #define BOV_1           (2800)
// #define BOV_2           (2920)
// #define BOV_3           (3000)
// #define BOV_recover     (2800)
#define BOV_filter3     100

#define BUV_1           (300 * SeriesNum)
#define BUV_2           (300 * SeriesNum)
#define BUV_3           (290 * SeriesNum)
#define BUV_recover     (300 * SeriesNum)
#define BUV_filter3     100

#define OTC_1           ((40 + 40) * 10)
#define OTC_2           ((50 + 40) * 10)
#define OTC_3           ((55 + 40) * 10)
#define OTC_recover     ((50 + 40) * 10)
#define OTC_filter3      100

#define UTC_1           ((5 + 40) * 10)
#define UTC_2           ((3 + 40) * 10)
#define UTC_3           ((0 + 40) * 10)
#define UTC_recover     ((3 + 40) * 10)
#define UTC_filter3      100

#define OTD_1           ((50 + 40) * 10)
#define OTD_2           ((50 + 40) * 10)
#define OTD_3           ((60 + 40) * 10)
#define OTD_recover     ((50 + 40) * 10)
#define OTD_filter3      100

#define UTD_1           ((-10 + 40) * 10)
#define UTD_2           ((-15 + 40) * 10)
#define UTD_3           ((-20 + 40) * 10)
#define UTD_recover     ((-10 + 40) * 10)
#define UTD_filter3      100

#define mos_1           ((75 + 40) * 10)
#define mos_2           ((85 + 40) * 10)
#define mos_3           ((95 + 40) * 10)
#define mos_recover     ((80 + 40) * 10)
#define mos_filter3      100

#define VDELTER_1       600
#define VDELTER_2       800
#define VDELTER_3       1000
#define VDELTER_recover 800
#define VDELTER_filter3  100

#define socLow_1        20
#define socLow_2        10
#define socLow_3        5
#define socLow_recover  11
#define socLow_filter3   100

#define OCC_1       (100)
#define OCC_2       (150)
#define OCC_3       (200)
#define OCC_recover (100)
#define OCC_filter3  10

#define ODC_1       (100)
#define ODC_2       (150)
#define ODC_3       (200)
#define ODC_recover (100)
#define ODC_filter3  10

#define E2P_PROTECT_DEFAULT_PRT {/* 鍗曡妭杩囧帇 */COV_1,   COV_2,  COV_3,  COV_recover,    COV_filter3,\
                                 /* 鍗曡妭浣庡帇 */CUV_1,   CUV_2,  CUV_3,  CUV_recover,    CUV_filter3,\
                                 /* 鎬诲帇杩囧帇 */BOV_1, BOV_2,    BOV_3,  BOV_recover,    BOV_filter3,\
                                 /* 鎬诲帇浣庡帇 */BUV_1, BUV_2,    BUV_3,  BUV_recover,    BUV_filter3,\
                                 /* 鍏呯數杩囨祦 */OCC_1,   OCC_2,  OCC_3,  OCC_recover,    OCC_filter3,\
                                 /* 鏀剧數杩囨祦 */ODC_1,   ODC_2,  ODC_3,  ODC_recover,    ODC_filter3,\
                                 /* 充电高温 */OTC_1, OTC_2,  OTC_3,  OTC_recover,    OTC_filter3,\
                                 /* 充电低温 */UTC_1, UTC_2,  UTC_3,  UTC_recover,    UTC_filter3,\
                                 /* 放电高温 */OTD_1, OTD_2,  OTD_3,  OTD_recover,    OTD_filter3,\
                                 /* 放电低温 */UTD_1, UTD_2,  UTD_3,  UTD_recover,    UTD_filter3,\
                                 /* MOS 高温 */mos_1,   mos_2,  mos_3,  mos_recover,    mos_filter3,\
                                 /* 压差过大 */VDELTER_1, VDELTER_2,  VDELTER_3,  VDELTER_recover,    VDELTER_filter3,\
                                 /* 低电量 */socLow_1,   socLow_2,   socLow_3,   socLow_recover,     socLow_filter3}

/* 公共参数接口 */

void LoadParam(void);
/* 加载并验证各持久域，确定启动输出资格。 */
void bms_parameters_startup(void);
/* 保存当前业务参数并返回存储结果。 */
uint8_t SaveParam(void);
/* 校验并持久化软件保护候选配置，成功后才发布。 */
uint8_t bms_protection_params_commit(const struct PRT_E2ROM_PARAS *candidate);
/*
 * false 表示加载的软件保护记录不安全。仍可读以供诊断，
 * 但完整有效记录持久化前公共门禁阻断 CHG 与 DSG。
 */
uint8_t bms_protection_params_valid(void);

#endif
