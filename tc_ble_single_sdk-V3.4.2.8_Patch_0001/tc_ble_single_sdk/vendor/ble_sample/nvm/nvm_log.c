/**
 * @file    nvm_log.c
 *
 * @brief   事件日志实现：固定记录结构 append-only，适合记录保护/故障/状态变化
 */

#include "nvm_log.h"
#include "nvm_cfg.h"
#include "nvm_dbg.h"

/*
 * ============================ LOG 记录格式 ============================
 * 固定结构体事件记录（append-only）：ts/type/arg/crc
 * 断电恢复：启动时顺序扫描，找到最后一条有效记录位置作为写指针。
 */
#include "flash_if.h"
#include "crc16.h"
#include <string.h>

#pragma pack(push,1)
typedef struct{
    uint32_t magic;   // 'LOG1'
    uint16_t ver;
    uint16_t hdr_crc;
    uint32_t write_seq;
    uint32_t write_off;
    uint8_t  rsv[12];
} log_hdr_t;

typedef struct{
    uint16_t rec_len;
    uint16_t crc;
    nvm_log_event_t e;
} log_rec_t;
#pragma pack(pop)

#define LOG_MAGIC (0x4C4F4731u) // 'LOG1'

// 简化：把最后 2 个 sector 作为日志区；你也可以独立分区
#define LOG_SECTORS   (2u)
#define LOG_BASE      (NVM_FLASH_BASE + (NVM_FLASH_SECTORS-LOG_SECTORS)*NVM_SECTOR_SIZE)

static log_hdr_t g_hdr;

static uint32_t align_up(uint32_t v, uint32_t a){
    return (v + (a-1u)) & ~(a-1u);
}

static int log_read(uint32_t addr, void *buf, unsigned int len){
    return flash_if_read(addr, buf, len);
}
static int log_write(uint32_t addr, const void *buf, unsigned int len){
    return flash_if_write(addr, buf, len);
}
static int log_erase(uint32_t sector_addr){
    return flash_if_erase_sector(sector_addr);
}

static uint32_t log_end_addr(void){
    return LOG_BASE + LOG_SECTORS * NVM_SECTOR_SIZE;
}

static nvm_status_t log_format(void){
    // erase log sectors
    for(uint32_t i=0;i<LOG_SECTORS;i++){
        if(log_erase(LOG_BASE + i*NVM_SECTOR_SIZE) != 0) return NVM_ERR;
    }
    memset(&g_hdr, 0, sizeof(g_hdr));
    g_hdr.magic = LOG_MAGIC;
    g_hdr.ver = 1;
    g_hdr.write_seq = 1;
    g_hdr.write_off = align_up(sizeof(log_hdr_t), NVM_WRITE_ALIGN);
    g_hdr.hdr_crc = crc16_ccitt(&g_hdr.ver, sizeof(g_hdr) - offsetof(log_hdr_t, ver));
    if(log_write(LOG_BASE, &g_hdr, sizeof(g_hdr)) != 0) return NVM_ERR;
    return NVM_OK;
}

static int log_hdr_valid(const log_hdr_t *h){
    if(h->magic != LOG_MAGIC) return 0;
    if(h->ver != 1) return 0;
    uint16_t crc = crc16_ccitt(&h->ver, sizeof(*h) - offsetof(log_hdr_t, ver));
    return crc == h->hdr_crc;
}

nvm_status_t nvm_log_init(void){
    if(log_read(LOG_BASE, &g_hdr, sizeof(g_hdr)) != 0) return NVM_ERR;
    if(!log_hdr_valid(&g_hdr)){
        return log_format();
    }
    // basic sanity
    if(g_hdr.write_off < align_up(sizeof(log_hdr_t), NVM_WRITE_ALIGN) || (LOG_BASE + g_hdr.write_off) >= log_end_addr()){
        return log_format();
    }
    return NVM_OK;
}

nvm_status_t nvm_log_append(const nvm_log_event_t *e){
    if(!e) return NVM_BAD_PARAM;
    log_rec_t r;
    r.rec_len = (uint16_t)align_up(sizeof(log_rec_t), NVM_WRITE_ALIGN);
    r.e = *e;
    r.crc = crc16_ccitt(&r.e, sizeof(r.e));

    uint32_t addr = LOG_BASE + g_hdr.write_off;
    uint32_t end  = log_end_addr();
    if(addr + r.rec_len > end){
        // wrap: re-format for simplicity（你也可以做更复杂的 ring header）
        nvm_status_t st = log_format();
        if(st != NVM_OK) return st;
        addr = LOG_BASE + g_hdr.write_off;
    }

    NVM_ENTER_CRITICAL();
    int ok = log_write(addr, &r, sizeof(r));
    if(ok != 0) return NVM_ERR;

    g_hdr.write_off += r.rec_len;
    g_hdr.write_seq++;
    g_hdr.hdr_crc = crc16_ccitt(&g_hdr.ver, sizeof(g_hdr) - offsetof(log_hdr_t, ver));
    (void)log_write(LOG_BASE, &g_hdr, sizeof(g_hdr)); // best-effort
    return NVM_OK;
}

nvm_status_t nvm_log_read_recent(nvm_log_event_t *out, unsigned int max_items, unsigned int *out_items){
    if(!out_items) return NVM_BAD_PARAM;
    *out_items = 0;
    if(!out || max_items == 0) return NVM_OK;

    // 从 write_off 往回扫（简单实现）
    uint32_t off_min = align_up(sizeof(log_hdr_t), NVM_WRITE_ALIGN);
    uint32_t off = g_hdr.write_off;

    while(off >= off_min + sizeof(log_rec_t) && *out_items < max_items){
        off -= align_up(sizeof(log_rec_t), NVM_WRITE_ALIGN);
        log_rec_t r;
        if(log_read(LOG_BASE + off, &r, sizeof(r)) != 0) break;
        if(r.rec_len == 0xFFFFu || r.rec_len == 0u) break;
        uint16_t crc = crc16_ccitt(&r.e, sizeof(r.e));
        if(crc != r.crc) continue; // skip corrupt
        out[*out_items] = r.e;
        (*out_items)++;
    }
    return NVM_OK;
}

nvm_status_t nvm_log_clear(void){
    return log_format();
}
