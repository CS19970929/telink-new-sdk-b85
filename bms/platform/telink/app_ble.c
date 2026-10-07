/********************************************************************************************************
 * @file    app_ble.c
 *
 * @brief   BLE SDK 应用源文件。
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
/* 文件功能：Telink BLE 生命周期、FIFO、Flash 回调与 SDK 初始化；业务启动在 app_init。 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app_config.h"
#include "app.h"
#include "bms_product.h"
#include "ble_ota.h"
#include "app_att.h"
#include "bms_debug_log.h"
#include "bms_state.h"
#include "bms_afe_hw_access.h"
#include "bms_storage_platform.h"
#include "bms_parameters.h"
#include <string.h>

static void task_connect(u8 e, u8 *p, int n);
static void task_terminate(u8 e, u8 *p, int n);
static void task_suspend_exit(u8 e, u8 *p, int n);
static void task_dle_exchange(u8 e, u8 *p, int n);
static int app_host_event_callback(u32 h, u8 *para, int n);
#if BLE_APP_SECURITY_ENABLE
static void app_switch_to_undirected_adv(u8 e, u8 *p, int n);
#endif
static void ble_build_adv_scanrsp(void);

#define APP_CONN_LATENCY_NORMAL 99
#define APP_CONN_LATENCY_OTA 0

#if BMS_DEBUG_LOG_ENABLE
static volatile u32 s_debug_suspend_exits;
u32 app_ble_suspend_exit_count(void) { return s_debug_suspend_exits; }
#endif

_attribute_data_retention_ u8 ota_is_working = 0;
_attribute_data_retention_ own_addr_type_t app_own_address_type = OWN_ADDRESS_PUBLIC;

/* @brief 配置链路层接收与发送 FIFO。 */
/* CAL_LL_ACL_RX_BUF_SIZE(maxRxOct) 为 maxRxOct+22，再按 16 字节对齐。 */
#define RX_FIFO_SIZE 64
/* 必须为 2 的幂且至少 4，推荐 4、8、16。 */
#define RX_FIFO_NUM 8

/* CAL_LL_ACL_TX_BUF_SIZE(maxTxOct) 为 maxTxOct+10，再按 4 字节对齐。 */
#define TX_FIFO_SIZE 40
/* 必须为 2 的幂且至少 8，推荐 8、16、32，不允许其它值。 */
#define TX_FIFO_NUM 16

_attribute_data_retention_ u8 blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_rxfifo = {
	RX_FIFO_SIZE,
	RX_FIFO_NUM,
	0,
	0,
	blt_rxfifo_b,
};

_attribute_data_retention_ u8 blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_txfifo = {
	TX_FIFO_SIZE,
	TX_FIFO_NUM,
	0,
	0,
	blt_txfifo_b,
};

u8 tbl_advData[31];
u8 tbl_advDataLen;

u8 tbl_scanRsp[31];
u8 tbl_scanRspLen;

_attribute_data_retention_ int device_in_connection_state;

_attribute_data_retention_ u32 advertise_begin_tick;

_attribute_data_retention_ u8 sendTerminate_before_enterDeep = 0;

/* 请求常规 BLE 连接参数。 */
void app_ble_request_normal_conn_param(void)
{
	if (device_in_connection_state)
	{
		bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, APP_CONN_LATENCY_NORMAL, CONN_TIMEOUT_4S);
	}
}

/* 请求 OTA 所需的 BLE 连接参数。 */
void app_ble_request_ota_conn_param(void)
{
	if (device_in_connection_state)
	{
		bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, APP_CONN_LATENCY_OTA, CONN_TIMEOUT_4S);
	}
}

/* 恢复常规 BLE 发射功率及连接设置。 */
void app_ble_restore_normal_power(void)
{
#if (BLE_APP_PM_ENABLE)
	bls_pm_setManualLatency(bls_ll_getConnectionLatency());
#endif
	app_ble_request_normal_conn_param();
}

_attribute_data_retention_ u32 latest_user_event_tick;


