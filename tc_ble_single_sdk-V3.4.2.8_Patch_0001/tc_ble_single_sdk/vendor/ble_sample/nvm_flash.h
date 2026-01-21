#pragma once
#include <stdint.h>
// #include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ====== 可按需改 ======
#ifndef NVM_SECTOR_SIZE
#define NVM_SECTOR_SIZE          4096u
#endif

#ifndef NVM_PAGE_SIZE
#define NVM_PAGE_SIZE            256u
#endif

// BLE timing 友好限制（来自手册经验值）
#ifndef NVM_MAX_READ_CHUNK
#define NVM_MAX_READ_CHUNK       64u   // flash_read_page <= 64B 更安全
#endif

#ifndef NVM_MAX_WRITE_CHUNK
#define NVM_MAX_WRITE_CHUNK      16u   // flash_write_page <= 16B（GD 等）
#endif

// 记录对齐（写入拆分也会按 16B）
#ifndef NVM_ALIGN
#define NVM_ALIGN                16u
#endif

// ====== 你的平台需要提供 Telink Flash API ======
// 这些通常在 Telink SDK 里已经有：flash_read_page / flash_write_page / flash_erase_sector
// void flash_read_page(uint32_t addr, uint32_t len, uint8_t *buf);
// void flash_write_page(uint32_t addr, uint32_t len, uint8_t *buf);
// void flash_erase_sector(uint32_t addr);

// ====== 你需要实现的“策略钩子” ======
// 连接态一般不允许 erase；advertising / idle 可允许。
// 返回 1：允许擦除；0：不允许擦除
int nvm_platform_can_erase_now(void);

// 低压保护（可选但强烈建议）：返回 1 表示电压安全
int nvm_platform_flash_voltage_ok(void);

// 喂狗（可选）
void nvm_platform_wd_clear(void);

// ====== NVM 接口 ======
typedef enum {
    NVM_OK = 0,
    NVM_ERR_PARAM = -1,
    NVM_ERR_NOT_FOUND = -2,
    NVM_ERR_NO_SPACE = -3,
    NVM_ERR_BUSY = -4,
    NVM_ERR_VOLTAGE = -5,
} nvm_rc_t;

typedef struct {
    uint32_t base_addr;      // NVM 区域起始地址（sector 对齐）
    uint32_t sector_count;   // NVM 使用多少个 sector（建议 2~4）
} nvm_cfg_t;

nvm_rc_t nvm_init(const nvm_cfg_t *cfg);
nvm_rc_t nvm_get(uint16_t key, void *out, uint16_t max_len, uint16_t *out_len);
nvm_rc_t nvm_set(uint16_t key, const void *data, uint16_t len);

// 主循环里周期调用：用于做“延迟擦除/垃圾回收”
void nvm_process(void);

// 调试/维护
void nvm_debug_dump_state(uint32_t *out_write_addr, uint32_t *out_active_sector);

#ifdef __cplusplus
}
#endif
