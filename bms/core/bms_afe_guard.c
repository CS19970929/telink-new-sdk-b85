/*
 * 文件功能：AFE 通信安全门禁；管理输出授权、失联隔离、硬件 watchdog 静默窗口和恢复样
 * 本资格。
 * bms/core/bms_afe_guard.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_afe_driver.h"
#include "bms_diag.h"
#include "bms_features.h"
#include "bms_error.h"
#include <string.h>

#define BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT 3u
#define BMS_AFE_COMM_FAILS_BEFORE_SILENCE    2u

/*
 * 失联安全分两层：MCU 立即撤销输出授权，
 * 在总线可能仍可用时尽力关闭一次；重复失败后停止全部 AFE 总线流量，
 * 让 AFE 自身硬件看门狗到期并最终关 MOS。DVC 量产 I2C 看门狗为 4 秒，
 * 等待 5 秒才探测恢复一次，避免失败重试持续喂狗；SH 量产约 32 秒，此处等待 35 秒。
 */
#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
#define BMS_AFE_FAILSAFE_WAIT_MS 5000u
#else
#define BMS_AFE_FAILSAFE_WAIT_MS 35000u
#endif

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
    uint32_t failsafe_start_tick_32k;
} bms_afe_guard_state_t;

/* 输出授权与通信恢复的唯一状态；主循环更新，backend 不得绕过 guard 清除 inhibit。 */
static bms_afe_guard_state_t s_guard;

/* 公共三帧资格只有一个状态所有者，后端 FET 调用也遵守。 */
uint8_t bms_afe_samples_qualified(void)
{
    return (!s_guard.comm_inhibit && !s_guard.config_inhibit && !s_guard.bus_silenced &&
            !s_guard.test_shutdown_hold &&
            s_guard.valid_snapshot_streak >= BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT) ? 1u : 0u;
}

/* 判断当前 guard 阶段是否允许访问 AFE 总线。 */
uint8_t bms_afe_bus_access_allowed(void)
{
    return (s_guard.bus_silenced || s_guard.test_shutdown_hold) ? 0u : 1u;
}

#if (BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124)
#define AFE_INIT() dvc1124_backend_init()
#define AFE_SAMPLE() dvc1124_backend_sample()
#define AFE_PENDING() dvc1124_backend_sample_pending()
#define AFE_SLEEP() dvc1124_backend_sleep()
#define AFE_TEST_SHUTDOWN() dvc1124_backend_enter_shutdown()
#define AFE_APPLY() dvc1124_backend_apply_protection_config()
#define AFE_FETS(c,d) dvc1124_backend_set_fets((c),(d))
#define AFE_OUTPUT(e) dvc1124_backend_set_output_enabled((e))
#define AFE_AUX(m) dvc1124_backend_get_aux_measurements((m))
#define AFE_FEATURE(s) dvc1124_backend_get_feature_snapshot((s))
#define AFE_CHARGER(p) dvc1124_backend_get_charge_source_present((p))
#define AFE_BAL_SET(m) dvc1124_backend_set_balance_mask((m))
#define AFE_BAL_GET(m) dvc1124_backend_get_balance_mask((m))
#define AFE_OW_START() dvc1124_backend_openwire_start()
#define AFE_OW_STOP() dvc1124_backend_openwire_stop()
#define AFE_OW_POLL(r) dvc1124_backend_openwire_poll((r))
#else
#define AFE_INIT() sh3673510_bms_afe_init()
#define AFE_SAMPLE() sh3673510_bms_afe_sample()
#define AFE_PENDING() sh3673510_bms_afe_sample_pending()
#define AFE_SLEEP() sh3673510_bms_afe_sleep()
#define AFE_APPLY() sh3673510_bms_afe_apply_protection_config()
#define AFE_FETS(c,d) sh3673510_bms_afe_set_fets((c),(d))
#define AFE_OUTPUT(e) sh3673510_bms_afe_set_output_enabled((e))
#define AFE_AUX(m) sh3673510_bms_afe_get_aux_measurements((m))
#define AFE_FEATURE(s) sh3673510_backend_get_feature_snapshot((s))
#define AFE_CHARGER(p) sh3673510_backend_get_charge_source_present((p))
#define AFE_BAL_SET(m) sh3673510_backend_set_balance_mask((m))
#define AFE_BAL_GET(m) sh3673510_backend_get_balance_mask((m))
#define AFE_OW_START() sh3673510_backend_openwire_start()
#define AFE_OW_STOP() sh3673510_backend_openwire_stop()
#define AFE_OW_POLL(r) sh3673510_backend_openwire_poll((r))
#endif

