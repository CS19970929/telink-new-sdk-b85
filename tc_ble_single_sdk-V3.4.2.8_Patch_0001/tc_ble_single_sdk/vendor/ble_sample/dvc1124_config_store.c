#include "dvc1124_config_store.h"

#include "tl_common.h"
#include "drivers.h"
#include "flash_kv32.h"
#include "flash_store_cfg.h"
#include "flash_store_safe.h"
#include <string.h>

#define DVC1124_CFG_KEY_BASE 0xD1124000u

typedef enum
{
    DVC_CFG_KEY_SCHEMA = 0,
    DVC_CFG_KEY_OPERATING,
    DVC_CFG_KEY_GP_MODES,
    DVC_CFG_KEY_POWER,
    DVC_CFG_KEY_CURRENT_WAKE_UV,
    DVC_CFG_KEY_BODY_DIODE_UV,
    DVC_CFG_KEY_HW_MISC,
    DVC_CFG_KEY_SCD,
    DVC_CFG_KEY_COUNT
} dvc1124_cfg_key_index_t;

static flash_kv32_t s_cfg_kv;
static u32 s_sector_addrs[FLASH_ADDR_AFE_CFG_KV_SECTORS];
static flash_kv32_key_def_t s_key_defs[DVC_CFG_KEY_COUNT];
static flash_kv32_cache_entry_t s_cache[DVC_CFG_KEY_COUNT];
static uint8_t s_restore_pending = 1u;
static uint8_t s_kv_ready;

static u32 dvc_cfg_key(dvc1124_cfg_key_index_t index)
{
    return DVC1124_CFG_KEY_BASE + (u32)index;
}

static void dvc_cfg_flash_read(void *ctx, u32 addr, u8 *buf, u32 len)
{
    (void)ctx;
    flash_read_page(addr, (int)len, buf);
}

static int dvc_cfg_flash_prog(void *ctx, u32 addr, const u8 *buf, u32 len)
{
    (void)ctx;
    return flash_store_prog_checked(addr, buf, len);
}

static int dvc_cfg_flash_erase(void *ctx, u32 addr, u32 size)
{
    (void)ctx;
    return flash_store_erase_sector_checked(addr, size);
}

static const flash_kv32_port_t *dvc_cfg_port(void)
{
    static const flash_kv32_port_t port = {
        0,
        dvc_cfg_flash_read,
        dvc_cfg_flash_prog,
        dvc_cfg_flash_erase,
        0,
        0,
    };
    return &port;
}

static dvc1124_i2c_wdt_code_t dvc_cfg_default_wdt_code(void)
{
    switch (DVC1124_I2C_WATCHDOG_SECONDS)
    {
    case 4u:  return DVC1124_I2C_WDT_4S;
    case 8u:  return DVC1124_I2C_WDT_8S;
    case 16u: return DVC1124_I2C_WDT_16S;
    case 32u: return DVC1124_I2C_WDT_32S;
    default:  return DVC1124_I2C_WDT_OFF;
    }
}

