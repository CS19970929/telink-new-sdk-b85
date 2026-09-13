#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "param.h"
#include "sh3673520.h"
#include "sh3673520_port.h"
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"

static uint8_t s_control_ready;
static uint8_t s_afe_sleeping;

/* Existing D011 product NTC table: resistance in 100ohm, temperature=(C+40)*10. */
static const uint16_t s_ntc_10k_table[] = {
    2037u, 0u, 1526u, 50u, 1161u, 100u, 893u, 150u,
    694u, 200u, 544u, 250u, 430u, 300u, 342u, 350u,
    275u, 400u, 221u, 450u, 180u, 500u, 147u, 550u,
    121u, 600u, 100u, 650u, 83u, 700u, 69u, 750u,
    58u, 800u, 49u, 850u, 41u, 900u, 35u, 950u,
    30u, 1000u, 26u, 1050u, 22u, 1100u, 19u, 1150u,
    16u, 1200u, 14u, 1250u, 12u, 1300u, 11u, 1350u,
    9u, 1400u, 8u, 1450u
};

static void sh3510_gpio_input(GPIO_PinTypeDef pin)
{
    gpio_set_func(pin, AS_GPIO);
    gpio_set_output_en(pin, 0);
    gpio_set_input_en(pin, 1);
}

static void sh3510_gpio_output_low(GPIO_PinTypeDef pin)
{
    gpio_set_func(pin, AS_GPIO);
    gpio_write(pin, 0);
    gpio_set_input_en(pin, 0);
    gpio_set_output_en(pin, 1);
}

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

static uint8_t sh3510_write_verify(uint8_t reg, uint8_t value, uint8_t mask)
{
    uint8_t verify;
    if (SH3673520_WriteReg(reg, value) != SH3673520_OK) return 0u;
    if (SH3673520_ReadReg(reg, &verify) != SH3673520_OK) return 0u;
    return ((verify & mask) == (value & mask)) ? 1u : 0u;
}

static uint8_t sh3510_delay_code(const uint16_t *table, uint8_t count,
                                 uint32_t requested_ms)
{
    uint8_t i;
    uint8_t selected = 0u;

    if ((table == 0) || (count == 0u)) return 0u;
    if (requested_ms <= table[0]) return 0u;
    for (i = 0u; i < count; ++i)
    {
        if ((uint32_t)table[i] <= requested_ms) selected = i;
        else break;
    }
    return selected;
}

static uint8_t sh3510_ov_delay_code(uint32_t requested_ms)
{
    static const uint16_t table[8] = {140u, 280u, 490u, 980u, 2030u, 3010u, 4970u, 10010u};
    return sh3510_delay_code(table, 8u, requested_ms);
}

static uint8_t sh3510_uv_delay_code(uint32_t requested_ms)
{
    static const uint16_t table[8] = {490u, 770u, 980u, 1470u, 2030u, 3010u, 4970u, 10010u};
    return sh3510_delay_code(table, 8u, requested_ms);
}

