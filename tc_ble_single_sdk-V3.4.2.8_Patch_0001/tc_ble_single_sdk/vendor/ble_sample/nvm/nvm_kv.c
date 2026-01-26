/**
 * @file    nvm_kv.c
 *
 * @brief   KV 参数存储实现：断电安全的 append-only 记录结构，提供 set/get/compact
 */

#include "nvm_kv.h"
#include "nvm_cfg.h"
#include "nvm_dbg.h"

/*
 * ============================ KV 记录格式（断电安全） ============================
 * 每条记录为 append-only：写入一次后不再修改。
 * 更新同一 key：追加新记录，旧记录仍在 Flash，RAM 索引指向最新记录。
 * 扫描恢复：依次扫描每条记录，通过 MAGIC/长度/CRC 判断有效性。
 * compact/轮转：当空间不足时，把“每个 key 的最新记录”搬到新扇区，擦除旧扇区。
 */
#include "flash_if.h"
#include "crc16.h"
#include <string.h>

#pragma pack(push,1)
typedef struct {
    uint32_t magic;     // NVM_MAGIC
    uint16_t version;   // NVM_VERSION
    uint16_t hdr_crc;   // crc16 of fields after magic? (simple)
    uint32_t seq;       // monotonically increasing
    uint8_t  state;     // 0xA5 active, 0x5A old
    uint8_t  rsv[7];
} nvm_sector_hdr_t;

typedef struct {
    uint16_t rec_len;   // total record bytes (header+payload+pads)
    uint8_t  type;      // 1=SET,2=DEL
    uint8_t  flags;     // reserved
    uint32_t key_hash;  // FNV1a32
    uint8_t  key_len;   // bytes
    uint16_t val_len;   // bytes (SET only)
    uint16_t crc;       // crc16 over (type..payload)
    // followed by: key[key_len] + val[val_len] (if SET) + padding
} nvm_kv_rec_hdr_t;
#pragma pack(pop)

#define SECTOR_STATE_ACTIVE  (0xA5u)
#define SECTOR_STATE_OLD     (0x5Au)

#define REC_TYPE_SET (1u)
#define REC_TYPE_DEL (2u)

typedef struct {
    uint32_t hash;
    uint32_t addr;   // record header addr
    uint16_t val_len;
    uint8_t  key_len;
    uint8_t  valid;
    char     key[NVM_KV_MAX_KEY];
} kv_index_t;

static kv_index_t g_index[NVM_KV_MAX_KEYS];
static nvm_kv_stats_t g_st;

