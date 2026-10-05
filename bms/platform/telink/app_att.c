/* 文件功能：BLE GATT 属性表与 Modbus 收发桥接；复用 RTU 解析器，将响应按 Notify 负载分片。
 * bms/platform/telink/app_att.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_debug_log.h"
#include "bms_afe_backend.h"
/********************************************************************************************************
 * @file    app_att.c
 *
 * @brief   This is the source file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    06,2022
 *
 * @par     Copyright (c) 2022, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#include "tl_common.h"

#include "stack/ble/ble.h"
#include "app.h"
#include "app_att.h"
#include "conf.h"
#include "modbus_rtu.h"
#include "btname_modbus.h"

////////////////////// SPP ////////////////////////////////////
static const u8 TelinkSppServiceUUID[16]	      	    = WRAPPING_BRACES(TELINK_SPP_UUID_SERVICE);
static const u8 TelinkSppDataServer2ClientUUID[16]      = WRAPPING_BRACES(TELINK_SPP_DATA_SERVER2CLIENT);
static const u8 TelinkSppDataClient2ServerUUID[16]      = WRAPPING_BRACES(TELINK_SPP_DATA_CLIENT2SERVER);

// Spp data from Server to Client characteristic variables
static u8 SppDataServer2ClientDataCCC[2]  				= {0};
//this array will not used for sending data(directly calling HandleValueNotify API), so cut array length from 20 to 1, saving some SRAM
static u8 SppDataServer2ClientData[1] 					= {0};  //SppDataServer2ClientData[20]
// Spp data from Client to Server characteristic variables
//this array will not used for receiving data(data processed by Attribute Write CallBack function), so cut array length from 20 to 1, saving some SRAM
static u8 SppDataClient2ServerData[1] 					= {0};  //SppDataClient2ServerData[20]

//SPP data descriptor
static const u8 TelinkSPPS2CDescriptor[] 		 		= "Telink SPP: Module->Phone";
static const u8 TelinkSPPC2SDescriptor[]        		= "Telink SPP: Phone->Module";

//// Telink spp  attribute values
// static const u8 TelinkSppDataServer2ClientCharVal[19] = {
// 	CHAR_PROP_READ | CHAR_PROP_NOTIFY,
// 	U16_LO(SPP_SERVER_TO_CLIENT_DP_H), U16_HI(SPP_SERVER_TO_CLIENT_DP_H),
// 	TELINK_SPP_DATA_SERVER2CLIENT
// };
// static const u8 TelinkSppDataClient2ServerCharVal[19] = {
// 	CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP,
// 	U16_LO(SPP_CLIENT_TO_SERVER_DP_H), U16_HI(SPP_CLIENT_TO_SERVER_DP_H),
// 	TELINK_SPP_DATA_CLIENT2SERVER
// };

static const u8 TelinkSppDataServer2ClientCharVal[19] = {
	CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_WRITE,
	U16_LO(SPP_SERVER_TO_CLIENT_DP_H), U16_HI(SPP_SERVER_TO_CLIENT_DP_H),
	TELINK_SPP_DATA_SERVER2CLIENT
};
static const u8 TelinkSppDataClient2ServerCharVal[19] = {
	CHAR_PROP_READ | CHAR_PROP_NOTIFY,
	U16_LO(SPP_CLIENT_TO_SERVER_DP_H), U16_HI(SPP_CLIENT_TO_SERVER_DP_H),
	TELINK_SPP_DATA_CLIENT2SERVER
};

static const u16 clientCharacterCfgUUID = GATT_UUID_CLIENT_CHAR_CFG;

static const u16 userdesc_UUID	= GATT_UUID_CHAR_USER_DESC;

static const u16 serviceChangeUUID = GATT_UUID_SERVICE_CHANGE;

static const u16 my_primaryServiceUUID = GATT_UUID_PRIMARY_SERVICE;

static const u16 my_characterUUID = GATT_UUID_CHARACTER;

static const u16 my_devServiceUUID = SERVICE_UUID_DEVICE_INFORMATION;

static const u16 my_PnPUUID = CHARACTERISTIC_UUID_PNP_ID;

static const u16 my_devNameUUID = GATT_UUID_DEVICE_NAME;

static const u16 my_gapServiceUUID = SERVICE_UUID_GENERIC_ACCESS;

static const u16 my_appearanceUUID = GATT_UUID_APPEARANCE;

static const u16 my_periConnParamUUID = GATT_UUID_PERI_CONN_PARAM;

static const u16 my_appearance = GAP_APPEARE_UNKNOWN;

static const u16 my_gattServiceUUID = SERVICE_UUID_GENERIC_ATTRIBUTE;

/* BLE GAP 连接参数按协议顺序编码：intervalMin、intervalMax、latency、timeout。 */
static const u16 my_periConnParameters[4] = {20, 40, 0, 1000};

