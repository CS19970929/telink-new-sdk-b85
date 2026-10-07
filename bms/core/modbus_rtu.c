/*
 * 文件功能：公共 Modbus RTU 解析与寄存器映射；UART/BLE 共用 CRC、读写校验及配置事务
 * 路径。
 * bms/core/modbus_rtu.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_debug_log.h"
#include "bms_diag.h"
#include "bms_product.h"
#include "bms_afe_backend.h"
#include "modbus_rtu.h"
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"
#include "bms_afe.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_afe_hw_profile.h"
#include "bms_afe_hw_access.h"
#include "bms_parameter_access.h"
#include "bms_config_store.h"
#include "bms_afe_hw_modbus.h"
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "dvc1124.h"
#else
#include "sh3673510_project_config.h"
#include "sh3673510_control.h"
#endif
#include "bms_parameters.h"
#include "bms_soc.h"
#include "bms_event_log.h"
#include "app.h"
#include "bms_afe_backend.h"
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "dvc1124_config_service.h"
#endif

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
#define BMS_AFE_ACTUAL_REG_BASE  0x2180u
#define BMS_AFE_ACTUAL_REG_COUNT 11u

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

/* 从大端字节序读取 16 位无符号值。 */
static u16 u16be(const u8 *p);
/* 把 ASCII 字符串的指定字节编码为协议寄存器。 */
static u16 read_ascii_string_reg(const u8 *str, u16 max_len, u16 reg_offset);
/* 读取生产信息与产品身份字段。 */
static u16 read_production_info_reg(u16 reg);
/* 读取当前电池测量和运行状态字段。 */
static u16 read_realtime_status_reg(u16 reg);
/* 把有符号电流转换为既有协议编码。 */
static u16 encode_signed_current_reg(void);
/* 按地址分派读取一个协议寄存器。 */
static u16 read_reg(u16 reg);
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
/* 读取 AFE 实际配置的协议诊断字段。 */
static u16 read_afe_actual_reg(u16 reg);
#endif
/* 校验地址及取值后写入一个协议寄存器。 */
static u8 write_reg(u16 reg, u16 val);

static bms_product_info_t s_product_info;

/* 判断地址是否属于请求硬件保护配置窗口。 */
static int afe_hw_profile_is_requested_reg(u16 reg)
{
    return (reg >= BMS_AFE_HW_REQUESTED_REG_BASE &&
            reg < (u16)(BMS_AFE_HW_REQUESTED_REG_BASE + BMS_AFE_HW_REQUESTED_REG_COUNT));
}

/* 判断地址是否属于实际硬件保护值窗口。 */
static int afe_hw_profile_is_effective_reg(u16 reg)
{
    return (reg >= BMS_AFE_HW_EFFECTIVE_REG_BASE &&
            reg < (u16)(BMS_AFE_HW_EFFECTIVE_REG_BASE + BMS_AFE_HW_EFFECTIVE_REG_COUNT));
}

/* 判断地址是否属于 AFE 硬件保护协议窗口。 */
static int afe_hw_profile_is_reg(u16 reg)
{
    return afe_hw_profile_is_requested_reg(reg) || afe_hw_profile_is_effective_reg(reg);
}

