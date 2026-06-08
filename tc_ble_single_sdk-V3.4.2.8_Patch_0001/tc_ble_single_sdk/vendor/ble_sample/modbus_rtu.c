#include "modbus_rtu.h"
// #include <string.h>
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "sci_upper.h"
#include "param.h"
#include "SocEnhance.h"
#include "bms_event_log.h"
#include "sh367309_datadeal.h"
#include "app.h"
#include "conf.h"
#include "runtime.h"
#include "bms_afe.h"
#include "sh3673520_afe.h"

#include "stack/ble/ble.h"
#include "btname_modbus.h"

#define MB_ADDR 0x01
#define BMS_REALTIME_REG_BASE 0xD120u
#define BMS_REALTIME_REG_COUNT 11u
#define BMS_REALTIME_REG_MAGIC 0x4253u
#define BMS_REALTIME_REG_VERSION 0x0001u
#define BMS_AFE_PARAM_REG_BASE SH3673520_PARAM_REG_BASE
#define BMS_AFE_PARAM_REG_COUNT SH3673520_PARAM_WORD_COUNT

#define BMS_REALTIME_REG_MAGIC_ADDR (BMS_REALTIME_REG_BASE + 0u)
#define BMS_REALTIME_REG_VERSION_ADDR (BMS_REALTIME_REG_BASE + 1u)
#define BMS_REALTIME_REG_VOLTAGE_ADDR (BMS_REALTIME_REG_BASE + 2u)
#define BMS_REALTIME_REG_CURRENT_ADDR (BMS_REALTIME_REG_BASE + 3u)
#define BMS_REALTIME_REG_SOC_ADDR (BMS_REALTIME_REG_BASE + 4u)
#define BMS_REALTIME_REG_TEMP_MAX_ADDR (BMS_REALTIME_REG_BASE + 5u)
#define BMS_REALTIME_REG_TEMP_MIN_ADDR (BMS_REALTIME_REG_BASE + 6u)
#define BMS_REALTIME_REG_TEMP_MOS_ADDR (BMS_REALTIME_REG_BASE + 7u)
#define BMS_REALTIME_REG_VCELL_MAX_ADDR (BMS_REALTIME_REG_BASE + 8u)
#define BMS_REALTIME_REG_VCELL_MIN_ADDR (BMS_REALTIME_REG_BASE + 9u)
#define BMS_REALTIME_REG_VCELL_DELTA_ADDR (BMS_REALTIME_REG_BASE + 10u)

static u16 read_ascii_string_reg(const u8 *str, u16 max_len, u16 reg_offset);
static u16 read_production_info_reg(u16 reg);
static int read_event_log_frame(u8 addr, u8 func, u16 reg, u16 qty, u8 *rsp, u32 *rsp_len);
static u16 read_realtime_status_reg(u16 reg);
static u16 encode_signed_current_reg(void);
void WriteProID_Default(void);

struct stCell_Info g_stCellInfoReport;
PRODUCTION_ID_INFO ProductionInfor;