_attribute_data_retention_	static u16 serviceChangeVal[2] = {0};

_attribute_data_retention_	static u8 serviceChangeCCC[2] = {0,0};

//static const u8 my_devName[] = {'t','S','a','m','p','l','e'};
// static const u8 my_devName[BTNAME_TOTAL_MAX_LEN] = DEV_NAME_STR;
u8 my_devName[BTNAME_TOTAL_MAX_LEN] = "BT_default";

static const u8 my_PnPtrs [] = {0x02, 0x8a, 0x24, 0x66, 0x82, 0x01, 0x00};

//////////////////////// Battery /////////////////////////////////////////////////
static const u16 my_batServiceUUID        = SERVICE_UUID_BATTERY;
static const u16 my_batCharUUID       	  = CHARACTERISTIC_UUID_BATTERY_LEVEL;
_attribute_data_retention_	static u8 batteryValueInCCC[2] = {0,0};
_attribute_data_retention_	static u8 my_batVal[1] 	= {99};

#if (BLE_OTA_SERVER_ENABLE)
//////////////////////// OTA //////////////////////////////////
static const  u8 my_OtaServiceUUID[16]				= WRAPPING_BRACES(TELINK_OTA_UUID_SERVICE);
static const  u8 my_OtaUUID[16]						= WRAPPING_BRACES(TELINK_SPP_DATA_OTA);
_attribute_data_retention_	static 		  u8 my_OtaData 						= 0x00;
_attribute_data_retention_	static 		  u8 otaDataCCC[2] 						= {0,0};
static const  u8 my_OtaName[] 						= {'O', 'T', 'A'};
#endif

// Include attribute (Battery service)

//// GAP attribute values
static const u8 my_devNameCharVal[5] = {
	CHAR_PROP_READ | CHAR_PROP_NOTIFY,
	U16_LO(GenericAccess_DeviceName_DP_H), U16_HI(GenericAccess_DeviceName_DP_H),
	U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)
};
static const u8 my_appearanceCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(GenericAccess_Appearance_DP_H), U16_HI(GenericAccess_Appearance_DP_H),
	U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE)
};
static const u8 my_periConnParamCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(CONN_PARAM_DP_H), U16_HI(CONN_PARAM_DP_H),
	U16_LO(GATT_UUID_PERI_CONN_PARAM), U16_HI(GATT_UUID_PERI_CONN_PARAM)
};

//// GATT attribute values
static const u8 my_serviceChangeCharVal[5] = {
	CHAR_PROP_INDICATE,
	U16_LO(GenericAttribute_ServiceChanged_DP_H), U16_HI(GenericAttribute_ServiceChanged_DP_H),
	U16_LO(GATT_UUID_SERVICE_CHANGE), U16_HI(GATT_UUID_SERVICE_CHANGE)
};

//// device Information  attribute values
static const u8 my_PnCharVal[5] = {
	CHAR_PROP_READ,
	U16_LO(DeviceInformation_pnpID_DP_H), U16_HI(DeviceInformation_pnpID_DP_H),
	U16_LO(CHARACTERISTIC_UUID_PNP_ID), U16_HI(CHARACTERISTIC_UUID_PNP_ID)
};

//// Battery attribute values
static const u8 my_batCharVal[5] = {
	CHAR_PROP_READ | CHAR_PROP_NOTIFY,
	U16_LO(BATT_LEVEL_INPUT_DP_H), U16_HI(BATT_LEVEL_INPUT_DP_H),
	U16_LO(CHARACTERISTIC_UUID_BATTERY_LEVEL), U16_HI(CHARACTERISTIC_UUID_BATTERY_LEVEL)
};