void DVC1124_ConfigStoreGetDefaults(dvc1124_persistent_config_t *cfg)
{
    if (cfg == NULL) return;

    memset(cfg, 0, sizeof(*cfg));
    cfg->operating.high_side_fet_mask = DVC1124_DEFAULT_HIGH_SIDE_FET_MASK;
    cfg->operating.cadc_work_enable = DVC1124_DEFAULT_CADC_WORK_ENABLE;
    cfg->operating.current_wake_enable = DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE;
    cfg->operating.cc1_work_time = DVC1124_DEFAULT_CC1_WORK_TIME;
    cfg->operating.cc1_sleep_wake_time = DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME;
    cfg->operating.charge_pump_voltage = DVC1124_CHARGE_PUMP_VOLTAGE_CODE;
    cfg->operating.cell_measurement_mask = DVC1124_DEFAULT_CELL_MEASUREMENT_MASK;
    cfg->operating.cell_voltage_signed = DVC1124_DEFAULT_CELL_VOLTAGE_SIGNED;
    cfg->operating.vadc_enable = DVC1124_DEFAULT_VADC_ENABLE;
    cfg->operating.vadc_sync_with_cc2 = DVC1124_DEFAULT_VADC_SYNC_WITH_CC2;
    cfg->operating.vadc_period = DVC1124_DEFAULT_VADC_PERIOD;
    cfg->operating.vadc_time = DVC1124_DEFAULT_VADC_TIME;
    cfg->operating.gp1_mode = DVC1124_GP1_DEFAULT_MODE;
    cfg->operating.gp2_mode = DVC1124_GP2_DEFAULT_MODE;
    cfg->operating.gp3_mode = DVC1124_GP3_DEFAULT_MODE;
    cfg->operating.gp4_mode = DVC1124_GP4_DEFAULT_MODE;
    cfg->operating.gp5_mode = DVC1124_GP5_DEFAULT_MODE;
    cfg->operating.gp6_mode = DVC1124_GP6_DEFAULT_MODE;
    cfg->operating.v3p3_sleep_enable = DVC1124_DEFAULT_V3P3_SLEEP_ENABLE;
    cfg->operating.v3p3_work_enable = DVC1124_DEFAULT_V3P3_WORK_ENABLE;
    cfg->operating.v3p3_timeout_restart = DVC1124_DEFAULT_V3P3_TIMEOUT_RESTART;
    cfg->operating.i2c_watchdog = dvc_cfg_default_wdt_code();
    cfg->operating.timed_wake = DVC1124_DEFAULT_TIMED_WAKE;
    cfg->operating.interrupt_mask = DVC1124_DEFAULT_INTERRUPT_MASK;

    cfg->current_wake_threshold_uv = DVC1124_CURRENT_WAKE_THRESHOLD_UV;
    cfg->body_diode_threshold_uv = DVC1124_BODY_DIODE_THRESHOLD_UV;
    cfg->dsg_pulldown_strength = DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH;
    cfg->i2c_timeout_close_chg = DVC1124_I2C_TIMEOUT_CLOSE_CHG ? 1u : 0u;
    cfg->i2c_timeout_close_dsg = DVC1124_I2C_TIMEOUT_CLOSE_DSG ? 1u : 0u;
    cfg->core_ot_code = DVC1124_DEFAULT_CORE_OT_CODE;
    cfg->scd_threshold_mv = DVC1124_HW_SCD_THRESHOLD_MV;
    cfg->scd_delay_us = DVC1124_HW_SCD_DELAY_US;
}

static int dvc_cfg_bool_ok(uint8_t value)
{
    return (value <= 1u) ? 1 : 0;
}

static int dvc_cfg_gp236_ok(uint8_t value)
{
    return ((value <= 2u) || (value == 6u) || (value == 7u)) ? 1 : 0;
}

