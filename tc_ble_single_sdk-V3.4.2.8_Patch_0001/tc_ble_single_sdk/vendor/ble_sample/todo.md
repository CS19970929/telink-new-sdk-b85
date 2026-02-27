
一个接口怎么实现一线通发送和串口modbus从机功能，以上是电路和一线通内容

owc是一线通接口，和一线通从设备通信，同时owc_tx、owc_rx直接通过端子可以作为串口，我是需要分时复用，或者说根据客户使用接口软件自动判断使用哪种通讯，软件该如何处理，能够区分使用哪种通讯


todo 
- soh、真实容量？？？
- soc校准
- mac地址、modbus读取？？？
- bls_pm_setSuspendMask (SUSPEND_DISABLE); 调用这个功耗太高了，不调用影响时序？？？soc计算？功能
- 不能去掉，去掉，通信都没法做了




telink有三种name，devname，tbl_advData，tbl_scanRsp，有什么区别
目前逻辑是这样的,还有telink部分蓝牙sdk
static void ble_build_adv_scanrsp(void)
{
	u8 i = 0;

	i = 0;
	// Flags: len=2, type=0x01, data=0x05
	tbl_advData[i++] = 0x02;
	tbl_advData[i++] = 0x01;
	tbl_advData[i++] = 0x05;

	// Appearance: len=3, type=0x19, data=0x0180
	tbl_advData[i++] = 0x03;
	tbl_advData[i++] = 0x19;
	tbl_advData[i++] = 0x80;
	tbl_advData[i++] = 0x01;

	// Incomplete 16-bit UUIDs: len=5, type=0x02, 0x1812, 0x180F
	tbl_advData[i++] = 0x05;
	tbl_advData[i++] = 0x02;
	tbl_advData[i++] = 0x12;
	tbl_advData[i++] = 0x18;
	tbl_advData[i++] = 0x0F;
	tbl_advData[i++] = 0x18;

	tbl_advDataLen = i;

	i = 0;
	tbl_scanRsp[i++] = (u8)(DEV_NAME_LEN + 1); // len = type(1)+name
	tbl_scanRsp[i++] = 0x09;				   // Complete Local Name
	memcpy(&tbl_scanRsp[i], DEV_NAME_STR, DEV_NAME_LEN);
	i += DEV_NAME_LEN;

	tbl_scanRspLen = i;
}
_attribute_no_inline_ void user_init_normal(void)
{

//////////////////////////// basic hardware Initialization  Begin //////////////////////////////////

	/* random number generator must be initiated before any BLE stack initialization.
	 * When deepSleep retention wakeUp, no need initialize again */
#if(MCU_CORE_TYPE == MCU_CORE_825x || MCU_CORE_TYPE == MCU_CORE_827x)
	random_generator_init();  //this is must
#endif

	//	debug init
	#if(UART_PRINT_DEBUG_ENABLE)
		tlkapi_debug_init();
		blc_debug_enableStackLog(STK_LOG_DISABLE);
	#endif

	blc_readFlashSize_autoConfigCustomFlashSector();

	/* attention that this function must be called after "blc_readFlashSize_autoConfigCustomFlashSector" !!!*/
	blc_app_loadCustomizedParameters_normal();


	/* attention that this function must be called after "blc_app_loadCustomizedParameters_normal" !!!
	   The reason is that the low battery check need the ADC calibration parameter, and this parameter
	   is loaded in blc_app_loadCustomizedParameters_normal.
	 */
	#if (APP_BATT_CHECK_ENABLE)
	/*The SDK must do a quick low battery detect during user initialization instead of waiting
	  until the main_loop. The reason for this process is to avoid application errors that the device
	  has already working at low power.
	  Considering the working voltage of MCU and the working voltage of flash, if the Demo is set below 2.0V,
	  the chip will alarm and deep sleep (Due to PM does not work in the current version of B92, it does not go
	  into deepsleep), and once the chip is detected to be lower than 2.0V, it needs to wait until the voltage rises to 2.2V,
	  the chip will resume normal operation. Consider the following points in this design:
		At 2.0V, when other modules are operated, the voltage may be pulled down and the flash will not
		work normally. Therefore, it is necessary to enter deepsleep below 2.0V to ensure that the chip no
		longer runs related modules;
		When there is a low voltage situation, need to restore to 2.2V in order to make other functions normal,
		this is to ensure that the power supply voltage is confirmed in the charge and has a certain amount of
		power, then start to restore the function can be safer.*/
		user_battery_power_check(VBAT_DEEP_THRES_MV);
	#endif


	#if (APP_FLASH_PROTECTION_ENABLE)
		app_flash_protection_operation(FLASH_OP_EVT_APP_INITIALIZATION, 0, 0);
		blc_appRegisterStackFlashOperationCallback(app_flash_protection_operation); //register flash operation callback for stack
	#endif



//////////////////////////// basic hardware Initialization  End //////////////////////////////////




//////////////////////////// BLE stack Initialization  Begin //////////////////////////////////
	//////////// Controller Initialization  Begin /////////////////////////
	u8  mac_public[6];
	u8  mac_random_static[6];
	/* for 512K Flash, flash_sector_mac_address equals to 0x76000, for 1M  Flash, flash_sector_mac_address equals to 0xFF000 */
	blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);
	tlkapi_send_string_data(APP_LOG_EN,"[APP][INI]Public Address", mac_public, 6);

	#if(BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_PUBLIC)
		app_own_address_type = OWN_ADDRESS_PUBLIC;
	#elif(BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_RANDOM_STATIC)
		app_own_address_type = OWN_ADDRESS_RANDOM;
		blc_ll_setRandomAddr(mac_random_static);
	#endif

	blc_ll_initBasicMCU();                      //mandatory
	blc_ll_initStandby_module(mac_public);		//mandatory
	blc_ll_initAdvertising_module(mac_public); 	//legacy advertising module: mandatory for BLE slave
	blc_ll_initConnection_module();				//connection module  mandatory for BLE slave/master
	blc_ll_initSlaveRole_module();				//slave module: 	 mandatory for BLE slave,
	//////////// Controller Initialization  End /////////////////////////



	//////////// Host Initialization  Begin /////////////////////////
	/* Host Initialization */
	/* GAP initialization must be done before any other host feature initialization !!! */
	blc_gap_peripheral_init();    //gap initialization
	blc_l2cap_register_handler (blc_l2cap_packet_receive);  	//l2cap initialization
	my_att_init(); //gatt initialization
	blc_att_setRxMtuSize(MTU_SIZE_SETTING); //set MTU size, default MTU is 23 if not call this API

	/* SMP Initialization may involve flash write/erase(when one sector stores too much information,
	 *   is about to exceed the sector threshold, this sector must be erased, and all useful information
	 *   should re_stored) , so it must be done after battery check */
	#if (BLE_APP_SECURITY_ENABLE)
		/* attention: If this API is used, must be called before "blc smp_peripheral_init" when initialization !!! */
		bls_smp_configPairingSecurityInfoStorageAddr(flash_sector_smp_storage);
		blc_smp_peripheral_init();

		/* Hid device on android7.0/7.1 or later version
		 * New paring: send security_request immediately after connection complete
		 * reConnect:  send security_request 1000mS after connection complete. If master start paring or encryption before 1000mS timeout, slave do not send security_request. */
		blc_smp_configSecurityRequestSending(SecReq_IMM_SEND, SecReq_PEND_SEND, 1000); //if not set, default is:  send "security request" immediately after link layer connection established(regardless of new connection or reconnection)
	#else
		blc_smp_setSecurityLevel(No_Security);
	#endif


	/* host(GAP/SMP/GATT/ATT) event process: register host event callback and set event mask */
	blc_gap_registerHostEventHandler(app_host_event_callback);
	/* enable some frequently-used host event by default, user can add more host event */
	blc_gap_setEventMask( GAP_EVT_MASK_SMP_PAIRING_BEGIN 			|  \
						  GAP_EVT_MASK_SMP_PAIRING_SUCCESS   		|  \
						  GAP_EVT_MASK_SMP_PAIRING_FAIL				|  \
						  GAP_EVT_MASK_ATT_EXCHANGE_MTU);
	//////////// Host Initialization  End /////////////////////////

	//////////// Service Initialization  Begin /////////////////////////
	#if (BLE_OTA_SERVER_ENABLE)
		////////////////// OTA relative ////////////////////////
		#if (UART_PRINT_DEBUG_ENABLE)
			blc_debug_addStackLog(STK_LOG_OTA_FLOW);
		#endif
		blc_ota_initOtaServer_module();

		//blc_ota_setOtaProcessTimeout(30);   //OTA process timeout:  30 seconds
		//blc_ota_setOtaDataPacketTimeout(4);	//OTA data packet timeout:  4 seconds
		blc_ota_registerOtaStartCmdCb(app_enter_ota_mode);
		blc_ota_registerOtaResultIndicationCb(app_ota_end_result);
	#endif
	//////////// Service Initialization  End   /////////////////////////

