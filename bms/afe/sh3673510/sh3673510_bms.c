/*
 * 文件功能：SH3673510 的 BMS 采样、保护状态、MOS 仲裁与恢复；
 * 区分请求、驱动缓存和有效测量。
 * bms/afe/sh3673510/sh3673510_bms.c；实际编译归属见各产品 sources.txt。
 */
/* 现有产品 10K NTC 表：电阻单位 100 Ω，温度编码为 (degC+40)*10。 */
#include "sh3673510_ntc.h"
#include "bms_afe_driver.h"
#include "tl_common.h"
#include "drivers.h"
#include "bms_product.h"
#include "bms_afe_backend.h"
#include "bms_parameters.h"
#include "bms_error.h"
#include "bms_diag.h"
#include "bms_config_store.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_features.h"
#include "bms_afe_hw_profile.h"
#include "sh3673520.h"
#include "sh3673520_reg.h"
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#include <string.h>

#define SH3510_SAMPLE_MS              200u
#define SH3510_SHORT_RELEASE_SAMPLES    10u /* 按 200 ms 节拍连续确认 LOADOFF 2 秒。 */
#define SH3510_OCD_RELEASE_FILTER_10MS  200u /* 连续确认负载移除或充电恢复 2 秒。 */
#define SH3510_MISSING_CELL_MV       61001u /* D000..D01F 未用串位的协议标记。 */
#define SH3510_DETECT_SETTLE_32K      6400u /*
 * 200 ms 大于 tLOAD 65 ms 与 VADC 70 ms。
 */
/*
 * 这是现有板级策略，不能把唤醒比较器规格当作正常模式 ADC 阈值保证；
 * 需要实板移除/重接负载测试。
 */
#define SH3510_CHARGER_ON_MV          2100u
#define SH3510_CHARGER_OFF_MV          900u
#define SH3510_ADC_MAX_AGE_32K       12800u /*
 * 400 ms 允许在 200 ms 轮询下完成 250 ms CADC。
 */
#define SH3510_TEMP_STARTUP_32K      38400u /*
 * 1.2 秒覆盖一次完整的正常模式 0.98 秒扫描。
 */

typedef enum {
    HW_REC_OV = 0, HW_REC_UV, HW_REC_OCD1, HW_REC_OCD2,
    HW_REC_OCC, HW_REC_OTC, HW_REC_OTD, HW_REC_UTC, HW_REC_UTD,
    HW_REC_COUNT
} sh3510_hw_recovery_id_t;

static bms_afe_aux_measurements_t s_aux;
static uint32_t s_ntc_ohm[4];
static uint8_t s_ntc_valid[4];
static uint8_t s_output_enabled;
static uint8_t s_snapshot_valid;
static uint8_t s_hw_afe_error;
static uint8_t s_requested_charge_on;
static uint8_t s_requested_discharge_on;
/* 短路硬件锁定；恢复依赖物理条件与 AFE 状态，不能由关 MOS 后零电流直接清除。 */
static uint8_t s_short_latched;
static uint8_t s_short_clear_pending;
static uint16_t s_short_release_count;
static uint16_t s_hw_recovery_count[HW_REC_COUNT];
static uint8_t s_hw_charge_protect;
static uint8_t s_hw_discharge_protect;
static uint8_t s_afe_reconfigure_required;
static uint8_t s_bstatus2;
static uint8_t s_flag1;
static uint8_t s_flag2;
static int16_t s_mos_ntc_raw;
static uint8_t s_fet_command_valid;
static uint8_t s_last_charge_command;
static uint8_t s_last_discharge_command;
/*
 * 清锁存事务失败后必须完整重初始化，
 * 否则重试间的成功 ADC 读取会持续重置门禁失败计数/喂 WDT。
 */
static uint8_t s_flag_clear_failed;
static uint8_t s_detection_initialized;
static uint8_t s_load_detection;
static uint32_t s_detection_tick;
static uint8_t s_load_removed;
static uint8_t s_charger_removed;
static uint8_t s_charger_known;
static uint8_t s_charger_present;
static uint8_t s_sample_pending;
static uint8_t s_sampling_restart;
static uint8_t s_vadc_seen;
static uint8_t s_cadc_seen;
static uint8_t s_temperature_started;
static uint32_t s_sampling_start_tick;
static uint32_t s_vadc_tick;
static uint32_t s_cadc_tick;


/* 复位转换时间与完成标志，要求重新采集有效 AFE 样本。 */
static void restart_sampling(void)
{
    bms_sw_protection_reset_current_recovery();
    s_sampling_start_tick = pm_get_32k_tick();
    s_vadc_tick = s_cadc_tick = s_sampling_start_tick;
    s_vadc_seen = s_cadc_seen = 0u;
    s_temperature_started = 0u;
    s_snapshot_valid = 0u;
    s_sample_pending = 1u;
    s_sampling_restart = 0u;
}

#if SH3673510_HW_PROTECT_ENABLE
/* 将保护延时换算为所需连续样本数。 */
static uint16_t filter_samples(uint16_t filter_10ms)
{
    uint32_t ms = (uint32_t)filter_10ms * 10u;
    uint32_t n;
    if (ms == 0u) return 1u;
    n = (ms + SH3510_SAMPLE_MS - 1u) / SH3510_SAMPLE_MS;
    if (n == 0u) n = 1u;
    if (n > 65535u) n = 65535u;
    return (uint16_t)n;
}

#endif

/* 将 NTC 原始测量换算为温度编码。 */
static uint16_t ntc_temp(uint32_t ohm)
{
    uint32_t r100 = (ohm + 50u) / 100u;
    if (r100 > 65535u) r100 = 65535u;
    return bms_lookup_u16(sh3673510_ntc_10k,
                          (uint16_t)(sizeof(sh3673510_ntc_10k) / sizeof(sh3673510_ntc_10k[0])),
                          (uint16_t)r100);
}

/* 按 NTC 电阻反算遗留接口所需的 ADC 毫伏值。 */
static uint16_t legacy_adc_mv(uint32_t ohm)
{
    uint32_t mv;
    if (ohm == 0u) return 0u;
    mv = (3300u * ohm) / (ohm + 10000u);
    return (uint16_t)((mv > 3299u) ? 3299u : mv);
}

