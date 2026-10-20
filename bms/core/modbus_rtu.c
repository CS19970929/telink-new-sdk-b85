/*
 * 文件功能：公共 Modbus RTU 解析与寄存器映射；UART/BLE 共用 CRC、读写校验及配置事务
 * 路径。
 * bms/core/modbus_rtu.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_debug_log.h"
#include "bms_diag.h"
#include "bms_sleep_status.h"
#include "bms_product.h"
#include "bms_afe_backend.h"
#include "modbus_rtu.h"
#include "bms_afe.h"
#include "bms_error.h"
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_afe_hw_profile.h"
#include "bms_afe_hw_access.h"
#include "bms_afe_registers.h"
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
#include "app_power.h"
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
#include "dvc1124_config_service.h"
#endif

#include "btname_modbus.h"
#include <string.h>

#define MB_ADDR 0x01

#define MB_EX_ILLEGAL_FUNCTION 0x01u
#define MB_EX_ILLEGAL_ADDRESS  0x02u
#define MB_EX_ILLEGAL_VALUE    0x03u
#define MB_EX_DEVICE_FAILURE   0x04u

#define BMS_REALTIME_REG_BASE 0xD120u
#define BMS_REALTIME_REG_COUNT 11u
#define BMS_REALTIME_REG_MAGIC 0x4253u
#define BMS_REALTIME_REG_VERSION 0x0001u
/* 独立 RAM MOS 许可窗口；不复用休眠命令，不改变持久化布局。 */
#define BMS_MOS_CONTROL_BASE 0x2F10u
#define BMS_MOS_CONTROL_COUNT 8u
#define BMS_MOS_CONTROL_MAGIC 0x4D43u
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
/* 按地址分派读取一个协议寄存器；返回 0 表示该地址不属于任何只读窗口。 */
static int read_reg(u16 reg, u16 *value);
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
/* 读取 AFE 实际配置的协议诊断字段。 */
static u16 read_afe_actual_reg(u16 offset, const sh3673510_protection_actual_t *actual, u8 valid);
#endif
/* 校验地址及取值后写入一个协议寄存器。 */
static u8 write_reg(u16 reg, u16 val);

static u8 s_serial_number[PRODUCT_ID_LENGTH_MAX];

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

