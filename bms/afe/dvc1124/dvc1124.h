/*
 * 文件功能：DVC1124 寄存器通信、采样、保护编码与驱动接口；
 * 为条件编译的 DVC backend 保留。
 * bms/afe/dvc1124/dvc1124.h；实际编译归属见各产品 sources.txt。
 */
#ifndef DVC1124_H_
#define DVC1124_H_

#include <stdint.h>
#include "dvc1124_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DVC1124-2 公共驱动接口。寄存器依据 DVC1124-2 V1.2 手册，
 * 板级依据 HS-D008 原理图/BOM；DVC11XX DemoCode V1.3 仅作辅助参考，
 * 冲突时不得覆盖 V1.2 定义。
 * Telink B85 的 i2c_master_init()/i2c_set_id() 使用含 R/W位位置的 8 位传输地址。
 * 本驱动保存写地址（LSB=0），读地址为 write address | 1。
 */
typedef enum
{
    DVC1124_MODEL_22 = 22,
    DVC1124_MODEL_24 = 24
} dvc1124_model_t;

typedef enum
{
    DVC1124_ADDR_FIXED = 0,      /* 正常 HS-D008 地址：写 0x40、读 0x41。 */
    DVC1124_ADDR_HARDWIRED = 1,  /* 级联地址由 AFE 硬件引脚选择。 */
    DVC1124_ADDR_EXPLICIT = 2    /* MCU 目标地址覆盖值；硬件地址必须已匹配。 */
} dvc1124_addr_mode_t;

typedef struct
{
    dvc1124_model_t model;
    dvc1124_addr_mode_t addr_mode;
    uint8_t hardwire_code;
    uint8_t explicit_write_addr;
    uint8_t cell_count;          /* DVC1124-2 V1.2 有效范围为 4..24。 */
    uint32_t shunt_uohm;
    uint8_t battery_ntc_gp;
    uint8_t mos_ntc_gp;
} dvc1124_config_t;

typedef struct
{
    uint8_t valid;
    uint8_t alarm;
    uint8_t status;
    uint8_t fet_status;          /* R6 CHGF/DSGF，与 R1 CST/RC 事件严格分开。 */
    uint8_t voltage_fresh;
    uint8_t current_fresh;
    uint8_t chip_version;
    uint8_t write_addr;
    uint8_t cell_count;
    uint32_t sample_tick_32k;    /* 采样时刻，使用 SDK 32K 时钟的 32 位回绕值。 */
    int32_t raw_current_ma;     /* 软件工厂校准之前的值。 */
    int32_t current_ma;          /* 正值放电，负值充电。 */
    uint32_t vtop_mv;
    uint32_t pack_mv;
    uint32_t load_mv;
    uint16_t cell_mv[24];
    uint16_t gp_code[6];
    uint32_t ntc_res_ohm[6];
    int16_t die_temp_x10;        /* 有符号温度，单位为 0.1 ℃。 */
    uint16_t rpu_ohm;
} dvc1124_snapshot_t;

typedef enum
{
    DVC1124_BOOT_ZERO_NOT_ATTEMPTED = 0u,
    DVC1124_BOOT_ZERO_IN_PROGRESS = 1u,
    DVC1124_BOOT_ZERO_VALID = 2u,
    DVC1124_BOOT_ZERO_DISABLED = 3u,
    DVC1124_BOOT_ZERO_FET_IO_ERROR = 4u,
    DVC1124_BOOT_ZERO_FET_ACTIVE = 5u,
    DVC1124_BOOT_ZERO_CAMZ_ERROR = 6u,
    DVC1124_BOOT_ZERO_SAMPLE_IO_ERROR = 7u,
    DVC1124_BOOT_ZERO_OUT_OF_RANGE = 8u,
    DVC1124_BOOT_ZERO_UNSTABLE = 9u
} dvc1124_boot_zero_status_t;

typedef struct
{
    dvc1124_boot_zero_status_t status;
    uint8_t sample_count;
    int32_t learned_offset_ma;      /* 持久化 offset/gain 校准后的残差。 */
    int32_t raw_sample1_ma;         /* 持久化工厂校准之前的值。 */
    int32_t raw_sample2_ma;
    int32_t calibrated_sample1_ma;  /* 已应用持久化 offset/gain。 */
    int32_t calibrated_sample2_ma;
    uint16_t spread_ma;
} dvc1124_boot_zero_diag_t;

typedef enum
{
    DVC1124_OPENWIRE_IDLE = 0u,
    DVC1124_OPENWIRE_WAITING = 1u,
    DVC1124_OPENWIRE_READY = 2u,
    DVC1124_OPENWIRE_ERROR = 3u,
} dvc1124_openwire_state_t;