#if (BLE_OTA_SERVER_ENABLE)
//// OTA attribute values
static const u8 my_OtaCharVal[19] = {
	CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_NOTIFY | CHAR_PROP_WRITE,
	U16_LO(OTA_CMD_OUT_DP_H), U16_HI(OTA_CMD_OUT_DP_H),
	TELINK_SPP_DATA_OTA,
};
#endif

/**
 * @brief      write callback of Attribute of TelinkSppDataClient2ServerUUID
 * @param[in]  para - rf_packet_att_write_t
 * @return     0
 */
static u8 ble_rsp_buf[MODBUS_RTU_FRAME_CAPACITY];

#define TELINK_NOTIFY_PAYLOAD 20
/* 按当前 ATT 负载逐片入 SDK 队列；首次入队失败立即返回，已入队不代表对端完整接收。 */
static ble_sts_t notify_big_packet(u16 conn, u16 handle, u8 *data, u16 len)
{
	u16 offset = 0;

	while (offset < len)
	{
		u8 chunk = (len - offset) > TELINK_NOTIFY_PAYLOAD ? TELINK_NOTIFY_PAYLOAD : (len - offset);

		ble_sts_t ret = blc_gatt_pushHandleValueNotify(
			conn,
			handle,
			data + offset,
			chunk);

		if (ret != BLE_SUCCESS)
		{
            BMS_LOG(BMS_LOG_WARN, BMS_LOG_COMM, BMS_LOG_BLE_NOTIFY_FAIL, ret, offset);
			return ret;
		}
		offset += chunk;
		// Telink 閻楄锟窖嶇窗闂囷拷鐟曚胶绮扮�靛湱顏稉锟介悙瑙勬闂傝揪绱濋崥锕�鍨� notify 娴兼俺顫﹂崥锟�
		// sleep_us(800);   // 0.8ms鐡掑啿顧勭�瑰鍙�
	}

	return BLE_SUCCESS;
}
static int module_onReceiveData(void *para)
{
	rf_packet_att_write_t *p = (rf_packet_att_write_t*)para;
	u16 len;
	const u8 *data;

	if (p == NULL || p->l2capLen <= 3u) return 0;
	len = (u16)(p->l2capLen - 3u);
	if (len > MODBUS_RTU_FRAME_CAPACITY) return 0;
	data = (const u8 *)&p->value;

	{
		u32 rsp_len = 0u;
		int ok = modbus_on_frame(data, len, ble_rsp_buf, &rsp_len);
#if BMS_DEBUG_LOG_ENABLE
        if (!bms_debug_log_is_read(data, len))
            BMS_LOG(ok ? BMS_LOG_DEBUG : BMS_LOG_WARN, BMS_LOG_COMM, BMS_LOG_BLE_FRAME,
                    ((uint32_t)len << 16) | (len >= 2u ? data[1] : 0u), rsp_len);
#endif

		if (ok && rsp_len)
		{
			(void)notify_big_packet(BLS_CONN_HANDLE,
							SPP_CLIENT_TO_SERVER_DP_H,
							ble_rsp_buf,
							(u16)rsp_len);
		}
	}

	return 0;
}

