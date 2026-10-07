/*
 * 文件功能：DVC1124 寄存器通信、采样、保护编码与驱动接口；
 * 为条件编译的 DVC backend 保留。
 * bms/afe/dvc1124/dvc1124.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_diag.h"
#include "dvc1124.h"
#include "bms_config_store.h"

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_parameters.h"
#include "bms_afe_hw_profile.h"
#include <string.h>

/* 寄存器地址和位定义仅来自 dvc1124_reg.h；本文件只处理驱动行为、换算和策略衔接。 */
#define DVC_MEAS_BYTES             (DVC1124_REG_CELL24_L + 1u)
#define DVC_I2C_RAW_MAX            (2u * (DVC1124_MAX_REGISTER + 1u))
#define DVC_READY_RETRY_COUNT      20u
#define DVC_TEMP_TABLE_LEN         56u

/* 本文件拥有的寄存器工具，业务与后端消费者不直接调用。 */
static uint8_t DVC1124_ResolveWriteAddress(dvc1124_model_t model,
                                    dvc1124_addr_mode_t mode,
                                    uint8_t hardwire_code,
                                    uint8_t explicit_write_addr,
                                    uint8_t *write_addr);
static uint8_t DVC1124_SetCellCount(uint8_t cell_count);
static uint8_t DVC1124_SetMosState(uint8_t charge_on, uint8_t discharge_on);
static uint8_t DVC1124_StartOpenWireCheck(void);
static uint8_t DVC1124_SetShortCircuitProtection(uint16_t threshold_mv, uint16_t delay_us);
static uint8_t DVC1124_AFE_IsReady(void);
static uint8_t DVC1124_EncodeCurrentWake(uint16_t threshold_uv, uint8_t *code);
static uint8_t DVC1124_EncodeBodyDiode(uint16_t threshold_uv, uint8_t *code);
static uint8_t DVC1124_EncodeI2cWatchdog(uint8_t seconds, dvc1124_i2c_wdt_code_t *code);

/* NTC 电阻与 ((degC + 40) * 10) 温度编码成对存储；电阻单位为 10 Ω。 */
static const uint16_t s_ntc_10k_table[DVC_TEMP_TABLE_LEN] = {
    11611u, 100u, 8935u, 150u, 6943u, 200u, 5442u, 250u,
    4300u, 300u, 3422u, 350u, 2751u, 400u, 2214u, 450u,
    1801u, 500u, 1470u, 550u, 1209u, 600u, 1000u, 650u,
    831u, 700u, 694u, 750u, 583u, 800u, 492u, 850u,
    416u, 900u, 355u, 950u, 303u, 1000u, 260u, 1050u,
    224u, 1100u, 193u, 1150u, 167u, 1200u, 146u, 1250u,
    127u, 1300u, 111u, 1350u, 98u, 1400u, 86u, 1450u,
};

static dvc1124_config_t s_cfg = {
    DVC1124_DEFAULT_MODEL,
    DVC1124_DEFAULT_ADDR_MODE,
    DVC1124_DEFAULT_HARDWIRE_CODE,
    DVC1124_DEFAULT_EXPLICIT_WRITE_ADDR,
    DVC1124_DEFAULT_CELL_COUNT,
    DVC1124_DEFAULT_SHUNT_UOHM,
    DVC1124_DEFAULT_BATTERY_NTC_GP,
    DVC1124_DEFAULT_MOS_NTC_GP,
};

static dvc1124_snapshot_t s_snapshot;
static uint8_t s_i2c_raw[DVC_I2C_RAW_MAX];
static uint8_t s_bus_initialized;
static uint8_t s_need_config = 1u;
static uint8_t s_output_enabled;
static uint32_t s_balance_requested_mask;
static uint32_t s_balance_last_refresh_tick;
static uint8_t s_balance_suspended;
/* DS V1.1 p13：CC2 256ms，VADC最长223ms；512ms是项目活性上限。 */
#define DVC_ADC_MAX_AGE_TICKS (512u * 32u)
static uint8_t s_pending_adc_events;
static uint8_t s_voltage_seen, s_current_seen, s_sample_pending;
static uint8_t s_voltage_since_current;
static uint32_t s_voltage_tick, s_current_tick, s_adc_wait_started;
static uint32_t s_snapshot_generation;
static uint32_t s_openwire_start_generation;
static uint32_t s_openwire_start_tick;
static dvc1124_openwire_result_t s_openwire_result;

#define DVC_BALANCE_REFRESH_INTERVAL_US 45000000u
#define DVC_OPENWIRE_SETTLE_US            200000u

/* 冻结的启动诊断，偏移相对 BMS_DIAG_BASE。 */
#define DVC_BOOT_ZERO_DIAG_STATUS          110u
#define DVC_BOOT_ZERO_DIAG_SAMPLE_COUNT    111u
#define DVC_BOOT_ZERO_DIAG_OFFSET_MA       112u
#define DVC_BOOT_ZERO_DIAG_RAW1_MA         114u
#define DVC_BOOT_ZERO_DIAG_RAW2_MA         116u
#define DVC_BOOT_ZERO_DIAG_CAL1_MA         118u
#define DVC_BOOT_ZERO_DIAG_CAL2_MA         120u
#define DVC_BOOT_ZERO_DIAG_FACTORY_OFF_MA  122u
#define DVC_BOOT_ZERO_DIAG_FACTORY_GAIN    124u
#define DVC_BOOT_ZERO_DIAG_SHUNT_UOHM      126u
#define DVC_BOOT_ZERO_DIAG_SPREAD_MA       127u

static dvc1124_boot_zero_diag_t s_boot_zero;

/* DVC 硬件实际表示的最近配置值，用于诊断。 */
typedef struct
{
    uint16_t cov_mv;
    uint16_t cov_delay_ms;
    uint16_t cuv_mv;
    uint16_t cuv_delay_ms;
    uint16_t ocd1_a_x10;
    uint16_t ocd1_delay_ms;
    uint16_t occ1_a_x10;
    uint16_t occ1_delay_ms;
    uint16_t ocd2_a_x10;
    uint16_t ocd2_delay_ms;
    uint16_t occ2_a_x10;
    uint16_t occ2_delay_ms;
    uint16_t scd_mv;
    uint16_t scd_delay_us;
    uint32_t quantized_mask;
} dvc_applied_protection_t;

static dvc_applied_protection_t s_applied;

#define DVC_QUANT_COV_DLY   (1uL << 0)
#define DVC_QUANT_CUV_DLY   (1uL << 1)
#define DVC_QUANT_OCD1_THR  (1uL << 2)
#define DVC_QUANT_OCC1_THR  (1uL << 3)
#define DVC_QUANT_OCD1_DLY  (1uL << 4)
#define DVC_QUANT_OCC1_DLY  (1uL << 5)
#define DVC_QUANT_OCD2_THR  (1uL << 6)
#define DVC_QUANT_OCC2_THR  (1uL << 7)
#define DVC_QUANT_OCD2_DLY  (1uL << 8)
#define DVC_QUANT_OCC2_DLY  (1uL << 9)

/* 计算 AFE 通信数据的 CRC8 校验值。 */
static uint8_t dvc_crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0u;
    uint8_t i;

    while (len-- != 0u)
    {
        crc ^= *data++;
        for (i = 0u; i < 8u; ++i)
        {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* 按毫秒执行 AFE 所需的短等待。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
static void dvc_delay_ms(uint16_t ms) __attribute__((noinline));
static void dvc_delay_ms(uint16_t ms)
{
    uint32_t tick = clock_time();
    uint32_t us = (uint32_t)ms * 1000u;

    while (!clock_time_exceed(tick, us))
    {
    }
}

/* 配置 DVC I2C 引脚、时钟及总线状态。 */
static void dvc_bus_init(void)
{
    uint8_t write_addr = DVC1124_FIXED_WRITE_ADDR;

    (void)DVC1124_ResolveWriteAddress(s_cfg.model, s_cfg.addr_mode,
                                      s_cfg.hardwire_code,
                                      s_cfg.explicit_write_addr,
                                      &write_addr);
    i2c_gpio_set(I2C_GPIO_GROUP_C0C1);
    i2c_master_init(write_addr,
                    (unsigned char)(CLOCK_SYS_CLOCK_HZ / (4u * 100000u)));
    s_bus_initialized = 1u;
}

/* 复位 I2C 控制器并恢复总线配置。 */
static void dvc_bus_recover(void)
{
    reset_i2c_module();
    s_bus_initialized = 0u;
    dvc_bus_init();
}

/* Telink I2C 硬件每次 BUSY 等待都必须有时间边界。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
static uint8_t dvc_i2c_wait_done(void) __attribute__((noinline));
static uint8_t dvc_i2c_wait_done(void)
{
    uint32_t tick = clock_time();

    while ((reg_i2c_status & FLD_I2C_CMD_BUSY) != 0u)
    {
        if (clock_time_exceed(tick, DVC1124_I2C_CMD_TIMEOUT_US))
        {
            return 0u;
        }
    }
    return 1u;
}

/* 检查 I2C 地址阶段是否得到有效应答。 */
static uint8_t dvc_i2c_address_ok(void)
{
    return ((reg_i2c_status & FLD_I2C_NAK) == 0u) ? 1u : 0u;
}

/* 发送 I2C STOP 并结束当前事务。 */
static uint8_t dvc_i2c_stop(void)
{
    reg_i2c_ctrl = FLD_I2C_CMD_STOP;
    return dvc_i2c_wait_done();
}

/* 执行带超时边界的原始 I2C 写事务。 */
static uint8_t dvc_i2c_write_raw(uint8_t write_addr,
                                 uint8_t reg,
                                 const uint8_t *data,
                                 uint16_t len)
{
    uint16_t i;

    reg_i2c_id = (uint8_t)(write_addr & (uint8_t)~FLD_I2C_WRITE_READ_BIT);
    reg_i2c_adr = reg;
    reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_START);
    if (!dvc_i2c_wait_done() || !dvc_i2c_address_ok()) return 0u;

    for (i = 0u; i < len; ++i)
    {
        reg_i2c_di = data[i];
        reg_i2c_ctrl = FLD_I2C_CMD_DI;
        if (!dvc_i2c_wait_done() || !dvc_i2c_address_ok()) return 0u;
    }
    return dvc_i2c_stop();
}

/* 执行带超时边界的原始 I2C 读事务。 */
static uint8_t dvc_i2c_read_raw(uint8_t write_addr,
                                uint8_t reg,
                                uint8_t *data,
                                uint16_t len)
{
    uint16_t i;

    if (len == 0u) return 0u;

    reg_i2c_id = (uint8_t)(write_addr & (uint8_t)~FLD_I2C_WRITE_READ_BIT);
    reg_i2c_adr = reg;
    reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_START);
    if (!dvc_i2c_wait_done() || !dvc_i2c_address_ok()) return 0u;

    reg_i2c_id = (uint8_t)(write_addr | FLD_I2C_WRITE_READ_BIT);
    reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_START);
    if (!dvc_i2c_wait_done() || !dvc_i2c_address_ok()) return 0u;

    for (i = 0u; i + 1u < len; ++i)
    {
        reg_i2c_ctrl = (FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID);
        if (!dvc_i2c_wait_done()) return 0u;
        data[i] = reg_i2c_di;
    }

    /* Telink SDK 在最后一个字节设置 FLD_I2C_CMD_ACK，以结束读取。 */
    reg_i2c_ctrl = (FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID | FLD_I2C_CMD_ACK);
    if (!dvc_i2c_wait_done()) return 0u;
    data[len - 1u] = reg_i2c_di;
    return dvc_i2c_stop();
}

