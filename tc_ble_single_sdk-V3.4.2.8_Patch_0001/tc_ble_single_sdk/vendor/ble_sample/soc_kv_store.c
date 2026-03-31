#include "soc_kv_store.h"
#include "runtime_state_store.h"
// #include <string.h>

// ================= 编码：u16[15:14]=type, u16[13:0]=value =================
#define TYPE_SHIFT    14
#define VAL_MASK      0x3FFF
#define VAL_FORBIDDEN 0x3FFF   // 16383 禁止（否则会与擦除态0xFFFF冲突）
#define WORD_ERASED   0xFFFF

#define SOC_SECTOR_MAGIC         0x534F4331u
#define SOC_SECTOR_COMMIT_MAGIC  0x434F4D54u

#define HDR_MAGIC_OFF            0u
#define HDR_GENERATION_OFF       8u
#define HDR_COMMIT_OFF           16u
#define HDR_SIZE_BYTES           24u

#define SNAPSHOT_MASK_ALL        0x07u

typedef struct {
    soc_kv_data_t data;
    u32 next_off;
    u32 generation;
    u8 has_any_valid;
    u8 has_full_snapshot;
    u8 prepared;
    u8 committed;
} sector_scan_t;

static soc_kv_data_t g_cache;       // 当前“最新计算值”
static soc_kv_data_t g_last_logged; // 上一次“写入flash”的值（用于变化>=1判断）
static soc_kv_data_t g_last_checkpoint;
static soc_kv_dbg_t  g_dbg;

static inline u16 pack_word(u16 type, u16 val14)
{
    return (u16)((type << TYPE_SHIFT) | (val14 & VAL_MASK));
}
static inline u16 word_type(u16 w) { return (u16)(w >> TYPE_SHIFT); }
static inline u16 word_val(u16 w)  { return (u16)(w & VAL_MASK); }

static inline u16 clamp_u14_safe(u16 v)
{
    if (v >= VAL_FORBIDDEN) return (VAL_FORBIDDEN - 1); // 16382
    return v;
}

#if SOC_STORE_REDUNDANT
    #define REC_WORDS 2   // word + ~word
#else
    #define REC_WORDS 1   // only word
#endif
#define REC_BYTES   (REC_WORDS * 2)

// ================ Flash IO（Telink） =================
static inline void flash_read_bytes(u32 addr, u8 *buf, u32 len)
{
    flash_read_page(addr, (int)len, buf);
}
static inline void flash_write_bytes(u32 addr, const u8 *buf, u32 len)
{
    flash_write_page(addr, (int)len, (u8*)buf);
}
static inline void flash_erase_sector_safe(u32 base)
{
    // 擦除会阻塞 20~100ms：只在允许卡顿的时机触发（init / 休眠前 / 关机前）
    flash_erase_sector(base);
}

static inline int flash_word32_all_erased(const u32 *buf, u32 word_cnt)
{
    for (u32 i = 0; i < word_cnt; ++i) {
        if (buf[i] != 0xFFFFFFFFu) {
            return 0;
        }
    }
    return 1;
}

static u32 other_sector(u32 base)
{
    return (base == FLASH_ADR_SOC_A) ? FLASH_ADR_SOC_B : FLASH_ADR_SOC_A;
}

static void sector_defaults(soc_kv_data_t *out)
{
    out->soc = 60;
    out->dsg = 0;
    out->cycle = 1;
}

static u16 diff_u16(u16 lhs, u16 rhs)
{
    return (lhs >= rhs) ? (lhs - rhs) : (rhs - lhs);
}

static void sync_runtime_checkpoint_if_needed(u8 force)
{
    if (!force)
    {
        if (g_cache.cycle != g_last_checkpoint.cycle) {
            force = 1;
        }
        else if (diff_u16(g_cache.soc, g_last_checkpoint.soc) >= 10u) {
            force = 1;
        }
        else if (diff_u16(g_cache.dsg, g_last_checkpoint.dsg) >= 10u) {
            force = 1;
        }
    }

    if (!force) {
        return;
    }

    if (runtime_state_store_save_soc_snapshot(g_cache.soc, g_cache.dsg, g_cache.cycle)) {
        g_last_checkpoint = g_cache;
    }
}

static int generation_is_newer(u32 lhs, u32 rhs)
{
    return (int32_t)(lhs - rhs) > 0;
}

