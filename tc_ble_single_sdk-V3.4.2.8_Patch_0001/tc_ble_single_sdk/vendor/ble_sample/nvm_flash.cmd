#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "nvm_flash.h"
// #include <string.h>

// ================== record format ==================
// [hdr][payload][pad]
// hdr 固定 16B（对齐 NVM_ALIGN）
#define NVM_MAGIC   0x4E564D31u  // 'NVM1'

typedef struct __attribute__((packed)) {
    uint32_t magic;     // NVM_MAGIC
    uint16_t key;       // 参数ID
    uint16_t len;       // payload长度
    uint32_t seq;       // 单调递增序号
    uint32_t crc32;     // hdr(除crc) + payload 的 crc32
} nvm_rec_hdr_t;

_Static_assert(sizeof(nvm_rec_hdr_t) == 16, "nvm_rec_hdr_t must be 16 bytes");

// ================== internal state ==================
static nvm_cfg_t   g_cfg;
static uint32_t    g_seq = 1;
static uint32_t    g_write_addr = 0;
static uint32_t    g_active_sector = 0;

// 需要回收的 sector（延迟擦除）
static int         g_gc_pending = 0;
static uint32_t    g_gc_sector_addr = 0;

// ================== tiny crc32 ==================
static uint32_t crc32_ieee(const void *data, size_t len)
{
    // 简单实现：够用但不追求极致性能
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t align_up(uint32_t x, uint32_t a) {
    return (x + a - 1u) & ~(a - 1u);
}

static uint32_t sector_addr(uint32_t idx) {
    return g_cfg.base_addr + idx * NVM_SECTOR_SIZE;
}

static void flash_read_safe(uint32_t addr, uint32_t len, uint8_t *buf)
{
    // 遵循手册建议：单次 <= 64B :contentReference[oaicite:16]{index=16}
    while (len) {
        uint32_t n = (len > NVM_MAX_READ_CHUNK) ? NVM_MAX_READ_CHUNK : len;
        flash_read_page(addr, n, buf);
        addr += n;
        buf  += n;
        len  -= n;
    }
}

// 写入必须：不跨 page，且单次 <= 16B（BLE timing 友好）:contentReference[oaicite:17]{index=17}
static void flash_write_safe(uint32_t addr, const uint8_t *data, uint32_t len)
{
    while (len) {
        // 1) 限制 chunk <= 16
        uint32_t n = (len > NVM_MAX_WRITE_CHUNK) ? NVM_MAX_WRITE_CHUNK : len;

        // 2) 限制不跨 page :contentReference[oaicite:18]{index=18}
        uint32_t page_off = addr & (NVM_PAGE_SIZE - 1u);
        uint32_t page_rem = NVM_PAGE_SIZE - page_off;
        if (n > page_rem) n = page_rem;

        // Telink flash_write_page 的 buf 建议在 RAM
        uint8_t tmp[16];
        memcpy(tmp, data, n);

        flash_write_page(addr, n, tmp);

        addr += n;
        data += n;
        len  -= n;
    }
}

// 判断一个区域是否“全 FF”
static int is_all_ff(const uint8_t *p, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (p[i] != 0xFF) return 0;
    }
    return 1;
}

static int rec_hdr_valid(const nvm_rec_hdr_t *h)
{
    if (h->magic != NVM_MAGIC) return 0;
    if (h->len == 0) return 0;
    if (h->len > 512) return 0; // 防御：BMS 参数一般不会大到离谱
    return 1;
}

static int rec_crc_ok(uint32_t addr, const nvm_rec_hdr_t *h)
{
    // 计算 crc32：hdr 除 crc32 字段 + payload
    uint32_t crc = 0;

    // hdr 前 12 字节（magic,key,len,seq）
    crc = crc32_ieee(h, 12);

    // payload
    uint8_t buf[64];
    uint32_t remain = h->len;
    uint32_t cur = addr + sizeof(nvm_rec_hdr_t);
    while (remain) {
        uint32_t n = (remain > sizeof(buf)) ? sizeof(buf) : remain;
        flash_read_safe(cur, n, buf);
        // 拼接 crc：用同一 crc32 算法连续喂入
        // 这里用“重新算一段再合并”会麻烦，所以直接复用一次性接口：用小缓冲累计
        // 为简单起见：把 payload 分段累计到一个 running crc
        uint32_t tmp_crc = 0xFFFFFFFFu;
        // 把已有 crc 作为“前态”会需要特殊实现；这里偷懒：一次性读到 RAM 再算
        // ——但 payload 可能较大。
        // 折中：限制 len <= 512，上面已防御；这里用静态 512 缓冲一次性读。
        // 更大需求再做 streaming crc。
        (void)tmp_crc;
        // 先 break，走一次性缓存方案
        break;
    }

    // 一次性缓存方案（len <= 512）
    uint8_t payload[512];
    flash_read_safe(addr + sizeof(nvm_rec_hdr_t), h->len, payload);
    uint8_t hdr12[12];
    memcpy(hdr12, h, 12);
    // 拼起来算
    uint8_t mix[12 + 512];
    memcpy(mix, hdr12, 12);
    memcpy(mix + 12, payload, h->len);
    crc = crc32_ieee(mix, 12 + h->len);

    return (crc == h->crc32);
}

