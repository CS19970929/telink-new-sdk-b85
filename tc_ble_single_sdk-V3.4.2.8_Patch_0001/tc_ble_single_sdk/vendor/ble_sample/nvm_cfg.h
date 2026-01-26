#pragma once
/**
 * NVM 配置文件：按你的 flash 分区修改
 * - Telink 常见为 4KB sector erase
 * - 写入通常有 page/word 对齐要求（由 flash 芯片 & SDK 决定）
 */

#include <stdint.h>

// ===== Flash 区域配置 =====
// 例：保留 32KB 给 NVM：8 个 sector（8 * 4KB）
#ifndef NVM_FLASH_BASE
#define NVM_FLASH_BASE      (0x78000u)     // TODO: 改成你的 NVM 起始地址
#endif

#ifndef NVM_FLASH_SECTORS
#define NVM_FLASH_SECTORS   (8u)           // TODO: 改成你的 NVM sector 数量
#endif

// ===== 基本参数 =====
#define NVM_SECTOR_SIZE         (4096u)
#define NVM_MAGIC               (0x4E564D31u)   // 'NVM1'
#define NVM_VERSION             (1u)

// KV：记录对齐（建议 4 或 16，依实际 flash 写入粒度）
#ifndef NVM_WRITE_ALIGN
#define NVM_WRITE_ALIGN         (4u)
#endif

// KV：单条 value 最大长度（按你的参数规模改）
#ifndef NVM_KV_MAX_VALUE
#define NVM_KV_MAX_VALUE        (256u)
#endif

// KV：key 最大长度（字符串 key）
#ifndef NVM_KV_MAX_KEY
#define NVM_KV_MAX_KEY          (32u)
#endif

// KV：RAM 索引容量（key 数量上限）
#ifndef NVM_KV_MAX_KEYS
#define NVM_KV_MAX_KEYS         (64u)
#endif

// LOG：事件条目容量（按结构体大小估算）
#ifndef NVM_LOG_MAX_ITEMS
#define NVM_LOG_MAX_ITEMS       (256u)
#endif

// ===== 可选：并发临界区（默认空） =====
// 若 flash 访问可能被中断/任务打断，请实现以下宏（关中断/互斥锁）
#ifndef NVM_ENTER_CRITICAL
#define NVM_ENTER_CRITICAL()    do{}while(0)
#endif
#ifndef NVM_EXIT_CRITICAL
#define NVM_EXIT_CRITICAL()     do{}while(0)
#endif


// ===== BLE 时序友好的 Flash I/O 限制 =====
// 外置 SPI Flash 的 page program 通常是 256B 且不能跨页；为了减少一次 Flash 操作阻塞 BLE 的时间，
// 我们在 flash_if.c 内部做分页 + 分块。
/* SPI Flash 页大小（典型 256B）。写入必须不跨页。 */
#ifndef NVM_FLASH_PAGE_SIZE
#define NVM_FLASH_PAGE_SIZE      (256u)
#endif

// 单次 read/write 的最大字节数（越小越“BLE 友好”，但写入次数会更多）
/* 单次 Flash 读最大字节数：越小越不影响 BLE，但越慢。 */
#ifndef NVM_FLASH_MAX_READ_BYTES
#define NVM_FLASH_MAX_READ_BYTES (128u)
#endif
/* 单次 Flash 写最大字节数：越小越不影响 BLE，但越慢。 */

#ifndef NVM_FLASH_MAX_WRITE_BYTES
#define NVM_FLASH_MAX_WRITE_BYTES (64u)
#endif

// 可选：分块之间插入一个很小的间隔（us）。默认 0（不阻塞）。
/* 每个小块 Flash 操作之间插入的延时（微秒）。用于给 BLE 留时间片。 */
#ifndef NVM_FLASH_OP_GAP_US
#define NVM_FLASH_OP_GAP_US      (0u)
#endif

// 你可以把下面两个宏实现成：关/开中断（不建议长时间关），或者一个互斥锁
#ifndef FLASH_IF_ENTER_CRITICAL
#define FLASH_IF_ENTER_CRITICAL()    NVM_ENTER_CRITICAL()
#endif
#ifndef FLASH_IF_EXIT_CRITICAL
#define FLASH_IF_EXIT_CRITICAL()     NVM_EXIT_CRITICAL()
#endif

// 可选：微延时函数（用于 NVM_FLASH_OP_GAP_US > 0 时）
#ifndef NVM_DELAY_US
#define NVM_DELAY_US(us)         do{ (void)(us); }while(0)
#endif
