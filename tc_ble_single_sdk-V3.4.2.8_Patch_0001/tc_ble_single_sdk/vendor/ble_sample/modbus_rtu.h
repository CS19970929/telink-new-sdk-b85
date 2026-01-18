#pragma once
#include "tl_common.h"

u16 mb_crc16(const u8 *buf, u32 len);

// 处理一帧：
// - in: req/req_len
// - out: rsp/rsp_len（返回1表示要发送；0表示不回）
int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len);
