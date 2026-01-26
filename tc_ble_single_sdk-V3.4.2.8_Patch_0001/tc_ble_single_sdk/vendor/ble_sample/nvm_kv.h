/**
 * @file    nvm_kv.h
 *
 * @brief   KV 参数存储接口（追加写 + 轮转/压缩 GC）
 */

#pragma once
#include "nvm_types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化（扫描区域，重建 RAM 索引）
nvm_status_t nvm_kv_init(void);

// 读写删
nvm_status_t nvm_kv_get(const char *key, void *out, size_t *inout_len);
nvm_status_t nvm_kv_set(const char *key, const void *val, size_t len);
nvm_status_t nvm_kv_del(const char *key);

// 统计
typedef struct {
    uint32_t active_sector;
    uint32_t write_off;
    uint32_t used_bytes;
    uint32_t total_bytes;
    uint32_t keys;
    uint32_t gc_count;
} nvm_kv_stats_t;

void nvm_kv_get_stats(nvm_kv_stats_t *st);

#ifdef __cplusplus
}
#endif
