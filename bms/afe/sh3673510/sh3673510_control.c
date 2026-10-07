/*
 * 文件功能：SH3673510 寄存器控制与硬件保护量化；执行配置验证、MOS/均衡、Sleep/Wake
 * 等器件流程。
 * bms/afe/sh3673510/sh3673510_control.c；实际编译归属见各产品 sources.txt。
 */
/* 所选 SH 产品 10K NTC 表：电阻单位 100 Ω，温度编码 (C+40)*10。 */
#include "sh3673510_ntc.h"
#include "tl_common.h"
#include "drivers.h"
#include "bms_product.h"
#include "bms_afe_backend.h"
#include "bms_parameters.h"
#include "bms_afe_hw_profile.h"
#include "sh3673520.h"
#include "sh3673520_port.h"
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#include "sh3673510_quantize.h"

static uint8_t s_control_ready;
static uint8_t s_afe_sleeping;
static sh3673510_protection_actual_t s_protection_actual;


/* 配置 SH 板级 GPIO 为输入并设置上下拉。 */
static void sh3510_gpio_input(GPIO_PinTypeDef pin)
{
    gpio_set_func(pin, AS_GPIO);
    gpio_set_output_en(pin, 0);
    gpio_set_input_en(pin, 1);
}

#if SH3673510_PRODUCT_HEATER_SUPPORTED
/* 先置低再配置 GPIO 输出，避免初始化高脉冲。 */
static void sh3510_gpio_output_low(GPIO_PinTypeDef pin)
{
    gpio_set_func(pin, AS_GPIO);
    gpio_write(pin, 0);
    gpio_set_input_en(pin, 0);
    gpio_set_output_en(pin, 1);
}
#endif

/* 按字段掩码读改写 SH 寄存器。 */
static uint8_t sh3510_update_reg(uint8_t reg, uint8_t mask, uint8_t bits)
{
    uint8_t value;
    uint8_t target;
    uint8_t verify;

    if (SH3673520_ReadReg(reg, &value) != SH3673520_OK) return 0u;
    target = (uint8_t)((value & (uint8_t)~mask) | (bits & mask));
    if (target != value)
    {
        if (SH3673520_WriteReg(reg, target) != SH3673520_OK) return 0u;
    }
    if (SH3673520_ReadReg(reg, &verify) != SH3673520_OK) return 0u;
    return ((verify & mask) == (target & mask)) ? 1u : 0u;
}

/* 写入寄存器并回读确认实际值。 */
static uint8_t sh3510_write_verify(uint8_t reg, uint8_t value, uint8_t mask)
{
    uint8_t verify;
    if (SH3673520_WriteReg(reg, value) != SH3673520_OK) return 0u;
    if (SH3673520_ReadReg(reg, &verify) != SH3673520_OK) return 0u;
    return ((verify & mask) == (value & mask)) ? 1u : 0u;
}

static const uint16_t s_ov_delay_ms[8] = {
    140u, 280u, 490u, 980u, 2030u, 3010u, 4970u, 10010u
};
static const uint16_t s_uv_delay_ms[8] = {
    490u, 770u, 980u, 1470u, 2030u, 3010u, 4970u, 10010u
};

/* 从硬件可表示值中选择向上量化编码。 */
static uint8_t sh3510_pick_ceiling_code(const uint16_t *table, uint8_t count,
                                         uint32_t requested)
{
    uint8_t i;
    if ((table == 0) || (count == 0u)) return 0u;
    for (i = 0u; i < count; ++i)
    {
        if (requested <= (uint32_t)table[i]) return i;
    }
    return (uint8_t)(count - 1u);
}

