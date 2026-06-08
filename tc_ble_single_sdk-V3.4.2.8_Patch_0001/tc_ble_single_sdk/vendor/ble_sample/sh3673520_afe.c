#include "sh3673520_afe.h"

#include "bms_cold_kv_store.h"
#include "drivers.h"
#include "register.h"
#include "sh367309_datadeal.h"
#include "spi.h"
#include <string.h>

#define SH3673520_SPI_CS_PIN              GPIO_PD6
#define SH3673520_SPI_GROUP               SPI_GPIO_GROUP_A2A3A4D6
#define SH3673520_RESET_PIN               AFE_CTL_PIN
#define SH3673520_SPI_ACK                 0xA5u
#define SH3673520_SPI_NACK                0xFFu

#define SH3673520_CMD_WRITE               0x01u
#define SH3673520_CMD_READ                0x02u
#define SH3673520_CMD_RESET               0x0Bu

#define SH3673520_SAMPLE_START            SH3673520_REG_FLAG1
#define SH3673520_SAMPLE_END              0x96u
#define SH3673520_SAMPLE_LEN              (SH3673520_SAMPLE_END - SH3673520_SAMPLE_START + 1u)

#define SH3673520_DEFAULT_RSENSE_UOHM     ((u16)(((u32)CS_Res * 1000u) / ((CS_Res_Num == 0u) ? 1u : (u32)CS_Res_Num)))

extern UINT16 GetEndValue(const UINT16 *ptbl, UINT16 tblsize, UINT16 dat);
extern const UINT16 iSheldTemp_10K_AFE[];

static u16 g_sh3673520_params[SH3673520_PARAM_WORD_COUNT];
static u8 g_sh3673520_params_loaded;
static u16 g_sh3673520_apply_status = SH3673520_STATUS_OK;
static u32 g_sh3673520_last_pack_mv;
static int g_sh3673520_last_current_ma;

static void sh3673520_set_status(u16 code, u8 context)
{
    g_sh3673520_apply_status = (u16)(code | ((u16)context << SH3673520_STATUS_CONTEXT_SHIFT));
}

static u8 sh3673520_crc8(const u8 *data, u16 len)
{
    u8 crc = 0u;
    u16 i;
    u8 bit;

    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x80u) ? (u8)((crc << 1) ^ 0x07u) : (u8)(crc << 1);
        }
    }

    return crc;
}

static u8 sh3673520_spi_xfer(u8 out)
{
    reg_spi_ctrl &= ~FLD_SPI_DATA_OUT_DIS;
    reg_spi_ctrl &= ~FLD_SPI_RD;
    reg_spi_data = out;
    while (reg_spi_ctrl & FLD_SPI_BUSY) {
    }
    return reg_spi_data;
}

static void sh3673520_select(void)
{
    gpio_write(SH3673520_SPI_CS_PIN, 0);
}

static void sh3673520_deselect(void)
{
    gpio_write(SH3673520_SPI_CS_PIN, 1);
}

static u8 sh3673520_write_reg(u8 reg, u8 value)
{
    u8 crc_data[3];
    u8 ack;

    crc_data[0] = SH3673520_CMD_WRITE;
    crc_data[1] = reg;
    crc_data[2] = value;

    sh3673520_select();
    (void)sh3673520_spi_xfer(SH3673520_CMD_WRITE);
    (void)sh3673520_spi_xfer(reg);
    (void)sh3673520_spi_xfer(value);
    (void)sh3673520_spi_xfer(sh3673520_crc8(crc_data, sizeof(crc_data)));
    ack = sh3673520_spi_xfer(0x00u);
    sh3673520_deselect();

    return (ack == SH3673520_SPI_ACK);
}

