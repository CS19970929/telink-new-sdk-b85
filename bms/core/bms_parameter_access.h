/*
 * 文件功能：公共业务参数只读能力、读写校验和分组恢复；遵守既有参数授权与持久化事务。
 * bms/core/bms_parameter_access.h；实际编译归属见各产品 sources.txt。
 */
#pragma once
#include "tl_common.h"
/*
 * 公共参数协议 v2 保留原 D008 接口 magic；这是接口签名，不是产品 ID。
 * 帧长保持不超过 20 字节。
 */
#define BMS_PARAMETER_INTERFACE_MAGIC 0xD008u
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
