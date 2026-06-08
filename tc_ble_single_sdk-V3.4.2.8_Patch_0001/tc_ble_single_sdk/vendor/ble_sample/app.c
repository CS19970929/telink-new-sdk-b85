/********************************************************************************************************
 * @file    app.c
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
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app_config.h"
#include "app.h"
#include "app_ui.h"
#include "app_att.h"
#include "battery_check.h"

#include "modbus_uart.h"
#include "modbus_rtu.h"

#include "sci_upper.h"
#include "sh367309_datadeal.h"
#include "bms_afe.h"

#include "SocEnhance.h"
#include "bms_event_log.h"
#include "soc_kv_store.h"
#include "sif_send.h"
// #include "nvm_flash.h"
#include "bus_mux.h"
#include "btname_modbus.h"
#include "runtime.h"
#include <string.h>

extern void LoadParam(void);
extern void Param_UpgradeReset_Apply(void);

struct stCell_Info g_stCellInfoReport;
volatile struct SYSTEM_ERROR System_ErrFlag;
bool deepsleep_en = false;
// nvm_cfg_t nvm_cfg;

#define APP_PM_TICKS_PER_SEC 32000u

typedef struct
{
	u32 last_tick_32k;
	u32 pending_tick_32k;
	u8 ready;
} app_pm_elapsed_ctx_t;

UINT8 IsChargerWakeupActive(void)
{
	return !gpio_read(CHG_IN_PIN);
}
UINT8 IsKeyWakeupActive(void)
{
	return !gpio_read(SW_PIN);
}
static u32 app_pm_take_elapsed_seconds(app_pm_elapsed_ctx_t *ctx)
{
	u32 now_tick_32k;
	u32 elapsed_tick_32k;
	u32 total_tick_32k;
	u32 elapsed_sec;

	if (ctx == NULL)
	{
		return 0u;
	}

	now_tick_32k = pm_get_32k_tick();
	if (!ctx->ready)
	{
		ctx->last_tick_32k = now_tick_32k;
		ctx->ready = 1u;
		return 0u;
	}

	elapsed_tick_32k = now_tick_32k - ctx->last_tick_32k;
	ctx->last_tick_32k = now_tick_32k;
	total_tick_32k = ctx->pending_tick_32k + elapsed_tick_32k;
	elapsed_sec = total_tick_32k / APP_PM_TICKS_PER_SEC;
	ctx->pending_tick_32k = total_tick_32k % APP_PM_TICKS_PER_SEC;
	return elapsed_sec;
}

static void app_event_log_1s_task(void)
{
	bms_event_log_sample_t sample;
	u8 eeprom_err;

	memset(&sample, 0, sizeof(sample));
	sample.sleep = sys_time.low_power_mode ? 1u : 0u;
	sample.balance = ((g_stCellInfoReport.u16BalanceFlag1 != 0u) || (g_stCellInfoReport.u16BalanceFlag2 != 0u)) ? 1u : 0u;
	sample.heat = SystemStatus.bits.b1Status_Heat ? 1u : 0u;
	sample.cool = SystemStatus.bits.b1Status_Cool ? 1u : 0u;

	sample.vcell_ovp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp ? 1u : 0u;
	sample.vbus_ovp = g_stCellInfoReport.unMdlFault_Third.bits.b1BatOvp ? 1u : 0u;
	sample.chg_ocp = g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp ? 1u : 0u;
	sample.vcell_uvp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp ? 1u : 0u;
	sample.vbus_uvp = g_stCellInfoReport.unMdlFault_Third.bits.b1BatUvp ? 1u : 0u;
	sample.dsg_ocp = g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp ? 1u : 0u;
	sample.chg_utp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp ? 1u : 0u;
	sample.dsg_utp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp ? 1u : 0u;
	sample.chg_otp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp ? 1u : 0u;
	sample.dsg_otp = g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp ? 1u : 0u;
	sample.vdelta_op = g_stCellInfoReport.unMdlFault_Third.bits.b1VcellDeltaBig ? 1u : 0u;

	sample.afe2_err = System_ERROR_UserCallback(ERROR_STATUS_AFE1) ? 1u : 0u;
	eeprom_err = (System_ERROR_UserCallback(ERROR_STATUS_EEPROM_STORE) ||
				  System_ERROR_UserCallback(ERROR_STATUS_EEPROM_COM))
					 ? 1u
					 : 0u;
	sample.eeprom_err = eeprom_err;
	sample.cbc_err = System_ERROR_UserCallback(ERROR_STATUS_CBC_DSG) ? 1u : 0u;

	bms_event_log_poll_1s(&sample);
}

static int app_deepsleep_pad_wakeup_active(void)
{
	return !gpio_read(CHG_IN_PIN);
}

static int app_note_sleep_and_enter_deepsleep(u8 need_afe_sleep)
{
	int sleep_status;

	if (app_deepsleep_pad_wakeup_active())
	{
		return 0;
	}

	bms_event_log_note_sleep();
	if (need_afe_sleep)
	{
		bms_afe_sleep();
	}
	Runtime_PrepareForDeepSleep();
	sleep_status = cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);
	Runtime_CancelPendingDeepSleep();
	return ((sleep_status & STATUS_GPIO_ERR_NO_ENTER_PM) == 0);
}

void open_ctlc(void)
{
	gpio_write(AFE_CTL_PIN, 1);
	// gpio_write(MCC_C_PIN, 1);
}
void close_ctlc(void)
{
	gpio_write(AFE_CTL_PIN, 0);

	// todo 会不会存在冲突，逻辑完备？？？
	gpio_write(MCC_C_PIN, 0);
}

void app_timer_test_init(void)
{
	// timer0 10ms interval irq
	reg_irq_mask |= FLD_IRQ_TMR0_EN;
	reg_tmr0_tick = 0; // clear counter
	// reg_tmr0_capt = 1 * CLOCK_SYS_CLOCK_1MS;
	reg_tmr0_capt = 500 * CLOCK_SYS_CLOCK_1US;
	reg_tmr_sta = FLD_TMR_STA_TMR0; // clear irq status
	reg_tmr_ctrl |= FLD_TMR0_EN;	// start timer
#if 0
	//timer1 15ms interval irq
	reg_irq_mask |= FLD_IRQ_TMR1_EN;
	reg_tmr1_tick = 0; //clear counter
	reg_tmr1_capt = 15 * CLOCK_SYS_CLOCK_1MS;
	reg_tmr_sta = FLD_TMR_STA_TMR1; //clear irq status
	reg_tmr_ctrl |= FLD_TMR1_EN;  //start timer

	//timer2 20ms interval irq
	reg_irq_mask |= FLD_IRQ_TMR2_EN;
	reg_tmr2_tick = 0; //clear counter
	reg_tmr2_capt = 20 * CLOCK_SYS_CLOCK_1MS;
	reg_tmr_sta = FLD_TMR_STA_TMR2; //clear irq status
	reg_tmr_ctrl |= FLD_TMR2_EN;  //start timer
#endif

	irq_enable();
}

volatile int timer0_irq_cnt = 0;
_attribute_ram_code_ void app_timer_test_irq_proc(void)
{
	if (reg_tmr_sta & FLD_TMR_STA_TMR0)
	{
		sif_send_data_handle();
		reg_tmr_sta = FLD_TMR_STA_TMR0; // clear irq status
		timer0_irq_cnt++;
		if (timer0_irq_cnt >= 200)
		{
			timer0_irq_cnt = 0;
		}
		// DBG_CHN0_TOGGLE;
	}
	// if(reg_tmr_sta & FLD_TMR_STA_TMR1){
	// 	reg_tmr_sta = FLD_TMR_STA_TMR1; //clear irq status
	// 	timer1_irq_cnt ++;
	// 	DBG_CHN1_TOGGLE;
	// }
	// if(reg_tmr_sta & FLD_TMR_STA_TMR2){
	// 	reg_tmr_sta = FLD_TMR_STA_TMR2; //clear irq status
	// 	timer2_irq_cnt ++;
	// 	DBG_CHN2_TOGGLE;
	// }
}

#define ADV_IDLE_ENTER_DEEP_TIME 60	 // 60 s
#define CONN_IDLE_ENTER_DEEP_TIME 60 // 60 s

#define APP_CONN_LATENCY_NORMAL 99
#define APP_CONN_LATENCY_OTA 0

#define MY_DIRECT_ADV_TIME 2000000

#define MY_APP_ADV_CHANNEL BLT_ENABLE_ADV_ALL
#define MY_ADV_INTERVAL_MIN ADV_INTERVAL_800MS
#define MY_ADV_INTERVAL_MAX ADV_INTERVAL_800MS
// #define 	MY_ADV_INTERVAL_MIN					ADV_INTERVAL_500MS
// #define 	MY_ADV_INTERVAL_MAX					ADV_INTERVAL_500MS
// #define 	MY_ADV_INTERVAL_MIN					ADV_INTERVAL_30MS
// #define 	MY_ADV_INTERVAL_MAX					ADV_INTERVAL_30MS

#define MY_RF_POWER_INDEX RF_POWER_P3dBm

#define BLE_DEVICE_ADDRESS_TYPE BLE_DEVICE_ADDRESS_PUBLIC

_attribute_data_retention_ u8 ota_is_working = 0;
_attribute_data_retention_ own_addr_type_t app_own_address_type = OWN_ADDRESS_PUBLIC;

/**
 * @brief      LinkLayer RX & TX FIFO configuration
 */