/* 从大端字节序读取 16 位无符号值。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
static uint16_t dvc_be16(const uint8_t *p) __attribute__((noinline));
static uint16_t dvc_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* 把 20 位补码读数扩展为有符号 32 位值。 */
static int32_t dvc_sign_extend20(uint32_t raw)
{
    raw &= 0x000FFFFFu;
    if ((raw & 0x00080000u) != 0u) raw |= 0xFFF00000u;
    return (int32_t)raw;
}

/* 计算有符号电流的绝对量。 */
static uint32_t dvc_abs_i32(int32_t value)
{
    return (value < 0) ? (0u - (uint32_t)value) : (uint32_t)value;
}

/* 按分流电阻将 CC2 原始读数换算为毫安。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
static int32_t dvc_cc2_to_raw_current_ma(int32_t cc2) __attribute__((noinline));
static int32_t dvc_cc2_to_raw_current_ma(int32_t cc2)
{
    int32_t current_num;

    if (s_cfg.shunt_uohm == 0u) return 0;
    /* CC2 的 LSB 为 0.3125 uV；约分后使有符号 20 位运算保持在 int32 范围内。 */
    current_num = cc2 * 625;
    return current_num / ((int32_t)s_cfg.shunt_uohm * 2);
}

/* 将启动零点偏移应用到电流读数。 */
static int32_t dvc_apply_boot_zero(int32_t calibrated_ma)
{
    if (s_boot_zero.status != DVC1124_BOOT_ZERO_VALID) return calibrated_ma;
    return calibrated_ma - s_boot_zero.learned_offset_ma;
}

/* 发布启动零点校准结果和诊断计数。 */
static void dvc_boot_zero_publish_diag(void)
{
    int32_t factory_offset = 0;
    uint32_t factory_gain = 1000000u;
    uint32_t shunt = s_cfg.shunt_uohm;
    (void)bms_config_get_current_calibration(&factory_offset, &factory_gain);

    bms_diag_boot_word(DVC_BOOT_ZERO_DIAG_STATUS, (uint16_t)s_boot_zero.status);
    bms_diag_boot_word(DVC_BOOT_ZERO_DIAG_SAMPLE_COUNT, s_boot_zero.sample_count);
    bms_diag_boot_u32(DVC_BOOT_ZERO_DIAG_OFFSET_MA, (uint32_t)s_boot_zero.learned_offset_ma);
    bms_diag_boot_u32(DVC_BOOT_ZERO_DIAG_RAW1_MA, (uint32_t)s_boot_zero.raw_sample1_ma);
    bms_diag_boot_u32(DVC_BOOT_ZERO_DIAG_RAW2_MA, (uint32_t)s_boot_zero.raw_sample2_ma);
    bms_diag_boot_u32(DVC_BOOT_ZERO_DIAG_CAL1_MA, (uint32_t)s_boot_zero.calibrated_sample1_ma);
    bms_diag_boot_u32(DVC_BOOT_ZERO_DIAG_CAL2_MA, (uint32_t)s_boot_zero.calibrated_sample2_ma);
    bms_diag_boot_u32(DVC_BOOT_ZERO_DIAG_FACTORY_OFF_MA, (uint32_t)factory_offset);
    bms_diag_boot_u32(DVC_BOOT_ZERO_DIAG_FACTORY_GAIN, factory_gain);
    bms_diag_boot_word(DVC_BOOT_ZERO_DIAG_SHUNT_UOHM,
                       (uint16_t)((shunt > 65535u) ? 65535u : shunt));
    bms_diag_boot_word(DVC_BOOT_ZERO_DIAG_SPREAD_MA, s_boot_zero.spread_ma);
}

/* 有界等待 CC2 下一次转换，避免重复采集旧读数。 */
static uint8_t dvc_boot_zero_wait_fresh_cc2(void)
{
    uint16_t remaining = DVC1124_BOOT_ZERO_SAMPLE_INTERVAL_MS;
    uint8_t status;
    uint8_t fresh;

    /* RM p6：CC2F读清；先排除前一次转换，270ms等待本身不是新样本证据。 */
    if (!DVC1124_ReadRegisters(DVC1124_REG_STATUS, &status, 1u)) return 0u;
    s_pending_adc_events = 0u;

    while (remaining != 0u)
    {
        uint16_t slice = (remaining > 90u) ? 90u : remaining;
#if (MODULE_WATCHDOG_ENABLE)
        wd_clear();
#endif
        dvc_delay_ms(slice);
        remaining = (uint16_t)(remaining - slice);
    }
#if (MODULE_WATCHDOG_ENABLE)
    wd_clear();
#endif
    fresh = DVC1124_ReadRegisters(DVC1124_REG_STATUS, &status, 1u) &&
        (s_pending_adc_events & DVC1124_STATUS_CC2F_MASK);
    /* 启动校准独占这些事件；正常采样从初始化完成后的新事件开始。 */
    s_pending_adc_events = 0u;
    s_adc_wait_started = pm_get_32k_tick();
    return fresh ? 1u : 0u;
}

/* 校准前关闭全部 FET 并检查写入结果。 */
static uint8_t dvc_boot_zero_force_all_fets_off(void)
{
    uint8_t current;
    uint8_t target;
    const uint8_t path_mask = (uint8_t)(DVC1124_FET_PDSGC_MASK |
                                        DVC1124_FET_PCHGC_MASK |
                                        DVC1124_FET_DSGC_MASK |
                                        DVC1124_FET_CHGC_MASK);

    DVC1124_SetOutputEnabled(0u);
    if (!DVC1124_ReadRegisters(DVC1124_REG_FET_CTRL, &current, 1u)) return 0u;
    target = (uint8_t)(current & (uint8_t)~path_mask);
    return DVC1124_WriteRegisterSafe(DVC1124_REG_FET_CTRL, target);
}

/* 读取零点校准所需的电流和 FET 状态。 */
static uint8_t dvc_boot_zero_read_sample(int32_t *raw_ma,
                                         int32_t *calibrated_ma,
                                         dvc1124_boot_zero_status_t *failure)
{
    uint8_t ctrl;
    uint8_t cc2_data[3];
    uint32_t raw20;
    int32_t cc2;
    const uint8_t command_mask = (uint8_t)(DVC1124_FET_PDSGC_MASK |
                                           DVC1124_FET_PCHGC_MASK |
                                           DVC1124_FET_DSGC_MASK |
                                           DVC1124_FET_CHGC_MASK);
    const uint8_t flag_mask = (uint8_t)(DVC1124_CC2_PDSGF_MASK |
                                        DVC1124_CC2_PCHGF_MASK |
                                        DVC1124_CC2_DSGF_MASK |
                                        DVC1124_CC2_CHGF_MASK);

    if ((raw_ma == 0) || (calibrated_ma == 0) || (failure == 0)) return 0u;
    if (!DVC1124_ReadRegisters(DVC1124_REG_FET_CTRL, &ctrl, 1u))
    {
        *failure = DVC1124_BOOT_ZERO_FET_IO_ERROR;
        return 0u;
    }
    if ((ctrl & command_mask) != 0u)
    {
        *failure = DVC1124_BOOT_ZERO_FET_ACTIVE;
        return 0u;
    }
    if (!DVC1124_ReadRegisters(DVC1124_REG_CC2_H, cc2_data, 3u))
    {
        *failure = DVC1124_BOOT_ZERO_SAMPLE_IO_ERROR;
        return 0u;
    }
    if ((cc2_data[2] & flag_mask) != 0u)
    {
        *failure = DVC1124_BOOT_ZERO_FET_ACTIVE;
        return 0u;
    }

    raw20 = ((uint32_t)cc2_data[0] << 12) |
            ((uint32_t)cc2_data[1] << 4) |
            ((uint32_t)cc2_data[2] >> 4);
    cc2 = dvc_sign_extend20(raw20);
    *raw_ma = dvc_cc2_to_raw_current_ma(cc2);
    *calibrated_ma = bms_config_calibrate_current(*raw_ma);
    return 1u;
}

/* 启动阶段确认 FET 关闭后采集残余电流并校准零点。 */
uint8_t DVC1124_BootCurrentZeroCalibrate(void)
{
    dvc1124_boot_zero_status_t failure = DVC1124_BOOT_ZERO_SAMPLE_IO_ERROR;
    int32_t sum;
    uint32_t spread;

    if (s_boot_zero.status != DVC1124_BOOT_ZERO_NOT_ATTEMPTED)
        return (s_boot_zero.status == DVC1124_BOOT_ZERO_VALID ||
                s_boot_zero.status == DVC1124_BOOT_ZERO_DISABLED) ? 1u : 0u;

#if !DVC1124_BOOT_ZERO_ENABLE
    s_boot_zero.status = DVC1124_BOOT_ZERO_DISABLED;
    dvc_boot_zero_publish_diag();
    return 1u;
#else
    memset(&s_boot_zero, 0, sizeof(s_boot_zero));
    s_boot_zero.status = DVC1124_BOOT_ZERO_IN_PROGRESS;
    dvc_boot_zero_publish_diag();

    if (!dvc_boot_zero_force_all_fets_off())
    {
        s_boot_zero.status = DVC1124_BOOT_ZERO_FET_IO_ERROR;
        dvc_boot_zero_publish_diag();
        return 0u;
    }
    if (!DVC1124_StartCadcCalibration())
    {
        s_boot_zero.status = DVC1124_BOOT_ZERO_CAMZ_ERROR;
        dvc_boot_zero_publish_diag();
        return 0u;
    }

    if (!dvc_boot_zero_wait_fresh_cc2() ||
        !dvc_boot_zero_read_sample(&s_boot_zero.raw_sample1_ma,
                                   &s_boot_zero.calibrated_sample1_ma,
                                   &failure))
    {
        s_boot_zero.status = failure;
        dvc_boot_zero_publish_diag();
        return 0u;
    }
    s_boot_zero.sample_count = 1u;

    if (!dvc_boot_zero_wait_fresh_cc2() ||
        !dvc_boot_zero_read_sample(&s_boot_zero.raw_sample2_ma,
                                   &s_boot_zero.calibrated_sample2_ma,
                                   &failure))
    {
        s_boot_zero.status = failure;
        dvc_boot_zero_publish_diag();
        return 0u;
    }
    s_boot_zero.sample_count = 2u;

    if ((dvc_abs_i32(s_boot_zero.calibrated_sample1_ma) > DVC1124_BOOT_ZERO_MAX_ABS_MA) ||
        (dvc_abs_i32(s_boot_zero.calibrated_sample2_ma) > DVC1124_BOOT_ZERO_MAX_ABS_MA))
    {
        s_boot_zero.status = DVC1124_BOOT_ZERO_OUT_OF_RANGE;
        dvc_boot_zero_publish_diag();
        return 0u;
    }

    spread = dvc_abs_i32(s_boot_zero.calibrated_sample2_ma -
                         s_boot_zero.calibrated_sample1_ma);
    s_boot_zero.spread_ma = (uint16_t)((spread > 65535u) ? 65535u : spread);
    if (spread > DVC1124_BOOT_ZERO_MAX_SPREAD_MA)
    {
        s_boot_zero.status = DVC1124_BOOT_ZERO_UNSTABLE;
        dvc_boot_zero_publish_diag();
        return 0u;
    }

    sum = s_boot_zero.calibrated_sample1_ma + s_boot_zero.calibrated_sample2_ma;
    s_boot_zero.learned_offset_ma = (sum >= 0) ? ((sum + 1) / 2) : ((sum - 1) / 2);
    s_boot_zero.status = DVC1124_BOOT_ZERO_VALID;
    dvc_boot_zero_publish_diag();
    return 1u;
#endif
}