/* 将温度阈值转换为 100 Ω 单位电阻。 */
static uint16_t sh3510_temp_to_res100(uint16_t temp_x10)
{
    uint8_t i;
    const uint8_t pairs = (uint8_t)(sizeof(sh3673510_ntc_10k) / sizeof(sh3673510_ntc_10k[0]) / 2u);

    if (temp_x10 <= sh3673510_ntc_10k[1]) return sh3673510_ntc_10k[0];
    if (temp_x10 >= sh3673510_ntc_10k[(pairs - 1u) * 2u + 1u])
        return sh3673510_ntc_10k[(pairs - 1u) * 2u];

    for (i = 0u; i + 1u < pairs; ++i)
    {
        uint16_t r1 = sh3673510_ntc_10k[i * 2u];
        uint16_t t1 = sh3673510_ntc_10k[i * 2u + 1u];
        uint16_t r2 = sh3673510_ntc_10k[(i + 1u) * 2u];
        uint16_t t2 = sh3673510_ntc_10k[(i + 1u) * 2u + 1u];
        if (temp_x10 >= t1 && temp_x10 <= t2)
        {
            uint32_t dt = (uint32_t)(temp_x10 - t1);
            uint32_t span = (uint32_t)(t2 - t1);
            uint32_t drop = ((uint32_t)(r1 - r2) * dt + span / 2u) / span;
            return (uint16_t)((uint32_t)r1 - drop);
        }
    }
    return 100u;
}

/* 量化高温保护阈值编码。 */
static uint8_t sh3510_high_temp_code(uint16_t temp_x10, uint8_t *code)
{
    uint32_t denominator;
    uint32_t result;
    uint16_t r100;

    if (code == 0) return 0u;
    r100 = sh3510_temp_to_res100(temp_x10);
    denominator = (uint32_t)r100 + 100u;

    /*
     * SH36735xx 外部 NTC 分压参考为 10K。高温阈值直接使用分压比例：
     * code = Rntc / (10K + Rntc) * 512；r100 和 100 均以 100 Ω 为单位。
     */
    result = ((uint32_t)r100 * 512u + (denominator / 2u)) / denominator;
    if (result > 255u) return 0u;
    *code = (uint8_t)result;
    return 1u;
}

/* 量化低温保护阈值编码。 */
static uint8_t sh3510_low_temp_code(uint16_t temp_x10, uint8_t *code)
{
    int32_t numerator;
    uint32_t denominator;
    uint16_t r100;
    int32_t result;

    if (code == 0) return 0u;
    r100 = sh3510_temp_to_res100(temp_x10);
    if (r100 < 100u) return 0u;
    denominator = (uint32_t)r100 + 100u;
    numerator = (int32_t)r100 - 100L;
    result = (numerator * 256L + (int32_t)(denominator / 2u)) /
             (int32_t)denominator;
    if (result < 0L || result > 255L) return 0u;
    *code = (uint8_t)result;
    return 1u;
}