/* CAL_LL_ACL_RX_BUF_SIZE(maxRxOct): maxRxOct + 22, then 16 byte align */
#define RX_FIFO_SIZE 64
/* must be: 2^n, (power of 2);at least 4; recommended value: 4, 8, 16 */
#define RX_FIFO_NUM 8

/* CAL_LL_ACL_TX_BUF_SIZE(maxTxOct):  maxTxOct + 10, then 4 byte align */
#define TX_FIFO_SIZE 40
/* must be: (2^n), (power of 2); at least 8; recommended value: 8, 16, 32, other value not allowed. */
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

void ble_build_adv_scanrsp(void)
{
	u8 i = 0;

	// --- ADV: 鏀� Flags + Appearance + UUID list锛堝缓璁� ADV 涓嶆斁鍚嶅瓧锛屽悕瀛楁斁 scanRsp锛� ---
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

	// --- ScanRsp: 鏀惧畬鏁村悕瀛� ---
	i = 0;
	tbl_scanRsp[i++] = (u8)(DEV_NAME_LEN + 1); // len = type(1)+name
	tbl_scanRsp[i++] = 0x09;				   // Complete Local Name
	memcpy(&tbl_scanRsp[i], DEV_NAME_STR, DEV_NAME_LEN);
	i += DEV_NAME_LEN;

	tbl_scanRspLen = i;
}

void open_chg_close_dsg(void)
{
	SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 瀵拷閸氱枌ADC
	SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 1; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
	SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 0; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
	MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
	gpio_write(MCC_C_PIN, 1);
}
void open_dsg_close_chg(void)
{
	SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 瀵拷閸氱枌ADC
	SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 0; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
	SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 1; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
	MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
	gpio_write(MCC_C_PIN, 0);
}

void close_chg(void)
{
	SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 瀵拷閸氱枌ADC
	SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 0; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
	SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 0; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
	MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
	gpio_write(MCC_C_PIN, 0);
}

void open_dsg(void)
{
	SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 瀵拷閸氱枌ADC
	SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 0; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
	SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 1; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
	MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
	gpio_write(MCC_C_PIN, 0);
}

void close_dsg(void)
{
	SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 瀵拷閸氱枌ADC
	SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 0; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
	SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 0; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
	MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
	gpio_write(MCC_C_PIN, 0);
}
void enter_fac_mode(bool on)
{
#if 1
	if (on)
	{
		SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 瀵拷閸氱枌ADC
		SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 1; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
		SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 1; // 閸忓懐鏁窶OS閻㈢泧FE绾兛娆㈤幒褍鍩�
		MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
		gpio_write(MCC_C_PIN, 1);
	}
	else
	{
		open_dsg_close_chg();
	}
#endif
}

extern volatile union System_Status SystemStatus;
static void factory_mode_apply_switch_state(void)
{
	if (MODE_FACTORY != Runtime_GetMode())
	{
		return;
	}

	if (IsChargerWakeupActive())
	{
		open_chg_close_dsg();
	}
	else if (IsKeyWakeupActive())
	{
		enter_fac_mode(true);
	}
	else
	{
		close_dsg();
	}
}

void charger_detect_and_keyLogi_200ms(void)
{
	static u8 state = 0;
	static u8 key_state = 0;

	switch (state)
	{
	case 0:
		if (!gpio_read(CHG_IN_PIN))
		{
			state = 1;
			open_chg_close_dsg();
			SystemStatus.bits.b1Status_Cool = 1;
		}
		else
		{
		}
		break;
	case 1:
		if (gpio_read(CHG_IN_PIN))
		{
			state = 0;
			if (MODE_FACTORY == Runtime_GetMode())
			{
				factory_mode_apply_switch_state();
			}
			else
			{
				close_chg();
				if(IsKeyWakeupActive())
				{
					open_dsg();
				}
			}
			SystemStatus.bits.b1Status_Cool = 0;
		}
		else
		{
		}
		break;
	default:
		state = 0;
		break;
	}

	if (MODE_FACTORY == Runtime_GetMode())
	{
		key_state = 0;
		factory_mode_apply_switch_state();
		return;
	}

	if (!IsChargerWakeupActive())
	{
		switch (key_state)
		{
		case 0:
			if(IsKeyWakeupActive())
			{
				open_dsg();
				key_state = 1;
			}
			break;
		case 1:
			if(!IsKeyWakeupActive())
			{
				close_dsg();
				key_state = 0;
			}
			break;
		default:
			key_state = 0;
			break;
		}
	}
}

void adc_init_common(void)
{
	adc_init();
	adc_power_on_sar_adc(1);
}

#define NTC_SETTLE_US 50u
static inline void delay_us(uint32_t us)
{
	uint32_t t0 = clock_time();
	while (!clock_time_exceed(t0, us))
	{
	}
}
unsigned int adc_read_gpio_mv(GPIO_PinTypeDef pin)
{
	// 1) 閲嶆柊鎶� ADC 杈撳叆鏄犲皠鍒拌 pin
	//    浣犵幇鏈� adc_base_init 浼氬仛涓�鍫嗗垵濮嬪寲锛屽亸閲�
	//    濡傛灉閲囨牱棰戠巼涓嶉珮锛堟瘮濡� 10~100ms 涓�娆★級锛岀敤瀹冩病闂
	adc_base_init(pin);
	// delay_us(NTC_SETTLE_US);

	// 2) 涓㈠純涓�娆★紙鍒囬�氶亾鍚庣ǔ瀹氾級
	// (void)adc_sample_and_get_result();

	// 3) 鍙栨湁鏁堝��
	return adc_sample_and_get_result();
}

