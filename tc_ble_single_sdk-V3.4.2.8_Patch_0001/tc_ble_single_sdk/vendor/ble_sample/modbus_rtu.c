#include "modbus_rtu.h"
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "bms_state.h"
#include "param.h"
#include "SocEnhance.h"
#include "bms_event_log.h"
#include "sh367309_datadeal.h"
#include "app.h"
#include "conf.h"
#include "runtime.h"
#include "dvc1124_config_service.h"

#include "stack/ble/ble.h"
#include "btname_modbus.h"

#define MB_ADDR 0x01

#define MB_EX_ILLEGAL_FUNCTION 0x01u
#define MB_EX_ILLEGAL_ADDRESS  0x02u
#define MB_EX_ILLEGAL_VALUE    0x03u
#define MB_EX_DEVICE_FAILURE   0x04u

#define BMS_REALTIME_REG_BASE 0xD120u
#define BMS_REALTIME_REG_COUNT 11u
#define BMS_REALTIME_REG_MAGIC 0x4253u
#define BMS_REALTIME_REG_VERSION 0x0001u

#define BMS_REALTIME_REG_MAGIC_ADDR        (BMS_REALTIME_REG_BASE + 0u)
#define BMS_REALTIME_REG_VERSION_ADDR      (BMS_REALTIME_REG_BASE + 1u)
#define BMS_REALTIME_REG_VOLTAGE_ADDR      (BMS_REALTIME_REG_BASE + 2u)
#define BMS_REALTIME_REG_CURRENT_ADDR      (BMS_REALTIME_REG_BASE + 3u)
#define BMS_REALTIME_REG_SOC_ADDR          (BMS_REALTIME_REG_BASE + 4u)
#define BMS_REALTIME_REG_TEMP_MAX_ADDR     (BMS_REALTIME_REG_BASE + 5u)
#define BMS_REALTIME_REG_TEMP_MIN_ADDR     (BMS_REALTIME_REG_BASE + 6u)
#define BMS_REALTIME_REG_TEMP_MOS_ADDR     (BMS_REALTIME_REG_BASE + 7u)
#define BMS_REALTIME_REG_VCELL_MAX_ADDR    (BMS_REALTIME_REG_BASE + 8u)
#define BMS_REALTIME_REG_VCELL_MIN_ADDR    (BMS_REALTIME_REG_BASE + 9u)
#define BMS_REALTIME_REG_VCELL_DELTA_ADDR  (BMS_REALTIME_REG_BASE + 10u)

static u16 read_ascii_string_reg(const u8 *str, u16 max_len, u16 reg_offset);
static u16 read_production_info_reg(u16 reg);
static int read_event_log_frame(u8 addr, u8 func, u16 reg, u16 qty, u8 *rsp, u32 *rsp_len);
static u16 read_realtime_status_reg(u16 reg);
static u16 encode_signed_current_reg(void);
static u16 read_reg(u16 reg);
static u8 write_reg(u16 reg, u16 val);
void WriteProID_Default(void);

extern struct stCell_Info g_stCellInfoReport;
PRODUCTION_ID_INFO ProductionInfor;

static int dvc_comm_is_semantic(u16 reg)
{
    return (reg >= DVC1124_COMM_REG_BASE &&
            reg < (u16)(DVC1124_COMM_REG_BASE + DVC1124_COMM_REG_COUNT));
}

static int dvc_comm_is_raw(u16 reg)
{
    return (reg >= DVC1124_RAW_REG_BASE &&
            reg < (u16)(DVC1124_RAW_REG_BASE + DVC1124_RAW_REG_COUNT));
}

static u16 dvc_comm_read(u16 reg)
{
    u32 value;
    u8 raw;
    dvc1124_config_result_t result;

    if (dvc_comm_is_semantic(reg))
    {
        result = DVC1124_ConfigServiceRead(
            (dvc1124_config_field_t)(reg - DVC1124_COMM_REG_BASE),
            &value);
        if (result != DVC1124_CFG_OK || value > 0xFFFFu) return 0xFFFFu;
        return (u16)value;
    }

    if (dvc_comm_is_raw(reg))
    {
        result = DVC1124_ConfigServiceReadRaw(
            (u8)(reg - DVC1124_RAW_REG_BASE),
            &raw);
        return (result == DVC1124_CFG_OK) ? raw : 0xFFFFu;
    }

    return 0xFFFFu;
}

