/*
 * 文件功能：DVC1124 测量发布与保护/MOS 适配；D014 使用 SH backend，
 * 不能据此推断 D014 硬件行为。
 * bms/afe/dvc1124/dvc1124_bms.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_diag.h"
#include "dvc1124.h"
#include "bms_afe_driver.h"

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_features.h"
#include "bms_afe_hw_profile.h"
#include "param.h"
#include <string.h>

/* 当前项目每 200 ms 调度一次 dvc1124_backend_sample()。 */
#define DVC_BMS_SAMPLE_PERIOD_MS 200u

/*
 * D008 恢复状态在 AFE 重初始化后保留，MCU 复位后丢失。
 * PB1 仅在有效样本且 DSGF=0 时有意义；连续高电平确认 200 ms，排除单次采样毛刺，
 * SDK 32K 时间差可安全跨回绕。
 */
#define DVC_OCC_RECOVERY_TICKS (30u * 32000u)
#define DVC_LOAD_REMOVED_TICKS (200u * 32u)
#define DVC_OCC_ALARMS (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK)
#define DVC_DSG_ALARMS (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK | DVC1124_ALARM_SCD_MASK)
static struct {
    uint32_t charge_started;
    uint32_t removed_started;
    uint32_t last_sample;
    uint8_t sample_seen;
    uint8_t charge;
    uint8_t discharge;
    uint8_t removed_pending;
    uint8_t hw_pending;
} s_current_recovery;

/* 查询是否仍有电流保护等待物理条件恢复。 */
uint8_t bms_afe_current_recovery_pending(void)
{
    return (s_current_recovery.charge || s_current_recovery.discharge) ? 1u : 0u;
}

/* 读取已配置 NTC 通道的温度。 */
static uint16_t dvc_get_configured_temperature(uint8_t gp)
{
    if ((gp == 0u) || (gp > 4u)) return 0u;
    return g_stCellInfoReport.u16Temperature[gp - 1u];
}

/* 检查已配置 NTC 通道的测量有效性。 */
static uint8_t dvc_configured_ntc_valid(const dvc1124_snapshot_t *snapshot,
                                        uint8_t gp)
{
    uint8_t index;

    /*
     * 温度换算/上报目前实现 GP1..GP4。不得从温度编码本身判断有效性：
     * 旧 (degC+40)*10 编码中 0 是 -40.0 ℃ 的有效值；
     * 应使用电阻对应的 NTC 测量有效性。
     */
    if ((snapshot == 0) || (gp == 0u) || (gp > 4u)) return 0u;
    index = (uint8_t)(gp - 1u);
    return (snapshot->ntc_res_ohm[index] != 0u) ? 1u : 0u;
}

/* 计算有效电池温度通道的最低与最高温度。 */
static uint8_t dvc_get_battery_temperature_range(const dvc1124_snapshot_t *snapshot,
                                                  uint16_t *min_temp,
                                                  uint16_t *max_temp)
{
    uint16_t t1;
    uint16_t t2;

    if ((snapshot == 0) || (min_temp == 0) || (max_temp == 0)) return 0u;
    if (!dvc_configured_ntc_valid(snapshot, DVC1124_DEFAULT_BATTERY_NTC_GP) ||
        !dvc_configured_ntc_valid(snapshot, DVC1124_DEFAULT_BATTERY_NTC2_GP))
    {
        *min_temp = 0u;
        *max_temp = 0u;
        return 0u;
    }

    t1 = dvc_get_configured_temperature(DVC1124_DEFAULT_BATTERY_NTC_GP);
    t2 = dvc_get_configured_temperature(DVC1124_DEFAULT_BATTERY_NTC2_GP);
    *min_temp = (t1 <= t2) ? t1 : t2;
    *max_temp = (t1 >= t2) ? t1 : t2;
    return 1u;
}