#define LENGTH_TBLTEMP_MCU_10K ((UINT16)60)
// const UINT16 iSheldTemp_10K[LENGTH_TBLTEMP_PORT_10K] = {
const UINT16 iSheldTemp_10K_mcu[LENGTH_TBLTEMP_MCU_10K] = {
	// AD		(Temp+40)*10
	2037,
	0, //-30
	1526,
	50, //-30
	1161,
	100, //-30
	893,
	150, //-25
	694,
	200, //-20
	544,
	250, //-15
	430,
	300, //-10
	342,
	350, //-5
	275,
	400, // 0
	221,
	450, // 5
	180,
	500, // 10
	147,
	550, // 15
	121,
	600, // 20
	100,
	650, // 25
	83,
	700, // 30
	69,
	750, // 35
	58,
	800, // 40
	49,
	850, // 45
	41,
	900, // 50
	35,
	950, // 55
	30,
	1000, // 60
	26,
	1050, // 65
	22,
	1100, // 70
	19,
	1150, // 75
	16,
	1200, // 80
	14,
	1250, // 85
	12,
	1300, // 90
	11,
	1350, // 95
	9,
	1400, // 100
	8,
	1450, // 105

};

void app_adc_multi_sample(void)
{
	static u32 power_on_delay = 0;
	static u16 weichi_delay = 0;
	static u8 mos_state = 0;
	static uint32_t rong_fuse = 0;
#ifdef _UL_RENZHENG_ENABLE_
	static u8 state_fuse = 0;
	static uint32_t rong_fuse_afe_err_cnt = 0;
#endif

	if (sys_time.low_power_mode)
	{
		mos_state = 0;
#ifdef _UL_RENZHENG_ENABLE_
		state_fuse = 0;
		rong_fuse_afe_err_cnt = 0;
#endif
		return;
	}

	unsigned int bat_temp_mv = adc_read_gpio_mv(ADC_NTC_PIN);
	unsigned int mos_temp_mv = adc_read_gpio_mv(ADC_NMOS_PIN);
	unsigned int Vbat_mv = adc_read_gpio_mv(ADC_VBUS_PIN);
	if (bat_temp_mv >= 3299)
		bat_temp_mv = 3299;
	if (mos_temp_mv >= 3299)
		mos_temp_mv = 3299;
	u32 bat_temp_r = 10 * 10 * bat_temp_mv / (3300 - bat_temp_mv);
	u32 mos_temp_r = 10 * 10 * mos_temp_mv / (3300 - mos_temp_mv);
	g_stCellInfoReport.u16Temperature[8] = GetEndValue(iSheldTemp_10K_mcu, (UINT16)LENGTH_TBLTEMP_MCU_10K, bat_temp_r);
	g_stCellInfoReport.u16Temperature[9] = GetEndValue(iSheldTemp_10K_mcu, (UINT16)LENGTH_TBLTEMP_MCU_10K, mos_temp_r);

	// ...
	Vbat_mv = Vbat_mv * 485 / 15;
#ifdef DISP_VBAT_AND_TEMP_
	g_stCellInfoReport.u16VCell[29] = bat_temp_mv;
	g_stCellInfoReport.u16VCell[30] = mos_temp_mv;
	g_stCellInfoReport.u16VCell[31] = Vbat_mv;
#endif // ! FAC_TEST

	switch (mos_state)
	{
	case 0:
		if (g_stCellInfoReport.u16Temperature[9] >= (95 + 40) * 10)
		{
			close_ctlc();
			FaultWarnRecord2(MosOTp_Third);
			mos_state = 1;
		}
		break;
	case 1:
		if (g_stCellInfoReport.u16Temperature[9] <= (75 + 40) * 10)
		{
			open_ctlc();
			mos_state = 0;
		}
		break;
	default:
		mos_state = 0;
		break;
	}

#ifdef _UL_RENZHENG_ENABLE_

	if (1 == System_ErrFlag.u8ErrFlag_Com_AFE1)
	{
		rong_fuse = 0;
		state_fuse = 0;

		close_ctlc();
		// todo mcc关了，when 开
		if (Vbat_mv >= 4280 * SeriesNum || g_stCellInfoReport.u16Temperature[8] >= (85 + 40) * 10)
		{
			if (++rong_fuse_afe_err_cnt >= 10)
			{
				rong_fuse_afe_err_cnt = 0;
#ifdef _UL_RENZHENG_ENABLE_
				gpio_write(RF_EN_PIN, 1);
#endif
			}
		}
	}
	else
	{
		static u16 delay_cnt = 0;

		switch (state_fuse)
		{
		case 0:
			if ((g_stCellInfoReport.u16Temperature[8] >= (80 + 40) * 10))
			{
				state_fuse = 1;
				close_ctlc();
				FaultWarnRecord2(CellChgOTp_Third);
				FaultWarnRecord2(CellDsgOTp_Third);
			}
			if ((g_stCellInfoReport.u16VCellMax >= 4270) && (g_stCellInfoReport.u16VCellMin >= 1000))
			{
				++delay_cnt;
				if (delay_cnt >= 15)
				{
					delay_cnt = 0;
					state_fuse = 1;
					close_ctlc();
					// 是否应该强制关掉放电？？？
					FaultWarnRecord2(CellOvp_Third);
					FaultWarnRecord2(BatOvp_Third);
				}
			}
			else
				delay_cnt = 0;
			break;
		case 1:
			if ((g_stCellInfoReport.u16Temperature[8] < (75 + 40) * 10) && (g_stCellInfoReport.u16VCellMax <= 4150))
			{
				state_fuse = 0;
				open_ctlc();
			}
			if (((g_stCellInfoReport.u16VCellMax >= 4280) || (Vbat_mv >= 4280 * SeriesNum) || g_stCellInfoReport.u16Temperature[8] >= (85 + 40) * 10) && (g_stCellInfoReport.u16Ichg))
			{
				if (++rong_fuse >= (15))
				{
					rong_fuse = 0;
#ifdef _UL_RENZHENG_ENABLE_
					gpio_write(RF_EN_PIN, 1);
#endif
				}
			}
			else
			{
				rong_fuse = 0;
			}
			break;
		default:
			state_fuse = 0;
			break;
		}
	}
#endif

#if 0
	if(++power_on_delay <= (60))
	{
		return;
	}
	power_on_delay = 61;

	//todo fuse logi ???
#ifdef _UL_RENZHENG_ENABLE_
	

	static u8 state_fuse = 0;
	switch (state_fuse)
	{
	case 0:
		if(Vbat_mv >= 4280 * SeriesNum || g_stCellInfoReport.u16Temperature[8] >= 1200)
		{
			if(++weichi_delay >= (10))
			{
				weichi_delay = 0;
				close_ctlc();
				state_fuse = 1;
			}
		}
		else
		{
			weichi_delay = 0;
		}
		break;
	case 1:
#ifdef _UL_RENZHENG_ENABLE_
			gpio_write(RF_EN_PIN, 1);
#endif
		// if(Vbat_mv < 3800 * SeriesNum && g_stCellInfoReport.u16Temperature[8] <= 900)
		// // if(Vbat_mv < 3800 * SeriesNum)
		// {
		// 	gpio_write(AFE_CTL_PIN, 1);
		// 	state_fuse = 0;
		// }
		//todo 冗余逻辑
		break;
	
	default:
		state_fuse = 0;
		break;
	}
#endif
#endif
}

void enter_rtc_mode(void)
{
	gpio_write(ADC_BUSEN_PIN, 0);

	gpio_write(ADC_EN_PIN, 0);
}
void quit_rtc_mode(void)
{
	gpio_write(ADC_BUSEN_PIN, 1);

	gpio_write(ADC_EN_PIN, 1);
}

