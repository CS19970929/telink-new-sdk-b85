#pragma once
#include "tl_common.h"
#include "conf.h"
#include "bms_afe_backend.h"

/* Includes the proprietary 0x7F echo frame; ordinary Modbus RTU fits in 256. */
#define MODBUS_RTU_FRAME_CAPACITY 268u

u16 mb_crc16(const u8 *buf, u32 len);
int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len);

#define BMS_SOFTWARE_VERSION_PREFIX             "a011-"
#define BMS_SOFTWARE_VERSION_SUFFIX             "-d011v1p0"

#define PROD_SN_REG_BASE                        0xc002
#define PROD_SN_REG_COUNT                       16
#define PROD_HW_VER_REG_BASE                    (PROD_SN_REG_BASE + 16)
#define PROD_HW_VER_REG_COUNT                   16
#define PROD_SW_VER_REG_BASE                    (PROD_HW_VER_REG_BASE + 16)
#define PROD_SW_VER_REG_COUNT                   16
#define PRODUCT_ID_LENGTH_MAX                   32

typedef struct {
    u8 BMS_SerialNumber[PRODUCT_ID_LENGTH_MAX];
    u8 BMS_HardWareVersion[PRODUCT_ID_LENGTH_MAX];
    u8 BMS_SoftWareVersion[PRODUCT_ID_LENGTH_MAX];
} PRODUCTION_ID_INFO;


#define BMS_AFE_HW_PROFILE_REG_BASE  0x2500u
#define BMS_AFE_HW_PROFILE_WORDS     35u
#define BMS_AFE_HW_PROFILE_REG_COUNT 40u
