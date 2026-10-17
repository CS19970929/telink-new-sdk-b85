/*
 * 文件功能：AFE 通信安全门禁；管理输出授权、失联隔离、硬件 watchdog 静默窗口和恢复样
 * 本资格。
 * bms/core/bms_afe_guard.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_afe_driver.h"
#include "bms_diag.h"
#include "bms_features.h"
#include "bms_error.h"
#include "bms_sw_protection.h"
#include "bms_parameters.h"
#include "bms_time.h"
#include <string.h>

#define BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT 3u
#define BMS_AFE_COMM_FAILS_BEFORE_SILENCE    2u

/* MCU 撤销请求后按所选后端的固定 watchdog 窗口保持总线静默。 */

typedef struct
{
    uint8_t requested_charge_on;
    uint8_t requested_discharge_on;
    uint8_t output_enabled;
    uint8_t comm_inhibit;
    uint8_t config_inhibit;
    uint8_t comm_fault_latched;
    uint8_t valid_snapshot_streak;
    uint8_t comm_failures;
    uint8_t bus_silenced;
    uint8_t test_shutdown_hold;
    uint8_t fet_driver_bits;
    uint8_t fet_driver_valid;
    uint32_t failsafe_start_tick_ms;
} bms_afe_guard_state_t;

/* 输出授权与通信恢复的唯一状态；主循环更新，backend 不得绕过 guard 清除 inhibit。 */
static bms_afe_guard_state_t s_guard;
/* 主循环独占；AFE 重初始化不清除，MCU reset 后 BSS 恢复为允许。 */
static uint8_t s_remote_off_mask;

/* 业务期访问统一受通信隔离/总线静默/测试关机门禁约束；
 * 配置禁止由各自写入事务控制，不能在此悄悄改变配置恢复策略。 */
static inline uint8_t runtime_access_allowed(void)
{
    return (!s_guard.comm_inhibit && !s_guard.bus_silenced &&
            !s_guard.test_shutdown_hold) ? 1u : 0u;
}

/* 公共三帧资格只有一个状态所有者，后端 FET 调用也遵守。 */
uint8_t bms_afe_samples_qualified(void)
{
    return (runtime_access_allowed() && !s_guard.config_inhibit &&
            s_guard.valid_snapshot_streak >= BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT) ? 1u : 0u;
}

/* 判断当前 guard 阶段是否允许访问 AFE 总线。 */
uint8_t bms_afe_bus_access_allowed(void)
{
    return (s_guard.bus_silenced || s_guard.test_shutdown_hold) ? 0u : 1u;
}

/* 设置本地输出禁止状态，撤销驱动授权。 */
static void inhibit_local(void)
{
    s_guard.comm_fault_latched = 1u;
    s_guard.comm_inhibit = 1u;
    s_guard.valid_snapshot_streak = 0u;
    bms_afe_publish_fet_status(0u, 0u);
    bms_features_stop();
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
}