static int sector_write_prepare_header(u32 base, u32 generation)
{
    const u32 hdr[4] = {
        SOC_SECTOR_MAGIC, ~SOC_SECTOR_MAGIC,
        generation, ~generation,
    };
    flash_write_bytes(base + HDR_MAGIC_OFF, (const u8 *)hdr, sizeof(hdr));
    return 1;
}

static int sector_write_commit_header(u32 base)
{
    const u32 commit[2] = {
        SOC_SECTOR_COMMIT_MAGIC, ~SOC_SECTOR_COMMIT_MAGIC,
    };
    flash_write_bytes(base + HDR_COMMIT_OFF, (const u8 *)commit, sizeof(commit));
    return 1;
}

static void sector_read_header(u32 base, sector_scan_t *scan)
{
    u32 magic_hdr[2];
    u32 gen_hdr[2];
    u32 commit_hdr[2];

    flash_read_bytes(base + HDR_MAGIC_OFF, (u8 *)magic_hdr, sizeof(magic_hdr));
    flash_read_bytes(base + HDR_GENERATION_OFF, (u8 *)gen_hdr, sizeof(gen_hdr));
    flash_read_bytes(base + HDR_COMMIT_OFF, (u8 *)commit_hdr, sizeof(commit_hdr));

    if (flash_word32_all_erased(magic_hdr, 2) &&
        flash_word32_all_erased(gen_hdr, 2) &&
        flash_word32_all_erased(commit_hdr, 2)) {
        return;
    }

    if (magic_hdr[0] != SOC_SECTOR_MAGIC || magic_hdr[1] != ~SOC_SECTOR_MAGIC) {
        return;
    }
    if (gen_hdr[1] != ~gen_hdr[0]) {
        return;
    }

    scan->generation = gen_hdr[0];
    scan->prepared = 1;

    if (commit_hdr[0] == SOC_SECTOR_COMMIT_MAGIC && commit_hdr[1] == ~SOC_SECTOR_COMMIT_MAGIC) {
        scan->committed = 1;
    }
}

// ================= 记录校验 =================
static int rec_is_erased(const u16 *rw)
{
#if SOC_STORE_REDUNDANT
    return (rw[0] == WORD_ERASED && rw[1] == WORD_ERASED);
#else
    return (rw[0] == WORD_ERASED);
#endif
}

static int rec_is_valid(const u16 *rw)
{
#if SOC_STORE_REDUNDANT
    if ((u16)(~rw[0]) != rw[1]) return 0;
#endif
    u16 w = rw[0];
    if (w == WORD_ERASED) return 0;

    u16 t = word_type(w);
    u16 v = word_val(w);

    if (t > 2) return 0;               // 只允许 0/1/2
    if (v == VAL_FORBIDDEN) return 0;  // 禁止值

    return 1;
}

// ================= 扫描扇区：恢复每类参数最新值 + next_off =================
static void scan_sector(u32 base, sector_scan_t *scan)
{
    memset(scan, 0, sizeof(*scan));
    sector_defaults(&scan->data);
    scan->next_off = HDR_SIZE_BYTES;

    sector_read_header(base, scan);
    if (!scan->prepared) {
        return;
    }

    u8 item_mask = 0;
    for (u32 off = HDR_SIZE_BYTES; off + REC_BYTES <= SOC_SECTOR_SIZE; off += REC_BYTES) {
        u16 rw[REC_WORDS];
        flash_read_bytes(base + off, (u8*)rw, REC_BYTES);

        // 全擦除态：append-only 尾部
        if (rec_is_erased(rw)) {
            scan->next_off = off;
            break;
        }

        // 遇到脏记录：最稳策略是停止（不信任后续）
        if (!rec_is_valid(rw)) {
            scan->next_off = off;
            break;
        }

        u16 w = rw[0];
        u16 t = word_type(w);
        u16 v = word_val(w);

        if (t == SOC_ITEM_SOC) {
            scan->data.soc = v;
            item_mask |= (1u << SOC_ITEM_SOC);
        }
        else if (t == SOC_ITEM_DSG_INT) {
            scan->data.dsg = v;
            item_mask |= (1u << SOC_ITEM_DSG_INT);
        }
        else if (t == SOC_ITEM_CYCLE) {
            scan->data.cycle = v;
            item_mask |= (1u << SOC_ITEM_CYCLE);
        }

        scan->has_any_valid = 1;
        scan->next_off = off + REC_BYTES;
    }

    if (scan->next_off > SOC_SECTOR_SIZE) {
        scan->next_off = SOC_SECTOR_SIZE;
    }
    if ((item_mask & SNAPSHOT_MASK_ALL) == SNAPSHOT_MASK_ALL) {
        scan->has_full_snapshot = 1;
    }
}

