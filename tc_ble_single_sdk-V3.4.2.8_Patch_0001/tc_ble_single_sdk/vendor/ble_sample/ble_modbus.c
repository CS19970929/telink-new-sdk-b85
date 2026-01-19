#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "ble_modbus.h"
#include "conf.h"
#include "app_att.h"
#include "sci_upper.h"
#include "sh367309_datadeal.h"
#include "param.h"

bool rev_master = false;
extern _attribute_data_retention_	int device_in_connection_state;
extern struct stCell_Info g_stCellInfoReport;

#define TELINK_NOTIFY_PAYLOAD 20 // MTU=23 閺冿拷 payload = 20

#define MAX_TEST_DATA_LEN 1024

extern UINT8 FaultPoint_First2;
extern UINT8 FaultPoint_Second2;
extern UINT8 FaultPoint_Third2;
extern UINT16 Fault_record_First2[Record_len];
extern UINT16 Fault_record_Second2[Record_len];
extern UINT16 Fault_record_Third2[Record_len];

// u8 test_buf[MAX_TEST_DATA_LEN];
u8 test_buf[MAX_TEST_DATA_LEN] = {
	0x01, 0x03, 0x4C,
	0x0C, 0x6D, 0x0C, 0x6C, 0x0C, 0x6C, 0x0C, 0x5C,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0x0C, 0x6D, 0x0C, 0x5C, 0x00, 0x01, 0x00, 0x04,
	0x00, 0x11, 0x04, 0xF6,
	0xAD, 0x2E};


u16 Sci_CRC16RTU(u8 *pszBuf, u8 unLength)
{
	u16 CRCC = 0XFFFF;
	u32 CRC_count;

	for (CRC_count = 0; CRC_count < unLength; CRC_count++)
	{
		int i;

		CRCC = CRCC ^ *(pszBuf + CRC_count);

		for (i = 0; i < 8; i++)
		{
			if (CRCC & 1)
			{
				CRCC >>= 1;
				CRCC ^= 0xA001;
			}
			else
			{
				CRCC >>= 1;
			}
		}
	}

	return CRCC;
}


ble_sts_t notify_big_packet(u16 conn, u16 handle, u8 *data, u16 len)
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
			printf("Send FAIL offset=%d ret=%x\r\n", offset, ret);
			return ret;
		}

		offset += chunk;

		// Telink 閻楄锟窖嶇窗闂囷拷鐟曚胶绮扮�靛湱顏稉锟介悙瑙勬闂傝揪绱濋崥锕�鍨� notify 娴兼俺顫﹂崥锟�
		// sleep_us(800);   // 0.8ms鐡掑啿顧勭�瑰鍙�
	}

	return BLE_SUCCESS;
}


