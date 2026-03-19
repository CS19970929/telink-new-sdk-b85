#pragma once
#include "tl_common.h"
#include "conf.h"

u16 mb_crc16(const u8 *buf, u32 len);

// 澶勭悊涓�甯э細
// - in: req/req_len
// - out: rsp/rsp_len锛堣繑鍥?1琛ㄧず瑕佸彂閫侊紱0琛ㄧず涓嶅洖锛?
int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len);


#define  BMS_SOFTWARE_VERDION_DEFAULT   "a009-20260316-c096v1p0"  //32
#define  BMS_SERIAL_NUMBER_DEFAULT  	"hanstar"

#define PROD_SN_REG_BASE                   0xc002   // 0xE000 ~ 0xE00F
#define PROD_SN_REG_COUNT                  16

#define PROD_HW_VER_REG_BASE               (PROD_SN_REG_BASE + 16)   // 0xE010 ~ 0xE01F
#define PROD_HW_VER_REG_COUNT              16

#define PROD_SW_VER_REG_BASE               (PROD_HW_VER_REG_BASE + 16)   // 0xE020 ~ 0xE02F
#define PROD_SW_VER_REG_COUNT              16

// #define PROD_SN_LEN_REG                    0xE030
// #define PROD_HW_VER_LEN_REG                0xE031
// #define PROD_SW_VER_LEN_REG                0xE032

// #define PROD_SN_HEAD_ADDR_REG              0xE033
// #define PROD_HW_VER_HEAD_ADDR_REG          0xE034
// #define PROD_SW_VER_HEAD_ADDR_REG          0xE035

// #define PROD_SN_WRITE_FLAG_REG             0xE036
// #define PROD_HW_VER_WRITE_FLAG_REG         0xE037
// #define PROD_SW_VER_WRITE_FLAG_REG         0xE038

#define PRODUCT_ID_LENGTH_MAX 32

typedef struct {
	//均为阿斯克码
	u8 BMS_SerialNumber[PRODUCT_ID_LENGTH_MAX];			//BMS序列号
	u8 BMS_HardWareVersion[PRODUCT_ID_LENGTH_MAX];		//BMS硬件版本号
	u8 BMS_SoftWareVersion[PRODUCT_ID_LENGTH_MAX];		//BMS软件版本号

	// u16 BMS_SerialNumberLength;			//BMS序列号地址				//意义不大，末端全部填0，阿斯克码为空
	// u16 BMS_HardWareVersionLength;		//BMS硬件版本号地址			//意义不大，末端全部填0，阿斯克码为空
	// u16 BMS_SoftWareVersionLength;		//BMS软件版本号地址			//意义不大，末端全部填0，阿斯克码为空
	
	// u16 BMS_SerialNumberHeadAdress;		//BMS序列号地址
	// u16 BMS_HardWareVersionHeadAdress;	//BMS硬件版本号地址
	// u16 BMS_SoftWareVersionHeadAdress;	//BMS软件版本号地址

	// u8 BMS_SerialNumber_WriteFlag;
	// u8 BMS_HardWareVersion_WriteFlag;
	// u8 BMS_SoftWareVersion_WriteFlag;
}PRODUCTION_ID_INFO;

extern PRODUCTION_ID_INFO ProductionInfor;