/* 累计通信失败并更新 AFE 错误状态。 */
static void note_comm_error(void)
{
    sh3673520_comm_stats_t stats;

    bms_sw_protection_reset_current_recovery();
    s_snapshot_valid = 0u;
    s_detection_initialized = 0u;
    s_load_removed = s_charger_removed = s_charger_known = 0u;
    s_vadc_seen = s_cadc_seen = 0u;
    s_sample_pending = 0u;
    /* 恢复要求连续有效样本，不能跨通信间断累计；保留故障锁存，仅作废恢复资格。 */
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    s_fet_command_valid = 0u;
    SH3673520_GetCommStats(&stats);
    if (stats.last_error == SH3673520_ERR_SPI ||
        stats.last_error == SH3673520_ERR_TIMEOUT ||
        stats.last_error == SH3673520_ERR_CRC ||
        stats.last_error == SH3673520_ERR_PROTOCOL) {
        if (!bms_error_get(BMS_ERROR_SPI)) bms_error_raise(BMS_ERROR_SPI);
    }
    if (!bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);
    g_bms_system_status.bits.b1Status_AFE1 = 0u;
}

/* 记录成功通信并恢复连续失败计数。 */
static void note_comm_ok(void)
{
    bms_error_clear(BMS_ERROR_SPI);
    if (!s_hw_afe_error) bms_error_clear(BMS_ERROR_AFE1);
    g_bms_system_status.bits.b1Status_AFE1 = s_hw_afe_error ? 0u : 1u;
}

/* 取得电池温度范围与有效性快照。 */
static uint8_t battery_temperature_snapshot(uint16_t *bat_min,
                                            uint16_t *bat_max)
{
    uint16_t t1;
    uint16_t t2;
    if (bat_min == 0 || bat_max == 0) return 0u;
    if (!s_ntc_valid[SH3673510_BOARD_BAT_NTC1_INDEX] ||
        !s_ntc_valid[SH3673510_BOARD_BAT_NTC2_INDEX]) return 0u;
    t1 = g_stCellInfoReport.u16Temperature[AFE1_TEMP1];
    t2 = g_stCellInfoReport.u16Temperature[AFE1_TEMP2];
    *bat_min = (t1 < t2) ? t1 : t2;
    *bat_max = (t1 > t2) ? t1 : t2;
    return 1u;
}

/* 汇总当前充电方向的保护阻断条件。 */
static uint8_t charge_blocked(void)
{
    return (s_hw_charge_protect ||
            bms_sw_protection_charge_blocked() ||
            bms_features_charge_direction_blocked()) ? 1u : 0u;
}

/* 汇总当前放电方向的保护阻断条件。 */
static uint8_t discharge_blocked(void)
{
    return (s_hw_discharge_protect ||
            bms_sw_protection_discharge_blocked() ||
            s_short_latched ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG)) ? 1u : 0u;
}

/* 检查 AFE 就绪、通信与输出相关状态。 */
static uint8_t sh3510_outputs_healthy(void)
{
    return (s_snapshot_valid && bms_afe_samples_qualified() && !s_hw_afe_error &&
            !bms_error_get(BMS_ERROR_AFE1) && !bms_error_get(BMS_ERROR_SPI)) ? 1u : 0u;
}

/* 合并输出授权和阻断条件后应用 MOS 请求。 */
static uint8_t sh3510_apply_requested_fets(void)
{
    uint8_t charge_on = 0u;
    uint8_t discharge_on = 0u;
    uint8_t charge_inhibit;
    uint8_t discharge_inhibit;

    if (!sh3673510_control_ready()) return 0u;

    charge_inhibit = charge_blocked();
    discharge_inhibit = discharge_blocked();

    if (s_output_enabled && sh3510_outputs_healthy())
    {
        charge_on = s_requested_charge_on ? 1u : 0u;
        discharge_on = s_requested_discharge_on ? 1u : 0u;

        /*
         * 同口反向恢复：充电保护可关闭 CHG，但确认 DSGING 后须允许重新开启 CHG，
         * 避免放电持续流经体二极管；放电保护对确认 CHGING 的情况对称处理。
         * BSTATUS2 来自 AFE 方向检测器，此路径不是盲目周期重试。
         * 双方向均受阻时不强开任一侧。
         */
        if (charge_inhibit)
            charge_on = (!discharge_inhibit && discharge_on &&
                         (s_bstatus2 & SH3673520_BSTATUS2_DSGING_MASK)) ? 1u : 0u;
        if (discharge_inhibit)
            discharge_on = (!charge_inhibit && charge_on &&
                            (s_bstatus2 & SH3673520_BSTATUS2_CHGING_MASK)) ? 1u : 0u;
    }

    /*
     * 不要每 200 ms 重写同一命令。请求未变时硬件保护可以改变实际 FET 输出，
     * 重复软件写入会对抗该硬件行为。
     */
    if (s_fet_command_valid &&
        s_last_charge_command == charge_on &&
        s_last_discharge_command == discharge_on)
        return 1u;

    if (!sh3673510_control_set_fets(charge_on, discharge_on))
    {
        note_comm_error();
        s_fet_command_valid = 0u;
        return 0u;
    }

    s_last_charge_command = charge_on;
    s_last_discharge_command = discharge_on;
    s_fet_command_valid = 1u;
    return 1u;
}