void init_bms_io(void)
{
	gpio_set_func(AFE1_PRO_EN_PIN, AS_GPIO);
	gpio_set_input_en(AFE1_PRO_EN_PIN, 0);
	gpio_set_output_en(AFE1_PRO_EN_PIN, 1);

	gpio_set_func(AFE_CTL_PIN, AS_GPIO); // PA4 姒涙顓绘稉锟� GPIO 閸旂喕鍏橀敍灞藉讲娴犮儰绗夌拋鍓х枂
	gpio_set_input_en(AFE_CTL_PIN, 0);
	gpio_set_output_en(AFE_CTL_PIN, 1);
	close_ctlc();

	{
#ifdef _UL_RENZHENG_ENABLE_
		gpio_set_func(RF_EN_PIN, AS_GPIO); // PA4 姒涙顓绘稉锟� GPIO 閸旂喕鍏橀敍灞藉讲娴犮儰绗夌拋鍓х枂
		gpio_set_input_en(RF_EN_PIN, 0);
		gpio_set_output_en(RF_EN_PIN, 1);
		// gpio_setup_up_down_resistor(RF_EN_PIN, PM_PIN_PULLDOWN_100K);
		gpio_write(RF_EN_PIN, 0);
#endif //

		gpio_set_func(SW_PIN, AS_GPIO); // PA4 姒涙顓绘稉锟� GPIO 閸旂喕鍏橀敍灞藉讲娴犮儰绗夌拋鍓х枂
		gpio_set_input_en(SW_PIN, 1);
		gpio_set_output_en(SW_PIN, 0);
		// gpio_write(GPIO_PA1, 1);

		gpio_set_func(MCC_C_PIN, AS_GPIO); // PA4 姒涙顓绘稉锟� GPIO 閸旂喕鍏橀敍灞藉讲娴犮儰绗夌拋鍓х枂
		gpio_set_input_en(MCC_C_PIN, 0);
		gpio_set_output_en(MCC_C_PIN, 1);
		gpio_write(MCC_C_PIN, 0);

		gpio_set_func(CHG_IN_PIN, AS_GPIO); // PA4 姒涙顓绘稉锟� GPIO 閸旂喕鍏橀敍灞藉讲娴犮儰绗夌拋鍓х枂
		gpio_setup_up_down_resistor(CHG_IN_PIN, PM_PIN_PULLUP_1M);
		// gpio_setup_up_down_resistor(CHG_IN_PIN, PM_PIN_PULLUP_10K);
		gpio_set_input_en(CHG_IN_PIN, 1);
		gpio_set_output_en(CHG_IN_PIN, 0);

		gpio_set_func(CHG_WK_PIN, AS_GPIO); // PA4 姒涙顓绘稉锟� GPIO 閸旂喕鍏橀敍灞藉讲娴犮儰绗夌拋鍓х枂
		gpio_set_input_en(CHG_WK_PIN, 1);
		gpio_set_output_en(CHG_WK_PIN, 0);

		gpio_set_func(ADC_BUSEN_PIN, AS_GPIO); // PA4 姒涙顓绘稉锟� GPIO 閸旂喕鍏橀敍灞藉讲娴犮儰绗夌拋鍓х枂
		gpio_set_input_en(ADC_BUSEN_PIN, 0);
		gpio_set_output_en(ADC_BUSEN_PIN, 1);
		gpio_write(ADC_BUSEN_PIN, 1);

		gpio_set_func(ADC_EN_PIN, AS_GPIO); // PA4 姒涙顓绘稉锟� GPIO 閸旂喕鍏橀敍灞藉讲娴犮儰绗夌拋鍓х枂
		gpio_set_input_en(ADC_EN_PIN, 0);
		gpio_set_output_en(ADC_EN_PIN, 1);
		gpio_write(ADC_EN_PIN, 1);
	}
}

void i2c_master_test_init(void)
{
	i2c_gpio_set(I2C_GPIO_GROUP_C0C1); // SDA/CK : C0/C1

	i2c_master_init(AFE_ID, (unsigned char)(CLOCK_SYS_CLOCK_HZ / (4 * 100000)));
}

_attribute_data_retention_ int device_in_connection_state;

_attribute_data_retention_ u32 advertise_begin_tick;

_attribute_data_retention_ u8 sendTerminate_before_enterDeep = 0;

void app_ble_request_normal_conn_param(void)
{
	if (device_in_connection_state)
	{
		bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, APP_CONN_LATENCY_NORMAL, CONN_TIMEOUT_4S);
	}
}

void app_ble_request_ota_conn_param(void)
{
	if (device_in_connection_state)
	{
		bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, APP_CONN_LATENCY_OTA, CONN_TIMEOUT_4S);
	}
}

void app_ble_restore_normal_power(void)
{
#if (BLE_APP_PM_ENABLE)
	bls_pm_setManualLatency(bls_ll_getConnectionLatency());
#endif
	app_ble_request_normal_conn_param();
}

_attribute_data_retention_ u32 latest_user_event_tick;

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_SUSPEND_ENTER"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void task_sleep_enter(u8 e, u8 *p, int n)
{
	(void)e;
	(void)p;
	(void)n;
	if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN && ((u32)(bls_pm_getSystemWakeupTick() - clock_time())) > 80 * SYSTEM_TIMER_TICK_1MS)
	{										   // suspend time > 30ms.add gpio wakeup
		bls_pm_setWakeupSource(PM_WAKEUP_PAD); // gpio pad wakeup suspend/deepsleep
	}
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_ADV_DURATION_TIMEOUT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void app_switch_to_undirected_adv(u8 e, u8 *p, int n)
{
	(void)e;
	(void)p;
	(void)n;
	bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
					   ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
					   0, NULL,
					   MY_APP_ADV_CHANNEL,
					   ADV_FP_NONE);

	/* clear resolving list:
	 * 1. delete all devices in resolving list.
	 * 2. disable address resolution */
	blc_ll_clearResolvingList();

	bls_ll_setAdvEnable(BLC_ADV_ENABLE); // must: set ADV enable
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_CONNECT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void task_connect(u8 e, u8 *p, int n)
{
	(void)e;
	(void)p;
	(void)n;
	tlk_contr_evt_connect_t *pConnEvt = (tlk_contr_evt_connect_t *)p;
	tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] connect, intA & advA:", pConnEvt->initA, 12);
	device_in_connection_state = 1;		 //
										 //	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 19, CONN_TIMEOUT_4S);  // 200mS
	app_ble_request_normal_conn_param(); // 1 S
										 //	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 149, CONN_TIMEOUT_8S);  // 1.5 S
										 //	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 199, CONN_TIMEOUT_8S);  // 2 S
										 //	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 249, CONN_TIMEOUT_8S);  // 2.5 S
										 //	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 299, CONN_TIMEOUT_8S);  // 3 S

	latest_user_event_tick = clock_time();

#if (UI_LED_ENABLE && !TEST_CONN_CURRENT_ENABLE)
	gpio_write(GPIO_LED_RED, LED_ON_LEVEL); // red led on
