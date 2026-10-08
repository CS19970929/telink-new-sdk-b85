/*
 * 文件功能：启动/存储/采样/SOC/MOS 运行诊断快照与 Trace；主循环更新，
 * 只读窗口供上位机核对状态。
 * bms/core/bms_diag.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_debug_log.h"
#include "bms_diag.h"
#include <string.h>

/* 诊断窗口 RAM 快照；只有主循环生产者更新，读协议不能触发 AFE/Flash 动作。 */
static uint16_t s_words[256];
static uint16_t s_sleep_words[BMS_DIAG_SLEEP_WORDS];
#if BMS_DIAG_TRACE_ENABLE
static uint16_t s_trace[BMS_DIAG_TRACE_COUNT][BMS_DIAG_TRACE_WORDS];
static uint32_t s_trace_sequence;
static uint16_t s_next;
#endif
static uint32_t s_sequence;
static uint8_t s_frozen;

/* 将 32 位值拆为低字在前的两个 16 位诊断字。 */
static void put32(uint16_t *p, uint32_t value)
{
    p[0] = (uint16_t)value; p[1] = (uint16_t)(value >> 16);
}
/* 从低字在前的两个诊断字恢复 32 位值。 */
static uint32_t get32(const uint16_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 16);
}
/* 递增诊断快照序号，标记内容发生变化。 */
static void changed(void) { ++s_sequence; put32(&s_words[4], s_sequence); }
/* 更新 16 位诊断字段并反映变化状态。 */
static uint8_t update16(uint16_t offset, uint16_t value)
{
    if (s_words[offset] == value) return 0u;
    s_words[offset] = value;
    return 1u;
}
/* 更新 32 位诊断字段并反映变化状态。 */
static uint8_t update32(uint16_t offset, uint32_t value)
{
    if (get32(&s_words[offset]) == value) return 0u;
    put32(&s_words[offset], value);
    return 1u;
}