// ================= 追加写一条记录（不擦除） =================
static int append_record(u32 base, u32 *io_off, soc_item_t item, u16 value)
{
    u32 off = *io_off;
    if (off + REC_BYTES > SOC_SECTOR_SIZE) return 0;

    u16 w = pack_word((u16)item, clamp_u14_safe(value));

#if SOC_STORE_REDUNDANT
    // 写入 4B：word + ~word（抗掉电撕裂/脏数据）
    u16 rw[2] = { w, (u16)(~w) };
    flash_write_bytes(base + off, (const u8*)rw, 4);
#else
    // 写入 2B：极省空间
    flash_write_bytes(base + off, (const u8*)&w, 2);
#endif

    *io_off = off + REC_BYTES;
    return 1;
}

// ================= rollover：满了切扇区 =================
// 1) 擦新扇区
// 2) 写入 PREPARE 头
// 3) 把当前缓存的 3 个值各写一条（让新扇区一开始就是“完整快照”）
// 4) 写入 COMMIT 标记
// 5) 切换 active
// 6) 擦旧扇区
static int rollover(u32 *io_active_base, u32 *io_write_off, const soc_kv_data_t *cache)
{
    u32 old_base = *io_active_base;
    u32 new_base = other_sector(old_base);
    u32 new_generation = g_dbg.generation + 1;

    flash_erase_sector_safe(new_base);
    if (!sector_write_prepare_header(new_base, new_generation)) return 0;

    u32 off = HDR_SIZE_BYTES;
    if (!append_record(new_base, &off, SOC_ITEM_SOC,   cache->soc))   return 0;
    if (!append_record(new_base, &off, SOC_ITEM_DSG_INT, cache->dsg)) return 0;
    if (!append_record(new_base, &off, SOC_ITEM_CYCLE, cache->cycle)) return 0;
    if (!sector_write_commit_header(new_base)) return 0;

    // 切换
    *io_active_base = new_base;
    *io_write_off   = off;
    g_dbg.generation = new_generation;

    // 新扇区写成功后再擦旧扇区（掉电更安全）
    flash_erase_sector_safe(old_base);

    return 1;
}

int soc_kv_store_init(void)
{
    memset(&g_cache, 0, sizeof(g_cache));
    memset(&g_last_logged, 0, sizeof(g_last_logged));
    memset(&g_dbg, 0, sizeof(g_dbg));

    sector_scan_t a;
    sector_scan_t b;

    scan_sector(FLASH_ADR_SOC_A, &a);
    scan_sector(FLASH_ADR_SOC_B, &b);

    if (a.committed && b.committed) {
        if (generation_is_newer(a.generation, b.generation)) {
            g_cache = a.data;
            g_dbg.active_base = FLASH_ADR_SOC_A;
            g_dbg.write_off = a.next_off;
            g_dbg.generation = a.generation;
        } else {
            g_cache = b.data;
            g_dbg.active_base = FLASH_ADR_SOC_B;
            g_dbg.write_off = b.next_off;
            g_dbg.generation = b.generation;
        }
        g_dbg.loaded = 1;
    } else if (a.committed) {
        g_cache = a.data;
        g_dbg.active_base = FLASH_ADR_SOC_A;
        g_dbg.write_off = a.next_off;
        g_dbg.generation = a.generation;
        g_dbg.loaded = 1;
    } else if (b.committed) {
        g_cache = b.data;
        g_dbg.active_base = FLASH_ADR_SOC_B;
        g_dbg.write_off = b.next_off;
        g_dbg.generation = b.generation;
        g_dbg.loaded = 1;
    } else if (a.prepared && a.has_full_snapshot) {
        g_cache = a.data;
        g_dbg.active_base = FLASH_ADR_SOC_A;
        g_dbg.write_off = a.next_off;
        g_dbg.generation = a.generation;
        g_dbg.loaded = 1;
    } else if (b.prepared && b.has_full_snapshot) {
        g_cache = b.data;
        g_dbg.active_base = FLASH_ADR_SOC_B;
        g_dbg.write_off = b.next_off;
        g_dbg.generation = b.generation;
        g_dbg.loaded = 1;
    } else {
        flash_runtime_state_t runtime_state;

        if (runtime_state_store_load(&runtime_state)) {
            g_cache.soc = clamp_u14_safe(runtime_state.soc);
            g_cache.dsg = clamp_u14_safe(runtime_state.dsg_int);
            g_cache.cycle = clamp_u14_safe(runtime_state.cycle);
            if (g_cache.cycle == 0) {
                g_cache.cycle = 1;
            }
        } else {
            // 没有有效记录：默认值
            sector_defaults(&g_cache);
        }

        g_dbg.active_base = FLASH_ADR_SOC_A;
        g_dbg.write_off = HDR_SIZE_BYTES;
        g_dbg.generation = 0;
        g_dbg.loaded = 0;
    }

    // 上一次已写入值 = 启动恢复值（避免启动后立刻重复写）
    g_last_logged = g_cache;
    g_last_checkpoint = g_cache;

    // 满了就 init 阶段 rollover（通常允许擦）
    if (g_dbg.write_off + REC_BYTES > SOC_SECTOR_SIZE) {
        if (!rollover(&g_dbg.active_base, &g_dbg.write_off, &g_cache)) return 0;
    }

    return 1;
}