#endif
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_TERMINATE"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void task_terminate(u8 e, u8 *p, int n) //*p is terminate reason
{
	(void)e;
	(void)n;

	device_in_connection_state = 0;

	tlk_contr_evt_terminate_t *pEvt = (tlk_contr_evt_terminate_t *)p;
	if (pEvt->terminate_reason == HCI_ERR_CONN_TIMEOUT)
	{
	}
	else if (pEvt->terminate_reason == HCI_ERR_REMOTE_USER_TERM_CONN)
	{
	}
	else if (pEvt->terminate_reason == HCI_ERR_CONN_TERM_MIC_FAILURE)
	{
	}
	else
	{
	}

	tlkapi_printf(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] disconnect, reason 0x%x\n", pEvt->terminate_reason);

#if (BLE_APP_PM_ENABLE)
	// user has push terminate packet to BLE TX buffer before deepsleep
	if (sendTerminate_before_enterDeep == 1 && !TEST_CONN_CURRENT_ENABLE)
	{
		sendTerminate_before_enterDeep = 2;
		bls_ll_setAdvEnable(BLC_ADV_DISABLE); // disable ADV
	}
#endif

#if (UI_LED_ENABLE && !TEST_CONN_CURRENT_ENABLE)
	gpio_write(GPIO_LED_RED, !LED_ON_LEVEL); // red led off
#endif

	advertise_begin_tick = clock_time();
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_SUSPEND_EXIT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void task_suspend_exit(u8 e, u8 *p, int n)
{
	(void)e;
	(void)p;
	(void)n;
	rf_set_power_level_index(MY_RF_POWER_INDEX);
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_DATA_LENGTH_EXCHANGE"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void task_dle_exchange(u8 e, u8 *p, int n)
{
	tlk_contr_evt_dataLenExg_t *pEvt = (tlk_contr_evt_dataLenExg_t *)p;
	tlkapi_send_string_data(APP_CONTR_EVENT_LOG_EN, "[APP][EVT] DLE exchange", &pEvt->connEffectiveMaxRxOctets, 4);
}

/**
 * @brief      callback function of Host Event
 * @param[in]  h - Host Event type
 * @param[in]  para - data pointer of event
 * @param[in]  n - data length of event
 * @return     0
 */
int app_host_event_callback(u32 h, u8 *para, int n)
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
		// gap_smp_connEncDoneEvt_t *pEvt = (gap_smp_connEncDoneEvt_t *)para;
	}
	break;

	case GAP_EVT_SMP_SECURITY_PROCESS_DONE:
	{
		// gap_smp_securityProcessDoneEvt_t *pEvt = (gap_smp_securityProcessDoneEvt_t *)para;
	}
	break;

	case GAP_EVT_SMP_TK_DISPLAY:
	{
		// u32 pinCode = MAKE_U32(para[3], para[2], para[1], para[0]);
	}
	break;

	case GAP_EVT_SMP_TK_REQUEST_PASSKEY:
	{
		// for this event, no data, "para" is NULL
	}
	break;

	case GAP_EVT_SMP_TK_REQUEST_OOB:
	{
		// for this event, no data, "para" is NULL
	}
	break;

	case GAP_EVT_SMP_TK_NUMERIC_COMPARE:
	{
		// u32 pinCode = MAKE_U32(para[3], para[2], para[1], para[0]);
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
		// for this event, no data, "para" is NULL
	}
	break;

	default:
		break;
	}

	return 0;
}

/**
 * @brief      power management code for application
 * @param	   none
 * @return     none
 */
void blt_pm_proc(void)
{
	static u16 sleep_cnt = 0;
	static u32 sleep_veryvlow_cnt = 0;
	static u32 sleep_vlow_cnt = 0;
	static u32 sleep_vnormal_cnt = 0;
	static u32 afe_comm_err_sleepcnt = 0;
	static app_pm_elapsed_ctx_t sleep_elapsed_ctx = {0};
	u32 sleep_elapsed_sec = app_pm_take_elapsed_seconds(&sleep_elapsed_ctx);

	if (sleep_elapsed_sec != 0u)
	{
#ifdef _DI_SWITCH_SYS_ONOFF
		if (!IsChargerWakeupActive())
		{
			if (!IsKeyWakeupActive())
			{
				sleep_cnt = (u16)(sleep_cnt + sleep_elapsed_sec);
				if (sleep_cnt >= 3u)
				{
					sleep_cnt = 0;
					app_note_sleep_and_enter_deepsleep(1u); // deepsleep
				}
			}
			else
			{
				sleep_cnt = 0;
			}
		}
		else
		{
			sleep_cnt = 0;
		}
#endif

		if (g_stCellInfoReport.u16VCellMin < 2550)
		{
			sleep_vlow_cnt = 0;
			sleep_vnormal_cnt = 0;
			afe_comm_err_sleepcnt = 0;

			sleep_veryvlow_cnt += sleep_elapsed_sec;
			if (sleep_veryvlow_cnt >= (60 * 60 * 1))
			{
				cpu_set_gpio_wakeup(SW_PIN, Level_Low, 0);

				sleep_veryvlow_cnt = 0;
				app_note_sleep_and_enter_deepsleep(1u); // deepsleep
			}
		}
		// else if ((g_stCellInfoReport.u16VCellMin <= 2750 && !g_stCellInfoReport.u16Ichg) || deepsleep_en)
		else if ((g_stCellInfoReport.u16VCellMin < __SLEEP_VLOW__))
		{
			sleep_veryvlow_cnt = 0;
			sleep_vnormal_cnt = 0;
			afe_comm_err_sleepcnt = 0;
			// if(deepsleep_en) {
			// 	deepsleep_en = false;
			// 	sleep_vlow_cnt = (60 * 60 * 1);
			// }
			sleep_vlow_cnt += sleep_elapsed_sec;
			if (sleep_vlow_cnt >= __SLEEP_TIMEVLOW__)
			{
				cpu_set_gpio_wakeup(SW_PIN, Level_Low, 0);

				sleep_vlow_cnt = 0;
				app_note_sleep_and_enter_deepsleep(1u); // deepsleep
			}
		}
		else if ((g_stCellInfoReport.u16VCellMin < __SLEEP_VNORMAL__ && !g_stCellInfoReport.u16Ichg))
		{
			sleep_veryvlow_cnt = 0;
			sleep_vlow_cnt = 0;
			afe_comm_err_sleepcnt = 0;

			sleep_vnormal_cnt += sleep_elapsed_sec;
			if (sleep_vnormal_cnt >= __SLEEP_TIMENORMAL__)
			// if (sleep_vnormal_cnt >= (60 * 30))
			{
				cpu_set_gpio_wakeup(SW_PIN, Level_Low, 0);

				sleep_vnormal_cnt = 0;
				app_note_sleep_and_enter_deepsleep(1u); // deepsleep
			}
		}
		else if (1 == System_ErrFlag.u8ErrFlag_Com_AFE1)
		{
			sleep_veryvlow_cnt = 0;
			sleep_vlow_cnt = 0;
			sleep_vnormal_cnt = 0;

			afe_comm_err_sleepcnt += sleep_elapsed_sec;
			if (afe_comm_err_sleepcnt >= (60 * 30))
			{
				afe_comm_err_sleepcnt = 0;
				cpu_set_gpio_wakeup(SW_PIN, Level_Low, 0);

				app_note_sleep_and_enter_deepsleep(1u); // deepsleep
			}
		}
		else
		{
			sleep_veryvlow_cnt = 0;
			sleep_vlow_cnt = 0;
			sleep_vnormal_cnt = 0;
			afe_comm_err_sleepcnt = 0;
		}
	}

#if 0
#if (BLE_APP_PM_ENABLE)
	if(blc_ll_getCurrentState() == BLS_LINK_STATE_IDLE){ //PM module can not manage Idle state low power.
		/* user manage BLE Idle state sleep with API "cpu_sleep_wakeup" */
#if (!TEST_CONN_CURRENT_ENABLE) // test connection power, should disable deepSleep
			if(sendTerminate_before_enterDeep == 2){  //Terminate OK
				analog_write(USED_DEEP_ANA_REG, analog_read(USED_DEEP_ANA_REG) | CONN_DEEP_FLG);
				app_note_sleep_and_enter_deepsleep(0u);  //deepSleep
			}
#endif
	}
	else{ //PM module manage advertising and ACL connection Slave role low power only

#if (PM_DEEPSLEEP_RETENTION_ENABLE)
			bls_pm_setSuspendMask (SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
#else
			bls_pm_setSuspendMask (SUSPEND_ADV | SUSPEND_CONN);
#endif


		//do not care about keyScan/button_detect power here, if you care about this, please refer to "ble_remote" demo
			if(0){
			}
#if (UI_KEYBOARD_ENABLE)
			else if(scan_pin_need || key_not_released){
				bls_pm_setSuspendMask (SUSPEND_DISABLE);
			}
#elif (UI_BUTTON_ENABLE)
			else if(button_not_released){
				bls_pm_setSuspendMask (SUSPEND_DISABLE);
			}
#endif
			else if(ota_is_working){
				bls_pm_setManualLatency(0);
			}

			// if(!gpio_read(CHG_IN_PIN) || g_stCellInfoReport.u16IDischg || )
			if(!gpio_read(CHG_IN_PIN) ||
				BUS_STATE_OWC_IDLE != bus_mux_get_state() || 
				g_stCellInfoReport.u16IDischg 
				)
			{
				bls_pm_setSuspendMask (SUSPEND_DISABLE);
			}

#if 0
#if (!TEST_CONN_CURRENT_ENABLE) // test connection power, should disable deepSleep
			if(!ota_is_working && !blc_ll_isControllerEventPending()){  //no controller event pending
				/* enter deepsleep mode after advertising for 60 seconds without being connected. */
				if( blc_ll_getCurrentState() == BLS_LINK_STATE_ADV && !sendTerminate_before_enterDeep && \
					clock_time_exceed(advertise_begin_tick , ADV_IDLE_ENTER_DEEP_TIME * 1000000)){
					cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);  //deepsleep
				}
				/* enter deepsleep mode after 60 seconds without any UI action(key/voice/led) in connection state. */
				else if( device_in_connection_state && \
						clock_time_exceed(latest_user_event_tick, CONN_IDLE_ENTER_DEEP_TIME * 1000000) ){
					bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN); //push terminate command into BLE TX buffer
					sendTerminate_before_enterDeep = 1;
				}
			}
#endif							// end of !TEST_CONN_CURRENT_ENABLE
#endif
	}
