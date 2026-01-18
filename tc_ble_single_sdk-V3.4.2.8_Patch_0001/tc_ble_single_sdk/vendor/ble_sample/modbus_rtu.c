#include "modbus_rtu.h"
// #include <string.h>
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "sci_upper.h"
#include "param.h"
#include "SocEnhance.h"

#define MB_ADDR        0x01

extern struct stCell_Info g_stCellInfoReport;
static u16 read_reg(u16 reg) {
    // TODO: 杩欓噷鎹㈡垚浣犵殑瀵勫瓨鍣ㄨ〃
    // 鍏堢粰涓彲瑙佺殑鍔ㄦ�佸��

    if(reg >= 0xd000 && reg <= 0xd03e)
    {
        // return g_stCellInfoReport.u16VCell[reg - 0xd000];
        return *(&g_stCellInfoReport.u16VCell[0] + (reg - 0xd000));
    }
    if(reg >= 0x2100 && reg <= 0x2140)
    {
        return *(&g_tParam.protect.u16VcellOvp_First + (reg - 0x2100));
    }
    return 0;
}
static void write_reg(u16 reg, u16 val) {
    (void)reg; (void)val;
    // TODO: 鍐欏瘎瀛樺櫒
    if(reg >= 0x2100 && reg <= 0x2140)
    {
        *(&g_tParam.protect.u16VcellOvp_First + (reg - 0x2100)) = val; 
    }
    // if(reg == 0x2318) 
    if(reg == 0x1005)  SOC_Calculate_Element.u8SOC_Now = val;
    if(reg == 0x2319)  SOC_Calculate_Element.u32Cycle_times = val;
    // if(reg == 0x231A)  SOC_Calculate_Element.uj32Cycle_times = val;

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

    if (req_len < 4) return 0;                 // 鑷冲皯 addr+func+crc
    if (req[0] != MB_ADDR && req[0] != 0x00) return 0; // 0x00骞挎挱鍙��

    if (req_len < 4) return 0;
    u16 crc_rx = ((u16)req[req_len-1] << 8) | req[req_len-2]; // 娉ㄦ剰锛歊TU CRC浣庡瓧鑺傚湪鍓�
    u16 crc = mb_crc16(req, req_len-2);
    if (crc != crc_rx) {
        return 0;
    }

    u8 addr = req[0];
    u8 func = req[1];

    // ====== 蹇�熼獙璇佹ā寮忥細鏀跺埌浠�涔堝氨鍥炰粈涔堬紙浠呴檺闈炲箍鎾級======
    // 鐢ㄦ潵楠岃瘉鈥滄敹->鍙戔�濋摼璺�
    if (func == 0x7F && addr != 0x00) { // 浣犻殢渚挎寫涓笉浼氬啿绐佺殑 func 鍋� echo
        if (req_len <= 268) {
            memcpy(rsp, req, req_len);
            *rsp_len = req_len;
            return 1;
        }
        return 0;
    }

    // 涓嬮潰鏄湡姝� Modbus 鍔熻兘鐮�
    if (func == 0x03) {
        if (req_len < 8) return 0;
        u16 reg = u16be(&req[2]);
        u16 qty = u16be(&req[4]);
        if (qty == 0 || qty > 0x7D) return 0; // 03 涓�娆℃渶澶� 125 瀵勫瓨鍣�

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
        rsp[l+0] = (u8)(c & 0xFF);       // CRC浣庡瓧鑺�
        rsp[l+1] = (u8)(c >> 8);
        *rsp_len = l + 2;
        return (addr != 0x00); // 骞挎挱涓嶅洖
    }
    else if (func == 0x06) {
        if (req_len < 8) return 0;
        u16 reg = u16be(&req[2]);
        u16 val = u16be(&req[4]);
        write_reg(reg, val);

        // 06 鍥炲寘=鍘熸牱鍥炴樉璇锋眰锛堥潪骞挎挱锛�
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
        if (qty == 0 || qty > 0x7B) return 0;          // 0x10 涓�娆℃渶澶� 123
        if (bytecnt != qty * 2) return 0;
        if (req_len < (u32)(7 + bytecnt + 2)) return 0;

        const u8 *pdata = &req[7];
        for (u16 i=0;i<qty;i++){
            u16 v = u16be(&pdata[i*2]);
            write_reg(reg+i, v);
        }

        // 鍥炲寘锛歛ddr func reg qty crc
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

    // 涓嶆敮鎸侊細寮傚父鍝嶅簲锛堝彲閫夛級
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
