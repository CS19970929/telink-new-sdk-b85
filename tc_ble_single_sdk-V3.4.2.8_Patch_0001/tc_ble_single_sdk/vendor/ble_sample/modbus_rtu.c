#include "modbus_rtu.h"
// #include <string.h>
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "sci_upper.h"
#include "param.h"
#include "SocEnhance.h"
#include "sh367309_datadeal.h"
#include "app.h"

#define MB_ADDR        0x01

extern struct stCell_Info g_stCellInfoReport;
static u16 read_reg(u16 reg) {
    // TODO: 这里换成你的寄存器表
    // 先给个可见的动态值
    UINT16 u16SciTemp;
	UINT16 i = 0, j;
	INT8 k;
	UINT8 a[4];
    u16 val;

    if(reg >= 0xd000 && reg <= 0xd03e)
    {
        // return g_stCellInfoReport.u16VCell[reg - 0xd000];
        return *(&g_stCellInfoReport.u16VCell[0] + (reg - 0xd000));
    }
    if(reg >= 0x2100 && reg <= 0x2140)
    {
        return *(&g_tParam.protect.u16VcellOvp_First + (reg - 0x2100));
    }
    if(reg >= 0xD100 && reg <= 0xD114)
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
        // System_ErrFlag.u8ErrFlag_Com_AFE1 = 1;
        // System_ErrFlag.u8ErrFlag_Com_AFE2 = 8;
        // System_ErrFlag.u8ErrFlag_Store_EEPROM = 66;
        // System_ErrFlag.u8ErrFlag_CBC_DSG = 88;
        if(reg >= 0xD109 && reg <= 0xD114)
        {
            return ((*(&System_ErrFlag.u8ErrFlag_Com_AFE1 + 2 * (reg - 0xd109))) << 8) | (*(&System_ErrFlag.u8ErrFlag_Com_AFE1 + 2 * (reg - 0xd109) + 1));
        }
    }
    SystemStatus.bits.b1StartUpBMS = 1;
    SystemStatus.bits.b1Status_ToSleep = 1;
    SystemStatus.bits.b1Status_AFE1 = 1;
    if(reg >= 0xD115 && reg <= 0xD118)
    {
        if(reg == 0xd115) return ((UINT16)(SystemStatus.all & 0x0000FFFF));
        if(reg == 0xd116) return ((UINT16)(SystemStatus.all >> 16));
        // if(reg == 0xd117) return ((UINT16)(System_OnOFF_Func.all & 0x0000FFFF));
        // if(reg == 0xd118) return ((UINT16)(System_OnOFF_Func.all >> 16));
    }
    return 0;
}
extern void enter_fac_mode(bool on);
extern bool deepsleep_en;
static void write_reg(u16 reg, u16 val) {
    (void)reg; (void)val;
    // TODO: 写寄存器
    if(reg >= 0x2100 && reg <= 0x2140)
    {
        *(&g_tParam.protect.u16VcellOvp_First + (reg - 0x2100)) = val; 
    }
    // if(reg == 0x2318) 
    if(reg == 0x1005)  set_soc_param(val, 1, 1);
    if(reg == 0x1102)
    {
        if(val == 0x03) enter_fac_mode(true);
        if(val == 0x0A) deepsleep_en = true;
    }
    if(reg == 0x1103)
    {
        if(val == 0x03) enter_fac_mode(false);
    }
    // if(reg == 0x1103)  SOC_Calculate_Element.u8SOC_Now = val;
    if(reg == 0x2319)  SOC_Calculate_Element.u32Cycle_times = val;
    

}

u16 mb_crc16(const u8 *buf, u32 len)
{
    u16 crc = 0xFFFF;
    for (u32 i = 0; i < len; i++) {
        crc ^= buf[i];
        for (u8 j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else         crc >>= 1;
        }
    }
    return crc;
}