static u16 read_reg(u16 reg)
{
    // TODO: 这里换成你的寄存器表
    // 先给个可见的动态值
    UINT16 u16SciTemp;
    UINT16 j;
    INT8 k;
    UINT8 a[4];
    u16 val;

    if (reg >= 0 && reg < 3)
    {
        // return *(&g_stCellInfoReport.mac_public[0] + (reg - 0x0000));
        switch (reg)
        {
        case 0:
            val = (g_stCellInfoReport.mac_public[0] << 8) | g_stCellInfoReport.mac_public[1]; // mac_public[0] + mac_public[1]
            break;
        case 1:
            val = (g_stCellInfoReport.mac_public[2] << 8) | g_stCellInfoReport.mac_public[3]; // mac_public[0] + mac_public[1]
            break;
        case 2:
            val = (g_stCellInfoReport.mac_public[4] << 8) | g_stCellInfoReport.mac_public[5]; // mac_public[0] + mac_public[1]
            break;
        default:
            // 错误的寄存器地址
            break;
        }
        return val;
    }

    if (reg >= BTNAME_REG_BASE && reg < BTNAME_REG_BASE + BTNAME_REG_COUNT)
    {
        u16 idx = reg - BTNAME_REG_BASE; // 寄存器索引
        u16 str_idx = idx * 2;           // 在字符串中的起始位置

        // 获取蓝牙名称
        const char *name = btname_get();

        // 获取两个字符
        u8 high_byte = 0x00;
        u8 low_byte = 0x00;

        if (str_idx < BTNAME_TOTAL_MAX_LEN && name[str_idx] != '\0')
        {
            high_byte = name[str_idx];
        }

        if (str_idx + 1 < BTNAME_TOTAL_MAX_LEN && name[str_idx + 1] != '\0')
        {
            low_byte = name[str_idx + 1];
        }

        // 组合成16位值（高字节在前，符合Modbus大端格式）
        return (high_byte << 8) | low_byte;
    }

    // uint16_t *p
    // if(reg >= 0xc002 && reg <= (0xc002 + 48))
    // {
    //     return *(&ProductionInfor.BMS_SerialNumber[0] + (reg ))

    // }
    /* ProductionInfor寄存器 */
    // if ((reg >= PROD_SN_REG_BASE && reg < (PROD_SW_VER_REG_BASE + PROD_SW_VER_REG_COUNT)) ||
    //     (reg >= PROD_SN_LEN_REG && reg <= PROD_SW_VER_WRITE_FLAG_REG))
    if (reg >= 0xc002 && reg <= (0xc002 + 48))
    {
        return read_production_info_reg(reg);
    }

    if (reg >= 0xd000 && reg <= 0xd03e)
    {
        // return g_stCellInfoReport.u16VCell[reg - 0xd000];
        return *(&g_stCellInfoReport.u16VCell[0] + (reg - 0xd000));
    }
    if (reg >= 0x2100 && reg <= 0x2140)
    {
        return *(&g_tParam.protect.u16VcellOvp_First + (reg - 0x2100));
    }
    if (reg >= BMS_AFE_PARAM_REG_BASE && reg < (BMS_AFE_PARAM_REG_BASE + BMS_AFE_PARAM_REG_COUNT))
    {
        return bms_afe_param_read_word((u16)(reg - BMS_AFE_PARAM_REG_BASE));
    }
    if (reg >= 0xD100 && reg <= 0xD114)
    {
        for (j = 0; j < 4; j++)
        {
            k = FaultPoint_First2 - 1 - j;
            if (k < 0)
            {
                k = Record_len + k;
            }
            a[j] = k;
        }
        for (j = 0; j < 4; j++)
        {
            k = FaultPoint_Second2 - 1 - j;
            if (k < 0)
            {
                k = Record_len + k;
            }
            a[j] = k;
        }
        for (j = 0; j < 4; j++)
        {
            k = FaultPoint_Third2 - 1 - j;
            if (k < 0)
            {
                k = Record_len + k;
            }
            a[j] = k;
        }

        switch (reg)
        {
        case 0xD100:
        case 0xD101:
        case 0xD102:
            return 0;
            break;
        case 0xD103:
            u16SciTemp = (Fault_record_First2[a[0]] << 8) | Fault_record_First2[a[1]];
            val = u16SciTemp;
            return val;
            break;
        case 0xD104:
            u16SciTemp = (Fault_record_First2[a[2]] << 8) | Fault_record_First2[a[3]];
            val = u16SciTemp;
            return val;
            break;
        case 0xD105:
            u16SciTemp = (Fault_record_Second2[a[0]] << 8) | Fault_record_Second2[a[1]];
            val = u16SciTemp;
            return val;
            break;
        case 0xD106:
            u16SciTemp = (Fault_record_Second2[a[2]] << 8) | Fault_record_Second2[a[3]];
            val = u16SciTemp;
            return val;
            break;
        case 0xD107:
            u16SciTemp = (Fault_record_Third2[a[0]] << 8) | Fault_record_Third2[a[1]];
            val = u16SciTemp;
            return val;
            break;
        case 0xD108:
            u16SciTemp = (Fault_record_Third2[a[2]] << 8) | Fault_record_Third2[a[3]];
            val = u16SciTemp;
            return val;
            break;
        default:
            break;
        }

        if (reg >= 0xD109 && reg <= 0xD114)
        {
            return ((*(&System_ErrFlag.u8ErrFlag_Com_AFE1 + 2 * (reg - 0xd109))) << 8) | (*(&System_ErrFlag.u8ErrFlag_Com_AFE1 + 2 * (reg - 0xd109) + 1));
        }
    }
    // SystemStatus.bits.b1StartUpBMS = 1;
    // SystemStatus.bits.b1Status_ToSleep = 1;
    // SystemStatus.bits.b1Status_AFE1 = 1;
    if (reg >= 0xD115 && reg <= 0xD118)
    {
        if (reg == 0xd115)
            return ((UINT16)(SystemStatus.all & 0x0000FFFF));
        if (reg == 0xd116)
            return ((UINT16)(SystemStatus.all >> 16));
        // if(reg == 0xd117) return ((UINT16)(System_OnOFF_Func.all & 0x0000FFFF));
        // if(reg == 0xd118) return ((UINT16)(System_OnOFF_Func.all >> 16));
    }
    if (reg >= BMS_REALTIME_REG_BASE && reg < (BMS_REALTIME_REG_BASE + BMS_REALTIME_REG_COUNT))
    {
        return read_realtime_status_reg(reg);
    }
    return 0;
}
extern void enter_fac_mode(bool on);
extern bool deepsleep_en;
extern uint8_t get_soc_real(void);
static int reg_requires_param_save(u16 reg)
{
    return (reg >= 0x2100u && reg <= 0x2140u);
}