static uint32_t fnv1a32(const void *data, unsigned int len){
    const uint8_t *p = (const uint8_t*)data;
    uint32_t h = 2166136261u;
    for(unsigned int i=0;i<len;i++){
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t align_up(uint32_t v, uint32_t a){
    return (v + (a-1u)) & ~(a-1u);
}

static uint32_t sector_addr(uint32_t idx){
    return NVM_FLASH_BASE + idx * NVM_SECTOR_SIZE;
}

static int read_sector_hdr(uint32_t sidx, nvm_sector_hdr_t *hdr){
    if(flash_if_read(sector_addr(sidx), hdr, sizeof(*hdr)) != 0) return -1;
    return 0;
}

static int write_sector_hdr(uint32_t sidx, const nvm_sector_hdr_t *hdr){
    return flash_if_write(sector_addr(sidx), hdr, sizeof(*hdr));
}

static int erase_sector(uint32_t sidx){
    return flash_if_erase_sector(sector_addr(sidx));
}

static void index_clear(void){
    memset(g_index, 0, sizeof(g_index));
    memset(&g_st, 0, sizeof(g_st));
    g_st.total_bytes = NVM_FLASH_SECTORS * NVM_SECTOR_SIZE;
}

static int index_find_slot(const char *key, uint32_t hash, uint8_t key_len){
    for(int i=0;i<NVM_KV_MAX_KEYS;i++){
        if(g_index[i].valid && g_index[i].hash == hash && g_index[i].key_len == key_len &&
           memcmp(g_index[i].key, key, key_len) == 0){
            return i;
        }
    }
    return -1;
}

static int index_find_free(void){
    for(int i=0;i<NVM_KV_MAX_KEYS;i++){
        if(!g_index[i].valid) return i;
    }
    return -1;
}

static void index_set(const char *key, uint8_t key_len, uint32_t hash, uint32_t rec_addr, uint16_t val_len){
    int s = index_find_slot(key, hash, key_len);
    if(s < 0) s = index_find_free();
    if(s < 0) return; // 索引满：仍可写入 flash，但 get 会退化（你可扩展为 scan-on-demand）
    kv_index_t *it = &g_index[s];
    memset(it, 0, sizeof(*it));
    it->valid = 1;
    it->hash = hash;
    it->addr = rec_addr;
    it->val_len = val_len;
    it->key_len = key_len;
    memcpy(it->key, key, key_len);
    if(g_st.keys < NVM_KV_MAX_KEYS) g_st.keys++;
}

static void index_del(const char *key, uint8_t key_len, uint32_t hash){
    int s = index_find_slot(key, hash, key_len);
    if(s >= 0){
        g_index[s].valid = 0;
        if(g_st.keys) g_st.keys--;
    }
}

static int sector_is_blank(const nvm_sector_hdr_t *hdr){
    // flash blank usually 0xFF
    return (hdr->magic == 0xFFFFFFFFu);
}

static uint32_t find_active_sector(uint32_t *out_seq){
    nvm_sector_hdr_t hdr;
    uint32_t best = 0xFFFFFFFFu;
    uint32_t best_seq = 0;

    for(uint32_t i=0;i<NVM_FLASH_SECTORS;i++){
        if(read_sector_hdr(i, &hdr) != 0) continue;
        if(hdr.magic != NVM_MAGIC || hdr.version != NVM_VERSION) continue;
        if(hdr.state != SECTOR_STATE_ACTIVE) continue;
        if(best == 0xFFFFFFFFu || hdr.seq > best_seq){
            best = i;
            best_seq = hdr.seq;
        }
    }
    if(out_seq) *out_seq = best_seq;
    return best;
}

static uint32_t find_next_sector(uint32_t cur){
    return (cur + 1u) % NVM_FLASH_SECTORS;
}

static nvm_status_t format_new(uint32_t sidx, uint32_t seq){
    if(erase_sector(sidx) != 0) return NVM_ERR;
    nvm_sector_hdr_t hdr = {0};
    hdr.magic = NVM_MAGIC;
    hdr.version = NVM_VERSION;
    hdr.seq = seq;
    hdr.state = SECTOR_STATE_ACTIVE;
    // hdr_crc: 简单对 version..rsv 做 crc
    hdr.hdr_crc = crc16_ccitt(&hdr.version, sizeof(hdr) - offsetof(nvm_sector_hdr_t, version));
    if(write_sector_hdr(sidx, &hdr) != 0) return NVM_ERR;

    g_st.active_sector = sidx;
    g_st.write_off = align_up(sizeof(nvm_sector_hdr_t), NVM_WRITE_ALIGN);
    return NVM_OK;
}

static nvm_status_t mark_sector_old(uint32_t sidx){
    nvm_sector_hdr_t hdr;
    if(read_sector_hdr(sidx, &hdr) != 0) return NVM_ERR;
    if(hdr.magic != NVM_MAGIC) return NVM_ERR;
    hdr.state = SECTOR_STATE_OLD;
    hdr.hdr_crc = crc16_ccitt(&hdr.version, sizeof(hdr) - offsetof(nvm_sector_hdr_t, version));
    if(write_sector_hdr(sidx, &hdr) != 0) return NVM_ERR;
    return NVM_OK;
}

static nvm_status_t append_record(uint8_t type, const char *key, const void *val, uint16_t val_len){
    uint8_t key_len = (uint8_t)strnlen(key, NVM_KV_MAX_KEY);
    if(key_len == 0 || key_len >= NVM_KV_MAX_KEY) return NVM_BAD_PARAM;
    if(type == REC_TYPE_SET && (val_len == 0 || val_len > NVM_KV_MAX_VALUE)) return NVM_BAD_PARAM;

    uint32_t hash = fnv1a32(key, key_len);
    nvm_kv_rec_hdr_t rh = {0};
    rh.type = type;
    rh.key_hash = hash;
    rh.key_len = key_len;
    rh.val_len = (type == REC_TYPE_SET) ? val_len : 0;

    uint32_t payload_len = key_len + ((type == REC_TYPE_SET) ? val_len : 0);
    uint32_t rec_len = sizeof(rh) + payload_len;
    rec_len = align_up(rec_len, NVM_WRITE_ALIGN);
    rh.rec_len = (uint16_t)rec_len;

    // crc over type..payload
    // build a small temp buffer for header (without crc) + payload crc streaming
    uint16_t crc = 0xFFFFu;
    // crc16_ccitt doesn't support streaming; do simple: copy into temp if small
    // payload max: 32+256=288, plus header ~14 => < 310
    uint8_t tmp[320];
    uint32_t idx = 0;
    memcpy(&tmp[idx], &rh.type, offsetof(nvm_kv_rec_hdr_t, crc) - offsetof(nvm_kv_rec_hdr_t, type));
    idx += offsetof(nvm_kv_rec_hdr_t, crc) - offsetof(nvm_kv_rec_hdr_t, type);
    memcpy(&tmp[idx], key, key_len); idx += key_len;
    if(type == REC_TYPE_SET){
        memcpy(&tmp[idx], val, val_len); idx += val_len;
    }
    crc = crc16_ccitt(tmp, idx);
    rh.crc = crc;

    // space check
    uint32_t write_addr = sector_addr(g_st.active_sector) + g_st.write_off;
    uint32_t remain = NVM_SECTOR_SIZE - g_st.write_off;
    if(remain < rec_len){
        return NVM_NO_SPACE;
    }

    // write header then payload then padding (0xFF)
    // 注意：不要在这里长时间关中断（会影响 BLE 时序）。flash_if.c 内部已按小块进入临界区。
    int ok = 0;
    ok |= flash_if_write(write_addr, &rh, sizeof(rh));
    write_addr += sizeof(rh);
    ok |= flash_if_write(write_addr, key, key_len);
    write_addr += key_len;
    if(type == REC_TYPE_SET){
        ok |= flash_if_write(write_addr, val, val_len);
        write_addr += val_len;
    }
    // padding not necessary (stays 0xFF), but some flashes require full words; we just advance offset
    if(ok != 0) return NVM_ERR;

    uint32_t rec_hdr_addr = sector_addr(g_st.active_sector) + g_st.write_off;
    g_st.write_off += rec_len;
    g_st.used_bytes += rec_len;

    if(type == REC_TYPE_SET) index_set(key, key_len, hash, rec_hdr_addr, val_len);
    else index_del(key, key_len, hash);

    return NVM_OK;
}

static nvm_status_t scan_active_build_index(uint32_t sidx){
    // scan records in active sector
    uint32_t off = align_up(sizeof(nvm_sector_hdr_t), NVM_WRITE_ALIGN);
    nvm_kv_rec_hdr_t rh;

    while(off + sizeof(rh) <= NVM_SECTOR_SIZE){
        uint32_t addr = sector_addr(sidx) + off;
        if(flash_if_read(addr, &rh, sizeof(rh)) != 0) return NVM_ERR;

        // blank means end
        if(rh.rec_len == 0xFFFFu || rh.rec_len == 0u){
            break;
        }
        if(rh.rec_len < sizeof(rh) || rh.rec_len > (NVM_SECTOR_SIZE - off)){
            // bad record -> stop (or you can continue searching next aligned boundary)
            break;
        }

        // read key+val into temp for crc
        uint32_t payload_len = rh.key_len + rh.val_len;
        if(rh.key_len == 0 || rh.key_len >= NVM_KV_MAX_KEY) break;
        if(rh.val_len > NVM_KV_MAX_VALUE) break;

        uint8_t tmp[320];
        uint32_t idx = 0;
        // type..val_len (excluding crc)
        memcpy(&tmp[idx], &rh.type, offsetof(nvm_kv_rec_hdr_t, crc) - offsetof(nvm_kv_rec_hdr_t, type));
        idx += offsetof(nvm_kv_rec_hdr_t, crc) - offsetof(nvm_kv_rec_hdr_t, type);

        // payload
        if(payload_len > sizeof(tmp) - idx) break;
        if(flash_if_read(addr + sizeof(rh), &tmp[idx], payload_len) != 0) return NVM_ERR;
        idx += payload_len;

        uint16_t crc = crc16_ccitt(tmp, idx);
        if(crc == rh.crc){
            char key[NVM_KV_MAX_KEY] = {0};
            memcpy(key, &tmp[offsetof(nvm_kv_rec_hdr_t, crc) - offsetof(nvm_kv_rec_hdr_t, type)], rh.key_len);

            if(rh.type == REC_TYPE_SET){
                index_set(key, rh.key_len, rh.key_hash, addr, rh.val_len);
            }else if(rh.type == REC_TYPE_DEL){
                index_del(key, rh.key_len, rh.key_hash);
            }
        }else{
            // crc mismatch -> stop
            break;
        }

        off += align_up(rh.rec_len, NVM_WRITE_ALIGN);
    }

    g_st.active_sector = sidx;
    g_st.write_off = off;
    g_st.used_bytes = off; // rough
    return NVM_OK;
}

static nvm_status_t kv_compact(void){
    // compact from active sector into next sector
    uint32_t cur = g_st.active_sector;
    uint32_t next = find_next_sector(cur);

    // determine next seq
    nvm_sector_hdr_t hdr;
    uint32_t cur_seq = 0;
    if(read_sector_hdr(cur, &hdr) == 0 && hdr.magic == NVM_MAGIC) cur_seq = hdr.seq;

    nvm_status_t st = format_new(next, cur_seq + 1u);
    if(st != NVM_OK) return st;

    // copy latest values in index
    for(int i=0;i<NVM_KV_MAX_KEYS;i++){
        if(!g_index[i].valid) continue;

        // read record header to locate payload
        nvm_kv_rec_hdr_t rh;
        if(flash_if_read(g_index[i].addr, &rh, sizeof(rh)) != 0) continue;
        if(rh.type != REC_TYPE_SET) continue;

        // read key & val
        char key[NVM_KV_MAX_KEY]={0};
        uint8_t val[NVM_KV_MAX_VALUE];
        if(rh.key_len >= NVM_KV_MAX_KEY || rh.val_len > NVM_KV_MAX_VALUE) continue;

        if(flash_if_read(g_index[i].addr + sizeof(rh), key, rh.key_len) != 0) continue;
        if(flash_if_read(g_index[i].addr + sizeof(rh) + rh.key_len, val, rh.val_len) != 0) continue;

        // write into new sector
        append_record(REC_TYPE_SET, key, val, rh.val_len);
    }

    // mark old sector
    (void)mark_sector_old(cur);
    g_st.gc_count++;
    return NVM_OK;
}

nvm_status_t nvm_kv_init(void){
    index_clear();

    uint32_t seq = 0;
    uint32_t active = find_active_sector(&seq);

    if(active == 0xFFFFFFFFu){
        // no valid -> format sector 0
        return format_new(0, 1u);
    }
    return scan_active_build_index(active);
}

nvm_status_t nvm_kv_get(const char *key, void *out, unsigned int *inout_len){
    if(!key || !inout_len) return NVM_BAD_PARAM;
    uint8_t key_len = (uint8_t)strnlen(key, NVM_KV_MAX_KEY);
    if(key_len == 0 || key_len >= NVM_KV_MAX_KEY) return NVM_BAD_PARAM;

    uint32_t hash = fnv1a32(key, key_len);
    int slot = index_find_slot(key, hash, key_len);
    if(slot < 0) return NVM_NOT_FOUND;

    nvm_kv_rec_hdr_t rh;
    if(flash_if_read(g_index[slot].addr, &rh, sizeof(rh)) != 0) return NVM_ERR;
    if(rh.type != REC_TYPE_SET) return NVM_NOT_FOUND;

    unsigned int need = rh.val_len;
    if(!out){
        *inout_len = need;
        return NVM_OK;
    }
    if(*inout_len < need) {
        *inout_len = need;
        return NVM_NO_SPACE;
    }
    if(flash_if_read(g_index[slot].addr + sizeof(rh) + rh.key_len, out, rh.val_len) != 0) return NVM_ERR;
    *inout_len = need;
    return NVM_OK;
}

nvm_status_t nvm_kv_set(const char *key, const void *val, unsigned int len){
    if(!key || !val || len==0 || len > 0xFFFFu) return NVM_BAD_PARAM;
    nvm_status_t st = append_record(REC_TYPE_SET, key, val, (uint16_t)len);
    if(st == NVM_NO_SPACE){
        // do GC then retry once
        st = kv_compact();
        if(st != NVM_OK) return st;
        st = append_record(REC_TYPE_SET, key, val, (uint16_t)len);
    }
    return st;
}

nvm_status_t nvm_kv_del(const char *key){
    if(!key) return NVM_BAD_PARAM;
    nvm_status_t st = append_record(REC_TYPE_DEL, key, NULL, 0);
    if(st == NVM_NO_SPACE){
        st = kv_compact();
        if(st != NVM_OK) return st;
        st = append_record(REC_TYPE_DEL, key, NULL, 0);
    }
    return st;
}

void nvm_kv_get_stats(nvm_kv_stats_t *st){
    if(!st) return;
    *st = g_st;
}