int DVC1124_ConfigStoreValidate(const dvc1124_persistent_config_t *cfg)
{
    uint32_t scd_code;

    if (cfg == NULL) return 0;

    if (!dvc_cfg_bool_ok(cfg->operating.high_side_fet_mask) ||
        !dvc_cfg_bool_ok(cfg->operating.cadc_work_enable) ||
        !dvc_cfg_bool_ok(cfg->operating.current_wake_enable) ||
        !dvc_cfg_bool_ok(cfg->operating.cell_measurement_mask) ||
        !dvc_cfg_bool_ok(cfg->operating.cell_voltage_signed) ||
        !dvc_cfg_bool_ok(cfg->operating.vadc_enable) ||
        !dvc_cfg_bool_ok(cfg->operating.vadc_sync_with_cc2) ||
        !dvc_cfg_bool_ok(cfg->operating.v3p3_sleep_enable) ||
        !dvc_cfg_bool_ok(cfg->operating.v3p3_work_enable) ||
        !dvc_cfg_bool_ok(cfg->operating.v3p3_timeout_restart) ||
        !dvc_cfg_bool_ok(cfg->i2c_timeout_close_chg) ||
        !dvc_cfg_bool_ok(cfg->i2c_timeout_close_dsg)) return 0;

    if ((uint8_t)cfg->operating.cc1_work_time > 3u ||
        (uint8_t)cfg->operating.cc1_sleep_wake_time > 3u ||
        (uint8_t)cfg->operating.charge_pump_voltage > 7u ||
        (uint8_t)cfg->operating.vadc_period > 3u ||
        (uint8_t)cfg->operating.vadc_time > 3u ||
        (uint8_t)cfg->operating.gp1_mode > 3u ||
        (uint8_t)cfg->operating.gp4_mode > 3u ||
        !dvc_cfg_gp236_ok((uint8_t)cfg->operating.gp2_mode) ||
        !dvc_cfg_gp236_ok((uint8_t)cfg->operating.gp3_mode) ||
        !dvc_cfg_gp236_ok((uint8_t)cfg->operating.gp5_mode) ||
        !dvc_cfg_gp236_ok((uint8_t)cfg->operating.gp6_mode)) return 0;

    if (!((cfg->operating.i2c_watchdog == DVC1124_I2C_WDT_OFF) ||
          (cfg->operating.i2c_watchdog == DVC1124_I2C_WDT_4S) ||
          (cfg->operating.i2c_watchdog == DVC1124_I2C_WDT_8S) ||
          (cfg->operating.i2c_watchdog == DVC1124_I2C_WDT_16S) ||
          (cfg->operating.i2c_watchdog == DVC1124_I2C_WDT_32S))) return 0;
    if ((uint8_t)cfg->operating.timed_wake > 15u) return 0;

    if ((cfg->current_wake_threshold_uv != 0u) &&
        ((cfg->current_wake_threshold_uv < 10u) ||
         (cfg->current_wake_threshold_uv > 2550u) ||
         ((cfg->current_wake_threshold_uv % 10u) != 0u))) return 0;

    if ((cfg->body_diode_threshold_uv != 0u) &&
        ((cfg->body_diode_threshold_uv < 40u) ||
         (cfg->body_diode_threshold_uv > 10200u) ||
         ((cfg->body_diode_threshold_uv % 40u) != 0u))) return 0;

    if (cfg->dsg_pulldown_strength > 30u || cfg->core_ot_code > 127u) return 0;

    if (cfg->scd_threshold_mv == 0u)
    {
        if (cfg->scd_delay_us != 0u) return 0;
    }
    else
    {
        if ((cfg->scd_threshold_mv < 10u) ||
            (cfg->scd_threshold_mv > 630u) ||
            ((cfg->scd_threshold_mv % 10u) != 0u)) return 0;
        scd_code = ((uint32_t)cfg->scd_delay_us * 100u) / 781u;
        if (scd_code > 255u) return 0;
    }

    return 1;
}

static u32 dvc_cfg_pack_operating(const dvc1124_persistent_config_t *cfg)
{
    u32 value = 0u;
    value |= (u32)(cfg->operating.high_side_fet_mask & 1u) << 0;
    value |= (u32)(cfg->operating.cadc_work_enable & 1u) << 1;
    value |= (u32)(cfg->operating.current_wake_enable & 1u) << 2;
    value |= (u32)((uint8_t)cfg->operating.cc1_work_time & 3u) << 3;
    value |= (u32)((uint8_t)cfg->operating.cc1_sleep_wake_time & 3u) << 5;
    value |= (u32)((uint8_t)cfg->operating.charge_pump_voltage & 7u) << 7;
    value |= (u32)(cfg->operating.cell_measurement_mask & 1u) << 10;
    value |= (u32)(cfg->operating.cell_voltage_signed & 1u) << 11;
    value |= (u32)(cfg->operating.vadc_enable & 1u) << 12;
    value |= (u32)(cfg->operating.vadc_sync_with_cc2 & 1u) << 13;
    value |= (u32)((uint8_t)cfg->operating.vadc_period & 3u) << 14;
    value |= (u32)((uint8_t)cfg->operating.vadc_time & 3u) << 16;
    return value;
}