/* 发布硬件保护与 FET 状态诊断。 */
static void publish_hw_status(const sh3673510_control_status_t *s)
{
#if SH3673510_HW_PROTECT_ENABLE
    uint8_t chg_flag1;
    uint8_t dsg_flag1;
    uint8_t chg_flag2;
    uint8_t dsg_flag2;
#endif

    if (s == 0) return;
    s_flag1 = s->flag1;
    s_flag2 = s->flag2;
    s_bstatus2 = s->bstatus2;
    g_bms_system_status.bits.b1Status_MOS_CHG =
        (s->bstatus1 & SH3673520_BSTATUS1_CHG_FET_MASK) ? 1u : 0u;
    g_bms_system_status.bits.b1Status_MOS_DSG =
        (s->bstatus1 & SH3673520_BSTATUS1_DSG_FET_MASK) ? 1u : 0u;
    s_hw_afe_error = (s->bstatus1 & SH3673520_BSTATUS1_E2P_ERR_MASK) ? 1u : 0u;
    if (s_hw_afe_error && !bms_error_get(BMS_ERROR_AFE1)) bms_error_raise(BMS_ERROR_AFE1);

#if SH3673510_HW_PROTECT_ENABLE
    chg_flag1 = (uint8_t)(s->flag1 & (SH3673520_FLAG1_OV_MASK |
                                      SH3673520_FLAG1_OCC_MASK |
                                      SH3673520_FLAG1_SC_MASK));
    dsg_flag1 = (uint8_t)(s->flag1 & (SH3673520_FLAG1_UV_MASK |
                                      SH3673520_FLAG1_OCD1_MASK |
                                      SH3673520_FLAG1_OCD2_MASK |
                                      SH3673520_FLAG1_SC_MASK));
    chg_flag2 = (uint8_t)(s->flag2 & (SH3673520_FLAG2_OTC_MASK |
                                      SH3673520_FLAG2_UTC_MASK |
                                      SH3673520_FLAG2_WDT_MASK));
    dsg_flag2 = (uint8_t)(s->flag2 & (SH3673520_FLAG2_OTD_MASK |
                                      SH3673520_FLAG2_UTD_MASK |
                                      SH3673520_FLAG2_WDT_MASK));
    s_hw_charge_protect = (chg_flag1 || chg_flag2) ? 1u : 0u;
    s_hw_discharge_protect = (dsg_flag1 || dsg_flag2) ? 1u : 0u;
#else
    s_hw_charge_protect = 0u;
    s_hw_discharge_protect = 0u;
    s_short_latched = 0u;
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    bms_error_clear(BMS_ERROR_DSG_SHORT);
    bms_error_clear(BMS_ERROR_CBC_DSG);
#endif

    /*
     * RST1 表示 RAM 配置恢复默认，RST2 表示 LDO2/SPI 域复位。
     * 不能只清诊断后继续运行，必须先阻断输出并重新应用完整、已校验 AFE 配置。
     */
    if ((s->flag1 & SH3673520_FLAG1_RST1_MASK) ||
        (s->flag2 & SH3673520_FLAG2_RST2_MASK)) {
        s_afe_reconfigure_required = 1u;
        s_snapshot_valid = 0u;
    }

#if SH3673510_HW_PROTECT_ENABLE
    if (s->flag1 & SH3673520_FLAG1_SC_MASK) {
        /* LOADOFF 资格确认期间硬件故障位保持锁存；每帧重见该位不能重新启动恢复窗口。 */
        if (!s_short_latched) {
            s_short_clear_pending = 0u;
            s_short_release_count = 0u;
        }
        s_short_latched = 1u;
        if (!bms_error_get(BMS_ERROR_DSG_SHORT)) bms_error_raise(BMS_ERROR_DSG_SHORT);
        if (!bms_error_get(BMS_ERROR_CBC_DSG)) bms_error_raise(BMS_ERROR_CBC_DSG);
    }
#endif
}

#if SH3673510_HW_PROTECT_ENABLE
/* 把 SH 硬件保护位合并到公共故障状态。 */
static void merge_hw_protection_faults(const sh3673510_control_status_t *s)
{
    union MDLCHGFAULT_REG *f;
    if (s == 0) return;

    /*
     * 硬件保护可在 200 ms 软件采样观察到越限前动作；
     * 将 AFE 锁存保护映射到 Third 报告，避免 MOS 已受阻时上位机仍显示无保护。
     */
    f = &g_stCellInfoReport.unMdlFault_Third;
    if (s->flag1 & SH3673520_FLAG1_OV_MASK) f->bits.b1CellOvp = 1u;
    if (s->flag1 & SH3673520_FLAG1_UV_MASK) f->bits.b1CellUvp = 1u;
    if (s->flag1 & SH3673520_FLAG1_OCC_MASK) f->bits.b1IchgOcp = 1u;
    if (s->flag1 & (SH3673520_FLAG1_OCD1_MASK | SH3673520_FLAG1_OCD2_MASK))
        f->bits.b1IdischgOcp = 1u;
    if (s->flag2 & SH3673520_FLAG2_OTC_MASK) f->bits.b1CellChgOtp = 1u;
    if (s->flag2 & SH3673520_FLAG2_UTC_MASK) f->bits.b1CellChgUtp = 1u;
    if (s->flag2 & SH3673520_FLAG2_OTD_MASK) f->bits.b1CellDischgOtp = 1u;
    if (s->flag2 & SH3673520_FLAG2_UTD_MASK) f->bits.b1CellDischgUtp = 1u;
}

/* 短路恢复必须满足器件状态和物理恢复窗口，不能仅凭关 MOS 后的零电流解除锁定。 */
static uint8_t service_short_recovery(const sh3673510_control_status_t *s)
{
    if ((s == 0) || !s_short_latched) return 1u;

    if (s_short_clear_pending) {
        if (((s->flag1 & SH3673520_FLAG1_SC_MASK) == 0u) &&
            s_load_removed) {
            s_short_latched = 0u;
            s_short_clear_pending = 0u;
            s_short_release_count = 0u;
            bms_error_clear(BMS_ERROR_DSG_SHORT);
            bms_error_clear(BMS_ERROR_CBC_DSG);
            return 1u;
        }
        /*
         * SC 再次置位或负载重接会使待清除资格失效；
         * 下次尝试前必须重新完成整个LOADOFF 窗口。
         */
        s_short_clear_pending = 0u;
        s_short_release_count = 0u;
    }

    if (s_load_removed) {
        if (s_short_release_count < SH3510_SHORT_RELEASE_SAMPLES) ++s_short_release_count;
    } else {
        s_short_release_count = 0u;
    }

    if (!s_short_clear_pending && s_short_release_count >= SH3510_SHORT_RELEASE_SAMPLES) {
        if (!sh3673510_control_clear_flag1(SH3673520_FLAG1_SC_MASK)) {
            s_flag_clear_failed = 1u;
            return 0u;
        }
        s_short_clear_pending = 1u;
        s_short_release_count = 0u;
    }
    return 1u;
}

/* 确认保护解除条件连续稳定达到所需时间。 */
static uint8_t hw_recovery_stable(sh3510_hw_recovery_id_t id,
                                  uint8_t safe,
                                  uint16_t filter_10ms)
{
    uint16_t needed;
    if ((uint8_t)id >= (uint8_t)HW_REC_COUNT) return 0u;
    if (!safe) {
        s_hw_recovery_count[id] = 0u;
        return 0u;
    }
    needed = filter_samples(filter_10ms);
    if (s_hw_recovery_count[id] < needed) ++s_hw_recovery_count[id];
    return (s_hw_recovery_count[id] >= needed) ? 1u : 0u;
}