static uint32_t rec_total_size(const nvm_rec_hdr_t *h)
{
    uint32_t raw = sizeof(nvm_rec_hdr_t) + h->len;
    return align_up(raw, NVM_ALIGN);
}

static void scan_find_tail_and_seq(void)
{
    // 扫描所有 sector，找到最后一个有效记录位置、最大 seq
    uint8_t hdr_buf[16];
    uint32_t best_seq = 0;
    uint32_t best_tail = sector_addr(0);

    for (uint32_t s = 0; s < g_cfg.sector_count; s++) {
        uint32_t base = sector_addr(s);
        uint32_t cur = base;

        while (cur + 16 <= base + NVM_SECTOR_SIZE) {
            flash_read_safe(cur, 16, hdr_buf);

            if (is_all_ff(hdr_buf, 16)) {
                // 这个位置开始是空的
                break;
            }

            nvm_rec_hdr_t h;
            memcpy(&h, hdr_buf, 16);

            if (!rec_hdr_valid(&h)) {
                // 认为后面都不可信，停止本 sector
                break;
            }

            // CRC 校验（坏记录直接停止：通常是写到一半掉电）
            if (!rec_crc_ok(cur, &h)) {
                break;
            }

            uint32_t total = rec_total_size(&h);
            if (cur + total > base + NVM_SECTOR_SIZE) {
                break;
            }

            if (h.seq > best_seq) best_seq = h.seq;
            cur += total;
        }

        // cur 是本 sector tail
        if (cur > best_tail) {
            best_tail = cur;
            g_active_sector = s;
        }
    }

    g_seq = (best_seq == 0) ? 1 : (best_seq + 1);
    g_write_addr = best_tail;

    // 若 tail 落在 sector 末尾外，修正到下一个 sector 起始
    uint32_t active_base = sector_addr(g_active_sector);
    if (g_write_addr >= active_base + NVM_SECTOR_SIZE) {
        g_active_sector = (g_active_sector + 1) % g_cfg.sector_count;
        g_write_addr = sector_addr(g_active_sector);
    }
}

static nvm_rc_t gc_prepare_next_sector(void)
{
    // 当前 sector 空间不足，需要切换到下一 sector
    uint32_t next = (g_active_sector + 1) % g_cfg.sector_count;
    uint32_t next_addr = sector_addr(next);

    // 如果 next sector 不是空的，需要擦除；但擦除只能在允许时机做 :contentReference[oaicite:19]{index=19}
    uint8_t probe[16];
    flash_read_safe(next_addr, 16, probe);

    if (!is_all_ff(probe, 16)) {
        if (!nvm_platform_can_erase_now()) {
            // 连接态/不安全时机：先挂起回收
            g_gc_pending = 1;
            g_gc_sector_addr = next_addr;
            return NVM_ERR_BUSY;
        }

        if (!nvm_platform_flash_voltage_ok()) {
            return NVM_ERR_VOLTAGE;
        }

        nvm_platform_wd_clear();
        flash_erase_sector(next_addr);
        nvm_platform_wd_clear();
    }

    g_active_sector = next;
    g_write_addr = next_addr;
    return NVM_OK;
}

nvm_rc_t nvm_init(const nvm_cfg_t *cfg)
{
    if (!cfg || cfg->sector_count < 2) return NVM_ERR_PARAM;
    if (cfg->base_addr & (NVM_SECTOR_SIZE - 1u)) return NVM_ERR_PARAM;

    g_cfg = *cfg;
    g_gc_pending = 0;
    g_gc_sector_addr = 0;
    g_active_sector = 0;
    g_write_addr = sector_addr(0);
    g_seq = 1;

    scan_find_tail_and_seq();
    return NVM_OK;
}