// TM : to modify
static const attribute_t my_Attributes[] = {

	{ATT_END_H - 1, 0,0,0,0,0,0,0},	// total num of attribute

	// 0x0001 - 0x0007  GAP
	{7,ATT_PERMISSIONS_READ,2,2,(u8*)(&my_primaryServiceUUID), 	(u8*)(&my_gapServiceUUID), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof(my_devNameCharVal),(u8*)(&my_characterUUID), (u8*)(my_devNameCharVal), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof(my_devName), (u8*)(&my_devNameUUID), (u8*)(my_devName), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof(my_appearanceCharVal),(u8*)(&my_characterUUID), (u8*)(my_appearanceCharVal), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof (my_appearance), (u8*)(&my_appearanceUUID), 	(u8*)(&my_appearance), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof(my_periConnParamCharVal),(u8*)(&my_characterUUID), (u8*)(my_periConnParamCharVal), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof (my_periConnParameters),(u8*)(&my_periConnParamUUID), 	(u8*)(&my_periConnParameters), 0, 0},

	// 0x0008 - 0x000B GATT
	{4,ATT_PERMISSIONS_READ,2,2,(u8*)(&my_primaryServiceUUID), 	(u8*)(&my_gattServiceUUID), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof(my_serviceChangeCharVal),(u8*)(&my_characterUUID), 		(u8*)(my_serviceChangeCharVal), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof (serviceChangeVal), (u8*)(&serviceChangeUUID), 	(u8*)(&serviceChangeVal), 0, 0},
	{0,ATT_PERMISSIONS_RDWR,2,sizeof (serviceChangeCCC),(u8*)(&clientCharacterCfgUUID), (u8*)(serviceChangeCCC), 0, 0},

	// 0x000C - 0x000E  Device Information Service
	{3,ATT_PERMISSIONS_READ,2,2,(u8*)(&my_primaryServiceUUID), 	(u8*)(&my_devServiceUUID), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof(my_PnCharVal),(u8*)(&my_characterUUID), (u8*)(my_PnCharVal), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof (my_PnPtrs),(u8*)(&my_PnPUUID), (u8*)(my_PnPtrs), 0, 0},

	////////////////////////////////////// Battery Service /////////////////////////////////////////////////////
	// 0x002A - 0x002D
	{4,ATT_PERMISSIONS_READ,2,2,(u8*)(&my_primaryServiceUUID), 	(u8*)(&my_batServiceUUID), 0, 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof(my_batCharVal),(u8*)(&my_characterUUID), (u8*)(my_batCharVal), 0, 0},				//prop
	{0,ATT_PERMISSIONS_READ,2,sizeof(my_batVal),(u8*)(&my_batCharUUID), 	(u8*)(my_batVal), 0, 0},	//value
	{0,ATT_PERMISSIONS_RDWR,2,sizeof(batteryValueInCCC),(u8*)(&clientCharacterCfgUUID), 	(u8*)(batteryValueInCCC), 0, 0},	//value

	////////////////spp/////////////////
	// 000f - 0016 SPP
	{8,ATT_PERMISSIONS_READ,2,16,(u8*)(&my_primaryServiceUUID), 	(u8*)(&TelinkSppServiceUUID), 0},
	{0,ATT_PERMISSIONS_READ,2,sizeof(TelinkSppDataServer2ClientCharVal),(u8*)(&my_characterUUID), 		(u8*)(TelinkSppDataServer2ClientCharVal), 0},				//prop
	// {0,ATT_PERMISSIONS_READ,16,sizeof(SppDataServer2ClientData),(u8*)(&TelinkSppDataServer2ClientUUID), (u8*)(SppDataServer2ClientData), 0},	//value
	{0,ATT_PERMISSIONS_RDWR,16,sizeof(SppDataServer2ClientData),(u8*)(&TelinkSppDataServer2ClientUUID), (u8*)(SppDataServer2ClientData), (att_readwrite_callback_t)&module_onReceiveData},	//value
	// {0,ATT_PERMISSIONS_RDWR,2,2,(u8*)&clientCharacterCfgUUID,(u8*)(&SppDataServer2ClientDataCCC)},
	{0,ATT_PERMISSIONS_READ,2,sizeof(TelinkSPPS2CDescriptor),(u8*)&userdesc_UUID,(u8*)(&TelinkSPPS2CDescriptor)},
	{0,ATT_PERMISSIONS_READ,2,sizeof(TelinkSppDataClient2ServerCharVal),(u8*)(&my_characterUUID), 		(u8*)(TelinkSppDataClient2ServerCharVal), 0},				//prop
	// {0,ATT_PERMISSIONS_RDWR,16,sizeof(SppDataClient2ServerData),(u8*)(&TelinkSppDataClient2ServerUUID), (u8*)(SppDataClient2ServerData), (att_readwrite_callback_t)&module_onReceiveData},	//value
	{0,ATT_PERMISSIONS_RDWR,16,sizeof(SppDataClient2ServerData),(u8*)(&TelinkSppDataClient2ServerUUID), (u8*)(SppDataClient2ServerData), 0},	//value
	{0,ATT_PERMISSIONS_RDWR,2,2,(u8*)&clientCharacterCfgUUID,(u8*)(&SppDataServer2ClientDataCCC)},
	{0,ATT_PERMISSIONS_READ,2,sizeof(TelinkSPPC2SDescriptor),(u8*)&userdesc_UUID,(u8*)(&TelinkSPPC2SDescriptor)},

	// ////////////////spp/////////////////
	// // 000f - 0016 SPP
	// {8,ATT_PERMISSIONS_READ,2,16,(u8*)(&my_primaryServiceUUID), 	(u8*)(&TelinkSppServiceUUID), 0},
	// {0,ATT_PERMISSIONS_READ,2,sizeof(TelinkSppDataServer2ClientCharVal),(u8*)(&my_characterUUID), 		(u8*)(TelinkSppDataServer2ClientCharVal), 0},				//prop
	// // {0,ATT_PERMISSIONS_READ,16,sizeof(SppDataServer2ClientData),(u8*)(&TelinkSppDataServer2ClientUUID), (u8*)(SppDataServer2ClientData), 0},	//value
	// {0,ATT_PERMISSIONS_RDWR,16,sizeof(SppDataServer2ClientData),(u8*)(&TelinkSppDataServer2ClientUUID), (u8*)(SppDataServer2ClientData), (att_readwrite_callback_t)&module_onReceiveData},	//value
	// // {0,ATT_PERMISSIONS_RDWR,2,2,(u8*)&clientCharacterCfgUUID,(u8*)(&SppDataServer2ClientDataCCC)},
	// {0,ATT_PERMISSIONS_READ,2,sizeof(TelinkSPPS2CDescriptor),(u8*)&userdesc_UUID,(u8*)(&TelinkSPPS2CDescriptor)},
	// {0,ATT_PERMISSIONS_READ,2,sizeof(TelinkSppDataClient2ServerCharVal),(u8*)(&my_characterUUID), 		(u8*)(TelinkSppDataClient2ServerCharVal), 0},				//prop
	// // {0,ATT_PERMISSIONS_RDWR,16,sizeof(SppDataClient2ServerData),(u8*)(&TelinkSppDataClient2ServerUUID), (u8*)(SppDataClient2ServerData), (att_readwrite_callback_t)&module_onReceiveData},	//value
	// {0,ATT_PERMISSIONS_RDWR,16,sizeof(SppDataClient2ServerData),(u8*)(&TelinkSppDataClient2ServerUUID), (u8*)(SppDataClient2ServerData), 0},	//value
	// {0,ATT_PERMISSIONS_RDWR,2,2,(u8*)&clientCharacterCfgUUID,(u8*)(&SppDataServer2ClientDataCCC)},
	// {0,ATT_PERMISSIONS_READ,2,sizeof(TelinkSPPC2SDescriptor),(u8*)&userdesc_UUID,(u8*)(&TelinkSPPC2SDescriptor)},

#if (BLE_OTA_SERVER_ENABLE)
	////////////////////////////////////// OTA /////////////////////////////////////////////////////
	// 0x002E - 0x0032
	{5,ATT_PERMISSIONS_READ, 2,16,(u8*)(&my_primaryServiceUUID), 	(u8*)(&my_OtaServiceUUID), 0, 0},
	{0,ATT_PERMISSIONS_READ, 2, sizeof(my_OtaCharVal),(u8*)(&my_characterUUID), (u8*)(my_OtaCharVal), 0, 0},				//prop
	{0,ATT_PERMISSIONS_RDWR,16,sizeof(my_OtaData),(u8*)(&my_OtaUUID),	(&my_OtaData), &otaWrite, NULL},				//value
	{0,ATT_PERMISSIONS_RDWR,2,sizeof(otaDataCCC),(u8*)(&clientCharacterCfgUUID), 	(u8*)(otaDataCCC), 0, 0},				//value
	{0,ATT_PERMISSIONS_READ, 2,sizeof (my_OtaName),(u8*)(&userdesc_UUID), (u8*)(my_OtaName), 0, 0},
#endif
};

/**
 * @brief      Initialize the attribute table
 * @param[in]  none
 * @return     none
 */
void	my_att_init(void)
{
	bls_att_setAttributeTable((u8 *)my_Attributes);
}