/* 设置本地输出禁止状态，撤销驱动授权。 */
static void inhibit_local(void)
{
    s_guard.comm_fault_latched = 1u;
    s_guard.comm_inhibit = 1u;
    s_guard.valid_snapshot_streak = 0u;
    bms_features_on_afe_invalid();
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
}

/* 尝试关闭 AFE 输出并保留无法确认关断的失败状态。 */
static void best_effort_shutdown(void)
{
    if (s_guard.bus_silenced || s_guard.test_shutdown_hold) return;

    /*
     * 这次软件关闭不是失联安全保证，只是总线可能仍接受命令时的最后尝试。
     * 总线已失效时，以硬件 AFE 看门狗关断为准。
     */
    (void)AFE_BAL_SET(0u);
    (void)AFE_FETS(0u, 0u);
}

/* 进入硬件看门狗故障安全等待阶段并禁止总线访问。 */
static void enter_failsafe_wait(void)
{
    inhibit_local();
    s_guard.bus_silenced = 1u;
    s_guard.failsafe_start_tick_32k = bms_diag_tick();
    s_guard.comm_failures = 0u;
}

/* 硬件 watchdog 等待阶段禁止 AFE 总线访问；到期仅探测一次，恢复仍需新样本资格。 */
static uint8_t service_failsafe_wait(void)
{
    if (!s_guard.bus_silenced && !s_guard.test_shutdown_hold) return 0u;

    /* 硬件看门狗计时期间绝不进行 AFE I2C/SPI 访问。 */
    if ((uint32_t)(bms_diag_tick() - s_guard.failsafe_start_tick_32k) <
        BMS_AFE_FAILSAFE_WAIT_MS * 32u)
    {
        if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
        return 1u;
    }

    /* 看门狗窗口结束后只作一次有界恢复尝试。 */
    s_guard.bus_silenced = 0u;
    s_guard.comm_failures = 0u;
    s_guard.valid_snapshot_streak = 0u;
    s_guard.comm_inhibit = 1u;

    AFE_INIT();
    AFE_OUTPUT(s_guard.output_enabled);

    /* 后端初始化可清自身通信错误，但尚未完成资格确认，因此系统 AFE 故障仍保持置位。 */
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    return 1u;
}

/* 合并调用方 MOS 意图与 guard 授权；应用侧请求并不等同于真实 Gate 已导通。 */
static uint8_t apply_requested(void)
{
    uint8_t c;
    uint8_t d;

    /* 通信禁止期间 MCU 只管理授权，不能反复写关闭命令而意外喂 AFE WDT。 */
    if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 1u;

    c = s_guard.requested_charge_on;
    d = s_guard.requested_discharge_on;
    if (!s_guard.output_enabled)
    {
        c = 0u;
        d = 0u;
    }
    if (bms_features_outputs_blocked()) { c = 0u; d = 0u; }
    /* 方向阻断由选定后端处理，保留同口放电及 DVC AUTO_DIODE 语义。 */
    return AFE_FETS(c, d);
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
    AFE_INIT();
    /* 同进程重初始化不能仅靠 RAM 清零解除未验证配置；完整应用成功才解除。 */
    if (config_inhibit && AFE_APPLY()) s_guard.config_inhibit = 0u;
    AFE_OUTPUT(0u);
    (void)AFE_FETS(0u, 0u);
    bms_features_init();
    (void)AFE_BAL_SET(0u);
    s_guard.comm_fault_latched = bms_error_get(BMS_ERROR_AFE1) ? 1u : 0u;
}

