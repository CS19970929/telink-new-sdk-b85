/*
 * 文件功能：BLE GATT 属性表与 Modbus 收发桥接；复用 RTU 解析器，
 * 将响应按 Notify 负载分片。
 * bms/platform/telink/app_att.h；实际编译归属见各产品 sources.txt。
 */
/********************************************************************************************************
 * @file    app_att.h
 *
 * @brief   BLE SDK 应用接口头文件。
 *
 * @author  BLE GROUP
 * @date    06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#ifndef APP_ATT_H_
#define APP_ATT_H_




// ATT 句柄定义。
typedef enum
{
	ATT_H_START = 0,


	// GAP 服务。
	/**********************************************************************************************/
	GenericAccess_PS_H, 					// UUID 为 2800，值为 uuid 1800。
	GenericAccess_DeviceName_CD_H,			// UUID 为 2803，属性为读取与通知。
	GenericAccess_DeviceName_DP_H,			// UUID 为 2A00，值为设备名称。
	GenericAccess_Appearance_CD_H,			// UUID 为 2803，属性为读取。
	GenericAccess_Appearance_DP_H,			// UUID 为 2A01，值为外观。
	CONN_PARAM_CD_H,						// UUID 为 2803，属性为读取。
	CONN_PARAM_DP_H,						// UUID 为 2A04，值为连接参数。


	// GATT 服务。
	/**********************************************************************************************/
	GenericAttribute_PS_H,					// UUID 为 2800，值为 uuid 1801。
	GenericAttribute_ServiceChanged_CD_H,	// UUID 为 2803，属性为指示。
	GenericAttribute_ServiceChanged_DP_H,   // UUID 为 2A05，值为服务变化。
	GenericAttribute_ServiceChanged_CCB_H,	// UUID 为 2902，值为 serviceChangeCCC。


	// 设备信息服务。
	/**********************************************************************************************/
	DeviceInformation_PS_H,					// UUID 为 2800，值为 uuid 180A。
	DeviceInformation_pnpID_CD_H,			// UUID 为 2803，属性为读取。
	DeviceInformation_pnpID_DP_H,			// UUID: 2A50, 取值: PnPtrs


	// HID ////
	/**********************************************************************************************/
	// HID_PS_H, //UUID: 2800, 取值: uuid 1812

	// 包含服务
	// HID_INCLUDE_H, //UUID: 2802, 取值: 包含服务

	// 协议模式
	// HID_PROTOCOL_MODE_CD_H, //UUID: 2803, 取值: 属性: 读取 | 无响应写入
	// HID_PROTOCOL_MODE_DP_H, //UUID: 2A4E, 取值: protocolMode

	// 启动协议键盘输入报告
	// HID_BOOT_KB_REPORT_INPUT_CD_H, //UUID: 2803, 取值: 属性: 读取 | 通知
	// HID_BOOT_KB_REPORT_INPUT_DP_H, //UUID: 2A22, 取值: bootKeyInReport
	// HID_BOOT_KB_REPORT_INPUT_CCB_H, //UUID: 2902, 取值: bootKeyInReportCCC

	// 启动协议键盘输出报告
	// HID_BOOT_KB_REPORT_OUTPUT_CD_H, //UUID: 2803, 取值: 属性: 读取 | 写入| 无响应写入
	// HID_BOOT_KB_REPORT_OUTPUT_DP_H, //UUID: 2A32, 取值: bootKeyOutReport

	// 消费控制输入报告
	// HID_CONSUME_REPORT_INPUT_CD_H, //UUID: 2803, 取值: 属性: 读取 | 通知
	// HID_CONSUME_REPORT_INPUT_DP_H, //UUID: 2A4D, 取值: reportConsumerIn
	// HID_CONSUME_REPORT_INPUT_CCB_H, //UUID: 2902, 取值: reportConsumerInCCC
	// HID_CONSUME_REPORT_INPUT_REF_H, //UUID: 2908 取值: REPORT_ID_CONSUMER, TYPE_INPUT

	// 键盘输入报告
	// HID_NORMAL_KB_REPORT_INPUT_CD_H, //UUID: 2803, 取值: 属性: 读取 | 通知
	// HID_NORMAL_KB_REPORT_INPUT_DP_H, //UUID: 2A4D, 取值: reportKeyIn
	// HID_NORMAL_KB_REPORT_INPUT_CCB_H, //UUID: 2902, 取值: reportKeyInInCCC
	// HID_NORMAL_KB_REPORT_INPUT_REF_H, //UUID: 2908 取值: REPORT_ID_KEYBOARD,
	// TYPE_INPUT

	// 键盘输出报告
	// HID_NORMAL_KB_REPORT_OUTPUT_CD_H, //UUID: 2803, 取值: 属性: 读取 | 写入| 无响应写
	// 入
	// HID_NORMAL_KB_REPORT_OUTPUT_DP_H, //UUID: 2A4D, 取值: reportKeyOut
	// HID_NORMAL_KB_REPORT_OUTPUT_REF_H, //UUID: 2908 取值: REPORT_ID_KEYBOARD,
	// TYPE_OUTPUT

	// 报告映射
	// HID_REPORT_MAP_CD_H, //UUID: 2803, 取值: 属性: 读取
	// HID_REPORT_MAP_DP_H, //UUID: 2A4B, 取值: reportKeyIn
	// HID_REPORT_MAP_EXT_REF_H, //UUID: 2907 取值: extService

	// HID 信息
	// HID_INFORMATION_CD_H, //UUID: 2803, 取值: 属性: 读取
	// HID_INFORMATION_DP_H, //UUID: 2A4A 取值: hidInformation

	// 控制点
	// HID_CONTROL_POINT_CD_H, //UUID: 2803, 取值: 属性: 无响应写入
	// HID_CONTROL_POINT_DP_H, //UUID: 2A4C 取值: controlPoint


	// 电池服务 ////
	/**********************************************************************************************/
	BATT_PS_H, 								// UUID: 2800, 取值: uuid 180f
	BATT_LEVEL_INPUT_CD_H,					// UUID 为 2803，属性为读取与通知。
	BATT_LEVEL_INPUT_DP_H,					// UUID: 2A19 取值: batVal
	BATT_LEVEL_INPUT_CCB_H,					// UUID: 2902, 取值: batValCCC

	// SPP ////
	/**********************************************************************************************/
	SPP_PS_H, 							 // UUID: 2800, 取值: Telink SPP 服务 UUID

	// 服务端到客户端
	SPP_SERVER_TO_CLIENT_CD_H,		     // UUID: 2803, 取值: 属性: 读取 | 通知
	SPP_SERVER_TO_CLIENT_DP_H,			 // UUID: Telink SPP 服务端到客户端 UUID, 取值:
	// SppDataServer2ClientData
	// SPP_SERVER_TO_CLIENT_CCB_H, //UUID: 2902, 取值: SppDataServer2ClientDataCCC
	SPP_SERVER_TO_CLIENT_DESC_H,		 // UUID: 2901, 取值: TelinkSPPS2CDescriptor

	// 客户端到服务端
	SPP_CLIENT_TO_SERVER_CD_H,		     // UUID: 2803, 取值: 属性: 读取 | 无响应写入
	SPP_CLIENT_TO_SERVER_DP_H,			 // UUID: Telink SPP 客户端到服务端 UUID, 取值:
	// SppDataClient2ServerData
	SPP_SERVER_TO_CLIENT_CCB_H,			 // UUID: 2902, 取值:
	// SppDataServer2ClientDataCCC
	SPP_CLIENT_TO_SERVER_DESC_H,		 // UUID: 2901, 取值: TelinkSPPC2SDescriptor

#if (BLE_OTA_SERVER_ENABLE)
	// Ota ////
	/**********************************************************************************************/
	OTA_PS_H, 								// UUID: 2800, 取值: Telink OTA 服务 UUID
	OTA_CMD_OUT_CD_H,						// UUID: 2803, 取值: 属性: 读取 | 无响应写入
	// | 通知
	OTA_CMD_OUT_DP_H,						// UUID: Telink OTA 特征 UUID, 取值: otaData
	OTA_CMD_INPUT_CCB_H,					// UUID: 2902, 取值: otaDataCCC
	OTA_CMD_OUT_DESC_H,						// UUID: 2901, 取值: otaName
#endif


	ATT_END_H,

}ATT_HANDLE;


/*
 * @brief      初始化 GATT 属性表。
 * @param[in]  无
 * @return     无
 */
/* 初始化 GATT 属性表、名称及通知相关状态。 */
void my_att_init(void);


#endif /* 条件编译结束： APP_ATT_H_ */