/* 读取硬件保护窗口中的一个协议寄存器。 */
static u16 afe_hw_profile_read_reg(u16 reg)
{
    bms_afe_hw_profile_t p;
    u16 offset;

    if (afe_hw_profile_is_effective_reg(reg))
    {
        offset = (u16)(reg - BMS_AFE_HW_EFFECTIVE_REG_BASE);
        if (offset >= BMS_AFE_HW_EFFECTIVE_REG_COUNT ||
            !bms_afe_hw_profile_get_effective(&p)) return 0xFFFFu;
        return ((const u16 *)&p)[offset];
    }

    if (!afe_hw_profile_is_requested_reg(reg)) return 0xFFFFu;
    offset = (u16)(reg - BMS_AFE_HW_REQUESTED_REG_BASE);
    if (offset < BMS_AFE_HW_PROFILE_WORD_COUNT)
    {
        if (!bms_afe_hw_profile_get(&p)) return 0xFFFFu;
        return ((const u16 *)&p)[offset];
    }

    switch (reg)
    {
    case BMS_AFE_HW_META_CAPABILITIES:      return bms_afe_hw_profile_capabilities();
    case BMS_AFE_HW_META_VALID:             return bms_afe_hw_profile_get(&p) ? 1u : 0u;
    case BMS_AFE_HW_META_CELL_COUNT:        return BMS_PRODUCT_CELL_COUNT;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    case BMS_AFE_HW_META_SHUNT_UOHM:        return DVC1124_DEFAULT_SHUNT_UOHM;
    case BMS_AFE_HW_META_WDT_SECONDS:       return DVC1124_I2C_WATCHDOG_SECONDS;
#else
    case BMS_AFE_HW_META_SHUNT_UOHM:        return SH3673510_BOARD_SHUNT_UOHM;
    case BMS_AFE_HW_META_WDT_SECONDS:       return SH3673510_BOARD_WDT_EN ? 32u : 0u;
#endif
    case BMS_AFE_HW_META_ACCESS_ACTIVE:     return bms_afe_hw_access_is_active() ? 1u : 0u;
    case BMS_AFE_HW_META_APPLY_STATE:       return bms_afe_hw_profile_apply_state();
    case BMS_AFE_HW_META_LAST_ERROR:        return bms_afe_hw_profile_last_error();
    case BMS_AFE_HW_META_INTERFACE_VERSION: return BMS_AFE_HW_INTERFACE_VERSION;
    default: return 0xFFFFu;
    }
}

/*
 * 完整 profile 校验、持久化、应用与回读事务；失败按原路径 rollback，
 * 不与软件保护参数联动。
 */
static u8 afe_hw_profile_write_block(const u8 *pdata, u16 qty)
{
    switch (bms_afe_hw_profile_commit_be(pdata, qty))
    {
    case BMS_AFE_HW_ERROR_NONE:       return 0u;
    case BMS_AFE_HW_ERROR_AUTH:       return MB_EX_ILLEGAL_ADDRESS;
    case BMS_AFE_HW_ERROR_VALIDATION: return MB_EX_ILLEGAL_VALUE;
    default:                        return MB_EX_DEVICE_FAILURE;
    }
}

/* 分片传输只能提交完整 AFE 0x10 帧，不能分派任意 Modbus 命令或部分应用配置。 */
u8 bms_afe_hw_write_complete_frame(const u8 *frame, u32 length)
{
    if (frame == 0 || length != 79u || frame[0] != 1u || frame[1] != 0x10u ||
        u16be(&frame[2]) != BMS_AFE_HW_REQUESTED_REG_BASE ||
        u16be(&frame[4]) != BMS_AFE_HW_PROFILE_WORD_COUNT || frame[6] != 70u ||
        mb_crc16(frame, 77u) != (u16)((u16)frame[77] | ((u16)frame[78] << 8)))
        return MB_EX_ILLEGAL_VALUE;
    return afe_hw_profile_write_block(&frame[7], BMS_AFE_HW_PROFILE_WORD_COUNT);
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
/* 判断地址是否属于 DVC 语义配置窗口。 */
static int dvc_comm_is_semantic(u16 reg)
{
    return (reg >= DVC1124_COMM_REG_BASE &&
            reg < (u16)(DVC1124_COMM_REG_BASE + DVC1124_COMM_REG_COUNT));
}

/* 判断地址是否属于 DVC 原始寄存器窗口。 */
static int dvc_comm_is_raw(u16 reg)
{
    return (reg >= DVC1124_RAW_REG_BASE &&
            reg < (u16)(DVC1124_RAW_REG_BASE + DVC1124_RAW_REG_COUNT));
}

/* 读取允许的 DVC 语义或原始配置字段。 */
static u16 dvc_comm_read(u16 reg)
{
    u32 value;
#if BMS_DVC_RAW_DIAG_ENABLE
    u8 raw;
#endif
    dvc1124_config_result_t result;

    if (dvc_comm_is_semantic(reg))
    {
        result = DVC1124_ConfigServiceRead(
            (dvc1124_config_field_t)(reg - DVC1124_COMM_REG_BASE),
            &value);
        if (result != DVC1124_CFG_OK || value > 0xFFFFu) return 0xFFFFu;
        return (u16)value;
    }

#if BMS_DVC_RAW_DIAG_ENABLE
    if (dvc_comm_is_raw(reg))
    {
        result = DVC1124_ConfigServiceReadRaw(
            (u8)(reg - DVC1124_RAW_REG_BASE),
            &raw);
        return (result == DVC1124_CFG_OK) ? raw : 0xFFFFu;
    }

#endif

    return 0xFFFFu;
}

/* 将 DVC 操作结果映射为 Modbus 异常码。 */
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
    case DVC1124_CFG_ERR_INCONSISTENT:
    default:
        return MB_EX_DEVICE_FAILURE;
    }
}