//////////////////////////// BLE stack Initialization  End //////////////////////////////////


//////////////////////////// User Configuration for BLE application ////////////////////////////
	////////////////// config ADV packet /////////////////////
	u8 adv_param_status = BLE_SUCCESS;
	#if (BLE_APP_SECURITY_ENABLE)
		u8 bond_number = blc_smp_param_getCurrentBondingDeviceNumber();  //get bonded device number
		smp_param_save_t  bondInfo;
		if(bond_number)   //at least 1 bonding device exist
		{
			bls_smp_param_loadByIndex( bond_number - 1, &bondInfo);  //get the latest bonding device (index: bond_number-1 )

		}

		if(bond_number)   //set direct ADV
		{
			/* set direct ADV
			 * bondInfo.peer_addr_type & bondInfo.peer_addr is the address in the air packet of "CONNECT_IND" PDU stored in Flash.
			 * if peer address is IDA(identity address), bondInfo.peer_addr is OK used here.
			 * if peer address is RPA(resolved private address), bondInfo.peer_addr is one RPA peer device has used, it has a correct relation
			 * with peer IRK, so it can match to peer device at any time even peer device changes it's RPA. */
			adv_param_status = bls_ll_setAdvParam( MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
											ADV_TYPE_CONNECTABLE_DIRECTED_LOW_DUTY, app_own_address_type,
											bondInfo.peer_addr_type,  bondInfo.peer_addr,
											MY_APP_ADV_CHANNEL,
											ADV_FP_NONE);

			/* If IRK distributed by peer device is valid, peer device may use RPA(resolved private address) at any time,
			 * even if it used IDA(identity address) in first pairing phase.
			 * So here must add peer IRK to resolving list and enable address resolution, since local device should check if
			 * "CONNECT_IND" PDU is sent by the device directed to.
			 * attention: local RPA not used, so parameter "local_irk" set to NULL */
			if(blc_app_isIrkValid(bondInfo.peer_irk)){
				blc_ll_addDeviceToResolvingList(bondInfo.peer_id_adrType, bondInfo.peer_id_addr, bondInfo.peer_irk, NULL);
				blc_ll_setAddressResolutionEnable(1);
			}

			//it is recommended that direct ADV only last for several seconds, then switch to undirected adv
			bls_ll_setAdvDuration(MY_DIRECT_ADV_TIME, 1);
			bls_app_registerEventCallback (BLT_EV_FLAG_ADV_DURATION_TIMEOUT, &app_switch_to_undirected_adv);

		}
		else   //set undirected adv
	#endif
		{
			adv_param_status = bls_ll_setAdvParam(  MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
											 ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
											 0,  NULL,
											 MY_APP_ADV_CHANNEL,
											 ADV_FP_NONE);
		}

	if(adv_param_status != BLE_SUCCESS){
		tlkapi_printf(APP_LOG_EN, "[APP][INI] ADV parameters error 0x%x!!!\n", adv_param_status);
	}

	ble_build_adv_scanrsp();
	bls_ll_setAdvData( (u8 *)tbl_advData, sizeof(tbl_advData) );
	bls_ll_setScanRspData( (u8 *)tbl_scanRsp, sizeof(tbl_scanRsp));
	bls_ll_setAdvEnable(BLC_ADV_ENABLE);  //ADV enable

	/* set RF power index, user must set it after every suspend wake_up, because relative setting will be reset in suspend */
	rf_set_power_level_index (MY_RF_POWER_INDEX);

	bls_app_registerEventCallback (BLT_EV_FLAG_CONNECT, &task_connect);
	bls_app_registerEventCallback (BLT_EV_FLAG_TERMINATE, &task_terminate);
	bls_app_registerEventCallback (BLT_EV_FLAG_SUSPEND_EXIT, &task_suspend_exit);
	bls_app_registerEventCallback (BLT_EV_FLAG_DATA_LENGTH_EXCHANGE, &task_dle_exchange);

	///////////////////// Power Management initialization///////////////////
	#if(BLE_APP_PM_ENABLE)
		blc_ll_initPowerManagement_module();

		#if (PM_DEEPSLEEP_RETENTION_ENABLE)
		    blc_app_setDeepsleepRetentionSramSize(); //select DEEPSLEEP_MODE_RET_SRAM_LOW16K or DEEPSLEEP_MODE_RET_SRAM_LOW32K
			bls_pm_setSuspendMask (SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
			blc_pm_setDeepsleepRetentionThreshold(95, 95);

			#if(MCU_CORE_TYPE == MCU_CORE_825x || MCU_CORE_TYPE == MCU_CORE_827x)
				blc_pm_setDeepsleepRetentionEarlyWakeupTiming(270);
			#else
				blc_pm_setDeepsleepRetentionEarlyWakeupTiming(340);
			#endif

		#else
			bls_pm_setSuspendMask (SUSPEND_ADV | SUSPEND_CONN);
		#endif

		bls_app_registerEventCallback (BLT_EV_FLAG_SUSPEND_ENTER, &task_sleep_enter);
	#else
		bls_pm_setSuspendMask (SUSPEND_DISABLE);
	#endif


	// #if (UI_KEYBOARD_ENABLE)
	// 	/////////// keyboard gpio wakeup init ////////
	// 	u32 pin[] = KB_DRIVE_PINS;
	// 	for (int i=0; i<(sizeof (pin)/sizeof(*pin)); i++)
	// 	{
	// 		cpu_set_gpio_wakeup (pin[i], Level_High,1);  //drive pin pad high wakeup deepsleep
	// 	}

	// 	bls_app_registerEventCallback (BLT_EV_FLAG_GPIO_EARLY_WAKEUP, &proc_keyboard);
	// #elif (UI_BUTTON_ENABLE)

	// 	cpu_set_gpio_wakeup (SW1_GPIO, Level_Low,1);  //button pin pad low wakeUp suspend/deepSleep
	// 	cpu_set_gpio_wakeup (SW2_GPIO, Level_Low,1);  //button pin pad low wakeUp suspend/deepSleep

	// 	bls_app_registerEventCallback (BLT_EV_FLAG_GPIO_EARLY_WAKEUP, &proc_button);

	// #endif
////////////////////////////////////////////////////////////////////////////////////////////////

	/* Check if any Stack(Controller & Host) Initialization error after all BLE initialization done.
	 * attention that code will stuck in "while(1)" if any error detected in initialization, user need find what error happens and then fix it */
	blc_app_checkControllerHostInitialization();

	advertise_begin_tick = clock_time();

	tlkapi_printf(APP_LOG_EN, "[APP][INI] BLE sample init \n");

	{
		//nvm_init(&nvm_cfg);
		init_bms_io();
		LoadParam();
		
		i2c_master_test_init();
		WaitMs(100);

		AFE_Reset();
		AFE_IsReady();
		SH367309_UpdataAfeConfig();
		SH367309_Enable_AFE_Wdt_Cadc_Drivers();

		adc_init_common();
		// bms_adc_init();
		cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 1);
		cpu_set_gpio_wakeup(SW_PIN, Level_Low, 1);

		soc_kv_store_init();
		soc_kv_data_t d = soc_kv_store_get();
		// d.soc = 100;
		soc_param_lib_init(&d);

	}
	
	app_timer_test_init();
	gpio_write(AFE_CTL_PIN, 1);

	bus_mux_init();
}