soc_kv_data_t soc_kv_store_get(void)
{
    return g_cache;
}

int soc_kv_store_put(soc_item_t item, u16 value)
{
    // 更新缓存（语义：缓存永远代表最新值）
    value = clamp_u14_safe(value);
    if (item == SOC_ITEM_SOC) g_cache.soc = value;
    else if (item == SOC_ITEM_DSG_INT) g_cache.dsg = value;
    else if (item == SOC_ITEM_CYCLE) g_cache.cycle = value;
    else return 0;

    // 空间不足：rollover（会擦除，建议只在“允许卡顿窗口”频繁触发写入）
    if (g_dbg.write_off + REC_BYTES > SOC_SECTOR_SIZE) {
        if (!rollover(&g_dbg.active_base, &g_dbg.write_off, &g_cache)) return 0;
        // rollover 已写入三条快照记录，不再额外写本次 item（省写入）
        return 1;
    }

    if (!g_dbg.loaded && g_dbg.write_off == HDR_SIZE_BYTES) {
        flash_erase_sector_safe(g_dbg.active_base);
        if (!sector_write_prepare_header(g_dbg.active_base, 1)) return 0;
        if (!sector_write_commit_header(g_dbg.active_base)) return 0;
        g_dbg.loaded = 1;
        g_dbg.generation = 1;
    }

    return append_record(g_dbg.active_base, &g_dbg.write_off, item, value);
}

void soc_kv_store_update_and_log_if_changed(u16 soc, u16 dsg, u16 cycle)
{
    soc   = clamp_u14_safe(soc);
    dsg   = clamp_u14_safe(dsg);
    cycle = clamp_u14_safe(cycle);

    // 更新缓存
    g_cache.soc = soc;
    g_cache.dsg = dsg;
    g_cache.cycle = cycle;

    // “没变化1就记录一次”：变化即写一条
    // 这里按“新值 != 上次写入值”作为触发（等价于变化至少1）
    if (soc != g_last_logged.soc) {
        if (soc_kv_store_put(SOC_ITEM_SOC, soc)) {
            g_last_logged.soc = soc;
        }
    }
    if (dsg != g_last_logged.dsg) {
        if (soc_kv_store_put(SOC_ITEM_DSG_INT, dsg)) {
            g_last_logged.dsg = dsg;
        }
    }
    if (cycle != g_last_logged.cycle) {
        if (soc_kv_store_put(SOC_ITEM_CYCLE, cycle)) {
            g_last_logged.cycle = cycle;
        }
    }

    sync_runtime_checkpoint_if_needed(0);
}

void soc_kv_store_factory_reset(void)
{
    flash_erase_sector_safe(FLASH_ADR_SOC_A);
    flash_erase_sector_safe(FLASH_ADR_SOC_B);

    g_cache.soc = 0;
    g_cache.dsg = 0;
    g_cache.cycle = 0;
    g_last_logged = g_cache;
    g_last_checkpoint = g_cache;

    g_dbg.active_base = FLASH_ADR_SOC_A;
    g_dbg.write_off = HDR_SIZE_BYTES;
    g_dbg.generation = 0;
    g_dbg.loaded = 0;
}

soc_kv_dbg_t soc_kv_store_get_dbg(void)
{
    return g_dbg;
}