/* 将配置通道温度发布到公共报告。 */
static void dvc_publish_temperature_report(const bms_sw_protection_inputs_t *sw)
{
    if (sw == 0) return;

    /*
     * D008 温度由 DVC1124 GP 测量/换算统一产生。GP2/GP3 为电池温度，
     * GP4 为功率 MOS 温度；旧报告槽位使用相同工程值，避免 Modbus/上位机再次查表。
     * ENV_TEMP3 镜像电池最高温度，MOS_TEMP1 对应 GP4。
     */
    g_stCellInfoReport.u16Temperature[ENV_TEMP3] =
        sw->battery_temp_valid ? sw->battery_temp_max : 0u;
    g_stCellInfoReport.u16Temperature[MOS_TEMP1] =
        sw->mos_temp_valid ? sw->mos_temp : 0u;

    if (sw->battery_temp_valid)
    {
        g_stCellInfoReport.u16TempMin = sw->battery_temp_min;
        g_stCellInfoReport.u16TempMax = sw->battery_temp_max;
    }
    else
    {
        g_stCellInfoReport.u16TempMin = 0u;
        g_stCellInfoReport.u16TempMax = 0u;
    }
}

#if DVC1124_HW_PROTECT_ENABLE
/* 确认保护解除条件连续稳定达到所需时间。 */
static uint8_t dvc_recovery_stable(uint8_t condition, uint16_t stable_ms, uint16_t *count)
{
    uint16_t required;
    if (count == 0) return 0u;
    if (!condition) { *count = 0u; return 0u; }
    required = (uint16_t)(((uint32_t)stable_ms + DVC_BMS_SAMPLE_PERIOD_MS - 1u) /
                          DVC_BMS_SAMPLE_PERIOD_MS);
    if (required == 0u) required = 1u;
    if (*count < required) ++(*count);
    return (*count >= required) ? 1u : 0u;
}

/* 清除已满足恢复条件的硬件保护锁存。 */
static uint8_t dvc_clear_recovered_hw_latches(uint8_t alarm,
                                             uint8_t sample_valid,
                                             uint32_t sample_tick_32k)
{
    static uint16_t cov_count;
    static uint16_t cuv_count;
    static uint32_t last_sample_tick;
    static uint8_t sample_seen;
    bms_afe_hw_profile_t hw;
    uint8_t clear_mask = 0u;
    uint8_t verify;

    /* 连续恢复证据不得跨越无效采样、AFE 重初始化或长时间未采样的休眠/调度间隔。 */
    if (!sample_valid || !bms_afe_hw_profile_get(&hw)) {
        cov_count = 0u;
        cuv_count = 0u;
        sample_seen = 0u;
        return alarm;
    }
    if (sample_seen && (uint32_t)(sample_tick_32k - last_sample_tick) >
        2u * DVC_BMS_SAMPLE_PERIOD_MS * 32u) {
        cov_count = 0u;
        cuv_count = 0u;
    }
    sample_seen = 1u;
    last_sample_tick = sample_tick_32k;

    if (alarm & DVC1124_ALARM_COV_MASK) {
        if (dvc_recovery_stable((uint8_t)(g_stCellInfoReport.u16VCellMax <= hw.cov_recover_mv),
                                hw.cov_recover_ms, &cov_count))
            clear_mask |= DVC1124_ALARM_COV_MASK;
    } else cov_count = 0u;

    if (alarm & DVC1124_ALARM_CUV_MASK) {
        if (dvc_recovery_stable((uint8_t)(g_stCellInfoReport.u16VCellMin >= hw.cuv_recover_mv),
                                hw.cuv_recover_ms, &cuv_count))
            clear_mask |= DVC1124_ALARM_CUV_MASK;
    } else cuv_count = 0u;

    if (clear_mask == 0u) return alarm;
    if (!DVC1124_ClearAlarmFlags(clear_mask)) return alarm;
    if (!DVC1124_ReadRegisters(DVC1124_REG_ALARM, &verify, 1u)) return alarm;
    return verify;
}

/* 将 DVC 硬件故障合并到公共保护状态。 */
static void dvc_merge_hw_faults(uint8_t alarm)
{
    bms_fault_reg_t *f = &g_stCellInfoReport.unMdlFault_Third;

    if (alarm & DVC1124_ALARM_COV_MASK) f->bits.b1CellOvp = 1u;
    if (alarm & DVC1124_ALARM_CUV_MASK) f->bits.b1CellUvp = 1u;
    if (alarm & (DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK))
        f->bits.b1IdischgOcp = 1u;
    if (alarm & (DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK))
        f->bits.b1IchgOcp = 1u;

    /* SCD 保留为硬件/后端锁定，不并入通用软件阈值恢复状态机。 */
    if (alarm & DVC1124_ALARM_SCD_MASK)
    {
        if (!bms_error_get(BMS_ERROR_CBC_DSG))
            bms_error_raise(BMS_ERROR_CBC_DSG);
    }
    else if (bms_error_get(BMS_ERROR_CBC_DSG))
    {
        bms_error_clear(BMS_ERROR_CBC_DSG);
    }
}
#endif

