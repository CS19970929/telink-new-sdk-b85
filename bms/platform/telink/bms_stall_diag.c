/* D014 开发取证：固定 RAM 快照，不参与业务决策、不主动通信或写 Flash。 */
#include "bms_product.h"
#include "bms_stall_diag.h"
#if BMS_STALL_DIAG_ENABLE
#include "tl_common.h"
#include "drivers.h"
#include "bms_diag.h"
#include "bms_timing.h"
#include "bms_sleep_config.h"

#if BMS_PRODUCT_ID != 14u || MCU_CORE_TYPE != MCU_CORE_825x
#error "Stall diagnostic PM/retained-register ownership is reviewed only for D014/B85"
#endif
#if !PM_TIM_RECOVER_MODE
#error "Stall diagnostic requires the reviewed B85 PM_TIM_RECOVER_MODE"
#endif
#if defined(PM_DEEPSLEEP_RETENTION_ENABLE) && PM_DEEPSLEEP_RETENTION_ENABLE
#error "Stall diagnostic boot evidence requires DeepSleep retention disabled"
#endif

#define STALL_DEEP_TAG 0xD0u
#define STALL_DEEP_UNATTRIBUTED 14u
#define STALL_DEEP_BOOT_HOLD 15u

enum {
    W_PHASE, W_PHASE_TICK, W_LOOPS, W_SDK_RETURNS, W_SAMPLES,
    W_SAMPLE_BEGIN, W_SAMPLE_END, W_SDK_ENTER, W_SDK_RETURN, W_APP_WAKES,
    W_PM_ENTERS, W_PM_RETURNS, W_PM_ENTER, W_PM_RETURN, W_PM_MODE,
    W_PM_SOURCE, W_PM_DEADLINE, W_PM_RESULT, W_RAW_32K, W_SAMPLE_DUE,
    W_SAMPLE_DEADLINE, W_IRQ_ENTERS, W_IRQ_EXITS, W_IRQ_SOURCE,
    W_TIMER_REGS, W_RECOVER_FLAG, W_AFE_FAILURES, W_AFE_LAST,
    W_GUARD_BITS, W_GUARD_ELAPSED, W_GUARD_WAIT, W_DETAIL,
    STALL_SNAPSHOT_U32
};
static uint32_t s_live[STALL_SNAPSHOT_U32], s_first_gap[STALL_SNAPSHOT_U32];
static uint32_t s_first_afe[8];
static uint32_t s_afe_flags, s_gap_ms;
enum {
    D_VETO_COUNT, D_VETO_REMAINING, D_PM_RAW_BEFORE, D_PM_RAW_AFTER,
    D_PM_TIMER_BEFORE, D_PM_RECOVER_BEFORE, D_PM_SYS_BEFORE, D_PM_32K_BEFORE,
    D_PM_SYS_AFTER, D_PM_32K_AFTER, D_SPI_BYTES, D_SPI_CONTROL, D_SPI_SAMPLE,
    D_FLAGS, STALL_DETAIL_U32
};
enum { D_HAS_PM=1u, D_RAW_BEFORE_VALID=2u, D_RAW_AFTER_VALID=4u, D_SPI_VALID=8u };
static uint32_t s_detail[STALL_DETAIL_U32];
enum { P_START, P_END, P_DEADLINE, P_REMAINING, P_SDK_TIMING, P_DECISION, STALL_PREP_U32 };
static uint32_t s_prepare[STALL_PREP_U32];
static uint16_t s_boot_words[6];
static uint8_t s_running, s_raw_ready, s_gap_reason, s_resume_stage;
static uint8_t s_sleep_reason, s_deep_committed;
static volatile uint32_t s_irq_enters, s_irq_exits, s_irq_source;
static cpu_pm_handler_t s_pm_driver;

/* 只有首次异常才复制32个字；IRQ独立计数，不改主循环阶段。 */
static _attribute_ram_code_ void freeze_gap(uint8_t reason, uint8_t resume, uint32_t gap_ms)
{
    uint8_t i;
    if (!s_running || s_gap_reason) return;
    s_live[W_IRQ_ENTERS] = s_irq_enters;
    s_live[W_IRQ_EXITS] = s_irq_exits;
    s_live[W_IRQ_SOURCE] = s_irq_source;
    for (i = 0u; i < STALL_SNAPSHOT_U32; ++i) s_first_gap[i] = s_live[i];
    s_gap_ms = gap_ms;
    s_resume_stage = resume;
    s_gap_reason = reason;
}