static int reg_requires_afe_apply(u16 reg)
{
    return (reg >= BMS_AFE_PARAM_REG_BASE && reg < (BMS_AFE_PARAM_REG_BASE + BMS_AFE_PARAM_REG_COUNT));
}

static void write_reg(u16 reg, u16 val)
{
    (void)reg;
    (void)val;

    if (reg >= 0x2100 && reg <= 0x2140)
    {
        *(&g_tParam.protect.u16VcellOvp_First + (reg - 0x2100)) = val;
    }
    if (reg_requires_afe_apply(reg))
    {
        if (!bms_afe_param_write_word((u16)(reg - BMS_AFE_PARAM_REG_BASE), val))
        {
            System_ERROR_UserCallback(ERROR_EEPROM_STORE);
        }
    }
    // if(reg == 0x2318)
    if (reg == 0x1005)
        set_soc_param(val, 1, 1);
    if (reg == 0x1102)
    {
        if (val == 0x03)
        {
            if (!Runtime_ReenterFactoryMode())
            {
                System_ERROR_UserCallback(ERROR_EEPROM_STORE);
            }
        }
        // if(val == 0x01) sys_time.enable_log_test_balance = true;
#ifdef __TEST_SOC__
        if (val == 0x01)
        {
            sys_time.CHG = CapacityFactory * 5;
            sys_time.DSG = 0;
        }
#endif // __TEST_SOC__
        // if(val == 0x03) sys_time.enable_current_test = true;
        // if(val == 0x06) sys_time.enable_log_test_first = true;
        // if(val == 0x06) sys_time.enable_current_test = false;
        if (val == 0x0A)
            deepsleep_en = true;
    }
    if (reg == 0x1103)
    {
#ifdef __TEST_SOC__
        if(val == 0x01) 
        {
            sys_time.CHG = 0;
            sys_time.DSG = CapacityFactory * 5;
        }
#endif // __TEST_SOC__
        // if(val == 0x03) enter_fac_mode(false);
        // if(val == 0x03) sys_time.enable_current_test = false;
    }
    // if(reg == 0x1103)  SOC_Calculate_Element.u8SOC_Now = val;
    if (reg == 0x2319)
    {
        SOC_Calculate_Element.u32Cycle_times = val;
        set_soc_param(get_soc_real(), 1, 1);
    }
    if ((reg == BMS_EVENT_LOG_RESET_REG) && (val == 0x0001u))
    {
        (void)bms_event_log_factory_reset();
    }
}