void notify_protect_status(void)
{
	INT8 k;
	UINT8 a[4];
	UINT16 i = 0, j;
	UINT16 u16SciTemp;
	// printf(" notify_protect_status");
	int len = 3 + 21 * 2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 21 * 2;

	u16 protect_status[21] = {0};
	protect_status[0] = 0;
	protect_status[1] = 0;
	protect_status[2] = 0;
	for (j = 0; j < 4; j++)
	{
		k = FaultPoint_First2 - 1 - j;
		if (k < 0)
		{
			k = Record_len + k;
		}
		a[j] = k;
	}
	protect_status[3] = (Fault_record_First2[a[0]] << 8) | Fault_record_First2[a[1]];
	protect_status[4] = (Fault_record_First2[a[2]] << 8) | Fault_record_First2[a[3]];
	for (j = 0; j < 4; j++)
	{
		k = FaultPoint_Second2 - 1 - j;
		if (k < 0)
		{
			k = Record_len + k;
		}
		a[j] = k;
	}
	protect_status[5] = (Fault_record_Second2[a[0]] << 8) | Fault_record_Second2[a[1]];
	protect_status[6] = (Fault_record_Second2[a[2]] << 8) | Fault_record_Second2[a[3]];
	for (j = 0; j < 4; j++)
	{
		k = FaultPoint_Third2 - 1 - j;
		if (k < 0)
		{
			k = Record_len + k;
		}
		a[j] = k;
	}
	protect_status[7] = (Fault_record_Third2[a[0]] << 8) | Fault_record_Third2[a[1]];
	protect_status[8] = (Fault_record_Third2[a[2]] << 8) | Fault_record_Third2[a[3]];
	i = 8;

	protect_status[18] = 0xffff;
	protect_status[19] = 0xffff;
	protect_status[20] = 0xffff;
	// for (j = 0; j < 12; j++)
	// { // 0xD002锟斤拷锟斤拷锟斤。
	// 	u16SciTemp = ((*(&System_ErrFlag.u8ErrFlag_Com_AFE1 + 2 * j)) << 8) | (*(&System_ErrFlag.u8ErrFlag_Com_AFE1 + 2 * j + 1));
	// 	protect_status[i++] = (u16SciTemp >> 8) & 0x00FF;
	// 	protect_status[i++] = u16SciTemp & 0x00FF;
	// }

	for (i = 0; i < 21; i++)
	{
		test_buf[3 + i * 2] = protect_status[i] >> 8;
		test_buf[4 + i * 2] = protect_status[i] & 0xff;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	// u16 crc = 0x4567;
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	ble_sts_t r = notify_big_packet(
		BLS_CONN_HANDLE,
		SPP_CLIENT_TO_SERVER_DP_H,
		test_buf,
		len);
}

void notify_soc(void)
{
	// printf("notify_soc");
	int len = 3 + 25 * 2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 25 * 2;

	size_t i;
	for (i = 0; i < 25; i++)
	{
		uint16_t u16SciTemp = *(&g_stCellInfoReport.u16Temperature[0] + i);
		test_buf[3 + i * 2] = u16SciTemp >> 8;
		test_buf[4 + i * 2] = u16SciTemp & 0xff;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	// u16 crc = 0x2345;
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	ble_sts_t r = notify_big_packet(
		BLS_CONN_HANDLE,
		SPP_CLIENT_TO_SERVER_DP_H, // 娴ｇ姷娈� notify 閸欍儲鐒�
		test_buf,
		len);
}

void notify_protect_prarm(void)
{
	// printf("notify_protect_prarm");
	int len = 3 + 65 * 2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 65 * 2;
	// u16 *p = &g_tParam.protect;
	u16 *p = &g_tParam.protect;

	size_t i;
	for (i = 0; i < 65; i++)
	{
		// test_buf[3 + i * 2] =  protect_para[i] >> 8;
		// test_buf[4 + i * 2] =  protect_para[i] & 0xff;
		test_buf[3 + i * 2] = *p >> 8;
		test_buf[4 + i * 2] = *p & 0xff;
		p++;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	ble_sts_t r = notify_big_packet(
		BLS_CONN_HANDLE,
		SPP_CLIENT_TO_SERVER_DP_H, // 娴ｇ姷娈� notify 閸欍儲鐒�
		test_buf,
		len);
}

void notify_votage(void)
{
	int len = 3 + 38 * 2 + 2; // 娴ｇ姵鍏傚ù瀣樋鐏忔垵姘ㄦ繅顐㈩樋鐏忥拷
	// printf("notify voltage");

	static u8 vol_cnt = 0;
	vol_cnt++;
	{
		test_buf[0] = 0x01;
		test_buf[1] = 0x03;
		test_buf[2] = 38 * 2;

		for (size_t i = 0; i < 39; i++)
		{
			test_buf[3 + i * 2] = 61001 >> 8;
			test_buf[4 + i * 2] = 61001 & 0xff;

			if (i <= 13)
			{
				int temp = g_stCellInfoReport.u16VCell[i];
				test_buf[3 + i * 2] = temp >> 8;
				test_buf[4 + i * 2] = temp & 0xff;
			}

			if (i == 32)
			{
				test_buf[3 + i * 2] = (g_stCellInfoReport.u16VCellMax) >> 8;
				test_buf[4 + i * 2] = (g_stCellInfoReport.u16VCellMax) & 0xff;
			}
			else if (i == 33)
			{
				test_buf[3 + i * 2] = (g_stCellInfoReport.u16VCellMin) >> 8;
				test_buf[4 + i * 2] = (g_stCellInfoReport.u16VCellMin) & 0xff;
			}
			else if (i == 34)
			{
				test_buf[3 + i * 2] = g_stCellInfoReport.u16VCellMaxPosition >> 8;
				test_buf[4 + i * 2] = g_stCellInfoReport.u16VCellMaxPosition & 0xff;
			}
			else if (i == 35)
			{
				test_buf[3 + i * 2] = g_stCellInfoReport.u16VCellMinPosition >> 8;
				test_buf[4 + i * 2] = g_stCellInfoReport.u16VCellMinPosition & 0xff;
			}
			else if (i == 36)
			{
				test_buf[3 + i * 2] = (g_stCellInfoReport.u16VCellDelta) >> 8;
				test_buf[4 + i * 2] = (g_stCellInfoReport.u16VCellDelta) & 0xff;
			}
			else if (i == 37)
			{
				test_buf[3 + i * 2] = (g_stCellInfoReport.u16VCellTotle) >> 8;
				test_buf[4 + i * 2] = (g_stCellInfoReport.u16VCellTotle) & 0xff;
			}
			else if (i == 38)
			{
				u16 crc = Sci_CRC16RTU(test_buf, len - 2);
				// u16 crc = 0x1234;
				test_buf[3 + i * 2] = crc & 0xff;
				test_buf[4 + i * 2] = crc >> 8;
			}
		}
	}

	ble_sts_t r = notify_big_packet(
		BLS_CONN_HANDLE,
		SPP_CLIENT_TO_SERVER_DP_H, // 娴ｇ姷娈� notify 閸欍儲鐒�
		test_buf,
		len);
}

void notify_other_status(void)
{
	// printf("notify_other_status");
	int len = 3 + 12 * 2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 12 * 2;
	u16 other_status[12] = {0};
	extern volatile union System_Status SystemStatus;
	other_status[0] = (UINT16)(SystemStatus.all & 0x0000FFFF);
	other_status[1] = (UINT16)(SystemStatus.all >> 16);
	// other_status[2] = (UINT16)(System_OnOFF_Func.all & 0x0000FFFF);
	// other_status[3] = (UINT16)(System_OnOFF_Func.all >> 16);
	size_t i;
	for (i = 2; i < 12; i++)
		other_status[i] = 1;
	for (i = 0; i < 12; i++)
	{
		test_buf[3 + i * 2] = other_status[i] >> 8;
		test_buf[4 + i * 2] = other_status[i] & 0xff;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	// u16 crc = 0x3456;
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	ble_sts_t r = notify_big_packet(
		BLS_CONN_HANDLE,
		SPP_CLIENT_TO_SERVER_DP_H, // 浣犵殑 notify 鍙ユ焺
		test_buf,
		len);
}

void app_ble_modbus(void)
{
    if (device_in_connection_state && rev_master)
    {
        extern u16 addr;
        rev_master = false;
        if (addr == 0xd000)
            notify_votage();
        else if (addr == 0x2100)
            notify_protect_prarm();
        else if (addr == 0xd026)
            notify_soc();
        else if (addr == 0xd115)
            notify_other_status();
        else if (addr == 0xd100)
            notify_protect_status();
    }
}
