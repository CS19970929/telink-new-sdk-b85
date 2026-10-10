/*
 * 文件功能：公共均衡、Open-Wire 与 heater 策略；依据采样可信度、温度和保护状态仲裁，
 * 硬件动作交给 backend。
 * bms/app/bms_features.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_FEATURES_H_
#define BMS_FEATURES_H_

#include <stdint.h>
#include "bms_afe.h"
#include "bms_timing.h"

/* 仅新鲜合格样本推进功能资格；按应用名义节拍换算，等待转换只会延长确认。 */
#define BMS_FEATURE_SERVICE_PERIOD_MS BMS_SAMPLE_PERIOD_MS

/* 现有固件温度统一编码为 (degC + 40) * 10。 */
#ifndef BMS_HEATER_START_TEMP_X10
#define BMS_HEATER_START_TEMP_X10 400u /* 仅低于 0 ℃ 时开始。 */
#endif
#ifndef BMS_HEATER_STOP_TEMP_X10
#define BMS_HEATER_STOP_TEMP_X10 450u  /* 达到 +5 ℃ 时停止，形成明确回差。 */
#endif

/* D008 均衡业务默认值独立于软件压差保护分组，起始电压仍可在线配置。 */
#ifndef BMS_BALANCE_ENABLE_DEFAULT
#define BMS_BALANCE_ENABLE_DEFAULT 1u
#endif
#ifndef BMS_BALANCE_START_VOLTAGE_MV_DEFAULT
#define BMS_BALANCE_START_VOLTAGE_MV_DEFAULT 3400u
#endif
#ifndef BMS_BALANCE_START_DELTA_MV_DEFAULT
#define BMS_BALANCE_START_DELTA_MV_DEFAULT 50u
#endif
#ifndef BMS_BALANCE_STOP_DELTA_MV_DEFAULT
#define BMS_BALANCE_STOP_DELTA_MV_DEFAULT 30u
#endif

/* 编译期校验产品均衡调度配置，不新增 Flash 字段或协议寄存器。 */
#if BMS_BALANCE_MAX_CELLS < 0 || BMS_BALANCE_MAX_CELLS > BMS_AFE_FEATURE_MAX_CELLS
#error "BMS_BALANCE_MAX_CELLS must be 0 (unlimited) or within the feature cell capacity"
#endif
#if BMS_BALANCE_TEMP_STOP_X10 == 0
#if BMS_BALANCE_TEMP_RESUME_X10 != 0
#error "Disabled balance temperature hysteresis requires both thresholds to be zero"
#endif
#elif BMS_BALANCE_TEMP_STOP_X10 < 0 || BMS_BALANCE_TEMP_STOP_X10 > 1650 || \
      BMS_BALANCE_TEMP_RESUME_X10 <= 0 || BMS_BALANCE_TEMP_RESUME_X10 >= BMS_BALANCE_TEMP_STOP_X10
#error "Balance temperatures must satisfy 0 < resume < stop <= 1650 in (C + 40) * 10"
#endif

/* 均衡不能基于单个可疑样本动作；这些是测量合理性/资格限值，不是客户保护阈值。 */
#ifndef BMS_BALANCE_TRUST_CONFIRM_MS
#define BMS_BALANCE_TRUST_CONFIRM_MS 1000u
#endif
#ifndef BMS_BALANCE_CELL_PLAUSIBLE_MIN_MV
#define BMS_BALANCE_CELL_PLAUSIBLE_MIN_MV 1000u
#endif
#ifndef BMS_BALANCE_CELL_PLAUSIBLE_MAX_MV
#define BMS_BALANCE_CELL_PLAUSIBLE_MAX_MV 5000u
#endif
#ifndef BMS_BALANCE_CELL_MAX_STEP_MV
#define BMS_BALANCE_CELL_MAX_STEP_MV 250u
#endif
#ifndef BMS_BALANCE_SUSPECT_DELTA_MV
#define BMS_BALANCE_SUSPECT_DELTA_MV 1000u
#endif

#ifndef BMS_OPENWIRE_FIRST_IDLE_MS
#define BMS_OPENWIRE_FIRST_IDLE_MS 10000u
#endif
#ifndef BMS_OPENWIRE_PERIOD_MS
#define BMS_OPENWIRE_PERIOD_MS 300000u
#endif
#define BMS_OPENWIRE_TIMEOUT_MS 3000u
#define BMS_OPENWIRE_RETRY_MS 10000u

/* 复位加热、均衡和断线检测的公共状态。 */
void bms_features_init(void);
/* 按有效快照推进加热、断线检测及均衡策略。 */
void bms_features_service(void);
/* AFE 样本失效时撤销功能资格并停止相关输出。 */
void bms_features_on_afe_invalid(void);

typedef enum {
    BMS_HEATER_IDLE = 0u,
    BMS_HEATER_ARMING = 1u,
    BMS_HEATER_ACTIVE = 2u
} bms_heater_state_t;

typedef struct {
    bms_heater_state_t heater_state;
    uint8_t heater_on;
    uint8_t heater_fuse_fired;
    uint8_t charge_session_active;
    uint8_t balance_active;
    uint8_t balance_voltage_trusted;
    uint8_t openwire_suspected;
    uint8_t openwire_active;
    /* 包含最后诊断帧和待清理窗口，供测量发布、软件电压保护和 SOC 隔离。 */
    uint8_t openwire_sample_active;
} bms_features_status_t;

/* 复制本模块 RAM 状态；不读取 AFE，不改变输出授权。 */
void bms_features_get_status(bms_features_status_t *status);
/* 编码加热、均衡和断线检测的阻断原因。 */
uint32_t bms_features_diag_reasons(uint8_t charge);

/*
 * 公共 AFE 门禁施加硬阻断；方向性充电阻断较软，DVC 映射为 AUTO_DIODE，
 * 以保留合法放电。
 */
uint8_t bms_features_outputs_blocked(void);
/* 查询充电方向相关的公共功能阻断。 */
uint8_t bms_features_charge_direction_blocked(void);
#endif /* 头文件保护：BMS_FEATURES_H_。 */
