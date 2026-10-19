/*
 * 文件功能：量产服务诊断、轨迹和栈水位的统一编译期策略。
 * bms/core/bms_diag_config.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_DIAG_CONFIG_H_
#define BMS_DIAG_CONFIG_H_

/* 开发/生产默认关闭服务诊断；常驻休眠协议独立于本开关，保留旧能力发现位。 */
#ifndef BMS_SERVICE_DIAG_ENABLE
#define BMS_SERVICE_DIAG_ENABLE 0
#endif

#if (BMS_SERVICE_DIAG_ENABLE != 0) && (BMS_SERVICE_DIAG_ENABLE != 1)
#error "BMS_SERVICE_DIAG_ENABLE must be 0 or 1"
#endif

#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD && BMS_SERVICE_DIAG_ENABLE
#error "Production firmware must disable BMS_SERVICE_DIAG_ENABLE"
#endif

/* 轨迹是完整服务诊断的可选子集，默认关闭且不能脱离服务诊断单独启用。 */
#ifndef BMS_DIAG_TRACE_ENABLE
#define BMS_DIAG_TRACE_ENABLE 0
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

/* 栈扫描仅用于显式开发观测；默认不保留水位状态和扫描循环。 */
#ifndef BMS_STACK_MONITOR_ENABLE
#define BMS_STACK_MONITOR_ENABLE 0
#endif

#if (BMS_STACK_MONITOR_ENABLE != 0) && (BMS_STACK_MONITOR_ENABLE != 1)
#error "BMS_STACK_MONITOR_ENABLE must be 0 or 1"
#endif

#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD && BMS_STACK_MONITOR_ENABLE
#error "Production firmware must disable BMS_STACK_MONITOR_ENABLE"
#endif

#endif /* BMS_DIAG_CONFIG_H_ */
