/*
 * 文件功能：SH36735xx 寄存器事务驱动；提供 CRC、重试、通信诊断、测量及芯片操作公共接
 * 口。
 * bms/afe/sh3673510/sh3673520.h；实际编译归属见各产品 sources.txt。
 */
#ifndef SH3673520_H
#define SH3673520_H

#include <stdint.h>

/*
 * SH36735xx 共用驱动。SH3673510/3514/3517/3520 共用寄存器/协议，仅最大串数不同；
 * 公共层支持最多 20 串，产品选择实际串数。Telink B85 common/types.h 先定义 size_t，
 * 再引入 TC32 stddef.h 会冲突；公共 ABI 使用编译器 __SIZE_TYPE__，
 * 不重新定义全局 size_t。
 */
#ifdef U32_MAX
# ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ sh3673520_size_t;
# else
typedef unsigned long sh3673520_size_t;
# endif
#else
#include <stddef.h>
typedef size_t sh3673520_size_t;
#endif

#include "sh3673520_reg.h"

typedef enum {
    SH3673520_OK = 0,
    SH3673520_ERR_INVALID_PARAM = -1,
    SH3673520_ERR_TIMEOUT = -2,
    SH3673520_ERR_SPI = -3,
    SH3673520_ERR_CRC = -4,
    SH3673520_ERR_PROTOCOL = -5,
    SH3673520_ERR_NOT_READY = -6,
    SH3673520_ERR_VERIFY = -7,
    SH3673520_ERR_RANGE = -8
} sh3673520_status_t;

typedef struct {
    uint32_t spi_error_count;
    uint32_t crc_error_count;
    uint32_t timeout_count;
    uint32_t protocol_error_count;
    uint32_t retry_count;
    uint32_t successful_transactions;
    uint32_t consecutive_failures;
    sh3673520_status_t last_error;
} sh3673520_comm_stats_t;

typedef struct {
    uint8_t bstatus1;
    uint8_t bstatus2;
} sh3673520_device_status_t;

typedef struct {
    int32_t vadc_raw;
    int32_t cadc_raw;
} sh3673520_current_raw_t;

typedef struct {
    int32_t external_raw[SH3673520_EXTERNAL_TEMP_COUNT];
    int32_t internal_raw;
} sh3673520_temperature_raw_t;

/* 绑定端口和配置，复位并初始化 AFE。 */
sh3673520_status_t SH3673520_Init(void);
/* 执行复位并清除驱动就绪状态。 */
sh3673520_status_t SH3673520_Reset(void);
/* 读取芯片状态寄存器，检查通信是否可达。 */
sh3673520_status_t SH3673520_Probe(void);
/* 查询 SH 驱动初始化就绪状态。 */
uint8_t SH3673520_IsReady(void);

/* 读取单个 AFE 寄存器。 */
sh3673520_status_t SH3673520_ReadReg(uint8_t reg, uint8_t *value);
/* 写入单个 AFE 寄存器并返回通信结果。 */
sh3673520_status_t SH3673520_WriteReg(uint8_t reg, uint8_t value);
/* 校验长度后读取连续 AFE 寄存器。 */
sh3673520_status_t SH3673520_ReadRegs(uint8_t start_reg,
                                      uint8_t *buffer,
                                      sh3673520_size_t length);
/* 校验范围后逐项写入 AFE 寄存器。 */
sh3673520_status_t SH3673520_WriteRegs(uint8_t start_reg,
                                       const uint8_t *buffer,
                                       sh3673520_size_t length);

/* 系列共用配置辅助接口，支持最多 20S 产品。 */
sh3673520_status_t SH3673520_SetCellCount(uint8_t cell_count);
/* 设置驱动均衡掩码并更新芯片寄存器。 */
sh3673520_status_t SH3673520_SetBalanceMask(uint32_t cell_mask,
                                            uint8_t cell_count);

/* 读取有效串数内的单体电压。 */
sh3673520_status_t SH3673520_ReadCellVoltages(int32_t *cell_mv, uint8_t cell_count);
/* 读取并换算电池包总压。 */
sh3673520_status_t SH3673520_ReadPackVoltage(int32_t *pack_mv);
/* 读取并换算有符号电流。 */
sh3673520_status_t SH3673520_ReadCurrent(sh3673520_current_raw_t *current);
/* 读取 NTC 通道测量及有效性。 */
sh3673520_status_t SH3673520_ReadTemperatures(sh3673520_temperature_raw_t *temperatures);
/* 读取 AFE 状态与保护标志。 */
sh3673520_status_t SH3673520_ReadStatus(sh3673520_device_status_t *status);

/* 计算 AFE 通信数据的 CRC8 校验值。 */
uint8_t SH3673520_Crc8(const uint8_t *data, sh3673520_size_t length);
/* 将两个原始字节解码为 16 位有符号值。 */
int32_t SH3673520_DecodeSigned16(uint8_t high, uint8_t low);
/* 将单体电压原始读数换算为毫伏。 */
int32_t SH3673520_CellRawToMilliVolt(int32_t raw);
/* 将总压原始读数换算为毫伏。 */
int32_t SH3673520_PackRawToMilliVolt(int32_t raw);
/* 按分流电阻将电流原始读数换算为毫安。 */
sh3673520_status_t SH3673520_CurrentRawToMilliAmp(int32_t raw,
                                                   uint32_t rsense_uohm,
                                                   int32_t *current_ma);
/* 将 NTC 原始测量换算为欧姆。 */
sh3673520_status_t SH3673520_NtcRawToOhm(int32_t raw,
                                         uint32_t *resistance_ohm);

/* 取得驱动通信统计快照。 */
void SH3673520_GetCommStats(sh3673520_comm_stats_t *stats);
/* 清零通信统计计数。 */
void SH3673520_ClearCommStats(void);

#endif /* SH3673520_H */
