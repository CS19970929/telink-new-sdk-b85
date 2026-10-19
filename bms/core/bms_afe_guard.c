/*
 * 文件功能：AFE 通信安全门禁；管理输出授权、失联隔离、watchdog 静默和完整轮次恢复。
 * bms/core/bms_afe_guard.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_afe_driver.h"
#include "bms_afe_registers.h"
#include "bms_afe_hw_access.h"
#include "bms_diag.h"
#include "bms_features.h"
#include "bms_error.h"
#include "bms_sw_protection.h"
#include "bms_parameters.h"
#include "bms_time.h"
#include "bms_state.h"
#include <string.h>

#define BMS_AFE_INVALID_FAILURES_BEFORE_SILENCE    2u
#define BMS_AFE_COMM_CONFIRM_MS             1000u

/* MCU 撤销请求后按所选后端的固定 watchdog 窗口保持总线静默。 */

typedef struct
{
    uint8_t requested_charge_on;
    uint8_t requested_discharge_on;
    uint8_t output_enabled;
    uint8_t comm_inhibit;
    uint8_t config_inhibit;
    uint8_t comm_fault_latched;
    uint8_t comm_failures;
    uint8_t bus_silenced;
    uint8_t test_shutdown_hold;
    uint8_t fet_driver_bits;
    uint8_t fet_driver_valid;
    uint8_t cycle_failed; /* 本轮任一必要事务失败，成功的局部事务不能清除观察。 */
    uint32_t comm_failure_started_ms;
    uint32_t failsafe_start_tick_ms;
} bms_afe_guard_state_t;

/* 输出授权与通信恢复的唯一状态；主循环更新，backend 不得绕过 guard 清除 inhibit。 */
static bms_afe_guard_state_t s_guard;
/* 主循环独占；AFE 重初始化不清除，MCU reset 后 BSS 恢复为允许。 */
static uint8_t s_remote_off_mask;
#if BMS_AFE_REGISTER_DEBUG_ENABLE
static uint8_t s_register_debug_hold;
#endif

/* 业务期访问统一受通信隔离/总线静默/测试关机门禁约束；
 * 配置禁止由各自写入事务控制，不能在此悄悄改变配置恢复策略。 */
static inline uint8_t runtime_access_allowed(void)
{
    return (!s_guard.comm_inhibit && !s_guard.bus_silenced &&
            !s_guard.test_shutdown_hold && !bms_afe_register_debug_held()) ? 1u : 0u;
}

/* 完整有效通信解除初始化禁止；配置事务仍拥有独立门禁。 */
static uint8_t output_ready(void)
{
    return runtime_access_allowed() && !s_guard.config_inhibit;
}

/* 判断当前 guard 阶段是否允许访问 AFE 总线。 */
uint8_t bms_afe_bus_access_allowed(void)
{
    return (s_guard.bus_silenced || s_guard.test_shutdown_hold ||
            bms_afe_register_debug_held()) ? 0u : 1u;
}

/* 设置本地输出禁止状态，撤销驱动授权。 */
static void inhibit_local(void)
{
    s_guard.comm_fault_latched = 1u;
    s_guard.comm_inhibit = 1u;
    s_guard.cycle_failed = 1u;
    g_bms_system_status.bits.afe1_status = 0u;
    bms_afe_publish_fet_status(0u, 0u);
    bms_features_stop();
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
}

/* 仅尽力关闭均衡/MOS，不发送芯片 Shutdown/Powerdown 模式命令。 */
static void best_effort_shutdown(void)
{
    if (s_guard.bus_silenced || s_guard.test_shutdown_hold || bms_afe_register_debug_held()) return;

    /*
     * 这次软件关闭不是失联安全保证，只是总线可能仍接受命令时的最后尝试。
     * 总线已失效时，以硬件 AFE 看门狗关断为准。
     */
#if BMS_FEATURE_BALANCE_SUPPORTED
    (void)bms_afe_driver_set_balance_mask(0u, NULL);
#endif
    (void)bms_afe_driver_set_fets(0u, 0u);
}