/* 追加一条带时间与参数的诊断轨迹。 */
void bms_diag_trace(uint16_t event, uint32_t arg0, uint32_t arg1)
{
    BMS_LOG(event == DIAG_EV_STORAGE ? BMS_LOG_WARN : BMS_LOG_INFO,
            (event == DIAG_EV_STORAGE || event == DIAG_EV_INIT) ? BMS_LOG_STORAGE :
            (event == DIAG_EV_MOS || event == DIAG_EV_PROTECTION) ? BMS_LOG_PROTECT :
            (event == DIAG_EV_AFE || event == DIAG_EV_SAMPLE_STATE || event == DIAG_EV_CURRENT_RECOVERY) ? BMS_LOG_AFE :
            event == DIAG_EV_PM_STATE ? BMS_LOG_POWER : BMS_LOG_SYSTEM, event, arg0, arg1);
#if BMS_DIAG_TRACE_ENABLE
    uint16_t *p = s_trace[s_next];
    put32(p, ++s_trace_sequence); put32(p + 2, bms_diag_tick());
    p[4] = event; p[5] = 0u; put32(p + 6, arg0); put32(p + 8, arg1);
    p[10] = 0u; p[11] = 0u;
    s_next = (uint16_t)((s_next + 1u) % BMS_DIAG_TRACE_COUNT);
    if (s_words[12] < BMS_DIAG_TRACE_COUNT) ++s_words[12];
    else if (get32(&s_words[10]) != 0xFFFFFFFFu)
        put32(&s_words[10], get32(&s_words[10]) + 1u);
    put32(&s_words[8], s_trace_sequence);
#else
    (void)event; (void)arg0; (void)arg1;
#endif
    changed();
}
/* 初始化诊断窗口和启动快照。 */
void bms_diag_init(void)
{
    bms_debug_log_init();
    memset(s_words, 0, sizeof(s_words));
    memset(s_sleep_words, 0, sizeof(s_sleep_words));
    s_sleep_words[0] = 0x534Cu;
    s_sleep_words[1] = 1u;
#if BMS_DIAG_TRACE_ENABLE
    memset(s_trace, 0, sizeof(s_trace));
    s_trace_sequence = 0u; s_next = 0u;
#endif
    s_sequence = 0u; s_frozen = 0u;
    /* 已删除模式的 wire slot 225 保持初始化零，无运行写入口。 */
    s_words[0] = 0x4447u; s_words[1] = 1u; s_words[2] = BMS_DIAG_CAPABILITIES;
    s_words[BMS_DIAG_RUNTIME_OFFSET] = BMS_DIAG_RUNTIME_VERSION;
    put32(&s_words[22], BMS_DIAG_BUILD_ID);
    s_words[14] = 0x1124u; s_words[15] = 0x8251u;
    bms_diag_trace(DIAG_EV_BOOT, 0u, 0u);
}
/* 更新尚未冻结的启动诊断 16 位字段。 */
void bms_diag_boot_word(uint16_t offset, uint16_t value)
{
    if (!s_frozen && offset >= 13u && offset < 128u) { s_words[offset] = value; changed(); }
}
/* 更新尚未冻结的启动诊断 32 位字段。 */
void bms_diag_boot_u32(uint16_t offset, uint32_t value)
{
    if (!s_frozen && offset >= 13u && offset < 127u) { put32(&s_words[offset], value); changed(); }
}
/* 记录持久参数升级与迁移诊断。 */
void bms_diag_upgrade(uint16_t stage, uint16_t invalid_mask)
{
    if (s_frozen) return;
    s_words[27] = stage; s_words[28] = invalid_mask;
    bms_diag_trace(DIAG_EV_UPGRADE, stage, invalid_mask);
}
/* 冻结启动快照，防止运行阶段覆盖启动证据。 */
void bms_diag_freeze_boot(void)
{
    if (s_frozen) return;
    s_frozen = 1u; s_words[3] = 1u;
    bms_diag_trace(DIAG_EV_BOOT_DONE, s_words[24], s_words[25]);
}
/* 记录存储或配置事务开始尝试。 */
void bms_diag_attempt(uint8_t domain)
{
    uint16_t offset;
    if (domain >= 4u) return;
    offset = (uint16_t)(32u + 16u * domain);
    if (!s_frozen) {
        if (s_words[offset + 4u] != 0xFFFFu) ++s_words[offset + 4u];
        s_words[offset + 6u] = DIAG_STARTED;
    }
    bms_diag_trace(DIAG_EV_INIT, domain, DIAG_STARTED);
}
/* 记录事务最终结果及诊断计数。 */
void bms_diag_result(uint8_t domain, uint16_t result)
{
    uint16_t offset;
    if (domain >= 4u) return;
    offset = (uint16_t)(32u + 16u * domain);
    if (!s_frozen) {
        s_words[offset + 6u] = result;
        if (result == DIAG_DEFAULTS) s_words[offset + 7u] = 1u;
        else if (result != DIAG_OK && s_words[offset + 5u] == 0u) {
            s_words[offset + 5u] = result;
            put32(&s_words[offset + 8u], bms_diag_tick());
        }
    }
    s_words[180u + domain] = result;
    bms_diag_trace(DIAG_EV_INIT, domain, result);
}
/* 记录存储错误和对应操作信息。 */
void bms_diag_storage_error(uint16_t reason, uint32_t address)
{
    /* 首个错误独立保留，不受环形覆盖和启动冻结影响。 */
    if (s_words[176] == 0u) { s_words[176] = reason; put32(&s_words[184], address); }
    s_words[177] = reason; put32(&s_words[178], address);
    bms_diag_trace(DIAG_EV_STORAGE, reason, address);
}
/* 更新当前参数来源和提交状态诊断。 */
void bms_diag_params(uint8_t valid, uint8_t upgrade)
{
    uint16_t bits = (uint16_t)((valid ? 1u : 0u) | (upgrade ? 2u : 0u));
    if (!s_frozen) s_words[24] = bits;
    if (s_words[144] != bits) { s_words[144] = bits; bms_diag_trace(DIAG_EV_PARAMS, bits, 0u); }
}
/* 记录软件 MOS 请求与充放电阻断原因；命令、驱动缓存、物理 Gate 是不同证据。 */
void bms_diag_mos(uint16_t requested, uint32_t charge, uint32_t discharge)
{
    if (s_words[128] == requested && get32(&s_words[136]) == charge &&
        get32(&s_words[138]) == discharge) return;
    s_words[128] = requested;
    s_words[129] = (uint16_t)((charge == 0u ? requested & 1u : 0u) |
                             (discharge == 0u ? requested & 2u : 0u));
    put32(&s_words[136], charge); put32(&s_words[138], discharge);
    bms_diag_trace(DIAG_EV_MOS, charge | ((uint32_t)requested << 16), discharge);
}
/* 记录外部控制命令及执行结果。 */
void bms_diag_command(uint8_t command, uint8_t valid)
{
    if (s_words[130] != command || s_words[131] != valid) {
        s_words[130] = command; s_words[131] = valid;
        bms_diag_trace(DIAG_EV_AFE, command, valid);
    }
}
/* 发布驱动 FET 缓存标志与有效性，不作为物理 Gate 证据。 */
void bms_diag_driver(uint8_t flags, uint8_t valid)
{
    if (!valid && !s_words[133]) return;
    if ((s_words[132] & 3u) != (flags & 3u) || s_words[133] != valid)
        bms_diag_trace(DIAG_EV_DRIVER, flags & 3u, valid);
    s_words[132] = flags; s_words[133] = valid;
    put32(&s_words[140], bms_diag_tick()); changed();
}
/* 更新指定诊断计数项。 */
void bms_diag_counter(uint16_t index, uint32_t value)
{
    if (index < 6u && get32(&s_words[160u + 2u * index]) != value) {
        put32(&s_words[160u + 2u * index], value); changed();
    }
}

