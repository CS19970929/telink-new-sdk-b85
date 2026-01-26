/**
 * @file    flash_if.h
 *
 * @brief   Flash 适配层接口声明（解耦 NVM 与 Telink SDK 的 flash_* API）
 *
 *          实现位于 flash_if.c，内部包含 BLE 友好的分块策略。
 */

#pragma once
#include "../../common/types.h"
/**
 * Flash 适配层：把 Telink SDK 的 flash 读写擦接口封装成统一 API
 * 目标：让上层 KV/LOG 不依赖具体 SDK 版本
 */
#ifdef __cplusplus
extern "C" {
#endif

// 读：任意地址任意长度
int flash_if_read(u32 addr, void *buf, u32 len);
// 写：要求不跨 sector；对齐/最小写入粒度由实现保证
int flash_if_write(u32 addr, const void *buf, u32 len);
// 擦：4KB sector
int flash_if_erase_sector(u32 sector_addr);
// 基本信息
u32 flash_if_sector_size(void);
#ifdef __cplusplus
}
#endif
