#ifndef FACTORY_TEST_H_
#define FACTORY_TEST_H_

#include "tl_common.h"

#define FACTORY_TEST_MODBUS_FUNC            0x41u
#define FACTORY_TEST_MODBUS_ADDRESS         0x01u /* 与 modbus_rtu.c 的 MB_ADDR 保持一致 */
#define FACTORY_TEST_UNLOCK_MAGIC           0x46414354UL
#define FACTORY_TEST_SESSION_TIMEOUT_MS     8000u
#define FACTORY_TEST_PROTOCOL_VERSION       1u

enum factory_test_command
{
    FACTORY_CMD_OPEN = 0x01,
    FACTORY_CMD_HEARTBEAT = 0x02,
    FACTORY_CMD_INJECT = 0x03,
    FACTORY_CMD_CLEAR = 0x04,
    FACTORY_CMD_CLOSE = 0x05,
    FACTORY_CMD_STATUS = 0x06
};

enum factory_test_status
{
    FACTORY_STATUS_OK = 0,
    FACTORY_STATUS_BAD_REQUEST = 1,
    FACTORY_STATUS_AUTH_REQUIRED = 2,
    FACTORY_STATUS_UNSUPPORTED = 3,
    FACTORY_STATUS_OUT_OF_RANGE = 4
};

enum factory_test_input_kind
{
    FACTORY_INPUT_CELL_MV = 1,
    FACTORY_INPUT_PACK_CV = 2,
    FACTORY_INPUT_CURRENT_TENTH_A = 3,
    FACTORY_INPUT_TEMP_DECI_RAW = 4,
    FACTORY_INPUT_MOS_TEMP_DECI_RAW = 5,
    FACTORY_INPUT_SOC_PERCENT = 6
};

#define FACTORY_INPUT_MASK_CELL       0x0001u
#define FACTORY_INPUT_MASK_PACK       0x0002u
#define FACTORY_INPUT_MASK_CURRENT    0x0004u
#define FACTORY_INPUT_MASK_TEMP       0x0008u
#define FACTORY_INPUT_MASK_MOS_TEMP   0x0010u
#define FACTORY_INPUT_MASK_SOC        0x0020u

typedef struct
{
    u16 cell_mv[32];
    u16 cell_max_mv;
    u16 cell_min_mv;
    u16 cell_delta_mv;
    u16 pack_cv;
    u16 charge_current_tenth_a;
    u16 discharge_current_tenth_a;
    u16 temp_max_deci_raw;
    u16 temp_min_deci_raw;
    u16 mos_temp_deci_raw;
    u16 soc_percent;
    u16 injection_mask;
} factory_test_effective_measurement_t;

int factory_test_modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len);
void factory_test_poll(void);
void factory_test_clear_session(void);
void factory_test_get_effective_measurement(factory_test_effective_measurement_t *measurement);
u8 factory_test_is_active(void);

#endif
