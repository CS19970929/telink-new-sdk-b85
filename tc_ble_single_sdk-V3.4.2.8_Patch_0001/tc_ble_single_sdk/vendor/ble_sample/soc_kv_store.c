#include "soc_kv_store.h"
// #include <string.h>

/*
todo 
1、增加更多接口？？？
2、冗余安全处理？？？

*/

// ================= 编码：u16[15:14]=type, u16[13:0]=value =================
#define TYPE_SHIFT    14
#define VAL_MASK      0x3FFF
#define VAL_FORBIDDEN 0x3FFF   // 16383 禁止（否则会与擦除态0xFFFF冲突）
#define WORD_ERASED   0xFFFF

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

static u32 other_sector(u32 base)
{
    return (base == FLASH_ADR_SOC_A) ? FLASH_ADR_SOC_B : FLASH_ADR_SOC_A;
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
static void scan_sector(u32 base, soc_kv_data_t *out, u32 *out_next_off, u8 *out_has_any_valid)
{
    // 默认值（按你习惯改）
    out->soc = 50;
    out->dsg = 0;
    out->cycle = 1;

    u32 next_off = 0;
    u8 has_any = 0;

    for (u32 off = 0; off + REC_BYTES <= SOC_SECTOR_SIZE; off += REC_BYTES) {
        u16 rw[REC_WORDS];
        flash_read_bytes(base + off, (u8*)rw, REC_BYTES);

        // 全擦除态：append-only 尾部
        if (rec_is_erased(rw)) {
            next_off = off;
            break;
        }

        // 遇到脏记录：最稳策略是停止（不信任后续）
        if (!rec_is_valid(rw)) {
            next_off = off;
            break;
        }

        u16 w = rw[0];
        u16 t = word_type(w);
        u16 v = word_val(w);

        if (t == SOC_ITEM_SOC) out->soc = v;
        else if (t == SOC_ITEM_SOH) out->dsg = v;
        else if (t == SOC_ITEM_CYCLE) out->cycle = v;

        has_any = 1;
        next_off = off + REC_BYTES;
    }

    if (next_off > SOC_SECTOR_SIZE) next_off = SOC_SECTOR_SIZE;
    *out_next_off = next_off;
    *out_has_any_valid = has_any;
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
// 2) 把当前缓存的 3 个值各写一条（让新扇区一开始就是“完整快照”）
// 3) 切换 active
// 4) 擦旧扇区
static int rollover(u32 *io_active_base, u32 *io_write_off, const soc_kv_data_t *cache)
{
    u32 old_base = *io_active_base;
    u32 new_base = other_sector(old_base);

    flash_erase_sector_safe(new_base);

    u32 off = 0;
    if (!append_record(new_base, &off, SOC_ITEM_SOC,   cache->soc))   return 0;
    if (!append_record(new_base, &off, SOC_ITEM_SOH,   cache->dsg))   return 0;
    if (!append_record(new_base, &off, SOC_ITEM_CYCLE, cache->cycle)) return 0;

    // 切换
    *io_active_base = new_base;
    *io_write_off   = off;

    // 新扇区写成功后再擦旧扇区（掉电更安全）
    flash_erase_sector_safe(old_base);

    return 1;
}

// ================= 模块状态 =================
static soc_kv_data_t g_cache;       // 当前“最新计算值”
static soc_kv_data_t g_last_logged; // 上一次“写入flash”的值（用于变化>=1判断）
static soc_kv_dbg_t  g_dbg;

int soc_kv_store_init(void)
{
    memset(&g_cache, 0, sizeof(g_cache));
    memset(&g_last_logged, 0, sizeof(g_last_logged));
    memset(&g_dbg, 0, sizeof(g_dbg));

    soc_kv_data_t a, b;
    u32 next_a, next_b;
    u8 has_a, has_b;

    //todo 逻辑是否有问题?上电初始化
    scan_sector(FLASH_ADR_SOC_A, &a, &next_a, &has_a);
    scan_sector(FLASH_ADR_SOC_B, &b, &next_b, &has_b);

    // 选择更“新”的扇区：优先有有效记录；两边都有时用 next_off 更大者（通常更新更多）
    if (has_a && (!has_b || next_a >= next_b)) {
        g_cache = a;
        g_dbg.active_base = FLASH_ADR_SOC_A;
        g_dbg.write_off = next_a;
        g_dbg.loaded = 1;
    } else if (has_b) {
        g_cache = b;
        g_dbg.active_base = FLASH_ADR_SOC_B;
        g_dbg.write_off = next_b;
        g_dbg.loaded = 1;
    } else {
        // 没有有效记录：默认值
        g_cache.soc = 50;
        g_cache.dsg = 0;
        g_cache.cycle = 1;

        g_dbg.active_base = FLASH_ADR_SOC_A;
        g_dbg.write_off = 0;
        g_dbg.loaded = 0;
    }

    // 上一次已写入值 = 启动恢复值（避免启动后立刻重复写）
    g_last_logged = g_cache;

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
    else if (item == SOC_ITEM_SOH) g_cache.dsg = value;
    else if (item == SOC_ITEM_CYCLE) g_cache.cycle = value;
    else return 0;

    // 空间不足：rollover（会擦除，建议只在“允许卡顿窗口”频繁触发写入）
    if (g_dbg.write_off + REC_BYTES > SOC_SECTOR_SIZE) {
        if (!rollover(&g_dbg.active_base, &g_dbg.write_off, &g_cache)) return 0;
        // rollover 已写入三条快照记录，不再额外写本次 item（省写入）
        return 1;
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
        if (soc_kv_store_put(SOC_ITEM_SOH, dsg)) {
            g_last_logged.dsg = dsg;
        }
    }
    if (cycle != g_last_logged.cycle) {
        if (soc_kv_store_put(SOC_ITEM_CYCLE, cycle)) {
            g_last_logged.cycle = cycle;
        }
    }
}

void soc_kv_store_factory_reset(void)
{
    flash_erase_sector_safe(FLASH_ADR_SOC_A);
    flash_erase_sector_safe(FLASH_ADR_SOC_B);

    g_cache.soc = 0;
    g_cache.dsg = 0;
    g_cache.cycle = 0;
    g_last_logged = g_cache;

    g_dbg.active_base = FLASH_ADR_SOC_A;
    g_dbg.write_off = 0;
    g_dbg.loaded = 0;
}

soc_kv_dbg_t soc_kv_store_get_dbg(void)
{
    return g_dbg;
}