nvm_rc_t nvm_get(uint16_t key, void *out, uint16_t max_len, uint16_t *out_len)
{
    if (!out || !out_len || max_len == 0) return NVM_ERR_PARAM;

    uint8_t hdr_buf[16];
    uint32_t found_seq = 0;
    uint32_t found_addr = 0;
    nvm_rec_hdr_t found_hdr;

    // 扫描所有 sector，找 key 对应的最新 seq
    for (uint32_t s = 0; s < g_cfg.sector_count; s++) {
        uint32_t base = sector_addr(s);
        uint32_t cur = base;

        while (cur + 16 <= base + NVM_SECTOR_SIZE) {
            flash_read_safe(cur, 16, hdr_buf);
            if (is_all_ff(hdr_buf, 16)) break;

            nvm_rec_hdr_t h;
            memcpy(&h, hdr_buf, 16);
            if (!rec_hdr_valid(&h)) break;
            if (!rec_crc_ok(cur, &h)) break;

            uint32_t total = rec_total_size(&h);
            if (cur + total > base + NVM_SECTOR_SIZE) break;

            if (h.key == key && h.seq >= found_seq) {
                found_seq = h.seq;
                found_addr = cur;
                found_hdr = h;
            }

            cur += total;
        }
    }

    if (!found_addr) return NVM_ERR_NOT_FOUND;
    if (found_hdr.len > max_len) return NVM_ERR_PARAM;

    flash_read_safe(found_addr + sizeof(nvm_rec_hdr_t), found_hdr.len, (uint8_t*)out);
    *out_len = found_hdr.len;
    return NVM_OK;
}

nvm_rc_t nvm_set(uint16_t key, const void *data, uint16_t len)
{
    if (!data || len == 0) return NVM_ERR_PARAM;

    // 不要求连接态禁写，但写必须拆 <=16B；这里已经保证
    // 但如果你希望“连接态完全不写”，可在这里加策略判断

    // 计算 record 大小
    nvm_rec_hdr_t h;
    h.magic = NVM_MAGIC;
    h.key   = key;
    h.len   = len;
    h.seq   = g_seq;

    // 计算 crc32
    uint8_t mix[12 + 512];
    if (len > 512) return NVM_ERR_PARAM; // 目前实现限制
    memcpy(mix, &h, 12);
    memcpy(mix + 12, data, len);
    h.crc32 = crc32_ieee(mix, 12 + len);

    uint32_t total = align_up(sizeof(nvm_rec_hdr_t) + len, NVM_ALIGN);

    // 当前 sector 剩余空间够不够
    uint32_t base = sector_addr(g_active_sector);
    uint32_t end  = base + NVM_SECTOR_SIZE;
    if (g_write_addr + total > end) {
        nvm_rc_t rc = gc_prepare_next_sector();
        if (rc != NVM_OK) return rc;
        base = sector_addr(g_active_sector);
        end  = base + NVM_SECTOR_SIZE;
        if (g_write_addr + total > end) return NVM_ERR_NO_SPACE;
    }

    if (!nvm_platform_flash_voltage_ok()) {
        return NVM_ERR_VOLTAGE;
    }

    // 写 header（16B）
    flash_write_safe(g_write_addr, (const uint8_t*)&h, sizeof(h));

    // 写 payload（拆 <=16B，且不跨 page）
    flash_write_safe(g_write_addr + sizeof(h), (const uint8_t*)data, len);

    // pad 区域不必写（保持 FF），因为我们用 align_up 定位下一个记录
    g_write_addr += total;
    g_seq++;

    return NVM_OK;
}

void nvm_process(void)
{
    // 延迟擦除：只有允许时机才做
    if (g_gc_pending) {
        if (!nvm_platform_can_erase_now()) return;
        if (!nvm_platform_flash_voltage_ok()) return;

        nvm_platform_wd_clear();
        flash_erase_sector(g_gc_sector_addr);
        nvm_platform_wd_clear();

        g_gc_pending = 0;
        g_gc_sector_addr = 0;
    }
}

void nvm_debug_dump_state(uint32_t *out_write_addr, uint32_t *out_active_sector)
{
    if (out_write_addr) *out_write_addr = g_write_addr;
    if (out_active_sector) *out_active_sector = g_active_sector;
}