static u8 dvc_result_to_modbus_exception(dvc1124_config_result_t result)
{
    switch (result)
    {
    case DVC1124_CFG_OK:
        return 0u;
    case DVC1124_CFG_ERR_ADDRESS:
    case DVC1124_CFG_ERR_READ_ONLY:
    case DVC1124_CFG_ERR_FORBIDDEN:
        return MB_EX_ILLEGAL_ADDRESS;
    case DVC1124_CFG_ERR_VALUE:
        return MB_EX_ILLEGAL_VALUE;
    case DVC1124_CFG_ERR_AFE_IO:
    case DVC1124_CFG_ERR_STORE:
    default:
        return MB_EX_DEVICE_FAILURE;
    }
}

static u8 dvc_comm_write(u16 reg, u16 val)
{
    dvc1124_config_result_t result;

    if (dvc_comm_is_semantic(reg))
    {
        result = DVC1124_ConfigServiceWrite(
            (dvc1124_config_field_t)(reg - DVC1124_COMM_REG_BASE),
            val);
        return dvc_result_to_modbus_exception(result);
    }

    if (dvc_comm_is_raw(reg))
    {
        if (val > 0xFFu) return MB_EX_ILLEGAL_VALUE;
        result = DVC1124_ConfigServiceWriteRaw(
            (u8)(reg - DVC1124_RAW_REG_BASE),
            (u8)val);
        return dvc_result_to_modbus_exception(result);
    }

    return MB_EX_ILLEGAL_ADDRESS;
}

static int dvc_comm_range_contains(u16 reg, u16 qty)
{
    u32 end;

    if (qty == 0u) return 0;
    end = (u32)reg + qty;

    if (dvc_comm_is_semantic(reg))
        return end <= (u32)DVC1124_COMM_REG_BASE + DVC1124_COMM_REG_COUNT;
    if (dvc_comm_is_raw(reg))
        return end <= (u32)DVC1124_RAW_REG_BASE + DVC1124_RAW_REG_COUNT;
    return 0;
}

static int modbus_exception(u8 addr,
                            u8 func,
                            u8 exception,
                            u8 *rsp,
                            u32 *rsp_len)
{
    u16 crc;

    if (addr == 0x00u) return 0;

    rsp[0] = addr;
    rsp[1] = (u8)(func | 0x80u);
    rsp[2] = exception;
    crc = mb_crc16(rsp, 3u);
    rsp[3] = (u8)(crc & 0xFFu);
    rsp[4] = (u8)(crc >> 8);
    *rsp_len = 5u;
    return 1;
}

