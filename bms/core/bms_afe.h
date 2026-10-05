/*
 * 文件功能：应用侧 AFE 契约；采样资格、MOS 请求、保护配置、休眠与辅助测量由
 * guard/backend 实现。
 * bms/core/bms_afe.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_AFE_H_
#define BMS_AFE_H_
#include <stdint.h>
#include "bms_afe_backend.h"
/* 初始化 guard 与选定后端，输出仍受采样资格约束。 */
void bms_afe_init(void);
/* 通过安全门禁采样并更新恢复资格及输出授权。 */
void bms_afe_sample(void);
/* 通过 guard 执行选定 AFE 的休眠流程。 */
uint8_t bms_afe_sleep(void);
/* 安全门禁允许时应用独立硬件保护配置。 */
uint8_t bms_afe_apply_protection_config(void);
/* 保存充放电意图并按 guard 资格应用到后端。 */
uint8_t bms_afe_set_fets(uint8_t charge_on, uint8_t discharge_on);
/* 读取应用侧保存的充放电请求。 */
void bms_afe_get_requested_fets(uint8_t *charge_on, uint8_t *discharge_on);
/* 设置应用输出授权并重新仲裁 MOS 请求。 */
void bms_afe_set_output_enabled(uint8_t enabled);
/*
 * 仅在有意的看门狗等待窗口返回 0；绕过正常门禁的诊断/原始路径也须遵守，
 * 不能访问 AFE。
 */
uint8_t bms_afe_bus_access_allowed(void);
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
/* 主循环只读 RAM 查询，无 I2C 或 FET 命令。 */
uint8_t bms_afe_current_recovery_pending(void);
#else
/* 查询是否仍有电流保护等待物理条件恢复。 */
static inline uint8_t bms_afe_current_recovery_pending(void) { return 0u; }
#endif
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
/*
 * 受保护的 D008 台架生命周期：shutdown 保留 FET 请求但禁止全部 AFE 访问；
 * 唤醒完整初始化并确认新样本后才允许请求到达硬件。
 */
/*
 * 只有均衡关闭、FET 关闭命令和 shutdown 应答全部成功才返回成功；
 * 冷启动/受保护唤醒前不再访问 AFE。
 */
uint8_t bms_afe_enter_shutdown(void);
/* 测试构建中发起 AFE shutdown 并记录结果。 */
uint8_t bms_afe_test_enter_shutdown(void);
/* 测试构建中执行 AFE 唤醒并重新获取资格。 */
uint8_t bms_afe_test_wake(void);
#endif
typedef struct {
    uint16_t battery_ntc_mv;
    uint16_t mos_ntc_mv;
    uint32_t battery_ntc_100ohm;
    uint32_t mos_ntc_100ohm;
    uint32_t pack_voltage_mv;
    int32_t raw_current_ma;
    int32_t current_ma; /* mA：正放电，负充电 */
    uint32_t sample_tick_32k;
} bms_afe_aux_measurements_t;
/* 取得选定后端的辅助测量快照。 */
uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *sample);
#define BMS_AFE_FEATURE_MAX_CELLS 24u
/* 温度字段均使用 (摄氏度 + 40) * 10。 */
typedef struct {
    uint8_t valid;
    uint8_t cell_count;
    uint8_t battery_temp_valid;
    uint8_t heater_temp_valid;
    uint8_t mos_temp_valid;
    uint16_t battery_temp_min_x10;
    uint16_t battery_temp_max_x10;
    uint16_t heater_temp_x10;
    uint16_t mos_temp_x10;
} bms_afe_feature_snapshot_t;
typedef enum {
    BMS_AFE_DIAG_IDLE = 0u,
    BMS_AFE_DIAG_BUSY = 1u,
    BMS_AFE_DIAG_READY = 2u,
    BMS_AFE_DIAG_ERROR = 3u
} bms_afe_diag_state_t;
typedef struct {
    uint8_t valid;
    uint8_t determinate;
    uint8_t cell_count;
    uint32_t open_cell_mask;
    uint16_t diagnostic_cell_mv[BMS_AFE_FEATURE_MAX_CELLS];
} bms_afe_openwire_result_t;
/* 取得公共功能所需的后端采样快照。 */
uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *snapshot);
/* 取得后端可验证的充电源存在状态。 */
uint8_t bms_afe_get_charge_source_present(uint8_t *present);
/* 门禁允许时更新有效通道均衡请求。 */
uint8_t bms_afe_set_balance_mask(uint32_t cell_mask);
/* 读取当前后端均衡状态掩码。 */
uint8_t bms_afe_get_balance_mask(uint32_t *cell_mask);
/* 门禁允许时启动后端断线检测。 */
uint8_t bms_afe_openwire_start(void);
/* 通过 guard 推进后端断线检测阶段。 */
bms_afe_diag_state_t bms_afe_openwire_poll(bms_afe_openwire_result_t *result);

/* 取得输出禁止、通信隔离和恢复阶段诊断位。 */
uint16_t bms_afe_get_guard_diagnostic_bits(void);
/* 从选定后端刷新诊断，遵守总线静默门禁。 */
void bms_afe_diag_poll(void);

#endif