/* V1.2 第 32 页的共模修正查表，K 放大 10000 倍。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
static uint16_t dvc_cell_k_x10000(uint32_t common_mode_mv) __attribute__((noinline));
static uint16_t dvc_cell_k_x10000(uint32_t common_mode_mv)
{
    if (common_mode_mv < 24000u) return 10000u;
    if (common_mode_mv < 40000u) return 9999u;
    if (common_mode_mv < 51000u) return 9998u;
    if (common_mode_mv < 60000u) return 9997u;
    if (common_mode_mv < 68000u) return 9996u;
    if (common_mode_mv < 75000u) return 9995u;
    if (common_mode_mv < 82000u) return 9994u;
    if (common_mode_mv < 88000u) return 9993u;
    if (common_mode_mv < 93000u) return 9992u;
    if (common_mode_mv < 99000u) return 9991u;
    if (common_mode_mv < 104000u) return 9990u;
    if (common_mode_mv < 109000u) return 9989u;
    if (common_mode_mv < 113000u) return 9988u;
    if (common_mode_mv < 117000u) return 9987u;
    return 9986u;
}

/* 按单体校准系数修正电压毫伏值。 */
static uint16_t dvc_correct_cell_mv(uint16_t raw_code, uint32_t common_mode_mv)
{
    uint32_t k = dvc_cell_k_x10000(common_mode_mv);
    uint32_t numerator = (uint32_t)raw_code * 10000u;
    uint32_t denominator = k * 10u; /* 原始 LSB 为 0.1 mV。 */
    return (uint16_t)((numerator + denominator / 2u) / denominator);
}

/* 写入连续寄存器并回读验证整块数据。 */
static uint8_t dvc_write_verified_block(uint8_t reg, const uint8_t *data, uint8_t len)
{
    uint8_t verify[8];
    uint8_t attempt;

    if ((data == NULL) || (len == 0u) || (len > sizeof(verify))) return 0u;

    for (attempt = 0u; attempt < DVC1124_I2C_RETRY_COUNT; ++attempt)
    {
        if (DVC1124_WriteRegisters(reg, data, len) &&
            DVC1124_ReadRegisters(reg, verify, len) &&
            (memcmp(data, verify, len) == 0))
        {
            return 1u;
        }
        dvc_delay_ms(1u);
    }
    return 0u;
}

/* 写入寄存器并回读确认实际值。 */
static uint8_t dvc_write_verified(uint8_t reg, uint8_t value)
{
    return dvc_write_verified_block(reg, &value, 1u);
}

/* 保留 clear_mask 之外的全部位，只校验本次操作负责的位。 */
static uint8_t dvc_update_reg(uint8_t reg, uint8_t clear_mask, uint8_t set_mask)
{
    uint8_t value;
    uint8_t target;
    uint8_t readback;
    uint8_t attempt;

    for (attempt = 0u; attempt < DVC1124_I2C_RETRY_COUNT; ++attempt)
    {
        if (!DVC1124_ReadRegisters(reg, &value, 1u))
        {
            dvc_delay_ms(1u);
            continue;
        }
        target = (uint8_t)((value & (uint8_t)~clear_mask) | (set_mask & clear_mask));
        if (DVC1124_WriteRegisters(reg, &target, 1u) &&
            DVC1124_ReadRegisters(reg, &readback, 1u) &&
            ((readback & clear_mask) == (target & clear_mask)))
        {
            return 1u;
        }
        dvc_delay_ms(1u);
    }
    return 0u;
}

#if DVC1124_HW_PROTECT_ENABLE
/* 选择不超过请求值的最大可用电压保护延时。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
static uint8_t dvc_voltage_delay_code(uint32_t requested_ms, uint16_t *actual_ms) __attribute__((noinline));
static uint8_t dvc_voltage_delay_code(uint32_t requested_ms, uint16_t *actual_ms)
{
    static const uint16_t table_ms[16] = {
        200u, 300u, 400u, 500u, 600u, 700u, 800u, 900u,
        1000u, 2000u, 3000u, 4000u, 5000u, 6000u, 7000u, 8000u
    };
    int i;

    if (requested_ms < table_ms[0])
    {
        *actual_ms = table_ms[0];
        return 0u;
    }
    for (i = 15; i >= 0; --i)
    {
        if (requested_ms >= table_ms[i])
        {
            *actual_ms = table_ms[i];
            return (uint8_t)i;
        }
    }
    *actual_ms = table_ms[0];
    return 0u;
}

/* OC1/OC2 延时为 (code+1)*step；向下取整，避免保护晚于请求时间。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
static uint8_t dvc_linear_delay_code(uint32_t requested_ms,
                                     uint16_t step_ms,
                                     uint16_t *actual_ms) __attribute__((noinline));
static uint8_t dvc_linear_delay_code(uint32_t requested_ms,
                                     uint16_t step_ms,
                                     uint16_t *actual_ms)
{
    uint32_t steps;

    if (requested_ms < step_ms)
    {
        *actual_ms = step_ms;
        return 0u;
    }
    steps = requested_ms / step_ms;
    if (steps == 0u) steps = 1u;
    if (steps > 256u) steps = 256u;
    *actual_ms = (uint16_t)(steps * step_ms);
    return (uint8_t)(steps - 1u);
}

/* 按分流电阻将采样压差微伏值换算为电流。 */
static uint16_t dvc_current_from_sense_uv(uint32_t sense_uv)
{
    uint32_t value;
    if (s_cfg.shunt_uohm == 0u) return 0u;
    value = (sense_uv * 10u + s_cfg.shunt_uohm / 2u) / s_cfg.shunt_uohm;
    if (value > 65535u) value = 65535u;
    return (uint16_t)value;
}

/* 将电流阈值量化为一级过流寄存器编码。 */
static uint8_t dvc_current_to_oc1_code(uint16_t requested_a_x10, uint16_t *actual_a_x10)
{
    uint32_t sense_uv;
    uint32_t code;

    if (requested_a_x10 == 0u)
    {
        *actual_a_x10 = 0u;
        return 0u;
    }
    if (s_cfg.shunt_uohm == 0u)
    {
        *actual_a_x10 = 0u;
        return 0u;
    }

    sense_uv = ((uint32_t)requested_a_x10 * s_cfg.shunt_uohm) / 10u;
    code = sense_uv / 250u; /* 阈值为 code * 0.25 mV。 */
    if (code == 0u) code = 1u;
    if (code > 255u) code = 255u;
    *actual_a_x10 = dvc_current_from_sense_uv(code * 250u);
    return (uint8_t)code;
}

/* 将电流阈值量化为二级过流寄存器编码。 */
static uint8_t dvc_current_to_oc2_code(uint16_t requested_a_x10, uint16_t *actual_a_x10)
{
    uint32_t sense_uv;
    uint32_t index;

    if (requested_a_x10 == 0u)
    {
        *actual_a_x10 = 0u;
        return 0u;
    }
    if (s_cfg.shunt_uohm == 0u)
    {
        *actual_a_x10 = 0u;
        return 0u;
    }

    sense_uv = ((uint32_t)requested_a_x10 * s_cfg.shunt_uohm) / 10u;
    index = sense_uv / 4000u; /* 阈值为 (code+1)*4 mV。 */
    if (index == 0u) index = 1u; /* 硬件最小阈值为 4 mV。 */
    if (index > 64u) index = 64u;
    *actual_a_x10 = dvc_current_from_sense_uv(index * 4000u);
    return (uint8_t)(index - 1u);
}

/* 请求值与实际量化值不同时置位对应诊断标志。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
static void dvc_note_quant(uint32_t bit, uint32_t requested, uint32_t actual) __attribute__((noinline));
static void dvc_note_quant(uint32_t bit, uint32_t requested, uint32_t actual)
{
    if (requested != actual) s_applied.quantized_mask |= bit;
}

#endif

/* 按有效串数配置电芯通道掩码。 */
static uint8_t dvc_apply_cell_masks(void)
{
    uint8_t mask[3] = {0u, 0u, 0u};
    uint8_t cell;

    /* CM[5]..CM[24] 屏蔽未使用的高位通道；DVC1124-2 支持 4..24S。 */
    for (cell = 5u; cell <= DVC1124_MAX_CELLS; ++cell)
    {
        if (cell <= s_cfg.cell_count) continue;
        if (cell >= 17u)
            mask[0] |= (uint8_t)(1u << (cell - 17u));
        else if (cell >= 9u)
            mask[1] |= (uint8_t)(1u << (cell - 9u));
        else
            mask[2] |= (uint8_t)(1u << (cell - 1u));
    }
    return dvc_write_verified_block(DVC1124_REG_CELL_MASK_24_17, mask, 3u);
}

/* 校验并编码 DVC 电流唤醒配置。 */
static uint8_t DVC1124_EncodeCurrentWake(uint16_t threshold_uv, uint8_t *code)
{
    if (code == 0) return 0u;
    if (threshold_uv == 0u) { *code = 0u; return 1u; }
    if ((threshold_uv < 10u) || (threshold_uv > 2550u) || ((threshold_uv % 10u) != 0u)) return 0u;
    *code = (uint8_t)(threshold_uv / 10u);
    return 1u;
}

/* 校验并编码共口体二极管恢复配置。 */
static uint8_t DVC1124_EncodeBodyDiode(uint16_t threshold_uv, uint8_t *code)
{
    if (code == 0) return 0u;
    if (threshold_uv == 0u) { *code = 0u; return 1u; }
    if ((threshold_uv < 40u) || (threshold_uv > 10200u) || ((threshold_uv % 40u) != 0u)) return 0u;
    *code = (uint8_t)(threshold_uv / 40u);
    return 1u;
}

/* 校验并编码 I2C 硬件看门狗配置。 */
static uint8_t DVC1124_EncodeI2cWatchdog(uint8_t seconds, dvc1124_i2c_wdt_code_t *code)
{
    if (code == 0) return 0u;
    switch (seconds)
    {
    case 0u:  *code = DVC1124_I2C_WDT_OFF; return 1u;
    case 4u:  *code = DVC1124_I2C_WDT_4S; return 1u;
    case 8u:  *code = DVC1124_I2C_WDT_8S; return 1u;
    case 16u: *code = DVC1124_I2C_WDT_16S; return 1u;
    case 32u: *code = DVC1124_I2C_WDT_32S; return 1u;
    default: return 0u;
    }
}

