/*
 * 文件功能：DVC1124 参数的 requested/effective 与原始寄存器访问；
 * 校验写入和固定配置边界。
 * bms/afe/dvc1124/dvc1124_config_service.h；实际编译归属见各产品 sources.txt。
 */
#ifndef DVC1124_CONFIG_SERVICE_H_
#define DVC1124_CONFIG_SERVICE_H_

#include "tl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 与传输方式无关的 DVC1124 诊断/配置字段 ID。
 * 0x2800 固定运行/板级字段由编译期产品策略推导，只读且不持久化。
 * 运行时保护另有所有者：软件保护使用 g_tParam.protect，
 * AFE 硬件保护使用 BMS_AFE_HW 配置事务。
 */
typedef enum
{
    DVC1124_CFG_SCHEMA                    = 0x00,
    DVC1124_CFG_MODEL                     = 0x01,
    DVC1124_CFG_CHIP_VERSION              = 0x02,
    DVC1124_CFG_WRITE_ADDR                = 0x03,
    DVC1124_CFG_CELL_COUNT                = 0x04,
    DVC1124_CFG_SHUNT_UOHM_LO             = 0x05,
    DVC1124_CFG_SHUNT_UOHM_HI             = 0x06,
    DVC1124_CFG_STATUS_CACHED              = 0x07,
    DVC1124_CFG_CORE_OT_EVENT_LATCHED      = 0x08,
    DVC1124_CFG_CONFIG_INCONSISTENT        = 0x09,

    DVC1124_CFG_HS_FET_MASK               = 0x10,
    DVC1124_CFG_CADC_WORK_ENABLE          = 0x11,
    DVC1124_CFG_CURRENT_WAKE_ENABLE       = 0x12,
    DVC1124_CFG_CC1_WORK_TIME             = 0x13,
    DVC1124_CFG_CC1_SLEEP_WAKE_TIME       = 0x14,
    DVC1124_CFG_CHARGE_PUMP_VOLTAGE       = 0x15,
    DVC1124_CFG_CELL_MEAS_MASK            = 0x16,
    DVC1124_CFG_CELL_SIGNED_MODE          = 0x17,
    DVC1124_CFG_VADC_ENABLE               = 0x18,
    DVC1124_CFG_VADC_SYNC                 = 0x19,
    DVC1124_CFG_VADC_PERIOD_CYCLES        = 0x1A,
    DVC1124_CFG_VADC_TIME_US              = 0x1B,

    DVC1124_CFG_GP1_MODE                  = 0x20,
    DVC1124_CFG_GP2_MODE                  = 0x21,
    DVC1124_CFG_GP3_MODE                  = 0x22,
    DVC1124_CFG_GP4_MODE                  = 0x23,
    DVC1124_CFG_GP5_MODE                  = 0x24,
    DVC1124_CFG_GP6_MODE                  = 0x25,
    DVC1124_CFG_V3P3_SLEEP_ENABLE         = 0x28,
    DVC1124_CFG_V3P3_WORK_ENABLE          = 0x29,
    DVC1124_CFG_V3P3_TIMEOUT_RESTART      = 0x2A,
    DVC1124_CFG_I2C_WDT_SECONDS           = 0x2B,
    DVC1124_CFG_TIMED_WAKE_SECONDS        = 0x2C,
    DVC1124_CFG_INTERRUPT_MASK            = 0x2D,
    DVC1124_CFG_CURRENT_WAKE_UV           = 0x30,
    DVC1124_CFG_BODY_DIODE_UV             = 0x31,
    DVC1124_CFG_DSG_PULLDOWN              = 0x32,
    DVC1124_CFG_I2C_TIMEOUT_CLOSE_CHG     = 0x33,
    DVC1124_CFG_I2C_TIMEOUT_CLOSE_DSG     = 0x34,
    DVC1124_CFG_CORE_OT_X10C              = 0x35,

    /*
     * 请求/实际生效的 AFE 保护诊断；写入必须走专用原子 AFE 硬件配置接口，
     * 不能使用此窗口。
     */
    DVC1124_CFG_REQ_COV_MV                = 0x40,
    DVC1124_CFG_REQ_COV_DELAY_MS          = 0x41,
    DVC1124_CFG_REQ_CUV_MV                = 0x42,
    DVC1124_CFG_REQ_CUV_DELAY_MS          = 0x43,
    DVC1124_CFG_REQ_OCD1_X10A             = 0x44,
    DVC1124_CFG_REQ_OCD1_DELAY_MS         = 0x45,
    DVC1124_CFG_REQ_OCC1_X10A             = 0x46,
    DVC1124_CFG_REQ_OCC1_DELAY_MS         = 0x47,
    DVC1124_CFG_REQ_OCD2_X10A             = 0x48,
    DVC1124_CFG_REQ_OCD2_DELAY_MS         = 0x49,
    DVC1124_CFG_REQ_OCC2_X10A             = 0x4A,
    DVC1124_CFG_REQ_OCC2_DELAY_MS         = 0x4B,
    DVC1124_CFG_REQ_SCD_MV                = 0x4C,
    DVC1124_CFG_REQ_SCD_DELAY_US          = 0x4D,

    DVC1124_CFG_EFF_COV_MV                = 0x50,
    DVC1124_CFG_EFF_COV_DELAY_MS          = 0x51,
    DVC1124_CFG_EFF_CUV_MV                = 0x52,
    DVC1124_CFG_EFF_CUV_DELAY_MS          = 0x53,
    DVC1124_CFG_EFF_OCD1_X10A             = 0x54,
    DVC1124_CFG_EFF_OCD1_DELAY_MS         = 0x55,
    DVC1124_CFG_EFF_OCC1_X10A             = 0x56,
    DVC1124_CFG_EFF_OCC1_DELAY_MS          = 0x57,
    DVC1124_CFG_EFF_OCD2_X10A             = 0x58,
    DVC1124_CFG_EFF_OCD2_DELAY_MS          = 0x59,
    DVC1124_CFG_EFF_OCC2_X10A             = 0x5A,
    DVC1124_CFG_EFF_OCC2_DELAY_MS          = 0x5B,
    DVC1124_CFG_EFF_SCD_MV                = 0x5C,
    DVC1124_CFG_EFF_SCD_DELAY_US           = 0x5D,
} dvc1124_config_field_t;