static void dvc_cfg_unpack_operating(u32 value, dvc1124_persistent_config_t *cfg)
{
    cfg->operating.high_side_fet_mask = (uint8_t)((value >> 0) & 1u);
    cfg->operating.cadc_work_enable = (uint8_t)((value >> 1) & 1u);
    cfg->operating.current_wake_enable = (uint8_t)((value >> 2) & 1u);
    cfg->operating.cc1_work_time = (dvc1124_cc1_work_time_t)((value >> 3) & 3u);
    cfg->operating.cc1_sleep_wake_time = (dvc1124_cc1_sleep_wake_time_t)((value >> 5) & 3u);
    cfg->operating.charge_pump_voltage = (dvc1124_cp_voltage_t)((value >> 7) & 7u);
    cfg->operating.cell_measurement_mask = (uint8_t)((value >> 10) & 1u);
    cfg->operating.cell_voltage_signed = (uint8_t)((value >> 11) & 1u);
    cfg->operating.vadc_enable = (uint8_t)((value >> 12) & 1u);
    cfg->operating.vadc_sync_with_cc2 = (uint8_t)((value >> 13) & 1u);
    cfg->operating.vadc_period = (dvc1124_vadc_period_t)((value >> 14) & 3u);
    cfg->operating.vadc_time = (dvc1124_vadc_time_t)((value >> 16) & 3u);
}

static u32 dvc_cfg_pack_gp(const dvc1124_persistent_config_t *cfg)
{
    u32 value = 0u;
    value |= (u32)((uint8_t)cfg->operating.gp1_mode & 3u) << 0;
    value |= (u32)((uint8_t)cfg->operating.gp2_mode & 7u) << 2;
    value |= (u32)((uint8_t)cfg->operating.gp3_mode & 7u) << 5;
    value |= (u32)((uint8_t)cfg->operating.gp4_mode & 3u) << 8;
    value |= (u32)((uint8_t)cfg->operating.gp5_mode & 7u) << 10;
    value |= (u32)((uint8_t)cfg->operating.gp6_mode & 7u) << 13;
    return value;
}

static void dvc_cfg_unpack_gp(u32 value, dvc1124_persistent_config_t *cfg)
{
    cfg->operating.gp1_mode = (dvc1124_gp14_mode_t)((value >> 0) & 3u);
    cfg->operating.gp2_mode = (dvc1124_gp236_mode_t)((value >> 2) & 7u);
    cfg->operating.gp3_mode = (dvc1124_gp236_mode_t)((value >> 5) & 7u);
    cfg->operating.gp4_mode = (dvc1124_gp14_mode_t)((value >> 8) & 3u);
    cfg->operating.gp5_mode = (dvc1124_gp236_mode_t)((value >> 10) & 7u);
    cfg->operating.gp6_mode = (dvc1124_gp236_mode_t)((value >> 13) & 7u);
}

static u32 dvc_cfg_pack_power(const dvc1124_persistent_config_t *cfg)
{
    u32 value = 0u;
    value |= (u32)(cfg->operating.v3p3_sleep_enable & 1u) << 0;
    value |= (u32)(cfg->operating.v3p3_work_enable & 1u) << 1;
    value |= (u32)(cfg->operating.v3p3_timeout_restart & 1u) << 2;
    value |= (u32)((uint8_t)cfg->operating.i2c_watchdog & 7u) << 3;
    value |= (u32)((uint8_t)cfg->operating.timed_wake & 15u) << 6;
    value |= (u32)cfg->operating.interrupt_mask << 10;
    return value;
}

static void dvc_cfg_unpack_power(u32 value, dvc1124_persistent_config_t *cfg)
{
    cfg->operating.v3p3_sleep_enable = (uint8_t)((value >> 0) & 1u);
    cfg->operating.v3p3_work_enable = (uint8_t)((value >> 1) & 1u);
    cfg->operating.v3p3_timeout_restart = (uint8_t)((value >> 2) & 1u);
    cfg->operating.i2c_watchdog = (dvc1124_i2c_wdt_code_t)((value >> 3) & 7u);
    cfg->operating.timed_wake = (dvc1124_timed_wake_t)((value >> 6) & 15u);
    cfg->operating.interrupt_mask = (uint8_t)((value >> 10) & 0xFFu);
}

static u32 dvc_cfg_pack_hw_misc(const dvc1124_persistent_config_t *cfg)
{
    u32 value = 0u;
    value |= (u32)(cfg->dsg_pulldown_strength & 31u) << 0;
    value |= (u32)(cfg->i2c_timeout_close_chg & 1u) << 5;
    value |= (u32)(cfg->i2c_timeout_close_dsg & 1u) << 6;
    value |= (u32)(cfg->core_ot_code & 0x7Fu) << 7;
    return value;
}

