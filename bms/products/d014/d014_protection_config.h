/* D014 参数入口：容量与 OTA 标识按产品保存；保护/SOC 默认在
 * ../sh3673510_protection_config.h。 */
#pragma once

/* 容量单位 0.1 Ah；GPIO、Rsense、串数与能力仍在 bms_product.h。 */
#ifndef BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH
#define BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH 116u
#endif

/* OTA_SOURCE_POLICY_BEGIN：本包更新选择和固定标识，保存后一起提交。 */
#define BMS_OTA_UPDATE_SW        0u
#define BMS_OTA_UPDATE_AFE       0u
#define BMS_OTA_UPDATE_BUSINESS  0u
#define BMS_OTA_UPDATE_SOC       0u
#define BMS_OTA_UPDATE_SOC_STATE 0u
#define BMS_OTA_UPDATE_FACTORY_RESET 0u /* 专用出厂包，须生成全新一次性 ID */
#ifndef BMS_OTA_UPDATE_ID_0
#define BMS_OTA_UPDATE_ID_0 0x0000u
#define BMS_OTA_UPDATE_ID_1 0x0000u
#define BMS_OTA_UPDATE_ID_2 0x0000u
#define BMS_OTA_UPDATE_ID_3 0x0000u
#define BMS_OTA_UPDATE_ID_4 0x0000u
#define BMS_OTA_UPDATE_ID_5 0x0000u
#endif
#define BMS_OTA_UPDATE_CONTENT_SHA256 "0000000000000000000000000000000000000000000000000000000000000000"
/* OTA_SOURCE_POLICY_END */

#include "../sh3673510_protection_config.h"