/* 校验 DVC 地址和写权限后更新配置字段。 */
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

#if BMS_DVC_RAW_DIAG_ENABLE
    if (dvc_comm_is_raw(reg))
    {
        if (val > 0xFFu) return MB_EX_ILLEGAL_VALUE;
        result = DVC1124_ConfigServiceWriteRaw(
            (u8)(reg - DVC1124_RAW_REG_BASE),
            (u8)val);
        return dvc_result_to_modbus_exception(result);
    }

#endif

    return MB_EX_ILLEGAL_ADDRESS;
}
#endif

/* 读取协议地址对应的历史故障编号。 */
static u16 read_fault_history_reg(u16 reg)
{
    u16 offset;
    uint8_t age;
    bms_fault_level_t level;

    if (reg < 0xD103u || reg > 0xD108u) return 0u;

    offset = (u16)(reg - 0xD103u);
    level = (bms_fault_level_t)(BMS_FAULT_LEVEL_FIRST + (offset / 2u));
    age = (uint8_t)((offset % 2u) * 2u);
    return (u16)(((u16)bms_fault_history_recent(level, age) << 8) |
                 bms_fault_history_recent(level, (uint8_t)(age + 1u)));
}

/* 读取公共错误状态对应的协议字段。 */
static u16 read_error_status_reg(u16 reg)
{
    uint8_t first = (uint8_t)(2u * (reg - 0xD109u));

    return (u16)(((u16)bms_error_get((bms_error_id_t)first) << 8) |
                 bms_error_get((bms_error_id_t)(first + 1u)));
}