#endif // end of BLE_APP_PM_ENABLE
#endif // end of BLE_APP_PM_ENABLE

	bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
	sys_time.low_power_mode = true;
	// do not care about keyScan/button_detect power here, if you care about this, please refer to "ble_remote" demo
	if (0)
	{
	}
#if (UI_KEYBOARD_ENABLE)
	else if (scan_pin_need || key_not_released)
	{
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
	}
#elif (UI_BUTTON_ENABLE)
	else if (button_not_released)
	{
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
	}
#endif
#if (BLE_OTA_SERVER_ENABLE)
	else if (ota_is_working)
	{
		sys_time.low_power_mode = false;
		bls_pm_setManualLatency(0);
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
		quit_rtc_mode();
	}
#endif

	// if(!gpio_read(CHG_IN_PIN) || g_stCellInfoReport.u16IDischg || )
	if (!gpio_read(CHG_IN_PIN) ||
		BUS_STATE_OWC_IDLE != bus_mux_get_state() ||
		g_stCellInfoReport.u16IDischg ||
		MODE_FACTORY == Runtime_GetMode() ||
		ota_is_working)
	// if(
	// 	g_stCellInfoReport.u16IDischg
	// 	)
	{
		sys_time.low_power_mode = false;
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
		quit_rtc_mode();
	}
	else if (device_in_connection_state)
	{
		sys_time.low_power_mode = false;
		quit_rtc_mode();
	}
	else
	{
		enter_rtc_mode();
	}
}

/**
 * @brief		user initialization when MCU power on or wake_up from deepSleep mode
 * @param[in]	none
 * @return      none
 */
_attribute_no_inline_ void user_init_normal(void)
{

	//////////////////////////// basic hardware Initialization  Begin //////////////////////////////////

	/* random number generator must be initiated before any BLE stack initialization.
	 * When deepSleep retention wakeUp, no need initialize again */
#if (MCU_CORE_TYPE == MCU_CORE_825x || MCU_CORE_TYPE == MCU_CORE_827x)
	random_generator_init(); // this is must
#endif

//	debug init
#if (UART_PRINT_DEBUG_ENABLE)
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
	blc_appRegisterStackFlashOperationCallback(app_flash_protection_operation); // register flash operation callback for stack
#endif

	//////////////////////////// basic hardware Initialization  End //////////////////////////////////

	//////////////////////////// BLE stack Initialization  Begin //////////////////////////////////
	//////////// Controller Initialization  Begin /////////////////////////
	u8 mac_public[6];
	u8 mac_random_static[6];
	/* for 512K Flash, flash_sector_mac_address equals to 0x76000, for 1M  Flash, flash_sector_mac_address equals to 0xFF000 */
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

	blc_ll_initBasicMCU();					   // mandatory
	blc_ll_initStandby_module(mac_public);	   // mandatory
	blc_ll_initAdvertising_module(mac_public); // legacy advertising module: mandatory for BLE slave
	blc_ll_initConnection_module();			   // connection module  mandatory for BLE slave/master
	blc_ll_initSlaveRole_module();			   // slave module: 	 mandatory for BLE slave,
	//////////// Controller Initialization  End /////////////////////////

	//////////// Host Initialization  Begin /////////////////////////
	/* Host Initialization */
	/* GAP initialization must be done before any other host feature initialization !!! */
	blc_gap_peripheral_init();							  // gap initialization
	blc_l2cap_register_handler(blc_l2cap_packet_receive); // l2cap initialization
	my_att_init();										  // gatt initialization
	blc_att_setRxMtuSize(MTU_SIZE_SETTING);				  // set MTU size, default MTU is 23 if not call this API

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
	blc_smp_configSecurityRequestSending(SecReq_IMM_SEND, SecReq_PEND_SEND, 1000); // if not set, default is:  send "security request" immediately after link layer connection established(regardless of new connection or reconnection)
#else
	blc_smp_setSecurityLevel(No_Security);
#endif

	/* host(GAP/SMP/GATT/ATT) event process: register host event callback and set event mask */
	blc_gap_registerHostEventHandler(app_host_event_callback);
	/* enable some frequently-used host event by default, user can add more host event */
	blc_gap_setEventMask(GAP_EVT_MASK_SMP_PAIRING_BEGIN |
						 GAP_EVT_MASK_SMP_PAIRING_SUCCESS |
						 GAP_EVT_MASK_SMP_PAIRING_FAIL |
						 GAP_EVT_MASK_ATT_EXCHANGE_MTU);
//////////// Host Initialization  End /////////////////////////

//////////// Service Initialization  Begin /////////////////////////
#if (BLE_OTA_SERVER_ENABLE)
////////////////// OTA relative ////////////////////////
#if (UART_PRINT_DEBUG_ENABLE)
	blc_debug_addStackLog(STK_LOG_OTA_FLOW);
#endif
	blc_ota_initOtaServer_module();

	blc_ota_setOtaProcessTimeout(APP_OTA_PROCESS_TIMEOUT_S);
	blc_ota_setOtaDataPacketTimeout(APP_OTA_DATA_PACKET_TIMEOUT_S);
	blc_ota_registerOtaStartCmdCb(app_enter_ota_mode);
	blc_ota_registerOtaResultIndicationCb(app_ota_end_result);
#endif
	//////////// Service Initialization  End   /////////////////////////

	//////////////////////////// BLE stack Initialization  End //////////////////////////////////

	//////////////////////////// User Configuration for BLE application ////////////////////////////
	////////////////// config ADV packet /////////////////////
	u8 adv_param_status = BLE_SUCCESS;
#if (BLE_APP_SECURITY_ENABLE)
	u8 bond_number = blc_smp_param_getCurrentBondingDeviceNumber(); // get bonded device number
	smp_param_save_t bondInfo;
	if (bond_number) // at least 1 bonding device exist
	{
		bls_smp_param_loadByIndex(bond_number - 1, &bondInfo); // get the latest bonding device (index: bond_number-1 )
	}

	if (bond_number) // set direct ADV
	{
		/* set direct ADV
		 * bondInfo.peer_addr_type & bondInfo.peer_addr is the address in the air packet of "CONNECT_IND" PDU stored in Flash.
		 * if peer address is IDA(identity address), bondInfo.peer_addr is OK used here.
		 * if peer address is RPA(resolved private address), bondInfo.peer_addr is one RPA peer device has used, it has a correct relation
		 * with peer IRK, so it can match to peer device at any time even peer device changes it's RPA. */
		adv_param_status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
											  ADV_TYPE_CONNECTABLE_DIRECTED_LOW_DUTY, app_own_address_type,
											  bondInfo.peer_addr_type, bondInfo.peer_addr,
											  MY_APP_ADV_CHANNEL,
											  ADV_FP_NONE);

		/* If IRK distributed by peer device is valid, peer device may use RPA(resolved private address) at any time,
		 * even if it used IDA(identity address) in first pairing phase.
		 * So here must add peer IRK to resolving list and enable address resolution, since local device should check if
		 * "CONNECT_IND" PDU is sent by the device directed to.
		 * attention: local RPA not used, so parameter "local_irk" set to NULL */
		if (blc_app_isIrkValid(bondInfo.peer_irk))
		{
			blc_ll_addDeviceToResolvingList(bondInfo.peer_id_adrType, bondInfo.peer_id_addr, bondInfo.peer_irk, NULL);
			blc_ll_setAddressResolutionEnable(1);
		}

		// it is recommended that direct ADV only last for several seconds, then switch to undirected adv
		bls_ll_setAdvDuration(MY_DIRECT_ADV_TIME, 1);
		bls_app_registerEventCallback(BLT_EV_FLAG_ADV_DURATION_TIMEOUT, &app_switch_to_undirected_adv);
	}
	else // set undirected adv
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
	bls_ll_setAdvEnable(BLC_ADV_ENABLE); // ADV enable

	/* set RF power index, user must set it after every suspend wake_up, because relative setting will be reset in suspend */
	rf_set_power_level_index(MY_RF_POWER_INDEX);

	bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &task_connect);
	bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &task_terminate);
	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &task_suspend_exit);
	bls_app_registerEventCallback(BLT_EV_FLAG_DATA_LENGTH_EXCHANGE, &task_dle_exchange);