/* 应用固定板级 AFE 工作配置。 */
static uint8_t dvc_apply_basic_config(void)
{
    uint8_t ok = 1u;
    uint8_t current_wake_code;
    uint8_t body_diode_code;
    dvc1124_i2c_wdt_code_t watchdog_code;
    uint8_t cpvs_bits;
    uint8_t cadc_bits = 0u;
    uint8_t dsg_mask = DVC1124_DEFAULT_DSG_MASK_POLICY;
    uint8_t chg_mask = DVC1124_DEFAULT_CHG_MASK_POLICY;

    if (DVC1124_CHARGE_PUMP_VOLTAGE_CODE > 7u) return 0u;
    if (!DVC1124_EncodeCurrentWake(DVC1124_CURRENT_WAKE_THRESHOLD_UV, &current_wake_code)) return 0u;
    if (!DVC1124_EncodeBodyDiode(DVC1124_BODY_DIODE_THRESHOLD_UV, &body_diode_code)) return 0u;
    if (!DVC1124_EncodeI2cWatchdog(DVC1124_I2C_WATCHDOG_SECONDS, &watchdog_code)) return 0u;

    ok &= dvc_apply_cell_masks();
    ok &= dvc_write_verified(DVC1124_REG_GP123_MODE, DVC1124_GP123_MODE_VALUE);
    ok &= dvc_write_verified(DVC1124_REG_GP456_MODE, DVC1124_GP456_MODE_VALUE);

    /*
     * HS-D008 使用 GP5/GP6 低侧 CHG/DSG。严格应用已审查默认值，
     * 避免复位配置瞬间解除未用高侧路径的屏蔽，或在 CWT 为零时启用 CAES。
     */
    if (DVC1124_DEFAULT_HIGH_SIDE_FET_MASK) cadc_bits |= DVC1124_CADC_HSFM_MASK;
    if (DVC1124_DEFAULT_CADC_WORK_ENABLE) cadc_bits |= DVC1124_CADC_CAEW_MASK;
    if (DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE) cadc_bits |= DVC1124_CADC_CAES_MASK;
    ok &= dvc_update_reg(DVC1124_REG_CADC_CTRL,
                         (uint8_t)(DVC1124_CADC_HSFM_MASK |
                                   DVC1124_CADC_CAEW_MASK |
                                   DVC1124_CADC_CAES_MASK),
                         cadc_bits);

    cpvs_bits = DVC1124_FIELD_PREP(DVC1124_CPVS_MASK,
                                    DVC1124_CPVS_SHIFT,
                                    DVC1124_CHARGE_PUMP_VOLTAGE_CODE);
    ok &= dvc_update_reg(DVC1124_REG_CP_CTRL,
                         (uint8_t)(DVC1124_CPVS_MASK |
                                   DVC1124_COW_MASK |
                                   DVC1124_CMM_MASK |
                                   DVC1124_CVS_MASK),
                         cpvs_bits); /* 配置值：COW=0、CMM=0、CVS=0。 */

    /* 启用 VADC，与每个 CC2 周期同步，测量时间为 1.54 ms。 */
    ok &= dvc_update_reg(DVC1124_REG_VADC_CTRL,
                         (uint8_t)(DVC1124_VADC_ENABLE_MASK |
                                   DVC1124_VADC_SYNC_MASK |
                                   DVC1124_VADC_PERIOD_MASK |
                                   DVC1124_VADC_TIME_MASK),
                         (uint8_t)(DVC1124_VADC_ENABLE_MASK |
                                   DVC1124_VADC_SYNC_MASK |
                                   DVC1124_VADC_TIME_1P54MS));

    ok &= dvc_write_verified(DVC1124_REG_CURRENT_WAKE, current_wake_code);
    ok &= dvc_write_verified(DVC1124_REG_BODY_DIODE, body_diode_code);
    ok &= dvc_update_reg(DVC1124_REG_I2C_WDT,
                         DVC1124_I2C_WDT_TIME_MASK,
                         watchdog_code);

#if DVC1124_I2C_TIMEOUT_CLOSE_DSG
    dsg_mask &= (uint8_t)~DVC1124_DSGMASK_DWM_MASK;
#else
    dsg_mask |= DVC1124_DSGMASK_DWM_MASK;
#endif
#if DVC1124_I2C_TIMEOUT_CLOSE_CHG
    chg_mask &= (uint8_t)~DVC1124_CHGMASK_CWM_MASK;
#else
    chg_mask |= DVC1124_CHGMASK_CWM_MASK;
#endif
    ok &= dvc_write_verified(DVC1124_REG_DSG_MASK, dsg_mask);
    ok &= dvc_write_verified(DVC1124_REG_CHG_MASK, chg_mask);

    /* 启动时保持安全状态，稍后由现有 mos_update() 请求应用输出状态。 */
    ok &= DVC1124_SetMosState(0u, 0u);
    return ok;
}

#if !DVC1124_HW_PROTECT_ENABLE
/* 台架隔离模式下关闭 AFE 阈值保护路径。 */
static uint8_t dvc_disable_threshold_protection(void)
{
    static const uint8_t disabled_vprot[2] = {0u, 0u};
    const uint8_t alarm_mask = (uint8_t)(DVC1124_ALARM_COV_MASK |
                                         DVC1124_ALARM_CUV_MASK |
                                         DVC1124_ALARM_OCD1_MASK |
                                         DVC1124_ALARM_OCC1_MASK |
                                         DVC1124_ALARM_OCD2_MASK |
                                         DVC1124_ALARM_OCC2_MASK |
                                         DVC1124_ALARM_SCD_MASK);
    uint8_t ok = 1u;

    /*
     * V1.2 规定 COV/CUV 与 OC1 的零阈值/编码表示关闭；OC2/SCD 使用独立使能位。
     * 同时屏蔽所有自主 FET 关闭来源，
     * 并关闭 watchdog、体二极管、电流唤醒和内核过温行为，
     * 使 HW=0 真正成为仅软件保护的台架模式，而非仅忽略标志。
     * 仍可直接通过 0x51 CHGC/DSGC 控制。
     */
    memset(&s_applied, 0, sizeof(s_applied));
    ok &= dvc_write_verified_block(DVC1124_REG_COV_H, disabled_vprot, 2u);
    ok &= dvc_write_verified_block(DVC1124_REG_CUV_H, disabled_vprot, 2u);
    ok &= dvc_write_verified(DVC1124_REG_OCD1_THR, 0u);
    ok &= dvc_write_verified(DVC1124_REG_OCC1_THR, 0u);
    ok &= dvc_write_verified(DVC1124_REG_OCD1_DLY, 0u);
    ok &= dvc_write_verified(DVC1124_REG_OCC1_DLY, 0u);
    ok &= dvc_update_reg(DVC1124_REG_OCD2,
                         (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK),
                         0u);
    ok &= dvc_update_reg(DVC1124_REG_OCC2,
                         (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK),
                         0u);
    ok &= dvc_write_verified(DVC1124_REG_OCD2_DLY, 0u);
    ok &= dvc_write_verified(DVC1124_REG_OCC2_DLY, 0u);
    ok &= DVC1124_SetShortCircuitProtection(0u, 0u);
    ok &= dvc_write_verified(DVC1124_REG_CURRENT_WAKE, 0u);
    ok &= dvc_write_verified(DVC1124_REG_BODY_DIODE, 0u);
    ok &= DVC1124_SetCoreOtThresholdCode(0u);
    ok &= dvc_update_reg(DVC1124_REG_I2C_WDT,
                         DVC1124_I2C_WDT_TIME_MASK,
                         DVC1124_I2C_WDT_OFF);
    ok &= dvc_write_verified(DVC1124_REG_DSG_MASK, 0xFFu);
    ok &= dvc_write_verified(DVC1124_REG_CHG_MASK, 0xFFu);
    if (ok) ok &= DVC1124_ClearAlarmFlags(alarm_mask);
    return ok;
}
#endif

/* 将独立硬件保护配置量化并写入 DVC。 */
static uint8_t dvc_apply_protection_from_params(void)
{
#if !DVC1124_HW_PROTECT_ENABLE
    return dvc_disable_threshold_protection();
#else
    bms_afe_hw_profile_t hw;
    dvc1124_config_t cfg;
    uint16_t cov_mv;
    uint16_t cuv_mv;
    uint16_t request;
    uint32_t cov_req_dly;
    uint32_t cuv_req_dly;
    uint32_t ocd1_req_dly;
    uint32_t ocd2_req_dly;
    uint32_t occ1_req_dly;
    uint32_t occ2_req_dly;
    uint32_t sc_sense_uv;
    uint16_t sc_mv;
    uint8_t buf[2];
    uint8_t code;
    uint16_t code12;
    uint16_t actual;
    uint8_t ok = 1u;

    if (!bms_afe_hw_profile_get(&hw)) return 0u;
    DVC1124_GetConfig(&cfg);
    if (cfg.shunt_uohm == 0u) return 0u;

    cov_mv = (hw.enable_mask & BMS_AFE_HW_EN_COV) ? hw.cov_mv : 0u;
    cuv_mv = (hw.enable_mask & BMS_AFE_HW_EN_CUV) ? hw.cuv_mv : 0u;
    cov_req_dly = hw.cov_delay_ms;
    cuv_req_dly = hw.cuv_delay_ms;
    ocd1_req_dly = hw.ocd1_delay_ms;
    ocd2_req_dly = hw.ocd2_delay_ms;
    occ1_req_dly = hw.occ1_delay_ms;
    occ2_req_dly = hw.occ2_delay_ms;

    memset(&s_applied, 0, sizeof(s_applied));

    if (cov_mv == 0u) {
        code12 = 0u;
    } else {
        if ((cov_mv < 501u) || (cov_mv > 4595u)) return 0u;
        code12 = (uint16_t)(cov_mv - 500u);
        s_applied.cov_mv = (uint16_t)(code12 + 500u);
    }
    code = dvc_voltage_delay_code(cov_req_dly, &actual);
    s_applied.cov_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_COV_DLY, cov_req_dly, actual);
    buf[0] = (uint8_t)(code12 >> 4);
    buf[1] = (uint8_t)(((code12 & 0x0Fu) << 4) | code);
    ok &= dvc_write_verified_block(DVC1124_REG_COV_H, buf, 2u);

    if (cuv_mv == 0u) {
        code12 = 0u;
    } else {
        if (cuv_mv > 4095u) return 0u;
        code12 = cuv_mv;
        s_applied.cuv_mv = code12;
    }
    code = dvc_voltage_delay_code(cuv_req_dly, &actual);
    s_applied.cuv_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_CUV_DLY, cuv_req_dly, actual);
    buf[0] = (uint8_t)(code12 >> 4);
    buf[1] = (uint8_t)(((code12 & 0x0Fu) << 4) | code);
    ok &= dvc_write_verified_block(DVC1124_REG_CUV_H, buf, 2u);

    request = (hw.enable_mask & BMS_AFE_HW_EN_OCD1) ? hw.ocd1_a10 : 0u;
    code = dvc_current_to_oc1_code(request, &actual);
    s_applied.ocd1_a_x10 = actual;
    dvc_note_quant(DVC_QUANT_OCD1_THR, request, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCD1_THR, code);

    request = (hw.enable_mask & BMS_AFE_HW_EN_OCC1) ? hw.occ1_a10 : 0u;
    code = dvc_current_to_oc1_code(request, &actual);
    s_applied.occ1_a_x10 = actual;
    dvc_note_quant(DVC_QUANT_OCC1_THR, request, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCC1_THR, code);

    code = dvc_linear_delay_code(ocd1_req_dly, 8u, &actual);
    s_applied.ocd1_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCD1_DLY, ocd1_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCD1_DLY, code);

    code = dvc_linear_delay_code(occ1_req_dly, 8u, &actual);
    s_applied.occ1_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCC1_DLY, occ1_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCC1_DLY, code);

    request = (hw.enable_mask & BMS_AFE_HW_EN_OCD2) ? hw.ocd2_a10 : 0u;
    if (request == 0u) {
        ok &= dvc_update_reg(DVC1124_REG_OCD2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK), 0u);
    } else {
        code = dvc_current_to_oc2_code(request, &actual);
        s_applied.ocd2_a_x10 = actual;
        dvc_note_quant(DVC_QUANT_OCD2_THR, request, actual);
        ok &= dvc_update_reg(DVC1124_REG_OCD2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK),
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | code));
    }

    request = (hw.enable_mask & BMS_AFE_HW_EN_OCC2) ? hw.occ2_a10 : 0u;
    if (request == 0u) {
        ok &= dvc_update_reg(DVC1124_REG_OCC2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK), 0u);
    } else {
        code = dvc_current_to_oc2_code(request, &actual);
        s_applied.occ2_a_x10 = actual;
        dvc_note_quant(DVC_QUANT_OCC2_THR, request, actual);
        ok &= dvc_update_reg(DVC1124_REG_OCC2,
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | DVC1124_OC2_THRESHOLD_MASK),
                             (uint8_t)(DVC1124_OC2_ENABLE_MASK | code));
    }

    code = dvc_linear_delay_code(ocd2_req_dly, 4u, &actual);
    s_applied.ocd2_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCD2_DLY, ocd2_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCD2_DLY, code);

    code = dvc_linear_delay_code(occ2_req_dly, 4u, &actual);
    s_applied.occ2_delay_ms = actual;
    dvc_note_quant(DVC_QUANT_OCC2_DLY, occ2_req_dly, actual);
    ok &= dvc_write_verified(DVC1124_REG_OCC2_DLY, code);

    sc_mv = 0u;
    if (hw.enable_mask & BMS_AFE_HW_EN_SC) {
        sc_sense_uv = ((uint32_t)hw.sc_a10 * cfg.shunt_uohm) / 10u;
        /* 向下量化为 10 mV 编码，使实际硬件动作阈值不高于请求的物理电流阈值。 */
        sc_mv = (uint16_t)((sc_sense_uv / 10000u) * 10u);
        if (sc_mv < 10u || sc_mv > 630u) return 0u;
    }
    ok &= DVC1124_SetShortCircuitProtection(sc_mv, hw.sc_delay_us);
    return ok;