/* 构造 Modbus 异常响应及 CRC。 */
static int modbus_exception(u8 addr,
                            u8 func,
                            u8 exception,
                            u8 *rsp,
                            u32 *rsp_len)
{
    u16 crc;

    BMS_LOG(BMS_LOG_WARN, BMS_LOG_COMM, BMS_LOG_MODBUS_EXCEPTION, func, exception);
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

/* 检查读请求地址是否属于已实现的窗口。 */
static int read_address_supported(u16 r)
{
    return bms_parameter_readable(r) || r<3u ||
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
           (r>=BMS_AFE_ACTUAL_REG_BASE && r<BMS_AFE_ACTUAL_REG_BASE+BMS_AFE_ACTUAL_REG_COUNT) ||
#endif
           (r>=BTNAME_REG_BASE && r<BTNAME_REG_BASE+BTNAME_REG_COUNT) ||
           (r>=0xC002u && r<0xC032u) || (r>=0xD000u && r<=0xD03Eu) ||
           (r>=0x2100u && r<=0x2140u) || (r>=0xD100u && r<=0xD116u) ||
           (r>=BMS_REALTIME_REG_BASE && r<BMS_REALTIME_REG_BASE+BMS_REALTIME_REG_COUNT) ||
           afe_hw_profile_is_reg(r)
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
           || dvc_comm_is_semantic(r) || dvc_comm_is_raw(r)
#endif
           ;
}

/* 按地址分派读取一个协议寄存器。 */
static u16 read_reg(u16 reg)
{
    if (bms_parameter_readable(reg)) return bms_parameter_read(reg);
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    if (reg >= BMS_AFE_ACTUAL_REG_BASE && reg < BMS_AFE_ACTUAL_REG_BASE+BMS_AFE_ACTUAL_REG_COUNT)
        return read_afe_actual_reg(reg);
#endif

    if (afe_hw_profile_is_reg(reg)) return afe_hw_profile_read_reg(reg);

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if (dvc_comm_is_semantic(reg) || dvc_comm_is_raw(reg))
        return dvc_comm_read(reg);
#endif

    if (reg < 3u)
    {
        switch (reg)
        {
        case 0:
            return (u16)((g_bms_report.mac_public[0] << 8) |
                         g_bms_report.mac_public[1]);
        case 1:
            return (u16)((g_bms_report.mac_public[2] << 8) |
                         g_bms_report.mac_public[3]);
        case 2:
            return (u16)((g_bms_report.mac_public[4] << 8) |
                         g_bms_report.mac_public[5]);
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
    {
        u16 value;
        /* 此协议窗口跨多个报告字段，不仅是单体数组。 */
        memcpy(&value, (const u8 *)&g_bms_report + (reg-0xD000u)*2u, sizeof(value));
        return value;
    }

    if (reg >= 0x2100u && reg <= 0x2140u)
    {
        u16 value;
        memcpy(&value, (const u8 *)&g_bms_protection_params + (reg-0x2100u)*2u, sizeof(value));
        return value;
    }

    if (reg >= 0xD100u && reg <= 0xD114u)
    {
        if (reg <= 0xD108u) return read_fault_history_reg(reg);
        return read_error_status_reg(reg);
    }

    if (reg >= 0xD115u && reg <= 0xD118u)
    {
        if (reg == 0xD115u) return (u16)(g_bms_system_status.all & 0x0000FFFFu);
        if (reg == 0xD116u) return (u16)(g_bms_system_status.all >> 16);
    }

    if (reg >= BMS_REALTIME_REG_BASE &&
        reg < (BMS_REALTIME_REG_BASE + BMS_REALTIME_REG_COUNT))
        return read_realtime_status_reg(reg);

    return 0u;
}

extern bool deepsleep_en;

/* 判断写入地址是否需要保存业务参数。 */
static int reg_requires_param_save(u16 reg)
{
    /* 固定 DVC 语义诊断为只读。 */
    return (reg >= 0x2100u && reg <= 0x2140u);
}

/* 校验地址及取值后写入一个协议寄存器。 */
static u8 write_reg(u16 reg, u16 val)
{
    if (afe_hw_profile_is_reg(reg)) return MB_EX_ILLEGAL_ADDRESS;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if (dvc_comm_is_semantic(reg) || dvc_comm_is_raw(reg))
        return dvc_comm_write(reg, val);
#endif

    if (reg==BMS_PARAM_REG_SOC || reg==BMS_PARAM_REG_CAPACITY || reg==BMS_PARAM_REG_CYCLE || (reg>=BMS_PARAM_REG_MAGIC && reg<0x2F00u)) {
        u8 bytes[2]={(u8)(val>>8),(u8)val};
        return bms_parameter_write(reg,1u,bytes);
    }
    if (reg==0x1102u && val==0x0Au) { deepsleep_en=true; return 0u; }
    return MB_EX_ILLEGAL_ADDRESS;
}

/* 校验并提交软件保护参数，失败保留原状态。 */
static u8 commit_protection_update(const bms_protection_params_t *candidate)
{
    if (!bms_sw_protection_validate_params(candidate)) return MB_EX_ILLEGAL_VALUE;
    return bms_protection_params_commit(candidate) ? 0u : MB_EX_DEVICE_FAILURE;
}

/* 从大端字节序读取 16 位无符号值。 */
static u16 u16be(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

/* 将 16 位值按大端字节序写入缓冲区。 */
static void put_u16be(u8 *p, u16 v)
{
    p[0] = (u8)(v >> 8);
    p[1] = (u8)(v & 0xFFu);
}

/*
 * UART/BLE 的共同协议入口；先检查长度、地址、CRC，再分派请求，
 * 返回值表示是否生成响应。
 */
int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len)
{
    u16 crc_rx;
    u16 crc;
    u8 addr;
    u8 func;

    if (req == NULL || rsp == NULL || rsp_len == NULL) return 0;
    *rsp_len = 0u;

    if (req_len < 4u || req_len > MODBUS_RTU_FRAME_CAPACITY) return 0;
    if (req[0] != MB_ADDR && req[0] != 0x00u) return 0;

    crc_rx = (u16)(((u16)req[req_len - 1u] << 8) | req[req_len - 2u]);
    crc = mb_crc16(req, req_len - 2u);
    if (crc != crc_rx) {
        BMS_LOG(BMS_LOG_WARN, BMS_LOG_COMM, BMS_LOG_CRC_REJECT, req_len, ((uint32_t)crc << 16) | crc_rx);
        return 0;
    }

    addr = req[0];
    func = req[1];

    if (func == BMS_AFE_HW_ACCESS_MODBUS_FUNC)
    {
        if (addr == 0x00u) return 0;
        return bms_afe_hw_access_modbus_on_frame(req, req_len, rsp, rsp_len);
    }

    /* 保留既有量产工具使用的调试回显。 */
    if (func == 0x7Fu && addr != 0x00u)
    {
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

        if (req_len != 8u) return 0;
        reg = u16be(&req[2]);
        qty = u16be(&req[4]);
        if (qty == 0u || qty > 0x7Du)
            return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);

        if ((u32)reg + qty > 65536u)
            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
        if (bms_debug_log_overlaps(reg, qty)) {
            if (!bms_debug_log_read(reg, qty, &rsp[3]))
                return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
        } else if (bms_diag_overlaps(reg, qty)) {
            if (!bms_diag_read(reg, qty, &rsp[3]))
                return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
        } else if (reg == BMS_EVENT_LOG_REG_BASE && qty <= BMS_EVENT_LOG_REG_COUNT) {
            for (i = 0u; i < qty; ++i)
                put_u16be(&rsp[3u + (u32)i * 2u], bms_event_log_read_reg(i));
        } else {
            /* 先校验整个范围；非法尾地址不能造成部分读取。 */
            for (i = 0u; i < qty; ++i)
                if (!read_address_supported((u16)(reg + i)))
                    return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
            for (i = 0u; i < qty; ++i)
                put_u16be(&rsp[3u + (u32)i * 2u], read_reg((u16)(reg + i)));
        }
        bytes = (u32)qty * 2u;
        rsp[0] = addr;
        rsp[1] = func;
        rsp[2] = (u8)bytes;

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
        bms_protection_params_t candidate;
        int protect_changed;

        if (req_len != 8u) return 0;
        reg = u16be(&req[2]);
        val = u16be(&req[4]);
        if (bms_diag_overlaps(reg, 1u) || bms_debug_log_overlaps(reg, 1u))
            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
        protect_changed = reg_requires_param_save(reg);
        if (protect_changed) {
            candidate = g_bms_protection_params;
            memcpy((u8 *)&candidate + (reg - 0x2100u) * 2u, &val, sizeof(val));
            exception = commit_protection_update(&candidate);
        } else {
            exception = write_reg(reg, val);
        }
        if (exception != 0u)
            return modbus_exception(addr, func, exception, rsp, rsp_len);

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
        u8 exception;
        bms_protection_params_t candidate;

        if (req_len < 9u) return 0;
        reg = u16be(&req[2]);
        qty = u16be(&req[4]);
        if ((u32)reg + qty > 65536u || bms_diag_overlaps(reg, qty) || bms_debug_log_overlaps(reg, qty))
            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
        bytecnt = req[6];

        if (qty == 0u || qty > 0x7Bu)
            return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);
        if (bytecnt != (u8)(qty * 2u))
            return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);
        if (req_len != (u32)(7u + bytecnt + 2u)) return 0;

        pdata = &req[7];
        if (reg == BMS_AFE_HW_REQUESTED_REG_BASE) {
            if (addr == 0x00u) return 0;
            if (qty != BMS_AFE_HW_PROFILE_WORD_COUNT)
                return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);
            exception = afe_hw_profile_write_block(pdata, qty);
            if (exception != 0u)
                return modbus_exception(addr, func, exception, rsp, rsp_len);
            rsp[0] = addr; rsp[1] = func; put_u16be(&rsp[2], reg); put_u16be(&rsp[4], qty);
            crc = mb_crc16(rsp, 6u); rsp[6] = (u8)(crc & 0xFFu); rsp[7] = (u8)(crc >> 8); *rsp_len = 8u;
            return 1;
        }
        if (afe_hw_profile_is_reg(reg) || afe_hw_profile_is_reg((u16)(reg + qty - 1u)))
            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);

        /* 预检全部范围；单帧只能修改一个状态所有者，执行副作用前拒绝跨界/未知写入。 */
        if (reg>=BMS_PARAM_REG_MAGIC && reg<0x2F00u) {
            exception=bms_parameter_write(reg,qty,pdata);
        } else if (reg==BTNAME_REG_BASE && qty<=BTNAME_REG_WORDS) {
            exception=btname_modbus_on_write_holding(reg,qty,(const uint16_t *)pdata) ? 0u : MB_EX_DEVICE_FAILURE;
        } else if (reg>=0x2100u && (u32)reg+qty<=0x2141u) {
            candidate=g_bms_protection_params;
            for (i=0u;i<qty;++i) {
                u16 value=u16be(&pdata[i*2u]);
                memcpy((u8 *)&candidate+(reg-0x2100u+i)*2u,&value,sizeof(value));
            }
            exception=commit_protection_update(&candidate);
        } else if (qty==1u) {
            exception=write_reg(reg,u16be(pdata));
        } else exception=MB_EX_ILLEGAL_ADDRESS;
        if (exception) return modbus_exception(addr,func,exception,rsp,rsp_len);

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

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
/* 读取 AFE 实际配置的协议诊断字段。 */
static u16 read_afe_actual_reg(u16 reg)
{
    sh3673510_protection_actual_t a;
    u16 offset=(u16)(reg-BMS_AFE_ACTUAL_REG_BASE);
    u8 valid=sh3673510_control_get_protection_actual(&a);
    if (!offset) return valid ? 1u : 0u;
    if (!valid) return 0xFFFFu;
    switch (offset) {
    case 1u: return a.ov_mv;
    case 2u: return a.uv_mv;
    case 3u: return a.ocd1_a10;
    case 4u: return a.ocd2_a10;
    case 5u: return a.occ_a10;
    case 6u: return a.ov_delay_ms;
    case 7u: return a.uv_delay_ms;
    case 8u: return a.ocd1_delay_ms;
    case 9u: return a.ocd2_delay_ms;
    case 10u: return a.occ_delay_ms;
    default: return 0xFFFFu;
    }
}
#endif

