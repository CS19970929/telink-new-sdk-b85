/**
 * @file    flash_if.c
 *
 * @brief   Telink SPI Flash 读/写/擦 适配层（BLE 时序友好版）
 *
 *          内部强制：写不跨 256B page；读写按小块分段；擦除 4KB 对齐。
 *          通过宏可控制每块大小、是否写后校验、是否在块间插入延时。
 */

#include "flash_if.h"
#include "nvm_dbg.h"
#include "nvm_cfg.h"
#include <string.h>

/*
 * Telink 常见 SDK（825x/827x/B85/B91/…）外置 SPI Flash API：
 *   void flash_read_page(unsigned long addr, unsigned long len, unsigned char *buf);
 *   void flash_write_page(unsigned long addr, unsigned long len, const unsigned char *buf);
 *   void flash_erase_sector(unsigned long addr); // 4KB
 *
 * 说明：
 * - SPI Flash 的 page program 通常为 256B，且“单次写入不能跨 256B 页边界”。
 * - 为了降低 Flash 操作对 BLE 时序的影响，这里做了：
 *   1) 写入强制按页边界拆分
 *   2) 读写再按 NVM_FLASH_MAX_*_BYTES 进一步分块（越小越 BLE 友好）
 *   3) 每个小块内可选临界区（建议：短临界区；不要长时间关中断）
 */

#if defined(__TELINK__) || defined(TELINK_SDK) || defined(MCU_CORE_TYPE)
/* 大多数 Telink 工程会有 drivers.h / flash.h；你按你的工程 include 调整即可 */
#include "drivers.h"
#endif

#ifndef NVM_FLASH_PAGE_SIZE
#define NVM_FLASH_PAGE_SIZE      256u
#endif

#ifndef NVM_FLASH_MAX_READ_BYTES
#define NVM_FLASH_MAX_READ_BYTES 128u
#endif

#ifndef NVM_FLASH_MAX_WRITE_BYTES
#define NVM_FLASH_MAX_WRITE_BYTES 64u
#endif

#ifndef NVM_FLASH_OP_GAP_US
#define NVM_FLASH_OP_GAP_US 0u
#endif

#ifndef NVM_FLASH_VERIFY_AFTER_WRITE
// 0: 不校验（最快）；1: 写后读回校验（更稳，稍慢）
#define NVM_FLASH_VERIFY_AFTER_WRITE 0
#endif

#ifndef FLASH_IF_ENTER_CRITICAL
#define FLASH_IF_ENTER_CRITICAL()    do{}while(0)
#endif
#ifndef FLASH_IF_EXIT_CRITICAL
#define FLASH_IF_EXIT_CRITICAL()     do{}while(0)
#endif

#ifndef NVM_DELAY_US
#define NVM_DELAY_US(us)             do{ (void)(us); }while(0)
#endif

// ---------- 基础 Telink Flash API 绑定（如需适配不同 SDK，在这里改） ----------
static inline void _tl_flash_read(uint32_t addr, uint32_t len, uint8_t *buf){
    // Telink 常见：flash_read_page(addr, len, buf);
    flash_read_page(addr, len, buf);
}

static inline void _tl_flash_write(uint32_t addr, uint32_t len, const uint8_t *buf){
    // Telink 常见：flash_write_page(addr, len, buf);
    flash_write_page(addr, len, buf);
}

static inline void _tl_flash_erase_sector(uint32_t sector_addr){
    // Telink 常见：flash_erase_sector(sector_addr);
    flash_erase_sector(sector_addr);
}
// ----------------------------------------------------------------------

// 计算：当前地址到本页末尾还能写多少
/**
 * @brief  计算从 addr 到本 256B page 末尾剩余的字节数
 * @note   SPI Flash Page Program 通常要求：一次写入不能跨 page 边界。
 */
static inline uint32_t _page_remain(uint32_t addr){
    uint32_t off = addr % (uint32_t)NVM_FLASH_PAGE_SIZE;
    return (uint32_t)NVM_FLASH_PAGE_SIZE - off;
}

static inline uint32_t _min_u32(uint32_t a, uint32_t b){ return a < b ? a : b; }

// “BLE 友好”的读：按最大块大小分段（读一般比写快，但也做限制）
/**
 * @brief  BLE 友好的 Flash 读取：按固定小块分段读取
 * @param  addr Flash 绝对地址
 * @param  buf  输出缓冲区
 * @param  len  读取长度
 * @return 0 成功；非 0 失败
 *
 * @details
 * - 读操作一般比擦/写轻，但一次读太长也可能占用较长时间片；
 *   因此默认按 NVM_FLASH_MAX_READ_BYTES 分段读取。
 * - 每段内部可选进入临界区（建议：短临界区）。
 */
static int telink_flash_read_limited(uint32_t addr, uint8_t *buf, uint32_t len){
    NVM_LOGT("flash_read req addr=0x%08X len=%u", (unsigned)addr, (unsigned)len);
    uint32_t done = 0;
    while(done < len){
        uint32_t chunk = _min_u32((uint32_t)NVM_FLASH_MAX_READ_BYTES, len - done);
        FLASH_IF_ENTER_CRITICAL();
        NVM_LOGT("flash_read chunk addr=0x%08X len=%u", (unsigned)(addr + done), (unsigned)chunk);
        _tl_flash_read(addr + done, chunk, buf + done);
        FLASH_IF_EXIT_CRITICAL();
        if(NVM_FLASH_OP_GAP_US) NVM_DELAY_US(NVM_FLASH_OP_GAP_US);
        done += chunk;
    }
    return 0;
}