static u8 sh3673520_read_regs(u8 reg, u8 len, u8 *buf)
{
    u8 crc_calc_buf[4u + SH3673520_SAMPLE_LEN];
    u8 crc_rx;
    u8 i;

    if ((buf == 0) || (len == 0u) || (len > SH3673520_SAMPLE_LEN)) {
        return 0u;
    }

    sh3673520_select();
    crc_calc_buf[0] = sh3673520_spi_xfer(SH3673520_CMD_READ);
    (void)sh3673520_spi_xfer(reg);
    (void)sh3673520_spi_xfer(len);
    (void)sh3673520_spi_xfer(0x00u);
    for (i = 0u; i < len; ++i) {
        buf[i] = sh3673520_spi_xfer(0x00u);
    }
    crc_rx = sh3673520_spi_xfer(0x00u);
    sh3673520_deselect();

    crc_calc_buf[0] = SH3673520_SPI_NACK;
    crc_calc_buf[1] = SH3673520_CMD_READ;
    crc_calc_buf[2] = reg;
    crc_calc_buf[3] = len;
    memcpy(&crc_calc_buf[4], buf, len);

    return (sh3673520_crc8(crc_calc_buf, (u16)(4u + len)) == crc_rx);
}

static u16 sh3673520_u16be(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

static void sh3673520_set_default_cfg_words(void)
{
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_SCONF2 - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_SCONF2;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_SCONF3 - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_SCONF3;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_SCONF4 - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_SCONF4;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_SCONF5 - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_SCONF5;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_SCONF6 - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_SCONF6_BOARD;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_SCONF7 - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_SCONF7;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_OWV_ALARMH - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_OWV_ALARMH;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_ALARML - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_ALARML;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_OVT_OVH - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_OVT_OVH;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_OVL - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_OVL;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_UVT_UVH - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_UVT_UVH;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_UVL - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_UVL;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_OCD1 - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_OCD1;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_OCD2 - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_OCD2;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_SCV_SCT - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_SCV_SCT;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_OCC - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_OCC;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_OTC - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_OTC;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_OTD - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_OTD;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_UTC - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_UTC;
    g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_UTD - SH3673520_CFG_REG_START)] = SH3673520_DEFAULT_UTD;
}

static u8 sh3673520_cfg_looks_legacy_blank(void)
{
    u16 nonzero = 0u;
    u16 i;
    u16 legacy_sconf4 = (u16)(SeriesNum & 0x1Fu);
    u16 sconf4 = g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_SCONF4 - SH3673520_CFG_REG_START)];
    u16 sconf6 = g_sh3673520_params[SH3673520_PARAM_CFG_BASE + (SH3673520_REG_SCONF6 - SH3673520_CFG_REG_START)];

    for (i = 0u; i < SH3673520_CFG_REG_COUNT; ++i) {
        if ((g_sh3673520_params[SH3673520_PARAM_CFG_BASE + i] & 0x00FFu) != 0u) {
            ++nonzero;
        }
    }

    return ((nonzero == 0u) ||
            ((nonzero <= 2u) &&
             (sconf4 == legacy_sconf4) &&
             (sconf6 == SH3673520_DEFAULT_SCONF6_BOARD)));
}

static void sh3673520_set_default_params(void)
{
    memset(g_sh3673520_params, 0, sizeof(g_sh3673520_params));
    g_sh3673520_params[SH3673520_PARAM_MODEL] = SH3673520_MODEL_WORD;
    g_sh3673520_params[SH3673520_PARAM_SERIES_COUNT] = SeriesNum;
    g_sh3673520_params[SH3673520_PARAM_RSENSE_UOHM] = SH3673520_DEFAULT_RSENSE_UOHM;
    sh3673520_set_default_cfg_words();
}

static u16 sh3673520_cfg_word_for_reg(u8 reg)
{
    u16 index = (u16)(SH3673520_PARAM_CFG_BASE + (u16)(reg - SH3673520_CFG_REG_START));
    if (index >= SH3673520_PARAM_WORD_COUNT) {
        return 0u;
    }
    return g_sh3673520_params[index] & 0x00FFu;
}