typedef struct
{
    dvc1124_openwire_state_t state;
    uint8_t valid;
    uint8_t cell_count;
    uint16_t cell_mv[24]; /* DVC1124 物理最大值；宏在此公共类型之后声明。 */
    uint32_t pack_mv;
    uint8_t error;
} dvc1124_openwire_result_t;



typedef enum
{
    DVC1124_REG_READ_SAFE = 0u,
    DVC1124_REG_READ_CLEAR = 1u
} dvc1124_reg_read_effect_t;

#define DVC1124_FIXED_WRITE_ADDR             0x40u
#define DVC1124_HARDWIRE_BASE_WRITE_ADDR     0xC0u
#define DVC1124_MAX_REGISTER                 DVC1124_REG_MAX
#define DVC1124_MIN_CELLS                    4u
#define DVC1124_MAX_CELLS                    24u
#define DVC1124_MAX_GP                       6u

/* 产品与板级默认值集中在项目拥有的配置文件中。 */
#include "bms_product.h"

#ifndef DVC1124_DEFAULT_MODEL
#define DVC1124_DEFAULT_MODEL                DVC1124_MODEL_22
#endif
#ifndef DVC1124_DEFAULT_ADDR_MODE
#define DVC1124_DEFAULT_ADDR_MODE            DVC1124_ADDR_FIXED
#endif
#ifndef DVC1124_DEFAULT_HARDWIRE_CODE
#define DVC1124_DEFAULT_HARDWIRE_CODE        0u
#endif
#ifndef DVC1124_DEFAULT_EXPLICIT_WRITE_ADDR
#define DVC1124_DEFAULT_EXPLICIT_WRITE_ADDR  DVC1124_FIXED_WRITE_ADDR
#endif
#ifndef DVC1124_DEFAULT_CELL_COUNT
#define DVC1124_DEFAULT_CELL_COUNT           24u
#endif
#ifndef DVC1124_DEFAULT_SHUNT_UOHM
#define DVC1124_DEFAULT_SHUNT_UOHM           200u
#endif
#ifndef DVC1124_DEFAULT_BATTERY_NTC_GP
#define DVC1124_DEFAULT_BATTERY_NTC_GP       4u
#endif
#ifndef DVC1124_DEFAULT_MOS_NTC_GP
#define DVC1124_DEFAULT_MOS_NTC_GP           1u
#endif

/* 取得 DVC 当前驱动配置。 */
void DVC1124_GetConfig(dvc1124_config_t *config);
/* 取得 DVC 最近一次测量与状态快照。 */
void DVC1124_GetSnapshot(dvc1124_snapshot_t *snapshot);
/* 取得 DVC 当前 I2C 写地址。 */
uint8_t DVC1124_GetWriteAddress(void);

/* 读取指定范围 DVC 寄存器并返回通信结果。 */
uint8_t DVC1124_ReadRegisters(uint8_t reg, uint8_t *data, uint8_t len);
/* 校验范围后写入 DVC 寄存器。 */
uint8_t DVC1124_WriteRegisters(uint8_t reg, const uint8_t *data, uint8_t len);
/* 设置 DVC 输出授权并同步 MOS 控制。 */
void DVC1124_SetOutputEnabled(uint8_t enabled);
/* 设置 DVC 均衡请求掩码并同步硬件。 */
uint8_t DVC1124_SetBalanceMask(uint32_t cell_mask);
/* 推进均衡服务并刷新通道状态。 */
void DVC1124_BalanceService(uint8_t allow_refresh);
/* 准备并开始 DVC 非阻塞断线检测流程。 */
uint8_t DVC1124_OpenWireBegin(void);
uint8_t DVC1124_OpenWireStop(void);
/* 推进断线检测阶段并收集完成结果。 */
void DVC1124_OpenWirePoll(void);
/* 取得最近一次断线检测结果。 */
void DVC1124_OpenWireGetResult(dvc1124_openwire_result_t *result);
/* 复位 DVC 断线检测阶段及结果。 */
void DVC1124_OpenWireReset(void);
/* 校验并设置核心过温阈值编码。 */
uint8_t DVC1124_SetCoreOtThresholdCode(uint8_t threshold_code);
/* 读取核心过温事件锁存状态。 */
uint8_t DVC1124_GetCoreOtEventLatched(void);

/* W0C 与自清除操作由 dvc1124_special.c 实现。 */
uint8_t DVC1124_ClearAlarmFlags(uint8_t flag_mask);
/* 发起 DVC 电流 ADC 校准。 */
uint8_t DVC1124_StartCadcCalibration(void);
/* 启动阶段确认 FET 关闭后采集残余电流并校准零点。 */
uint8_t DVC1124_BootCurrentZeroCalibrate(void);
/*
 * 读取副作用依据 V1.2 手册：0x01 的 VADF/CC1F/CC2F 为 RC，读字节会消耗标志；
 * 0x76 的 COTF 为 RC，读字节会消耗硬件标志。
 */
