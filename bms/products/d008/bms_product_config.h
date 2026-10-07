/* 文件功能：产品身份、AFE 后端及化学体系的编译期选择。 */
#pragma once
#define BMS_PRODUCT_ID 8u
#define BMS_AFE_BACKEND 1
#define BMS_PRODUCT_CHEMISTRY D008_PRODUCT_CHEMISTRY
#define BMS_PRODUCT_SOC_PROFILE_ID D008_PRODUCT_SOC_PROFILE_ID
#include "d008_product_profile.h"

/* 需产品签核与依据提交后才能置 1；不改变现有阈值或 SCD 开关。 */
#define BMS_D008_SCD_POLICY_APPROVED 0
#define BMS_D008_20S_NMC_PROTECTION_APPROVED 0