/* 推进已满足恢复条件的硬件标志清除，保留失败结果。 */
static uint8_t service_hw_flag_recovery(const sh3673510_control_status_t *s)
{
    sh3673510_protection_actual_t actual;
    bms_afe_hw_profile_t hw;
    uint8_t actual_ok;
    uint8_t c1 = 0u, c2 = 0u;
    uint16_t bat_min = 0u, bat_max = 0u;
    uint8_t bat_temp_ok;
    uint8_t dsg_ocp_release_ok;
    if (s == 0) return 0u;
    if (!bms_afe_hw_profile_get(&hw)) return 0u;

    actual_ok = sh3673510_control_get_protection_actual(&actual);
    /*
     * OCD 关闭 DSG 后电流自然为零，零电流不能证明外部过载已解除。
     * 清除 OCD 前需连续 LOADOFF 或真实充电方向证据。
     */
    dsg_ocp_release_ok = (uint8_t)((s_load_removed ||
                                    (s->bstatus2 & SH3673520_BSTATUS2_CHGING_MASK)) ? 1u : 0u);

    /*
     * 复位/唤醒事件用于诊断；此处不清 RST1/RST2，
     * 这些状态由service_afe_reconfiguration() 管理。
     */
    c1 |= (uint8_t)(s->flag1 & SH3673520_FLAG1_WK_MASK);
    if (s->flag2 & SH3673520_FLAG2_WDT_MASK) c2 |= SH3673520_FLAG2_WDT_MASK;

    if (s->flag1 & SH3673520_FLAG1_OV_MASK) {
        if (hw_recovery_stable(HW_REC_OV,
                actual_ok &&
                g_stCellInfoReport.u16VCellMax <= hw.cov_recover_mv &&
                g_stCellInfoReport.u16VCellMax < actual.ov_mv,
                (u16)((hw.cov_recover_ms + 5u) / 10u))) c1 |= SH3673520_FLAG1_OV_MASK;
    } else s_hw_recovery_count[HW_REC_OV] = 0u;

    if (s->flag1 & SH3673520_FLAG1_UV_MASK) {
        if (hw_recovery_stable(HW_REC_UV,
                actual_ok &&
                g_stCellInfoReport.u16VCellMin >= hw.cuv_recover_mv &&
                g_stCellInfoReport.u16VCellMin > actual.uv_mv,
                (u16)((hw.cuv_recover_ms + 5u) / 10u))) c1 |= SH3673520_FLAG1_UV_MASK;
    } else s_hw_recovery_count[HW_REC_UV] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCD1_MASK) {
        if (hw_recovery_stable(HW_REC_OCD1,
                actual_ok && dsg_ocp_release_ok &&
                g_stCellInfoReport.u16IDischg <= hw.ocd_recover_a10 &&
                g_stCellInfoReport.u16IDischg < actual.ocd1_a10,
                (u16)((hw.ocd_recover_ms + 5u) / 10u))) c1 |= SH3673520_FLAG1_OCD1_MASK;
    } else s_hw_recovery_count[HW_REC_OCD1] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCD2_MASK) {
        if (hw_recovery_stable(HW_REC_OCD2,
                actual_ok && dsg_ocp_release_ok &&
                g_stCellInfoReport.u16IDischg <= hw.ocd_recover_a10 &&
                g_stCellInfoReport.u16IDischg < actual.ocd2_a10,
                (u16)((hw.ocd_recover_ms + 5u) / 10u))) c1 |= SH3673520_FLAG1_OCD2_MASK;
    } else s_hw_recovery_count[HW_REC_OCD2] = 0u;

    if (s->flag1 & SH3673520_FLAG1_OCC_MASK) {
        if (hw_recovery_stable(HW_REC_OCC,
                actual_ok && (s_charger_removed ||
                              (s->bstatus2 & SH3673520_BSTATUS2_DSGING_MASK)) &&
                g_stCellInfoReport.u16Ichg <= hw.occ_recover_a10 &&
                g_stCellInfoReport.u16Ichg < actual.occ_a10,
                (u16)((hw.occ_recover_ms + 5u) / 10u))) c1 |= SH3673520_FLAG1_OCC_MASK;
    } else s_hw_recovery_count[HW_REC_OCC] = 0u;

    /* SCONF6 仅为 TS1/TS2 启用硬件温度保护，因此恢复 TEMP 标志只能使用电池传感器。 */
    bat_temp_ok = battery_temperature_snapshot(&bat_min, &bat_max);
    if (s->flag2 & SH3673520_FLAG2_OTC_MASK) {
        if (hw_recovery_stable(HW_REC_OTC,
                bat_temp_ok && bat_max <= hw.chg_ot_recover_x10,
                (u16)((hw.temp_recover_ms + 5u) / 10u))) c2 |= SH3673520_FLAG2_OTC_MASK;
    } else s_hw_recovery_count[HW_REC_OTC] = 0u;

    if (s->flag2 & SH3673520_FLAG2_OTD_MASK) {
        if (hw_recovery_stable(HW_REC_OTD,
                bat_temp_ok && bat_max <= hw.dsg_ot_recover_x10,
                (u16)((hw.temp_recover_ms + 5u) / 10u))) c2 |= SH3673520_FLAG2_OTD_MASK;
    } else s_hw_recovery_count[HW_REC_OTD] = 0u;

    if (s->flag2 & SH3673520_FLAG2_UTC_MASK) {
        if (hw_recovery_stable(HW_REC_UTC,
                bat_temp_ok && bat_min >= hw.chg_ut_recover_x10,
                (u16)((hw.temp_recover_ms + 5u) / 10u))) c2 |= SH3673520_FLAG2_UTC_MASK;
    } else s_hw_recovery_count[HW_REC_UTC] = 0u;

    if (s->flag2 & SH3673520_FLAG2_UTD_MASK) {
        if (hw_recovery_stable(HW_REC_UTD,
                bat_temp_ok && bat_min >= hw.dsg_ut_recover_x10,
                (u16)((hw.temp_recover_ms + 5u) / 10u))) c2 |= SH3673520_FLAG2_UTD_MASK;
    } else s_hw_recovery_count[HW_REC_UTD] = 0u;

    /* 明确排除 SC；SC 仅在连续 LOADOFF 后解除。 */
    if ((c1 && !sh3673510_control_clear_flag1(c1)) ||
        (c2 && !sh3673510_control_clear_flag2(c2))) {
        s_flag_clear_failed = 1u;
        return 0u;
    }
    return 1u;
}

#endif

/* 推进 AFE 重配置流程并检查恢复资格。 */
static uint8_t service_afe_reconfiguration(void)
{
    if (!s_afe_reconfigure_required) return 1u;

    /* 下面的配置和直接关闭操作绕过普通命令缓存。 */
    s_fet_command_valid = 0u;
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    s_snapshot_valid = 0u;
    s_detection_initialized = 0u;
    s_load_removed = s_charger_removed = s_charger_known = 0u;
    (void)sh3673510_control_set_balance(0u);
    (void)sh3673510_control_set_fets(0u, 0u);
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));

    if (!sh3673510_control_init()) return 0u;
    if (!sh3673510_control_clear_flag1(SH3673520_FLAG1_RST1_MASK)) return 0u;
    if (!sh3673510_control_clear_flag2(SH3673520_FLAG2_RST2_MASK)) return 0u;
    restart_sampling();

    s_afe_reconfigure_required = 0u;
    s_hw_charge_protect = 0u;
    s_hw_discharge_protect = 0u;
    return 1u;
}