/* 通过安全门禁采样并更新恢复资格及输出授权。 */
void bms_afe_sample(void)
{
    bms_afe_aux_measurements_t m;

    if (s_guard.test_shutdown_hold || service_failsafe_wait()) return;

    /* 新样本确认恢复资格期间，保留真正已发生的通信故障。 */
    if (bms_error_get(BMS_ERROR_AFE1)) s_guard.comm_fault_latched = 1u;
    AFE_SAMPLE();
    memset(&m, 0, sizeof(m));
    if (!AFE_AUX(&m))
    {
        if (AFE_PENDING()) return;
        note_invalid();
        return;
    }

    s_guard.comm_failures = 0u;
    if (!AFE_PENDING())
    {
        if (s_guard.valid_snapshot_streak < BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT)
            ++s_guard.valid_snapshot_streak;
    }

    if (!s_guard.config_inhibit &&
        s_guard.valid_snapshot_streak >= BMS_AFE_VALID_SNAPSHOT_RELEASE_COUNT)
    {
        s_guard.comm_inhibit = 0u;
        s_guard.comm_fault_latched = 0u;
    }
    else if (s_guard.comm_fault_latched)
    {
        /* 健康启动仅禁止输出，不凭空产生 AFE1 故障。 */
        if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    }

    /*
     * 资格尚未确认不是采样失败。
     * comm_inhibit 时调用功能服务会令快照读取失败并锁存虚假断线疑似，
     * 导致健康启动后两个 FET 长期关闭。
     */
    /* 缓存等待不能消费“最后诊断帧”标志，或推进空闲/均衡确认计数。 */
    if (!s_guard.comm_inhibit && !AFE_PENDING()) bms_features_service();
    if (!apply_requested()) note_invalid();
}

/* 通过 guard 执行选定 AFE 的休眠流程。 */
uint8_t bms_afe_sleep(void)
{
    if (s_guard.bus_silenced || s_guard.test_shutdown_hold) return 0u;
    inhibit_local();
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    best_effort_shutdown();
#endif
    /* 本地禁止会主动置 AFE1；此处依据实际命令应答。 */
    if (!AFE_SLEEP()) { note_invalid(); return 0u; }
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
    ok = AFE_APPLY();
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
    if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 1u;

    if (!apply_requested())
    {
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
        AFE_OUTPUT(s_guard.output_enabled);

    if (!s_guard.output_enabled)
    {
        bms_features_on_afe_invalid();
        if (!s_guard.bus_silenced && !s_guard.test_shutdown_hold) best_effort_shutdown();
        return;
    }

    if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return;
    if (!apply_requested()) note_invalid();
}

/* 取得选定后端的辅助测量快照。 */
uint8_t bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m)
{
    if (!m) return 0u;
    if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold)
    {
        memset(m, 0, sizeof(*m));
        return 0u;
    }
    if (!AFE_AUX(m))
    {
        memset(m, 0, sizeof(*m));
        return 0u;
    }
    return 1u;
}

/* 取得公共功能所需的后端采样快照。 */
uint8_t bms_afe_get_feature_snapshot(bms_afe_feature_snapshot_t *s)
{
    if (!s || s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 0u;
    return AFE_FEATURE(s);
}

/* 取得后端可验证的充电源存在状态。 */
uint8_t bms_afe_get_charge_source_present(uint8_t *p)
{
    if (!p || s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 0u;
    return AFE_CHARGER(p);
}

/* 门禁允许时更新有效通道均衡请求。 */
uint8_t bms_afe_set_balance_mask(uint32_t m)
{
    if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold)
        return (m == 0u) ? 1u : 0u;
    return AFE_BAL_SET(m);
}

/* 读取当前后端均衡状态掩码。 */
uint8_t bms_afe_get_balance_mask(uint32_t *m)
{
    if (!m) return 0u;
    if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold)
    {
        *m = 0u;
        return 0u;
    }
    return AFE_BAL_GET(m);
}

/* 门禁允许时启动后端断线检测。 */
uint8_t bms_afe_openwire_start(void)
{
    if (s_guard.comm_inhibit || s_guard.config_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold) return 0u;
    if (!AFE_BAL_SET(0u)) return 0u;
    return AFE_OW_START();
}