/* 把有符号电流转换为既有协议编码。 */
static u16 encode_signed_current_reg(void)
{
    int16_t signed_current = 0;

    if (g_bms_report.u16IDischg)
        signed_current = (int16_t)(-((int16_t)g_bms_report.u16IDischg));
    else if (g_bms_report.u16Ichg)
        signed_current = (int16_t)g_bms_report.u16Ichg;

    return (u16)signed_current;
}

/* 读取当前电池测量和运行状态字段。 */
static u16 read_realtime_status_reg(u16 reg)
{
    switch (reg)
    {
    case BMS_REALTIME_REG_MAGIC_ADDR:       return BMS_REALTIME_REG_MAGIC;
    case BMS_REALTIME_REG_VERSION_ADDR:     return BMS_REALTIME_REG_VERSION;
    case BMS_REALTIME_REG_VOLTAGE_ADDR:     return g_bms_report.u16VCellTotle;
    case BMS_REALTIME_REG_CURRENT_ADDR:     return encode_signed_current_reg();
    case BMS_REALTIME_REG_SOC_ADDR:         return g_bms_report.SocElement.u16Soc;
    case BMS_REALTIME_REG_TEMP_MAX_ADDR:    return g_bms_report.u16TempMax;
    case BMS_REALTIME_REG_TEMP_MIN_ADDR:    return g_bms_report.u16TempMin;
    case BMS_REALTIME_REG_TEMP_MOS_ADDR:    return g_bms_report.u16Temperature[MOS_TEMP1];
    case BMS_REALTIME_REG_VCELL_MAX_ADDR:   return g_bms_report.u16VCellMax;
    case BMS_REALTIME_REG_VCELL_MIN_ADDR:   return g_bms_report.u16VCellMin;
    case BMS_REALTIME_REG_VCELL_DELTA_ADDR: return g_bms_report.u16VCellDelta;
    default: return 0u;
    }
}