static void dvc_cfg_unpack_hw_misc(u32 value, dvc1124_persistent_config_t *cfg)
{
    cfg->dsg_pulldown_strength = (uint8_t)((value >> 0) & 31u);
    cfg->i2c_timeout_close_chg = (uint8_t)((value >> 5) & 1u);
    cfg->i2c_timeout_close_dsg = (uint8_t)((value >> 6) & 1u);
    cfg->core_ot_code = (uint8_t)((value >> 7) & 0x7Fu);
}

static u32 dvc_cfg_pack_scd(const dvc1124_persistent_config_t *cfg)
{
    return ((u32)cfg->scd_threshold_mv & 0x03FFu) |
           (((u32)cfg->scd_delay_us & 0x0FFFu) << 10);
}

static void dvc_cfg_unpack_scd(u32 value, dvc1124_persistent_config_t *cfg)
{
    cfg->scd_threshold_mv = (uint16_t)(value & 0x03FFu);
    cfg->scd_delay_us = (uint16_t)((value >> 10) & 0x0FFFu);
}

static void dvc_cfg_fill_key_defs(const dvc1124_persistent_config_t *defaults)
{
    s_key_defs[DVC_CFG_KEY_SCHEMA].key = dvc_cfg_key(DVC_CFG_KEY_SCHEMA);
    s_key_defs[DVC_CFG_KEY_SCHEMA].default_value = DVC1124_CONFIG_STORE_SCHEMA_VERSION;
    s_key_defs[DVC_CFG_KEY_OPERATING].key = dvc_cfg_key(DVC_CFG_KEY_OPERATING);
    s_key_defs[DVC_CFG_KEY_OPERATING].default_value = dvc_cfg_pack_operating(defaults);
    s_key_defs[DVC_CFG_KEY_GP_MODES].key = dvc_cfg_key(DVC_CFG_KEY_GP_MODES);
    s_key_defs[DVC_CFG_KEY_GP_MODES].default_value = dvc_cfg_pack_gp(defaults);
    s_key_defs[DVC_CFG_KEY_POWER].key = dvc_cfg_key(DVC_CFG_KEY_POWER);
    s_key_defs[DVC_CFG_KEY_POWER].default_value = dvc_cfg_pack_power(defaults);
    s_key_defs[DVC_CFG_KEY_CURRENT_WAKE_UV].key = dvc_cfg_key(DVC_CFG_KEY_CURRENT_WAKE_UV);
    s_key_defs[DVC_CFG_KEY_CURRENT_WAKE_UV].default_value = defaults->current_wake_threshold_uv;
    s_key_defs[DVC_CFG_KEY_BODY_DIODE_UV].key = dvc_cfg_key(DVC_CFG_KEY_BODY_DIODE_UV);
    s_key_defs[DVC_CFG_KEY_BODY_DIODE_UV].default_value = defaults->body_diode_threshold_uv;
    s_key_defs[DVC_CFG_KEY_HW_MISC].key = dvc_cfg_key(DVC_CFG_KEY_HW_MISC);
    s_key_defs[DVC_CFG_KEY_HW_MISC].default_value = dvc_cfg_pack_hw_misc(defaults);
    s_key_defs[DVC_CFG_KEY_SCD].key = dvc_cfg_key(DVC_CFG_KEY_SCD);
    s_key_defs[DVC_CFG_KEY_SCD].default_value = dvc_cfg_pack_scd(defaults);
}

