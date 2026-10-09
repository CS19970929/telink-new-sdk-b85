/*
 * 文件功能：BIN 内的参数更新选择与一次性标记；保留历史编号只读接口。
 * bms/core/bms_update_policy.h；实际编译归属见各产品 sources.txt。
 */
#pragma once

#include <stdint.h>
#include "bms_parameter_policy.h"

/* 旧编号仅保留只读协议兼容；OTA 参数选择使用下面的一次性标记。 */
typedef enum {
    BMS_UPDATE_SW,
    BMS_UPDATE_AFE,
    BMS_UPDATE_BUSINESS,
    BMS_UPDATE_SOC,
    BMS_UPDATE_CALIBRATION,
    BMS_UPDATE_IDENTITY,
    BMS_UPDATE_SOC_STATE,
    BMS_UPDATE_RESERVED_7, /* 协议 0x2E87 保留，不再属于持久域。 */
    BMS_UPDATE_EVENTS,
    BMS_UPDATE_GROUP_COUNT
} bms_update_group_t;

#define BMS_UPDATE_CONFIG_GROUP_COUNT BMS_UPDATE_SOC_STATE

#if BMS_UPDATE_SW_REVISION < 1 || BMS_UPDATE_SW_REVISION > 65535
#error "BMS_UPDATE_SW_REVISION must be in 1..65535"
#endif
#if BMS_UPDATE_AFE_REVISION < 1 || BMS_UPDATE_AFE_REVISION > 65535
#error "BMS_UPDATE_AFE_REVISION must be in 1..65535"
#endif
#if BMS_UPDATE_BUSINESS_REVISION < 1 || BMS_UPDATE_BUSINESS_REVISION > 65535
#error "BMS_UPDATE_BUSINESS_REVISION must be in 1..65535"
#endif
#if BMS_UPDATE_SOC_REVISION < 1 || BMS_UPDATE_SOC_REVISION > 65535
#error "BMS_UPDATE_SOC_REVISION must be in 1..65535"
#endif
#if BMS_UPDATE_CALIBRATION_REVISION < 1 || BMS_UPDATE_CALIBRATION_REVISION > 65535
#error "BMS_UPDATE_CALIBRATION_REVISION must be in 1..65535"
#endif
#if BMS_UPDATE_IDENTITY_REVISION < 1 || BMS_UPDATE_IDENTITY_REVISION > 65535
#error "BMS_UPDATE_IDENTITY_REVISION must be in 1..65535"
#endif
#if BMS_UPDATE_SOC_STATE_REVISION < 1 || BMS_UPDATE_SOC_STATE_REVISION > 65535
#error "BMS_UPDATE_SOC_STATE_REVISION must be in 1..65535"
#endif
#if BMS_UPDATE_EVENTS_REVISION < 1 || BMS_UPDATE_EVENTS_REVISION > 65535
#error "BMS_UPDATE_EVENTS_REVISION must be in 1..65535"
#endif

/* 返回旧只读协议的历史编号，不参与新 OTA 更新决策。 */
static inline uint16_t bms_update_revision(bms_update_group_t group)
{
    static const uint16_t revisions[BMS_UPDATE_GROUP_COUNT] = {
        BMS_UPDATE_SW_REVISION,
        BMS_UPDATE_AFE_REVISION,
        BMS_UPDATE_BUSINESS_REVISION,
        BMS_UPDATE_SOC_REVISION,
        BMS_UPDATE_CALIBRATION_REVISION,
        BMS_UPDATE_IDENTITY_REVISION,
        BMS_UPDATE_SOC_STATE_REVISION,
        1u, /* 已废弃老化组的原协议占位。 */
        BMS_UPDATE_EVENTS_REVISION,
    };
    return (unsigned)group < BMS_UPDATE_GROUP_COUNT ? revisions[group] : 0u;
}

/* 复用 Config 尾部原六个编号槽，保持 schema、长度和 Flash 槽位不变。
 * 第一个 word 恒为 0，与旧固件合法的 1..65535 编号区分；其余为随机标识。
 * State 在已废弃学习字段中保存自己的标记，两个域各自提交，不跨域假定原子性。 */
#define BMS_OTA_UPDATE_ID_WORDS 6u
#define BMS_OTA_UPDATE_CONFIG_MASK 0x000Fu
#define BMS_OTA_UPDATE_SUPPORTED_MASK 0x004Fu
#ifndef BMS_OTA_UPDATE_MASK
#define BMS_OTA_UPDATE_MASK 0u
#endif
#ifndef BMS_OTA_UPDATE_ID_0
#define BMS_OTA_UPDATE_ID_0 0u
#define BMS_OTA_UPDATE_ID_1 0u
#define BMS_OTA_UPDATE_ID_2 0u
#define BMS_OTA_UPDATE_ID_3 0u
#define BMS_OTA_UPDATE_ID_4 0u
#define BMS_OTA_UPDATE_ID_5 0u
#endif
#if (BMS_OTA_UPDATE_MASK & ~BMS_OTA_UPDATE_SUPPORTED_MASK) != 0u
#error "Unsupported OTA parameter group"
#endif
#if BMS_OTA_UPDATE_ID_0 != 0u || BMS_OTA_UPDATE_ID_1 > 65535u || BMS_OTA_UPDATE_ID_2 > 65535u || BMS_OTA_UPDATE_ID_3 > 65535u || BMS_OTA_UPDATE_ID_4 > 65535u || BMS_OTA_UPDATE_ID_5 > 65535u
#error "Invalid OTA update identifier"
#endif
#if BMS_OTA_UPDATE_MASK != 0u && (BMS_OTA_UPDATE_ID_1 | BMS_OTA_UPDATE_ID_2 | BMS_OTA_UPDATE_ID_3 | BMS_OTA_UPDATE_ID_4 | BMS_OTA_UPDATE_ID_5) == 0u
#error "Selected OTA groups require an update identifier"
#endif

static inline uint8_t bms_ota_update_selected(bms_update_group_t group)
{
    return (unsigned)group < BMS_UPDATE_GROUP_COUNT &&
           (BMS_OTA_UPDATE_MASK & (1u << (unsigned)group)) != 0u;
}

static inline void bms_ota_update_copy_id(uint16_t *destination)
{
    const uint16_t id[BMS_OTA_UPDATE_ID_WORDS] = {
        BMS_OTA_UPDATE_ID_0, BMS_OTA_UPDATE_ID_1, BMS_OTA_UPDATE_ID_2,
        BMS_OTA_UPDATE_ID_3, BMS_OTA_UPDATE_ID_4, BMS_OTA_UPDATE_ID_5
    };
    unsigned i;
    for (i = 0u; i < BMS_OTA_UPDATE_ID_WORDS; ++i) destination[i] = id[i];
}

static inline uint8_t bms_ota_update_id_matches(const uint16_t *saved)
{
    uint16_t id[BMS_OTA_UPDATE_ID_WORDS];
    unsigned i;
    bms_ota_update_copy_id(id);
    for (i = 0u; i < BMS_OTA_UPDATE_ID_WORDS; ++i)
        if (saved[i] != id[i]) return 0u;
    return 1u;
}
