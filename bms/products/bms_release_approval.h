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
/* D008 的合法串数和化学体系由源码 profile 及芯片边界检查，不再维护固定装配白名单。 */
#elif BMS_PRODUCT_ID == 13u
#if !BMS_D013_HW_CONFIG_APPROVED
#error "D013 hardware configuration is not approved"
#endif
#endif
