/*
 * 文件功能：AFE 硬件参数写授权会话；校验 token、超时和 Modbus 请求，
 * 授权状态不持久化。
 * bms/core/bms_afe_hw_access.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_AFE_HW_ACCESS_H_
#define BMS_AFE_HW_ACCESS_H_

#include "tl_common.h"

#define BMS_AFE_HW_ACCESS_MODBUS_FUNC       0x42u
#define BMS_AFE_HW_ACCESS_UNLOCK_MAGIC      0x41464548UL /* 标识字符串：AFEH。 */
#define BMS_AFE_HW_ACCESS_PROTOCOL_VERSION  2u
#define BMS_AFE_HW_ACCESS_TIMEOUT_SECONDS   60u

typedef enum
{
    BMS_AFE_HW_ACCESS_CMD_OPEN = 0x01u,
    BMS_AFE_HW_ACCESS_CMD_HEARTBEAT = 0x02u,
    BMS_AFE_HW_ACCESS_CMD_CLOSE = 0x03u,
    BMS_AFE_HW_ACCESS_CMD_STATUS = 0x04u,
    BMS_AFE_HW_ACCESS_CMD_STAGE = 0x05u,
    BMS_AFE_HW_ACCESS_CMD_COMMIT = 0x06u,
} bms_afe_hw_access_command_t;

typedef enum
{
    BMS_AFE_HW_ACCESS_STATUS_OK = 0u,
    BMS_AFE_HW_ACCESS_STATUS_BAD_REQUEST = 1u,
    BMS_AFE_HW_ACCESS_STATUS_AUTH_REQUIRED = 2u,
    BMS_AFE_HW_ACCESS_STATUS_UNSUPPORTED = 3u,
    BMS_AFE_HW_ACCESS_STATUS_APPLY_FAILED = 4u,
} bms_afe_hw_access_status_t;

/* 解析硬件写授权功能码，校验长度和会话条件。 */
int bms_afe_hw_access_modbus_on_frame(const u8 *req,
                                      u32 req_len,
                                      u8 *rsp,
                                      u32 *rsp_len);
/* 内部完整帧入口，与 0x10 共用校验、应用和回滚。 */
u8 bms_afe_hw_write_complete_frame(const u8 *frame, u32 length);
/* 检查授权超时并关闭失效会话。 */
void bms_afe_hw_access_poll(void);
/* 关闭 AFE 硬件写授权会话并清除令牌。 */
void bms_afe_hw_access_close(void);
/* 查询硬件写授权会话当前是否有效。 */
u8 bms_afe_hw_access_is_active(void);
/* 取得硬件写授权会话的剩余秒数。 */
u16 bms_afe_hw_access_remaining_seconds(void);

#endif /* 头文件保护：BMS_AFE_HW_ACCESS_H_。 */