/* 深睡保留唤醒时恢复 SDK 与应用必要状态。 */
_attribute_ram_code_ void user_init_deepRetn(void)
{
#if (PM_DEEPSLEEP_RETENTION_ENABLE)

	blc_app_loadCustomizedParameters_deepRetn();
	blc_ll_initBasicMCU();
	rf_set_power_level_index(MY_RF_POWER_INDEX);
	blc_ll_recoverDeepRetention();
	DBG_CHN0_HIGH;
	irq_enable();

#if (UI_KEYBOARD_ENABLE)
	u32 pin[] = KB_DRIVE_PINS;
	for (int i = 0; i < (sizeof(pin) / sizeof(*pin)); i++)
	{
		cpu_set_gpio_wakeup(pin[i], Level_High, 1);
	}
#elif (UI_BUTTON_ENABLE)
	cpu_set_gpio_wakeup(SW1_GPIO, Level_Low, 1);
	cpu_set_gpio_wakeup(SW2_GPIO, Level_Low, 1);
#endif
#endif
}


/* 处理 BLE 断开并恢复广播和电源策略。 */
static void task_terminate(u8 e, u8 *p, int n)
{
	(void)e;
	(void)n;

	device_in_connection_state = 0;

	tlk_contr_evt_terminate_t *pEvt = (tlk_contr_evt_terminate_t *)p;
    /* 每次 BLE 断连撤销设备范围的授权；UART 也必须在该边界后取得新 token。 */
    bms_afe_hw_access_close();

	tlkapi_printf(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] disconnect, reason 0x%x\n", pEvt->terminate_reason);

#if (BLE_APP_PM_ENABLE)
	if (sendTerminate_before_enterDeep == 1 && !TEST_CONN_CURRENT_ENABLE)
	{
		sendTerminate_before_enterDeep = 2;
		bls_ll_setAdvEnable(BLC_ADV_DISABLE);
	}
#endif

#if (UI_LED_ENABLE && !TEST_CONN_CURRENT_ENABLE)
	gpio_write(GPIO_LED_RED, !LED_ON_LEVEL);
#endif

	advertise_begin_tick = clock_time();
}


/* 构造广播与扫描响应中的产品名称和数据。 */
static void ble_build_adv_scanrsp(void)
{
	u8 i = 0;

	// 广播包含 Flags、Appearance 和 UUID 列表，名称放在 scanRsp 中。
	i = 0;
	tbl_advData[i++] = 0x02;
	tbl_advData[i++] = 0x01;
	tbl_advData[i++] = 0x05;

	tbl_advData[i++] = 0x03;
	tbl_advData[i++] = 0x19;
	tbl_advData[i++] = 0x80;
	tbl_advData[i++] = 0x01;

	tbl_advData[i++] = 0x05;
	tbl_advData[i++] = 0x02;
	tbl_advData[i++] = 0x12;
	tbl_advData[i++] = 0x18;
	tbl_advData[i++] = 0x0F;
	tbl_advData[i++] = 0x18;

	tbl_advDataLen = i;

	i = 0;
	tbl_scanRsp[i++] = (u8)(DEV_NAME_LEN + 1);
	tbl_scanRsp[i++] = 0x09;
	memcpy(&tbl_scanRsp[i], DEV_NAME_STR, DEV_NAME_LEN);
	i += DEV_NAME_LEN;

	tbl_scanRspLen = i;
}

#if BLE_APP_SECURITY_ENABLE
/* 切换为非定向 BLE 广播。 */
static void app_switch_to_undirected_adv(u8 e, u8 *p, int n)
{
	(void)e;
	(void)p;
	(void)n;
	bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
					   ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
					   0, NULL,
					   MY_APP_ADV_CHANNEL,
					   ADV_FP_NONE);

	blc_ll_clearResolvingList();
	bls_ll_setAdvEnable(BLC_ADV_ENABLE);
}

#endif

/* 处理 BLE 连接建立并更新连接与低功耗状态。 */
static void task_connect(u8 e, u8 *p, int n)
{
	(void)e;
	(void)p;
	(void)n;
	tlk_contr_evt_connect_t *pConnEvt = (tlk_contr_evt_connect_t *)p;
	tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] connect, intA & advA:", pConnEvt->initA, 12);
	device_in_connection_state = 1;
	app_ble_request_normal_conn_param();
	latest_user_event_tick = clock_time();

#if (UI_LED_ENABLE && !TEST_CONN_CURRENT_ENABLE)
	gpio_write(GPIO_LED_RED, LED_ON_LEVEL);
#endif
}

