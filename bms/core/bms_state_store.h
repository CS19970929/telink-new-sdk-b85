/*
 * 文件功能：SOC/循环/学习数据及运行分钟数的状态记录；
 * 管理缓存、变化保存和恢复默认入口。
 * bms/core/bms_state_store.h；实际编译归属见各产品 sources.txt。
 */
#pragma once

/* Storage V1 的 State 域保存 SOC、循环、学习容量及运行时检查点。 */

#include "tl_common.h"
#include "conf.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BMS_STATE_DEFAULT_SOC
#define BMS_STATE_DEFAULT_SOC    ((u32)FAC_INIT_soc)
#endif
#ifndef BMS_STATE_DEFAULT_DSG
#define BMS_STATE_DEFAULT_DSG    0u
#endif
#ifndef BMS_STATE_DEFAULT_CYCLE
#define BMS_STATE_DEFAULT_CYCLE  0u
#endif
#ifndef BMS_STATE_DEFAULT_LEARNED_CAPACITY
#define BMS_STATE_DEFAULT_LEARNED_CAPACITY 0u
#endif
#ifndef BMS_STATE_DEFAULT_FLAGS
#define BMS_STATE_DEFAULT_FLAGS 0u
#endif

#define BMS_STATE_FLAG_CAPACITY_LEARNED 0x00000001u
#define BMS_STATE_FLAG_LEARNING_META    0x00000002u
#define BMS_STATE_FLAG_LEARNING_ACTIVE  0x00000004u
#define BMS_STATE_FLAG_LOW_MASK         0x0000FFFFu
#define BMS_STATE_FLAG_NOMINAL_MASK     0xFFFF0000u
#define BMS_STATE_FLAG_NOMINAL_SHIFT    16u

typedef struct {
    u32 soc;
    u32 dsg;
    u32 cycle;
    u32 learned_capacity_0p1ah;
    u32 flags;
    u32 candidate_capacity_0p1ah;
    u32 valid_learning_count;
    u32 rejected_learning_count;
    u32 last_learning_reject_reason;
    u32 candidate_match_count;
} bms_state_store_data_t;

/* 加载并验证持久状态，建立当前缓存。 */
int  bms_state_store_init(void);
/* 取得 SOC、循环和学习状态默认值。 */
bms_state_store_data_t bms_state_store_get_default_data(void);
/* 取得缓存的 SOC、循环和学习状态。 */
bms_state_store_data_t bms_state_store_get(void);
/* 保存完整状态并按结果发布缓存。 */
int  bms_state_store_write_all(u32 soc, u32 dsg, u32 cycle);
/* 校验并更新待保存的学习元数据，不立即写 Flash。 */
int  bms_state_store_write_learning_meta(u32 learned_capacity_0p1ah, u32 flags,
                                         u32 candidate_capacity_0p1ah,
                                         u32 valid_learning_count,
                                         u32 rejected_learning_count,
                                         u32 last_learning_reject_reason,
                                         u32 candidate_match_count);
/* 仅在状态变化且策略允许时提交检查点。 */
void bms_state_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle);
/* 取得累计运行分钟数。 */
u32 bms_state_store_get_runtime_min(void);
/* 保存累计运行分钟数。 */
int bms_state_store_write_runtime_min(u32 runtime_min);
/* 恢复运行计时持久状态默认值。 */
int bms_state_store_reset_runtime(void);

/* 更新 SOC 与循环次数状态并保存。 */
int bms_state_store_set_soc_cycle(u32 soc, u32 dsg, u32 cycle);

#ifdef __cplusplus
}
#endif