#endif
}

/* 更新 DVC 通信结果及错误状态。 */
static void dvc_note_comm_result(uint8_t ok)
{
    if (ok)
    {
        g_bms_system_status.bits.b1Status_AFE1 = 1u;
        bms_error_clear(BMS_ERROR_AFE1);
    }
    else
    {
        g_bms_system_status.bits.b1Status_AFE1 = 0u;
        bms_error_raise(BMS_ERROR_AFE1);
    }
}

/* 应用并验证 DVC 硬件保护参数。 */
uint8_t DVC1124_ApplyProtectionConfig(void)
{
    uint8_t ok = dvc_apply_protection_from_params();

    dvc_note_comm_result(ok);
    return ok;
}

/* 将 NTC 电阻换算为协议温度编码。 */
static uint16_t dvc_ntc_temp_report(uint32_t r_ohm)
{
    uint32_t code = r_ohm / 10u;
    if (code > 65535u) code = 65535u;
    return bms_lookup_u16(s_ntc_10k_table, DVC_TEMP_TABLE_LEN, (uint16_t)code);
}

/* 根据 ADC 读数计算 NTC 电阻。 */
static uint8_t dvc_ntc_resistance(uint16_t gp_code,
                                  uint16_t v1p8_code,
                                  uint16_t rpu_ohm,
                                  uint32_t *res_ohm)
{
    uint32_t denominator;

    if (res_ohm == NULL) return 0u;
    *res_ohm = 0u;
    if (v1p8_code == 0u) return 0u;
    if (gp_code == 0u) return 0u;             /* 短路或无效输入。 */
    if (v1p8_code <= gp_code) return 0u;      /* 开路或饱和。 */

    denominator = (uint32_t)v1p8_code - gp_code;
    *res_ohm = ((uint32_t)gp_code * rpu_ohm) / denominator;
    return 1u;
}

/* 按型号和地址模式解析 DVC 的 I2C 写地址。 */
static uint8_t DVC1124_ResolveWriteAddress(dvc1124_model_t model,
                                    dvc1124_addr_mode_t mode,
                                    uint8_t hardwire_code,
                                    uint8_t explicit_write_addr,
                                    uint8_t *write_addr)
{
    uint8_t addr;

    if (write_addr == NULL) return 0u;
    if ((model != DVC1124_MODEL_22) && (model != DVC1124_MODEL_24)) return 0u;

    switch (mode)
    {
    case DVC1124_ADDR_FIXED:
        addr = DVC1124_FIXED_WRITE_ADDR;
        break;
    case DVC1124_ADDR_HARDWIRED:
        if (model == DVC1124_MODEL_22)
        {
            if (hardwire_code > 3u) return 0u;
        }
        else if (hardwire_code > 15u)
        {
            return 0u;
        }
        addr = (uint8_t)(DVC1124_HARDWIRE_BASE_WRITE_ADDR | (uint8_t)(hardwire_code << 1));
        break;
    case DVC1124_ADDR_EXPLICIT:
        if ((explicit_write_addr & 0x01u) != 0u) return 0u;
        addr = explicit_write_addr;
        break;
    default:
        return 0u;
    }

    *write_addr = addr;
    return 1u;
}

/* 设置驱动的有效电芯串数。 */
static uint8_t DVC1124_SetCellCount(uint8_t cell_count)
{
    /* DVC1124-2 V1.2 明确支持 4..24 串。 */
    if ((cell_count < DVC1124_MIN_CELLS) || (cell_count > DVC1124_MAX_CELLS)) return 0u;
    s_cfg.cell_count = cell_count;
    s_need_config = 1u;
    return 1u;
}

/* 取得 DVC 当前驱动配置。 */
void DVC1124_GetConfig(dvc1124_config_t *config)
{
    if (config != NULL) *config = s_cfg;
}

/* 取得 DVC 最近一次测量与状态快照。 */
void DVC1124_GetSnapshot(dvc1124_snapshot_t *snapshot)
{
    if (snapshot != NULL) *snapshot = s_snapshot;
}

/* 取得 DVC 当前 I2C 写地址。 */
uint8_t DVC1124_GetWriteAddress(void)
{
    uint8_t addr = DVC1124_FIXED_WRITE_ADDR;
    (void)DVC1124_ResolveWriteAddress(s_cfg.model, s_cfg.addr_mode,
                                      s_cfg.hardwire_code,
                                      s_cfg.explicit_write_addr, &addr);
    return addr;
}

/* 读取指定范围 DVC 寄存器并返回通信结果。 */
uint8_t DVC1124_ReadRegisters(uint8_t reg, uint8_t *data, uint8_t len)
{
    uint8_t write_addr;
    uint8_t read_addr;
    uint8_t attempt;
    uint8_t i;

    if ((data == NULL) || (len == 0u)) return 0u;
    if (((uint16_t)reg + len - 1u) > DVC1124_MAX_REGISTER) return 0u;
    if (((uint16_t)len * 2u) > sizeof(s_i2c_raw)) return 0u;

    write_addr = DVC1124_GetWriteAddress();
    read_addr = (uint8_t)(write_addr | 0x01u);
    if (!s_bus_initialized) dvc_bus_init();

    for (attempt = 0u; attempt < DVC1124_I2C_RETRY_COUNT; ++attempt)
    {
        uint8_t first_crc_input[4];
        uint8_t valid = 1u;

        memset(s_i2c_raw, 0, (size_t)len * 2u);
        if (!dvc_i2c_read_raw(write_addr, reg, s_i2c_raw, (uint16_t)len * 2u))
        {
            dvc_bus_recover();
            dvc_delay_ms(1u);
            continue;
        }

        first_crc_input[0] = write_addr;
        first_crc_input[1] = reg;
        first_crc_input[2] = read_addr;
        first_crc_input[3] = s_i2c_raw[0];
        if (dvc_crc8(first_crc_input, 4u) != s_i2c_raw[1]) valid = 0u;
        if (valid && reg == DVC1124_REG_STATUS)
            s_pending_adc_events |= (uint8_t)(s_i2c_raw[0] &
                (DVC1124_STATUS_VADF_MASK | DVC1124_STATUS_CC2F_MASK));

        for (i = 1u; (i < len) && valid; ++i)
        {
            if (dvc_crc8(&s_i2c_raw[(uint16_t)i * 2u], 1u) !=
                s_i2c_raw[(uint16_t)i * 2u + 1u]) valid = 0u;
            /* 已通过该字节CRC的RC事件不能被后续字节失败/重试丢弃。 */
            if (valid && (uint16_t)reg + i == DVC1124_REG_STATUS)
                s_pending_adc_events |= (uint8_t)(s_i2c_raw[(uint16_t)i * 2u] &
                    (DVC1124_STATUS_VADF_MASK | DVC1124_STATUS_CC2F_MASK));
        }

        if (valid)
        {
            for (i = 0u; i < len; ++i) data[i] = s_i2c_raw[(uint16_t)i * 2u];
            return 1u;
        }
        dvc_delay_ms(1u);
    }
    return 0u;
}

/* 校验范围后写入 DVC 寄存器。 */
uint8_t DVC1124_WriteRegisters(uint8_t reg, const uint8_t *data, uint8_t len)
{
    uint8_t write_addr;
    uint8_t first_crc_input[3];
    uint8_t attempt;
    uint8_t i;

    if ((data == NULL) || (len == 0u)) return 0u;
    if (((uint16_t)reg + len - 1u) > DVC1124_MAX_REGISTER) return 0u;
    if (((uint16_t)len * 2u) > sizeof(s_i2c_raw)) return 0u;

    write_addr = DVC1124_GetWriteAddress();
    if (!s_bus_initialized) dvc_bus_init();

    first_crc_input[0] = write_addr;
    first_crc_input[1] = reg;
    first_crc_input[2] = data[0];
    s_i2c_raw[0] = data[0];
    s_i2c_raw[1] = dvc_crc8(first_crc_input, 3u);
    for (i = 1u; i < len; ++i)
    {
        s_i2c_raw[(uint16_t)i * 2u] = data[i];
        s_i2c_raw[(uint16_t)i * 2u + 1u] = dvc_crc8(&data[i], 1u);
    }

    for (attempt = 0u; attempt < DVC1124_I2C_RETRY_COUNT; ++attempt)
    {
        if (dvc_i2c_write_raw(write_addr, reg, s_i2c_raw, (uint16_t)len * 2u)) return 1u;
        dvc_bus_recover();
        dvc_delay_ms(1u);
    }
    return 0u;
}

/* 按充放电请求配置 DVC MOS 控制状态。 */
static uint8_t DVC1124_SetMosState(uint8_t charge_on, uint8_t discharge_on)
{
    uint8_t set = 0u;

    if (charge_on && s_output_enabled)
    {
        set |= DVC1124_FIELD_PREP(DVC1124_FET_CHGC_MASK,
                                   DVC1124_FET_CHGC_SHIFT,
                                   DVC1124_FET_DRIVE_ON);
    }
    if (discharge_on && s_output_enabled)
    {
        set |= DVC1124_FIELD_PREP(DVC1124_FET_DSGC_MASK,
                                   DVC1124_FET_DSGC_SHIFT,
                                   DVC1124_FET_DRIVE_ON);
    }

    return dvc_update_reg(DVC1124_REG_FET_CTRL,
                          (uint8_t)(DVC1124_FET_DSGC_MASK |
                                    DVC1124_FET_CHGC_MASK),
                          set);
}