/* 进入硬件看门狗故障安全等待阶段并禁止总线访问。 */
static void enter_failsafe_wait(void)
{
    inhibit_local();
    s_guard.bus_silenced = 1u;
    s_guard.failsafe_start_tick_ms = bms_time_ms();
    s_guard.comm_failures = 0u;
}

/* 硬件 watchdog 等待阶段禁止 AFE 总线访问；到期仅探测一次，恢复仍需完整有效测量。 */
static uint8_t service_failsafe_wait(void)
{
    if (!s_guard.bus_silenced) return 0u; /* 调用方已单独拦截主动 shutdown。 */

    /* 硬件看门狗计时期间绝不进行 AFE I2C/SPI 访问。 */
    if ((uint32_t)(bms_time_ms() - s_guard.failsafe_start_tick_ms) <=
        bms_afe_driver_policy.failsafe_wait_ms)
    {
        if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
        return 1u;
    }

    /* 看门狗窗口结束后只作一次有界恢复尝试。 */
    s_guard.bus_silenced = 0u;
    s_guard.comm_failures = 0u;
    s_guard.comm_inhibit = 1u;

    /* SH 重建观察窗口，不能把 AFE 恢复当成电压/温度故障已经解除。 */
    if (bms_afe_driver_policy.reset_protection_before_init)
        bms_sw_protection_reset_filters();
    bms_afe_driver_init();

    /* 后端初始化可清自身通信错误，但尚未完成资格确认，因此系统 AFE 故障仍保持置位。 */
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    return 1u;
}

/* 合并调用方 MOS 意图与 guard 授权；应用侧请求并不等同于真实 Gate 已导通。 */
static uint8_t apply_requested(uint8_t explicit_off_mask)
{
    uint8_t c;
    uint8_t d;
    uint8_t direction_allow_mask;
    uint8_t sample_valid;

    /* 通信禁止期间 MCU 只管理授权，不能反复写关闭命令而意外喂 AFE WDT。 */
    if (!runtime_access_allowed()) return 1u;

    c = s_guard.requested_charge_on;
    d = s_guard.requested_discharge_on;
    sample_valid = !s_guard.cycle_failed &&
        bms_afe_driver_get_aux_measurements(NULL);
    if (!s_guard.output_enabled || !bms_protection_params_valid() ||
        !output_ready())
    {
        c = 0u;
        d = 0u;
    }
    /* 方向未知不代表此前反向放行已结束。失效轮次只关闭产品撤销、全局禁止、
     * 本次新用户关闭及后端已确认保护；不能重放旧远程 OFF 意图。 */
    if (!sample_valid)
        return bms_afe_driver_disable_fets(
            !c || (explicit_off_mask & BMS_AFE_DIRECTION_ALLOW_CHARGE_MASK),
            !d || (explicit_off_mask & BMS_AFE_DIRECTION_ALLOW_DISCHARGE_MASK));
    direction_allow_mask = bms_afe_driver_get_direction_allow_mask();
    /*
     * 用户关闭意图始终保留。AFE 确认放电时临时放行充电 MOS，确认充电时
     * 临时放行放电 MOS；方向消失后下一次仲裁自动恢复用户关闭。
     */
    if ((s_remote_off_mask & BMS_AFE_DIRECTION_ALLOW_CHARGE_MASK) &&
        !(direction_allow_mask & BMS_AFE_DIRECTION_ALLOW_CHARGE_MASK)) c = 0u;
    if ((s_remote_off_mask & BMS_AFE_DIRECTION_ALLOW_DISCHARGE_MASK) &&
        !(direction_allow_mask & BMS_AFE_DIRECTION_ALLOW_DISCHARGE_MASK)) d = 0u;
    /* 方向阻断由选定后端处理，保留同口放电及 DVC AUTO_DIODE 语义。 */
    return bms_afe_driver_set_fets(c, d);
}