/* 发布运行采样资格、测量与错误快照。 */
void bms_diag_runtime_sample(uint8_t valid, int32_t raw_current_ma,
                             int32_t current_ma, uint32_t sample_tick_32k,
                             uint8_t current_recovery_pending)
{
    BMS_LOG(BMS_LOG_DEBUG, BMS_LOG_AFE, BMS_LOG_SAMPLE, valid, current_ma);
    uint16_t old_flags = s_words[193];
    uint16_t flags = (uint16_t)((valid ? 1u : 0u) |
                                (current_recovery_pending ? 2u : 0u));
    uint8_t dirty = 0u;
    dirty |= update16(193u, flags);
    dirty |= update32(194u, (uint32_t)raw_current_ma);
    dirty |= update32(196u, (uint32_t)current_ma);
    dirty |= update32(198u, sample_tick_32k);
    if ((old_flags & 1u) != (flags & 1u))
        bms_diag_trace(DIAG_EV_SAMPLE_STATE, flags & 1u, (uint32_t)current_ma);
    else if (dirty)
        changed();
}

/* 发布 SOC 运行快照；恒零的旧学习槽只存在于 wire 缓存。 */
void bms_diag_runtime_soc(const bms_soc_diag_t *soc)
{
    uint16_t flags;
    uint16_t eta;
    uint8_t dirty = 0u;
    if (soc == 0) return;
    if (s_words[202] != soc->soc_estimate || s_words[203] != soc->soc_display || s_words[204] != soc->ocv_state)
        BMS_LOG(BMS_LOG_INFO, BMS_LOG_SOC, BMS_LOG_SOC_STATE,
                ((uint32_t)soc->soc_estimate << 16) | soc->soc_display, soc->ocv_state);
    dirty |= update16(200u, soc->current_deadband_ma);
    dirty |= update16(202u, soc->soc_estimate);
    dirty |= update16(203u, soc->soc_display);
    dirty |= update16(204u, soc->ocv_state);
    dirty |= update16(205u, soc->ocv_center);
    dirty |= update16(206u, soc->ocv_low);
    dirty |= update16(207u, soc->ocv_high);
    dirty |= update16(208u, soc->ocv_confidence);
    dirty |= update16(209u, soc->rest_seconds);
    dirty |= update16(210u, 0u);
    dirty |= update16(211u, 0u);
    dirty |= update16(212u, 0u);

    BMS_LOG(BMS_LOG_DEBUG, BMS_LOG_SOC, BMS_LOG_SOC_DECISION,
            ((uint32_t)soc->last_soc_action << 24) | ((uint32_t)soc->last_soc_before << 16) |
            ((uint32_t)soc->last_soc_after << 8) | soc->last_soc_target,
            ((uint32_t)soc->last_decision_detail << 16) | soc->rest_seconds);
    BMS_LOG(BMS_LOG_DEBUG, BMS_LOG_SOC, BMS_LOG_SOC_TIME,
            soc->last_sample_elapsed_32k, soc->last_integral_delta_as10);
    flags = (uint16_t)((soc->eta_valid ? 4u : 0u) |
                       ((soc->endpoint_event_flags & 0x00FFu) << 8));
    eta = (uint16_t)((soc->eta_state & 0x000Fu) |
                     ((soc->eta_direction & 0x000Fu) << 4) |
                     ((soc->eta_confidence & 0x00FFu) << 8));
    dirty |= update16(226u, soc->chemistry);
    dirty |= update16(227u, soc->profile_id);
    dirty |= update16(228u, soc->profile_version);
    dirty |= update16(229u, soc->endpoint_state);
    dirty |= update16(230u, flags);
    dirty |= update16(231u, soc->nominal_capacity_0p1ah);
    dirty |= update16(232u, soc->effective_capacity_0p1ah);
    dirty |= update16(233u, soc->remaining_capacity_0p1ah);
    dirty |= update32(234u, (uint32_t)soc->filtered_current_ma);
    dirty |= update16(236u, soc->current_variation_ma);
    dirty |= update16(237u, soc->time_to_empty_min);
    dirty |= update16(238u, soc->time_to_full_min);
    dirty |= update16(239u, eta);
    dirty |= update16(240u, soc->soh);
    dirty |= update16(241u, soc->soh_source);
    dirty |= update16(242u, soc->soh_confidence);
    dirty |= update16(243u, 0u);
    dirty |= update16(244u, 0u);
    dirty |= update16(245u, 0u);
    dirty |= update16(246u, 0u);
    dirty |= update16(247u, 0u);
    dirty |= update16(248u, soc->ocv_cell_mv);
    dirty |= update16(249u, (uint16_t)((soc->last_sample_state & 0x0Fu) |
                                       ((soc->last_integral_direction & 0x0Fu) << 4) |
                                       ((uint16_t)soc->last_soc_action << 8)));
    dirty |= update32(250u, soc->last_sample_elapsed_32k);
    dirty |= update32(252u, soc->last_integral_delta_as10);
    dirty |= update16(254u, (uint16_t)(soc->last_soc_before |
                                       ((uint16_t)soc->last_soc_after << 8)));
    dirty |= update16(255u, (uint16_t)(soc->last_soc_target |
                                       ((uint16_t)soc->last_decision_detail << 8)));
    if (dirty) changed();
}