u16 mb_crc16(const u8 *buf, u32 len)
{
    u16 crc = 0xFFFF;
    for (u32 i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (u8 j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static u16 u16be(const u8 *p) { return ((u16)p[0] << 8) | p[1]; }
static void put_u16be(u8 *p, u16 v)
{
    p[0] = v >> 8;
    p[1] = v & 0xFF;
}

extern int AFE_PARAM_WRITE_Flag;
extern void test_SH367309_UpdataAfeConfig(void);

int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len)
{
    *rsp_len = 0;

    if (req_len < 4)
        return 0; // 至少 addr+func+crc
    if (req[0] != MB_ADDR && req[0] != 0x00)
        return 0; // 0x00广播可选

    if (req_len < 4)
        return 0;
    u16 crc_rx = ((u16)req[req_len - 1] << 8) | req[req_len - 2]; // 注意：RTU CRC低字节在前
    u16 crc = mb_crc16(req, req_len - 2);
    if (crc != crc_rx)
    {
        return 0;
    }

    u8 addr = req[0];
    u8 func = req[1];

    // ====== 快速验证模式：收到什么就回什么（仅限非广播）======
    // 用来验证“收->发”链路
    if (func == 0x7F && addr != 0x00)
    { // 你随便挑个不会冲突的 func 做 echo
        if (req_len <= 268)
        {
            memcpy(rsp, req, req_len);
            *rsp_len = req_len;
            return 1;
        }
        return 0;
    }

    // 下面是真正 Modbus 功能码
    if (func == 0x03)
    {
        if (req_len < 8)
            return 0;
        u16 reg = u16be(&req[2]);
        u16 qty = u16be(&req[4]);
        if (qty == 0 || qty > 0x7D)
            return 0; // 03 一次最多 125 寄存器

        // rsp: addr func bytecnt data... crc
        if (read_event_log_frame(addr, func, reg, qty, rsp, rsp_len))
        {
            return (addr != 0x00);
        }
        u32 bytes = qty * 2;
        rsp[0] = addr;
        rsp[1] = func;
        rsp[2] = (u8)bytes;
        for (u16 i = 0; i < qty; i++)
        {
            u16 v = read_reg(reg + i);
            put_u16be(&rsp[3 + i * 2], v);
        }
        u32 l = 3 + bytes;
        u16 c = mb_crc16(rsp, l);
        rsp[l + 0] = (u8)(c & 0xFF); // CRC低字节
        rsp[l + 1] = (u8)(c >> 8);
        *rsp_len = l + 2;
        return (addr != 0x00); // 广播不回
    }
    else if (func == 0x06)
    {
        if (req_len < 8)
            return 0;
        u16 reg = u16be(&req[2]);
        u16 val = u16be(&req[4]);
        write_reg(reg, val);
        if (reg_requires_param_save(reg))
        {
            SaveParam();
            AFE_PARAM_WRITE_Flag = 1;
            // test_SH367309_UpdataAfeConfig();
        }
        if (reg_requires_afe_apply(reg))
        {
            if (!bms_afe_param_commit_and_apply())
            {
                System_ERROR_UserCallback(ERROR_EEPROM_STORE);
            }
        }

        // 06 回包=原样回显请求（非广播）
        if (addr == 0x00)
            return 0;
        memcpy(rsp, req, req_len);
        *rsp_len = req_len;
        return 1;
    }
    else if (func == 0x10)
    {
        if (req_len < 9)
            return 0;
        u16 reg = u16be(&req[2]);
        u16 qty = u16be(&req[4]);
        u8 bytecnt = req[6];
        int need_save_param = 0;
        int need_apply_afe = 0;
        if (qty == 0 || qty > 0x7B)
            return 0; // 0x10 一次最多 123
        if (bytecnt != qty * 2)
            return 0;
        if (req_len < (u32)(7 + bytecnt + 2))
            return 0;

        const u8 *pdata = &req[7];
        for (u16 i = 0; i < qty; i++)
        {
            u16 v = u16be(&pdata[i * 2]);
            write_reg(reg + i, v);
            if (reg_requires_param_save((u16)(reg + i)))
            {
                need_save_param = 1;
            }
            if (reg_requires_afe_apply((u16)(reg + i)))
            {
                need_apply_afe = 1;
            }
        }
        if (need_save_param)
        {
            SaveParam();
            AFE_PARAM_WRITE_Flag = 1;
            // test_SH367309_UpdataAfeConfig();
        }
        if (need_apply_afe)
        {
            if (!bms_afe_param_commit_and_apply())
            {
                System_ERROR_UserCallback(ERROR_EEPROM_STORE);
            }
        }

        // 如果是蓝牙名称相关寄存器，调用btname_modbus_on_write_holding
        if (reg >= BTNAME_REG_BASE && reg < (BTNAME_REG_BASE + BTNAME_REG_WORDS))
        {
            btname_modbus_on_write_holding(addr, qty, (const uint16_t *)pdata);
            // btname_modbus_on_write_holding(addr, bytecnt, (const uint16_t *)pdata);
        }

        // 回包：addr func reg qty crc
        if (addr == 0x00)
            return 0;
        rsp[0] = addr;
        rsp[1] = func;
        put_u16be(&rsp[2], reg);
        put_u16be(&rsp[4], qty);
        u16 c = mb_crc16(rsp, 6);
        rsp[6] = (u8)(c & 0xFF);
        rsp[7] = (u8)(c >> 8);
        *rsp_len = 8;
        return 1;
    }

    // 不支持：异常响应（可选）
    if (addr == 0x00)
        return 0;
    rsp[0] = addr;
    rsp[1] = func | 0x80;
    rsp[2] = 0x01; // illegal function
    u16 c = mb_crc16(rsp, 3);
    rsp[3] = (u8)(c & 0xFF);
    rsp[4] = (u8)(c >> 8);
    *rsp_len = 5;
    return 1;
}

static int read_event_log_frame(u8 addr, u8 func, u16 reg, u16 qty, u8 *rsp, u32 *rsp_len)
{
    u16 i;
    u16 v;
    u32 bytes;
    u32 l;
    u16 c;

    /*
     * 兼容旧上位机日志协议:
     * 旧工程以 0xC008 作为日志入口，但当前项目 0xC002~0xC032
     * 已经用于产品信息，所以这里只对“从 0xC008 发起的日志读”
     * 做特判，不改现有寄存器平铺逻辑。
     */
    if (reg != BMS_EVENT_LOG_REG_BASE)
    {
        return 0;
    }

    if ((qty == 0u) || (qty > BMS_EVENT_LOG_REG_COUNT))
    {
        return 0;
    }

    bytes = (u32)qty * 2u;
    rsp[0] = addr;
    rsp[1] = func;
    rsp[2] = (u8)bytes;
    for (i = 0u; i < qty; ++i)
    {
        v = bms_event_log_read_reg(i);
        put_u16be(&rsp[3 + i * 2u], v);
    }

    l = 3u + bytes;
    c = mb_crc16(rsp, l);
    rsp[l + 0u] = (u8)(c & 0xFFu);
    rsp[l + 1u] = (u8)(c >> 8);
    *rsp_len = l + 2u;
    return 1;
}

static u16 encode_signed_current_reg(void)
{
    int16_t signed_current = 0;

    if (g_stCellInfoReport.u16IDischg)
    {
        signed_current = (int16_t)(-((int16_t)g_stCellInfoReport.u16IDischg));
    }
    else if (g_stCellInfoReport.u16Ichg)
    {
        signed_current = (int16_t)g_stCellInfoReport.u16Ichg;
    }

    return (u16)signed_current;
}

static u16 read_realtime_status_reg(u16 reg)
{
    switch (reg)
    {
    case BMS_REALTIME_REG_MAGIC_ADDR:
        return BMS_REALTIME_REG_MAGIC;
    case BMS_REALTIME_REG_VERSION_ADDR:
        return BMS_REALTIME_REG_VERSION;
    case BMS_REALTIME_REG_VOLTAGE_ADDR:
        return g_stCellInfoReport.u16VCellTotle;
    case BMS_REALTIME_REG_CURRENT_ADDR:
        return encode_signed_current_reg();
    case BMS_REALTIME_REG_SOC_ADDR:
        return g_stCellInfoReport.SocElement.u16Soc;
    case BMS_REALTIME_REG_TEMP_MAX_ADDR:
        return g_stCellInfoReport.u16TempMax;
    case BMS_REALTIME_REG_TEMP_MIN_ADDR:
        return g_stCellInfoReport.u16TempMin;
    case BMS_REALTIME_REG_TEMP_MOS_ADDR:
        return g_stCellInfoReport.u16Temperature[MOS_TEMP1];
    case BMS_REALTIME_REG_VCELL_MAX_ADDR:
        return g_stCellInfoReport.u16VCellMax;
    case BMS_REALTIME_REG_VCELL_MIN_ADDR:
        return g_stCellInfoReport.u16VCellMin;
    case BMS_REALTIME_REG_VCELL_DELTA_ADDR:
        return g_stCellInfoReport.u16VCellDelta;
    default:
        return 0;
    }
}

static u16 read_ascii_string_reg(const u8 *str, u16 max_len, u16 reg_offset)
{
    u16 str_idx = reg_offset * 2;
    u8 high_byte = 0x00;
    u8 low_byte = 0x00;

    if (str_idx < max_len && str[str_idx] != '\0')
    {
        high_byte = str[str_idx];
    }

    if ((str_idx + 1) < max_len && str[str_idx + 1] != '\0')
    {
        low_byte = str[str_idx + 1];
    }

    return ((u16)high_byte << 8) | low_byte;
}

static u16 read_production_info_reg(u16 reg)
{
    /* 1. 序列号 */
    if (reg >= PROD_SN_REG_BASE && reg < (PROD_SN_REG_BASE + PROD_SN_REG_COUNT))
    {
        return read_ascii_string_reg(ProductionInfor.BMS_SerialNumber,
                                     PRODUCT_ID_LENGTH_MAX,
                                     reg - PROD_SN_REG_BASE);
    }

    /* 2. 硬件版本 */
    if (reg >= PROD_HW_VER_REG_BASE && reg < (PROD_HW_VER_REG_BASE + PROD_HW_VER_REG_COUNT))
    {
        return read_ascii_string_reg(ProductionInfor.BMS_HardWareVersion,
                                     PRODUCT_ID_LENGTH_MAX,
                                     reg - PROD_HW_VER_REG_BASE);
    }

    /* 3. 软件版本 */
    if (reg >= PROD_SW_VER_REG_BASE && reg < (PROD_SW_VER_REG_BASE + PROD_SW_VER_REG_COUNT))
    {
        return read_ascii_string_reg(ProductionInfor.BMS_SoftWareVersion,
                                     PRODUCT_ID_LENGTH_MAX,
                                     reg - PROD_SW_VER_REG_BASE);
    }

    /* 4. 长度 */
    // switch (reg)
    // {
    //     case PROD_SN_LEN_REG:
    //         return ProductionInfor.BMS_SerialNumberLength;

    //     case PROD_HW_VER_LEN_REG:
    //         return ProductionInfor.BMS_HardWareVersionLength;

    //     case PROD_SW_VER_LEN_REG:
    //         return ProductionInfor.BMS_SoftWareVersionLength;

    //     case PROD_SN_HEAD_ADDR_REG:
    //         return ProductionInfor.BMS_SerialNumberHeadAdress;

    //     case PROD_HW_VER_HEAD_ADDR_REG:
    //         return ProductionInfor.BMS_HardWareVersionHeadAdress;

    //     case PROD_SW_VER_HEAD_ADDR_REG:
    //         return ProductionInfor.BMS_SoftWareVersionHeadAdress;

    //     case PROD_SN_WRITE_FLAG_REG:
    //         return ProductionInfor.BMS_SerialNumber_WriteFlag;

    //     case PROD_HW_VER_WRITE_FLAG_REG:
    //         return ProductionInfor.BMS_HardWareVersion_WriteFlag;

    //     case PROD_SW_VER_WRITE_FLAG_REG:
    //         return ProductionInfor.BMS_SoftWareVersion_WriteFlag;

    //     default:
    //         break;
    // }

    return 0;
}

#if 1
void WriteProID_Default(void)
{
    UINT8 harewareCount = sizeof(BMS_HARDWARE_VERDION_DEFAULT) > 32 ? 32 : sizeof(BMS_HARDWARE_VERDION_DEFAULT);
    UINT8 softwareCount = sizeof(BMS_SOFTWARE_VERDION_DEFAULT) > 32 ? 32 : sizeof(BMS_SOFTWARE_VERDION_DEFAULT);
    UINT8 serialNumberCount = sizeof(BMS_SERIAL_NUMBER_DEFAULT) > 32 ? 32 : sizeof(BMS_SERIAL_NUMBER_DEFAULT);

    memset(&ProductionInfor, 0, sizeof(PRODUCTION_ID_INFO));

    memcpy(&ProductionInfor.BMS_HardWareVersion[0], BMS_HARDWARE_VERDION_DEFAULT, harewareCount);
    memcpy(&ProductionInfor.BMS_SoftWareVersion[0], BMS_SOFTWARE_VERDION_DEFAULT, softwareCount);
    memcpy(&ProductionInfor.BMS_SerialNumber[0], BMS_SERIAL_NUMBER_DEFAULT, serialNumberCount);
}
#else

void WriteProID_Default(void)
{
    const char *hw = BMS_HARDWARE_VERDION_DEFAULT;
    const char *sw = BMS_SOFTWARE_VERDION_DEFAULT;
    const char *sn = BMS_SERIAL_NUMBER_DEFAULT;

    UINT8 hardwareCount = strlen(hw);
    UINT8 softwareCount = strlen(sw);
    UINT8 serialNumberCount = strlen(sn);

    if (hardwareCount > PRODUCT_ID_LENGTH_MAX - 1)
    {
        hardwareCount = PRODUCT_ID_LENGTH_MAX - 1;
    }
    if (softwareCount > PRODUCT_ID_LENGTH_MAX - 1)
    {
        softwareCount = PRODUCT_ID_LENGTH_MAX - 1;
    }
    if (serialNumberCount > PRODUCT_ID_LENGTH_MAX - 1)
    {
        serialNumberCount = PRODUCT_ID_LENGTH_MAX - 1;
    }

    memset(&ProductionInfor, 0, sizeof(PRODUCTION_ID_INFO));

    memcpy(ProductionInfor.BMS_HardWareVersion, hw, hardwareCount);
    memcpy(ProductionInfor.BMS_SoftWareVersion, sw, softwareCount);
    memcpy(ProductionInfor.BMS_SerialNumber, sn, serialNumberCount);

    ProductionInfor.BMS_HardWareVersion[hardwareCount] = '\0';
    ProductionInfor.BMS_SoftWareVersion[softwareCount] = '\0';
    ProductionInfor.BMS_SerialNumber[serialNumberCount] = '\0';

    ProductionInfor.BMS_HardWareVersionLength = hardwareCount;
    ProductionInfor.BMS_SoftWareVersionLength = softwareCount;
    ProductionInfor.BMS_SerialNumberLength = serialNumberCount;

    /* 如果这些HeadAdress有实际用途，这里初始化 */
    /* ProductionInfor.BMS_SerialNumberHeadAdress = ...; */
    /* ProductionInfor.BMS_HardWareVersionHeadAdress = ...; */
    /* ProductionInfor.BMS_SoftWareVersionHeadAdress = ...; */

    ProductionInfor.BMS_SerialNumber_WriteFlag = 0;
    ProductionInfor.BMS_HardWareVersion_WriteFlag = 0;
    ProductionInfor.BMS_SoftWareVersion_WriteFlag = 0;
}
#endif