#ifndef LL_ADV_H_
#define LL_ADV_H_

#include "stack/ble/ble_format.h"




/**
 * @brief      This function is used to initialize advertising module.
 * @param[in]  public_adr -  public address pointer
 * @return     none
 */
void 		blc_ll_initAdvertising_module(u8 *public_adr);


/**
 * @brief	   This function is used to set the data used in advertising packets that have a data field.
 *  		   Please refer to BLE Core Specification: Vol 4, Part E, 7.8.7 for more information to understand the meaning of each parameters and
 * 			   the return values.
 * @param[in]  data -  advertising data buffer
 * @param[in]  len - The number of significant octets in the Advertising_Data.
 * @return     Status - 0x00: command succeeded; 0x01-0xFF: command failed
 */
ble_sts_t	bls_ll_setAdvData(u8 *data, u8 len);


/**
 * @brief	   This function is used to provide data used in Scanning Packets that have a data field.
 * 			   Please refer to BLE Core Specification: Vol 4, Part E, 7.8.8 for more information to understand the meaning of each parameters and
 * 			   the return values.
 * @param[in]  data -  Scan_Response_Data buffer
 * @param[in]  len - The number of significant octets in the Scan_Response_Data.
 * @return     Status - 0x00: command succeeded; 0x01-0xFF: command failed
 */