/* 把 ASCII 字符串的指定字节编码为协议寄存器。 */
static u16 read_ascii_string_reg(const u8 *str, u16 max_len, u16 reg_offset)
{
    u16 str_idx = reg_offset * 2u;
    u8 high_byte = 0x00u;
    u8 low_byte = 0x00u;

    if (str_idx < max_len && str[str_idx] != '\0') high_byte = str[str_idx];
    if ((str_idx + 1u) < max_len && str[str_idx + 1u] != '\0') low_byte = str[str_idx + 1u];

    return (u16)(((u16)high_byte << 8) | low_byte);
}

/* 读取生产信息与产品身份字段。 */
static u16 read_production_info_reg(u16 reg)
{
    if (reg >= PROD_SN_REG_BASE && reg < (PROD_SN_REG_BASE + PROD_SN_REG_COUNT))
        return read_ascii_string_reg(s_product_info.BMS_SerialNumber,
                                     PRODUCT_ID_LENGTH_MAX,
                                     (u16)(reg - PROD_SN_REG_BASE));

    if (reg >= PROD_HW_VER_REG_BASE && reg < (PROD_HW_VER_REG_BASE + PROD_HW_VER_REG_COUNT))
        return read_ascii_string_reg(s_product_info.BMS_HardWareVersion,
                                     PRODUCT_ID_LENGTH_MAX,
                                     (u16)(reg - PROD_HW_VER_REG_BASE));

    if (reg >= PROD_SW_VER_REG_BASE && reg < (PROD_SW_VER_REG_BASE + PROD_SW_VER_REG_COUNT))
        return read_ascii_string_reg(s_product_info.BMS_SoftWareVersion,
                                     PRODUCT_ID_LENGTH_MAX,
                                     (u16)(reg - PROD_SW_VER_REG_BASE));

    return 0u;
}

