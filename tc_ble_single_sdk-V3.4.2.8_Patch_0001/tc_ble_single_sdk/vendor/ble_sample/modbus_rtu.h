#pragma once
#include "tl_common.h"
#include "conf.h"
//#include "build_version_auto.h"

u16 mb_crc16(const u8 *buf, u32 len);

// 处理一帧：
// - in: req/req_len
// - out: rsp/rsp_len（返�?1表示要发送；0表示不回�?
int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len);


#define  BMS_SOFTWARE_VERSION_PREFIX    "a009-"
#define  BMS_SOFTWARE_VERSION_SUFFIX    "-c096v1p0"
// #define  BMS_SOFTWARE_VERDION_DEFAULT   BMS_SOFTWARE_VERSION_PREFIX BMS_SOFTWARE_BUILD_TIMESTAMP BMS_SOFTWARE_VERSION_SUFFIX  //32

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
	//��Ϊ��˹����
	u8 BMS_SerialNumber[PRODUCT_ID_LENGTH_MAX];			//BMS���к�
	u8 BMS_HardWareVersion[PRODUCT_ID_LENGTH_MAX];		//BMSӲ���汾��
	u8 BMS_SoftWareVersion[PRODUCT_ID_LENGTH_MAX];		//BMS����汾��

	// u16 BMS_SerialNumberLength;			//BMS���кŵ�ַ				//���岻��ĩ��ȫ����0����˹����Ϊ��
	// u16 BMS_HardWareVersionLength;		//BMSӲ���汾�ŵ�ַ			//���岻��ĩ��ȫ����0����˹����Ϊ��
	// u16 BMS_SoftWareVersionLength;		//BMS����汾�ŵ�ַ			//���岻��ĩ��ȫ����0����˹����Ϊ��
	
	// u16 BMS_SerialNumberHeadAdress;		//BMS���кŵ�ַ
	// u16 BMS_HardWareVersionHeadAdress;	//BMSӲ���汾�ŵ�ַ
	// u16 BMS_SoftWareVersionHeadAdress;	//BMS����汾�ŵ�ַ

	// u8 BMS_SerialNumber_WriteFlag;
	// u8 BMS_HardWareVersion_WriteFlag;
	// u8 BMS_SoftWareVersion_WriteFlag;
}PRODUCTION_ID_INFO;

extern PRODUCTION_ID_INFO ProductionInfor;
