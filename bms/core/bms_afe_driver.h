/* sources.txt 只选择一个后端；后端直接实现以下接口，没有运行时函数表。 */
#ifndef BMS_AFE_DRIVER_H_
#define BMS_AFE_DRIVER_H_
#include "bms_afe.h"

/* 固定芯片语义，不属于用户参数，也不进入业务持久记录。 */
typedef struct {
    uint32_t failsafe_wait_ms;
    uint8_t reset_protection_before_init;
    uint8_t sleep_holds_bus;
} bms_afe_driver_policy_t;
extern const bms_afe_driver_policy_t bms_afe_driver_policy;

void bms_afe_driver_init(void);
bms_afe_sample_status_t bms_afe_driver_sample(void);
/* 最后一次命令成功仅确认 AFE 事务，不代表实测 MOS/Gate 状态。 */
uint8_t bms_afe_driver_sleep(void);
uint8_t bms_afe_driver_apply_protection_config(void);
uint8_t bms_afe_driver_set_fets(uint8_t charge_on, uint8_t discharge_on);
/* 无可信测量时只允许关闭指定方向及已确认保护方向，不重放另一侧开启命令。 */
uint8_t bms_afe_driver_disable_fets(uint8_t charge_off, uint8_t discharge_off);

/* 最新芯片方向仅临时覆盖用户单管关闭，不能绕过保护或公共门禁。 */
#define BMS_AFE_DIRECTION_ALLOW_CHARGE_MASK    0x01u
#define BMS_AFE_DIRECTION_ALLOW_DISCHARGE_MASK 0x02u
uint8_t bms_afe_driver_get_direction_allow_mask(void);
/* 主循环只读 RAM；sample 为 NULL 时仅查询当前有效性，不复制、不改变资格。 */
uint8_t bms_afe_driver_get_aux_measurements(bms_afe_aux_measurements_t *sample);
uint8_t bms_afe_driver_get_feature_snapshot(bms_afe_feature_snapshot_t *snapshot);
uint8_t bms_afe_driver_get_charge_source_present(uint8_t *present);
/* 成功必须已回读确认实际掩码；失败不能发布请求值为已应用值。 */
uint8_t bms_afe_driver_set_balance_mask(uint32_t cell_mask);
uint8_t bms_afe_driver_get_balance_mask(uint32_t *cell_mask);

void bms_afe_publish_fet_status(uint8_t driver_bits, uint8_t valid);
#if BMS_SERVICE_DIAG_ENABLE
/* 仅诊断读取；总线静默与主动休眠需要不同的阻断原因。 */
uint8_t bms_afe_shutdown_held(void);
#endif
#endif