/* 恢复 suspend 后的外设与应用时序状态。 */
static void task_suspend_exit(u8 e, u8 *p, int n)
{
#if BMS_DEBUG_LOG_ENABLE
    ++s_debug_suspend_exits; /* 此处不写环形缓存、不读 tick、不格式化、不执行 I/O。 */
#endif
	(void)e;
	(void)p;
	(void)n;
	rf_set_power_level_index(MY_RF_POWER_INDEX);
}

/* 处理 BLE 数据长度交换事件。 */
static void task_dle_exchange(u8 e, u8 *p, int n)
{
	tlk_contr_evt_dataLenExg_t *pEvt = (tlk_contr_evt_dataLenExg_t *)p;
	tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] DLE exchange", &pEvt->connEffectiveMaxRxOctets, 4);
}

/* 分派 BLE 主机事件并更新应用连接状态。 */
static int app_host_event_callback(u32 h, u8 *para, int n)
{

	u8 event = h & 0xFF;

	switch (event)
	{
	case GAP_EVT_SMP_PAIRING_BEGIN:
	{
		gap_smp_pairingBeginEvt_t *pEvt = (gap_smp_pairingBeginEvt_t *)para;
		tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] paring begin:", pEvt, sizeof(gap_smp_pairingBeginEvt_t));
	}
	break;

	case GAP_EVT_SMP_PAIRING_SUCCESS:
	{
		gap_smp_pairingSuccessEvt_t *pEvt = (gap_smp_pairingSuccessEvt_t *)para;
		tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] paring success:", pEvt, sizeof(gap_smp_pairingSuccessEvt_t));
	}
	break;

	case GAP_EVT_SMP_PAIRING_FAIL:
	{
		gap_smp_pairingFailEvt_t *pEvt = (gap_smp_pairingFailEvt_t *)para;
		tlkapi_send_string_data(APP_SMP_LOG_EN, "[APP][SMP] paring fail:", pEvt, sizeof(gap_smp_pairingFailEvt_t));
	}
	break;

	case GAP_EVT_SMP_CONN_ENCRYPTION_DONE:
	{
	}
	break;

	case GAP_EVT_SMP_SECURITY_PROCESS_DONE:
	{
	}
	break;

	case GAP_EVT_SMP_TK_DISPLAY:
	{
	}
	break;

	case GAP_EVT_SMP_TK_REQUEST_PASSKEY:
	{
	}
	break;

	case GAP_EVT_SMP_TK_REQUEST_OOB:
	{
	}
	break;

	case GAP_EVT_SMP_TK_NUMERIC_COMPARE:
	{
	}
	break;

	case GAP_EVT_ATT_EXCHANGE_MTU:
	{
		gap_gatt_mtuSizeExchangeEvt_t *pEvt = (gap_gatt_mtuSizeExchangeEvt_t *)para;
		tlkapi_send_string_data(APP_HOST_EVENT_LOG_EN, "[APP][MTU] mtu exchange", pEvt, sizeof(gap_gatt_mtuSizeExchangeEvt_t));
	}
	break;

	case GAP_EVT_GATT_HANDLE_VALUE_CONFIRM:
	{
	}
	break;

	default:
		break;
	}

	return 0;
}


#if (APP_FLASH_PROTECTION_ENABLE)

_attribute_data_retention_ u16 flash_lockBlock_cmd = 0;
_attribute_data_retention_ static u8 g_app_flash_stack_session_active = 0;

/* 判断 Flash 操作后是否需要恢复保护锁。 */
int app_flash_lock_restore_enabled(void)
{
	return (g_app_flash_stack_session_active == 0u);
}

/* 按 SDK Flash 操作阶段解锁或恢复保护范围。 */
void app_flash_protection_operation(u8 flash_op_evt, u32 op_addr_begin, u32 op_addr_end)
{
	if (flash_op_evt == FLASH_OP_EVT_APP_INITIALIZATION)
	{
		g_app_flash_stack_session_active = 0u;
		flash_protection_init();
		u32 app_lockBlock = 0;
#if (BLE_OTA_SERVER_ENABLE)
		u32 multiBootAddress = blc_ota_getCurrentUsedMultipleBootAddress();
		if (multiBootAddress == MULTI_BOOT_ADDR_0x20000)
		{
			app_lockBlock = FLASH_LOCK_FW_LOW_256K;
		}
		else if (multiBootAddress == MULTI_BOOT_ADDR_0x40000)
		{
			app_lockBlock = FLASH_LOCK_FW_LOW_512K;
		}
#if (MCU_CORE_TYPE == MCU_CORE_827x)
		else if (multiBootAddress == MULTI_BOOT_ADDR_0x80000)
		{
			if (blc_flash_capacity < FLASH_SIZE_1M)
			{
				blc_flashProt.init_err = 1;
			}
			else
			{
				app_lockBlock = FLASH_LOCK_FW_LOW_1M;
			}
		}
#endif
#else
		app_lockBlock = FLASH_LOCK_FW_LOW_256K;
#endif

		flash_lockBlock_cmd = flash_change_app_lock_block_to_flash_lock_block(app_lockBlock);

		if (blc_flashProt.init_err)
		{
			tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] flash protection initialization error!!!\n");
		}

		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] initialization, lock flash\n");
		flash_lock(flash_lockBlock_cmd);
	}
