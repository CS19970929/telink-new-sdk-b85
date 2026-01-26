/**
 * @file    nvm_log.h
 *
 * @brief   事件日志接口（追加写，支持遍历/清空）
 */

#pragma once
#include "nvm_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 事件结构：按你的 BMS 定义（建议固定长度，便于 ring 存储）
#pragma pack(push,1)
typedef struct {
    uint32_t ts;     // 时间戳（秒/ms 都行，统一即可）
    uint16_t type;   // 事件类型
    uint16_t arg0;   // 参数
    uint32_t arg1;   // 参数
    uint32_t arg2;   // 参数
} nvm_log_event_t;
#pragma pack(pop)

nvm_status_t nvm_log_init(void);
nvm_status_t nvm_log_append(const nvm_log_event_t *e);

// 读最近 N 条（从最新往回）
nvm_status_t nvm_log_read_recent(nvm_log_event_t *out, size_t max_items, size_t *out_items);

// 清空日志（擦除日志区）
nvm_status_t nvm_log_clear(void);

#ifdef __cplusplus
}
#endif