static inline dvc1124_reg_read_effect_t DVC1124_RegReadEffect(uint8_t reg)
{
    if ((reg == DVC1124_REG_STATUS) || (reg == DVC1124_REG_CORE_OT))
        return DVC1124_REG_READ_CLEAR;
    return DVC1124_REG_READ_SAFE;
}

/* 判断读取寄存器是否会改变硬件状态。 */
static inline uint8_t DVC1124_RegReadHasSideEffect(uint8_t reg)
{
    return (DVC1124_RegReadEffect(reg) != DVC1124_REG_READ_SAFE) ? 1u : 0u;
}

/* 检查该寄存器是否允许通用读改写。 */
static inline uint8_t DVC1124_RegGenericRmwAllowed(uint8_t reg)
{
    if ((reg == DVC1124_REG_ALARM) ||
        (reg == DVC1124_REG_STATUS) ||
        (reg == DVC1124_REG_CORE_OT))
        return 0u;
    return 1u;
}

/*
 * 安全字段/寄存器配置辅助接口。拒绝具有读清除或特殊写入语义的寄存器；
 * ALARM W0C、STATUS 命令、CORE_OT RC+RW 和自清除命令必须使用专用 API。
 */
uint8_t DVC1124_WriteRegisterSafe(uint8_t reg, uint8_t requested);

/* 读取并提取寄存器中的指定字段。 */
static inline uint8_t DVC1124_ReadRegisterField(uint8_t reg,
                                                uint8_t mask,
                                                uint8_t shift,
                                                uint8_t *value)
{
    uint8_t raw;

    if ((value == 0) || (mask == 0u) || (reg > DVC1124_MAX_REGISTER)) return 0u;
    if (DVC1124_RegReadHasSideEffect(reg)) return 0u;
    if (!DVC1124_ReadRegisters(reg, &raw, 1u)) return 0u;
    *value = DVC1124_FIELD_GET(mask, shift, raw);
    return 1u;
}

/* 检查副作用和允许写位后更新寄存器字段。 */
static inline uint8_t DVC1124_WriteRegisterFieldSafe(uint8_t reg,
                                                      uint8_t mask,
                                                      uint8_t shift,
                                                      uint8_t value)
{
    uint8_t current;
    uint8_t target;
    uint8_t verify;
    uint8_t owned = DVC1124_RegDocumentedWriteMask(reg);
    uint8_t field_max;

    if ((reg > DVC1124_MAX_REGISTER) || (mask == 0u) || (shift >= 8u)) return 0u;
    if ((mask & owned) != mask) return 0u;
    if (!DVC1124_RegGenericRmwAllowed(reg)) return 0u;
    if (DVC1124_RegReadHasSideEffect(reg)) return 0u;

    /* FIELD_PREP 会掩码截断值，必须先校验再编码，避免静默截断。 */
    field_max = (uint8_t)(mask >> shift);
    if ((field_max == 0u) || (value > field_max)) return 0u;

    if (!DVC1124_ReadRegisters(reg, &current, 1u)) return 0u;

    target = (uint8_t)((current & (uint8_t)~mask) |
                       DVC1124_FIELD_PREP(mask, shift, value));
    if (!DVC1124_WriteRegisters(reg, &target, 1u)) return 0u;
    if (!DVC1124_ReadRegisters(reg, &verify, 1u)) return 0u;

    return ((verify & mask) == (target & mask)) ? 1u : 0u;
}

/* 固定运行配置由固件拥有，不提供可变的传输配置 API。 */
uint8_t DVC1124_ApplyProjectOperatingConfig(void);

/* DVC 后端入口；应用代码使用 bms_afe.h。 */
void DVC1124_App_AFEGet(void);
/* 采样后发布测量、合并保护并更新 DVC FET 仲裁。 */
void DVC1124_BmsApp_AFEGet(void);
/* 复位 DVC AFE 并重建驱动状态。 */
void DVC1124_AFE_Reset(void);
/* 请求 DVC 进入 shutdown 并返回通信结果。 */
uint8_t DVC1124_AFE_Sleep(void);
/* 按当前参数更新 DVC AFE 配置。 */
void DVC1124_UpdataAfeConfig(void);
/* 应用并验证 DVC 硬件保护参数。 */
uint8_t DVC1124_ApplyProtectionConfig(void);

#ifdef __cplusplus
}
#endif

#endif /* 头文件保护：DVC1124_H_。 */