/* 记录无效样本并更新连续失败与输出隔离状态。 */
static void note_invalid(void)
{
    /* 普通读失败观察不能吞掉随后已确认故障的首次关闭尝试。 */
    if (!s_guard.comm_fault_latched) s_guard.comm_failures = 0u;
    inhibit_local();

    /*
     * 只作一次尽力受控关断；失效总线上的重复写入既无效，
     * 也可能阻止不稳定 AFE WDT 到期。
     */
    if (s_guard.comm_failures == 0u) best_effort_shutdown();

    if (s_guard.comm_failures != 0xFFu) ++s_guard.comm_failures;
    if (s_guard.comm_failures >= BMS_AFE_INVALID_FAILURES_BEFORE_SILENCE)
        enter_failsafe_wait();
}

/* 普通通信失败只记录观察；不能通过撤销资格间接关闭已运行的 MOS。
 * 同一失败周期中不以零星成功事务清零，完整读取和输出仲裁成功后才复位。
 */
static void note_communication_failure(void)
{
    uint32_t now = bms_time_ms();
    s_guard.cycle_failed = 1u;
    bms_afe_publish_fet_status(0u, 0u);
    if (s_guard.comm_fault_latched) s_guard.comm_inhibit = 1u;
    bms_features_stop();
    if (s_guard.comm_failures == 0u) s_guard.comm_failure_started_ms = now;
    if (s_guard.comm_failures != 0xFFu) ++s_guard.comm_failures;
    if ((uint32_t)(now - s_guard.comm_failure_started_ms) >= BMS_AFE_COMM_CONFIRM_MS) {
        best_effort_shutdown();
        enter_failsafe_wait();
    }
}

/* 初始化 guard 与选定后端；完整测量和配置验证前禁止输出。 */
void bms_afe_init(void)
{
    uint8_t config_inhibit = s_guard.config_inhibit;
    memset(&s_guard, 0, sizeof(s_guard));
    s_guard.comm_inhibit = 1u;
    s_guard.cycle_failed = 1u;
    s_guard.config_inhibit = config_inhibit;
    if (bms_afe_driver_policy.reset_protection_before_init)
        bms_sw_protection_init();
    bms_afe_driver_init();
    /* 同进程重初始化不能仅靠 RAM 清零解除未验证配置；完整应用成功才解除。 */
    if (config_inhibit && bms_afe_driver_apply_protection_config()) s_guard.config_inhibit = 0u;
    if (!bms_afe_driver_set_fets(0u, 0u)) note_invalid();
    if (!bms_afe_driver_policy.reset_protection_before_init)
        bms_sw_protection_init();
    bms_features_init();
#if BMS_FEATURE_BALANCE_SUPPORTED
    (void)bms_afe_driver_set_balance_mask(0u, NULL);
#endif
    g_bms_system_status.bits.afe1_status = 0u;
}

/* 通过安全门禁采样并更新恢复资格及输出授权。 */
bms_afe_sample_status_t bms_afe_sample(void)
{
    bms_afe_sample_status_t result = BMS_AFE_SAMPLE_NOT_READY;

#if BMS_AFE_REGISTER_DEBUG_ENABLE
    bms_afe_hw_access_poll();
#endif
    if (bms_afe_register_debug_held()) return result;
    if (s_guard.test_shutdown_hold || service_failsafe_wait()) return result;
    s_guard.cycle_failed = 0u;
    result = bms_afe_driver_sample();
    if (result == BMS_AFE_SAMPLE_NOT_READY) {
        /* 芯片初始化/重配置的必要等待不计为通信失败。 */
        s_guard.comm_inhibit = 1u;
        s_guard.cycle_failed = 1u;
        bms_afe_publish_fet_status(0u, 0u);
        bms_features_stop();
        return result;
    }
    if (result == BMS_AFE_SAMPLE_READ_ERROR) {
        note_communication_failure();
        /* 后端只允许撤销已有命令；已读出的真实保护仍可立即关断方向。 */
        (void)apply_requested(0u);
        return result;
    }
    if (result == BMS_AFE_SAMPLE_ERROR ||
        !bms_afe_driver_get_aux_measurements(NULL)) {
        note_invalid();
        result = BMS_AFE_SAMPLE_ERROR;
        return result;
    }

    /* 只在完整测量之后允许仲裁；AFE1 要等本轮必要控制也成功才解除。 */
    if (!s_guard.config_inhibit) s_guard.comm_inhibit = 0u;
    return result;
}