/* 编码本请求的硬件保护配置和元数据，不重复获取配置。 */
static u16 afe_hw_profile_read_reg(u16 reg, const bms_afe_hw_profile_t *profile, u8 valid)
{
    u16 offset = (u16)(reg - BMS_AFE_HW_REQUESTED_REG_BASE);
    if (offset < BMS_AFE_HW_PROFILE_WORD_COUNT)
        return valid ? ((const u16 *)profile)[offset] : 0xFFFFu;

    if(reg>=BMS_AFE_HW_DIAGNOSTIC_BASE && reg<BMS_AFE_HW_DIAGNOSTIC_BASE+BMS_AFE_HW_DIAGNOSTIC_COUNT)
        return bms_afe_hw_profile_diagnostic_word((u16)(reg-BMS_AFE_HW_DIAGNOSTIC_BASE));
    switch (reg)
    {
    case BMS_AFE_HW_META_CAPABILITIES:      return bms_afe_hw_profile_capabilities();
    case BMS_AFE_HW_META_VALID:             return valid ? 1u : 0u;
    case BMS_AFE_HW_META_CELL_COUNT:        return BMS_PRODUCT_CELL_COUNT;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    case BMS_AFE_HW_META_FEATURES:          return BMS_AFE_HW_FEATURE_DIAGNOSTIC;
    case BMS_AFE_HW_META_SHUNT_UOHM:        return DVC1124_DEFAULT_SHUNT_UOHM;
    case BMS_AFE_HW_META_SHUNT_ELEMENT_UOHM: return DVC1124_DEFAULT_SHUNT_UOHM;
    case BMS_AFE_HW_META_SHUNT_PARALLEL_COUNT: return 1u;
    case BMS_AFE_HW_META_WDT_SECONDS:       return DVC1124_I2C_WATCHDOG_SECONDS;
#else
    case BMS_AFE_HW_META_SHUNT_UOHM:        return SH3673510_BOARD_SHUNT_UOHM;
    case BMS_AFE_HW_META_FEATURES:          return BMS_AFE_HW_FEATURE_TEMP_EFFECTIVE | BMS_AFE_HW_FEATURE_DIAGNOSTIC;
    case BMS_AFE_HW_META_SHUNT_ELEMENT_UOHM: return SH3673510_BOARD_SHUNT_ELEMENT_UOHM;
    case BMS_AFE_HW_META_SHUNT_PARALLEL_COUNT: return SH3673510_BOARD_SHUNT_PARALLEL_COUNT;
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
    if (bms_afe_register_debug_held()) return MB_EX_DEVICE_FAILURE;
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

/* 按地址分派读取一个协议寄存器；返回 0 表示该地址不属于任何只读窗口。 */
static int read_reg(u16 reg, u16 *value)
{
    if (reg >= BMS_MOS_CONTROL_BASE && reg < BMS_MOS_CONTROL_BASE+BMS_MOS_CONTROL_COUNT) {
        uint8_t c, d;
        uint8_t driver = 0u;
        uint8_t driver_valid = bms_afe_get_fet_status(&driver);
        uint16_t guard = bms_afe_get_guard_diagnostic_bits();
        bms_afe_get_requested_fets(&c, &d);
        switch (reg-BMS_MOS_CONTROL_BASE) {
        case 0: *value = BMS_MOS_CONTROL_MAGIC; break;
        case 1: *value = 1u; break;
        case 2: *value = bms_afe_get_remote_allowed_mask() & 1u; break;
        case 3: *value = (bms_afe_get_remote_allowed_mask() >> 1) & 1u; break;
        case 4: *value = driver; break;
        case 5: *value = (driver_valid &&
            (guard & DIAG_GUARD_SAMPLES_QUALIFIED)) ? 1u : 0u; break;
        case 6: *value = guard; break;
        case 7: *value = (uint16_t)(c | (d << 1)); break;
        default: *value = 0u; break;
        }
        return 1;
    }
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if (dvc_comm_is_semantic(reg) || dvc_comm_is_raw(reg)) {
        *value = dvc_comm_read(reg);
        return 1;
    }
#endif

    if (reg < 3u)
    {
        switch (reg)
        {
        case 0:
            *value = (u16)((g_bms_report.mac_public[0] << 8) |
                           g_bms_report.mac_public[1]);
            break;
        case 1:
            *value = (u16)((g_bms_report.mac_public[2] << 8) |
                           g_bms_report.mac_public[3]);
            break;
        case 2:
            *value = (u16)((g_bms_report.mac_public[4] << 8) |
                           g_bms_report.mac_public[5]);
            break;
        default:
            *value = 0u;
            break;
        }
        return 1;
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
        *value = (u16)(((u16)high_byte << 8) | low_byte);
        return 1;
    }

    if (reg >= 0xC002u && reg < 0xC032u) {
        *value = read_production_info_reg(reg);
        return 1;
    }

    if (reg >= 0xD000u && reg <= 0xD03Eu)
    {
        /* 此协议窗口跨多个报告字段，不仅是单体数组。 */
        memcpy(value, (const u8 *)&g_bms_report + (reg-0xD000u)*2u, sizeof(*value));
        return 1;
    }

    if (reg >= 0x2100u && reg <= 0x2140u)
    {
        memcpy(value, (const u8 *)&g_bms_protection_params + (reg-0x2100u)*2u, sizeof(*value));
        return 1;
    }

    if (reg >= 0xD100u && reg <= 0xD108u) {
        *value = read_fault_history_reg(reg);
        return 1;
    }
    if (reg >= 0xD109u && reg <= 0xD114u) {
        *value = read_error_status_reg(reg);
        return 1;
    }
    if (reg == 0xD115u) {
        *value = (u16)(g_bms_system_status.all & 0x0000FFFFu);
        return 1;
    }
    if (reg == 0xD116u) {
        *value = (u16)(g_bms_system_status.all >> 16);
        return 1;
    }

    if (reg >= BMS_REALTIME_REG_BASE &&
        reg < (BMS_REALTIME_REG_BASE + BMS_REALTIME_REG_COUNT)) {
        *value = read_realtime_status_reg(reg);
        return 1;
    }

    return 0;
}

/* 校验地址及取值后写入一个协议寄存器。 */
static u8 write_reg(u16 reg, u16 val)
{
    if (reg == BMS_MOS_CONTROL_BASE+2u || reg == BMS_MOS_CONTROL_BASE+3u) {
        if (val > 1u) return MB_EX_ILLEGAL_VALUE;
        return bms_afe_set_channel_allowed((uint8_t)(reg-BMS_MOS_CONTROL_BASE-2u), (uint8_t)val)
            ? 0u : MB_EX_DEVICE_FAILURE;
    }
    if (afe_hw_profile_is_reg(reg)) return MB_EX_ILLEGAL_ADDRESS;
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    if (dvc_comm_is_semantic(reg) || dvc_comm_is_raw(reg))
        return dvc_comm_write(reg, val);
#endif

    if (reg==BMS_PARAM_REG_SOC || reg==BMS_PARAM_REG_CAPACITY || reg==BMS_PARAM_REG_CYCLE ||
        (reg>=BMS_PARAM_REG_MAGIC && reg<0x2F00u) ||
        (reg>=BMS_PARAM_REG_SOC_PROFILE_STAGE_BASE && reg<BMS_PARAM_REG_SOC_PROFILE_STAGE_END)) {
        u8 bytes[2]={(u8)(val>>8),(u8)val};
        return bms_parameter_write(reg,1u,bytes);
    }
    if (reg==0x1102u && val==0x0Au) { deepsleep_en=1u; return 0u; }
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

/* 统一追加正常响应 CRC；广播仍完成既有处理但不发送响应。 */
static int modbus_finish_response(u8 *rsp, u32 payload_len, u32 *rsp_len)
{
    u16 crc = mb_crc16(rsp, payload_len);
    rsp[payload_len] = (u8)(crc & 0xFFu);
    rsp[payload_len + 1u] = (u8)(crc >> 8);
    *rsp_len = payload_len + 2u;
    return rsp[0] != 0x00u;
}

/* 0x03：先校验完整地址范围，再编码原有读取窗口。 */
static int modbus_read_holding(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len)
{
    u8 addr = req[0];
    u8 func = req[1];
    u16 reg;
    u16 qty;
    u32 bytes;
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
    } else if (bms_sleep_status_overlaps(reg, qty)) {
        if (!bms_sleep_status_read(reg, qty, &rsp[3]))
            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
    } else if (bms_diag_overlaps(reg, qty)) {
        if (!bms_diag_read(reg, qty, &rsp[3]))
            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
    } else if (reg == BMS_EVENT_LOG_REG_BASE && qty <= BMS_EVENT_LOG_REG_COUNT) {
        for (i = 0u; i < qty; ++i)
            put_u16be(&rsp[3u + (u32)i * 2u], bms_event_log_read_reg(i));
    } else if (afe_hw_profile_is_effective_reg(reg)) {
        bms_afe_hw_profile_t effective;
        u16 offset = (u16)(reg - BMS_AFE_HW_EFFECTIVE_REG_BASE);
        u8 valid;
        if ((u32)offset + qty > BMS_AFE_HW_EFFECTIVE_REG_COUNT)
            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
        /* 一次查询只回读一份配置；失败整段未知，避免重复 AFE I/O 和混合结果。 */
        valid = bms_afe_hw_profile_get_effective(&effective);
        for (i = 0u; i < qty; ++i)
            put_u16be(&rsp[3u + (u32)i * 2u],
                valid ? ((const u16 *)&effective)[offset + i] : 0xFFFFu);
    } else if (afe_hw_profile_is_requested_reg(reg)) {
        bms_afe_hw_profile_t requested;
        u16 offset = (u16)(reg - BMS_AFE_HW_REQUESTED_REG_BASE);
        u8 valid = 0u;
        if ((u32)offset + qty > BMS_AFE_HW_REQUESTED_REG_COUNT)
            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
        /* 仅含固定元数据时无需配置；数据和 VALID 共用一次 RAM 读取/校验。 */
        if (offset < BMS_AFE_HW_PROFILE_WORD_COUNT ||
            (reg <= BMS_AFE_HW_META_VALID && (u32)reg + qty > BMS_AFE_HW_META_VALID))
            valid = bms_afe_hw_profile_get(&requested);
        for (i = 0u; i < qty; ++i)
            put_u16be(&rsp[3u + (u32)i * 2u],
                      afe_hw_profile_read_reg((u16)(reg + i), &requested, valid));
#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
    } else if (reg >= BMS_AFE_ACTUAL_REG_BASE && reg < BMS_AFE_ACTUAL_REG_BASE+BMS_AFE_ACTUAL_REG_COUNT) {
        sh3673510_protection_actual_t actual;
        u16 offset = (u16)(reg - BMS_AFE_ACTUAL_REG_BASE);
        u8 valid;
        if ((u32)offset + qty > BMS_AFE_ACTUAL_REG_COUNT)
            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
        valid = sh3673510_control_get_protection_actual(&actual);
        for (i = 0u; i < qty; ++i)
            put_u16be(&rsp[3u + (u32)i * 2u],
                      read_afe_actual_reg((u16)(offset + i), &actual, valid));
#endif
    } else {
        /* 逐地址读取；任一地址不属于只读窗口即整帧按非法地址拒绝。 */
        i = 0u;
        while (i < qty) {
            u16 value;
            if (bms_parameter_readable((u16)(reg + i))) {
                u16 start = i;
                do { ++i; } while (i < qty && bms_parameter_readable((u16)(reg + i)));
                if (!bms_parameter_read_block((u16)(reg + start), (u16)(i - start),
                                               &rsp[3u + (u32)start * 2u]))
                    return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
            } else if (read_reg((u16)(reg + i), &value)) {
                put_u16be(&rsp[3u + (u32)i * 2u], value);
                ++i;
            } else {
                return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
            }
        }
    }
    bytes = (u32)qty * 2u;
    rsp[0] = addr;
    rsp[1] = func;
    rsp[2] = (u8)bytes;

    return modbus_finish_response(rsp, 3u + bytes, rsp_len);
}

/* 0x06：单寄存器候选提交成功后才回显，广播副作用保持原语义。 */
static int modbus_write_single(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len)
{
    u8 addr = req[0];
    u8 func = req[1];
    u16 reg;
    u16 val;
    u8 exception;
    bms_protection_params_t candidate;

    if (req_len != 8u) return 0;
    reg = u16be(&req[2]);
    val = u16be(&req[4]);
    if (addr == 0u && reg >= BMS_MOS_CONTROL_BASE && reg < BMS_MOS_CONTROL_BASE+BMS_MOS_CONTROL_COUNT)
        return 0; /* 多设备广播不得切换 MOS。 */
    if (bms_sleep_status_overlaps(reg, 1u) || bms_diag_overlaps(reg, 1u) || bms_debug_log_overlaps(reg, 1u))
        return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
    if (reg >= 0x2100u && reg <= 0x2140u) {
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

/* 0x10：整帧校验并只提交一个状态所有者，AFE 仍要求完整授权事务。 */
static int modbus_write_multiple(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len)
{
    u8 addr = req[0];
    u8 func = req[1];
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
    if ((u32)reg + qty > 65536u || bms_sleep_status_overlaps(reg, qty) ||
        bms_diag_overlaps(reg, qty) || bms_debug_log_overlaps(reg, qty))
        return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);
    bytecnt = req[6];

    if (qty == 0u || qty > 0x7Bu)
        return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);
    if (bytecnt != (u8)(qty * 2u))
        return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);
    if (req_len != (u32)(7u + bytecnt + 2u)) return 0;

    if (addr == 0u && reg < BMS_MOS_CONTROL_BASE+BMS_MOS_CONTROL_COUNT &&
        (u32)reg+qty > BMS_MOS_CONTROL_BASE) return 0;
    pdata = &req[7];
    if (reg == BMS_AFE_HW_REQUESTED_REG_BASE) {
        if (addr == 0x00u) return 0;
        if (qty != BMS_AFE_HW_PROFILE_WORD_COUNT)
            return modbus_exception(addr, func, MB_EX_ILLEGAL_VALUE, rsp, rsp_len);
        exception = afe_hw_profile_write_block(pdata, qty);
        if (exception != 0u)
            return modbus_exception(addr, func, exception, rsp, rsp_len);
    } else {
        if (afe_hw_profile_is_reg(reg) || afe_hw_profile_is_reg((u16)(reg + qty - 1u)))
            return modbus_exception(addr, func, MB_EX_ILLEGAL_ADDRESS, rsp, rsp_len);

        /* 预检全部范围；单帧只能修改一个状态所有者，执行副作用前拒绝跨界/未知写入。 */
        if ((reg>=BMS_PARAM_REG_MAGIC && reg<0x2F00u) ||
            (reg>=BMS_PARAM_REG_SOC_PROFILE_STAGE_BASE &&
             (u32)reg+qty<=BMS_PARAM_REG_SOC_PROFILE_STAGE_END)) {
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
    }

    if (addr == 0x00u) return 0;
    rsp[0] = addr;
    rsp[1] = func;
    put_u16be(&rsp[2], reg);
    put_u16be(&rsp[4], qty);
    return modbus_finish_response(rsp, 6u, rsp_len);
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

    /* 调试冻结期间只开放读取和专用寄存器会话，避免其它参数写入改变恢复基线。 */
    if (bms_afe_register_debug_held() && (func == 0x06u || func == 0x10u)) {
        if (addr == 0u) return 0;
        return modbus_exception(addr, func, MB_EX_DEVICE_FAILURE, rsp, rsp_len);
    }

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

    switch (func)
    {
    case 0x03u: return modbus_read_holding(req, req_len, rsp, rsp_len);
    case 0x06u: return modbus_write_single(req, req_len, rsp, rsp_len);
    case 0x10u: return modbus_write_multiple(req, req_len, rsp, rsp_len);
    default: return modbus_exception(addr, func, MB_EX_ILLEGAL_FUNCTION, rsp, rsp_len);
    }
}

#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510
/* 读取 AFE 实际配置的协议诊断字段。 */
static u16 read_afe_actual_reg(u16 offset, const sh3673510_protection_actual_t *actual, u8 valid)
{
    if (!offset) return valid ? 1u : 0u;
    if (!valid) return 0xFFFFu;
    switch (offset) {
    case 1u: return actual->ov_mv;
    case 2u: return actual->uv_mv;
    case 3u: return actual->ocd1_a10;
    case 4u: return actual->ocd2_a10;
    case 5u: return actual->occ_a10;
    case 6u: return actual->ov_delay_ms;
    case 7u: return actual->uv_delay_ms;
    case 8u: return actual->ocd1_delay_ms;
    case 9u: return actual->ocd2_delay_ms;
    case 10u: return actual->occ_delay_ms;
    default: return 0xFFFFu;
    }
}
#endif

/* 把有符号电流转换为既有协议编码。 */
static u16 encode_signed_current_reg(void)
{
    int16_t signed_current = 0;

    if (g_bms_report.discharge_current_a10)
        signed_current = (int16_t)(-((int16_t)g_bms_report.discharge_current_a10));
    else if (g_bms_report.charge_current_a10)
        signed_current = (int16_t)g_bms_report.charge_current_a10;

    return (u16)signed_current;
}

/* 读取当前电池测量和运行状态字段。 */
static u16 read_realtime_status_reg(u16 reg)
{
    switch (reg)
    {
    case BMS_REALTIME_REG_MAGIC_ADDR:       return BMS_REALTIME_REG_MAGIC;
    case BMS_REALTIME_REG_VERSION_ADDR:     return BMS_REALTIME_REG_VERSION;
    case BMS_REALTIME_REG_VOLTAGE_ADDR:     return g_bms_report.pack_voltage_10mv;
    case BMS_REALTIME_REG_CURRENT_ADDR:     return encode_signed_current_reg();
    case BMS_REALTIME_REG_SOC_ADDR:         return g_bms_report.soc.soc_percent;
    case BMS_REALTIME_REG_TEMP_MAX_ADDR:    return g_bms_report.temperature_max_x10;
    case BMS_REALTIME_REG_TEMP_MIN_ADDR:    return g_bms_report.temperature_min_x10;
    case BMS_REALTIME_REG_TEMP_MOS_ADDR:    return g_bms_report.temperature_x10[MOS_TEMP1];
    case BMS_REALTIME_REG_VCELL_MAX_ADDR:   return g_bms_report.cell_max_mv;
    case BMS_REALTIME_REG_VCELL_MIN_ADDR:   return g_bms_report.cell_min_mv;
    case BMS_REALTIME_REG_VCELL_DELTA_ADDR: return g_bms_report.cell_delta_mv;
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
        return read_ascii_string_reg(s_serial_number,
                                     PRODUCT_ID_LENGTH_MAX,
                                     (u16)(reg - PROD_SN_REG_BASE));

    if (reg >= PROD_HW_VER_REG_BASE && reg < (PROD_HW_VER_REG_BASE + PROD_HW_VER_REG_COUNT))
        return read_ascii_string_reg((const u8 *)BMS_PRODUCT_HW_VERSION,
                                     sizeof(BMS_PRODUCT_HW_VERSION),
                                     (u16)(reg - PROD_HW_VER_REG_BASE));

    if (reg >= PROD_SW_VER_REG_BASE && reg < (PROD_SW_VER_REG_BASE + PROD_SW_VER_REG_COUNT))
        return read_ascii_string_reg((const u8 *)BMS_PRODUCT_SW_VERSION,
                                     sizeof(BMS_PRODUCT_SW_VERSION),
                                     (u16)(reg - PROD_SW_VER_REG_BASE));

    return 0u;
}

/* 固定版本直接读编译字符串；只有可写 SN 保留 32-byte 协议缓存。 */
void bms_product_info_refresh(void)
{
    bms_user_params_t user;
    uint8_t serialNumberCount = sizeof(BMS_PRODUCT_DEFAULT_SERIAL) > PRODUCT_ID_LENGTH_MAX
                                  ? PRODUCT_ID_LENGTH_MAX
                                  : sizeof(BMS_PRODUCT_DEFAULT_SERIAL);

    memset(s_serial_number, 0, sizeof(s_serial_number));
    memcpy(s_serial_number, BMS_PRODUCT_DEFAULT_SERIAL, serialNumberCount);
    if (bms_config_get_user(&user) && user.serial[0])
        memcpy(s_serial_number,user.serial,sizeof(user.serial));
}

/* 按类别恢复软件业务参数并提交存储。 */
u8 bms_reset_software_parameters(void)
{
    bms_protection_params_t defaults;
    bms_config_system_params_t system;
    if (!bms_config_store_get_system(&system)) return MB_EX_DEVICE_FAILURE;
    bms_config_store_get_chemistry_protect((u8)system.battery_chemistry, &defaults);
    return commit_protection_update(&defaults);
}
/* 恢复独立 AFE 硬件保护默认参数并应用。 */
u8 bms_reset_afe_parameters(void)
{
    bms_afe_hw_profile_t defaults;
    u8 payload[70];
    u16 i, value;
    bms_config_system_params_t system;
    if (!bms_config_store_get_system(&system)) return MB_EX_DEVICE_FAILURE;
    bms_afe_hw_profile_build_chemistry_default((u8)system.battery_chemistry, &defaults);
    for (i=0u;i<BMS_AFE_HW_PROFILE_WORD_COUNT;++i) {
        memcpy(&value, (const u8 *)&defaults+i*2u, sizeof(value));
        put_u16be(payload+i*2u,value);
    }
    return afe_hw_profile_write_block(payload,BMS_AFE_HW_PROFILE_WORD_COUNT);
}
