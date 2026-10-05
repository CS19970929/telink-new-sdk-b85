/* 文件功能：产品应用配置及历史公共类型；容量、时间和功能宏必须遵守板级参数签核边界。
 * bms/core/conf.h；实际编译归属见各产品 sources.txt。
 */
#ifndef CONF_H_
#define CONF_H_
#include "common/types.h"
#include <stdint.h>
#include "flash_store_cfg.h"
#include "bms_afe_backend.h"
#include "bms_product_conf.h"
#define FAC_INIT_soc 60u
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef int16_t INT16;
typedef int8_t INT8;

#define BMS_STATE_SAVE_INTERVAL_32K (60u * 32000u)
#define BMS_STORAGE_RETRY_INTERVAL_32K (5u * 32000u)
#define BMS_EVENT_SAVE_INTERVAL_32K (60u * 32000u)
#endif