#if (BLE_OTA_SERVER_ENABLE)
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_BEGIN)
	{
		g_app_flash_stack_session_active = 1u;
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA clear old FW begin, unlock flash\n");
		flash_unlock();
	}
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_END)
	{
		g_app_flash_stack_session_active = 0u;
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA clear old FW end, restore flash locking\n");
		flash_lock(flash_lockBlock_cmd);
	}
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_BEGIN)
	{
		g_app_flash_stack_session_active = 1u;
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA write new FW begin, unlock flash\n");
		flash_unlock();
	}
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_END)
	{
		g_app_flash_stack_session_active = 0u;
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA write new FW end, restore flash locking\n");
		flash_lock(flash_lockBlock_cmd);
	}
#endif
	(void)op_addr_begin;
	(void)op_addr_end;
}

#else
/* 判断 Flash 操作后是否需要恢复保护锁。 */
int app_flash_lock_restore_enabled(void)
{
    return 1;
}
#endif

/* 正常启动时初始化参数、硬件、BLE 与应用状态。 */
_attribute_no_inline_ void user_init_normal(void)
{

	// 基础硬件初始化开始。

#if (MCU_CORE_TYPE == MCU_CORE_825x || MCU_CORE_TYPE == MCU_CORE_827x)
	random_generator_init();
#endif

#if (UART_PRINT_DEBUG_ENABLE)
	tlkapi_debug_init();
	blc_debug_enableStackLog(STK_LOG_DISABLE);
#endif

	blc_readFlashSize_autoConfigCustomFlashSector();
	blc_app_loadCustomizedParameters_normal();

#if (APP_FLASH_PROTECTION_ENABLE)
	app_flash_protection_operation(FLASH_OP_EVT_APP_INITIALIZATION, 0, 0);
	blc_appRegisterStackFlashOperationCallback(app_flash_protection_operation);
#endif

	// 基础硬件初始化结束。

	// BLE 协议栈初始化开始。
	u8 mac_public[6];
	u8 mac_random_static[6];
	blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);
	tlkapi_send_string_data(APP_LOG_EN, "[APP][INI]Public Address", mac_public, 6);

#if (BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_PUBLIC)
	app_own_address_type = OWN_ADDRESS_PUBLIC;
#elif (BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_RANDOM_STATIC)
	app_own_address_type = OWN_ADDRESS_RANDOM;
	blc_ll_setRandomAddr(mac_random_static);
#endif

	for (size_t i = 0; i < 6; i++)
	{
		g_stCellInfoReport.mac_public[i] = mac_public[5 - i];
	}

	blc_ll_initBasicMCU();
	blc_ll_initStandby_module(mac_public);
	blc_ll_initAdvertising_module(mac_public);
	blc_ll_initConnection_module();
	blc_ll_initSlaveRole_module();

	blc_gap_peripheral_init();
	blc_l2cap_register_handler(blc_l2cap_packet_receive);
	my_att_init();
	blc_att_setRxMtuSize(MTU_SIZE_SETTING);

#if (BLE_APP_SECURITY_ENABLE)
	bls_smp_configPairingSecurityInfoStorageAddr(flash_sector_smp_storage);
	blc_smp_peripheral_init();
	#if BMS_AFE_BACKEND == BMS_AFE_BACKEND_DVC1124
    bls_smp_configSecurityRequestSending(SecReq_IMM_SEND, SecReq_PEND_SEND, 1000);
#else
    blc_smp_configSecurityRequestSending(SecReq_IMM_SEND, SecReq_PEND_SEND, 1000);