static u16 read_reg(u16 reg)
{
    u16 val;

    if (dvc_comm_is_semantic(reg) || dvc_comm_is_raw(reg))
        return dvc_comm_read(reg);

    if (reg < 3u)
    {
        switch (reg)
        {
        case 0:
            return (u16)((g_stCellInfoReport.mac_public[0] << 8) |
                         g_stCellInfoReport.mac_public[1]);
        case 1:
            return (u16)((g_stCellInfoReport.mac_public[2] << 8) |
                         g_stCellInfoReport.mac_public[3]);
        case 2:
            return (u16)((g_stCellInfoReport.mac_public[4] << 8) |
                         g_stCellInfoReport.mac_public[5]);
        default:
            return 0u;
        }
    }

    if (reg >= BTNAME_REG_BASE && reg < BTNAME_REG_BASE + BTNAME_REG_COUNT)
    {
        u16 idx = reg - BTNAME_REG_BASE;
        u16 str_idx = idx * 2u;
        const char *name = btname_get();
        u8 high_byte = 0x00u;
        u8 low_byte = 0x00u;

        if (str_idx < BTNAME_TOTAL_MAX_LEN && name[str_idx] != '\0')
            high_byte = (u8)name[str_idx];
        if ((str_idx + 1u) < BTNAME_TOTAL_MAX_LEN && name[str_idx + 1u] != '\0')
            low_byte = (u8)name[str_idx + 1u];
        return (u16)(((u16)high_byte << 8) | low_byte);
    }

    if (reg >= 0xC002u && reg <= (0xC002u + 48u))
        return read_production_info_reg(reg);

    if (reg >= 0xD000u && reg <= 0xD03Eu)
        return *(&g_stCellInfoReport.u16VCell[0] + (reg - 0xD000u));

    if (reg >= 0x2100u && reg <= 0x2140u)
        return *(&g_tParam.protect.u16VcellOvp_First + (reg - 0x2100u));

    if (reg >= 0xD100u && reg <= 0xD114u)
    {
        UINT16 u16SciTemp;
        UINT16 j;
        INT8 k;
        UINT8 a[4];

        for (j = 0; j < 4; j++)
        {
            k = FaultPoint_First2 - 1 - j;
            if (k < 0) k = Record_len + k;
            a[j] = (UINT8)k;
        }
        for (j = 0; j < 4; j++)
        {
            k = FaultPoint_Second2 - 1 - j;
            if (k < 0) k = Record_len + k;
            a[j] = (UINT8)k;
        }
        for (j = 0; j < 4; j++)
        {
            k = FaultPoint_Third2 - 1 - j;
            if (k < 0) k = Record_len + k;
            a[j] = (UINT8)k;
        }

        switch (reg)
        {
        case 0xD100u:
        case 0xD101u:
        case 0xD102u:
            return 0u;
        case 0xD103u:
            u16SciTemp = (u16)((Fault_record_First2[a[0]] << 8) | Fault_record_First2[a[1]]);
            return u16SciTemp;
        case 0xD104u:
            u16SciTemp = (u16)((Fault_record_First2[a[2]] << 8) | Fault_record_First2[a[3]]);
            return u16SciTemp;
        case 0xD105u:
            u16SciTemp = (u16)((Fault_record_Second2[a[0]] << 8) | Fault_record_Second2[a[1]]);
            return u16SciTemp;
        case 0xD106u:
            u16SciTemp = (u16)((Fault_record_Second2[a[2]] << 8) | Fault_record_Second2[a[3]]);
            return u16SciTemp;
        case 0xD107u:
            u16SciTemp = (u16)((Fault_record_Third2[a[0]] << 8) | Fault_record_Third2[a[1]]);
            return u16SciTemp;
        case 0xD108u:
            u16SciTemp = (u16)((Fault_record_Third2[a[2]] << 8) | Fault_record_Third2[a[3]]);
            return u16SciTemp;
        default:
            break;
        }

        if (reg >= 0xD109u && reg <= 0xD114u)
        {
            return (u16)(((*(&System_ErrFlag.u8ErrFlag_Com_AFE1 +
                              2u * (reg - 0xD109u))) << 8) |
                         (*(&System_ErrFlag.u8ErrFlag_Com_AFE1 +
                            2u * (reg - 0xD109u) + 1u))));
        }
    }

    if (reg >= 0xD115u && reg <= 0xD118u)
    {
        if (reg == 0xD115u) return (u16)(SystemStatus.all & 0x0000FFFFu);
        if (reg == 0xD116u) return (u16)(SystemStatus.all >> 16);
    }

    if (reg >= BMS_REALTIME_REG_BASE &&
        reg < (BMS_REALTIME_REG_BASE + BMS_REALTIME_REG_COUNT))
        return read_realtime_status_reg(reg);

    return 0u;
}

extern bool deepsleep_en;
extern uint8_t get_soc_real(void);

static int reg_requires_param_save(u16 reg)
{
    /* DVC 0x2800 semantic writes persist inside dvc1124_config_service. */
    return (reg >= 0x2100u && reg <= 0x2140u);
}

