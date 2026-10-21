/* D014 开发取证：固定 RAM 快照，不参与业务决策、不主动通信或写 Flash。 */
#include "bms_product.h"
#include "bms_stall_diag.h"
#if BMS_STALL_DIAG_ENABLE
#include "tl_common.h"
#include "drivers.h"
#include "bms_diag.h"
#include "bms_timing.h"

#if BMS_PRODUCT_ID != 14u || MCU_CORE_TYPE != MCU_CORE_825x
#error "Stall diagnostic PM/retained-register ownership is reviewed only for D014/B85"
#endif
#if !PM_TIM_RECOVER_MODE
#error "Stall diagnostic requires the reviewed B85 PM_TIM_RECOVER_MODE"
#endif

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
static uint16_t s_boot_words[5];
static uint8_t s_running, s_raw_ready, s_gap_reason, s_resume_stage;
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
 * 不读取32K/模拟总线，不改变IRQ、唤醒源、期限或PM许可。 */
static _attribute_ram_code_ int stall_sleep_wakeup(SleepMode_TypeDef mode,
    SleepWakeupSrc_TypeDef sources, unsigned int deadline)
{
    int result;
    uint32_t elapsed;
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
    result = s_pm_driver(mode, sources, deadline);
    /* 必须先保存驱动返回，不能由后面的SDK回调覆盖本次唤醒证据。 */
    s_live[W_PM_RETURN] = clock_time();
    s_live[W_PM_RESULT] = (uint32_t)result;
    ++s_live[W_PM_RETURNS];
    s_live[W_TIMER_REGS] = (uint32_t)reg_system_tick_mode |
        ((uint32_t)reg_system_tick_ctrl << 8) | ((uint32_t)REG_ADDR16(0x750) << 16);
    s_live[W_RECOVER_FLAG] = pm_timRecover.recover_flag;
    elapsed = s_live[W_PM_RETURN] - s_live[W_PM_ENTER];
    if (elapsed > BMS_SAMPLE_MAX_POLL_GAP_MS * 16000u)
        freeze_gap(BMS_STALL_GAP_PM, BMS_STALL_PM_RETURN, elapsed / 16000u);
    s_live[W_PHASE] = BMS_STALL_PM_RETURN;
    s_live[W_PHASE_TICK] = s_live[W_PM_RETURN];
    return result;
}

/* B85 pm.h允许REG1跨watchdog/chip reset保留，仅掉电清零。
 * 本轮清单及两份B85库未发现直接引用0x3b；REG0/REG2/REG6继续归SDK/原PM。
 * 仅启动读取并递增一次，不在每轮或每次睡眠写模拟寄存器。 */
_attribute_ram_code_ void bms_stall_diag_boot(void)
{
    uint8_t previous = analog_read(DEEP_ANA_REG1);
    s_boot_words[0] = (uint16_t)(((uint16_t)previous << 8) | (uint8_t)(previous + 1u));
    s_boot_words[1] = REG_ADDR8(0x72); /* 原始WD状态，只读，不清标志。 */
    s_boot_words[2] = analog_read(DEEP_ANA_REG0);
    s_boot_words[3] = analog_read(DEEP_ANA_REG6);
    analog_write(DEEP_ANA_REG1, (uint8_t)(previous + 1u));
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

void bms_stall_diag_schedule(uint8_t due, uint32_t deadline)
{
    s_live[W_SAMPLE_DUE] = due;
    s_live[W_SAMPLE_DEADLINE] = deadline;
}

void bms_stall_diag_time(uint32_t raw_tick)
{
    uint32_t elapsed = raw_tick - s_live[W_RAW_32K];
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

/* 每个word大端；u32高word在前。读取不更新时间、不消费或清除冻结现场。
 * IRQ仅更新三个独立volatile计数，跨请求须对照boot/冻结原因；不声称IRQ原子快照。 */
int bms_stall_diag_read(uint16_t start, uint16_t count, uint8_t *bytes)
{
    uint16_t i;
    if (!bytes || !count || count > 125u || start < BMS_STALL_DIAG_BASE ||
        (uint32_t)start + count > BMS_STALL_DIAG_END) return 0;
    for (i = 0u; i < count; ++i) {
        uint16_t offset = (uint16_t)(start - BMS_STALL_DIAG_BASE + i);
        uint16_t value = 0u;
        if (offset < 16u) {
            if (offset == 0u) value = 0x5354u;
            else if (offset == 1u || offset == 2u) value = 1u;
            else if (offset == 3u) value = BMS_STALL_DIAG_WORDS;
            else if (offset == 4u) value = s_gap_reason;
            else if (offset == 5u) value = s_resume_stage;
            else if (offset < 8u) value = (uint16_t)(BMS_DIAG_BUILD_ID >> ((7u-offset)*16u));
            else if (offset < 13u) value = s_boot_words[offset-8u];
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