/* 主循环独占 CRLD 模式和恢复证据；切换后等待，不用旧模式的读数解除保护。 */
static uint8_t sample_release_evidence(const sh3673510_control_status_t *s)
{
    uint8_t load = (s_short_latched || bms_sw_protection_discharge_overcurrent_active() ||
        (s->flag1 & (SH3673520_FLAG1_SC_MASK | SH3673520_FLAG1_OCD1_MASK |
                     SH3673520_FLAG1_OCD2_MASK))) ? 1u : 0u;
    uint8_t data[2];
    uint8_t changed;
    int16_t raw;
    uint32_t mv;
    uint32_t now = pm_get_32k_tick();

    s_load_removed = s_charger_removed = 0u;
    if (!sh3673510_control_set_load_detection(load, &changed)) return 0u;
    if (changed || !s_detection_initialized || s_load_detection != load) {
        s_detection_initialized = 1u;
        s_load_detection = load;
        s_detection_tick = now;
        s_charger_known = 0u;
        return 1u;
    }
    if ((uint32_t)(now - s_detection_tick) < SH3510_DETECT_SETTLE_32K) return 1u;
    if (load) {
        s_load_removed = ((s->bstatus2 & SH3673520_BSTATUS2_LOADOFF_MASK) &&
                          !(s->bstatus2 & SH3673520_BSTATUS2_LOADON_MASK)) ? 1u : 0u;
        return 1u;
    }
    /* 负载检测模式有效或新 VADC 扫描完成前，不使用过时 C+ 证据。 */
    if (!(s->flag2 & SH3673520_FLAG2_VADC_MASK)) return 1u;
    if (SH3673520_ReadRegs(SH3673520_REG_VCHGRH, data, 2u) != SH3673520_OK) return 0u;
    raw = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    if (raw < 0) { s_charger_known = 0u; return 1u; }
    mv = ((uint32_t)(uint16_t)raw * 125u + 16u) / 32u;
    if ((s->bstatus2 & SH3673520_BSTATUS2_CHGING_MASK) || mv >= SH3510_CHARGER_ON_MV) {
        s_charger_known = s_charger_present = 1u;
    } else if (mv <= SH3510_CHARGER_OFF_MV) {
        s_charger_known = s_charger_removed = 1u;
        s_charger_present = 0u;
    }
    return 1u;
}

/* 完整读取后按 ADC 完成标志发布；状态位每次读取，未完成转换不推进测量时间。 */
static uint8_t publish_measurements(void)
{
    int32_t cell[SH3673510_BOARD_CELL_COUNT];
    int32_t pack_mv;
    int32_t current_ma;
    sh3673520_current_raw_t current;
    sh3673520_temperature_raw_t temp;
    sh3673510_control_status_t status;
    bms_sw_protection_inputs_t sw;
    uint16_t max_mv = 0u, min_mv = 0xFFFFu;
    uint16_t bat_temp_min = 0u, bat_temp_max = 0u;
    uint8_t max_pos = 0u, min_pos = 0u, i;
    uint32_t now;

    if (!sh3673510_control_wake()) return 0u;
    if (s_sampling_restart) restart_sampling();
    /*
     * 先读取读清除就绪标志，再读相应数据。后续转换可能替换寄存器，
     * 但不能把旧值标为新样本。
     */
    if (!sh3673510_control_read_status(&status)) return 0u;
    if (SH3673520_ReadCellVoltages(cell, SH3673510_BOARD_CELL_COUNT) != SH3673520_OK) return 0u;
    if (SH3673520_ReadPackVoltage(&pack_mv) != SH3673520_OK) return 0u;
    if (SH3673520_ReadCurrent(&current) != SH3673520_OK) return 0u;
    if (SH3673520_ReadTemperatures(&temp) != SH3673520_OK) return 0u;
    if (SH3673520_CurrentRawToMilliAmp(current.cadc_raw,
        SH3673510_BOARD_SHUNT_UOHM, &current_ma) != SH3673520_OK) return 0u;

    /* 修改共享报告前先拒绝整个无效帧。 */
    for (i = 0u; i < SH3673510_BOARD_CELL_COUNT; ++i)
        if (cell[i] < 0L || cell[i] > 65535L) return 0u;
    now = pm_get_32k_tick();
    if (status.flag2 & SH3673520_FLAG2_VADC_MASK) { s_vadc_seen = 1u; s_vadc_tick = now; }
    if (status.flag2 & SH3673520_FLAG2_CADC_MASK) { s_cadc_seen = 1u; s_cadc_tick = now; }
    if ((uint32_t)(now - s_vadc_tick) > SH3510_ADC_MAX_AGE_32K ||
        (uint32_t)(now - s_cadc_tick) > SH3510_ADC_MAX_AGE_32K) return 0u;
    s_sample_pending = ((status.flag2 & (SH3673520_FLAG2_VADC_MASK | SH3673520_FLAG2_CADC_MASK)) !=
                       (SH3673520_FLAG2_VADC_MASK | SH3673520_FLAG2_CADC_MASK)) ? 1u : 0u;
    publish_hw_status(&status);
    if (s_afe_reconfigure_required) return 1u;
    if (!sample_release_evidence(&status)) return 0u;
    if ((uint32_t)(now - s_sampling_start_tick) >= SH3510_TEMP_STARTUP_32K)
        s_temperature_started = 1u;
    if (!s_vadc_seen || !s_cadc_seen || !s_temperature_started ||
        (!s_snapshot_valid && s_sample_pending)) {
        s_sample_pending = 1u;
        return 1u;
    }

    if (status.flag2 & SH3673520_FLAG2_VADC_MASK) {
        for (i = 0u; i < SH3673510_BOARD_CELL_COUNT; ++i) {
            uint16_t mv;
            mv = (uint16_t)cell[i];
            g_stCellInfoReport.u16VCell[i] = mv;
            if (mv > max_mv) { max_mv = mv; max_pos = (uint8_t)(i + 1u); }
            if (mv < min_mv) { min_mv = mv; min_pos = (uint8_t)(i + 1u); }
        }
        for (i = SH3673510_BOARD_CELL_COUNT; i < 32u; ++i)
            g_stCellInfoReport.u16VCell[i] = SH3510_MISSING_CELL_MV;
        g_stCellInfoReport.u16VCellMax = max_mv;
        g_stCellInfoReport.u16VCellMin = min_mv;
        g_stCellInfoReport.u16VCellMaxPosition = max_pos;
        g_stCellInfoReport.u16VCellMinPosition = min_pos;
        g_stCellInfoReport.u16VCellDelta = (uint16_t)(max_mv - min_mv);

        if (pack_mv < 0L) pack_mv = 0L;
        s_aux.pack_voltage_mv = (uint32_t)pack_mv;
        g_stCellInfoReport.u16VCellTotle = (uint16_t)(((uint32_t)pack_mv + 5u) / 10u);
    }

    if (status.flag2 & SH3673520_FLAG2_CADC_MASK) {
        s_aux.raw_current_ma = (current_ma == INT32_MIN) ? INT32_MAX : -current_ma;
        s_aux.current_ma = bms_config_calibrate_current(s_aux.raw_current_ma);
        s_aux.sample_tick_32k = now;

        /* 公共 mA 正放电；无符号取绝对值覆盖 INT32_MIN，0.1A 报告饱和。 */
        {
            uint32_t ma = (s_aux.current_ma < 0) ?
                (0u - (uint32_t)s_aux.current_ma) : (uint32_t)s_aux.current_ma;
            uint32_t a10 = (ma + 50u) / 100u;
            if (a10 > 65535u) a10 = 65535u;
            g_stCellInfoReport.u16IDischg = (s_aux.current_ma > 0) ? (uint16_t)a10 : 0u;
            g_stCellInfoReport.u16Ichg = (s_aux.current_ma < 0) ? (uint16_t)a10 : 0u;
        }
    }

    if (status.flag2 & SH3673520_FLAG2_VADC_MASK) {
        s_mos_ntc_raw = (int16_t)temp.external_raw[SH3673510_BOARD_MOS_NTC_INDEX];
        memset(s_ntc_valid, 0, sizeof(s_ntc_valid));
        memset(s_ntc_ohm, 0, sizeof(s_ntc_ohm));
        for (i = 0u; i < 4u; ++i) {
            uint32_t r;
            if (SH3673520_NtcRawToOhm(temp.external_raw[i], &r) == SH3673520_OK &&
                r >= 500u && r <= 300000u) { s_ntc_valid[i] = 1u; s_ntc_ohm[i] = r; }
        }
        g_stCellInfoReport.u16Temperature[AFE1_TEMP1] =
            s_ntc_valid[SH3673510_BOARD_BAT_NTC1_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_BOARD_BAT_NTC1_INDEX]) : 0u;
        g_stCellInfoReport.u16Temperature[AFE1_TEMP2] =
            s_ntc_valid[SH3673510_BOARD_BAT_NTC2_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_BOARD_BAT_NTC2_INDEX]) : 0u;