///////////////////// Power Management initialization///////////////////
#if (BLE_APP_PM_ENABLE)
	blc_ll_initPowerManagement_module();

#if (PM_DEEPSLEEP_RETENTION_ENABLE)
	blc_app_setDeepsleepRetentionSramSize(); // select DEEPSLEEP_MODE_RET_SRAM_LOW16K or DEEPSLEEP_MODE_RET_SRAM_LOW32K
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
		// bus_mux_task();
		// nvm_init(&nvm_cfg);
		init_bms_io();
		LoadParam();
		Param_UpgradeReset_Apply();
		bms_event_log_init();

		bms_afe_bus_init();
		WaitMs(100);

		// todo 待测试 , 断线检测测试
		bms_afe_reset();
		bms_afe_is_ready();
		bms_afe_apply_params();

		adc_init_common();
		cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 1);
		cpu_set_gpio_wakeup(SW_PIN, Level_Low, 1);

		/* 先取一帧电压/电流快照，给 SOC 启动合理性校正提供输入。 */
		bms_afe_sample();
		soc_kv_store_init();
		soc_kv_data_t d = soc_kv_store_get();
		// d.soc = 100;
		soc_param_lib_init(&d);
	}

	app_timer_test_init();

	bus_mux_init();
	btname_init();
	bms_event_log_note_startup();

	Runtime_Init();

	if (IsChargerWakeupActive())
	{
		open_chg_close_dsg();
	}
	else
	{
		if (MODE_FACTORY == Runtime_GetMode())
		{
			factory_mode_apply_switch_state();
		}
		else
		{
			open_dsg_close_chg();
		}
	}

	extern void WriteProID_Default(void);
	WriteProID_Default();
	// sys_time.isdebugenable = 1;
	open_ctlc();
}

/**
 * @brief		user initialization when MCU wake_up from deepSleep_retention mode
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void user_init_deepRetn(void)
{
#if (PM_DEEPSLEEP_RETENTION_ENABLE)

	blc_app_loadCustomizedParameters_deepRetn();

	blc_ll_initBasicMCU(); // mandatory
	rf_set_power_level_index(MY_RF_POWER_INDEX);

	blc_ll_recoverDeepRetention();

#if (APP_BATT_CHECK_ENABLE)
	/* ADC settings will lost during deepsleep retention mode, so here need clear flag */
	battery_clear_adc_setting_flag();
#endif

	DBG_CHN0_HIGH; // debug

	irq_enable();

#if (UI_KEYBOARD_ENABLE)
	/////////// keyboard GPIO wake_up initialization ////////
	u32 pin[] = KB_DRIVE_PINS;
	for (int i = 0; i < (sizeof(pin) / sizeof(*pin)); i++)
	{
		cpu_set_gpio_wakeup(pin[i], Level_High, 1); // drive pin high level wake_up deepsleep
	}
#elif (UI_BUTTON_ENABLE)

	cpu_set_gpio_wakeup(SW1_GPIO, Level_Low, 1); // button pin pad low wakeUp suspend/deepSleep
	cpu_set_gpio_wakeup(SW2_GPIO, Level_Low, 1); // button pin pad low wakeUp suspend/deepSleep
#endif
#endif
}

#if (APP_FLASH_PROTECTION_ENABLE)

/**
 * @brief      flash protection operation, including all locking & unlocking for application
 * 			   handle all flash write & erase action for this demo code. use should add more more if they have more flash operation.
 * @param[in]  flash_op_evt - flash operation event, including application layer action and stack layer action event(OTA write & erase)
 * 			   attention 1: if you have more flash write or erase action, you should should add more type and process them
 * 			   attention 2: for "end" event, no need to pay attention on op_addr_begin & op_addr_end, we set them to 0 for
 * 			   			    stack event, such as stack OTA write new firmware end event
 * @param[in]  op_addr_begin - operating flash address range begin value
 * @param[in]  op_addr_end - operating flash address range end value
 * 			   attention that, we use: [op_addr_begin, op_addr_end)
 * 			   e.g. if we write flash sector from 0x10000 to 0x20000, actual operating flash address is 0x10000 ~ 0x1FFFF
 * 			   		but we use [0x10000, 0x20000):  op_addr_begin = 0x10000, op_addr_end = 0x20000
 * @return     none
 */