/* 把独立硬件 profile 量化为本芯片寄存器并验证；effective 值用于报告真实可表示阈值。 */
uint8_t sh3673510_control_apply_protection(void)
{
    bms_afe_hw_profile_t hw;
    uint32_t ov_delay_ms;
    uint32_t uv_delay_ms;
    uint32_t ocd1_delay_ms;
    uint32_t ocd2_delay_ms;
    uint32_t occ_delay_ms;
    uint16_t ov_code;
    uint16_t uv_code;
    uint16_t actual_a10;
    uint8_t ov_dly;
    uint8_t uv_dly;
    uint8_t regv;
    uint8_t high;
    uint8_t low;
    uint8_t code;
    uint8_t ok = 1u;

    s_protection_actual.valid = 0u;
    if (!s_control_ready || !bms_afe_hw_profile_get(&hw)) return 0u;
    ov_delay_ms = hw.cov_delay_ms;
    uv_delay_ms = hw.cuv_delay_ms;
    ocd1_delay_ms = hw.ocd1_delay_ms;
    ocd2_delay_ms = hw.ocd2_delay_ms;
    occ_delay_ms = hw.occ1_delay_ms;

    ov_code = (uint16_t)(((uint32_t)hw.cov_mv + 2u) / 5u);
    uv_code = (uint16_t)(((uint32_t)hw.cuv_mv + 2u) / 5u);
    if (ov_code > 0x03FFu) ov_code = 0x03FFu;
    if (uv_code > 0x03FFu) uv_code = 0x03FFu;
    ov_dly = sh3510_pick_ceiling_code(s_ov_delay_ms, 8u, ov_delay_ms);
    uv_dly = sh3510_pick_ceiling_code(s_uv_delay_ms, 8u, uv_delay_ms);

    high = (uint8_t)((ov_dly << 4) | ((ov_code >> 8) & 0x03u));
    low = (uint8_t)(ov_code & 0xFFu);
    ok &= sh3510_write_verify(SH3673520_REG_OVT_OVH, high, 0x73u);
    ok &= sh3510_write_verify(SH3673520_REG_OVL, low, 0xFFu);

    high = (uint8_t)((uv_dly << 4) | ((uv_code >> 8) & 0x03u));
    low = (uint8_t)(uv_code & 0xFFu);
    ok &= sh3510_write_verify(SH3673520_REG_UVT_UVH, high, 0x73u);
    ok &= sh3510_write_verify(SH3673520_REG_UVL, low, 0xFFu);

    actual_a10 = sh3673510_quantize_current_a10(hw.ocd1_a10,
        SH3673510_BOARD_SHUNT_UOHM, 5000u, 15u, &code);
    regv = (uint8_t)((sh3510_pick_ceiling_code(s_ov_delay_ms, 8u, ocd1_delay_ms) << 4) | code);
    ok &= sh3510_write_verify(SH3673520_REG_OCD1V_OCD1T, regv, 0x7Fu);
    s_protection_actual.ocd1_a10 = actual_a10;
    s_protection_actual.ocd1_delay_ms = s_ov_delay_ms[(regv >> 4) & 0x07u];

    actual_a10 = sh3673510_quantize_current_a10(hw.ocd2_a10,
        SH3673510_BOARD_SHUNT_UOHM, 10000u, 15u, &code);
    {
        uint32_t steps = (ocd2_delay_ms + 24u) / 25u;
        uint8_t dly;
        if (steps == 0u) steps = 1u;
        if (steps > 16u) steps = 16u;
        dly = (uint8_t)(steps - 1u);
        regv = (uint8_t)((dly << 4) | code);
    }
    ok &= sh3510_write_verify(SH3673520_REG_OCD2V_OCD2T, regv, 0xFFu);
    s_protection_actual.ocd2_a10 = actual_a10;
    s_protection_actual.ocd2_delay_ms = (uint16_t)((((regv >> 4) & 0x0Fu) + 1u) * 25u);

    {
        static const uint8_t sc_mult[4] = {2u, 3u, 4u, 6u};
        /* SH36735XX V1.0A/V0.2C p37–38：SCT 是完整四位非等距表。 */
        static const uint16_t sc_delay[16] = {
            0u, 32u, 64u, 96u, 128u, 192u, 224u, 256u,
            288u, 320u, 384u, 448u, 480u, 512u, 544u, 576u
        };
        uint8_t mult_code = 0u;
        uint8_t delay_code = 0u;
        uint32_t base = s_protection_actual.ocd2_a10;
        if ((hw.enable_mask & BMS_AFE_HW_EN_SC) && base != 0u) {
            while (mult_code < 3u && (u32)base * sc_mult[mult_code] < hw.sc_a10) ++mult_code;
            while (delay_code < 15u && sc_delay[delay_code] < hw.sc_delay_us) ++delay_code;
        }
        regv = (uint8_t)((mult_code << 4) | delay_code);
        ok &= sh3510_write_verify(SH3673520_REG_SCV_SCT, regv, 0x3Fu);
        base *= sc_mult[mult_code];
        /* 旧参数仍加载，但不能把超出报告位宽的实际阈值回绕成低电流。 */
        if (base > 65535u && (hw.enable_mask & BMS_AFE_HW_EN_SC)) ok = 0u;
        s_protection_actual.sc_a10 = (uint16_t)((base > 65535u) ? 65535u : base);
        s_protection_actual.sc_delay_us = sc_delay[delay_code];
    }

    actual_a10 = sh3673510_quantize_current_a10(hw.occ1_a10,
        SH3673510_BOARD_SHUNT_UOHM, 1375u, 31u, &code);
    regv = (uint8_t)((sh3510_pick_ceiling_code(s_ov_delay_ms, 8u, occ_delay_ms) << 5) | code);
    ok &= sh3510_write_verify(SH3673520_REG_OCCV_OCCT, regv, 0xFFu);
    s_protection_actual.occ_a10 = actual_a10;
    s_protection_actual.occ_delay_ms = s_ov_delay_ms[(regv >> 5) & 0x07u];

    if (!sh3510_high_temp_code(hw.chg_ot_x10, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_OTC, code, 0xFFu);
    if (!sh3510_high_temp_code(hw.dsg_ot_x10, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_OTD, code, 0xFFu);
    if (!sh3510_low_temp_code(hw.chg_ut_x10, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_UTC, code, 0xFFu);
    if (!sh3510_low_temp_code(hw.dsg_ut_x10, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_UTD, code, 0xFFu);

    /* 硬件保护使能归独立 AFE 配置负责。 */
    ok &= sh3510_update_reg(SH3673520_REG_SCONF5,
                            SH3673520_SCONF5_OCC_EN_MASK,
                            (hw.enable_mask & BMS_AFE_HW_EN_OCC1) ? SH3673520_SCONF5_OCC_EN_MASK : 0u);
    regv = 0u;
    if (hw.enable_mask & BMS_AFE_HW_EN_COV)  regv |= SH3673520_SCONF6_OV_EN_MASK;
    if (hw.enable_mask & BMS_AFE_HW_EN_CUV)  regv |= SH3673520_SCONF6_UV_EN_MASK;
    if (hw.enable_mask & (BMS_AFE_HW_EN_OCD1 | BMS_AFE_HW_EN_OCD2)) regv |= SH3673520_SCONF6_OCD_EN_MASK;
    if (hw.enable_mask & BMS_AFE_HW_EN_SC)   regv |= SH3673520_SCONF6_SC_EN_MASK;
    if (hw.enable_mask & BMS_AFE_HW_EN_TEMP) regv |= (SH3673520_SCONF6_TS1_EN_MASK | SH3673520_SCONF6_TS2_EN_MASK);
    ok &= sh3510_write_verify(SH3673520_REG_SCONF6, regv, SH3673520_SCONF6_ALL_MASK);

    s_protection_actual.ov_mv = (uint16_t)(ov_code * 5u);
    s_protection_actual.uv_mv = (uint16_t)(uv_code * 5u);
    s_protection_actual.ov_delay_ms = s_ov_delay_ms[ov_dly];
    s_protection_actual.uv_delay_ms = s_uv_delay_ms[uv_dly];
    s_protection_actual.valid = ok ? 1u : 0u;
    return ok ? 1u : 0u;
}

/* 取得 SH 硬件实际可表示的保护配置。 */
uint8_t sh3673510_control_get_protection_actual(sh3673510_protection_actual_t *actual)
{
    if (actual == 0) return 0u;
    *actual = s_protection_actual;
    return s_protection_actual.valid;
}

typedef struct {
    uint8_t reg;
    uint8_t value;
    uint8_t verify_mask;
} sh3510_static_reg_cfg_t;

/*
 * 所选产品使用确定性的静态 AFE 配置。
 * 0x49..0x54 保护阈值独立于 g_bms_protection_params 应用；阈值有效后才写 SCONF6，
 * 避免对意外复位阈值启用硬件保护。
 */
static const sh3510_static_reg_cfg_t s_static_reg_cfg[] = {
    { SH3673520_REG_SCONF1,     SH3673510_BOARD_SCONF1_BOOT_VALUE, 0xFFu },
    { SH3673520_REG_SCONF2,     SH3673510_BOARD_SCONF2_VALUE,      SH3673520_SCONF2_ALL_MASK },
    { SH3673520_REG_SCONF3,     SH3673510_BOARD_SCONF3_VALUE,      SH3673520_SCONF3_CONFIG_MASK },
    { SH3673520_REG_SCONF4,     SH3673510_BOARD_SCONF4_VALUE,      SH3673520_SCONF4_ALL_MASK },
    { SH3673520_REG_SCONF5,     SH3673510_BOARD_SCONF5_VALUE,      SH3673520_SCONF5_CONFIG_MASK },
    { SH3673520_REG_SCONF7,     SH3673510_BOARD_SCONF7_VALUE,      SH3673520_SCONF7_CONFIG_MASK },
    { SH3673520_REG_OWV_ALARMH, SH3673510_BOARD_OWV_ALARMH_VALUE,  SH3673520_ALARMH_ALL_MASK },
    { SH3673520_REG_ALARML,     SH3673510_BOARD_ALARML_VALUE,      SH3673520_ALARML_ALL_MASK },
};

/* 配置 SH 工作模式、采样和固定板级字段。 */
static uint8_t sh3510_configure_runtime(void)
{
    uint8_t i;

    for (i = 0u; i < (uint8_t)(sizeof(s_static_reg_cfg) / sizeof(s_static_reg_cfg[0])); ++i)
    {
        if (!sh3510_write_verify(s_static_reg_cfg[i].reg,
                                 s_static_reg_cfg[i].value,
                                 s_static_reg_cfg[i].verify_mask))
            return 0u;
    }

    return (SH3673520_SetBalanceMask(0u, SH3673510_BOARD_CELL_COUNT) == SH3673520_OK) ? 1u : 0u;
}

/* 初始化 SH 控制层并验证固定配置。 */
uint8_t sh3673510_control_init(void)
{
    sh3673520_port_status_t port_status;

    s_control_ready = 0u;
    s_afe_sleeping = 0u;

    /* RESET 和 ALARM 是 AFE 开漏输出，MCU 绝不能驱动为输出。 */
    sh3510_gpio_input(BMS_BOARD_AFE_RESET_OUT_PIN);
    sh3510_gpio_input(BMS_BOARD_AFE_ALARM_PIN);
    sh3510_gpio_input(BMS_BOARD_INT_WK_MCU_PIN);

#if SH3673510_PRODUCT_HEATER_SUPPORTED
    sh3510_gpio_output_low(BMS_BOARD_HEATER_CHG_PIN);
    sh3673510_board_force_heater_fuse_safe();
#endif

    /* 板级唤醒高有效，AFE 告警/复位脉冲低有效。 */
    cpu_set_gpio_wakeup(BMS_BOARD_INT_WK_MCU_PIN, Level_High, 1);
    cpu_set_gpio_wakeup(BMS_BOARD_AFE_ALARM_PIN, Level_Low, 1);
    cpu_set_gpio_wakeup(BMS_BOARD_AFE_RESET_OUT_PIN, Level_Low, 1);

    port_status = SH3673520_PortConfigure(SH3673510_BOARD_SPI_GROUP);
    if (port_status != SH3673520_PORT_OK) return 0u;
    if (SH3673520_Init() != SH3673520_OK) return 0u;

    s_control_ready = 1u;
    if (!sh3510_configure_runtime())
    {
        s_control_ready = 0u;
        return 0u;
    }
    if (!sh3673510_control_apply_protection())
    {
        s_control_ready = 0u;
        return 0u;
    }
    if (!sh3673510_control_set_fets(0u, 0u))
    {
        s_control_ready = 0u;
        return 0u;
    }
    return 1u;
}

/* 根据请求设置充放电 FET 控制位。 */
uint8_t sh3673510_control_set_fets(uint8_t charge_on, uint8_t discharge_on)
{
    uint8_t bits = 0u;
    if (!s_control_ready) return 0u;
    if ((charge_on || discharge_on) && !s_protection_actual.valid) return 0u;
    if (charge_on) bits |= SH3673520_SCONF2_CHGMOS_MASK;
    if (discharge_on) bits |= SH3673520_SCONF2_DSGMOS_MASK;
    return sh3510_update_reg(SH3673520_REG_SCONF2,
                             SH3673520_SCONF2_FET_MASK,
                             bits);
}

/* 读取 SH 状态、保护标志及控制寄存器。 */
uint8_t sh3673510_control_read_status(sh3673510_control_status_t *status)
{
    uint8_t pair[2];
    if ((status == 0) || !s_control_ready) return 0u;

    if (SH3673520_ReadReg(SH3673520_REG_FLAG1, &status->flag1) != SH3673520_OK)
        return 0u;
    /* 集中进行有意的 FLAG2 读取，会消耗 VADC/CADC 就绪标志。 */
    if (SH3673520_ReadReg(SH3673520_REG_FLAG2, &status->flag2) != SH3673520_OK)
        return 0u;
    if (SH3673520_ReadRegs(SH3673520_REG_BSTATUS1, pair, 2u) != SH3673520_OK)
        return 0u;
    status->bstatus1 = pair[0];
    status->bstatus2 = pair[1];
    return 1u;
}

/* 切换负载检测模式，供物理恢复证据采集。 */
uint8_t sh3673510_control_set_load_detection(uint8_t enabled, uint8_t *changed)
{
    uint8_t value, verify, target;
    uint8_t bits = enabled ? SH3673520_SCONF3_CRLD_LOAD : SH3673520_SCONF3_CRLD_CPLUS;
    if (!s_control_ready || s_afe_sleeping || changed == 0) return 0u;
    *changed = 0u;
    if (SH3673520_ReadReg(SH3673520_REG_SCONF3, &value) != SH3673520_OK) return 0u;
    /* p16/p32：TRG 是自清命令；切换检测模式不重发旧触发，只验证稳定配置位。 */
    target = (uint8_t)((value & (uint8_t)~(SH3673520_SCONF3_CRLD_EN_MASK |
                         SH3673520_SCONF3_OWD_TRG_MASK)) | bits);
    if ((target & (SH3673520_SCONF3_CONFIG_MASK & (uint8_t)~SH3673520_SCONF3_OWD_TRG_MASK)) !=
        (value & (SH3673520_SCONF3_CONFIG_MASK & (uint8_t)~SH3673520_SCONF3_OWD_TRG_MASK))) {
        if (SH3673520_WriteReg(SH3673520_REG_SCONF3, target) != SH3673520_OK) return 0u;
        *changed = 1u;
    }
    if (SH3673520_ReadReg(SH3673520_REG_SCONF3, &verify) != SH3673520_OK) return 0u;
    return ((verify & (SH3673520_SCONF3_CONFIG_MASK &
                       (uint8_t)~SH3673520_SCONF3_OWD_TRG_MASK)) ==
            (target & (SH3673520_SCONF3_CONFIG_MASK &
                       (uint8_t)~SH3673520_SCONF3_OWD_TRG_MASK))) ? 1u : 0u;
}

/* 按允许清除位更新保护标志寄存器。 */
static uint8_t sh3510_clear_flags(uint8_t reg, uint8_t clear_mask)
{
    uint8_t sconf2;
    uint8_t value;
    uint8_t ok = 1u;

    if (clear_mask == 0u) return 1u;
    if (!s_control_ready) return 0u;
    if (SH3673520_ReadReg(SH3673520_REG_SCONF2, &sconf2) != SH3673520_OK) return 0u;
    if (!sh3510_update_reg(SH3673520_REG_SCONF2,
                           SH3673520_SCONF2_LTCLR_MASK,
                           SH3673520_SCONF2_LTCLR_MASK)) return 0u;

    value = (uint8_t)~clear_mask; /* W0C：0 清选定位，1 保留。 */
    if (SH3673520_WriteReg(reg, value) != SH3673520_OK) ok = 0u;

    if (!sh3510_update_reg(SH3673520_REG_SCONF2,
                           SH3673520_SCONF2_LTCLR_MASK,
                           (uint8_t)(sconf2 & SH3673520_SCONF2_LTCLR_MASK))) ok = 0u;
    return ok;
}

/* 清除指定 FLAG1 保护标志并检查结果。 */
uint8_t sh3673510_control_clear_flag1(uint8_t clear_mask)
{
    return sh3510_clear_flags(SH3673520_REG_FLAG1, clear_mask);
}

/* 清除指定 FLAG2 保护标志并检查结果。 */
uint8_t sh3673510_control_clear_flag2(uint8_t clear_mask)
{
    /* W0C 写入不得清除读清除 ADC 就绪位。 */
    clear_mask &= (uint8_t)~(SH3673520_FLAG2_VADC_MASK | SH3673520_FLAG2_CADC_MASK);
    return sh3510_clear_flags(SH3673520_REG_FLAG2, clear_mask);
}

/* 写入有效电芯通道的均衡掩码。 */
uint8_t sh3673510_control_set_balance(uint16_t cell_mask)
{
    if (!s_control_ready) return 0u;
    return (SH3673520_SetBalanceMask((uint32_t)(cell_mask & 0x03FFu),
                                     SH3673510_BOARD_CELL_COUNT) == SH3673520_OK) ? 1u : 0u;
}

/* 将支持的加热/熔断引脚保持安全电平。 */
void sh3673510_board_force_heater_fuse_safe(void)
{
#if SH3673510_PRODUCT_HEATER_SUPPORTED
    gpio_set_func(BMS_BOARD_HEATER_FUSE_TRIGGER_PIN, AS_GPIO);
    gpio_write(BMS_BOARD_HEATER_FUSE_TRIGGER_PIN, BMS_BOARD_HEATER_FUSE_SAFE_LEVEL);
    gpio_set_input_en(BMS_BOARD_HEATER_FUSE_TRIGGER_PIN, 0);
    gpio_set_output_en(BMS_BOARD_HEATER_FUSE_TRIGGER_PIN, 1);
#endif
}

/* 仅在产品支持时驱动板级加热输出。 */
void sh3673510_board_set_heater(uint8_t enabled)
{
#if SH3673510_PRODUCT_HEATER_SUPPORTED
    /* 只有声明加热能力的产品才编译加热实现。 */
    sh3673510_board_force_heater_fuse_safe();
    gpio_write(BMS_BOARD_HEATER_CHG_PIN, enabled ? 1u : 0u);
#else
    (void)enabled;
#endif
}

/* 查询板级 AFE 唤醒信号状态。 */
uint8_t sh3673510_board_wake_active(void)
{
    return gpio_read(BMS_BOARD_INT_WK_MCU_PIN) ? 1u : 0u;
}

/* 关闭相关输出并按器件流程进入休眠。 */
uint8_t sh3673510_control_sleep(void)
{
    if (!s_control_ready) return 0u;
    if (sh3673510_board_wake_active()) return 0u;

#if SH3673510_PRODUCT_HEATER_SUPPORTED
    sh3673510_board_set_heater(0u);
    sh3673510_board_force_heater_fuse_safe();
#endif
    if (!sh3673510_control_set_balance(0u)) return 0u;
    if (!sh3673510_control_set_fets(0u, 0u)) return 0u;

    if (!sh3510_update_reg(SH3673520_REG_SCONF3,
                            SH3673520_SCONF3_CGR_WK_MASK,
                            SH3673520_SCONF3_CGR_WK_MASK)) return 0u;
    /* 应答丢失不能证明 AFE 拒绝 SLEEP；下次测量前强制 NORMAL 并恢复运行/保护配置。 */
    s_afe_sleeping = 1u;
    return (SH3673520_WriteReg(SH3673520_REG_SCONF1, SH3673520_SCONF1_SLEEP) ==
            SH3673520_OK) ? 1u : 0u;
}

/* 执行 SH 唤醒并重建配置、验证状态。 */
uint8_t sh3673510_control_wake(void)
{
    if (!s_control_ready) return 0u;
    if (!s_afe_sleeping) return 1u;
    if (SH3673520_WriteReg(SH3673520_REG_SCONF1, SH3673520_SCONF1_NORMAL) != SH3673520_OK)
        return 0u;
    sh3673520_port_delay_ms(10u);
    if (!sh3510_configure_runtime()) return 0u;
    if (!sh3673510_control_apply_protection()) return 0u;
    if (!sh3673510_control_set_fets(0u, 0u)) return 0u;
    s_afe_sleeping = 0u;
    return 1u;
}

/* 查询 SH 控制层的就绪状态。 */
uint8_t sh3673510_control_ready(void)
{
    return s_control_ready;
}