void bms_afe_apply_outputs(void)
{
    if (!runtime_access_allowed()) return;
    if (!apply_requested(0u) || s_guard.cycle_failed) {
        note_communication_failure();
        return;
    }
    s_guard.comm_failures = 0u;
    if (!s_guard.config_inhibit) {
        s_guard.comm_fault_latched = 0u;
        bms_error_clear(BMS_ERROR_AFE1);
        g_bms_system_status.bits.afe1_status = 1u;
    }
}

/* 产品低功耗模式固定：DVC 仅用 Shutdown，SH 仅用 Sleep。 */
uint8_t bms_afe_sleep(void)
{
#if BMS_AFE_REGISTER_DEBUG_ENABLE
    if (s_register_debug_hold) {
        /* 强制休眠接管调试：只撤销输出和写授权，不恢复配置或开启 MOS。
         * 之后仍遵守总线静默及其它访问门禁，失败不能恢复调试业务。 */
        s_guard.output_enabled = 0u;
        s_register_debug_hold = 0u;
        bms_afe_hw_access_close();
        /* 冻结时前一次撤销不能访问 AFE；接管后补做只关闭尝试。 */
        best_effort_shutdown();
    }
#endif
    if (!bms_afe_bus_access_allowed() ||
        (bms_afe_driver_policy.sleep_holds_bus && !runtime_access_allowed())) return 0u;
    inhibit_local();
    if (bms_afe_driver_policy.sleep_holds_bus) {
#if BMS_FEATURE_BALANCE_SUPPORTED
        if (!bms_afe_driver_set_balance_mask(0u, NULL)) { note_invalid(); return 0u; }
#endif
        if (!bms_afe_driver_set_fets(0u, 0u)) { note_invalid(); return 0u; }
    }
    if (!bms_afe_driver_sleep()) { note_invalid(); return 0u; }
    if (bms_afe_driver_policy.sleep_holds_bus) s_guard.test_shutdown_hold = 1u;
    s_guard.comm_failures = 0u;
    return 1u;
}

/* 安全门禁允许时应用独立硬件保护配置。 */
uint8_t bms_afe_apply_protection_config(void)
{
    uint8_t ok;

    if (bms_afe_register_debug_held() || s_guard.bus_silenced || s_guard.test_shutdown_hold ||
        (s_guard.comm_inhibit && !s_guard.config_inhibit)) return 0u;
    s_guard.config_inhibit = 1u;
    ok = bms_afe_driver_apply_protection_config();
    if (ok) s_guard.config_inhibit = 0u;
    else note_invalid();
    return ok;
}

/* 配置事务所有者撤销授权；通信恢复不能证明部分写入的寄存器已经一致。 */
void bms_afe_invalidate_configuration(void)
{
    s_guard.config_inhibit = 1u;
    inhibit_local();
    best_effort_shutdown();
}

/* 保存充放电意图并按 guard 资格应用到后端。 */
uint8_t bms_afe_set_fets(uint8_t c, uint8_t d)
{
    uint8_t requested_c = c ? 1u : 0u;
    uint8_t requested_d = d ? 1u : 0u;

    if (s_guard.requested_charge_on == requested_c &&
        s_guard.requested_discharge_on == requested_d)
    {
        return 1u;
    }

    s_guard.requested_charge_on = requested_c;
    s_guard.requested_discharge_on = requested_d;

    /* apply_requested 统一执行总线门禁，未合格时仅保存意图。 */
    if (!apply_requested(0u))
    {
        note_communication_failure();
        return 0u;
    }
    return 1u;
}

uint8_t bms_afe_get_remote_allowed_mask(void)
{
    return (uint8_t)(3u & ~s_remote_off_mask);
}