void bms_stall_diag_stage(uint8_t stage, uint32_t detail)
{
    uint32_t now;
    uint32_t elapsed;
    if (stage == BMS_STALL_APP_WAKE) {
        ++s_live[W_APP_WAKES];
        return; /* 回调不改外层阶段，保留SDK/PM尚未返回这一事实。 */
    }
    now = clock_time();
    if (stage == BMS_STALL_LOOP) ++s_live[W_LOOPS];
    else if (stage == BMS_STALL_SDK_ENTER) s_live[W_SDK_ENTER] = now;
    else if (stage == BMS_STALL_SDK_RETURN) {
        elapsed = now - s_live[W_SDK_ENTER];
        if (elapsed > BMS_SAMPLE_MAX_POLL_GAP_MS * 16000u)
            freeze_gap(BMS_STALL_GAP_SDK, stage, elapsed / 16000u);
        ++s_live[W_SDK_RETURNS];
        s_live[W_SDK_RETURN] = now;
    } else if (stage == BMS_STALL_SAMPLE_BEGIN) {
        elapsed = now - s_live[W_SAMPLE_BEGIN];
        if (s_live[W_SAMPLES] && elapsed > BMS_SAMPLE_MAX_POLL_GAP_MS * 16000u)
            freeze_gap(BMS_STALL_GAP_SAMPLE, stage, elapsed / 16000u);
        ++s_live[W_SAMPLES];
        s_live[W_SAMPLE_BEGIN] = now;
    } else if (stage == BMS_STALL_SAMPLE_END) s_live[W_SAMPLE_END] = now;
    elapsed = now - s_live[W_PHASE_TICK];
    if (elapsed > BMS_SAMPLE_MAX_POLL_GAP_MS * 16000u)
        freeze_gap(BMS_STALL_GAP_PHASE, stage, elapsed / 16000u);
    s_live[W_PHASE] = stage;
    s_live[W_PHASE_TICK] = now;
    s_live[W_DETAIL] = detail;
}

/* RAM wrapper只转交一次原驱动，参数和完整返回值原样保留。
 * 普通Suspend不读32K/模拟总线；DeepSleep仅额外维护REG7标记。
 * 不改变IRQ、唤醒源、期限或PM许可。 */
