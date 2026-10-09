/* 文件功能：正式生产镜像签核门；工程 ELF 验证不代表产品签核。 */
#pragma once
#include "bms_product.h"
#if BMS_PRODUCT_RELEASE_APPROVED != 1
#error "Product release is not approved"
#endif
#if BMS_PRODUCT_ID == 8u
#if !BMS_D008_SCD_POLICY_APPROVED
#error "D008 SCD policy is not approved"
#endif
#if D008_PRODUCT_CELL_COUNT == 20u && BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_NMC && !BMS_D008_20S_NMC_PROTECTION_APPROVED
#error "D008 20S NMC protection parameters are not approved"
#endif
/* 开放开发配置不等于扩大既有生产签核的串数/体系范围。 */
#if D008_PRODUCT_PROFILE == D008_PRODUCT_PROFILE_CUSTOM
#if !((BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_LFP && (D008_PRODUCT_CELL_COUNT == 16u || D008_PRODUCT_CELL_COUNT == 24u)) || (BMS_PRODUCT_CHEMISTRY == BMS_SOC_CHEMISTRY_NMC && D008_PRODUCT_CELL_COUNT == 20u))
#error "Custom D008 assembly requires a source-controlled production approval"
#endif
#endif
#elif BMS_PRODUCT_ID == 13u
#if !BMS_D013_HW_CONFIG_APPROVED
#error "D013 hardware configuration is not approved"
#endif
#endif