static u16 u16be(const u8 *p){ return ((u16)p[0] << 8) | p[1]; }
static void put_u16be(u8 *p, u16 v){ p[0] = v >> 8; p[1] = v & 0xFF; }

int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len)
{
    *rsp_len = 0;

    if (req_len < 4) return 0;                 // 至少 addr+func+crc
    if (req[0] != MB_ADDR && req[0] != 0x00) return 0; // 0x00广播可选

    if (req_len < 4) return 0;
    u16 crc_rx = ((u16)req[req_len-1] << 8) | req[req_len-2]; // 注意：RTU CRC低字节在前
    u16 crc = mb_crc16(req, req_len-2);
    if (crc != crc_rx) {
        return 0;
    }

    u8 addr = req[0];
    u8 func = req[1];

    // ====== 快速验证模式：收到什么就回什么（仅限非广播）======
    // 用来验证“收->发”链路
    if (func == 0x7F && addr != 0x00) { // 你随便挑个不会冲突的 func 做 echo
        if (req_len <= 268) {
            memcpy(rsp, req, req_len);
            *rsp_len = req_len;
            return 1;
        }
        return 0;
    }

    // 下面是真正 Modbus 功能码
    if (func == 0x03) {
        if (req_len < 8) return 0;
        u16 reg = u16be(&req[2]);
        u16 qty = u16be(&req[4]);
        if (qty == 0 || qty > 0x7D) return 0; // 03 一次最多 125 寄存器

        // rsp: addr func bytecnt data... crc
        u32 bytes = qty * 2;
        rsp[0] = addr;
        rsp[1] = func;
        rsp[2] = (u8)bytes;
        for (u16 i = 0; i < qty; i++) {
            u16 v = read_reg(reg + i);
            put_u16be(&rsp[3 + i*2], v);
        }
        u32 l = 3 + bytes;
        u16 c = mb_crc16(rsp, l);
        rsp[l+0] = (u8)(c & 0xFF);       // CRC低字节
        rsp[l+1] = (u8)(c >> 8);
        *rsp_len = l + 2;
        return (addr != 0x00); // 广播不回
    }
    else if (func == 0x06) {
        if (req_len < 8) return 0;
        u16 reg = u16be(&req[2]);
        u16 val = u16be(&req[4]);
        write_reg(reg, val);

        // 06 回包=原样回显请求（非广播）
        if (addr == 0x00) return 0;
        memcpy(rsp, req, req_len);
        *rsp_len = req_len;
        return 1;
    }
    else if (func == 0x10) {
        if (req_len < 9) return 0;
        u16 reg = u16be(&req[2]);
        u16 qty = u16be(&req[4]);
        u8  bytecnt = req[6];
        if (qty == 0 || qty > 0x7B) return 0;          // 0x10 一次最多 123
        if (bytecnt != qty * 2) return 0;
        if (req_len < (u32)(7 + bytecnt + 2)) return 0;

        const u8 *pdata = &req[7];
        for (u16 i=0;i<qty;i++){
            u16 v = u16be(&pdata[i*2]);
            write_reg(reg+i, v);
        }

        // 回包：addr func reg qty crc
        if (addr == 0x00) return 0;
        rsp[0]=addr; rsp[1]=func;
        put_u16be(&rsp[2], reg);
        put_u16be(&rsp[4], qty);
        u16 c = mb_crc16(rsp, 6);
        rsp[6]=(u8)(c & 0xFF);
        rsp[7]=(u8)(c >> 8);
        *rsp_len = 8;
        return 1;
    }

    // 不支持：异常响应（可选）
    if (addr == 0x00) return 0;
    rsp[0]=addr;
    rsp[1]=func | 0x80;
    rsp[2]=0x01; // illegal function
    u16 c = mb_crc16(rsp, 3);
    rsp[3]=(u8)(c & 0xFF);
    rsp[4]=(u8)(c >> 8);
    *rsp_len = 5;
    return 1;
}