/*
 * 记录 suspend 阻断原因变化；正常 sample_pending 节拍保留在快照，
 * 不让其淹没状态日志。
 */
void bms_diag_runtime_pm(uint8_t suspend_allowed, uint32_t block_mask,
                         uint8_t low_voltage_region, uint32_t low_voltage_seconds,
                         uint8_t ble_connected, uint8_t sample_pending,
                         uint16_t suspend_current_threshold_ma)
{
    uint32_t previous_mask = get32(&s_words[213]);
    uint32_t previous_trace_mask = previous_mask & (uint32_t)~DIAG_PM_BLOCK_SAMPLE_PENDING;
    uint32_t trace_mask = block_mask & (uint32_t)~DIAG_PM_BLOCK_SAMPLE_PENDING;
    uint8_t dirty = 0u;
    dirty |= update32(213u, block_mask);
    dirty |= update16(215u, suspend_allowed ? 1u : 0u);
    dirty |= update16(216u, low_voltage_region);
    dirty |= update32(217u, low_voltage_seconds);
    dirty |= update16(219u, ble_connected ? 1u : 0u);
    dirty |= update16(220u, sample_pending ? 1u : 0u);
    dirty |= update16(221u, suspend_current_threshold_ma);
    /*
     * sample_pending 是正常的 200 ms 调度边沿；实时快照保留它，
     * 但不能让它反复占满 64 条轨迹。
     */
    if (previous_trace_mask != trace_mask)
        bms_diag_trace(DIAG_EV_PM_STATE,
            (uint32_t)(trace_mask == 0u ? 1u : 0u) |
            ((uint32_t)low_voltage_region << 8) |
            ((uint32_t)(ble_connected ? 1u : 0u) << 16),
            trace_mask);
    else if (dirty)
        changed();
}

/* 发布各级保护故障位快照。 */
void bms_diag_runtime_faults(uint16_t level1, uint16_t level2, uint16_t level3)
{
    if (s_words[222] == level1 && s_words[223] == level2 && s_words[224] == level3)
        return;
    s_words[222] = level1;
    s_words[223] = level2;
    s_words[224] = level3;
    bms_diag_trace(DIAG_EV_PROTECTION,
                   (uint32_t)level1 | ((uint32_t)level2 << 16),
                   level3);
}

/* 发布电源策略快照；毫秒计数仅复制，不产生轨迹、I/O 或 Flash 写入。 */
void bms_diag_sleep(uint8_t reason, uint32_t block_mask, uint32_t elapsed_ms,
                    uint32_t delay_ms, uint32_t retry_ms, uint8_t suspend_allowed)
{
    uint32_t remaining_ms = elapsed_ms < delay_ms ? delay_ms - elapsed_ms : 0u;
    s_sleep_words[2] = reason == DIAG_SLEEP_REASON_NONE ? DIAG_SLEEP_NONE :
        (block_mask ? DIAG_SLEEP_BLOCKED : (retry_ms ? DIAG_SLEEP_RETRY :
        (remaining_ms ? DIAG_SLEEP_COUNTING : DIAG_SLEEP_READY)));
    s_sleep_words[3] = reason;
    s_sleep_words[4] = suspend_allowed ? 1u : 0u;
    put32(&s_sleep_words[6], block_mask);
    put32(&s_sleep_words[8], elapsed_ms);
    put32(&s_sleep_words[10], delay_ms);
    put32(&s_sleep_words[12], remaining_ms);
    put32(&s_sleep_words[14], retry_ms);
    put32(&s_sleep_words[16], bms_diag_tick());
}