/*
 * “BLE 友好”的写：
 * 1) 不能跨 page（256B）边界：先按页拆分
 * 2) 每页内再按 NVM_FLASH_MAX_WRITE_BYTES 拆分
 *
 * 注意：SPI Flash 写入只能把 1->0，不允许 0->1。
 * 上层必须保证目标区域已擦除（0xFF），本 NVM 模块设计就是 append + erase sector，所以满足。
 */
/**
 * @brief  BLE 友好的 Flash 写入：强制“先按 page 边界拆分，再按小块限制写入”
 * @param  addr Flash 绝对地址
 * @param  buf  源数据
 * @param  len  写入长度
 * @return 0 成功；非 0 失败
 *
 * @important
 * 1) 写入不能跨 256B page（SPI Flash 的 Page Program 典型限制）
 * 2) 写入只能把 bit 从 1 写成 0；上层必须保证目标区域已擦除为 0xFF
 * 3) 为减少对 BLE 的影响，本函数把一次写拆成多个“短写块”
 */
static int telink_flash_write_limited(uint32_t addr, const uint8_t *buf, uint32_t len){
    NVM_LOGT("flash_write req addr=0x%08X len=%u", (unsigned)addr, (unsigned)len);
    uint32_t done = 0;
    while(done < len){
        uint32_t remain_in_page = _page_remain(addr + done);
        uint32_t chunk = _min_u32(len - done, remain_in_page);
        // 再按“单次写最大”限制
        while(chunk){
            uint32_t c2 = _min_u32(chunk, (uint32_t)NVM_FLASH_MAX_WRITE_BYTES);
            FLASH_IF_ENTER_CRITICAL();
            NVM_LOGT("flash_write chunk addr=0x%08X len=%u (page_rem=%u)", (unsigned)(addr + done), (unsigned)c2, (unsigned)_page_remain(addr + done));
            _tl_flash_write(addr + done, c2, buf + done);
#if NVM_FLASH_VERIFY_AFTER_WRITE
            {
                uint8_t vr[128];
                uint32_t vlen = c2 > sizeof(vr) ? (uint32_t)sizeof(vr) : c2;
                _tl_flash_read(addr + done, vlen, vr);
                if(memcmp(vr, buf + done, vlen) != 0){
                    NVM_LOGE("flash_verify FAIL addr=0x%08X len=%u", (unsigned)(addr + done), (unsigned)vlen);
                    nvm_dbg_hexdump("expect", buf + done, vlen);
                    nvm_dbg_hexdump("readbk", vr, vlen);
                    return -9;
                }
            }
#endif

            FLASH_IF_EXIT_CRITICAL();
            if(NVM_FLASH_OP_GAP_US) NVM_DELAY_US(NVM_FLASH_OP_GAP_US);
            done += c2;
            chunk -= c2;
        }
    }
    return 0;
}

/**
 * @brief  擦除 4KB 扇区（强制地址对齐检查）
 * @param  addr 扇区起始地址（必须 4KB 对齐）
 * @return 0 成功；非 0 失败
 *
 * @warning 擦除是最重的 Flash 操作，时间长、寿命损耗大；
 *          建议仅在 NVM GC/轮转时发生，并尽量避免频繁擦除。
 */
static int telink_flash_erase_sector_checked(uint32_t addr){
    NVM_LOGD("flash_erase_sector req addr=0x%08X", (unsigned)addr);
    // 强制 4KB 对齐（Telink 常见 sector = 4096）
    if((addr & (NVM_SECTOR_SIZE - 1u)) != 0u){
        return -2;
    }
    FLASH_IF_ENTER_CRITICAL();
    NVM_LOGD("flash_erase_sector do addr=0x%08X", (unsigned)addr);
    _tl_flash_erase_sector(addr);
    FLASH_IF_EXIT_CRITICAL();
    return 0;
}

// ------------------ 对外统一接口 ------------------
int flash_if_read(uint32_t addr, void *buf, size_t len){
    NVM_LOGT("flash_if_read addr=0x%08X len=%u", (unsigned)addr, (unsigned)len);
    if(!buf || !len) return -1;
    return telink_flash_read_limited(addr, (uint8_t*)buf, (uint32_t)len);
}

int flash_if_write(uint32_t addr, const void *buf, size_t len){
    NVM_LOGT("flash_if_write addr=0x%08X len=%u", (unsigned)addr, (unsigned)len);
    if(!buf || !len) return -1;
    return telink_flash_write_limited(addr, (const uint8_t*)buf, (uint32_t)len);
}

int flash_if_erase_sector(uint32_t sector_addr){
    NVM_LOGD("flash_if_erase_sector addr=0x%08X", (unsigned)sector_addr);
    return telink_flash_erase_sector_checked(sector_addr);
}

uint32_t flash_if_sector_size(void){
    return NVM_SECTOR_SIZE;
}