static u16 sh3673520_current_raw_to_ma(u16 raw)
{
    u16 rsense = g_sh3673520_params[SH3673520_PARAM_RSENSE_UOHM];
    u32 magnitude = (u32)(raw & 0x7FFFu);

    if (rsense == 0u) {
        rsense = SH3673520_DEFAULT_RSENSE_UOHM;
    }

    return (u16)(((uint64_t)magnitude * 100000000u + ((uint64_t)29127u * rsense / 2u)) / ((uint64_t)29127u * rsense));
}

static u16 sh3673520_temp_raw_to_offset_01c(u16 raw)
{
    u32 rt_kohm_x100;

    if (raw >= 32768u) {
        raw = 32767u;
    }
    if (raw == 0u) {
        return 0u;
    }

    rt_kohm_x100 = ((u32)raw * 1000u) / (32768u - (u32)raw);
    return GetEndValue(iSheldTemp_10K_AFE, (UINT16)56u, (UINT16)rt_kohm_x100);
}

void sh3673520_bus_init(void)
{
    reset_spi_module();
    spi_master_gpio_set(SH3673520_SPI_GROUP);
    spi_master_init(SPI_CLK_500K, SPI_MODE3);
    spi_masterCSpin_select(SH3673520_SPI_CS_PIN);

    gpio_set_func(SH3673520_RESET_PIN, AS_GPIO);
    gpio_set_input_en(SH3673520_RESET_PIN, 0);
    gpio_set_output_en(SH3673520_RESET_PIN, 1);
    gpio_write(SH3673520_RESET_PIN, 1);
}

void sh3673520_param_load(void)
{
    u32 magic = 0u;
    u32 value = 0u;
    u16 i;

    sh3673520_set_default_params();

    if (bms_cold_kv_store_get_afe_param_word(SH3673520_PARAM_MODEL, &magic) &&
        ((u16)magic == SH3673520_MODEL_WORD)) {
        for (i = 0u; i < SH3673520_PARAM_WORD_COUNT; ++i) {
            if (bms_cold_kv_store_get_afe_param_word(i, &value)) {
                g_sh3673520_params[i] = (u16)value;
            }
        }
        if (sh3673520_cfg_looks_legacy_blank()) {
            sh3673520_set_default_cfg_words();
            (void)sh3673520_param_commit();
        }
    } else {
        (void)sh3673520_param_commit();
    }

    g_sh3673520_params[SH3673520_PARAM_MODEL] = SH3673520_MODEL_WORD;
    g_sh3673520_params[SH3673520_PARAM_SERIES_COUNT] = SeriesNum;
    g_sh3673520_params_loaded = 1u;
}

u8 sh3673520_reset(void)
{
    u8 crc_data[3] = { SH3673520_CMD_RESET, 0xBBu, 0xCCu };
    u8 ack;

    gpio_write(SH3673520_RESET_PIN, 0);
    WaitMs(2);
    gpio_write(SH3673520_RESET_PIN, 1);
    WaitMs(5);

    sh3673520_select();
    (void)sh3673520_spi_xfer(SH3673520_CMD_RESET);
    (void)sh3673520_spi_xfer(0xBBu);
    (void)sh3673520_spi_xfer(0xCCu);
    (void)sh3673520_spi_xfer(sh3673520_crc8(crc_data, sizeof(crc_data)));
    ack = sh3673520_spi_xfer(0x00u);
    sh3673520_deselect();

    WaitMs(10);
    return (ack == SH3673520_SPI_ACK);
}

u8 sh3673520_is_ready(void)
{
    u8 status[2] = {0};
    return sh3673520_read_regs(SH3673520_REG_BSTATUS1, sizeof(status), status);
}

u8 sh3673520_apply_params(void)
{
    u8 reg;

    if (!g_sh3673520_params_loaded) {
        sh3673520_param_load();
    }

    for (reg = SH3673520_CFG_REG_START; reg <= SH3673520_CFG_REG_END; ++reg) {
        if (!sh3673520_write_reg(reg, (u8)sh3673520_cfg_word_for_reg(reg))) {
            sh3673520_set_status(SH3673520_STATUS_SPI_FAIL, reg);
            return 0u;
        }
    }

    sh3673520_set_status(SH3673520_STATUS_OK, 0u);
    return 1u;
}