/* 设置 DVC 输出授权并同步 MOS 控制。 */
void DVC1124_SetOutputEnabled(uint8_t enabled)
{
    s_output_enabled = enabled ? 1u : 0u;
    if (!s_output_enabled)
    {
        (void)DVC1124_SetMosState(0u, 0u);
    }
}

/* 根据有效串数生成电芯通道掩码。 */
/* TC32多点内联会突破D008生产Flash预留；保留单一函数实体，见AFE修复记录。 */
static uint32_t dvc_valid_cell_mask(void) __attribute__((noinline));
static uint32_t dvc_valid_cell_mask(void)
{
    return (s_cfg.cell_count >= 24u)
               ? 0x00FFFFFFu
               : ((1uL << s_cfg.cell_count) - 1uL);
}

/* 回读并刷新均衡实际状态。 */
static uint8_t dvc_refresh_balance_state(void)
{
    uint8_t data[3];
    uint32_t actual;

    if (!DVC1124_ReadRegisters(DVC1124_REG_BAL_24_17, data, 3u)) return 0u;
    actual = (((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2]) & dvc_valid_cell_mask();
    g_stCellInfoReport.u16BalanceFlag1 = (uint16_t)(actual & 0xFFFFu);
    g_stCellInfoReport.u16BalanceFlag2 = (uint16_t)((actual >> 16) & 0x00FFu);
    return 1u;
}

/* 将有效均衡通道掩码写入硬件。 */
static uint8_t dvc_write_balance_hw(uint32_t cell_mask)
{
    uint8_t data[3];
    cell_mask &= dvc_valid_cell_mask();
    data[0] = (uint8_t)(cell_mask >> 16);
    data[1] = (uint8_t)(cell_mask >> 8);
    data[2] = (uint8_t)cell_mask;
    if (!dvc_write_verified_block(DVC1124_REG_BAL_24_17, data, 3u)) return 0u;
    return dvc_refresh_balance_state();
}

/* 设置 DVC 均衡请求掩码并同步硬件。 */
uint8_t DVC1124_SetBalanceMask(uint32_t cell_mask)
{
    s_balance_requested_mask = cell_mask & dvc_valid_cell_mask();
    if (s_balance_requested_mask == 0u)
    {
        s_balance_suspended = 0u;
        s_balance_last_refresh_tick = clock_time();
        return dvc_write_balance_hw(0u);
    }

    /*
     * 非零请求只表示待授权，不立即通电；
     * 仅在充电和故障条件允许时由 BMS 服务应用或续期。
     */
    s_balance_suspended = 1u;
    return dvc_refresh_balance_state();
}

/* 推进均衡服务并刷新通道状态。 */
void DVC1124_BalanceService(uint8_t allow_refresh)
{
    if (s_balance_requested_mask == 0u)
    {
        (void)dvc_refresh_balance_state();
        return;
    }

    if (!allow_refresh || (s_openwire_result.state == DVC1124_OPENWIRE_WAITING))
    {
        if (!s_balance_suspended)
        {
            if (dvc_write_balance_hw(0u)) s_balance_suspended = 1u;
        }
        return;
    }

    if (s_balance_suspended ||
        clock_time_exceed(s_balance_last_refresh_tick, DVC_BALANCE_REFRESH_INTERVAL_US))
    {
        if (dvc_write_balance_hw(s_balance_requested_mask))
        {
            s_balance_suspended = 0u;
            s_balance_last_refresh_tick = clock_time();
        }
    }
    else
    {
        (void)dvc_refresh_balance_state();
    }
}

/* 启动 DVC 电芯断线检测。 */
static uint8_t DVC1124_StartOpenWireCheck(void)
{
    uint8_t reg;

    /* COW 约 1 秒后自清除，因此不要求持续回读为 1。 */
    if (!DVC1124_ReadRegisters(DVC1124_REG_CP_CTRL, &reg, 1u)) return 0u;
    reg |= DVC1124_COW_MASK;
    return DVC1124_WriteRegisters(DVC1124_REG_CP_CTRL, &reg, 1u);
}

/* 复位 DVC 断线检测阶段及结果。 */
void DVC1124_OpenWireReset(void)
{
    memset(&s_openwire_result, 0, sizeof(s_openwire_result));
    s_openwire_result.state = DVC1124_OPENWIRE_IDLE;
    s_openwire_start_tick = 0u;
    s_openwire_start_generation = s_snapshot_generation;
}

/* 准备并开始 DVC 非阻塞断线检测流程。 */
uint8_t DVC1124_OpenWireBegin(void)
{
    if (s_openwire_result.state == DVC1124_OPENWIRE_WAITING) return 0u;

    if (!s_balance_suspended && s_balance_requested_mask != 0u)
    {
        if (!dvc_write_balance_hw(0u)) return 0u;
        s_balance_suspended = 1u;
    }

    memset(&s_openwire_result, 0, sizeof(s_openwire_result));
    {
        uint8_t status;
        if (!DVC1124_ReadRegisters(DVC1124_REG_STATUS, &status, 1u)) return 0u;
        s_pending_adc_events &= (uint8_t)~DVC1124_STATUS_VADF_MASK;
    }
    s_openwire_result.state = DVC1124_OPENWIRE_WAITING;
    s_openwire_start_generation = s_snapshot_generation;
    s_openwire_start_tick = clock_time();
    if (!DVC1124_StartOpenWireCheck())
    {
        s_openwire_result.state = DVC1124_OPENWIRE_ERROR;
        return 0u;
    }
    return 1u;
}

/* 推进断线检测阶段并收集完成结果。 */
void DVC1124_OpenWirePoll(void)
{
    uint8_t cp;

    if (s_openwire_result.state != DVC1124_OPENWIRE_WAITING) return;
    if (!clock_time_exceed(s_openwire_start_tick, DVC_OPENWIRE_SETTLE_US)) return;

    /*
     * COW 对单体输入施加约 1 秒的 100 uA 下拉。必须在 COW 有效期间抓取诊断样本；
     * 等待自动清除后读到的是普通电压，无法区分断线与正常输入。
     */
    if (!s_snapshot.valid || !s_snapshot.voltage_fresh ||
        s_snapshot_generation == s_openwire_start_generation) return;
    if (!DVC1124_ReadRegisters(DVC1124_REG_CP_CTRL, &cp, 1u))
    {
        s_openwire_result.state = DVC1124_OPENWIRE_ERROR;
        return;
    }
    if (!(cp & DVC1124_COW_MASK))
    {
        s_openwire_result.state = DVC1124_OPENWIRE_ERROR;
        return;
    }

    s_openwire_result.valid = 1u;
    s_openwire_result.cell_count = s_snapshot.cell_count;
    memcpy(s_openwire_result.cell_mv, s_snapshot.cell_mv, sizeof(s_openwire_result.cell_mv));
    s_openwire_result.pack_mv = s_snapshot.pack_mv;

    /* 诊断样本抓取后立即停止激励。COW 是命令位，不使用普通持久配置的回读规则。 */
    cp &= (uint8_t)~DVC1124_COW_MASK;
    if (!DVC1124_WriteRegisters(DVC1124_REG_CP_CTRL, &cp, 1u))
    {
        s_openwire_result.state = DVC1124_OPENWIRE_ERROR;
        return;
    }
    s_openwire_result.state = DVC1124_OPENWIRE_READY;
}

/* 取得最近一次断线检测结果。 */
void DVC1124_OpenWireGetResult(dvc1124_openwire_result_t *result)
{
    if (result != NULL) *result = s_openwire_result;
}

/* 配置 DVC 短路保护开关及参数。 */
static uint8_t DVC1124_SetShortCircuitProtection(uint16_t threshold_mv, uint16_t delay_us)
{
    uint8_t threshold_code;
    uint8_t delay_code;
    uint32_t delay_code32;
    uint16_t actual_delay_us;
    uint8_t ok;

    if (threshold_mv == 0u)
    {
        s_applied.scd_mv = 0u;
        s_applied.scd_delay_us = 0u;
        ok = dvc_update_reg(DVC1124_REG_SCD,
                            (uint8_t)(DVC1124_SCD_ENABLE_MASK |
                                      DVC1124_SCD_THRESHOLD_MASK),
                            0u);
        ok &= dvc_write_verified(DVC1124_REG_SCD_DLY, 0u);
        return ok;
    }

    /* V1.2：阈值为 SCDT*10 mV；编码 0 不适合作为已启用的阈值。 */
    if ((threshold_mv < 10u) || (threshold_mv > 630u) || ((threshold_mv % 10u) != 0u)) return 0u;
    threshold_code = (uint8_t)(threshold_mv / 10u);

    /* V1.2：延时为 SCDD*7.81 us；先写延时，再启用 SCD。 */
    delay_code32 = ((uint32_t)delay_us * 100u) / 781u;
    if (delay_code32 > 255u) return 0u;
    delay_code = (uint8_t)delay_code32;
    actual_delay_us = (uint16_t)(((uint32_t)delay_code * 781u + 50u) / 100u);

    if (!dvc_write_verified(DVC1124_REG_SCD_DLY, delay_code)) return 0u;
    if (!dvc_update_reg(DVC1124_REG_SCD,
                        (uint8_t)(DVC1124_SCD_ENABLE_MASK |
                                  DVC1124_SCD_THRESHOLD_MASK),
                        (uint8_t)(DVC1124_SCD_ENABLE_MASK | threshold_code))) return 0u;

    s_applied.scd_mv = (uint16_t)threshold_code * 10u;
    s_applied.scd_delay_us = actual_delay_us;
    return 1u;
}

/* 复位 DVC AFE 并重建驱动状态。 */
void DVC1124_AFE_Reset(void)
{
    uint8_t cmd = (uint8_t)DVC1124_CST_RESET_REGISTERS;

    /* HS-D008 的 MCU-AFE-EN 为 PD7，高电平有效。 */
    gpio_set_func(GPIO_PD7, AS_GPIO);
    gpio_set_input_en(GPIO_PD7, 0u);
    gpio_set_output_en(GPIO_PD7, 1u);
    gpio_write(GPIO_PD7, 1u);
    dvc_delay_ms(20u);

    dvc_bus_init();
    (void)DVC1124_WriteRegisters(DVC1124_REG_STATUS, &cmd, 1u); /* 配置值：CST=1101。 */
    dvc_delay_ms(DVC1124_RESET_SETTLE_MS);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(&s_applied, 0, sizeof(s_applied));
    s_balance_requested_mask = 0u;
    s_balance_suspended = 0u;
    s_balance_last_refresh_tick = clock_time();
    s_snapshot_generation = 0u;
    s_pending_adc_events = 0u;
    s_voltage_seen = s_current_seen = s_sample_pending = 0u;
    s_voltage_since_current = 0u;
    s_adc_wait_started = pm_get_32k_tick();
    DVC1124_OpenWireReset();
    s_need_config = 1u;
}

/* 查询 DVC 初始化就绪状态。 */
static uint8_t DVC1124_AFE_IsReady(void)
{
    uint8_t attempt;
    uint8_t version = 0u;
    uint8_t frt = 0u;

    if (!s_bus_initialized) dvc_bus_init();
    for (attempt = 0u; attempt < DVC_READY_RETRY_COUNT; ++attempt)
    {
        if (DVC1124_ReadRegisters(DVC1124_REG_CHIP_VERSION, &version, 1u) &&
            DVC1124_ReadRegisters(DVC1124_REG_FRT, &frt, 1u))
        {
            s_snapshot.chip_version = version;
            s_snapshot.rpu_ohm = (uint16_t)(6800u + (uint16_t)frt * 25u);
            dvc_note_comm_result(1u);
            return 0u; /* 旧 API 约定：0 表示已就绪。 */
        }
        dvc_delay_ms(5u);
    }
    dvc_note_comm_result(0u);
    return 1u;
}

/* 按当前参数更新 DVC AFE 配置。 */
void DVC1124_UpdataAfeConfig(void)
{
    uint8_t ok;
    bms_diag_boot_word(25u, DIAG_STARTED);

    /* AFE 通道使用以 D008 实际装配配置为准。 */
    if (!DVC1124_SetCellCount((uint8_t)DVC1124_DEFAULT_CELL_COUNT))
    {
        bms_diag_boot_word(25u, DIAG_INVALID);
        dvc_note_comm_result(0u);
        return;
    }
    if (DVC1124_AFE_IsReady() != 0u) { bms_diag_boot_word(25u, DIAG_INVALID); return; }

    ok = dvc_apply_basic_config();
    ok &= dvc_apply_protection_from_params();
    bms_diag_boot_word(25u, ok ? DIAG_OK : DIAG_INVALID);
    if (ok)
    {
        s_need_config = 0u;
        dvc_note_comm_result(1u);
    }
    else
    {
        s_need_config = 1u;
        dvc_note_comm_result(0u);
    }
}

/* 按现有器件时序使 DVC 进入休眠。 */
uint8_t DVC1124_AFE_Sleep(void)
{
    uint8_t cmd = (uint8_t)DVC1124_CST_ENTER_SLEEP;
    if (!DVC1124_WriteRegisters(DVC1124_REG_STATUS, &cmd, 1u)) {
        dvc_note_comm_result(0u);
        return 0u;
    }
    return 1u;
}

/* 把电流测量转换并发布到公共报告。 */
static void dvc_publish_current_report(int32_t current_ma)
{
    uint32_t magnitude_ma = (current_ma < 0) ?
        (0u - (uint32_t)current_ma) : (uint32_t)current_ma;
    uint32_t a10;

    g_stCellInfoReport.u16Ichg = 0u;
    g_stCellInfoReport.u16IDischg = 0u;
    /*
     * 先在 mA 单位应用可信电流下限，再转为旧 0.1 A 编码。
     * snapshot.current_ma 保留未屏蔽值，供诊断及 PM/SOC 策略使用。
     */
    if (magnitude_ma <= BMS_CURRENT_UNRELIABLE_MAX_MA) return;
    a10 = magnitude_ma / 100u;
    if (a10 > 65535u) a10 = 65535u;
    if (current_ma < 0) g_stCellInfoReport.u16Ichg = (uint16_t)a10;
    else g_stCellInfoReport.u16IDischg = (uint16_t)a10;
}

/* 采集 DVC 测量和状态并更新驱动快照。 */
void DVC1124_App_AFEGet(void)
{
    uint8_t data[DVC_MEAS_BYTES];
    uint16_t v1p8_code;
    uint32_t common_mode_mv = 0u;
    uint32_t total_mv = 0u;
    uint16_t max_mv = 0u;
    uint16_t min_mv = 0xFFFFu;
    uint8_t max_pos = 0u;
    uint8_t min_pos = 0u;
    uint8_t i;
    uint32_t raw20;
    int32_t cc2;
    int32_t current_ma;
    int32_t factory_current_ma;
    uint8_t write_addr;
    uint8_t configured_ntc_ok = 1u;
    uint32_t now = pm_get_32k_tick();
    s_sample_pending = 0u;
    s_snapshot.voltage_fresh = s_snapshot.current_fresh = 0u;

    if (s_need_config)
    {
        DVC1124_UpdataAfeConfig();
        if (s_need_config)
        {
            s_snapshot.valid = 0u;
            return;
        }
    }

    if (!DVC1124_ReadRegisters(DVC1124_REG_ALARM, data, DVC_MEAS_BYTES))
    {
        s_snapshot.valid = 0u;
        g_stCellInfoReport.u16Ichg = 0u;
        g_stCellInfoReport.u16IDischg = 0u;
        dvc_note_comm_result(0u);
        return;
    }

    write_addr = DVC1124_GetWriteAddress();
    s_snapshot.voltage_fresh = (s_pending_adc_events & DVC1124_STATUS_VADF_MASK) ? 1u : 0u;
    s_snapshot.current_fresh = (s_pending_adc_events & DVC1124_STATUS_CC2F_MASK) ? 1u : 0u;
    s_pending_adc_events = 0u;
    s_snapshot.fet_status = data[DVC1124_REG_CC2_L_FLAGS];
    s_snapshot.alarm = data[DVC1124_REG_ALARM];
    s_snapshot.status = data[DVC1124_REG_STATUS];
    s_snapshot.write_addr = write_addr;
    s_snapshot.cell_count = s_cfg.cell_count;
    if (s_snapshot.current_fresh) {
        raw20 = ((uint32_t)data[DVC1124_REG_CC2_H] << 12) |
                ((uint32_t)data[DVC1124_REG_CC2_M] << 4) |
                ((uint32_t)data[DVC1124_REG_CC2_L_FLAGS] >> 4);
        cc2 = dvc_sign_extend20(raw20);
        current_ma = dvc_cc2_to_raw_current_ma(cc2);
        s_snapshot.raw_current_ma = current_ma;
        factory_current_ma = bms_config_calibrate_current(current_ma);
        current_ma = dvc_apply_boot_zero(factory_current_ma);
        s_snapshot.current_ma = current_ma;

        dvc_publish_current_report(current_ma);

        s_current_seen = 1u;
        s_current_tick = now;
        s_snapshot.sample_tick_32k = now;
    }

    if (s_snapshot.voltage_fresh) {
        s_snapshot.vtop_mv = ((uint32_t)dvc_be16(&data[DVC1124_REG_VTOP_H]) * 128u + 5u) / 10u;
        s_snapshot.pack_mv = ((uint32_t)dvc_be16(&data[DVC1124_REG_VPACK_H]) * 128u + 5u) / 10u;
        s_snapshot.load_mv = ((uint32_t)dvc_be16(&data[DVC1124_REG_VLOAD_H]) * 128u + 5u) / 10u;

        for (i = 0u; i < s_cfg.cell_count; ++i)
        {
            uint8_t reg = (uint8_t)(DVC1124_REG_CELL1_H + (uint8_t)(i * 2u));
            uint16_t raw_cell = dvc_be16(&data[reg]);
            uint16_t mv = dvc_correct_cell_mv(raw_cell, common_mode_mv);

            s_snapshot.cell_mv[i] = mv;
            g_stCellInfoReport.u16VCell[i] = mv;
            total_mv += mv;
            common_mode_mv += mv;
            if (mv > max_mv) { max_mv = mv; max_pos = i; }
            if (mv < min_mv) { min_mv = mv; min_pos = i; }
        }
        for (; i < DVC1124_MAX_CELLS; ++i) s_snapshot.cell_mv[i] = 0u;
        for (i = s_cfg.cell_count; i < 32u; ++i) g_stCellInfoReport.u16VCell[i] = 61001u;

        g_stCellInfoReport.u16VCellTotle = (uint16_t)((total_mv / 10u) > 65535u ? 65535u : (total_mv / 10u));
        g_stCellInfoReport.u16VCellMax = max_mv;
        g_stCellInfoReport.u16VCellMin = (min_mv == 0xFFFFu) ? 0u : min_mv;
        g_stCellInfoReport.u16VCellDelta = (uint16_t)(max_mv - g_stCellInfoReport.u16VCellMin);
        g_stCellInfoReport.u16VCellMaxPosition = (uint16_t)max_pos + 1u;
        g_stCellInfoReport.u16VCellMinPosition = (uint16_t)min_pos + 1u;

        v1p8_code = dvc_be16(&data[DVC1124_REG_V1P8_H]);
        for (i = 0u; i < DVC1124_MAX_GP; ++i)
        {
            uint8_t reg = (uint8_t)(DVC1124_REG_GP1_H + (uint8_t)(i * 2u));
            uint16_t gp_code = dvc_be16(&data[reg]);
            uint32_t r_ohm = 0u;
            uint8_t ntc_ok;

            s_snapshot.gp_code[i] = gp_code;
            ntc_ok = dvc_ntc_resistance(gp_code, v1p8_code, s_snapshot.rpu_ohm, &r_ohm);
            s_snapshot.ntc_res_ohm[i] = r_ohm;

            if (i < 4u)
                g_stCellInfoReport.u16Temperature[i] = ntc_ok ? dvc_ntc_temp_report(r_ohm) : 0u;

            if (((i + 1u) == s_cfg.battery_ntc_gp || (i + 1u) == s_cfg.mos_ntc_gp) && !ntc_ok)
                configured_ntc_ok = 0u;
        }

        if (!DVC1124_SW_TEMP_PROTECT_ENABLE || configured_ntc_ok)
        {
            bms_error_clear(BMS_ERROR_TEMP_BREAK);
        }
        else
        {
            bms_error_raise(BMS_ERROR_TEMP_BREAK);
        }

        {
            /* V1.2：T = VCT*0.24467 - 271.03 ℃；整数单位为 0.1 ℃。 */
            int32_t die_x10 = ((int32_t)dvc_be16(&data[DVC1124_REG_VCT_H]) * 24467) / 10000 - 2710;
            int32_t report_temp = die_x10 + 400;
            if (report_temp < 0) report_temp = 0;
            if (report_temp > 65535) report_temp = 65535;
            s_snapshot.die_temp_x10 = (int16_t)die_x10;
            g_stCellInfoReport.u16Temperature[4] = (uint16_t)report_temp;
        }

        {
            uint16_t tmax = 0u;
            uint16_t tmin = 0xFFFFu;
            for (i = 0u; i < 5u; ++i)
            {
                uint16_t t = g_stCellInfoReport.u16Temperature[i];
                if (t == 0u) continue;
                if (t > tmax) tmax = t;
                if (t < tmin) tmin = t;
            }
            g_stCellInfoReport.u16TempMax = tmax;
            g_stCellInfoReport.u16TempMin = (tmin == 0xFFFFu) ? 0u : tmin;
        }

        s_voltage_seen = 1u;
        s_voltage_since_current = 1u;
        s_voltage_tick = now;
        ++s_snapshot_generation;
    }
    s_snapshot.valid = s_voltage_seen && s_current_seen &&
        (uint32_t)(now - s_voltage_tick) <= DVC_ADC_MAX_AGE_TICKS &&
        (uint32_t)(now - s_current_tick) <= DVC_ADC_MAX_AGE_TICKS;
    s_sample_pending = (!s_voltage_since_current || !s_snapshot.current_fresh) &&
        (s_snapshot.valid || ((!s_voltage_seen || !s_current_seen) &&
         (uint32_t)(now - s_adc_wait_started) <= DVC_ADC_MAX_AGE_TICKS));
    if (!s_sample_pending && s_snapshot.valid) s_voltage_since_current = 0u;

    g_bms_system_status.bits.b1Status_MOS_CHG =
        (data[DVC1124_REG_CC2_L_FLAGS] & DVC1124_CC2_CHGF_MASK) ? 1u : 0u;
    g_bms_system_status.bits.b1Status_MOS_DSG =
        (data[DVC1124_REG_CC2_L_FLAGS] & DVC1124_CC2_DSGF_MASK) ? 1u : 0u;

    bms_diag_driver(data[DVC1124_REG_CC2_L_FLAGS], 1u);
    DVC1124_OpenWirePoll();

    /* 0x67..0x69 在 60 秒后自清除；上报 AFE 实际状态，不用缓存请求替代。 */
    if (!dvc_refresh_balance_state())
    {
        g_stCellInfoReport.u16BalanceFlag1 = 0u;
        g_stCellInfoReport.u16BalanceFlag2 = 0u;
    }
    dvc_note_comm_result(1u);
}

/* 有效缓存等待新的转换完成，不计为新样本，也不伪造通信故障。 */
uint8_t dvc1124_backend_sample_pending(void)
{
    return s_sample_pending;
}

/* 统一读改写与校验实现，集中管理边界及保留位，保持调试入口稳定。 */
__attribute__((noinline)) uint8_t DVC1124_WriteRegisterSafe(uint8_t reg, uint8_t requested)
{
    uint8_t current;
    uint8_t target;
    uint8_t verify;
    uint8_t mask = DVC1124_RegDocumentedWriteMask(reg);

    if ((reg > DVC1124_MAX_REGISTER) || (mask == 0u)) return 0u;
    if (!DVC1124_RegGenericRmwAllowed(reg)) return 0u;
    if (DVC1124_RegReadHasSideEffect(reg)) return 0u;
    if (!DVC1124_ReadRegisters(reg, &current, 1u)) return 0u;

    target = (uint8_t)((current & (uint8_t)~mask) | (requested & mask));
    if (!DVC1124_WriteRegisters(reg, &target, 1u)) return 0u;
    if (!DVC1124_ReadRegisters(reg, &verify, 1u)) return 0u;

    return ((verify & mask) == (target & mask)) ? 1u : 0u;
}

/*
 * 此处应用唯一固定产品策略，芯片编码器仍能看到常量字段；
 * 启动模块保留阶段与唤醒/重试职责。
 */
uint8_t DVC1124_ApplyProjectOperatingConfig(void)
{
    uint8_t cadc;
    uint8_t cc1;
    uint8_t cp;
    uint8_t vadc;
    uint8_t gp123;
    uint8_t gp456;
    uint8_t wdt_bits;
    uint8_t timed;

    dvc1124_i2c_wdt_code_t wdt;
    uint8_t current_wake_code;
    uint8_t body_diode_code;
    uint8_t dsg_mask;
    uint8_t chg_mask;
    uint8_t core_ot_code;
    uint8_t ok = 1u;

#if DVC1124_HW_PROTECT_ENABLE
    if (!DVC1124_EncodeI2cWatchdog(DVC1124_I2C_WATCHDOG_SECONDS, &wdt)) return 0u;
    if (!DVC1124_EncodeCurrentWake(DVC1124_CURRENT_WAKE_THRESHOLD_UV,
                                         &current_wake_code)) return 0u;
    if (!DVC1124_EncodeBodyDiode(DVC1124_BODY_DIODE_THRESHOLD_UV,
                                       &body_diode_code)) return 0u;
    if (DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE &&
        (current_wake_code == 0u)) return 0u;

    dsg_mask = DVC1124_DEFAULT_DSG_MASK_POLICY;
    chg_mask = DVC1124_DEFAULT_CHG_MASK_POLICY;
#if DVC1124_I2C_TIMEOUT_CLOSE_DSG
    dsg_mask &= (uint8_t)~DVC1124_DSGMASK_DWM_MASK;
#else
    dsg_mask |= DVC1124_DSGMASK_DWM_MASK;
#endif
#if DVC1124_I2C_TIMEOUT_CLOSE_CHG
    chg_mask &= (uint8_t)~DVC1124_CHGMASK_CWM_MASK;
#else
    chg_mask |= DVC1124_CHGMASK_CWM_MASK;
#endif
    core_ot_code = DVC1124_DEFAULT_CORE_OT_CODE;
#else
    /*
     * 关闭硬件保护是明确的台架隔离模式；
     * 按 dvc1124.c 策略关闭全部自主保护与故障安全来源。
     */
    wdt = DVC1124_I2C_WDT_OFF;
    current_wake_code = 0u;
    body_diode_code = 0u;
    dsg_mask = 0xFFu;
    chg_mask = 0xFFu;
    core_ot_code = 0u;
#endif

    if (DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH > 30u) return 0u;
    if (DVC1124_DEFAULT_CORE_OT_CODE > 127u) return 0u;

    if ((uint8_t)DVC1124_DEFAULT_CC1_WORK_TIME > 3u) return 0u;
    if ((uint8_t)DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME > 3u) return 0u;
    if ((uint8_t)DVC1124_CHARGE_PUMP_VOLTAGE_CODE > 7u) return 0u;
    if ((uint8_t)DVC1124_DEFAULT_VADC_PERIOD > 3u) return 0u;
    if ((uint8_t)DVC1124_DEFAULT_VADC_TIME > 3u) return 0u;
    if ((uint8_t)DVC1124_GP1_DEFAULT_MODE > 3u || (uint8_t)DVC1124_GP4_DEFAULT_MODE > 3u) return 0u;
    if (((uint8_t)DVC1124_GP2_DEFAULT_MODE > 2u && (uint8_t)DVC1124_GP2_DEFAULT_MODE < 6u) ||
        ((uint8_t)DVC1124_GP3_DEFAULT_MODE > 2u && (uint8_t)DVC1124_GP3_DEFAULT_MODE < 6u) ||
        ((uint8_t)DVC1124_GP5_DEFAULT_MODE > 2u && (uint8_t)DVC1124_GP5_DEFAULT_MODE < 6u) ||
        ((uint8_t)DVC1124_GP6_DEFAULT_MODE > 2u && (uint8_t)DVC1124_GP6_DEFAULT_MODE < 6u)) return 0u;
    if ((uint8_t)DVC1124_GP2_DEFAULT_MODE > 7u || (uint8_t)DVC1124_GP3_DEFAULT_MODE > 7u ||
        (uint8_t)DVC1124_GP5_DEFAULT_MODE > 7u || (uint8_t)DVC1124_GP6_DEFAULT_MODE > 7u) return 0u;
    if (!((wdt == DVC1124_I2C_WDT_OFF) ||
          (wdt == DVC1124_I2C_WDT_4S) ||
          (wdt == DVC1124_I2C_WDT_8S) ||
          (wdt == DVC1124_I2C_WDT_16S) ||
          (wdt == DVC1124_I2C_WDT_32S))) return 0u;
    if ((uint8_t)DVC1124_DEFAULT_TIMED_WAKE > 15u) return 0u;

    cadc = 0u;
    if (DVC1124_DEFAULT_HIGH_SIDE_FET_MASK) cadc |= DVC1124_CADC_HSFM_MASK;
    if (DVC1124_DEFAULT_CADC_WORK_ENABLE) cadc |= DVC1124_CADC_CAEW_MASK;
    if (DVC1124_HW_PROTECT_ENABLE && DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE) cadc |= DVC1124_CADC_CAES_MASK;

    cc1 = (uint8_t)(DVC1124_FIELD_PREP(DVC1124_CC1_WORK_TIME_MASK,
                                       DVC1124_CC1_WORK_TIME_SHIFT,
                                       DVC1124_DEFAULT_CC1_WORK_TIME) |
                    DVC1124_FIELD_PREP(DVC1124_CC1_SLEEP_WAKE_TIME_MASK,
                                       DVC1124_CC1_SLEEP_WAKE_TIME_SHIFT,
                                       DVC1124_DEFAULT_CC1_SLEEP_WAKE_TIME));

    cp = DVC1124_FIELD_PREP(DVC1124_CPVS_MASK, DVC1124_CPVS_SHIFT, DVC1124_CHARGE_PUMP_VOLTAGE_CODE);
    if (DVC1124_DEFAULT_CELL_MEASUREMENT_MASK) cp |= DVC1124_CMM_MASK;
    if (DVC1124_DEFAULT_CELL_VOLTAGE_SIGNED) cp |= DVC1124_CVS_MASK;

    vadc = DVC1124_FIELD_PREP(DVC1124_VADC_PERIOD_MASK, DVC1124_VADC_PERIOD_SHIFT, DVC1124_DEFAULT_VADC_PERIOD);
    vadc |= DVC1124_FIELD_PREP(DVC1124_VADC_TIME_MASK, DVC1124_VADC_TIME_SHIFT, DVC1124_DEFAULT_VADC_TIME);
    if (DVC1124_DEFAULT_VADC_ENABLE) vadc |= DVC1124_VADC_ENABLE_MASK;
    if (DVC1124_DEFAULT_VADC_SYNC_WITH_CC2) vadc |= DVC1124_VADC_SYNC_MASK;

    gp123 = DVC1124_GP123_ENCODE(DVC1124_GP1_DEFAULT_MODE, DVC1124_GP2_DEFAULT_MODE, DVC1124_GP3_DEFAULT_MODE);
    gp456 = DVC1124_GP456_ENCODE(DVC1124_GP4_DEFAULT_MODE, DVC1124_GP5_DEFAULT_MODE, DVC1124_GP6_DEFAULT_MODE);

    wdt_bits = (uint8_t)wdt;
    if (DVC1124_DEFAULT_V3P3_SLEEP_ENABLE) wdt_bits |= DVC1124_V3P3_SLEEP_ENABLE_MASK;
    if (DVC1124_DEFAULT_V3P3_WORK_ENABLE) wdt_bits |= DVC1124_V3P3_WORK_ENABLE_MASK;
    if (DVC1124_DEFAULT_V3P3_TIMEOUT_RESTART) wdt_bits |= DVC1124_V3P3_TIMEOUT_RESTART_MASK;

    timed = (uint8_t)DVC1124_DEFAULT_TIMED_WAKE;

    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_CADC_CTRL, cadc);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_CC1_TIMING, cc1);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_CP_CTRL, cp);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_VADC_CTRL, vadc);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_GP123_MODE, gp123);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_GP456_MODE, gp456);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_I2C_WDT, wdt_bits);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_TIMED_WAKE, timed);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_INT_MASK, DVC1124_DEFAULT_INTERRUPT_MASK);
    ok &= DVC1124_WriteRegisterFieldSafe(DVC1124_REG_DSG_PULLDOWN,
                                          DVC1124_DPC_MASK,
                                          DVC1124_DPC_SHIFT,
                                          DVC1124_DEFAULT_DSG_PULLDOWN_STRENGTH);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_CURRENT_WAKE,
                                     current_wake_code);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_BODY_DIODE,
                                     body_diode_code);
    ok &= DVC1124_SetCoreOtThresholdCode(core_ot_code);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_DSG_MASK, dsg_mask);
    ok &= DVC1124_WriteRegisterSafe(DVC1124_REG_CHG_MASK, chg_mask);

    return ok ? 1u : 0u;
}
