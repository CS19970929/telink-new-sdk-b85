/*
 * 文件功能：公共均衡、Open-Wire 与 heater 策略；依据采样可信度、温度和保护状态仲裁，
 * 硬件动作交给 backend。
 * bms/app/bms_features.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_debug_log.h"
#include "bms_diag.h"
#include "bms_features.h"
#include "bms_config_store.h"

#include "bms_board.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_parameters.h"
#include <string.h>

#define BMS_OPENWIRE_FIRST_IDLE_SAMPLES \
    ((BMS_OPENWIRE_FIRST_IDLE_MS + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) / BMS_FEATURE_SERVICE_PERIOD_MS)
#define BMS_OPENWIRE_PERIOD_SAMPLES \
    ((BMS_OPENWIRE_PERIOD_MS + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) / BMS_FEATURE_SERVICE_PERIOD_MS)
#define BMS_BALANCE_TRUST_CONFIRM_SAMPLES \
    ((BMS_BALANCE_TRUST_CONFIRM_MS + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) / BMS_FEATURE_SERVICE_PERIOD_MS)

typedef struct {
    uint8_t heater_on;
    uint8_t heater_fuse_fired;
    bms_heater_state_t heater_state;
    uint16_t heater_off_hot_samples;

    uint8_t charge_session_active;

    uint8_t openwire_active;
    uint8_t openwire_sample_active;
    uint8_t openwire_fault_latched;
    uint8_t openwire_suspected;
    uint16_t openwire_idle_samples;
    uint16_t openwire_cooldown_samples;
    bms_afe_openwire_result_t openwire_result;

    uint8_t balance_active;
    uint8_t balance_voltage_trusted;
    uint16_t balance_trust_samples;
    uint8_t balance_prev_cell_count;
    uint16_t balance_prev_cell_mv[BMS_AFE_FEATURE_MAX_CELLS];
    uint32_t balance_requested_mask;
} bms_feature_state_t;

/*
 * 公共功能的资格/阶段状态；仅由 service/init/AFE 失效处理入口更新，
 * 产品能力由配置提供。
 */
static bms_feature_state_t s_feature;

/* 汇总板级与 AFE 可确认的充电源证据。 */
static uint8_t charge_source_present(void)
{
    uint8_t present = 0u;
    if (bms_afe_get_charge_source_present(&present)) return present ? 1u : 0u;
    return bms_board_charge_source_present() ? 1u : 0u;
}

/* 更新充电会话状态及稳定确认计数。 */
static void update_charge_session(void)
{
    /*
     * D008 无电流检测前的 AFE 硬件充电低温关断，因此可信充电电流用于建立会话。
     * 预热主动阻断 CHG 后电流消失，零电流不能清会话；
     * 真实放电方向是退出充电场景的可信证据。
     */
    if (g_bms_report.u16IDischg != 0u)
        s_feature.charge_session_active = 0u;
    else if ((g_bms_report.u16Ichg != 0u) || charge_source_present())
        s_feature.charge_session_active = 1u;
}

/* 更新加热请求并驱动支持的板级输出。 */
static void set_heater(uint8_t on)
{
    on = (on && bms_board_heater_supported()) ? 1u : 0u;
    bms_board_heater_set(on);
    s_feature.heater_on = on;
    g_bms_system_status.bits.b1Status_Heat = on;
}

/* 停止加热并复位当前加热阶段。 */
static void heater_idle(void)
{
    set_heater(0u);
    s_feature.heater_state = BMS_HEATER_IDLE;
}

/* 把加热确认延时转换为连续样本数。 */
static uint16_t heater_confirm_samples(void)
{
    uint32_t confirm_ms = bms_board_heater_off_fault_confirm_ms();
    uint32_t samples;

    if (confirm_ms == 0u) return 1u;
    samples = (confirm_ms + BMS_FEATURE_SERVICE_PERIOD_MS - 1u) /
              BMS_FEATURE_SERVICE_PERIOD_MS;
    if (samples == 0u) samples = 1u;
    if (samples > 65535u) samples = 65535u;
    return (uint16_t)samples;
}

/* 满足独立故障条件时触发支持的不可逆熔断输出。 */
static void fire_heater_fuse(void)
{
    heater_idle();
    if (!s_feature.heater_fuse_fired)
    {
        bms_board_heater_fuse_fire();
        s_feature.heater_fuse_fired = 1u;
    }
    if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
}

