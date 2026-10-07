/*
 * 文件功能：SOC/循环数据的状态记录；
 * 管理缓存、变化保存和恢复默认入口。
 * bms/core/bms_state_store.h；实际编译归属见各产品 sources.txt。
 */
#pragma once

/* Storage V1 的 State 域保存 SOC 和循环检查点。 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BMS_STATE_DEFAULT_SOC
#define BMS_STATE_DEFAULT_SOC    60u
#endif
#ifndef BMS_STATE_DEFAULT_DSG
#define BMS_STATE_DEFAULT_DSG    0u
#endif
#ifndef BMS_STATE_DEFAULT_CYCLE
#define BMS_STATE_DEFAULT_CYCLE  0u
#endif

typedef struct {
    uint32_t soc;
    uint32_t dsg;
    uint32_t cycle;
} bms_state_store_data_t;

/* 加载并验证持久状态，建立当前缓存。 */
int  bms_state_store_init(void);
/* 取得 SOC 和循环状态默认值。 */
bms_state_store_data_t bms_state_store_get_default_data(void);
/* 取得缓存的 SOC 和循环状态。 */
bms_state_store_data_t bms_state_store_get(void);
/* 保存完整状态并按结果发布缓存。 */
int  bms_state_store_write_all(uint32_t soc, uint32_t dsg, uint32_t cycle);
/* 仅在状态变化且策略允许时提交检查点。 */
void bms_state_store_update_and_log_if_changed(uint32_t soc, uint32_t dsg, uint32_t cycle);
/* 更新 SOC 与循环次数状态并保存。 */
int bms_state_store_set_soc_cycle(uint32_t soc, uint32_t dsg, uint32_t cycle);

#ifdef __cplusplus
}
#endif
