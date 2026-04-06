#pragma once

#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"

#if (APP_FLASH_PROTECTION_ENABLE)
#include "flash_prot.h"
extern u16 flash_lockBlock_cmd;
#endif

#ifndef FLASH_STORE_VERIFY_CHUNK
#define FLASH_STORE_VERIFY_CHUNK  64u
#endif

#ifndef FLASH_STORE_PAGE_BYTES
#define FLASH_STORE_PAGE_BYTES    256u
#endif

static inline void flash_store_begin_modify(void)
{
#if (APP_FLASH_PROTECTION_ENABLE)
    flash_unlock();
#endif
}

static inline void flash_store_end_modify(void)
{
#if (APP_FLASH_PROTECTION_ENABLE)
    flash_lock(flash_lockBlock_cmd);
#endif
}

static inline int flash_store_verify_bytes(u32 addr, const u8 *buf, u32 len)
{
    u8 verify_buf[FLASH_STORE_VERIFY_CHUNK];
    u32 chunk;
    u32 i;

    while (len != 0u) {
        chunk = (len > FLASH_STORE_VERIFY_CHUNK) ? FLASH_STORE_VERIFY_CHUNK : len;
        flash_read_page(addr, (int)chunk, verify_buf);
        for (i = 0u; i < chunk; ++i) {
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

static inline int flash_store_verify_erased(u32 addr, u32 len)
{
    u8 verify_buf[FLASH_STORE_VERIFY_CHUNK];
    u32 chunk;
    u32 i;

    while (len != 0u) {
        chunk = (len > FLASH_STORE_VERIFY_CHUNK) ? FLASH_STORE_VERIFY_CHUNK : len;
        flash_read_page(addr, (int)chunk, verify_buf);
        for (i = 0u; i < chunk; ++i) {
            if (verify_buf[i] != 0xFFu) {
                return 0;
            }
        }
        addr += chunk;
        len -= chunk;
    }

    return 1;
}

static inline int flash_store_prog_checked(u32 addr, const u8 *buf, u32 len)
{
    u32 page_off;
    u32 chunk;
    u32 write_addr = addr;
    const u8 *write_buf = buf;
    u32 write_len = len;

    flash_store_begin_modify();
    while (write_len != 0u) {
        page_off = write_addr % FLASH_STORE_PAGE_BYTES;
        chunk = FLASH_STORE_PAGE_BYTES - page_off;
        if (chunk > write_len) {
            chunk = write_len;
        }

        flash_write_page(write_addr, (int)chunk, (u8 *)write_buf);
        write_addr += chunk;
        write_buf += chunk;
        write_len -= chunk;
    }
    flash_store_end_modify();

    return flash_store_verify_bytes(addr, buf, len);
}

static inline int flash_store_erase_sector_checked(u32 addr, u32 sector_size)
{
    flash_store_begin_modify();
    flash_erase_sector(addr);
    flash_store_end_modify();
    return flash_store_verify_erased(addr, sector_size);
}