int DVC1124_ConfigStoreInit(void)
{
    dvc1124_persistent_config_t defaults;
    flash_kv32_cfg_t cfg;
    u32 base;
    u16 sectors;
    u16 i;

    if (s_kv_ready) return 1;

    base = flash_store_cfg_get_afe_cfg_kv_base();
    sectors = flash_store_cfg_get_afe_cfg_kv_sectors();
    if ((base == 0u) || (sectors < 2u) ||
        (sectors > FLASH_ADDR_AFE_CFG_KV_SECTORS)) return 0;

    DVC1124_ConfigStoreGetDefaults(&defaults);
    if (!DVC1124_ConfigStoreValidate(&defaults)) return 0;
    dvc_cfg_fill_key_defs(&defaults);

    for (i = 0u; i < sectors; ++i)
    {
        s_sector_addrs[i] = base + (u32)i * FLASH_SECTOR_SIZE;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.port = dvc_cfg_port();
    cfg.sector_addrs = s_sector_addrs;
    cfg.keys = s_key_defs;
    cfg.sector_count = sectors;
    cfg.sector_size = FLASH_SECTOR_SIZE;
    cfg.write_align = 4u;
    cfg.key_count = DVC_CFG_KEY_COUNT;

    if (!flash_kv32_init(&s_cfg_kv, &cfg, s_cache)) return 0;
    s_kv_ready = 1u;
    return 1;
}

static int dvc_cfg_get(dvc1124_cfg_key_index_t index, u32 *value)
{
    if ((value == NULL) || !DVC1124_ConfigStoreInit()) return 0;
    return flash_kv32_get(&s_cfg_kv, dvc_cfg_key(index), value);
}

int DVC1124_ConfigStoreLoad(dvc1124_persistent_config_t *cfg)
{
    u32 schema;
    u32 operating;
    u32 gp;
    u32 power;
    u32 current_wake;
    u32 body_diode;
    u32 hw_misc;
    u32 scd;

    if (cfg == NULL || !DVC1124_ConfigStoreInit()) return 0;
    if (!dvc_cfg_get(DVC_CFG_KEY_SCHEMA, &schema) ||
        schema != DVC1124_CONFIG_STORE_SCHEMA_VERSION) return 0;
    if (!dvc_cfg_get(DVC_CFG_KEY_OPERATING, &operating) ||
        !dvc_cfg_get(DVC_CFG_KEY_GP_MODES, &gp) ||
        !dvc_cfg_get(DVC_CFG_KEY_POWER, &power) ||
        !dvc_cfg_get(DVC_CFG_KEY_CURRENT_WAKE_UV, &current_wake) ||
        !dvc_cfg_get(DVC_CFG_KEY_BODY_DIODE_UV, &body_diode) ||
        !dvc_cfg_get(DVC_CFG_KEY_HW_MISC, &hw_misc) ||
        !dvc_cfg_get(DVC_CFG_KEY_SCD, &scd)) return 0;

    memset(cfg, 0, sizeof(*cfg));
    dvc_cfg_unpack_operating(operating, cfg);
    dvc_cfg_unpack_gp(gp, cfg);
    dvc_cfg_unpack_power(power, cfg);
    cfg->current_wake_threshold_uv = (uint16_t)current_wake;
    cfg->body_diode_threshold_uv = (uint16_t)body_diode;
    dvc_cfg_unpack_hw_misc(hw_misc, cfg);
    dvc_cfg_unpack_scd(scd, cfg);

    return DVC1124_ConfigStoreValidate(cfg);
}

int DVC1124_ConfigStoreSave(const dvc1124_persistent_config_t *cfg)
{
    flash_kv32_pair_t pairs[DVC_CFG_KEY_COUNT];

    if (!DVC1124_ConfigStoreValidate(cfg) || !DVC1124_ConfigStoreInit()) return 0;

    pairs[DVC_CFG_KEY_SCHEMA].key = dvc_cfg_key(DVC_CFG_KEY_SCHEMA);
    pairs[DVC_CFG_KEY_SCHEMA].value = DVC1124_CONFIG_STORE_SCHEMA_VERSION;
    pairs[DVC_CFG_KEY_OPERATING].key = dvc_cfg_key(DVC_CFG_KEY_OPERATING);
    pairs[DVC_CFG_KEY_OPERATING].value = dvc_cfg_pack_operating(cfg);
    pairs[DVC_CFG_KEY_GP_MODES].key = dvc_cfg_key(DVC_CFG_KEY_GP_MODES);
    pairs[DVC_CFG_KEY_GP_MODES].value = dvc_cfg_pack_gp(cfg);
    pairs[DVC_CFG_KEY_POWER].key = dvc_cfg_key(DVC_CFG_KEY_POWER);
    pairs[DVC_CFG_KEY_POWER].value = dvc_cfg_pack_power(cfg);
    pairs[DVC_CFG_KEY_CURRENT_WAKE_UV].key = dvc_cfg_key(DVC_CFG_KEY_CURRENT_WAKE_UV);
    pairs[DVC_CFG_KEY_CURRENT_WAKE_UV].value = cfg->current_wake_threshold_uv;
    pairs[DVC_CFG_KEY_BODY_DIODE_UV].key = dvc_cfg_key(DVC_CFG_KEY_BODY_DIODE_UV);
    pairs[DVC_CFG_KEY_BODY_DIODE_UV].value = cfg->body_diode_threshold_uv;
    pairs[DVC_CFG_KEY_HW_MISC].key = dvc_cfg_key(DVC_CFG_KEY_HW_MISC);
    pairs[DVC_CFG_KEY_HW_MISC].value = dvc_cfg_pack_hw_misc(cfg);
    pairs[DVC_CFG_KEY_SCD].key = dvc_cfg_key(DVC_CFG_KEY_SCD);
    pairs[DVC_CFG_KEY_SCD].value = dvc_cfg_pack_scd(cfg);

    return flash_kv32_write_pairs(&s_cfg_kv, pairs, DVC_CFG_KEY_COUNT);
}

int DVC1124_ConfigStoreApply(const dvc1124_persistent_config_t *cfg)
{
    uint8_t cwt;
    uint8_t bdpt;
    uint8_t ok = 1u;

    if (!DVC1124_ConfigStoreValidate(cfg)) return 0;

    cwt = (cfg->current_wake_threshold_uv == 0u)
              ? 0u
              : (uint8_t)(cfg->current_wake_threshold_uv / 10u);
    bdpt = (cfg->body_diode_threshold_uv == 0u)
               ? 0u
               : (uint8_t)(cfg->body_diode_threshold_uv / 40u);

    /* Configure timeout behavior before a stored non-zero I2C watchdog is enabled. */
    ok &= DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CHG_MASK,
                                          DVC1124_CHGMASK_CWM_MASK,
                                          7u,
                                          cfg->i2c_timeout_close_chg ? 0u : 1u);
    ok &= DVC1124_WriteRegisterFieldSafe(DVC1124_REG_DSG_MASK,
                                          DVC1124_DSGMASK_DWM_MASK,
                                          3u,
                                          cfg->i2c_timeout_close_dsg ? 0u : 1u);
    ok &= DVC1124_WriteRegisterFieldSafe(DVC1124_REG_DSG_PULLDOWN,
                                          DVC1124_DPC_MASK,
                                          DVC1124_DPC_SHIFT,
                                          cfg->dsg_pulldown_strength);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_CURRENT_WAKE, cwt);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_BODY_DIODE, bdpt);
    ok &= DVC1124_WriteRegisterFieldSafe(DVC1124_REG_CORE_OT,
                                          DVC1124_CORE_OT_THRESHOLD_MASK,
                                          DVC1124_CORE_OT_THRESHOLD_SHIFT,
                                          cfg->core_ot_code);
    ok &= DVC1124_SetShortCircuitProtection(cfg->scd_threshold_mv,
                                             cfg->scd_delay_us);
    ok &= DVC1124_ApplyOperatingConfig(&cfg->operating);

    return ok ? 1 : 0;
}

