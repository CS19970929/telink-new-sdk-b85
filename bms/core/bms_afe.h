/*
 * 文件功能：应用侧 AFE 契约；采样资格、MOS 请求、保护配置、休眠与辅助测量由
 * guard/backend 实现。
 * bms/core/bms_afe.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_AFE_H_
#define BMS_AFE_H_
#include <stdint.h>
#include "bms_afe_backend.h"
#include "bms_diag_config.h"
#include "bms_timing.h"
/* 初始化 guard 与选定后端，完整测量和配置验证前禁止输出。 */
void bms_afe_init(void);
/* OK 表示本轮完整读取成功；转换完成事件只在后端用于校准及保护恢复。 */
typedef enum {
    BMS_AFE_SAMPLE_OK = 0,
    BMS_AFE_SAMPLE_NOT_READY,
    BMS_AFE_SAMPLE_ERROR,
    /* 可观察的业务总线事务失败；测量是否有效另由 getter 提供。
     * 不等同于明确芯片故障或需要完整重配的配置/清锁存失败。 */
    BMS_AFE_SAMPLE_READ_ERROR
} bms_afe_sample_status_t;
bms_afe_sample_status_t bms_afe_sample(void);
/* 应用在功能决策后调用；实际输出仍由 Guard 检查全部授权。 */
void bms_afe_apply_outputs(void);
/* 强制休眠接管寄存器冻结且保持输出关闭；DVC 请求 Shutdown，SH 请求 Sleep。
 * 总线静默等门禁拒绝或命令失败返回 0，调用方仍执行 MCU 深睡/断电。 */
uint8_t bms_afe_sleep(void);
/* 安全门禁允许时应用独立硬件保护配置。 */
uint8_t bms_afe_apply_protection_config(void);
/* 配置提交/回滚未验证时保持输出禁止，直到完整配置重新验证。 */
void bms_afe_invalidate_configuration(void);
/* 保存充放电意图并按 guard 资格应用到后端。 */
uint8_t bms_afe_set_fets(uint8_t charge_on, uint8_t discharge_on);
/* 读取应用侧保存的充放电请求。 */
void bms_afe_get_requested_fets(uint8_t *charge_on, uint8_t *discharge_on);
/* 读取最近一次 AFE 实际 FET 回读；返回 0 表示状态未知。 */
uint8_t bms_afe_get_fet_status(uint8_t *driver_bits);
/*
 * RAM 远程许可：channel 0=充电、1=放电；允许不绕过保护，禁止不请求休眠。
 * 用户禁止意图持续保留；AFE 确认反向电流期间只临时开通对应 MOS，方向消失后恢复关闭。
 */
uint8_t bms_afe_set_channel_allowed(uint8_t channel, uint8_t allowed);
uint8_t bms_afe_get_remote_allowed_mask(void);
/* 设置应用输出授权并重新仲裁 MOS 请求。 */
void bms_afe_set_output_enabled(uint8_t enabled);
/*
 * 看门狗总线静默或主动 shutdown 后返回 0；诊断/原始路径也须遵守，不能访问 AFE。
 */
uint8_t bms_afe_bus_access_allowed(void);
/* 主循环只读 RAM 查询，无总线事务或 FET 命令。 */
uint8_t bms_afe_current_recovery_pending(void);
#if defined(BMS_HOST_TEST) && BMS_HOST_TEST
/* 主动保持的休眠在重新初始化、完整测量和必要控制成功后恢复。 */
uint8_t bms_afe_test_wake(void);
#endif
typedef struct {
    int32_t raw_current_ma;
    int32_t current_ma; /* mA：正放电，负充电 */
    uint32_t sample_time_ms;
    uint32_t sample_tick_32k; /* 对外兼容观测，不参与业务超时。 */
} bms_afe_aux_measurements_t;
/* 取得选定后端的辅助测量快照。 */
uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *sample);
#define BMS_AFE_FEATURE_MAX_CELLS 24u
/* 温度字段均使用 (摄氏度 + 40) * 10。 */
typedef struct {
    uint8_t cell_count;
    uint8_t battery_temp_valid;
    uint8_t heater_temp_valid;
    uint8_t mos_temp_valid;
    uint16_t battery_temp_min_x10;
    uint16_t battery_temp_max_x10;
    uint16_t heater_temp_x10;
    uint16_t mos_temp_x10;
} bms_afe_feature_snapshot_t;
/* 成功返回 1 并完整填写快照；失败时调用方不得使用内容。 */
uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *snapshot);
/* 取得后端可验证的充电源存在状态。 */
uint8_t bms_afe_get_charge_source_present(uint8_t *present);
/* 有均衡能力时返回 1 代表实际硬件掩码已回读确认等于请求；总线静默返回 0。
 * known_actual 非空时只能指向本轮刚成功读取的实际值；失败/写入后不得复用。
 * 独立关闭、初始化及补偿传 NULL，由后端自行读取确认。 */
uint8_t bms_afe_set_balance_mask(uint32_t cell_mask, const uint32_t *known_actual);
/* 读取当前后端均衡状态掩码。 */
uint8_t bms_afe_get_balance_mask(uint32_t *cell_mask);
/* 取得输出禁止、通信隔离和恢复阶段诊断位。 */
uint16_t bms_afe_get_guard_diagnostic_bits(void);
#if BMS_SERVICE_DIAG_ENABLE
/* 从选定后端刷新开发诊断，遵守总线静默门禁。 */
void bms_afe_diag_poll(void);
#endif

#endif