typedef enum
{
    DVC1124_CFG_OK = 0,
    DVC1124_CFG_ERR_ADDRESS,
    DVC1124_CFG_ERR_READ_ONLY,
    DVC1124_CFG_ERR_VALUE,
    DVC1124_CFG_ERR_AFE_IO,
    DVC1124_CFG_ERR_STORE,
    DVC1124_CFG_ERR_FORBIDDEN,
    DVC1124_CFG_ERR_INCONSISTENT,
} dvc1124_config_result_t;

#define DVC1124_CONFIG_SCHEMA_VERSION 0x0001u

/* 读取 DVC 语义配置窗口中的指定字段。 */
dvc1124_config_result_t DVC1124_ConfigServiceRead(dvc1124_config_field_t field,
                                                   u32 *value);
/* 拒绝固定配置和保护窗口写入，返回只读或地址错误。 */
dvc1124_config_result_t DVC1124_ConfigServiceWrite(dvc1124_config_field_t field,
                                                    u32 value);

/* 原始镜像仅供只读诊断；拒绝具有读清除副作用的寄存器，应读取缓存/软件锁存语义诊断。 */
dvc1124_config_result_t DVC1124_ConfigServiceReadRaw(u8 reg, u8 *value);
/* 拒绝原始寄存器写入，返回只读或地址错误。 */
dvc1124_config_result_t DVC1124_ConfigServiceWriteRaw(u8 reg, u8 value);

#ifdef __cplusplus
}
#endif

#endif /* 头文件保护：DVC1124_CONFIG_SERVICE_H_。 */