int DVC1124_ConfigStoreCaptureCurrent(dvc1124_persistent_config_t *cfg)
{
    uint8_t raw;
    uint8_t delay;

    if (cfg == NULL) return 0;
    memset(cfg, 0, sizeof(*cfg));

    if (!DVC1124_GetOperatingConfig(&cfg->operating)) return 0;

    if (!DVC1124_ReadRegisters(DVC1124_REG_CURRENT_WAKE, &raw, 1u)) return 0;
    cfg->current_wake_threshold_uv = (uint16_t)raw * 10u;

    if (!DVC1124_ReadRegisters(DVC1124_REG_BODY_DIODE, &raw, 1u)) return 0;
    cfg->body_diode_threshold_uv = (uint16_t)raw * 40u;

    if (!DVC1124_ReadRegisters(DVC1124_REG_DSG_PULLDOWN, &raw, 1u)) return 0;
    cfg->dsg_pulldown_strength = DVC1124_FIELD_GET(DVC1124_DPC_MASK,
                                                   DVC1124_DPC_SHIFT,
                                                   raw);

    if (!DVC1124_ReadRegisters(DVC1124_REG_CHG_MASK, &raw, 1u)) return 0;
    cfg->i2c_timeout_close_chg = (raw & DVC1124_CHGMASK_CWM_MASK) ? 0u : 1u;

    if (!DVC1124_ReadRegisters(DVC1124_REG_DSG_MASK, &raw, 1u)) return 0;
    cfg->i2c_timeout_close_dsg = (raw & DVC1124_DSGMASK_DWM_MASK) ? 0u : 1u;

    if (!DVC1124_ReadRegisters(DVC1124_REG_CORE_OT, &raw, 1u)) return 0;
    cfg->core_ot_code = (uint8_t)(raw & DVC1124_CORE_OT_THRESHOLD_MASK);

    if (!DVC1124_ReadRegisters(DVC1124_REG_SCD, &raw, 1u)) return 0;
    if ((raw & DVC1124_SCD_ENABLE_MASK) != 0u)
    {
        cfg->scd_threshold_mv = (uint16_t)(raw & DVC1124_SCD_THRESHOLD_MASK) * 10u;
        if (!DVC1124_ReadRegisters(DVC1124_REG_SCD_DLY, &delay, 1u)) return 0;
        cfg->scd_delay_us = (uint16_t)(((uint32_t)delay * 781u + 50u) / 100u);
    }
    else
    {
        cfg->scd_threshold_mv = 0u;
        cfg->scd_delay_us = 0u;
    }

    return DVC1124_ConfigStoreValidate(cfg);
}