void sh3673520_sleep(void)
{
    (void)sh3673520_write_reg(SH3673520_REG_SCONF1, 0xAAu);
}

u8 sh3673520_read_sample(sh3673520_sample_t *sample)
{
    u8 raw[SH3673520_SAMPLE_LEN];
    u8 i;
    u16 cur;

    if (sample == 0) {
        return 0u;
    }

    memset(sample, 0, sizeof(*sample));
    if (!sh3673520_read_regs(SH3673520_SAMPLE_START, SH3673520_SAMPLE_LEN, raw)) {
        sh3673520_set_status(SH3673520_STATUS_READ_FAIL, SH3673520_SAMPLE_START);
        return 0u;
    }

    sample->flag1 = raw[SH3673520_REG_FLAG1 - SH3673520_SAMPLE_START];
    sample->flag2 = raw[SH3673520_REG_FLAG2 - SH3673520_SAMPLE_START];
    sample->flag3 = raw[SH3673520_REG_FLAG3 - SH3673520_SAMPLE_START];
    sample->status1 = raw[SH3673520_REG_BSTATUS1 - SH3673520_SAMPLE_START];
    sample->status2 = raw[SH3673520_REG_BSTATUS2 - SH3673520_SAMPLE_START];
    g_sh3673520_params[SH3673520_PARAM_LAST_STATUS] = (u16)(((u16)sample->status1 << 8) | sample->status2);
    g_sh3673520_params[SH3673520_PARAM_FLAG1] = sample->flag1;
    g_sh3673520_params[SH3673520_PARAM_FLAG2] = sample->flag2;
    g_sh3673520_params[SH3673520_PARAM_FLAG3] = sample->flag3;

    for (i = 0u; i < 4u; ++i) {
        u16 temp_raw = sh3673520_u16be(&raw[(SH3673520_REG_TEMP1 - SH3673520_SAMPLE_START) + (i * 2u)]);
        sample->temp_01c_offset[i] = sh3673520_temp_raw_to_offset_01c(temp_raw);
    }

    cur = sh3673520_u16be(&raw[SH3673520_REG_CUR - SH3673520_SAMPLE_START]);
    sample->current_raw = cur;
    if ((cur & 0x8000u) != 0u) {
        sample->discharge_ma = sh3673520_current_raw_to_ma(cur);
    } else {
        sample->charge_ma = sh3673520_current_raw_to_ma(cur);
    }

    for (i = 0u; i < SH3673520_MAX_CELL_COUNT; ++i) {
        u16 raw_cell = sh3673520_u16be(&raw[(SH3673520_REG_CELL1 - SH3673520_SAMPLE_START) + (i * 2u)]);
        sample->cell_mv[i] = (u16)(((u32)raw_cell * 5u) >> 5);
        sample->pack_mv += sample->cell_mv[i];
    }

    sample->cplus_mv = (u16)((((u32)sh3673520_u16be(&raw[SH3673520_REG_CPLUS - SH3673520_SAMPLE_START]) * 5u) >> 5) * 25u);
    g_sh3673520_last_pack_mv = sample->pack_mv;
    g_sh3673520_last_current_ma = (int)sample->charge_ma - (int)sample->discharge_ma;
    return 1u;
}