uint8_t bms_afe_set_channel_allowed(uint8_t channel, uint8_t allowed)
{
    uint8_t bit;
    uint8_t old_mask;
    if (channel > 1u || allowed > 1u) return 0u;
    bit = (uint8_t)(1u << channel);
    old_mask = s_remote_off_mask;
    if (allowed) s_remote_off_mask &= (uint8_t)~bit;
    else s_remote_off_mask |= bit;
    if (s_remote_off_mask == old_mask) return 1u;
    /* 命令已变化但尚未获得新回读，不能继续发布旧 driver 状态。 */
    bms_afe_publish_fet_status(0u, 0u);
    /* 静默阶段只记意图，不访问总线；AFE 恢复后继续执行关闭。 */
    if (!apply_requested(allowed ? 0u : bit)) {
        /* 第一次失败立即封住本轮开启，补偿成功也不能消除该观察。 */
        note_communication_failure();
        /* 开启许可失败时恢复原来的关闭许可；关闭失败仍保留 fail-safe 意图。 */
        if (allowed) {
            s_remote_off_mask = old_mask;
            (void)apply_requested(bit);
        }
        return 0u;
    }
    return 1u;
}

/* 读取应用侧保存的充放电请求。 */
void bms_afe_get_requested_fets(uint8_t *charge_on, uint8_t *discharge_on)
{
    if (charge_on != 0) *charge_on = s_guard.requested_charge_on;
    if (discharge_on != 0) *discharge_on = s_guard.requested_discharge_on;
}

/* 后端发布最新实际 FET 回读，诊断只是该业务状态的可选镜像。 */
void bms_afe_publish_fet_status(uint8_t driver_bits, uint8_t valid)
{
    s_guard.fet_driver_bits = valid ? (driver_bits & 3u) : 0u;
    s_guard.fet_driver_valid = valid ? 1u : 0u;
    bms_diag_driver(s_guard.fet_driver_bits, s_guard.fet_driver_valid);
}

/* 标准控制窗口读取业务状态，不依赖开发诊断缓存。 */
uint8_t bms_afe_get_fet_status(uint8_t *driver_bits)
{
    if (driver_bits == 0) return 0u;
    *driver_bits = s_guard.fet_driver_bits;
    return s_guard.fet_driver_valid;
}

/* 取得输出禁止、通信隔离和恢复阶段诊断位。 */
uint16_t bms_afe_get_guard_diagnostic_bits(void)
{
    uint16_t bits = 0u;
    if (s_guard.output_enabled) bits |= DIAG_GUARD_OUTPUT_ENABLED;
    if (s_guard.comm_inhibit) bits |= DIAG_GUARD_COMM_INHIBIT;
    if (s_guard.bus_silenced || s_guard.test_shutdown_hold) bits |= DIAG_GUARD_BUS_SILENCED;
    if (s_guard.comm_fault_latched) bits |= DIAG_GUARD_FAULT_LATCHED;
    if (output_ready())
        bits |= DIAG_GUARD_SAMPLES_QUALIFIED;
    return bits;
}

/* 设置应用输出授权并重新仲裁 MOS 请求。 */
void bms_afe_set_output_enabled(uint8_t e)
{
    s_guard.output_enabled = e ? 1u : 0u;

    if (!s_guard.output_enabled)
    {
        /* 关闭输出不代表电压测量或采样线故障。 */
        bms_features_stop();
        bms_afe_publish_fet_status(0u, 0u);
        best_effort_shutdown();
        return;
    }

    if (!apply_requested(0u)) note_communication_failure();
}

/* 取得选定后端的辅助测量快照。 */
uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m)
{
    if (!m) return 0u;
    if (!runtime_access_allowed() || !bms_afe_driver_get_aux_measurements(m))
    {
        memset(m, 0, sizeof(*m));
        return 0u;
    }
    return 1u;
}

/* 取得公共功能所需的后端采样快照。 */
uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *s)
{
    /* SOC 同样消费电池温度；测量有效性不能由加热/均衡能力裁剪。 */
    if (!s || !runtime_access_allowed()) return 0u;
    return bms_afe_driver_get_feature_snapshot(s);
}

/* 取得后端可验证的充电源存在状态。 */
uint8_t bms_afe_get_charge_source_present(uint8_t *p)
{
#if BMS_FEATURE_HEATER_SUPPORTED
    if (!p || !runtime_access_allowed()) return 0u;
    return bms_afe_driver_get_charge_source_present(p);
#else
    if (p) *p = 0u;
    return 0u;
#endif
}