static _attribute_ram_code_ int stall_sleep_wakeup(SleepMode_TypeDef mode,
    SleepWakeupSrc_TypeDef sources, unsigned int deadline)
{
    int result;
    uint32_t elapsed;
    uint32_t timer_before, recover_before, sys_before, raw_before, cached_before;
    uint8_t boot_hold = s_live[W_PHASE] == BMS_STALL_BOOT_HOLD;
    if (!s_gap_reason) {
        s_detail[D_FLAGS] &= ~(D_HAS_PM | D_RAW_BEFORE_VALID | D_RAW_AFTER_VALID);
        if (s_prepare[P_DECISION] != BMS_STALL_PREP_LATE_VETO)
            s_prepare[P_DECISION] = BMS_STALL_PREP_NONE;
    }
    ++s_live[W_PM_ENTERS];
    s_live[W_PM_MODE] = mode;
    s_live[W_PM_SOURCE] = sources;
    s_live[W_PM_DEADLINE] = deadline;
    s_live[W_PM_ENTER] = clock_time();
    elapsed = s_live[W_PM_ENTER] - s_live[W_PHASE_TICK];
    if (elapsed > BMS_SAMPLE_MAX_POLL_GAP_MS * 16000u)
        freeze_gap(BMS_STALL_GAP_PHASE, BMS_STALL_PM_ENTER, elapsed / 16000u);
    s_live[W_PHASE] = BMS_STALL_PM_ENTER;
    s_live[W_PHASE_TICK] = s_live[W_PM_ENTER];
    s_live[W_DETAIL] = 0u;
    if (mode == DEEPSLEEP_MODE) {
        /* REG7由本D014诊断独占：仅DeepSleep尝试时写，不在普通Suspend访问模拟总线。
         * 记录的是进入驱动的意图；驱动返回时撤销，重新启动时由boot先捕获。
         * B85 pm.h规定它跨DeepSleep保留、WDT/chip reset/掉电清除。 */
        uint8_t reason = boot_hold ? STALL_DEEP_BOOT_HOLD :
            (s_deep_committed ? s_sleep_reason : STALL_DEEP_UNATTRIBUTED);
        s_live[W_DETAIL] = STALL_DEEP_TAG | (reason & 0x0Fu);
        analog_write(DEEP_ANA_REG7, (uint8_t)s_live[W_DETAIL]);
    }
    /* 仅数字寄存器/RAM快照；不额外访问模拟总线或改变原驱动参数。 */
    timer_before = (uint32_t)reg_system_tick_mode |
        ((uint32_t)reg_system_tick_ctrl << 8) | ((uint32_t)REG_ADDR16(0x750) << 16);
    recover_before = pm_timRecover.recover_flag;
    sys_before = pm_timRecover.tick_sysClk;
    raw_before = pm_timRecover.tick_32k;
    cached_before = s_live[W_RAW_32K];
    result = s_pm_driver(mode, sources, deadline);
    /* 必须先保存驱动返回，不能由后面的SDK回调覆盖本次唤醒证据。 */
    s_live[W_PM_RETURN] = clock_time();
    s_live[W_PM_RESULT] = (uint32_t)result;
    ++s_live[W_PM_RETURNS];
    if (mode == DEEPSLEEP_MODE) analog_write(DEEP_ANA_REG7, 0u);
    s_live[W_TIMER_REGS] = (uint32_t)reg_system_tick_mode |
        ((uint32_t)reg_system_tick_ctrl << 8) | ((uint32_t)REG_ADDR16(0x750) << 16);
    s_live[W_RECOVER_FLAG] = pm_timRecover.recover_flag;
    elapsed = s_live[W_PM_RETURN] - s_live[W_PM_ENTER];
    /* 冻结前保留最近一对PM上下文；即使SDK改写tick把长停顿缩短，
     * 后续原有32K读发现长间隔时仍有这一对证据。它不自行判定物理入睡。 */
    if (s_running && !s_gap_reason) {
        s_detail[D_PM_RAW_BEFORE] = cached_before;
        s_detail[D_PM_TIMER_BEFORE] = timer_before;
        s_detail[D_PM_RECOVER_BEFORE] = recover_before;
        s_detail[D_PM_SYS_BEFORE] = sys_before;
        s_detail[D_PM_32K_BEFORE] = raw_before;
        s_detail[D_PM_SYS_AFTER] = pm_timRecover.tick_sysClk;
        s_detail[D_PM_32K_AFTER] = pm_timRecover.tick_32k;
        s_detail[D_FLAGS] |= D_HAS_PM | (s_raw_ready ? D_RAW_BEFORE_VALID : 0u);
    }
    if (elapsed > BMS_SAMPLE_MAX_POLL_GAP_MS * 16000u)
        freeze_gap(BMS_STALL_GAP_PM, BMS_STALL_PM_RETURN, elapsed / 16000u);
    s_live[W_PHASE] = BMS_STALL_PM_RETURN;
    s_live[W_PHASE_TICK] = s_live[W_PM_RETURN];
    return result;
}

/* B85 pm.h允许REG1跨watchdog/chip reset保留，仅掉电清零。
 * 本轮清单及两份B85库未发现直接引用0x3b；REG0/REG2/REG6继续归SDK/原PM。
 * REG1仅启动递增一次；REG7在启动捕获后清零，另在DeepSleep调用前/返回后记录/撤销。
 * 普通Suspend及每轮任务不写模拟寄存器。 */
_attribute_ram_code_ void bms_stall_diag_boot(void)
{
    uint8_t previous = analog_read(DEEP_ANA_REG1);
    s_boot_words[0] = (uint16_t)(((uint16_t)previous << 8) | (uint8_t)(previous + 1u));
    s_boot_words[1] = REG_ADDR8(0x72); /* 原始WD状态，只读，不清标志。 */
    s_boot_words[2] = analog_read(DEEP_ANA_REG0);
    s_boot_words[3] = analog_read(DEEP_ANA_REG6);
    s_boot_words[5] = analog_read(DEEP_ANA_REG7);
    analog_write(DEEP_ANA_REG1, (uint8_t)(previous + 1u));
    analog_write(DEEP_ANA_REG7, 0u);
}

void bms_stall_diag_install_pm(void)
{
    if (cpu_sleep_wakeup && cpu_sleep_wakeup != stall_sleep_wakeup) {
        s_pm_driver = cpu_sleep_wakeup;
        cpu_sleep_wakeup = stall_sleep_wakeup;
    }
    s_boot_words[4] = (uint16_t)pm_get_wakeup_src();
}

void bms_stall_diag_start(void)
{
    s_live[W_PHASE] = BMS_STALL_LOOP;
    s_live[W_PHASE_TICK] = clock_time();
    s_raw_ready = 0u;
    s_running = 1u; /* 初始化必要等待不触发运行长间断。 */
}