#endif
#else
	blc_smp_setSecurityLevel(No_Security);
#endif

	blc_gap_registerHostEventHandler(app_host_event_callback);
	blc_gap_setEventMask(GAP_EVT_MASK_SMP_PAIRING_BEGIN |
						 GAP_EVT_MASK_SMP_PAIRING_SUCCESS |
						 GAP_EVT_MASK_SMP_PAIRING_FAIL |
						 GAP_EVT_MASK_ATT_EXCHANGE_MTU);

#if (BLE_OTA_SERVER_ENABLE)
#if (UART_PRINT_DEBUG_ENABLE)
	blc_debug_addStackLog(STK_LOG_OTA_FLOW);
#endif
	blc_ota_initOtaServer_module();
	blc_ota_setOtaProcessTimeout(APP_OTA_PROCESS_TIMEOUT_S);
	blc_ota_setOtaDataPacketTimeout(APP_OTA_DATA_PACKET_TIMEOUT_S);
	blc_ota_registerOtaStartCmdCb(app_enter_ota_mode);
	blc_ota_registerOtaResultIndicationCb(app_ota_end_result);
#endif

	u8 adv_param_status = BLE_SUCCESS;
#if (BLE_APP_SECURITY_ENABLE)
	u8 bond_number = blc_smp_param_getCurrentBondingDeviceNumber();
	smp_param_save_t bondInfo;
	if (bond_number)
	{
		bls_smp_param_loadByIndex(bond_number - 1, &bondInfo);
	}

	if (bond_number)
	{
		adv_param_status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
											  ADV_TYPE_CONNECTABLE_DIRECTED_LOW_DUTY, app_own_address_type,
											  bondInfo.peer_addr_type, bondInfo.peer_addr,
											  MY_APP_ADV_CHANNEL,
											  ADV_FP_NONE);

		if (blc_app_isIrkValid(bondInfo.peer_irk))
		{
			blc_ll_addDeviceToResolvingList(bondInfo.peer_id_adrType, bondInfo.peer_id_addr, bondInfo.peer_irk, NULL);
			blc_ll_setAddressResolutionEnable(1);
		}

		bls_ll_setAdvDuration(MY_DIRECT_ADV_TIME, 1);
		bls_app_registerEventCallback(BLT_EV_FLAG_ADV_DURATION_TIMEOUT, &app_switch_to_undirected_adv);
	}
	else
#endif
	{
		adv_param_status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
											  ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
											  0, NULL,
											  MY_APP_ADV_CHANNEL,
											  ADV_FP_NONE);
	}

	if (adv_param_status != BLE_SUCCESS)
	{
		tlkapi_printf(APP_LOG_EN, "[APP][INI] ADV parameters error 0x%x!!!\n", adv_param_status);
	}

	ble_build_adv_scanrsp();
	bls_ll_setAdvData((u8 *)tbl_advData, sizeof(tbl_advData));
	bls_ll_setScanRspData((u8 *)tbl_scanRsp, sizeof(tbl_scanRsp));
	bls_ll_setAdvEnable(BLC_ADV_ENABLE);

	rf_set_power_level_index(MY_RF_POWER_INDEX);

	bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &task_connect);
	bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &task_terminate);
	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &task_suspend_exit);
	bls_app_registerEventCallback(BLT_EV_FLAG_DATA_LENGTH_EXCHANGE, &task_dle_exchange);

#if (BLE_APP_PM_ENABLE)
	blc_ll_initPowerManagement_module();

#if (PM_DEEPSLEEP_RETENTION_ENABLE)
	blc_app_setDeepsleepRetentionSramSize();
	bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
	blc_pm_setDeepsleepRetentionThreshold(95, 95);

#if (MCU_CORE_TYPE == MCU_CORE_825x || MCU_CORE_TYPE == MCU_CORE_827x)
	blc_pm_setDeepsleepRetentionEarlyWakeupTiming(270);
#else
	blc_pm_setDeepsleepRetentionEarlyWakeupTiming(340);
#endif

#else
	bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
#endif

	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER, &task_sleep_enter);
#else
	bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif

	blc_app_checkControllerHostInitialization();

	advertise_begin_tick = clock_time();
	tlkapi_printf(APP_LOG_EN, "[APP][INI] BLE sample init \n");

    app_init();
}