void sh3673520_publish_to_cell_info(const sh3673520_sample_t *sample, struct stCell_Info *report)
{
    u8 i;
    u16 max_v = 0u;
    u16 min_v = 0xFFFFu;
    u16 max_pos = 0u;
    u16 min_pos = 0u;
    u16 max_t = 0u;
    u16 min_t = 0xFFFFu;

    if ((sample == 0) || (report == 0)) {
        return;
    }

    for (i = 0u; i < SH3673520_MAX_CELL_COUNT; ++i) {
        u16 mv = sample->cell_mv[i];
        report->u16VCell[i] = mv;
        if (i < SeriesNum) {
            if (mv > max_v) {
                max_v = mv;
                max_pos = (u16)(i + 1u);
            }
            if ((mv != 0u) && (mv < min_v)) {
                min_v = mv;
                min_pos = (u16)(i + 1u);
            }
        }
    }
    for (i = SH3673520_MAX_CELL_COUNT; i < 32u; ++i) {
        report->u16VCell[i] = 0u;
    }

    for (i = 0u; i < 4u; ++i) {
        u16 temp = sample->temp_01c_offset[i];
        report->u16Temperature[i] = temp;
        if (temp > max_t) {
            max_t = temp;
        }
        if ((temp != 0u) && (temp < min_t)) {
            min_t = temp;
        }
    }

    report->u16VCellTotle = (sample->pack_mv > 0xFFFFu) ? 0xFFFFu : (u16)sample->pack_mv;
    report->u32VCellTotalMv = sample->pack_mv;
    report->u16VCellMax = max_v;
    report->u16VCellMin = (min_v == 0xFFFFu) ? 0u : min_v;
    report->u16VCellMaxPosition = max_pos;
    report->u16VCellMinPosition = min_pos;
    report->u16VCellDelta = (max_v >= report->u16VCellMin) ? (u16)(max_v - report->u16VCellMin) : 0u;
    report->u16Ichg = sample->charge_ma;
    report->u16IDischg = sample->discharge_ma;
    report->i32CurrentMa = (int)sample->charge_ma - (int)sample->discharge_ma;
    report->u16TempMax = max_t;
    report->u16TempMin = (min_t == 0xFFFFu) ? 0u : min_t;
}

u16 sh3673520_param_read_word(u16 index)
{
    if (!g_sh3673520_params_loaded) {
        sh3673520_param_load();
    }
    if (index >= SH3673520_PARAM_WORD_COUNT) {
        return 0u;
    }
    return g_sh3673520_params[index];
}

int sh3673520_param_write_word(u16 index, u16 value)
{
    if (!g_sh3673520_params_loaded) {
        sh3673520_param_load();
    }
    if (!sh3673520_param_is_writable(index)) {
        sh3673520_set_status(SH3673520_STATUS_INVALID_WRITE, (u8)index);
        return 0;
    }

    if (index >= SH3673520_PARAM_CFG_BASE) {
        value &= 0x00FFu;
    }
    g_sh3673520_params[index] = value;
    return 1;
}

int sh3673520_param_commit(void)
{
    u16 i;

    g_sh3673520_params[SH3673520_PARAM_MODEL] = SH3673520_MODEL_WORD;
    g_sh3673520_params[SH3673520_PARAM_SERIES_COUNT] = SeriesNum;

    for (i = 0u; i < SH3673520_PARAM_WORD_COUNT; ++i) {
        if (!bms_cold_kv_store_set_afe_param_word(i, g_sh3673520_params[i])) {
            sh3673520_set_status(SH3673520_STATUS_FLASH_FAIL, (u8)i);
            return 0;
        }
    }

    return 1;
}

int sh3673520_param_is_writable(u16 index)
{
    if (index >= SH3673520_PARAM_WORD_COUNT) {
        return 0;
    }
    if ((index == SH3673520_PARAM_MODEL) ||
        (index == SH3673520_PARAM_SERIES_COUNT) ||
        (index == SH3673520_PARAM_LAST_STATUS) ||
        (index == SH3673520_PARAM_FLAG1) ||
        (index == SH3673520_PARAM_FLAG2) ||
        (index == SH3673520_PARAM_FLAG3)) {
        return 0;
    }
    return 1;
}

u16 sh3673520_get_apply_status(void)
{
    return g_sh3673520_apply_status;
}

u32 sh3673520_get_last_pack_mv(void)
{
    return g_sh3673520_last_pack_mv;
}

int sh3673520_get_last_current_ma(void)
{
    return g_sh3673520_last_current_ma;
}