uint8_t bms_afe_openwire_stop(void)
{
    /* 失联后的清理也必须尊重 watchdog 总线静默，不能用重试续命。 */
    if (s_guard.bus_silenced || s_guard.test_shutdown_hold) return 0u;
    if (AFE_OW_STOP()) return 1u;
    /* 清理 I/O 失败属于真实 AFE 通信失败，继续使用既有恢复资格。 */
    note_invalid();
    return 0u;
}

/* 通过 guard 推进后端断线检测阶段。 */
bms_afe_diag_state_t bms_afe_openwire_poll(bms_afe_openwire_result_t *r)
{
    if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold)
        return BMS_AFE_DIAG_ERROR;
    return AFE_OW_POLL(r);
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
/* 撤销输出授权并执行后端 shutdown。 */
uint8_t bms_afe_enter_shutdown(void)
{
    if (s_guard.comm_inhibit || s_guard.bus_silenced || s_guard.test_shutdown_hold)
        return 0u;

    inhibit_local();
    /*
     * 有意关机不同于失联尽力关闭，必须传播每个关闭/回读失败；
     * 准备失败不能切 MCU 电源。
     */
    if (!AFE_BAL_SET(0u) || !AFE_FETS(0u, 0u))
    {
        note_invalid();
        return 0u;
    }
    if (!AFE_TEST_SHUTDOWN())
    {
        note_invalid();
        return 0u;
    }

    s_guard.test_shutdown_hold = 1u;
    s_guard.comm_failures = 0u;
    return 1u;
}

#if defined(BMS_HOST_TEST) && BMS_HOST_TEST
/* 测试构建中发起 AFE shutdown 并记录结果。 */
uint8_t bms_afe_test_enter_shutdown(void)
{
    return bms_afe_enter_shutdown();
}

/* 测试构建中执行 AFE 唤醒并重新获取资格。 */
uint8_t bms_afe_test_wake(void)
{
    if (!s_guard.test_shutdown_hold) return 0u;

    s_guard.test_shutdown_hold = 0u;
    s_guard.comm_inhibit = 1u;
    s_guard.valid_snapshot_streak = 0u;
    s_guard.comm_failures = 0u;
    s_guard.bus_silenced = 0u;
    s_guard.failsafe_start_tick_32k = 0u;

    /*
     * 正常后端初始化负责完整 shutdown 唤醒/复位、持久硬件配置、编译期策略与回读；
     * 公共门禁确认三帧新样本前保持请求阻断。
     */
    AFE_INIT();
    AFE_OUTPUT(s_guard.output_enabled);
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    return 1u;
}
#endif

#endif

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
/* 从选定后端刷新诊断，遵守总线静默门禁。 */
void bms_afe_diag_poll(void)
{
    uint32_t c = bms_features_diag_reasons(1u), d = bms_features_diag_reasons(0u);
    uint32_t common = 0u;
    uint16_t params = bms_diag_cached_word(144u);
    if (!(params & 1u)) common |= DIAG_BLOCK_PARAMS;
    if (!(params & 2u)) common |= DIAG_BLOCK_UPGRADE;
    if (!s_guard.output_enabled) common |= DIAG_BLOCK_OUTPUT;
    if (s_guard.comm_inhibit || s_guard.bus_silenced) common |= DIAG_BLOCK_COMM;
    if (s_guard.test_shutdown_hold) common |= DIAG_BLOCK_SHUTDOWN;
    c |= bms_diag_cached_word(146u); d |= bms_diag_cached_word(147u);
    bms_diag_mos((uint16_t)(s_guard.requested_charge_on | (s_guard.requested_discharge_on << 1)), c | common, d | common);
    if (common & (DIAG_BLOCK_COMM | DIAG_BLOCK_SHUTDOWN)) {
        bms_diag_driver(0u, 0u);
        bms_diag_command((uint8_t)bms_diag_cached_word(130u), 0u);
    }
    bms_diag_mos_capture();
}

#endif