/*
 * 在软件状态机发布自身 Third 位之后、合并硬件故障之前调用；
 * 不得把上一周期合并后的位反馈进来。
 */
static uint8_t dvc_recover_current_faults(const dvc1124_snapshot_t *snapshot,
                                         uint8_t alarm, uint8_t load_removed)
{
    uint32_t now = snapshot->sample_tick_32k;
    uint8_t sw_charge = g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp;
    uint8_t sw_discharge = g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp;
    uint8_t charge_ready = 0u, discharge_ready = 0u;
    uint8_t release_reason = 0u;
    uint8_t before = (uint8_t)(s_current_recovery.charge | (s_current_recovery.discharge << 1));
#if DVC1124_HW_PROTECT_ENABLE
    uint8_t clear_mask, verify;
    s_current_recovery.hw_pending |= (uint8_t)(alarm & (DVC_OCC_ALARMS | DVC_DSG_ALARMS));
#endif
    /* 未经观察的采样或重初始化间隔不能算作连续恢复证据。 */
    if (s_current_recovery.sample_seen &&
        (uint32_t)(now - s_current_recovery.last_sample) > 2u * DVC_BMS_SAMPLE_PERIOD_MS * 32u)
        s_current_recovery.removed_pending = 0u;
    s_current_recovery.sample_seen = 1u;
    s_current_recovery.last_sample = now;
    if (!s_current_recovery.charge && (sw_charge || (alarm & DVC_OCC_ALARMS))) {
        s_current_recovery.charge = 1u;
        s_current_recovery.charge_started = now;
    }
    if (sw_discharge || (alarm & DVC_DSG_ALARMS))
        s_current_recovery.discharge = 1u;

    /* 仍然有效的软件阈值故障独立阻断输出。 */
    if (s_current_recovery.charge && !sw_charge &&
        (uint32_t)(now - s_current_recovery.charge_started) >= DVC_OCC_RECOVERY_TICKS)
        charge_ready = 1u;
    /*
     * 负电流表示充电；既有 +/-200 mA 不可信区间不能证明充电。
     * AUTO_DIODE 允许放电故障锁存期间反向充电；DSGF=1 时忽略单独 PB1 证据。
     */
    if (snapshot->current_ma < -(int32_t)BMS_CURRENT_UNRELIABLE_MAX_MA)
        release_reason = 2u;
    else if (load_removed && !(snapshot->status & DVC1124_CC2_DSGF_MASK))
        release_reason = 1u;
    if (s_current_recovery.discharge && release_reason) {
        if (s_current_recovery.removed_pending != release_reason) {
            s_current_recovery.removed_pending = release_reason;
            s_current_recovery.removed_started = now;
        } else if (!sw_discharge &&
                   (uint32_t)(now - s_current_recovery.removed_started) >= DVC_LOAD_REMOVED_TICKS) {
            discharge_ready = 1u;
        }
    } else s_current_recovery.removed_pending = 0u;

#if DVC1124_HW_PROTECT_ENABLE
    clear_mask = (uint8_t)(s_current_recovery.hw_pending &
                 ((charge_ready ? DVC_OCC_ALARMS : 0u) |
                  (discharge_ready ? DVC_DSG_ALARMS : 0u)));
    if (clear_mask) {
        /*
         * 只有 W1C 和回读均成功才解除软件锁存，
         * 即使 AFE 重初始化已清除其易失告警寄存器也必须如此。
         */
        if (!DVC1124_ClearAlarmFlags(clear_mask) ||
            !DVC1124_ReadRegisters(DVC1124_REG_ALARM, &verify, 1u)) {
            charge_ready = discharge_ready = 0u;
        } else {
            alarm = verify;
            s_current_recovery.hw_pending &= (uint8_t)~(clear_mask & (uint8_t)~verify);
            if (verify & DVC_OCC_ALARMS) charge_ready = 0u;
            if (verify & DVC_DSG_ALARMS) discharge_ready = 0u;
            /* 保留回读期间新出现的其它电流故障。 */
            s_current_recovery.hw_pending |= (uint8_t)(verify & (DVC_OCC_ALARMS | DVC_DSG_ALARMS));
            if (verify & DVC_DSG_ALARMS) s_current_recovery.discharge = 1u;
            if ((verify & DVC_OCC_ALARMS) && !s_current_recovery.charge) {
                s_current_recovery.charge = 1u;
                s_current_recovery.charge_started = now;
            }
        }
    }
#endif
    if (charge_ready) s_current_recovery.charge = 0u;
    if (discharge_ready) {
        s_current_recovery.discharge = 0u;
        s_current_recovery.removed_pending = 0u;
    }
    if (before != (uint8_t)(s_current_recovery.charge | (s_current_recovery.discharge << 1)))
        bms_diag_trace(DIAG_EV_CURRENT_RECOVERY,
            (uint32_t)(s_current_recovery.charge | (s_current_recovery.discharge << 1)),
            (uint32_t)alarm | ((uint32_t)load_removed << 8) | ((uint32_t)snapshot->status << 16) | ((uint32_t)release_reason << 24));
    if (s_current_recovery.charge) g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp = 1u;
    if (s_current_recovery.discharge) g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp = 1u;
    return (uint8_t)(alarm | s_current_recovery.hw_pending);
}