/* 仅尽力关闭均衡/MOS，不发送芯片 Shutdown/Powerdown 模式命令。 */
static void best_effort_shutdown(void)
{
    if (s_guard.bus_silenced || s_guard.test_shutdown_hold) return;

    /*
     * 这次软件关闭不是失联安全保证，只是总线可能仍接受命令时的最后尝试。
     * 总线已失效时，以硬件 AFE 看门狗关断为准。
     */
#if BMS_FEATURE_BALANCE_SUPPORTED
    (void)bms_afe_driver_set_balance_mask(0u);
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

/* 硬件 watchdog 等待阶段禁止 AFE 总线访问；到期仅探测一次，恢复仍需新样本资格。 */
static uint8_t service_failsafe_wait(void)
{
    if (!s_guard.bus_silenced && !s_guard.test_shutdown_hold) return 0u;

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
    s_guard.valid_snapshot_streak = 0u;
    s_guard.comm_inhibit = 1u;

    /* SH 原恢复路径重建软件保护滤波，DVC 保持原有恢复语义。 */
    if (bms_afe_driver_policy.reset_protection_before_init)
        bms_sw_protection_init();
    bms_afe_driver_init();
    bms_afe_driver_set_output_enabled(s_guard.output_enabled);

    /* 后端初始化可清自身通信错误，但尚未完成资格确认，因此系统 AFE 故障仍保持置位。 */
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    return 1u;
}

/* 合并调用方 MOS 意图与 guard 授权；应用侧请求并不等同于真实 Gate 已导通。 */
static uint8_t apply_requested(void)
{
    uint8_t c;
    uint8_t d;
    uint8_t direction_allow_mask;

    /* 通信禁止期间 MCU 只管理授权，不能反复写关闭命令而意外喂 AFE WDT。 */
    if (!runtime_access_allowed()) return 1u;

    c = s_guard.requested_charge_on;
    d = s_guard.requested_discharge_on;
    direction_allow_mask = bms_afe_driver_get_direction_allow_mask();
    /*
     * 用户关闭意图始终保留。AFE 确认放电时临时放行充电 MOS，确认充电时
     * 临时放行放电 MOS；方向消失后下一次仲裁自动恢复用户关闭。
     */
    if ((s_remote_off_mask & BMS_AFE_DIRECTION_ALLOW_CHARGE_MASK) &&
        !(direction_allow_mask & BMS_AFE_DIRECTION_ALLOW_CHARGE_MASK)) c = 0u;
    if ((s_remote_off_mask & BMS_AFE_DIRECTION_ALLOW_DISCHARGE_MASK) &&
        !(direction_allow_mask & BMS_AFE_DIRECTION_ALLOW_DISCHARGE_MASK)) d = 0u;
    /* 参数资格直接由输出门禁消费，不借业务功能代为阻断。 */
    if (!s_guard.output_enabled || !bms_protection_params_valid() ||
        !bms_afe_samples_qualified())
    {
        c = 0u;
        d = 0u;
    }
    /* 方向阻断由选定后端处理，保留同口放电及 DVC AUTO_DIODE 语义。 */
    return bms_afe_driver_set_fets(c, d);
}

/* 记录无效样本并更新连续失败与输出隔离状态。 */
static void note_invalid(void)
{
    inhibit_local();

    /*
     * 只作一次尽力受控关断；失效总线上的重复写入既无效，
     * 也可能阻止不稳定 AFE WDT 到期。
     */
    if (s_guard.comm_failures == 0u) best_effort_shutdown();

    if (s_guard.comm_failures != 0xFFu) ++s_guard.comm_failures;
    if (s_guard.comm_failures >= BMS_AFE_COMM_FAILS_BEFORE_SILENCE)
        enter_failsafe_wait();
}

/* 初始化 guard 与选定后端，输出仍受采样资格约束。 */
void bms_afe_init(void)
{
    uint8_t config_inhibit = s_guard.config_inhibit;
    memset(&s_guard, 0, sizeof(s_guard));
    s_guard.comm_inhibit = 1u;
    s_guard.config_inhibit = config_inhibit;
    if (bms_afe_driver_policy.reset_protection_before_init)
        bms_sw_protection_init();
    bms_afe_driver_init();
    /* 同进程重初始化不能仅靠 RAM 清零解除未验证配置；完整应用成功才解除。 */
    if (config_inhibit && bms_afe_driver_apply_protection_config()) s_guard.config_inhibit = 0u;
    bms_afe_driver_set_output_enabled(0u);
    (void)bms_afe_driver_set_fets(0u, 0u);
    if (!bms_afe_driver_policy.reset_protection_before_init)
        bms_sw_protection_init();
    bms_features_init();
#if BMS_FEATURE_BALANCE_SUPPORTED
    (void)bms_afe_driver_set_balance_mask(0u);
#endif
    s_guard.comm_fault_latched = bms_error_get(BMS_ERROR_AFE1) ? 1u : 0u;
}

/* 通过安全门禁采样并更新恢复资格及输出授权。 */
bms_afe_sample_result_t bms_afe_sample(void)
{
    bms_afe_aux_measurements_t m;
    bms_afe_sample_result_t result = { BMS_AFE_SAMPLE_NOT_READY, 0u };

    if (s_guard.test_shutdown_hold || service_failsafe_wait()) return result;
    if (bms_error_get(BMS_ERROR_AFE1)) s_guard.comm_fault_latched = 1u;
    result = bms_afe_driver_sample();
    if (result.status == BMS_AFE_SAMPLE_NOT_READY) {
        /* 启动/重配置等待撤销旧资格，但不记通信失败或重复访问总线。
         * 普通缓存属于 OK，不会经过这里。 */
        s_guard.comm_inhibit = 1u;
        s_guard.valid_snapshot_streak = 0u;
        bms_afe_publish_fet_status(0u, 0u);
        bms_features_stop();
        return result;
    }
    if (result.status == BMS_AFE_SAMPLE_ERROR ||
        !bms_afe_driver_get_aux_measurements(&m)) {
        note_invalid();
        result.status = BMS_AFE_SAMPLE_ERROR;
        result.confirm_sample = 0u;
        return result;
    }

    s_guard.comm_failures = 0u;
    if (result.confirm_sample &&
        s_guard.valid_snapshot_streak < BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT)
        ++s_guard.valid_snapshot_streak;
    if (!s_guard.config_inhibit &&
        s_guard.valid_snapshot_streak >= BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT) {
        s_guard.comm_inhibit = 0u;
        s_guard.comm_fault_latched = 0u;
    } else if (s_guard.comm_fault_latched) {
        if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    }
    return result;
}

void bms_afe_apply_outputs(void)
{
    if (!apply_requested()) note_invalid();
}

/* 产品低功耗模式固定：DVC 仅用 Shutdown，SH 仅用 Sleep。 */
uint8_t bms_afe_sleep(void)
{
    if (!bms_afe_bus_access_allowed() ||
        (bms_afe_driver_policy.sleep_holds_bus && !runtime_access_allowed())) return 0u;
    inhibit_local();
    if (bms_afe_driver_policy.sleep_holds_bus) {
#if BMS_FEATURE_BALANCE_SUPPORTED
        if (!bms_afe_driver_set_balance_mask(0u)) { note_invalid(); return 0u; }
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

    if (s_guard.bus_silenced || s_guard.test_shutdown_hold ||
        (s_guard.comm_inhibit && !s_guard.config_inhibit)) return 0u;
    s_guard.config_inhibit = 1u;
    s_guard.valid_snapshot_streak = 0u;
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

    /* 通信未合格时只缓存产品请求；请求状态变化不能成为访问静默总线的理由。 */
    if (!runtime_access_allowed()) return 1u;

    if (!apply_requested())
    {
        note_invalid();
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
    if (!apply_requested()) {
        /* 开启许可失败时恢复原来的关闭许可；关闭失败仍保留 fail-safe 意图。 */
        if (allowed) {
            s_remote_off_mask = old_mask;
            (void)apply_requested();
        }
        note_invalid();
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
    if (bms_afe_samples_qualified())
        bits |= DIAG_GUARD_SAMPLES_QUALIFIED;
    return bits;
}

/* 设置应用输出授权并重新仲裁 MOS 请求。 */
void bms_afe_set_output_enabled(uint8_t e)
{
    s_guard.output_enabled = e ? 1u : 0u;

    if (!s_guard.bus_silenced && !s_guard.test_shutdown_hold)
        bms_afe_driver_set_output_enabled(s_guard.output_enabled);

    if (!s_guard.output_enabled)
    {
        /* 关闭输出不代表电压测量或采样线故障。 */
        bms_features_stop();
        bms_afe_publish_fet_status(0u, 0u);
        if (!s_guard.bus_silenced && !s_guard.test_shutdown_hold) best_effort_shutdown();
        return;
    }

    if (!runtime_access_allowed()) return;
    if (!apply_requested()) note_invalid();
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
#if BMS_FEATURE_HEATER_SUPPORTED || BMS_FEATURE_BALANCE_SUPPORTED
    if (!s || !runtime_access_allowed()) return 0u;
    return bms_afe_driver_get_feature_snapshot(s);
#else
    if (s) memset(s, 0, sizeof(*s));
    return 0u;
#endif
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
uint8_t bms_afe_set_balance_mask(uint32_t m)
{
#if BMS_FEATURE_BALANCE_SUPPORTED
    /* 即使请求 OFF，总线无法访问也不能声称已经物理确认关闭。 */
    if (!runtime_access_allowed()) return 0u;
    return bms_afe_driver_set_balance_mask(m);
#else
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
    return bms_afe_driver_get_balance_mask(m);
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
    s_guard.valid_snapshot_streak = 0u;
    s_guard.comm_failures = 0u;
    s_guard.bus_silenced = 0u;
    s_guard.failsafe_start_tick_ms = 0u;

    /*
     * 正常后端初始化负责完整 shutdown 唤醒/复位、持久硬件配置、编译期策略与回读；
     * 公共门禁确认三帧新样本前保持请求阻断。
     */
    bms_afe_driver_init();
    bms_afe_driver_set_output_enabled(s_guard.output_enabled);
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    return 1u;
}
#endif

#if BMS_SERVICE_DIAG_ENABLE
uint8_t bms_afe_shutdown_held(void)
{
    return s_guard.test_shutdown_hold;
}
#endif
