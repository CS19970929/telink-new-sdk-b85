/*
 * 文件功能：Flash 安全访问辅助接口；供存储层按已有锁、地址与保护条件操作。
 * bms/platform/telink/flash_store_safe.h；实际编译归属见各产品 sources.txt。
 */
#pragma once

#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"

#if (APP_FLASH_PROTECTION_ENABLE)
#include "flash_prot.h"
extern u16 flash_lockBlock_cmd;
/* 判断 Flash 操作后是否需要恢复保护锁。 */
int app_flash_lock_restore_enabled(void);
#endif

#ifndef FLASH_STORE_VERIFY_CHUNK
#define FLASH_STORE_VERIFY_CHUNK  64u
#endif

#ifndef FLASH_STORE_PAGE_BYTES
#define FLASH_STORE_PAGE_BYTES    256u
#endif

/* 检查 OTA 与 Flash 保护条件并开始修改事务。 */
static inline void flash_store_begin_modify(void)
{
#if (APP_FLASH_PROTECTION_ENABLE)
    if (app_flash_lock_restore_enabled()) {
        flash_unlock();
    }
#endif
}

/* 结束 Flash 修改并恢复先前保护状态。 */
static inline void flash_store_end_modify(void)
{
#if (APP_FLASH_PROTECTION_ENABLE)
    if (app_flash_lock_restore_enabled()) {
        flash_lock(flash_lockBlock_cmd);
    }
#endif
}

/* 回读比较 Flash 字节以验证编程结果。 */
static inline int flash_store_verify_bytes(u32 addr, const u8 *buf, u32 len)
{
    u8 verify_buf[FLASH_STORE_VERIFY_CHUNK];

    while (len != 0u) {
        u32 chunk = (len > FLASH_STORE_VERIFY_CHUNK) ? FLASH_STORE_VERIFY_CHUNK : len;
        flash_read_page(addr, (int)chunk, verify_buf);
        for (u32 i = 0u; i < chunk; ++i) {
            if (verify_buf[i] != buf[i]) {
                return 0;
            }
        }
        addr += chunk;
        buf += chunk;
        len -= chunk;
    }

    return 1;
}

/* 检查擦除区域是否全部为擦除值。 */
static inline int flash_store_verify_erased(u32 addr, u32 len)
{
    u8 verify_buf[FLASH_STORE_VERIFY_CHUNK];

    while (len != 0u) {
        u32 chunk = (len > FLASH_STORE_VERIFY_CHUNK) ? FLASH_STORE_VERIFY_CHUNK : len;
        flash_read_page(addr, (int)chunk, verify_buf);
        for (u32 i = 0u; i < chunk; ++i) {
            if (verify_buf[i] != 0xFFu) {
                return 0;
            }
        }
        addr += chunk;
        len -= chunk;
    }

    return 1;
}