/* 汇总当前充电方向的保护阻断条件。 */
static uint8_t dvc_charge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;

    return (f->b1CellOvp || f->b1BatOvp || f->b1IchgOcp ||
            f->b1CellChgOtp || f->b1CellChgUtp || f->b1TmosOtp ||
            bms_features_charge_direction_blocked() ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}

/* 汇总当前放电方向的保护阻断条件。 */
static uint8_t dvc_discharge_blocked(void)
{
    const struct MDLCHGFAULT_BITS *f = &g_stCellInfoReport.unMdlFault_Third.bits;

    return (f->b1CellUvp || f->b1BatUvp || f->b1IdischgOcp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp || f->b1TmosOtp ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK)) ? 1u : 0u;
}

/* 按 NTC 电阻反算遗留接口所需的 ADC 毫伏值。 */
static uint16_t dvc_legacy_adc_mv(uint32_t resistance_ohm)
{
    uint32_t mv;

    if (resistance_ohm == 0u) return 0u;
    mv = (3300u * resistance_ohm) / (resistance_ohm + 10000u);
    return (uint16_t)((mv > 3299u) ? 3299u : mv);
}

/* 将电阻转换为遗留接口的 100 Ω 单位。 */
static uint32_t dvc_legacy_resistance_100ohm(uint16_t adc_mv)
{
    if (adc_mv >= 3300u) adc_mv = 3299u;
    return (100u * adc_mv) / (3300u - adc_mv);
}

/*
 * R81 CHGC/DSGC 是模式命令，CHGF/DSGF 是驱动输出状态。AUTO_DIODE 命令可仍为 10b，
 * 而 DVC 在反向电流超过 BDPT 后自主恢复驱动。不得每 200 ms 重写相同 R81 模式，
 * 否则会扰动自主状态，即使 BMS 请求未变。
 */
static uint8_t dvc_set_fet_modes_if_changed(dvc1124_fet_drive_t charge_mode,
                                            dvc1124_fet_drive_t discharge_mode)
{
    uint8_t current;
    uint8_t target;
    uint8_t ok;

    if (!DVC1124_ReadRegisters(DVC1124_REG_FET_CTRL, &current, 1u)) {
        bms_diag_command(0u, 0u); return 0u;
    }
    bms_diag_command(current, 1u);

    if ((DVC1124_FIELD_GET(DVC1124_FET_CHGC_MASK,
                           DVC1124_FET_CHGC_SHIFT,
                           current) == (uint8_t)charge_mode) &&
        (DVC1124_FIELD_GET(DVC1124_FET_DSGC_MASK,
                           DVC1124_FET_DSGC_SHIFT,
                           current) == (uint8_t)discharge_mode))
    {
        return 1u;
    }

    target = (uint8_t)(current &
                       (uint8_t)~(DVC1124_FET_CHGC_MASK |
                                  DVC1124_FET_DSGC_MASK));
    target |= DVC1124_FIELD_PREP(DVC1124_FET_CHGC_MASK,
                                  DVC1124_FET_CHGC_SHIFT,
                                  charge_mode);
    target |= DVC1124_FIELD_PREP(DVC1124_FET_DSGC_MASK,
                                  DVC1124_FET_DSGC_SHIFT,
                                  discharge_mode);

    /*
     * 只进行一次组合 R81 写入，不在 AUTO_DIODE 前先切换硬关闭。
     * DVC1124_WriteRegisterSafe() 保留其它字段并校验手册规定的可写位。
     */
    ok = DVC1124_WriteRegisterSafe(DVC1124_REG_FET_CTRL, target);
    bms_diag_command(target, ok);
    return ok;
}