ble_sts_t 	bls_ll_setScanRspData(u8 *data, u8 len);



/**
* @brief	   This function is used to set whether to check the adv_interval.
* 			   ADV interval is checked for undirected ADV by default according to BLE Core Specification.
* 			   User can use this API to bypass the rule if they want some none standard ADV interval being used for undirected ADV.
* @param[in]   enable -  1: check ADV interval; 0: not check ADV interval
* @return      none
*/
void 		blc_ll_setAdvIntervalCheckEnable(u8 enable);



/**
 * @brief      This function is used to set the advertising parameters.
 * 			   attention: this API is as same as LE controller commands "LE Set Advertising Parameters command".
 * 			   Please refer to BLE Core Specification: Vol 4, Part E, 7.8.5 for more information to understand the meaning of each
 * 			   parameters and the return value.
 * @param[in]  intervalMin - Minimum advertising interval(Time = N * 0.625 ms, Range: 0x0020 to 0x4000)
 * @param[in]  intervalMin - Maximum advertising interval(Time = N * 0.625 ms, Range: 0x0020 to 0x4000)
 * @param[in]  advType - Advertising_Type
 * @param[in]  ownAddrType - Own_Address_Type
 * @param[in]  peerAddrType - Peer_Address_Type
 * @param[in]  peerAddr - Peer_Address
 * @param[in]  adv_channelMap - Advertising_Channel_Map
 * @param[in]  advFilterPolicy - Advertising_Filter_Policy
 * @return     Status - 0x00: command succeeded; 0x01-0xFF: command failed
 */
