/*
 * 文件功能：公共业务参数只读能力、读写校验和分组恢复；遵守既有参数授权与持久化事务。
 * bms/core/bms_parameter_access.h；实际编译归属见各产品 sources.txt。
 */
#pragma once
#include "common/types.h"
/*
 * 公共参数协议 v2 保留原 D008 接口 magic；这是接口签名，不是产品 ID。
 * 帧长保持不超过 20 字节。
 */
#define BMS_PARAMETER_INTERFACE_MAGIC 0xD008u

/* 既有业务 wire 地址，保持数值、缩放和各事务边界。 */
#define BMS_PARAM_REG_SOC                         0x1005u
#define BMS_PARAM_REG_CAPACITY                    0x2318u
#define BMS_PARAM_REG_CYCLE                       0x2319u
#define BMS_PARAM_REG_MAGIC                       0x2E00u
#define BMS_PARAM_REG_VERSION                     0x2E01u
#define BMS_PARAM_REG_CAPABILITIES                0x2E02u
#define BMS_PARAM_REG_LAST_RESULT                 0x2E03u
#define BMS_PARAM_REG_SEQUENCE                    0x2E04u
#define BMS_PARAM_REG_CONFIG_FORMAT               0x2E05u
#define BMS_PARAM_REG_SN_GENERATION               0x2E06u
#define BMS_PARAM_REG_RESET_POLICY                0x2E07u
#define BMS_PARAM_REG_DEFAULT_CAPACITY            0x2E08u
#define BMS_PARAM_REG_HEATER_SUPPORTED            0x2E09u
#define BMS_PARAM_REG_DEFAULT_HEATER_START        0x2E0Au
#define BMS_PARAM_REG_DEFAULT_HEATER_STOP         0x2E0Bu
#define BMS_PARAM_REG_DEFAULT_BALANCE_ENABLE      0x2E0Cu
#define BMS_PARAM_REG_DEFAULT_BALANCE_START       0x2E0Du
#define BMS_PARAM_REG_DEFAULT_BALANCE_DELTA_START 0x2E0Eu
#define BMS_PARAM_REG_DEFAULT_BALANCE_DELTA_STOP  0x2E0Fu
#define BMS_PARAM_REG_RESET_COMMAND               0x2E10u
#define BMS_PARAM_REG_HEATER_ENABLE               0x2E20u
#define BMS_PARAM_REG_HEATER_START                0x2E21u
#define BMS_PARAM_REG_HEATER_STOP                 0x2E22u
#define BMS_PARAM_REG_CURRENT_OFFSET_LO           0x2E24u
#define BMS_PARAM_REG_CURRENT_OFFSET_HI           0x2E25u
#define BMS_PARAM_REG_CURRENT_GAIN_LO             0x2E26u
#define BMS_PARAM_REG_CURRENT_GAIN_HI             0x2E27u
#define BMS_PARAM_REG_RAW_CURRENT_LO              0x2E28u
#define BMS_PARAM_REG_RAW_CURRENT_HI              0x2E29u
#define BMS_PARAM_REG_CURRENT_LO                  0x2E2Au
#define BMS_PARAM_REG_CURRENT_HI                  0x2E2Bu
#define BMS_PARAM_REG_SAMPLE_VALID                0x2E2Cu
#define BMS_PARAM_REG_SAMPLE_TICK_LO              0x2E2Du
#define BMS_PARAM_REG_SAMPLE_TICK_HI              0x2E2Eu
#define BMS_PARAM_REG_SN_BASE                     0x2E30u
#define BMS_PARAM_REG_SN_LAST                     0x2E3Fu
#define BMS_PARAM_REG_SN_COMMAND                  0x2E40u
#define BMS_PARAM_REG_SN_STAGE_BASE               0x2E50u
#define BMS_PARAM_REG_SN_STAGE_LAST               0x2E5Fu
#define BMS_PARAM_REG_SN_STAGE_END                0x2E60u
#define BMS_PARAM_REG_BALANCE_ENABLE              0x2E70u
#define BMS_PARAM_REG_BALANCE_START               0x2E71u
#define BMS_PARAM_REG_BALANCE_DELTA_START         0x2E72u
#define BMS_PARAM_REG_BALANCE_DELTA_STOP          0x2E73u
#define BMS_PARAM_REG_UPDATE_REVISION_BASE        0x2E80u


#define PROD_SN_REG_BASE                   0xc002
#define PROD_SN_REG_COUNT                  16

#define PROD_HW_VER_REG_BASE               (PROD_SN_REG_BASE + 16)
#define PROD_HW_VER_REG_COUNT              16

#define PROD_SW_VER_REG_BASE               (PROD_HW_VER_REG_BASE + 16)
#define PROD_SW_VER_REG_COUNT              16

#define PRODUCT_ID_LENGTH_MAX 32

typedef struct {
    u8 BMS_SerialNumber[PRODUCT_ID_LENGTH_MAX];
    u8 BMS_HardWareVersion[PRODUCT_ID_LENGTH_MAX];
    u8 BMS_SoftWareVersion[PRODUCT_ID_LENGTH_MAX];
} bms_product_info_t;


/* 从 Config SN 和编译身份刷新协议 RAM 缓存，不写 Flash。 */
void bms_product_info_refresh(void);

/* 检查业务参数地址是否允许读取。 */
int bms_parameter_readable(u16 reg);
/* 按地址读取业务参数并编码为协议字。 */
u16 bms_parameter_read(u16 reg);
/* 校验并写入业务参数，按类别执行持久化。 */
u8 bms_parameter_write(u16 reg, u16 qty, const u8 *data);
/* 按类别恢复软件业务参数并提交存储。 */
u8 bms_reset_software_parameters(void);
/* 恢复独立 AFE 硬件保护默认参数并应用。 */
u8 bms_reset_afe_parameters(void);
