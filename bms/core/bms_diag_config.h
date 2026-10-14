/*
 * 文件功能：量产服务诊断、轨迹和栈水位的统一编译期策略。
 * bms/core/bms_diag_config.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_DIAG_CONFIG_H_
#define BMS_DIAG_CONFIG_H_

/* 正式版本只保留只读兼容窗口，不维护启动、运行、MOS、SOC 或休眠快照。 */
#ifndef BMS_SERVICE_DIAG_ENABLE
#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD
#define BMS_SERVICE_DIAG_ENABLE 0
#else
#define BMS_SERVICE_DIAG_ENABLE 1
#endif
#endif

#if (BMS_SERVICE_DIAG_ENABLE != 0) && (BMS_SERVICE_DIAG_ENABLE != 1)
#error "BMS_SERVICE_DIAG_ENABLE must be 0 or 1"
#endif

#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD && BMS_SERVICE_DIAG_ENABLE
#error "Production firmware must disable BMS_SERVICE_DIAG_ENABLE"
#endif

/* 轨迹是完整服务诊断的可选子集，不能脱离服务诊断单独启用。 */
#ifndef BMS_DIAG_TRACE_ENABLE
#define BMS_DIAG_TRACE_ENABLE BMS_SERVICE_DIAG_ENABLE
#endif

#if (BMS_DIAG_TRACE_ENABLE != 0) && (BMS_DIAG_TRACE_ENABLE != 1)
#error "BMS_DIAG_TRACE_ENABLE must be 0 or 1"
#endif

#if BMS_DIAG_TRACE_ENABLE && !BMS_SERVICE_DIAG_ENABLE
#error "BMS_DIAG_TRACE_ENABLE requires BMS_SERVICE_DIAG_ENABLE"
#endif

#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD && BMS_DIAG_TRACE_ENABLE
#error "Production firmware must disable BMS_DIAG_TRACE_ENABLE"
#endif

/* 栈扫描仅用于开发观测；量产不保留水位状态和扫描循环。 */
#ifndef BMS_STACK_MONITOR_ENABLE
#define BMS_STACK_MONITOR_ENABLE BMS_SERVICE_DIAG_ENABLE
#endif

#if (BMS_STACK_MONITOR_ENABLE != 0) && (BMS_STACK_MONITOR_ENABLE != 1)
#error "BMS_STACK_MONITOR_ENABLE must be 0 or 1"
#endif

#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD && BMS_STACK_MONITOR_ENABLE
#error "Production firmware must disable BMS_STACK_MONITOR_ENABLE"
#endif

#endif /* BMS_DIAG_CONFIG_H_ */
