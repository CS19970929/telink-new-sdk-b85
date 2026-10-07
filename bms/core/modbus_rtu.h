/*
 * 文件功能：公共 Modbus RTU 解析与寄存器映射；UART/BLE 共用 CRC、读写校验及配置事务
 * 路径。
 * bms/core/modbus_rtu.h；实际编译归属见各产品 sources.txt。
 */
#pragma once
#include "common/types.h"

/* 包含私有 0x7F 回显帧，普通 Modbus RTU 可容纳于 256 字节。 */
#define MODBUS_RTU_FRAME_CAPACITY 268u

/* 计算 Modbus RTU 使用的 CRC16 校验值。 */
u16 mb_crc16(const u8 *buf, u32 len);

/*
 * 处理一个兼容 Modbus 帧，UART 与 BLE SPP 共用；
 * 不得在两个传输层分别实现 DVC1124 配置语义。
 */
int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len);