/* 按共口保护与方向条件应用充放电 FET 状态。 */
static uint8_t dvc_apply_common_port_fet_state(uint8_t charge_on,
                                              uint8_t discharge_on)
{
    uint8_t charge_blocked = dvc_charge_blocked();
    uint8_t discharge_blocked = dvc_discharge_blocked();
    dvc1124_fet_drive_t charge_mode = DVC1124_FET_DRIVE_OFF;
    dvc1124_fet_drive_t discharge_mode = DVC1124_FET_DRIVE_OFF;

    /*
     * D008 正常请求双 FET 开启。只有一个方向受保护时，将受阻 FET 直接置 AUTO_DIODE，
     * 另一 FET 保持开启；反向电流恢复由 DVC 硬件处理，
     * MCU 不轮询方向或反复切换受保护FET。
     */
    if (charge_on && discharge_on)
    {
        if (charge_blocked && !discharge_blocked)
        {
            charge_mode = DVC1124_FET_DRIVE_AUTO_DIODE;
            discharge_mode = DVC1124_FET_DRIVE_ON;
        }
        else if (discharge_blocked && !charge_blocked)
        {
            charge_mode = DVC1124_FET_DRIVE_ON;
            discharge_mode = DVC1124_FET_DRIVE_AUTO_DIODE;
        }
        else if (!charge_blocked && !discharge_blocked)
        {
            charge_mode = DVC1124_FET_DRIVE_ON;
            discharge_mode = DVC1124_FET_DRIVE_ON;
        }
        /* 双方向受阻时仍必须硬关闭两个 FET。 */
    }
    else
    {
        /*
         * 显式关闭、AFE 通信禁止、休眠和断线路径保持硬关闭，
         * 绝不能提升为 AUTO_DIODE。
         */
        if (charge_on && !charge_blocked)
            charge_mode = DVC1124_FET_DRIVE_ON;
        if (discharge_on && !discharge_blocked)
            discharge_mode = DVC1124_FET_DRIVE_ON;
    }

    return dvc_set_fet_modes_if_changed(charge_mode, discharge_mode);
}