static u8 write_reg(u16 reg, u16 val)
{
    if (dvc_comm_is_semantic(reg) || dvc_comm_is_raw(reg))
        return dvc_comm_write(reg, val);

    if (reg >= 0x2100u && reg <= 0x2140u)
    {
        *(&g_tParam.protect.u16VcellOvp_First + (reg - 0x2100u)) = val;
        return 0u;
    }

    if (reg == 0x1005u)
    {
        set_soc_param(val, 1, 1);
        return 0u;
    }

    if (reg == 0x1102u)
    {
        if (val == 0x03u)
        {
            if (!Runtime_ReenterFactoryMode())
            {
                System_ERROR_UserCallback(ERROR_EEPROM_STORE);
                return MB_EX_DEVICE_FAILURE;
            }
        }
#ifdef __TEST_SOC__
        if (val == 0x01u)
        {
            sys_time.CHG = CapacityFactory * 5;
            sys_time.DSG = 0;
        }
#endif
        if (val == 0x0Au) deepsleep_en = true;
        return 0u;
    }

    if (reg == 0x1103u)
    {
#ifdef __TEST_SOC__
        if (val == 0x01u)
        {
            sys_time.CHG = 0;
            sys_time.DSG = CapacityFactory * 5;
        }
#endif
        return 0u;
    }

    if (reg == 0x2319u)
    {
        SOC_Calculate_Element.u32Cycle_times = val;
        set_soc_param(get_soc_real(), 1, 1);
        return 0u;
    }

    if (reg == BMS_EVENT_LOG_RESET_REG)
    {
        if (val != 0x0001u) return MB_EX_ILLEGAL_VALUE;
        return bms_event_log_factory_reset() ? 0u : MB_EX_DEVICE_FAILURE;
    }

    /* Preserve historical behavior for unhandled legacy write addresses. */
    return 0u;
}

u16 mb_crc16(const u8 *buf, u32 len)
{
    u16 crc = 0xFFFFu;
    u32 i;
    u8 j;

    for (i = 0u; i < len; i++)
    {
        crc ^= buf[i];
        for (j = 0u; j < 8u; j++)
        {
            if (crc & 1u)
                crc = (u16)((crc >> 1) ^ 0xA001u);
            else
                crc >>= 1;
        }
    }
    return crc;
}

static u16 u16be(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

static void put_u16be(u8 *p, u16 v)
{
    p[0] = (u8)(v >> 8);
    p[1] = (u8)(v & 0xFFu);
}

extern int AFE_PARAM_WRITE_Flag;

int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len)
{
    u16 crc_rx;
    u16 crc;
    u8 addr;
    u8 func;

    *rsp_len = 0u;

    if (req_len < 4u) return 0;
    if (req[0] != MB_ADDR && req[0] != 0x00u) return 0;

    crc_rx = (u16)(((u16)req[req_len - 1u] << 8) | req[req_len - 2u]);
    crc = mb_crc16(req, req_len - 2u);
    if (crc != crc_rx) return 0;

    addr = req[0];
    func = req[1];

    /* Debug echo retained for existing production tools. */
    if (func == 0x7Fu && addr != 0x00u)
    {
        if (req_len > 268u) return 0;
        memcpy(rsp, req, req_len);
        *rsp_len = req_len;
        return 1;
    }

    if (func == 0x03u)
    {
        u16 reg;
        u16 qty;
        u32 bytes;
        u32 l;
        u16 i;

        if (req_len < 8u) return 0;
        reg = u16be(&req[2]);
        qty = u16be(&req[4]);
        if (qty == 0u || qty > 0x7Du)
            return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);

        if (read_event_log_frame(addr, func, reg, qty, rsp, rsp_len))
            return (addr != 0x00u);

        bytes = (u32)qty * 2u;
        rsp[0] = addr;
        rsp[1] = func;
        rsp[2] = (u8)bytes;
        for (i = 0u; i < qty; i++)
            put_u16be(&rsp[3u + (u32)i * 2u], read_reg((u16)(reg + i)));

        l = 3u + bytes;
        crc = mb_crc16(rsp, l);
        rsp[l] = (u8)(crc & 0xFFu);
        rsp[l + 1u] = (u8)(crc >> 8);
        *rsp_len = l + 2u;
        return (addr != 0x00u);
    }

    if (func == 0x06u)
    {
        u16 reg;
        u16 val;
        u8 exception;

        if (req_len < 8u) return 0;
        reg = u16be(&req[2]);
        val = u16be(&req[4]);

        exception = write_reg(reg, val);
        if (exception != 0u)
            return modbus_exception(addr, func, exception, rsp, rsp_len);

        if (reg_requires_param_save(reg))
        {
            SaveParam();
            AFE_PARAM_WRITE_Flag = 1;
        }

        if (addr == 0x00u) return 0;
        memcpy(rsp, req, req_len);
        *rsp_len = req_len;
        return 1;
    }

    if (func == 0x10u)
    {
        u16 reg;
        u16 qty;
        u8 bytecnt;
        const u8 *pdata;
        u16 i;
        int need_save_param = 0;
        u8 exception;

        if (req_len < 9u) return 0;
        reg = u16be(&req[2]);
        qty = u16be(&req[4]);
        bytecnt = req[6];

        if (qty == 0u || qty > 0x7Bu)
            return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);
        if (bytecnt != (u8)(qty * 2u))
            return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);
        if (req_len < (u32)(7u + bytecnt + 2u)) return 0;

        /*
         * DVC safety configuration is transactional per semantic field today.
         * Reject multi-field writes instead of accepting a half-updated AFE
         * when a later field fails. Use 0x06 until batch commit is implemented.
         */
        if (qty > 1u && dvc_comm_range_contains(reg, qty))
            return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);

        pdata = &req[7];
        for (i = 0u; i < qty; i++)
        {
            u16 write_addr = (u16)(reg + i);
            u16 value = u16be(&pdata[(u32)i * 2u]);

            exception = write_reg(write_addr, value);
            if (exception != 0u)
                return modbus_exception(addr, func, exception, rsp, rsp_len);

            if (reg_requires_param_save(write_addr)) need_save_param = 1;
        }

        if (need_save_param)
        {
            SaveParam();
            AFE_PARAM_WRITE_Flag = 1;
        }

        if (reg >= BTNAME_REG_BASE && reg < (BTNAME_REG_BASE + BTNAME_REG_WORDS))
            btname_modbus_on_write_holding(addr, qty, (const uint16_t *)pdata);

        if (addr == 0x00u) return 0;
        rsp[0] = addr;
        rsp[1] = func;
        put_u16be(&rsp[2], reg);
        put_u16be(&rsp[4], qty);
        crc = mb_crc16(rsp, 6u);
        rsp[6] = (u8)(crc & 0xFFu);
        rsp[7] = (u8)(crc >> 8);
        *rsp_len = 8u;
        return 1;
    }

    return modbus_exception(addr, func, MB_EX_ILLEGAL_FUNCTION, rsp, rsp_len);
}