#if SH3673510_PRODUCT_HEATER_NTC_SUPPORTED
        g_stCellInfoReport.u16Temperature[AFE1_TEMP3] =
            s_ntc_valid[SH3673510_BOARD_HEATER_NTC_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_BOARD_HEATER_NTC_INDEX]) : 0u;
#else
        g_stCellInfoReport.u16Temperature[AFE1_TEMP3] = 0u;
#endif
#if SH3673510_PRODUCT_MOS_NTC_SUPPORTED
        g_stCellInfoReport.u16Temperature[MOS_TEMP1] =
            s_ntc_valid[SH3673510_BOARD_MOS_NTC_INDEX] ? ntc_temp(s_ntc_ohm[SH3673510_BOARD_MOS_NTC_INDEX]) : 0u;
#else
        g_stCellInfoReport.u16Temperature[MOS_TEMP1] = 0u;
#endif

        /*
         * 实时温度极值只使用经过校验的 TS1/TS2 电池温度；加热与 MOS 角色由产品定义。
         * TS4 单独发布给 MOS 高温保护，0 仍是旧协议无效/温度断线标记。
         */
        if (battery_temperature_snapshot(&bat_temp_min, &bat_temp_max)) {
            g_stCellInfoReport.u16TempMin = bat_temp_min;
            g_stCellInfoReport.u16TempMax = bat_temp_max;
        } else {
            g_stCellInfoReport.u16TempMin = 0u;
            g_stCellInfoReport.u16TempMax = 0u;
        }

        /* 报告温度只由本次 AFE 采样发布，应用层不再二次查表覆盖。 */
        g_stCellInfoReport.u16Temperature[ENV_TEMP3] = g_stCellInfoReport.u16TempMax;

        if (s_ntc_valid[SH3673510_BOARD_BAT_NTC1_INDEX] &&
            s_ntc_valid[SH3673510_BOARD_BAT_NTC2_INDEX])
            s_aux.battery_ntc_100ohm =
                ((s_ntc_ohm[SH3673510_BOARD_BAT_NTC1_INDEX] < s_ntc_ohm[SH3673510_BOARD_BAT_NTC2_INDEX]) ?
                 s_ntc_ohm[SH3673510_BOARD_BAT_NTC1_INDEX] : s_ntc_ohm[SH3673510_BOARD_BAT_NTC2_INDEX]) / 100u;
        else if (s_ntc_valid[SH3673510_BOARD_BAT_NTC1_INDEX])
            s_aux.battery_ntc_100ohm = s_ntc_ohm[SH3673510_BOARD_BAT_NTC1_INDEX] / 100u;
        else if (s_ntc_valid[SH3673510_BOARD_BAT_NTC2_INDEX])
            s_aux.battery_ntc_100ohm = s_ntc_ohm[SH3673510_BOARD_BAT_NTC2_INDEX] / 100u;
        else s_aux.battery_ntc_100ohm = 0u;
#if SH3673510_PRODUCT_MOS_NTC_SUPPORTED
        s_aux.mos_ntc_100ohm = s_ntc_valid[SH3673510_BOARD_MOS_NTC_INDEX] ?
            s_ntc_ohm[SH3673510_BOARD_MOS_NTC_INDEX] / 100u : 0u;
#else
        s_aux.mos_ntc_100ohm = 0u;