/* 采样后发布测量、合并保护并更新 DVC FET 仲裁。 */
void DVC1124_BmsApp_AFEGet(void)
{
    dvc1124_snapshot_t snapshot;
    bms_sw_protection_inputs_t sw;
    uint8_t alarm = 0u;

    uint16_t diag_c = 0u, diag_d = 0u;

    DVC1124_App_AFEGet();
    DVC1124_GetSnapshot(&snapshot);
    if (!snapshot.valid) {
        s_current_recovery.removed_pending = 0u;
#if DVC1124_HW_PROTECT_ENABLE
        (void)dvc_clear_recovered_hw_latches(0u, 0u, 0u);
#endif
        bms_diag_driver(0u, 0u); return;
    }

    memset(&sw, 0, sizeof(sw));
    sw.battery_temp_valid = dvc_get_battery_temperature_range(
        &snapshot, &sw.battery_temp_min, &sw.battery_temp_max);
    sw.mos_temp_required = 1u;
    sw.mos_temp_valid = dvc_configured_ntc_valid(
        &snapshot, DVC1124_DEFAULT_MOS_NTC_GP);
    sw.mos_temp = sw.mos_temp_valid ?
        dvc_get_configured_temperature(DVC1124_DEFAULT_MOS_NTC_GP) : 0u;

    /* 所有保护隔离模式都保持测量与上报活动。 */
    dvc_publish_temperature_report(&sw);

    bms_sw_protection_update_groups(&sw, DVC1124_SW_PROTECT_ENABLE,
                                    DVC1124_SW_TEMP_PROTECT_ENABLE);
    if (bms_sw_protection_charge_blocked()) diag_c |= DIAG_BLOCK_SW;
    if (bms_sw_protection_discharge_blocked()) diag_d |= DIAG_BLOCK_SW;

#if DVC1124_HW_PROTECT_ENABLE
    alarm = dvc_clear_recovered_hw_latches(snapshot.alarm, 1u,
                                          snapshot.sample_tick_32k);
#endif
    alarm = dvc_recover_current_faults(&snapshot, alarm,
                                      (uint8_t)(gpio_read(CHG_IN_PIN) != 0u));
#if DVC1124_HW_PROTECT_ENABLE
    dvc_merge_hw_faults(alarm);
    if (alarm & (DVC1124_ALARM_COV_MASK | DVC1124_ALARM_OCC1_MASK | DVC1124_ALARM_OCC2_MASK)) diag_c |= DIAG_BLOCK_HW;
    if (alarm & (DVC1124_ALARM_CUV_MASK | DVC1124_ALARM_OCD1_MASK | DVC1124_ALARM_OCD2_MASK | DVC1124_ALARM_SCD_MASK)) diag_d |= DIAG_BLOCK_HW;
#else
    /*
     * 关闭硬件保护的台架模式不使用 SCD/COV/CUV/OC 标志作为保护输入；
     * 底层驱动也关闭相应硬件使能。
     */
    bms_error_clear(BMS_ERROR_CBC_DSG);
#endif

    if (bms_error_get(BMS_ERROR_TEMP_BREAK)) { diag_c |= DIAG_BLOCK_TEMP; diag_d |= DIAG_BLOCK_TEMP; }
    if (dvc_charge_blocked() && !diag_c) diag_c |= DIAG_BLOCK_BACKEND;
    if (dvc_discharge_blocked() && !diag_d) diag_d |= DIAG_BLOCK_BACKEND;
    bms_diag_backend(diag_c, diag_d);
    bms_sw_protection_record_fault_edges();

    /*
     * 均衡授权由 bms_features 拥有。公共策略先校验电压、断线、加热和充电会话条件，
     * 后端设置函数才执行 DVC 60 秒有效期续期。
     */

    /*
     * 本次采样后由 bms_afe_guard 立即仲裁 FET，保留通信与断线硬阻断语义；
     * 本函数仅更新故障状态。
     */
}

/* 设置后端充放电 MOS 请求并进行保护仲裁。 */
uint8_t dvc1124_backend_set_fets(uint8_t charge_on, uint8_t discharge_on)
{
    if (dvc_apply_common_port_fet_state(charge_on, discharge_on)) return 1u;

    bms_error_raise(BMS_ERROR_AFE1);
    return 0u;
}

/* 设置后端输出授权，禁止绕过公共安全门禁。 */
void dvc1124_backend_set_output_enabled(uint8_t enabled)
{
    DVC1124_SetOutputEnabled(enabled);
}

/* 取得后端辅助测量与有效性。 */
uint8_t dvc1124_backend_get_aux_measurements(bms_afe_aux_measurements_t *measurements)
{
    dvc1124_snapshot_t snapshot;
    uint32_t pack_adc_mv;

    if (measurements == NULL) return 0u;
    memset(measurements, 0, sizeof(*measurements));

    DVC1124_GetSnapshot(&snapshot);
    if (!snapshot.valid) return 0u;

    /*
     * 仅兼容旧诊断表示。电池主通道为 GP2，保护使用 GP2/GP3，
     * MOS 诊断为真实功率 MOS 的 GP4。
     */
    measurements->battery_ntc_mv =
        dvc_legacy_adc_mv(snapshot.ntc_res_ohm[DVC1124_DEFAULT_BATTERY_NTC_GP - 1u]);
    measurements->battery_ntc_100ohm =
        dvc_legacy_resistance_100ohm(measurements->battery_ntc_mv);
    measurements->mos_ntc_mv =
        dvc_legacy_adc_mv(snapshot.ntc_res_ohm[DVC1124_DEFAULT_MOS_NTC_GP - 1u]);
    measurements->mos_ntc_100ohm =
        dvc_legacy_resistance_100ohm(measurements->mos_ntc_mv);

    /* 仅兼容旧诊断表示，此处不使用 MCU ADC。 */
    pack_adc_mv = (snapshot.vtop_mv * 15u) / 485u;
    if (pack_adc_mv > 3299u) pack_adc_mv = 3299u;
    measurements->pack_voltage_mv = (pack_adc_mv * 485u) / 15u;
    measurements->raw_current_ma = snapshot.raw_current_ma;
    measurements->current_ma = snapshot.current_ma;
    measurements->sample_tick_32k = snapshot.sample_tick_32k;
    return 1u;
}