int DVC1124_ConfigStoreCaptureAndSave(void)
{
    dvc1124_persistent_config_t cfg;
    if (!DVC1124_ConfigStoreCaptureCurrent(&cfg)) return 0;
    return DVC1124_ConfigStoreSave(&cfg);
}

int DVC1124_ConfigStoreRestore(void)
{
    dvc1124_persistent_config_t cfg;

    if (!DVC1124_ConfigStoreLoad(&cfg))
    {
        DVC1124_ConfigStoreGetDefaults(&cfg);
        if (!DVC1124_ConfigStoreValidate(&cfg)) return 0;
        if (!DVC1124_ConfigStoreApply(&cfg)) return 0;
        (void)DVC1124_ConfigStoreSave(&cfg);
        s_restore_pending = 0u;
        return 1;
    }

    if (!DVC1124_ConfigStoreApply(&cfg)) return 0;
    s_restore_pending = 0u;
    return 1;
}

int DVC1124_ConfigStoreWritePersistentRegister(uint8_t reg, uint8_t requested)
{
    uint8_t current;
    uint8_t target;
    uint8_t verify;
    uint8_t mask = DVC1124_RegPersistentConfigMask(reg);

    if ((reg > DVC1124_MAX_REGISTER) || (mask == 0u)) return 0;
    if (!DVC1124_ReadRegisters(reg, &current, 1u)) return 0;

    target = (uint8_t)((current & (uint8_t)~mask) | (requested & mask));
    if (!DVC1124_WriteRegisters(reg, &target, 1u)) return 0;
    if (!DVC1124_ReadRegisters(reg, &verify, 1u)) return 0;
    if ((verify & mask) != (target & mask)) return 0;

    return DVC1124_ConfigStoreCaptureAndSave();
}

void DVC1124_ConfigStore_AFE_Reset(void)
{
    s_restore_pending = 1u;
    DVC1124_AFE_Reset();
}

void DVC1124_ConfigStore_UpdataAfeConfig(void)
{
    DVC1124_UpdataAfeConfig();
    if (!DVC1124_ConfigStoreRestore()) s_restore_pending = 1u;
}

void DVC1124_ConfigStore_BmsApp_AFEGet(void)
{
    DVC1124_BmsApp_AFEGet();
    if (s_restore_pending)
    {
        (void)DVC1124_ConfigStoreRestore();
    }
}
