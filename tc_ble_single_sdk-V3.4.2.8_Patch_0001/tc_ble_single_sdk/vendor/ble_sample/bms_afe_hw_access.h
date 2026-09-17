#ifndef BMS_AFE_HW_ACCESS_H_
#define BMS_AFE_HW_ACCESS_H_

#include "tl_common.h"

#define BMS_AFE_HW_ACCESS_MODBUS_FUNC       0x42u
#define BMS_AFE_HW_ACCESS_UNLOCK_MAGIC      0x41464548UL /* "AFEH" */
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

int bms_afe_hw_access_modbus_on_frame(const u8 *req,
                                      u32 req_len,
                                      u8 *rsp,
                                      u32 *rsp_len);
/* Internal complete-frame entry: same validation/apply/rollback as 0x10. */
u8 bms_afe_hw_write_complete_frame(const u8 *frame, u32 length);
void bms_afe_hw_access_poll(void);
void bms_afe_hw_access_close(void);
u8 bms_afe_hw_access_is_active(void);
u16 bms_afe_hw_access_remaining_seconds(void);

#endif /* BMS_AFE_HW_ACCESS_H_ */