#endif
        s_aux.battery_ntc_mv = legacy_adc_mv(s_aux.battery_ntc_100ohm * 100u);
        s_aux.mos_ntc_mv = legacy_adc_mv(s_aux.mos_ntc_100ohm * 100u);
    }

    if (!s_afe_reconfigure_required) {
        memset(&sw, 0, sizeof(sw));
        sw.current_recovery_requires_evidence = 1u;
        sw.current_recovery_sample_fresh = !s_sample_pending;
        sw.charge_recovery_allowed =
            (s_charger_removed || (status.bstatus2 & SH3673520_BSTATUS2_DSGING_MASK));
        sw.discharge_recovery_allowed =
            (s_load_removed || (status.bstatus2 & SH3673520_BSTATUS2_CHGING_MASK));
        sw.mos_temp_required = SH3673510_PRODUCT_MOS_NTC_SUPPORTED;
        sw.battery_temp_valid = battery_temperature_snapshot(&sw.battery_temp_min,
                                                              &sw.battery_temp_max);
#if SH3673510_PRODUCT_MOS_NTC_SUPPORTED
        sw.mos_temp_valid = s_ntc_valid[SH3673510_BOARD_MOS_NTC_INDEX] ? 1u : 0u;
        if (sw.mos_temp_valid)
            sw.mos_temp = g_stCellInfoReport.u16Temperature[MOS_TEMP1];
#else
        sw.mos_temp_valid = 0u;
        sw.mos_temp = 0u;
#endif
#if SH3673510_SW_PROTECT_ENABLE
        bms_sw_protection_update(&sw);
#else
        bms_sw_protection_clear();
#endif
#if SH3673510_HW_PROTECT_ENABLE
        merge_hw_protection_faults(&status);
        /* 待完成 CADC 可暂停恢复计数，但观察到负载重接时必须立即作废旧物理窗口。 */
        if (!s_load_removed && !(status.bstatus2 & SH3673520_BSTATUS2_CHGING_MASK))
            s_hw_recovery_count[HW_REC_OCD1] = s_hw_recovery_count[HW_REC_OCD2] = 0u;
        if (!s_charger_removed && !(status.bstatus2 & SH3673520_BSTATUS2_DSGING_MASK))
            s_hw_recovery_count[HW_REC_OCC] = 0u;
        if (!service_short_recovery(&status)) return 0u;
        if (!s_sample_pending && !service_hw_flag_recovery(&status)) return 0u;
#endif
        bms_sw_protection_record_fault_edges();
    }
    s_snapshot_valid = 1u;
    return 1u;
}

/* 初始化选定 AFE 后端并应用产品配置。 */
void sh3673510_bms_afe_init(void)
{
    uint8_t i;

    bms_sw_protection_init();
    for (i = SH3673510_BOARD_CELL_COUNT; i < 32u; ++i)
        g_stCellInfoReport.u16VCell[i] = SH3510_MISSING_CELL_MV;
    memset(&s_aux, 0, sizeof(s_aux));
    s_hw_afe_error = 0u;
    s_requested_charge_on = 0u;
    s_requested_discharge_on = 0u;

    s_snapshot_valid = 0u;
    s_detection_initialized = 0u;
    s_load_removed = s_charger_removed = s_charger_known = 0u;
    /* AFE 通信重初始化期间保留 s_short_latched。 */
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    s_hw_charge_protect = 0u;
    s_hw_discharge_protect = 0u;
    s_afe_reconfigure_required = 0u;
    s_bstatus2 = 0u;
    s_flag1 = 0u;
    s_flag2 = 0u;
    s_mos_ntc_raw = 0;
    s_fet_command_valid = 0u;
    s_last_charge_command = 0u;
    s_last_discharge_command = 0u;
    s_flag_clear_failed = 0u;
    sh3673510_board_force_heater_fuse_safe();
    sh3673510_board_set_heater(0u);
    if (!sh3673510_control_init()) { note_comm_error(); return; }
    restart_sampling();
    note_comm_ok();
}

/* 采集 AFE 测量和状态，并更新样本有效性。 */
void sh3673510_bms_afe_sample(void)
{
    s_sample_pending = 0u;
    sh3673510_board_force_heater_fuse_safe();
    if (s_flag_clear_failed) { note_comm_error(); return; }
    if (!publish_measurements()) {
        /* 公共 bms_afe_guard 管理关闭请求、WDT 静默和有界重初始化。 */
        s_snapshot_valid = 0u;
        note_comm_error();
        return;
    }

    if (s_afe_reconfigure_required) {
        if (!service_afe_reconfiguration()) note_comm_error();
        return; /* 下一周期必须使用配置完成后的新测量。 */
    }

    note_comm_ok();
    /* 公共功能模块管理加热/均衡，公共门禁负责最终 FET 应用。 */
}

/* 查询是否仍有 AFE 采样流程未完成。 */
uint8_t sh3673510_bms_afe_sample_pending(void)
{
    return s_sample_pending;
}

/* 应用独立 AFE 硬件保护配置并返回结果。 */
uint8_t sh3673510_bms_afe_apply_protection_config(void)
{
    if (!sh3673510_control_ready()) return 0u;
    if (!sh3673510_control_apply_protection()) { note_comm_error(); return 0u; }
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    return 1u;
}

/* 设置后端充放电 MOS 请求并进行保护仲裁。 */
uint8_t sh3673510_bms_afe_set_fets(uint8_t requested_charge_on,
                                   uint8_t requested_discharge_on)
{
    s_requested_charge_on = requested_charge_on ? 1u : 0u;
    s_requested_discharge_on = requested_discharge_on ? 1u : 0u;
    return sh3510_apply_requested_fets();
}

/* 设置后端输出授权，禁止绕过公共安全门禁。 */
void sh3673510_bms_afe_set_output_enabled(uint8_t enabled)
{
    s_output_enabled = enabled ? 1u : 0u;
    if (!s_output_enabled) {
        s_fet_command_valid = 0u;
        if (sh3673510_control_ready()) {
            (void)sh3673510_control_set_balance(0u);
            if (!sh3673510_control_set_fets(0u, 0u)) note_comm_error();
        }
    } else if (s_snapshot_valid) {
        (void)sh3510_apply_requested_fets();
    }
}

/* 主循环只读 RAM 查询；有效性与数据属于最近一次接受的帧。 */
uint8_t sh3673510_backend_get_charge_source_present(uint8_t *present)
{
    if (present == 0 || !s_snapshot_valid || !s_charger_known) return 0u;
    *present = s_charger_present;
    return 1u;
}

