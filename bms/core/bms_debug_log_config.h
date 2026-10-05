/*
 * 文件功能：运行日志编译开关、等级、模块和量产门禁。
 * 仅包含预处理配置，避免把 stdint 类型引入 SDK 全局 app_config。
 */
#ifndef BMS_DEBUG_LOG_CONFIG_H
#define BMS_DEBUG_LOG_CONFIG_H
#ifndef BMS_DEBUG_LOG_ENABLE
#define BMS_DEBUG_LOG_ENABLE 0
#endif
#ifndef BMS_DEBUG_LOG_LEVEL
#define BMS_DEBUG_LOG_LEVEL 3
#endif
#ifndef BMS_DEBUG_LOG_MODULE_MASK
#define BMS_DEBUG_LOG_MODULE_MASK 0x01FFu
#endif
#if (BMS_DEBUG_LOG_ENABLE != 0) && (BMS_DEBUG_LOG_ENABLE != 1)
#error "BMS_DEBUG_LOG_ENABLE must be 0 or 1"
#endif
#if BMS_DEBUG_LOG_LEVEL < 1 || BMS_DEBUG_LOG_LEVEL > 4
#error "BMS_DEBUG_LOG_LEVEL must be 1..4"
#endif
#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD && BMS_DEBUG_LOG_ENABLE
#error "Production firmware must disable BMS_DEBUG_LOG_ENABLE"
#endif

#endif