/* 从 Config SN 和编译身份刷新协议 RAM 缓存，不写 Flash。 */
void bms_product_info_refresh(void)
{
    bms_user_params_t user;
    uint8_t hardwareCount = sizeof(BMS_PRODUCT_HW_VERSION) > PRODUCT_ID_LENGTH_MAX
                              ? PRODUCT_ID_LENGTH_MAX
                              : sizeof(BMS_PRODUCT_HW_VERSION);
    uint8_t softwareCount = sizeof(BMS_PRODUCT_SW_VERSION) > PRODUCT_ID_LENGTH_MAX
                              ? PRODUCT_ID_LENGTH_MAX
                              : sizeof(BMS_PRODUCT_SW_VERSION);
    uint8_t serialNumberCount = sizeof(BMS_PRODUCT_DEFAULT_SERIAL) > PRODUCT_ID_LENGTH_MAX
                                  ? PRODUCT_ID_LENGTH_MAX
                                  : sizeof(BMS_PRODUCT_DEFAULT_SERIAL);

    memset(&s_product_info, 0, sizeof(bms_product_info_t));
    memcpy(&s_product_info.BMS_HardWareVersion[0], BMS_PRODUCT_HW_VERSION, hardwareCount);
    memcpy(&s_product_info.BMS_SoftWareVersion[0], BMS_PRODUCT_SW_VERSION, softwareCount);
    memcpy(&s_product_info.BMS_SerialNumber[0], BMS_PRODUCT_DEFAULT_SERIAL, serialNumberCount);
    if (bms_config_get_user(&user) && user.serial[0])
        memcpy(s_product_info.BMS_SerialNumber,user.serial,sizeof(user.serial));
}

/* 按类别恢复软件业务参数并提交存储。 */
u8 bms_reset_software_parameters(void)
{
    bms_protection_params_t defaults;
    bms_config_store_get_default_protect(&defaults);
    return commit_protection_update(&defaults);
}
/* 恢复独立 AFE 硬件保护默认参数并应用。 */
u8 bms_reset_afe_parameters(void)
{
    bms_afe_hw_profile_t defaults;
    u8 payload[70];
    u16 i, value;
    bms_afe_hw_profile_build_default(&defaults);
    for (i=0u;i<BMS_AFE_HW_PROFILE_WORD_COUNT;++i) {
        memcpy(&value, (const u8 *)&defaults+i*2u, sizeof(value));
        put_u16be(payload+i*2u,value);
    }
    return afe_hw_profile_write_block(payload,BMS_AFE_HW_PROFILE_WORD_COUNT);
}