/* 取得均衡、温度和断线策略需要的后端快照。 */
uint8_t sh3673510_backend_get_feature_snapshot(bms_afe_feature_snapshot_t *out)
{
    if (out == 0) return 0u;
    memset(out, 0, sizeof(*out));
    if (!s_snapshot_valid) return 0u;
    out->valid = 1u;
    out->cell_count = SH3673510_BOARD_CELL_COUNT;
    out->battery_temp_valid = battery_temperature_snapshot(&out->battery_temp_min_x10,
                                                            &out->battery_temp_max_x10);
#if SH3673510_PRODUCT_HEATER_NTC_SUPPORTED
    out->heater_temp_valid = s_ntc_valid[SH3673510_BOARD_HEATER_NTC_INDEX];
    out->heater_temp_x10 = g_stCellInfoReport.u16Temperature[AFE1_TEMP3];
#endif
#if SH3673510_PRODUCT_MOS_NTC_SUPPORTED
    out->mos_temp_valid = s_ntc_valid[SH3673510_BOARD_MOS_NTC_INDEX];
    out->mos_temp_x10 = g_stCellInfoReport.u16Temperature[MOS_TEMP1];
#endif
    return 1u;
}

/* 取得后端辅助测量与有效性。 */
uint8_t sh3673510_bms_afe_get_aux_measurements(bms_afe_aux_measurements_t *m)
{
    if (m == 0) return 0u;
    if (!s_snapshot_valid) { memset(m, 0, sizeof(*m)); return 0u; }
    *m = s_aux;
    return 1u;
}

/* 取得 FET 请求、控制和保护阻断诊断。 */
uint8_t sh3673510_bms_afe_get_fet_diagnostics(uint8_t *command_bits,
                                               uint8_t *command_valid,
                                               uint8_t *driver_bits,
                                               uint8_t *driver_valid)
{
    if ((command_bits == 0) || (command_valid == 0) ||
        (driver_bits == 0) || (driver_valid == 0)) return 0u;
    *command_bits = (uint8_t)((s_last_charge_command ? 1u : 0u) |
                              (s_last_discharge_command ? 2u : 0u));
    *command_valid = s_fet_command_valid;
    *driver_bits = (uint8_t)((g_bms_system_status.bits.b1Status_MOS_CHG ? 1u : 0u) |
                             (g_bms_system_status.bits.b1Status_MOS_DSG ? 2u : 0u));
    *driver_valid = s_snapshot_valid;
    return 1u;
}

/* 取得 FET 仲裁的详细诊断字段。 */
uint8_t sh3673510_bms_afe_get_fet_diag_detail(sh3673510_fet_diag_detail_t *detail)
{
    uint32_t common = 0u;
    if (detail == 0) return 0u;
    memset(detail, 0, sizeof(*detail));
    detail->flag1 = s_flag1;
    detail->flag2 = s_flag2;
    detail->bstatus2 = s_bstatus2;
    detail->mos_ntc_raw = (uint16_t)s_mos_ntc_raw;
    detail->mos_ntc_ohm = s_ntc_ohm[SH3673510_BOARD_MOS_NTC_INDEX];
    detail->mos_temp_x10 = g_stCellInfoReport.u16Temperature[MOS_TEMP1];

    if (s_output_enabled) detail->backend_state |= DIAG_SH_OUTPUT_ENABLED;
    if (s_snapshot_valid) detail->backend_state |= DIAG_SH_SNAPSHOT_VALID;
    if (!bms_afe_samples_qualified()) detail->backend_state |= DIAG_SH_OUTPUT_INHIBIT;
    if (s_hw_afe_error) detail->backend_state |= DIAG_SH_E2P_ERROR;
    if (s_hw_charge_protect) detail->backend_state |= DIAG_SH_CHARGE_HW_BLOCK;
    if (s_hw_discharge_protect) detail->backend_state |= DIAG_SH_DISCHARGE_HW_BLOCK;
    if (s_short_latched) detail->backend_state |= DIAG_SH_SHORT_LATCHED;
    if (s_afe_reconfigure_required) detail->backend_state |= DIAG_SH_RECONFIGURE;
    if (bms_error_get(BMS_ERROR_TEMP_BREAK)) detail->backend_state |= DIAG_SH_TEMP_BREAK;
    if (bms_error_get(BMS_ERROR_AFE1)) detail->backend_state |= DIAG_SH_AFE_ERROR;
    if (bms_error_get(BMS_ERROR_SPI)) detail->backend_state |= DIAG_SH_SPI_ERROR;
    if (s_ntc_valid[SH3673510_BOARD_BAT_NTC1_INDEX])
        detail->sensor_state |= DIAG_SH_TS1_VALID;
    if (s_ntc_valid[SH3673510_BOARD_BAT_NTC2_INDEX])
        detail->sensor_state |= DIAG_SH_TS2_VALID;
#if SH3673510_PRODUCT_MOS_NTC_SUPPORTED
    detail->sensor_state |= DIAG_SH_MOS_NTC_SUPPORTED;
    if (s_ntc_valid[SH3673510_BOARD_MOS_NTC_INDEX])
        detail->sensor_state |= DIAG_SH_MOS_NTC_VALID;
#endif

    if (!s_output_enabled) common |= DIAG_BLOCK_OUTPUT;
    if (!s_snapshot_valid || !bms_afe_samples_qualified() || s_hw_afe_error ||
        s_afe_reconfigure_required || bms_error_get(BMS_ERROR_AFE1) ||
        bms_error_get(BMS_ERROR_SPI)) common |= DIAG_BLOCK_COMM;
    if (bms_error_get(BMS_ERROR_TEMP_BREAK)) common |= DIAG_BLOCK_TEMP;
    detail->charge_block_reasons = common;
    detail->discharge_block_reasons = common;
    if (bms_sw_protection_charge_blocked())
        detail->charge_block_reasons |= DIAG_BLOCK_SW;
    if (bms_sw_protection_discharge_blocked())
        detail->discharge_block_reasons |= DIAG_BLOCK_SW;
    if (s_hw_charge_protect) detail->charge_block_reasons |= DIAG_BLOCK_HW;
    if (s_hw_discharge_protect || s_short_latched ||
        bms_error_get(BMS_ERROR_DSG_SHORT) || bms_error_get(BMS_ERROR_CBC_DSG))
        detail->discharge_block_reasons |= DIAG_BLOCK_HW;
    return 1u;
}

/* 按器件与板级时序进入 AFE 休眠。 */
uint8_t sh3673510_bms_afe_sleep(void)
{
    s_short_clear_pending = 0u;
    s_short_release_count = 0u;

    s_fet_command_valid = 0u;
    memset(s_hw_recovery_count, 0, sizeof(s_hw_recovery_count));
    /* 即使状态转换中止，也必须作废旧驱动/样本证据。 */
    s_snapshot_valid = 0u;
    s_detection_initialized = 0u;
    s_load_removed = s_charger_removed = s_charger_known = 0u;
    s_sampling_restart = 1u;
    if (!sh3673510_control_sleep()) {
        note_comm_error();
        return 0u;
    }
    return 1u;
}