ble_sts_t   bls_ll_setAdvParam( u16 intervalMin,  u16 intervalMax,  adv_type_t advType,  		 	  own_addr_type_t ownAddrType,  \
							     u8 peerAddrType, u8  *peerAddr,    adv_chn_map_t 	adv_channelMap,   adv_fp_type_t   advFilterPolicy);




/**
 * @brief      This function is used to request the Controller to start or stop advertising.
 *             Please refer to BLE Core Specification: Vol 4, Part E, 7.8.9 for more information to understand the meaning of each parameters and
 * 			   the return values.
 * @param[in]  adv_enable - Advertising_Enable
 * @return     Status - 0x00: command succeeded; 0x01-0xFF: command failed
 */
ble_sts_t   bls_ll_setAdvEnable(int adv_enable);






/**
 * @brief      This function is used to set advertise duration time.
 * @param[in]  duration_us - the length of duration, the unit is us.
 * @param[in]  duration_en - Duration_Enable
 * @return     Status - 0x00: BLE success; 0x01-0xFF: fail
 */
ble_sts_t   bls_ll_setAdvDuration (u32 duration_us, u8 duration_en);





/**
 * @brief      This function is used to set some other channel to replace advertising chn37/38/39.
 * @param[in]  chn0 - channel to replace channel 37
 * @param[in]  chn1 - channel to replace channel 38
 * @param[in]  chn2 - channel to replace channel 39
 * @return     none
 */
void 		blc_ll_setAdvCustomedChannel (u8 chn0, u8 chn1, u8 chn2);

/**
 * @brief      This function is used to set whether to continue sending broadcast packets when receiving scan request in the current adv interval.
 * @param[in]  enable - enable:continue sending broadcast packets when receiving scan request.
 * @return     none.
 */
