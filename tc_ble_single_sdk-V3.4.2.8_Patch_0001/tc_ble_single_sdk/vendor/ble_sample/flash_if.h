/**
 * @file    flash_if.h
 *
 * @brief   Flash 适配层接口声明（解耦 NVM 与 Telink SDK 的 flash_* API）
 *
 *          实现位于 flash_if.c，内部包含 BLE 友好的分块策略。
 */

#pragma once
/**
 * Flash 适配层：把 Telink SDK 的 flash 读写擦接口封装成统一 API
 * 目标：让上层 KV/LOG 不依赖具体 SDK 版本
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 读：任意地址任意长度
int flash_if_read(uint32_t addr, void *buf, size_t len);

// 写：要求不跨 sector；对齐/最小写入粒度由实现保证
int flash_if_write(uint32_t addr, const void *buf, size_t len);

// 擦：4KB sector
int flash_if_erase_sector(uint32_t sector_addr);

// 基本信息
uint32_t flash_if_sector_size(void);

#ifdef __cplusplus
}
#endif