void bms_diag_sleep_committed(void)
{
    s_sleep_words[2] = DIAG_SLEEP_COMMITTED;
    put32(&s_sleep_words[8], get32(&s_sleep_words[10]));
    put32(&s_sleep_words[12], 0u);
    put32(&s_sleep_words[6], 0u);
    put32(&s_sleep_words[14], 0u);
    put32(&s_sleep_words[16], bms_diag_tick());
}

/* 判断请求寄存器范围是否与诊断窗口重叠。 */
int bms_diag_overlaps(uint16_t start, uint16_t count)
{
    uint32_t end = (uint32_t)start + count;
    return count != 0u && ((start < BMS_DIAG_END && end > BMS_DIAG_BASE) ||
        (start < BMS_DIAG_SLEEP_END && end > BMS_DIAG_SLEEP_BASE));
}
/* 从 RAM 诊断快照读取指定寄存器范围。 */
int bms_diag_read(uint16_t start, uint16_t count, uint8_t *bytes)
{
    uint16_t i;
    uint32_t end = (uint32_t)start + count;
    uint32_t tick = bms_diag_tick();
    if (bytes && count && count <= BMS_DIAG_SLEEP_WORDS &&
        start >= BMS_DIAG_SLEEP_BASE && end <= BMS_DIAG_SLEEP_END) {
        put32(&s_sleep_words[18], tick);
        for (i = 0u; i < count; ++i) {
            uint16_t word = s_sleep_words[start - BMS_DIAG_SLEEP_BASE + i];
            bytes[2u*i] = (uint8_t)(word >> 8);
            bytes[2u*i+1u] = (uint8_t)word;
        }
        return 1;
    }
    if (!bytes || !count || count > 125u || start < BMS_DIAG_BASE ||
        end > BMS_DIAG_END || (start < BMS_DIAG_TRACE_BASE && end > BMS_DIAG_TRACE_BASE)) return 0;
    /*
     * 调用者和生产者均在主循环，无需屏蔽中断或再复制 2 KB；
     * 显式编码避免 ABI 打包依赖。
     */
    for (i = 0u; i < count; ++i) {
        uint16_t word, offset = (uint16_t)(start + i - BMS_DIAG_BASE);
        if (offset == 6u) word = (uint16_t)tick;
        else if (offset == 7u) word = (uint16_t)(tick >> 16);
        else if (offset < 256u) word = s_words[offset];
        else {
#if BMS_DIAG_TRACE_ENABLE
            offset = (uint16_t)(offset - 256u);
            word = s_trace[offset / BMS_DIAG_TRACE_WORDS][offset % BMS_DIAG_TRACE_WORDS];
#else
            word = 0u;
#endif
        }
        bytes[2u*i] = (uint8_t)(word >> 8); bytes[2u*i+1u] = (uint8_t)word;
    }
    return 1;
}

/* 取得单个缓存诊断字，不访问硬件。 */
uint16_t bms_diag_cached_word(uint16_t offset)
{
    return offset < 256u ? s_words[offset] : 0u;
}
/* 更新所选 AFE 后端的诊断字段。 */
void bms_diag_backend(uint16_t charge, uint16_t discharge)
{
    if (s_words[146] != charge || s_words[147] != discharge) {
        s_words[146] = charge; s_words[147] = discharge; changed();
    }
}

/* 记录产品编译功能标志。 */
void bms_diag_set_build_flags(uint16_t flags) { bms_diag_boot_word(13u, flags); }
/* 记录启动门禁最终结果。 */
void bms_diag_set_boot_result(uint16_t afe, uint16_t params) { bms_diag_boot_word(24u, afe); bms_diag_boot_word(25u, params); }

/* 发布后端配置、控制与恢复的详细诊断。 */
void bms_diag_backend_details(const uint16_t *words)
{
    uint16_t i;
    uint8_t dirty = 0u;
    if (!words) return;
    if (s_words[142] != words[0] || s_words[143] != words[1] ||
        s_words[146] != words[4] || s_words[147] != words[5])
        BMS_LOG(BMS_LOG_INFO, BMS_LOG_AFE, BMS_LOG_AFE_STATE,
                ((uint32_t)words[5] << 16) | words[4], ((uint32_t)words[0] << 16) | words[1]);
    for (i=0u; i<11u; ++i) dirty |= update16((uint16_t)(142u+i), words[i]);
    if (dirty) changed();
}