/* 门禁允许时更新有效通道均衡请求。 */
uint8_t bms_afe_set_balance_mask(uint32_t m, const uint32_t *known_actual)
{
#if BMS_FEATURE_BALANCE_SUPPORTED
    /* 即使请求 OFF，总线无法访问也不能声称已经物理确认关闭。 */
    if (!runtime_access_allowed()) return 0u;
    if (s_guard.cycle_failed && m != 0u) return 0u;
    if (s_guard.cycle_failed) known_actual = NULL;
    if (bms_afe_driver_set_balance_mask(m, known_actual)) return 1u;
    s_guard.cycle_failed = 1u;
    return 0u;
#else
    (void)known_actual;
    return (m == 0u) ? 1u : 0u;
#endif
}

/* 读取当前后端均衡状态掩码。 */
uint8_t bms_afe_get_balance_mask(uint32_t *m)
{
    if (!m) return 0u;
#if BMS_FEATURE_BALANCE_SUPPORTED
    if (!runtime_access_allowed())
    {
        *m = 0u;
        return 0u;
    }
    if (bms_afe_driver_get_balance_mask(m)) return 1u;
    s_guard.cycle_failed = 1u;
    return 0u;
#else
    *m = 0u;
    return 1u;
#endif
}

#if defined(BMS_HOST_TEST) && BMS_HOST_TEST
/* 测试构建中执行 AFE 唤醒并重新获取资格。 */
uint8_t bms_afe_test_wake(void)
{
    if (!s_guard.test_shutdown_hold) return 0u;

    s_guard.test_shutdown_hold = 0u;
    s_guard.comm_inhibit = 1u;
    s_guard.comm_failures = 0u;
    s_guard.bus_silenced = 0u;
    s_guard.failsafe_start_tick_ms = 0u;

    /*
     * 正常后端初始化负责完整 shutdown 唤醒/复位、持久硬件配置、编译期策略与回读；
     * 公共门禁确认完整有效测量前保持请求阻断。
     */
    bms_afe_driver_init();
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    return 1u;
}
#endif

#if BMS_AFE_REGISTER_DEBUG_ENABLE
uint8_t bms_afe_register_debug_held(void) { return s_register_debug_hold; }

uint8_t bms_afe_register_bus_access_allowed(void)
{
    return (!s_guard.bus_silenced && !s_guard.test_shutdown_hold) ? 1u : 0u;
}

uint8_t bms_afe_register_debug_begin(void)
{
    bms_afe_aux_measurements_t sample;
    if (s_register_debug_hold) return 1u;
    if (!output_ready() || !bms_afe_driver_get_aux_measurements(&sample) ||
        sample.current_ma < -200 || sample.current_ma > 200) return 0u;
    bms_features_stop();
    if (!bms_afe_driver_disable_fets(1u, 1u)) { note_invalid(); return 0u; }
#if BMS_FEATURE_BALANCE_SUPPORTED
    if (!bms_afe_driver_set_balance_mask(0u, NULL)) { note_invalid(); return 0u; }
#endif
    s_register_debug_hold = 1u;
    s_guard.cycle_failed = 1u;
    g_bms_system_status.bits.afe1_status = 0u;
    bms_afe_publish_fet_status(0u, 0u);
    return 1u;
}

uint8_t bms_afe_register_debug_restore(void)
{
    if (!s_register_debug_hold) return 1u;
    if (!bms_afe_register_bus_access_allowed()) return 0u;
    /* 不调用 bms_afe_init，避免清除软件活动故障、用户请求和远程关闭意图。
     * 后端完整恢复源码固定配置和持久 profile；失败继续冻结，不能直接恢复输出。 */
    s_guard.comm_inhibit = 1u;
    s_guard.cycle_failed = 1u;
    bms_afe_driver_init();
    if (!bms_afe_driver_register_config_ready() || !bms_afe_driver_apply_protection_config() ||
        !bms_afe_driver_disable_fets(1u, 1u)) return 0u;
    s_register_debug_hold = 0u;
    return 1u;
}
#endif

#if BMS_SERVICE_DIAG_ENABLE
uint8_t bms_afe_shutdown_held(void)
{
    return s_guard.test_shutdown_hold;
}
#endif