/*
 * 加热电路安全独立于功率 MOS 高温保护。GP1 测加热 MOS 区域；
 * 软件关闭 PA1/MCC-EN-HT 后，GP1 持续超温达到板级确认时间，
 * 认为加热功率路径卡住/故障，驱动 PD4/MCC-EN-RF 触发不可逆保险丝。
 * 加热期间过热先关闭命令，只有关闭后持续过热才推进不可逆动作。
 */
static uint8_t heater_circuit_safe(const bms_afe_feature_snapshot_t *s)
{
    uint16_t trip;
    uint16_t required;

    if (s_feature.heater_fuse_fired)
    {
        heater_idle();
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    if ((s == 0) || !s->heater_temp_valid)
    {
        s_feature.heater_off_hot_samples = 0u;
        heater_idle();
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    trip = bms_board_heater_off_fault_temp_x10();
    if (!bms_board_heater_fuse_supported() || (trip == 0u))
    {
        s_feature.heater_off_hot_samples = 0u;
        bms_error_clear(BMS_ERROR_HEAT);
        return 1u;
    }

    if (s->heater_temp_x10 < trip)
    {
        s_feature.heater_off_hot_samples = 0u;
        bms_error_clear(BMS_ERROR_HEAT);
        return 1u;
    }

    if (s_feature.heater_on)
    {
        s_feature.heater_off_hot_samples = 0u;
        set_heater(0u);
        if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
        return 0u;
    }

    required = heater_confirm_samples();
    if (s_feature.heater_off_hot_samples < required)
        ++s_feature.heater_off_hot_samples;
    if (!bms_error_get(BMS_ERROR_HEAT)) bms_error_raise(BMS_ERROR_HEAT);
    set_heater(0u);

    if (s_feature.heater_off_hot_samples >= required)
        fire_heater_fuse();
    return 0u;
}

/* 检查必须立即停止加热的硬故障条件。 */
static uint8_t heater_hard_fault(void)
{
    const bms_fault_bits_t *f = &g_bms_report.unMdlFault_Third.bits;

    /*
     * 充放电低温不作为加热硬故障，低温正是预热要恢复的条件；CUV 也不是加热硬故障，
     * 深度放电包可能需预热后才能安全充电。
     */
    return (!bms_protection_params_valid() ||
            s_feature.openwire_fault_latched ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            f->b1CellOvp || f->b1BatOvp ||
            f->b1IchgOcp || f->b1IdischgOcp ||
            f->b1CellChgOtp || f->b1CellDischgOtp ||
            f->b1TmosOtp) ? 1u : 0u;
}

/* 判断电池温度是否形成加热需求。 */
static uint8_t heater_demand(const bms_afe_feature_snapshot_t *s,
                             const bms_user_params_t *config)
{
    uint16_t charge_utp_trip;

    if ((s == 0) || (config == 0) || !s->battery_temp_valid) return 0u;

    /*
     * 不能等待滤波后的充电低温故障位再预热；首个可信电流样本就需停止低温充电尝试。
     * HeaterStart 是用户策略，Third UTP 阈值仍是绝对软件充电许可边界。
     */
    charge_utp_trip = g_bms_protection_params.u16TchgUTp_Third;
    if ((charge_utp_trip != 0u) &&
        (s->battery_temp_min_x10 <= charge_utp_trip))
        return 1u;
    if (g_bms_report.unMdlFault_Third.bits.b1CellChgUtp) return 1u;

    if (s_feature.heater_state == BMS_HEATER_ACTIVE)
        return (s->battery_temp_min_x10 < config->heater_stop_x10) ? 1u : 0u;
    return (s->battery_temp_min_x10 < config->heater_start_x10) ? 1u : 0u;
}

/* 推进加热需求、资格确认和故障处置状态。 */
static void service_heater(const bms_afe_feature_snapshot_t *s)
{
    bms_user_params_t config;
    uint8_t demand;

    if ((s == 0) || !s->valid || !bms_board_heater_supported() || !bms_board_heater_allowed())
    {
        heater_idle();
        return;
    }

    if (!heater_circuit_safe(s)) return;

    if (!bms_config_get_user(&config) || !config.heater_enable ||
        !s->battery_temp_valid || heater_hard_fault())
    {
        heater_idle();
        return;
    }

    if (!s_feature.charge_session_active)
    {
        heater_idle();
        return;
    }

    /* 断线诊断拥有短时硬隔离窗口；已请求预热时保留请求，诊断结束后恢复。 */
    if (s_feature.openwire_active)
    {
        set_heater(0u);
        return;
    }

    demand = heater_demand(s, &config);

    if (s_feature.heater_state == BMS_HEATER_IDLE)
    {
        if (demand)
        {
            /*
             * PREHEAT 阻断充电后允许充电会话在 Ichg=0 时保留，
             * 但不能据此从 IDLE 开始新加热周期：普通充电后充电器可能已拔除且无负载，
             * 没有可见电流变化清会话。
             * 请求加热前需新充电方向证据或未来批准的物理充电器信号；
             * 进入 ARMING 后零电流才是充电路径已阻断的预期证据。
             */
            if ((g_bms_report.u16Ichg != 0u) || charge_source_present())
            {
                s_feature.heater_state = BMS_HEATER_ARMING;
            }
            set_heater(0u);
        }
        else
        {
            set_heater(0u);
        }
        return;
    }

    if (s_feature.heater_state == BMS_HEATER_ARMING)
    {
        if (!demand)
        {
            heater_idle();
            return;
        }
        if (g_bms_report.u16Ichg != 0u)
        {
            set_heater(0u);
            return;
        }
        set_heater(1u);
        s_feature.heater_state = BMS_HEATER_ACTIVE;
        return;
    }

    if (!demand)
    {
        heater_idle();
        return;
    }

    /* 本服务前放电事件已清 charge_session；若仍收到矛盾报告，按故障安全关闭加热。 */
    if (g_bms_report.u16IDischg != 0u)
    {
        s_feature.charge_session_active = 0u;
        heater_idle();
        return;
    }

    set_heater(1u);
}

/* 检查禁止断线检测的硬故障条件。 */
static uint8_t openwire_hard_fault(void)
{
    return (!bms_protection_params_valid() ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG)) ? 1u : 0u;
}

/* 检查采样、负载和保护状态是否允许断线检测。 */
static uint8_t openwire_eligible(void)
{
    if (s_feature.heater_on) return 0u;
    if (g_bms_report.u16Ichg || g_bms_report.u16IDischg) return 0u;
    return openwire_hard_fault() ? 0u : 1u;
}

/* 发布均衡通道和工作状态到公共报告。 */
static void publish_balance(uint32_t mask)
{
    g_bms_report.u16BalanceFlag1 = (uint16_t)(mask & 0xFFFFu);
    g_bms_report.u16BalanceFlag2 = (uint16_t)((mask >> 16) & 0x00FFu);
    g_bms_system_status.bits.b1Status_Balance = mask ? 1u : 0u;
}

/* 应用均衡掩码并记录实际成功状态。 */
static uint8_t apply_balance_mask(uint32_t desired)
{
    uint32_t actual;

    if (s_feature.balance_requested_mask != desired)
        BMS_LOG(BMS_LOG_INFO, BMS_LOG_FEATURE, BMS_LOG_BALANCE_REQUEST, s_feature.balance_requested_mask, desired);
    s_feature.balance_requested_mask = desired;
    if (!bms_afe_set_balance_mask(desired))
    {
        BMS_LOG(BMS_LOG_ERROR, BMS_LOG_FEATURE, BMS_LOG_BALANCE_RESULT, desired, UINT32_MAX);
        if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
        if (bms_afe_get_balance_mask(&actual)) publish_balance(actual);
        return 0u;
    }

    if (!bms_afe_get_balance_mask(&actual))
    {
        if (!bms_error_get(BMS_ERROR_BALANCE)) bms_error_raise(BMS_ERROR_BALANCE);
        return 0u;
    }

    bms_error_clear(BMS_ERROR_BALANCE);
    publish_balance(actual);
    return 1u;
}

/* 按采样可信度推进非阻塞 Open-Wire 阶段；可疑状态与已确认断线必须区分。 */
static void service_openwire(void)
{
    bms_afe_diag_state_t state;
    bms_afe_openwire_result_t result;

    if (s_feature.openwire_active)
    {
        memset(&result, 0, sizeof(result));
        state = bms_afe_openwire_poll(&result);
        if (state == BMS_AFE_DIAG_READY)
        {
            s_feature.openwire_result = result;
            if (result.valid && result.determinate)
            {
                s_feature.openwire_fault_latched = result.open_cell_mask ? 1u : 0u;
                s_feature.openwire_suspected = s_feature.openwire_fault_latched;
                if (!s_feature.openwire_fault_latched)
                {
                    s_feature.balance_voltage_trusted = 0u;
                    s_feature.balance_trust_samples = 0u;
                    s_feature.balance_prev_cell_count = 0u;
                }
            }
            else
            {
                s_feature.openwire_suspected = 1u;
            }
            s_feature.openwire_active = 0u;
            s_feature.openwire_idle_samples = 0u;
            s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_PERIOD_SAMPLES;
        }
        else if (state == BMS_AFE_DIAG_ERROR)
        {
            s_feature.openwire_suspected = 1u;
            s_feature.openwire_active = 0u;
            s_feature.openwire_idle_samples = 0u;
            s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES;
        }
        return;
    }

    if (!s_feature.openwire_suspected && s_feature.openwire_cooldown_samples != 0u)
    {
        --s_feature.openwire_cooldown_samples;
        return;
    }

    if (!openwire_eligible())
    {
        s_feature.openwire_idle_samples = 0u;
        return;
    }

    if (!s_feature.openwire_suspected &&
        s_feature.openwire_idle_samples < (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES)
    {
        ++s_feature.openwire_idle_samples;
        return;
    }

    s_feature.balance_active = 0u;
    if (!apply_balance_mask(0u))
    {
        s_feature.openwire_idle_samples = 0u;
        return;
    }

    if (bms_afe_openwire_start())
    {
        s_feature.openwire_active = 1u;
        s_feature.openwire_idle_samples = 0u;
    }
    else
    {
        s_feature.openwire_suspected = 1u;
        s_feature.openwire_idle_samples = 0u;
        s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES;
    }
}

/* 检查电芯电压快照是否可用于均衡策略。 */
static uint8_t balance_sample_plausible(const bms_afe_feature_snapshot_t *s)
{
    uint16_t vmin = 0xFFFFu;
    uint16_t vmax = 0u;
    uint8_t i;

    if ((s == 0) || !s->valid || (s->cell_count == 0u) ||
        (s->cell_count > BMS_AFE_FEATURE_MAX_CELLS))
        return 0u;

    for (i = 0u; i < s->cell_count; ++i)
    {
        uint16_t cell = g_bms_report.u16VCell[i];
        if (cell < BMS_BALANCE_CELL_PLAUSIBLE_MIN_MV ||
            cell > BMS_BALANCE_CELL_PLAUSIBLE_MAX_MV)
            return 0u;

        if (s_feature.balance_prev_cell_count == s->cell_count)
        {
            uint16_t previous = s_feature.balance_prev_cell_mv[i];
            uint16_t step = (cell >= previous) ?
                            (uint16_t)(cell - previous) :
                            (uint16_t)(previous - cell);
            if (step > BMS_BALANCE_CELL_MAX_STEP_MV) return 0u;
        }

        if (cell < vmin) vmin = cell;
        if (cell > vmax) vmax = cell;
    }

    if ((uint16_t)(vmax - vmin) > BMS_BALANCE_SUSPECT_DELTA_MV) return 0u;
    if (g_bms_report.u16VCellMin != vmin ||
        g_bms_report.u16VCellMax != vmax ||
        g_bms_report.u16VCellDelta != (uint16_t)(vmax - vmin))
        return 0u;

    for (i = 0u; i < s->cell_count; ++i)
        s_feature.balance_prev_cell_mv[i] = g_bms_report.u16VCell[i];
    s_feature.balance_prev_cell_count = s->cell_count;
    return 1u;
}

/* 更新均衡电压可信度及连续确认状态。 */
static void update_balance_voltage_trust(const bms_afe_feature_snapshot_t *s)
{
    uint16_t required = (uint16_t)BMS_BALANCE_TRUST_CONFIRM_SAMPLES;

    if (required == 0u) required = 1u;

    if (!balance_sample_plausible(s))
    {
        s_feature.balance_voltage_trusted = 0u;
        s_feature.balance_trust_samples = 0u;
        s_feature.balance_active = 0u;
        s_feature.openwire_suspected = 1u;
        return;
    }

    if (s_feature.openwire_suspected || s_feature.openwire_fault_latched)
    {
        s_feature.balance_voltage_trusted = 0u;
        s_feature.balance_trust_samples = 0u;
        return;
    }

    if (s_feature.balance_trust_samples < required)
        ++s_feature.balance_trust_samples;
    s_feature.balance_voltage_trusted =
        (s_feature.balance_trust_samples >= required) ? 1u : 0u;
}

/* 检查温度有效性与均衡允许温区。 */
static uint8_t balance_temperature_safe(const bms_afe_feature_snapshot_t *s)
{
    uint16_t charge_ot_recover;
    uint16_t charge_ut_recover;
    uint16_t mos_ot_recover;

    if ((s == 0) || !s->battery_temp_valid || !s->mos_temp_valid) return 0u;

    /*
     * 均衡会产生热量，使用既有保护恢复边界作为保守准入窗口，
     * 不依赖零电流时可能清除的方向性故障位；阈值零表示相应保护关闭。
     */
    charge_ot_recover = g_bms_protection_params.u16TChgOTp_Rcv;
    charge_ut_recover = g_bms_protection_params.u16TchgUTp_Rcv;
    mos_ot_recover = g_bms_protection_params.u16TmosOTp_Rcv;

    if ((charge_ot_recover != 0u) &&
        (s->battery_temp_max_x10 >= charge_ot_recover))
        return 0u;
    if ((charge_ut_recover != 0u) &&
        (s->battery_temp_min_x10 <= charge_ut_recover))
        return 0u;
    if ((mos_ot_recover != 0u) &&
        (s->mos_temp_x10 >= mos_ot_recover))
        return 0u;
    return 1u;
}

/* 检查必须关闭均衡的硬故障条件。 */
static uint8_t balance_hard_fault(void)
{
    const bms_fault_bits_t *f = &g_bms_report.unMdlFault_Third.bits;

    /* 有意不列入单体过压：充电已阻断时，确认的被动泄放是高单体的合法恢复路径。 */
    return (!bms_protection_params_valid() ||
            bms_error_get(BMS_ERROR_AFE1) ||
            bms_error_get(BMS_ERROR_TEMP_BREAK) ||
            bms_error_get(BMS_ERROR_DSG_SHORT) ||
            bms_error_get(BMS_ERROR_CBC_DSG) ||
            f->b1CellUvp || f->b1BatUvp || f->b1BatOvp ||
            f->b1IchgOcp || f->b1IdischgOcp ||
            f->b1CellChgOtp || f->b1CellChgUtp ||
            f->b1CellDischgOtp || f->b1CellDischgUtp ||
            f->b1TmosOtp) ? 1u : 0u;
}

/*
 * 统一均衡资格与 mask 仲裁；只使用有效 cell，
 * 失效测量和硬故障应通过现有路径停止均衡。
 */
static void service_balance(const bms_afe_feature_snapshot_t *s)
{
    bms_user_params_t config;
    uint16_t threshold;
    uint32_t desired = 0u;
    uint8_t i;
    uint8_t allowed;

    allowed = (uint8_t)((s != 0) && s->valid &&
                        bms_config_get_user(&config) &&
                        config.balance_enable &&
                        bms_board_balance_supported() &&
                        s_feature.balance_voltage_trusted &&
                        !s_feature.openwire_active &&
                        !s_feature.openwire_fault_latched &&
                        !s_feature.openwire_suspected &&
                        (s_feature.heater_state == BMS_HEATER_IDLE) &&
                        s_feature.charge_session_active &&
                        balance_temperature_safe(s) &&
                        !balance_hard_fault());

    if (allowed)
    {
        threshold = s_feature.balance_active ?
                    config.balance_stop_delta_mv :
                    config.balance_start_delta_mv;

        if (g_bms_report.u16VCellMax >= config.balance_start_mv &&
            g_bms_report.u16VCellDelta >= threshold)
        {
            for (i = 0u; i < s->cell_count && i < BMS_AFE_FEATURE_MAX_CELLS; ++i)
            {
                uint16_t cell = g_bms_report.u16VCell[i];
                if (cell >= config.balance_start_mv &&
                    cell >= g_bms_report.u16VCellMin &&
                    (uint16_t)(cell - g_bms_report.u16VCellMin) >= threshold)
                    desired |= (1uL << i);
            }
        }
    }

    if (apply_balance_mask(desired))
        s_feature.balance_active = desired ? 1u : 0u;
    else
        s_feature.balance_active = 0u;
}

/* 复位加热、均衡和断线检测的公共状态。 */
void bms_features_init(void)
{
    memset(&s_feature, 0, sizeof(s_feature));
    bms_sw_protection_init();
    s_feature.openwire_cooldown_samples = (uint16_t)BMS_OPENWIRE_FIRST_IDLE_SAMPLES;
    bms_board_features_init();
    bms_board_heater_set(0u);
    g_bms_system_status.bits.b1Status_Heat = 0u;
    publish_balance(0u);
}

/* 按有效快照推进加热、断线检测及均衡策略。 */
void bms_features_service(void)
{
    bms_afe_feature_snapshot_t s;

    /* 轮询可能在本帧采样后完成 COW 并清 active；下次采样前不能把该诊断电压交给 SOC。 */
    s_feature.openwire_sample_active = s_feature.openwire_active;
    memset(&s, 0, sizeof(s));
    if (!bms_afe_get_feature_snapshot(&s) || !s.valid)
    {
        bms_features_on_afe_invalid();
        return;
    }

    update_charge_session();
    update_balance_voltage_trust(&s);
    service_openwire();
    service_heater(&s);
    service_balance(&s);
}

/* AFE 样本失效时撤销功能资格并停止相关输出。 */
void bms_features_on_afe_invalid(void)
{
    heater_idle();
    s_feature.heater_off_hot_samples = 0u;
    s_feature.charge_session_active = 0u;
    s_feature.balance_requested_mask = 0u;
    s_feature.balance_active = 0u;
    s_feature.balance_voltage_trusted = 0u;
    s_feature.balance_trust_samples = 0u;
    s_feature.balance_prev_cell_count = 0u;
    s_feature.openwire_active = 0u;
    s_feature.openwire_suspected = 1u;
    s_feature.openwire_idle_samples = 0u;

    /*
     * AFE 总线失效时物理均衡状态未知；不能把期望关闭状态当成硬件反馈。
     * 保留最后回读值至通信恢复，DVC 独立均衡定时器仍作硬件回退。
     */
    if ((g_bms_report.u16BalanceFlag1 != 0u) ||
        (g_bms_report.u16BalanceFlag2 != 0u))
    {
        if (!bms_error_get(BMS_ERROR_BALANCE))
            bms_error_raise(BMS_ERROR_BALANCE);
    }

    if (s_feature.heater_fuse_fired && !bms_error_get(BMS_ERROR_HEAT))
        bms_error_raise(BMS_ERROR_HEAT);
}

/* 复制 feature 所有者的 RAM 状态，不额外读取总线或推进状态机。 */
void bms_features_get_status(bms_features_status_t *status)
{
    if (status == 0) return;
    status->heater_on = s_feature.heater_on;
    status->heater_fuse_fired = s_feature.heater_fuse_fired;
    status->heater_state = s_feature.heater_state;
    status->charge_session_active = s_feature.charge_session_active;
    status->balance_active = s_feature.balance_active;
    status->balance_voltage_trusted = s_feature.balance_voltage_trusted;
    status->openwire_suspected = s_feature.openwire_suspected;
    status->openwire_active = s_feature.openwire_active;
    status->openwire_sample_active =
        (s_feature.openwire_active || s_feature.openwire_sample_active) ? 1u : 0u;
}

/* 查询配置无效或断线检测造成的双向硬性阻断。 */
uint8_t bms_features_outputs_blocked(void)
{
    return (!bms_protection_params_valid() ||
            s_feature.openwire_active ||
            s_feature.openwire_fault_latched) ? 1u : 0u;
}

/* 查询充电方向相关的公共功能阻断。 */
uint8_t bms_features_charge_direction_blocked(void)
{
    return (s_feature.heater_state == BMS_HEATER_ARMING ||
            s_feature.heater_state == BMS_HEATER_ACTIVE) ? 1u : 0u;
}

/* 编码加热、均衡和断线检测的阻断原因。 */
uint32_t bms_features_diag_reasons(uint8_t charge)
{
    uint32_t reason = 0u;
    if (s_feature.openwire_active || s_feature.openwire_fault_latched)
        reason |= DIAG_BLOCK_OPENWIRE;
    if (charge && bms_features_charge_direction_blocked())
        reason |= DIAG_BLOCK_HEATER;
    return reason;
}
