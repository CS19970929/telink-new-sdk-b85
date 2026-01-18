#pragma once
#include "tl_common.h"
#include "drivers.h"
#include "conf.h"

#ifdef __cplusplus
extern "C" {
#endif

// ====== 你要自己确保这两个地址不冲突、4KB对齐 ======
#ifndef FLASH_ADR_SOC_A
#define FLASH_ADR_SOC_A   FLASH_ADDR_USER_DATA_BASE1
#endif
#ifndef FLASH_ADR_SOC_B
#define FLASH_ADR_SOC_B   (FLASH_ADR_SOC_A + FLASH_SECTOR_SIZE)
#endif
#ifndef SOC_SECTOR_SIZE
#define SOC_SECTOR_SIZE   4096
#endif

// 0：每条记录 2 字节（极省空间）
// 1：每条记录 4 字节（word + ~word，更抗掉电撕裂，BMS建议默认用这个）
#ifndef SOC_STORE_REDUNDANT
#define SOC_STORE_REDUNDANT  1
#endif

#define SOC_VAL_MAX_SAFE   16382   // 0..16382 可用（16383保留，避免与0xFFFF冲突）

typedef enum {
    SOC_ITEM_SOC   = 0,
    SOC_ITEM_SOH   = 1,
    SOC_ITEM_CYCLE = 2,
} soc_item_t;

typedef struct {
    u16 soc;    // 整数
    u16 dsg;    // 整数
    u16 cycle;  // 整数
} soc_kv_data_t;

typedef struct {
    u32 active_base;
    u32 write_off;
    u8  loaded;
} soc_kv_dbg_t;

/**
 * @brief 初始化：扫描 A/B 扇区，恢复最新 soc/soh/cycle
 */
int  soc_kv_store_init(void);

/**
 * @brief 获取当前缓存值（已恢复 + 运行中更新）
 */
soc_kv_data_t soc_kv_store_get(void);

/**
 * @brief 直接写一条日志记录（不做“变化判断”）
 * @return 1 成功，0 失败
 */
int  soc_kv_store_put(soc_item_t item, u16 value);

/**
 * @brief 高层接口：按“变化>=1就记录一次”的规则更新并写入
 *        你每次计算出新值就调用这个（或定期调用也行）
 */
void soc_kv_store_update_and_log_if_changed(u16 soc, u16 soh, u16 cycle);

/**
 * @brief 出厂清空
 */
void soc_kv_store_factory_reset(void);

soc_kv_dbg_t soc_kv_store_get_dbg(void);

#ifdef __cplusplus
}
#endif