static int read_event_log_frame(u8 addr,
                                u8 func,
                                u16 reg,
                                u16 qty,
                                u8 *rsp,
                                u32 *rsp_len)
{
    u16 i;
    u32 bytes;
    u32 l;
    u16 crc;

    if (reg != BMS_EVENT_LOG_REG_BASE) return 0;
    if ((qty == 0u) || (qty > BMS_EVENT_LOG_REG_COUNT)) return 0;

    bytes = (u32)qty * 2u;
    rsp[0] = addr;
    rsp[1] = func;
    rsp[2] = (u8)bytes;
    for (i = 0u; i < qty; ++i)
        put_u16be(&rsp[3u + (u32)i * 2u], bms_event_log_read_reg(i));

    l = 3u + bytes;
    crc = mb_crc16(rsp, l);
    rsp[l] = (u8)(crc & 0xFFu);
    rsp[l + 1u] = (u8)(crc >> 8);
    *rsp_len = l + 2u;
    return 1;
}

static u16 encode_signed_current_reg(void)
{
    int16_t signed_current = 0;

    if (g_stCellInfoReport.u16IDischg)
        signed_current = (int16_t)(-((int16_t)g_stCellInfoReport.u16IDischg));
    else if (g_stCellInfoReport.u16Ichg)
        signed_current = (int16_t)g_stCellInfoReport.u16Ichg;

    return (u16)signed_current;
}