static uint16_t sh3510_temp_to_res100(uint16_t temp_x10)
{
    uint8_t i;
    const uint8_t pairs = (uint8_t)(sizeof(s_ntc_10k_table) / sizeof(s_ntc_10k_table[0]) / 2u);

    if (temp_x10 <= s_ntc_10k_table[1]) return s_ntc_10k_table[0];
    if (temp_x10 >= s_ntc_10k_table[(pairs - 1u) * 2u + 1u])
        return s_ntc_10k_table[(pairs - 1u) * 2u];

    for (i = 0u; i + 1u < pairs; ++i)
    {
        uint16_t r1 = s_ntc_10k_table[i * 2u];
        uint16_t t1 = s_ntc_10k_table[i * 2u + 1u];
        uint16_t r2 = s_ntc_10k_table[(i + 1u) * 2u];
        uint16_t t2 = s_ntc_10k_table[(i + 1u) * 2u + 1u];
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

static uint8_t sh3510_high_temp_code(uint16_t temp_x10, uint8_t *code)
{
    int32_t numerator;
    uint32_t denominator;
    uint16_t r100;
    int32_t result;

    if (code == 0) return 0u;
    r100 = sh3510_temp_to_res100(temp_x10);
    denominator = (uint32_t)r100 + 100u;
    /* code = (0.7 - RT/(10+RT))*512, RT represented in 100ohm units. */
    numerator = 700L - (3L * (int32_t)r100);
    if (numerator < 0L) return 0u;
    result = (numerator * 512L + (int32_t)(denominator * 5u)) /
             (int32_t)(denominator * 10u);
    if (result < 0L || result > 255L) return 0u;
    *code = (uint8_t)result;
    return 1u;
}

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
    /* code = (RT/(10+RT)-0.5)*512. */
    numerator = (int32_t)r100 - 100L;
    result = (numerator * 256L + (int32_t)(denominator / 2u)) /
             (int32_t)denominator;
    if (result < 0L || result > 255L) return 0u;
    *code = (uint8_t)result;
    return 1u;
}

static uint32_t sh3510_current_a10_to_sense_uv(uint16_t current_a10)
{
    /* A*10 -> mA = *100; uV = mA*uOhm/1000. */
    return ((uint32_t)current_a10 * SH3673510_D011_SHUNT_UOHM + 5u) / 10u;
}

static uint8_t sh3510_step_code_not_later(uint32_t requested_uv,
                                          uint32_t step_uv,
                                          uint8_t max_code)
{
    uint32_t steps;
    if (step_uv == 0u) return 0u;
    steps = requested_uv / step_uv;
    if (steps == 0u) steps = 1u;
    if (steps > (uint32_t)max_code + 1u) steps = (uint32_t)max_code + 1u;
    return (uint8_t)(steps - 1u);
}

uint8_t sh3673510_control_apply_protection(void)
{
    uint32_t ov_delay_ms = (uint32_t)g_tParam.protect.u16VcellOvp_Filter * 10u;
    uint32_t uv_delay_ms = (uint32_t)g_tParam.protect.u16VcellUvp_Filter * 10u;
    uint32_t ocd_delay_ms = (uint32_t)g_tParam.protect.u16IdsgOcp_Filter * 10u;
    uint32_t occ_delay_ms = (uint32_t)g_tParam.protect.u16IchgOcp_Filter * 10u;
    uint16_t ov_code;
    uint16_t uv_code;
    uint32_t sense_uv;
    uint8_t regv;
    uint8_t high;
    uint8_t low;
    uint8_t code;
    uint8_t ok = 1u;

    if (!s_control_ready) return 0u;

    ov_code = (uint16_t)(g_tParam.protect.u16VcellOvp_Third / 5u);
    uv_code = (uint16_t)(g_tParam.protect.u16VcellUvp_Third / 5u);
    if (ov_code > 0x03FFu) ov_code = 0x03FFu;
    if (uv_code > 0x03FFu) uv_code = 0x03FFu;

    high = (uint8_t)((sh3510_ov_delay_code(ov_delay_ms) << 4) |
                     ((ov_code >> 8) & 0x03u));
    low = (uint8_t)(ov_code & 0xFFu);
    ok &= sh3510_write_verify(SH3673520_REG_OVT_OVH, high, 0x73u);
    ok &= sh3510_write_verify(SH3673520_REG_OVL, low, 0xFFu);

    high = (uint8_t)((sh3510_uv_delay_code(uv_delay_ms) << 4) |
                     ((uv_code >> 8) & 0x03u));
    low = (uint8_t)(uv_code & 0xFFu);
    ok &= sh3510_write_verify(SH3673520_REG_UVT_UVH, high, 0x73u);
    ok &= sh3510_write_verify(SH3673520_REG_UVL, low, 0xFFu);

    sense_uv = sh3510_current_a10_to_sense_uv(g_tParam.protect.u16IdsgOcp_First);
    code = sh3510_step_code_not_later(sense_uv, 5000u, 15u);
    regv = (uint8_t)((sh3510_ov_delay_code(ocd_delay_ms) << 4) | code);
    ok &= sh3510_write_verify(SH3673520_REG_OCD1V_OCD1T, regv, 0x7Fu);

    sense_uv = sh3510_current_a10_to_sense_uv(g_tParam.protect.u16IdsgOcp_Second);
    code = sh3510_step_code_not_later(sense_uv, 10000u, 15u);
    {
        uint32_t steps = ocd_delay_ms / 25u;
        uint8_t dly;
        if (steps == 0u) steps = 1u;
        if (steps > 16u) steps = 16u;
        dly = (uint8_t)(steps - 1u);
        regv = (uint8_t)((dly << 4) | code);
    }
    ok &= sh3510_write_verify(SH3673520_REG_OCD2V_OCD2T, regv, 0xFFu);

    regv = (uint8_t)((SH3673510_D011_SC_MULTIPLIER_CODE << 4) |
                     SH3673510_D011_SC_DELAY_CODE);
    ok &= sh3510_write_verify(SH3673520_REG_SCV_SCT, regv, 0x3Fu);

    sense_uv = sh3510_current_a10_to_sense_uv(g_tParam.protect.u16IchgOcp_First);
    code = sh3510_step_code_not_later(sense_uv, 1375u, 31u);
    regv = (uint8_t)((sh3510_ov_delay_code(occ_delay_ms) << 5) | code);
    ok &= sh3510_write_verify(SH3673520_REG_OCCV_OCCT, regv, 0xFFu);

    if (!sh3510_high_temp_code(g_tParam.protect.u16TChgOTp_Third, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_OTC, code, 0xFFu);
    if (!sh3510_high_temp_code(g_tParam.protect.u16TdischgOTp_Third, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_OTD, code, 0xFFu);
    if (!sh3510_low_temp_code(g_tParam.protect.u16TchgUTp_Third, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_UTC, code, 0xFFu);
    if (!sh3510_low_temp_code(g_tParam.protect.u16TdischgUTp_Third, &code)) return 0u;
    ok &= sh3510_write_verify(SH3673520_REG_UTD, code, 0xFFu);

    /* Enable hardware OV/UV/OCD/SC and battery TS1/TS2 protections only.
     * TS3 is NC; TS4 is MOS temperature with a higher software threshold. */
    ok &= sh3510_update_reg(SH3673520_REG_SCONF6, 0xFFu,
                            (uint8_t)(SH3673520_SCONF6_TS2_EN_MASK |
                                      SH3673520_SCONF6_TS1_EN_MASK |
                                      SH3673520_SCONF6_ALL_PROTECT_MASK));

    return ok ? 1u : 0u;
}

static uint8_t sh3510_configure_runtime(void)
{
    uint8_t ok = 1u;

    ok &= sh3510_update_reg(SH3673520_REG_SCONF4,
                            SH3673520_SCONF4_CELL_COUNT_MASK,
                            SH3673510_D011_CELL_COUNT);

    ok &= sh3510_update_reg(SH3673520_REG_SCONF2,
                            (uint8_t)(SH3673520_SCONF2_PUMP_EN_MASK |
                                      SH3673520_SCONF2_PDSGMOS_MASK |
                                      SH3673520_SCONF2_FET_MASK),
                            SH3673520_SCONF2_PUMP_EN_MASK);

    ok &= sh3510_update_reg(SH3673520_REG_SCONF3,
                            (uint8_t)(SH3673520_SCONF3_CGR_WK_MASK |
                                      SH3673520_SCONF3_CRLD_EN_MASK |
                                      SH3673520_SCONF3_LD_WK_MASK),
                            (uint8_t)(SH3673520_SCONF3_CGR_WK_MASK |
                                      SH3673520_SCONF3_CRLD_CPLUS));

    ok &= sh3510_update_reg(SH3673520_REG_SCONF5,
                            (uint8_t)(SH3673520_SCONF5_MOS_EN_MASK |
                                      SH3673520_SCONF5_OCC_EN_MASK |
                                      SH3673520_SCONF5_CADC_EN_MASK |
                                      SH3673520_SCONF5_WDT_EN_MASK |
                                      SH3673520_SCONF5_WDT_MASK),
                            (uint8_t)(SH3673520_SCONF5_MOS_EN_MASK |
                                      SH3673520_SCONF5_OCC_EN_MASK |
                                      SH3673520_SCONF5_CADC_EN_MASK |
                                      SH3673520_SCONF5_WDT_EN_MASK |
                                      SH3673510_D011_WDT_CODE));
    return ok;
}

uint8_t sh3673510_control_init(void)
{
    sh3673520_port_status_t port_status;

    s_control_ready = 0u;
    s_afe_sleeping = 0u;

    /* RESET and ALARM are open-drain outputs from the AFE, never MCU outputs. */
    sh3510_gpio_input(D011_AFE_RESET_OUT_PIN);
    sh3510_gpio_input(D011_AFE_ALARM_PIN);
    sh3510_gpio_input(D011_INT_WK_MCU_PIN);

    sh3510_gpio_output_low(D011_HEATER_CHG_PIN);
    sh3510_gpio_output_low(D011_HEATER_RF_EN_PIN);
    sh3510_gpio_output_low(D011_CMNT_EN_PIN);

    /* Active-high board wake and active-low AFE alarm/reset pulses. */
    cpu_set_gpio_wakeup(D011_INT_WK_MCU_PIN, Level_High, 1);
    cpu_set_gpio_wakeup(D011_AFE_ALARM_PIN, Level_Low, 1);
    cpu_set_gpio_wakeup(D011_AFE_RESET_OUT_PIN, Level_Low, 1);

    port_status = SH3673520_PortConfigure(SH3673510_D011_SPI_GROUP,
                                          SH3673510_D011_SPI_TARGET_HZ);
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

uint8_t sh3673510_control_set_fets(uint8_t charge_on, uint8_t discharge_on)
{
    uint8_t bits = 0u;
    if (!s_control_ready) return 0u;
    if (charge_on) bits |= SH3673520_SCONF2_CHGMOS_MASK;
    if (discharge_on) bits |= SH3673520_SCONF2_DSGMOS_MASK;
    return sh3510_update_reg(SH3673520_REG_SCONF2,
                             SH3673520_SCONF2_FET_MASK,
                             bits);
}

uint8_t sh3673510_control_read_status(sh3673510_control_status_t *status)
{
    uint8_t pair[2];
    if ((status == 0) || !s_control_ready) return 0u;

    if (SH3673520_ReadReg(SH3673520_REG_FLAG1, &status->flag1) != SH3673520_OK)
        return 0u;
    /* Centralized intentional FLAG2 read: this consumes VADC/CADC ready flags. */
    if (SH3673520_ReadReg(SH3673520_REG_FLAG2, &status->flag2) != SH3673520_OK)
        return 0u;
    if (SH3673520_ReadRegs(SH3673520_REG_BSTATUS1, pair, 2u) != SH3673520_OK)
        return 0u;
    status->bstatus1 = pair[0];
    status->bstatus2 = pair[1];
    return 1u;
}

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

    value = (uint8_t)~clear_mask; /* W0C: zeros clear selected flags, ones preserve. */
    if (SH3673520_WriteReg(reg, value) != SH3673520_OK) ok = 0u;

    if (!sh3510_update_reg(SH3673520_REG_SCONF2,
                           SH3673520_SCONF2_LTCLR_MASK,
                           (uint8_t)(sconf2 & SH3673520_SCONF2_LTCLR_MASK))) ok = 0u;
    return ok;
}

uint8_t sh3673510_control_clear_flag1(uint8_t clear_mask)
{
    return sh3510_clear_flags(SH3673520_REG_FLAG1, clear_mask);
}

uint8_t sh3673510_control_clear_flag2(uint8_t clear_mask)
{
    /* Never ask the W0C write to clear read-clear ADC ready bits. */
    clear_mask &= (uint8_t)~(SH3673520_FLAG2_VADC_MASK | SH3673520_FLAG2_CADC_MASK);
    return sh3510_clear_flags(SH3673520_REG_FLAG2, clear_mask);
}

uint8_t sh3673510_control_set_balance(uint16_t cell_mask)
{
    uint8_t values[3];
    uint16_t valid = (uint16_t)(cell_mask & 0x03FFu);
    if (!s_control_ready) return 0u;

    /* 10S uses CB1..CB10 only. */
    values[0] = 0u;
    values[1] = (uint8_t)((valid >> 8) & 0x03u); /* CB10..CB9 */
    values[2] = (uint8_t)(valid & 0xFFu);        /* CB8..CB1 */
    if (SH3673520_WriteRegs(SH3673520_REG_BALANCEH, values, 3u) != SH3673520_OK)
        return 0u;
    return 1u;
}

void sh3673510_board_set_heater(uint8_t enabled)
{
    uint8_t on = enabled ? 1u : 0u;
    gpio_write(D011_HEATER_CHG_PIN, on);
    gpio_write(D011_HEATER_RF_EN_PIN, on);
}

uint8_t sh3673510_board_wake_active(void)
{
    return gpio_read(D011_INT_WK_MCU_PIN) ? 1u : 0u;
}

void sh3673510_control_sleep(void)
{
    if (!s_control_ready) return;

    /* Never put the AFE to sleep if the external active-high wake is already asserted. */
    if (sh3673510_board_wake_active()) return;

    (void)sh3673510_control_set_balance(0u);
    sh3673510_board_set_heater(0u);
    (void)sh3673510_control_set_fets(0u, 0u);

    /* Charger wake remains enabled while SLEEP disables CADC/WDT/protections/FETs. */
    (void)sh3510_update_reg(SH3673520_REG_SCONF3,
                            SH3673520_SCONF3_CGR_WK_MASK,
                            SH3673520_SCONF3_CGR_WK_MASK);
    if (SH3673520_WriteReg(SH3673520_REG_SCONF1, SH3673520_SCONF1_SLEEP) == SH3673520_OK)
        s_afe_sleeping = 1u;
}

uint8_t sh3673510_control_wake(void)
{
    if (!s_control_ready) return 0u;
    if (!s_afe_sleeping) return 1u;
    if (SH3673520_WriteReg(SH3673520_REG_SCONF1, SH3673520_SCONF1_NORMAL) != SH3673520_OK)
        return 0u;
    sh3673520_port_delay_ms(10u);
    s_afe_sleeping = 0u;
    return sh3510_configure_runtime() && sh3673510_control_apply_protection();
}

uint8_t sh3673510_control_ready(void)
{
    return s_control_ready;
}