void bms_stall_diag_sleep_reason(uint8_t reason)
{
    /* 只观察PM已经发布的原因，不读取时间或推进策略；未来原因不能截断成旧原因。 */
    s_sleep_reason = reason <= BMS_SLEEP_REASON_OVERDISCHARGE ? reason : STALL_DEEP_UNATTRIBUTED;
}

void bms_stall_diag_deep_commit(void)
{
    s_deep_committed = 1u;
    bms_stall_diag_stage(BMS_STALL_DEEP_COMMIT, s_sleep_reason);
}

void bms_stall_diag_schedule(uint8_t due, uint32_t deadline)
{
    s_live[W_SAMPLE_DUE] = due;
    s_live[W_SAMPLE_DEADLINE] = deadline;
}

void bms_stall_diag_time(uint32_t raw_tick)
{
    uint32_t elapsed = raw_tick - s_live[W_RAW_32K];
    /* 复用业务本来就会执行的第一次32K读取；不在PM wrapper新增模拟访问。
     * 这个差包含PM内部及返回后的业务时间，不能等同物理Suspend时长。 */
    if ((s_detail[D_FLAGS] & D_HAS_PM) && !(s_detail[D_FLAGS] & D_RAW_AFTER_VALID)) {
        s_detail[D_PM_RAW_AFTER] = raw_tick;
        s_detail[D_FLAGS] |= D_RAW_AFTER_VALID;
    }
    s_live[W_DETAIL] &= 0x7FFFFFFFu; /* 读已经返回，冻结现场不能误报仍在等待。 */
    if (s_raw_ready && elapsed > BMS_SAMPLE_MAX_POLL_GAP_MS * 32u)
        freeze_gap(BMS_STALL_GAP_32K, (uint8_t)s_live[W_PHASE], elapsed / 32u);
    s_live[W_RAW_32K] = raw_tick;
    s_raw_ready = s_running;
}

void bms_stall_diag_time_begin(void)
{
    s_live[W_DETAIL] |= 0x80000000u;
}

void bms_stall_diag_bus(uint8_t writing, uint8_t reg)
{
    s_live[W_DETAIL] = (s_live[W_DETAIL] & 0x80000000u) |
        (writing ? 0x200u : 0x100u) | reg;
}

_attribute_ram_code_ void bms_stall_diag_irq(uint8_t entering)
{
    if (entering) {
        ++s_irq_enters;
        s_irq_source = reg_irq_src;
    } else ++s_irq_exits;
}

void bms_stall_diag_afe_flags(uint8_t valid, uint8_t flag1, uint8_t flag2)
{
    s_afe_flags = ((uint32_t)valid << 16) | ((uint32_t)flag1 << 8) | flag2;
}

void bms_stall_diag_afe(uint16_t stage, int16_t error, uint32_t crc, uint32_t retries)
{
    uint32_t event = ((uint32_t)stage << 16) | (uint16_t)error;
    ++s_live[W_AFE_FAILURES];
    s_live[W_AFE_LAST] = event;
    if (s_first_afe[0]) return;
    s_first_afe[0] = event;
    s_first_afe[1] = clock_time();
    s_first_afe[2] = s_live[W_RAW_32K];
    s_first_afe[3] = s_afe_flags;
    s_first_afe[4] = crc;
    s_first_afe[5] = retries;
    s_first_afe[6] = s_live[W_GUARD_BITS];
    s_first_afe[7] = s_live[W_PHASE];
}

void bms_stall_diag_guard(uint16_t bits, uint32_t elapsed_ms, uint32_t wait_ms)
{
    s_live[W_GUARD_BITS] = bits;
    s_live[W_GUARD_ELAPSED] = elapsed_ms;
    s_live[W_GUARD_WAIT] = wait_ms;
}

void bms_stall_diag_suspend_veto(int32_t remaining_ticks)
{
    ++s_detail[D_VETO_COUNT];
    s_detail[D_VETO_REMAINING] = (uint32_t)remaining_ticks;
}

void bms_stall_diag_prepare_begin(uint32_t start_tick, uint32_t deadline, uint32_t sdk_timing_us)
{
    if (s_gap_reason || s_prepare[P_DECISION] == BMS_STALL_PREP_LATE_VETO) return;
    s_prepare[P_START] = start_tick;
    s_prepare[P_END] = start_tick;
    s_prepare[P_DEADLINE] = deadline;
    s_prepare[P_REMAINING] = deadline - start_tick;
    s_prepare[P_SDK_TIMING] = sdk_timing_us;
    s_prepare[P_DECISION] = BMS_STALL_PREP_RUNNING;
}