void 		bls_ll_continue_adv_after_scan_req(u8 enable);

/**
 * @brief      This function is used to set direct advertising initial address type.
 * @param[in]  cmdPara - command parameter
 * @return     Status - 0x00:  success;
 * 						other: fail
 */
u8 			blt_set_adv_direct_init_addrtype(u8* cmdPara);


/**
 * @brief      This function is used to set advertising type.
 * @param[in]  advType - adv type
 * @return     Status - 0x00:  success;
 * 						other: fail
 */
ble_sts_t   bls_ll_setAdvType(u8 advType);


/**
 * @brief      This function is used to set address type.
 * @param[in]  cmdPara - command parameter
 * @return     Status - 0x00:  success;
 * 						other: fail
 */
ble_sts_t 	blt_set_adv_addrtype(u8* cmdPara);


extern u32  blc_rcvd_connReq_tick;


/**
 * @brief      This function is used to get connection time.
 * @param	   none
 * @return     connection time
 */
static inline u32 	bls_ll_getConnectionCreateTime(void)
{
	return blc_rcvd_connReq_tick;
}


/**
 * @brief      This function is used to add adv in connection slave role.
 * @param      none
 * @return     Status - 0x00:  success;
 * 						other: fail
 */
ble_sts_t   blc_ll_addAdvertisingInConnSlaveRole(void);


/**
 * @brief      This function is used to remove adv in connection slave role.
 * @param      none
 * @return     Status - 0x00:  success;
 * 						other: fail
 */
ble_sts_t   blc_ll_removeAdvertisingFromConnSLaveRole(void);


/**
 * @brief      This function is used to set ADV parameter in slave role.
 * @param[in]  adv_data -advertising data
 * @param[in]  advData_len - length of the advertising data.
 * @param[in]  scanRsp_data - scan response data
 * @param[in]  scanRspData_len -  length of the scan response data.
 * @param[in]  advType - advertising type
 * @param[in]  ownAddrType - address type of the local device, which can be public or random
 * @param[in]  adv_channelMap - channel map
 * @param[in]  advFilterPolicy - advertising filter policy
 * @return     Status - 0x00:  success;
 * 						other: fail
 */
ble_sts_t 	blc_ll_setAdvParamInConnSlaveRole( u8 		  *adv_data,  u8              advData_len, u8 *scanRsp_data,  u8 scanRspData_len,
											   adv_type_t  advType,   own_addr_type_t ownAddrType, u8 adv_channelMap, adv_fp_type_t advFilterPolicy);


/**
 * @brief      This function is used to set ADV interval in slave role.
 * @param[in]  intervalMin - minimum adv interval
 * @param[in]  intervalMin - maximum adv interval
 * @return     Status - 0x00:  success;
 * 						other: fail
 */
ble_sts_t 	bls_ll_setAdvInterval(u16 intervalMin, u16 intervalMax);


/**
 * @brief      This function is used to set ADV channel used in slave role.
 * @param[in]  adv_channelMap - channel map
 * @return     Status - 0x00:  success;
 * 						other: fail
 */
ble_sts_t 	bls_ll_setAdvChannelMap(adv_chn_map_t adv_channelMap);


/**
 * @brief      This function is used to set ADV filter policy used in slave role.
 * @param[in]  advFilterPolicy - advertising filter policy
 * @return     Status - 0x00:  success;
 * 						other: fail
 */
ble_sts_t 	bls_ll_setAdvFilterPolicy(adv_fp_type_t advFilterPolicy);


typedef int (*advertise_prepare_handler_t) (rf_packet_adv_t * p);


/**
 * @brief      This function is used to set advertising prepare_handler.
 * @param[in]  p - data pointer
 * @return     none
 */
void 		bls_set_advertise_prepare (void *p);


#endif /* LL_ADV_H_ */


tbl_advData为什么没有像tbl_scanRsp那样memcpy DEV_NAME_STR

蓝牙名最长多少？
给蓝牙修改命名，逻辑不需要这么复杂，专门配一个sector起始地址专门用于存储蓝牙名字，
这个地址不会改变，而且蓝牙名字不会经常改变，因此也不用考虑擦写均衡等等，保证代码稳定、简洁即可，现在总结我的需求，并给出完整实现代码