static u16 read_realtime_status_reg(u16 reg)
{
    switch (reg)
    {
    case BMS_REALTIME_REG_MAGIC_ADDR:       return BMS_REALTIME_REG_MAGIC;
    case BMS_REALTIME_REG_VERSION_ADDR:     return BMS_REALTIME_REG_VERSION;
    case BMS_REALTIME_REG_VOLTAGE_ADDR:     return g_stCellInfoReport.u16VCellTotle;
    case BMS_REALTIME_REG_CURRENT_ADDR:     return encode_signed_current_reg();
    case BMS_REALTIME_REG_SOC_ADDR:         return g_stCellInfoReport.SocElement.u16Soc;
    case BMS_REALTIME_REG_TEMP_MAX_ADDR:    return g_stCellInfoReport.u16TempMax;
    case BMS_REALTIME_REG_TEMP_MIN_ADDR:    return g_stCellInfoReport.u16TempMin;
    case BMS_REALTIME_REG_TEMP_MOS_ADDR:    return g_stCellInfoReport.u16Temperature[MOS_TEMP1];
    case BMS_REALTIME_REG_VCELL_MAX_ADDR:   return g_stCellInfoReport.u16VCellMax;
    case BMS_REALTIME_REG_VCELL_MIN_ADDR:   return g_stCellInfoReport.u16VCellMin;
    case BMS_REALTIME_REG_VCELL_DELTA_ADDR: return g_stCellInfoReport.u16VCellDelta;
    default: return 0u;
    }
}

static u16 read_ascii_string_reg(const u8 *str, u16 max_len, u16 reg_offset)
{
    u16 str_idx = reg_offset * 2u;
    u8 high_byte = 0x00u;
    u8 low_byte = 0x00u;

    if (str_idx < max_len && str[str_idx] != '\0') high_byte = str[str_idx];
    if ((str_idx + 1u) < max_len && str[str_idx + 1u] != '\0') low_byte = str[str_idx + 1u];

    return (u16)(((u16)high_byte << 8) | low_byte);
}

static u16 read_production_info_reg(u16 reg)
{
    if (reg >= PROD_SN_REG_BASE && reg < (PROD_SN_REG_BASE + PROD_SN_REG_COUNT))
        return read_ascii_string_reg(ProductionInfor.BMS_SerialNumber,
                                     PRODUCT_ID_LENGTH_MAX,
                                     (u16)(reg - PROD_SN_REG_BASE));

    if (reg >= PROD_HW_VER_REG_BASE && reg < (PROD_HW_VER_REG_BASE + PROD_HW_VER_REG_COUNT))
        return read_ascii_string_reg(ProductionInfor.BMS_HardWareVersion,
                                     PRODUCT_ID_LENGTH_MAX,
                                     (u16)(reg - PROD_HW_VER_REG_BASE));

    if (reg >= PROD_SW_VER_REG_BASE && reg < (PROD_SW_VER_REG_BASE + PROD_SW_VER_REG_COUNT))
        return read_ascii_string_reg(ProductionInfor.BMS_SoftWareVersion,
                                     PRODUCT_ID_LENGTH_MAX,
                                     (u16)(reg - PROD_SW_VER_REG_BASE));

    return 0u;
}

void WriteProID_Default(void)
{
    UINT8 hardwareCount = sizeof(BMS_HARDWARE_VERDION_DEFAULT) > PRODUCT_ID_LENGTH_MAX
                              ? PRODUCT_ID_LENGTH_MAX
                              : sizeof(BMS_HARDWARE_VERDION_DEFAULT);
    UINT8 softwareCount = sizeof(BMS_SOFTWARE_VERDION_DEFAULT) > PRODUCT_ID_LENGTH_MAX
                              ? PRODUCT_ID_LENGTH_MAX
                              : sizeof(BMS_SOFTWARE_VERDION_DEFAULT);
    UINT8 serialNumberCount = sizeof(BMS_SERIAL_NUMBER_DEFAULT) > PRODUCT_ID_LENGTH_MAX
                                  ? PRODUCT_ID_LENGTH_MAX
                                  : sizeof(BMS_SERIAL_NUMBER_DEFAULT);

    memset(&ProductionInfor, 0, sizeof(PRODUCTION_ID_INFO));
    memcpy(&ProductionInfor.BMS_HardWareVersion[0], BMS_HARDWARE_VERDION_DEFAULT, hardwareCount);
    memcpy(&ProductionInfor.BMS_SoftWareVersion[0], BMS_SOFTWARE_VERDION_DEFAULT, softwareCount);
    memcpy(&ProductionInfor.BMS_SerialNumber[0], BMS_SERIAL_NUMBER_DEFAULT, serialNumberCount);
}