void bms_stall_diag_prepare_end(uint32_t end_tick, uint8_t decision)
{
    if (s_gap_reason || s_prepare[P_DECISION] == BMS_STALL_PREP_LATE_VETO) return;
    s_prepare[P_END] = end_tick;
    s_prepare[P_REMAINING] = s_prepare[P_DEADLINE] - end_tick;
    s_prepare[P_DECISION] = decision;
}

void bms_stall_diag_spi_mismatch(uint8_t tx, uint8_t expected, uint8_t actual)
{
    /* 重试期间只保留最新回显；首次AFE事件确定后冻结。样本编号用于排除旧重试。
     * 不重读SPI数据、不清标志，不参与驱动恢复或输出决策。 */
    if (s_first_afe[0]) return;
    s_detail[D_SPI_BYTES] = ((uint32_t)tx << 16) | ((uint32_t)expected << 8) | actual;
    s_detail[D_SPI_CONTROL] = (s_live[W_DETAIL] & 0xFFFFu) | ((uint32_t)reg_spi_ctrl << 16);
    s_detail[D_SPI_SAMPLE] = s_live[W_SAMPLES];
    s_detail[D_FLAGS] |= D_SPI_VALID;
}

/* 每个word大端；u32高word在前。读取不更新时间、不消费或清除冻结现场。
 * IRQ仅更新三个独立volatile计数，跨请求须对照boot/冻结原因；不声称IRQ原子快照。 */
int bms_stall_diag_read(uint16_t start, uint16_t count, uint8_t *bytes)
{
    uint16_t i;
    uint16_t base = start >= BMS_STALL_PREP_BASE ? BMS_STALL_PREP_BASE :
        (start >= BMS_STALL_DETAIL_BASE ? BMS_STALL_DETAIL_BASE : BMS_STALL_DIAG_BASE);
    uint16_t words = base == BMS_STALL_PREP_BASE ? BMS_STALL_PREP_WORDS :
        (base == BMS_STALL_DETAIL_BASE ? BMS_STALL_DETAIL_WORDS : BMS_STALL_DIAG_WORDS);
    if (!bytes || !count || count > 125u || start < base ||
        (uint32_t)start + count > (uint32_t)base + words) return 0;
    for (i = 0u; i < count; ++i) {
        uint16_t offset = (uint16_t)(start - base + i);
        uint16_t value = 0u;
        if (base == BMS_STALL_PREP_BASE) {
            if (offset < 4u)
                value = offset == 0u ? 0x5350u : (offset == 3u ? BMS_STALL_PREP_WORDS : 1u);
            else value = (uint16_t)(s_prepare[(offset-4u)/2u] >> ((offset & 1u) ? 0u : 16u));
        } else if (base == BMS_STALL_DETAIL_BASE) {
            if (offset < 4u)
                value = offset == 0u ? 0x5358u : (offset == 3u ? BMS_STALL_DETAIL_WORDS : 1u);
            else value = (uint16_t)(s_detail[(offset-4u)/2u] >> ((offset & 1u) ? 0u : 16u));
        } else if (offset < 16u) {
            if (offset == 0u) value = 0x5354u;
            else if (offset == 1u) value = BMS_STALL_DIAG_SCHEMA;
            else if (offset == 2u) value = 1u;
            else if (offset == 3u) value = BMS_STALL_DIAG_WORDS;
            else if (offset == 4u) value = s_gap_reason;
            else if (offset == 5u) value = s_resume_stage;
            else if (offset < 8u) value = (uint16_t)(BMS_DIAG_BUILD_ID >> ((7u-offset)*16u));
            else if (offset < 13u) value = s_boot_words[offset-8u];
            else if (offset == 13u) value = s_boot_words[5];
            else if (offset == 14u) value = (uint16_t)(s_gap_ms >> 16);
            else if (offset == 15u) value = (uint16_t)s_gap_ms;
        } else {
            uint32_t word;
            if (offset < 80u) {
                uint16_t slot = (uint16_t)((offset - 16u) / 2u);
                word = slot == W_IRQ_ENTERS ? s_irq_enters :
                    (slot == W_IRQ_EXITS ? s_irq_exits :
                     (slot == W_IRQ_SOURCE ? s_irq_source : s_live[slot]));
            } else if (offset < 144u) word = s_first_gap[(offset-80u)/2u];
            else word = s_first_afe[(offset-144u)/2u];
            value = (uint16_t)(word >> ((offset & 1u) ? 0u : 16u));
        }
        bytes[2u*i] = (uint8_t)(value >> 8);
        bytes[2u*i+1u] = (uint8_t)value;
    }
    return 1;
}
#endif