_attribute_data_retention_ u16 flash_lockBlock_cmd = 0;
_attribute_data_retention_ static u8 g_app_flash_stack_session_active = 0;

int app_flash_lock_restore_enabled(void)
{
	return (g_app_flash_stack_session_active == 0u);
}

void app_flash_protection_operation(u8 flash_op_evt, u32 op_addr_begin, u32 op_addr_end)
{
	if (flash_op_evt == FLASH_OP_EVT_APP_INITIALIZATION)
	{
		g_app_flash_stack_session_active = 0u;

		/* ignore "op addr_begin" and "op addr_end" for initialization event
		 * must call "flash protection_init" first, will choose correct flash protection relative API according to current internal flash type in MCU */
		flash_protection_init();

		/* just sample code here, protect all flash area for old firmware and OTA new firmware.
		 * user can change this design if have other consideration */
		u32 app_lockBlock = 0;
#if (BLE_OTA_SERVER_ENABLE)
		u32 multiBootAddress = blc_ota_getCurrentUsedMultipleBootAddress();
		if (multiBootAddress == MULTI_BOOT_ADDR_0x20000)
		{
			app_lockBlock = FLASH_LOCK_FW_LOW_256K;
		}
		else if (multiBootAddress == MULTI_BOOT_ADDR_0x40000)
		{
			/* attention that 512K capacity flash can not lock all 512K area, should leave some upper sector
			 * for system data(SMP storage data & calibration data & MAC address) and user data
			 * will use a approximate value */
			app_lockBlock = FLASH_LOCK_FW_LOW_512K;
		}
#if (MCU_CORE_TYPE == MCU_CORE_827x)
		else if (multiBootAddress == MULTI_BOOT_ADDR_0x80000)
		{
			if (blc_flash_capacity < FLASH_SIZE_1M)
			{ // for flash capacity smaller than 1M, OTA can not use 512K as multiple boot address
				blc_flashProt.init_err = 1;
			}
			else
			{
				/* attention that 1M capacity flash can not lock all 1M area, should leave some upper sector for
				 * system data(SMP storage data & calibration data & MAC address) and user data
				 * will use a approximate value */
				app_lockBlock = FLASH_LOCK_FW_LOW_1M;
			}
		}
#endif
#else
		app_lockBlock = FLASH_LOCK_FW_LOW_256K; // just demo value, user can change this value according to application
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

		/* OTA clear old firmware begin event is triggered by stack, in "blc ota_initOtaServer_module", rebooting from a successful OTA.
		 * Software will erase whole old firmware for potential next new OTA, need unlock flash if any part of flash address from
		 * "op addr_begin" to "op addr_end" is in locking block area.
		 * In this sample code, we protect whole flash area for old and new firmware, so here we do not need judge "op addr_begin" and "op addr_end",
		 * must unlock flash */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA clear old FW begin, unlock flash\n");
		flash_unlock();
	}
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_END)
	{
		g_app_flash_stack_session_active = 0u;

		/* ignore "op addr_begin" and "op addr_end" for END event
		 * OTA clear old firmware end event is triggered by stack, in "blc ota_initOtaServer_module", erasing old firmware data finished.
		 * In this sample code, we need lock flash again, because we have unlocked it at the begin event of clear old firmware */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA clear old FW end, restore flash locking\n");
		flash_lock(flash_lockBlock_cmd);
	}
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_BEGIN)
	{
		g_app_flash_stack_session_active = 1u;

		/* OTA write new firmware begin event is triggered by stack, when receive first OTA data PDU.
		 * Software will write data to flash on new firmware area,  need unlock flash if any part of flash address from
		 * "op addr_begin" to "op addr_end" is in locking block area.
		 * In this sample code, we protect whole flash area for old and new firmware, so here we do not need judge "op addr_begin" and "op addr_end",
		 * must unlock flash */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA write new FW begin, unlock flash\n");
		flash_unlock();
	}
	else if (flash_op_evt == FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_END)
	{
		g_app_flash_stack_session_active = 0u;

		/* ignore "op addr_begin" and "op addr_end" for END event
		 * OTA write new firmware end event is triggered by stack, after OTA end or an OTA error happens, writing new firmware data finished.
		 * In this sample code, we need lock flash again, because we have unlocked it at the begin event of write new firmware */
		tlkapi_printf(APP_FLASH_PROT_LOG_EN, "[FLASH][PROT] OTA write new FW end, restore flash locking\n");
		flash_lock(flash_lockBlock_cmd);
	}
#endif
	/* add more flash protection operation for your application if needed */
}

#endif

/////////////////////////////////////////////////////////////////////s
// main loop flow
/////////////////////////////////////////////////////////////////////

_attribute_data_retention_ static u32 test_task_tick = 0;

/**
 * @brief		This is main_loop function
 * @param[in]	none
 * @return      none
 */
_attribute_no_inline_ void main_loop(void)
{
	////////////////////////////////////// BLE entry /////////////////////////////////
	blt_sdk_main_loop();
	Runtime_Poll();
	////////////////////////////////////// UI entry /////////////////////////////////
	///////////////////////////////////// Battery Check ////////////////////////////////

#if (APP_BATT_CHECK_ENABLE)
	/*The frequency of low battery detect is controlled by the variable lowBattDet_tick, which is executed every
	 500ms in the demo. Users can modify this time according to their needs.*/
	if (battery_get_detect_enable() && clock_time_exceed(lowBattDet_tick, 500000))
	{
		lowBattDet_tick = clock_time();
		user_battery_power_check(VBAT_DEEP_THRES_MV);
	}
#endif
	if (clock_time_exceed(test_task_tick, 1000 * 200))
	{
		test_task_tick = clock_time();
		tlkapi_printf(APP_LOG_EN, "hello World!!!\n");
		APP_SOC_IntEnhance_Ctrl();
		charger_detect_and_keyLogi_200ms();
	}
	_attribute_data_retention_ static u32 update_bms_info_tick = 0;
	if (clock_time_exceed(update_bms_info_tick, 1000 * 1000))
	{
		// todo 低功耗，时基偏移
		update_bms_info_tick = clock_time();
		bms_afe_sample();
		app_adc_multi_sample();
		app_event_log_1s_task();
	}
	extern uint16_t get_idle_stable_ticks(void);
	extern uint16_t get_idle_adjust_ticks(void);
	// g_stCellInfoReport.u16VCell[30] = get_idle_stable_ticks();
	// g_stCellInfoReport.u16VCell[31] = get_idle_adjust_ticks();

	bus_mux_task();
#ifdef _FUNC_UART_
	main_loop_modbus();
#endif
	soc_kv_store_update_and_log_if_changed(SOC_Calculate_Element.u8SOC_Now, SOC_Calculate_Element.u8DSG_SOC_Int, SOC_Calculate_Element.u32Cycle_times);
	// soc_kv_store_update_and_log_if_changed(g_stCellInfoReport.SocElement.u16Soc, SOC_Calculate_Element.u8DSG_SOC_Int, SOC_Calculate_Element.u32Cycle_times);
	// nvm_process();
	////////////////////////////////////// PM Process /////////////////////////////////
	// test_SH367309_UpdataAfeConfig();

	extern void test_log_app(void);
	extern void test_log_balance_first(void);
	// if(sys_time.enable_log_test_first)
	// {
	// 	sys_time.enable_log_test_first = false;
	// 	test_log_app();
	// }
	// if(sys_time.enable_log_test_balance)
	// {
	// 	sys_time.enable_log_test_balance = false;
	// 	test_log_balance_first();
	// }
	blt_pm_proc();
}
