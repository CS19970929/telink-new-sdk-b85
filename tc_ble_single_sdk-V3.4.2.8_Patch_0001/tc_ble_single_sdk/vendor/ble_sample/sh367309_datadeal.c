#include "sh367309_datadeal.h"
#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "sci_upper.h"
#include "app.h"

int AFE_PARAM_WRITE_Flag = 1;
int AFE_ResetFlag = 0;
extern struct stCell_Info g_stCellInfoReport;

UINT32 u32_ChgCur_mA = 0;
UINT32 u32_DsgCur_mA = 0;
u8 System_ERROR_UserCallback(enum SYSTEM_ERROR_COMMAND errorCode);
volatile union System_Status SystemStatus;

UINT8 FaultPoint_First2;
UINT8 FaultPoint_Second2;
UINT8 FaultPoint_Third2;

UINT16 Fault_record_First2[Record_len];
UINT16 Fault_record_Second2[Record_len];
UINT16 Fault_record_Third2[Record_len];

UINT8 Monitor_TempBreak(UINT16 *temp_AD)
{
    static UINT8 su8_Recover_Cnt = 0;
    static UINT8 su8_StartUp_Flag = 0;
    static UINT8 su8_Delay_Cnt = 0;
    UINT8 result = 0;

    switch (su8_StartUp_Flag)
    {
    case 0: // 刚开机，不能判断，因为查询AFE函数已经被分割，不能拿到数据，此时判断必为错
        if (++su8_Delay_Cnt >= 20)
        {
            su8_Delay_Cnt = 0;
            su8_StartUp_Flag = 1;
        }
        break;

    case 1:
        if (*temp_AD < 110)
        {
            ++result;
            *temp_AD = 110; // 定死在-29摄氏度。以防上位机显示NA以为没问题
            System_ERROR_UserCallback(ERROR_TEMP_BREAK);
            su8_Recover_Cnt = 0;
        }
        else
        {
            if (System_ERROR_UserCallback(ERROR_STATUS_TEMP_BREAK))
            {
                if (++su8_Recover_Cnt >= 50)
                { // 判断50次自动复原，约为200*50=10s
                    su8_Recover_Cnt = 0;
                    System_ERROR_UserCallback(ERROR_REMOVE_TEMP_BREAK);
                }
            }
        }
        break;

    default:
        su8_StartUp_Flag = 0;
        break;
    }

    return result;
}

const unsigned char SeriesSelect_AFE1[16][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 1串
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 2串
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 3
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 4
    {0, 1, 2, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 5
    {0, 1, 2, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 6
    {0, 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // 7
    {0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0},      // 8
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0},      // 9
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0},      // 10
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0, 0},     // 11
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0, 0, 0},    // 12
    {0, 1, 2, 3, 4, 5, 6, 7, 9, 9, 10, 11, 12, 0, 0, 0},   // 13
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 0},  // 14
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0}, // 15
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15} // 16
};

#define LENGTH_TBLTEMP_AFE_10K ((UINT16)56)
const UINT16 iSheldTemp_10K_AFE[LENGTH_TBLTEMP_AFE_10K] = {
    // AD(kΩ*100)		(Temp+40)*10
    11611,
    100, //-30
    8935,
    150, //-25
    6943,
    200, //-20
    5442,
    250, //-15
    4300,
    300, //-10
    3422,
    350, //-5
    2751,
    400, // 0
    2214,
    450, // 5
    1801,
    500, // 10
    1470,
    550, // 15
    1209,
    600, // 20
    1000,
    650, // 25
    831,
    700, // 30
    694,
    750, // 35
    583,
    800, // 40
    492,
    850, // 45
    416,
    900, // 50
    355,
    950, // 55
    303,
    1000, // 60
    260,
    1050, // 65
    224,
    1100, // 70
    193,
    1150, // 75
    167,
    1200, // 80
    146,
    1250, // 85
    127,
    1300, // 90
    111,
    1350, // 95
    98,
    1400, // 100
    86,
    1450, // 105
};

u16 iSheldTemp_10K_NTC[141] = {20375, 19204, 18115, 17100, 16152, 15266, 14437, 13661, 12934, 12251,
                               11611, 11008, 10442, 9909, 9407, 8935, 8489, 8068, 7672, 7297,
                               6943, 6608, 6292, 5993, 5710, 5442, 5188, 4948, 4720, 4504,
                               4300, 4105, 3921, 3746, 3580, 3422, 3272, 3130, 2994, 2866,
                               2751, 2627, 2516, 2410, 2310, 2214, 2123, 2036, 1953, 1874,
                               1801, 1726, 1658, 1592, 1530, 1470, 1413, 1358, 1306, 1256,
                               1209, 1163, 1119, 1078, 1038, 1000, 963, 928, 894, 862,
                               831, 801, 773, 746, 719, 694, 670, 647, 625, 604,
                               583, 563, 544, 526, 509, 492, 476, 460, 445, 431,
                               416, 403, 390, 378, 366, 355, 343, 333, 322, 312,
                               303, 294, 285, 276, 268, 260, 252, 244, 237, 230,
                               224, 217, 211, 205, 199, 193, 188, 182, 177, 172,
                               167, 163, 158, 154, 150, 146, 142, 138, 134, 131,
                               127, 124, 120, 117, 114, 111, 108, 106, 103, 100,
                               98};

// 鍓�26涓瘎瀛樺櫒榛樿鍙傛暟
u8 ucMTPBuffer[26] = {
    BYTE_00H_SCONF1, BYTE_01H_SCONF2, BYTE_02H_OVT_LDRT_OVH, BYTE_03H_OVL, BYTE_04H_UVT_OVRH,
    BYTE_05H_OVRL, BYTE_06H_UV, BYTE_07H_UVR, BYTE_08H_BALV, BYTE_09H_PREV,
    BYTE_0AH_L0V, BYTE_0BH_PFV, BYTE_0CH_OCD1V_OCD1T, BYTE_0DH_OCD2V_OCD2T, BYTE_0EH_SCV_SCT,
    BYTE_0FH_OCCV_OCCT, BYTE_10H_MOST_OCRT_PFT, BYTE_11H_OTC, BYTE_12H_OTCR, BYTE_13H_UTC,
    BYTE_14H_UTCR, BYTE_15H_OTD, BYTE_16H_OTDR, BYTE_17H_UTD, BYTE_18H_UTDR,
    BYTE_19H_TR};

const u16 AFE_OCD1V_OCCV[16] = {20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 160, 180, 200};                    // 涓�绾ф斁鐢佃繃娴佸拰鍏呯數杩囨祦锛屽崟浣峬v
const u16 AFE_SCV[16] = {50, 80, 110, 140, 170, 200, 230, 260, 290, 320, 350, 400, 500, 600, 800, 1000};                    // 鐭矾淇濇姢鐢靛帇锛屽崟浣峬v
const u16 AFE_OVT_UVT[16] = {100, 200, 300, 400, 600, 800, 1000, 2000, 3000, 4000, 6000, 8000, 10000, 20000, 30000, 40000}; // 杩囧帇浣庡帇寤舵椂鏃堕棿銆傚崟浣峬s
const u16 AFE_SCT[16] = {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};                      // 鐭矾寤舵椂,鍗曚綅us銆�
const u16 AFE_OCD1T[16] = {50, 100, 200, 400, 600, 800, 1000, 2000, 4000, 6000, 8000, 10000, 15000, 20000, 30000, 40000};   // 鏀剧數杩囨祦1寤舵椂銆傚崟浣峬s
const u16 AFE_OCCT_OCD2T[16] = {10, 20, 40, 60, 80, 100, 200, 400, 600, 800, 1000, 2000, 4000, 8000, 10000, 20000};         // 鏀剧數杩囨祦2鍜屽厖鐢佃繃娴佸欢鏃躲�傚崟浣峬s

const u8 CRC8Table[] = { // 120424-1			CRC Table
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3};

AFE_ROM_PARAMETERS_TypeDef AFE_ROM_PARAMETERS_Struction = {0};
AFE_Parameters_RS485_Typedef AFE_Parameters_RS485_Struction = AFE_PARAMETERS_RS485_STRUCTION_DEFAULT;
SH367309_REG_STORE SH367309_Reg_Store;
sh367309_ram_t ram_reg_309;
struct SH367309_Read SH367309_Read_AFE1;

// u8 CRC8cal(u8 *p, u8 Length)
// { // look-up table calculte CRC
//     u8 crc8 = 0;

//     for (; Length > 0; Length--)
//     {
//         crc8 = CRC8Table[crc8 ^ *p];
//         p++;
//     }

//     return (crc8);
// }
static inline int16_t SH309_U8HILO_TO_S16(uint8_t h, uint8_t l)
{
    return (int16_t)((uint16_t)((uint16_t)h << 8) | (uint16_t)l);
}
u8 CRC8cal(const u8 *data, u32 len)
{
    u8 crc = 0x00;
    for (u32 i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (u8 b = 0; b < 8; b++)
        {
            if (crc & 0x80)
                crc = (u8)((crc << 1) ^ 0x07);
            else
                crc = (u8)(crc << 1);
        }
    }
    return crc;
}
void Delay1ms(u8 delaycnt)
{
    WaitMs(delaycnt);
}
u8 TwiWrite(u8 SlaveID, u16 WrAddr, u8 Length, u8 *WrBuf)
{
    u8 i;
    u8 TempBuf[4];
    u8 result = 0;

    TempBuf[0] = SlaveID;
    TempBuf[1] = (u8)WrAddr;
    TempBuf[2] = *WrBuf;
    TempBuf[3] = CRC8cal(TempBuf, 3);

    // i2c_write_series(((u16)WrAddr << 8) | Length, 2, (unsigned char *)WrBuf, Length + 1);
    // i2c_write_series(((u16)WrAddr << 8) | Length, 2, (unsigned char *)TempBuf[2], 2);
    // i2c_write_series(((u16)WrAddr << 8), 1, (unsigned char *)TempBuf[2], 2);
    i2c_write_series(WrAddr, 1, (unsigned char *)&TempBuf[2], 2);
}

int Choose_Right_Value(u16 cur_Value, const u16 *AFE_list)
{
    int i = 0;
    for (i = 0; i < 15; i++)
    {
        if (cur_Value <= AFE_list[i])
        {
            break;
        }
    }
    return i;
}

u8 System_ERROR_UserCallback(enum SYSTEM_ERROR_COMMAND errorCode)
{
    // printf("system error code: %d", errorCode);
    return 0;
}

u8 MTPWrite(u8 WrAddr, u8 Length, u8 *WrBuf)
{
    u8 result;
    u8 i;
    Feed_IWatchDog;

    for (i = 0; i < Length; i++)
    {
        // result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
        TwiWrite(AFE_ID, WrAddr, 1, WrBuf);

        WrAddr++;
        WrBuf++;
        Delay1ms(1);
    }
}
#if 0
u8 MTPWrite(u8 WrAddr, u8 Length, u8 *WrBuf)
{
	u8 result;
	u8 i;
	Feed_IWatchDog;

	for (i = 0; i < Length; i++)
	{
		result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
		if (!result)
		{
			Delay1ms(1);
			result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
			if (!result)
			{
				break;
			}
		}
		WrAddr++;
		WrBuf++;
		Delay1ms(1);
	}

	if (!result)
	{
		System_ERROR_UserCallback(ERROR_AFE1);
	}

	return result;
}
#endif

#if 0
u8 TwiWrite(u8 SlaveID, u16 WrAddr, u8 Length, u8 *WrBuf)
{
    u8 i;
    u8 TempBuf[4];
    u8 result = 0;

    TempBuf[0] = SlaveID;
    TempBuf[1] = (u8)WrAddr;
    TempBuf[2] = *WrBuf;
    TempBuf[3] = CRC8cal(TempBuf, 3);

    if (Length > 0)
    {
        TwiStart();

        if (!TwiSendData(SlaveID, 1))
        { // Send Slave ID
            goto WrErr;
        }

        if (TwiSendData(WrAddr, 0))
        { // Send Write Address(Low 8bit)
            result = 1;
            for (i = 0; i < Length; i++)
            {
                if (TwiSendData(*WrBuf, 0))
                { // Send Write Data
                    WrBuf++;
                }
                else
                {
                    result = 0;
                    break;
                }
            }
            if (!TwiSendData(TempBuf[3], 0))
            { // write CRC
                result = 0;
            }
        }
    WrErr:
        TwiStop();
    }

    return result;
}
#endif
/*******************************************************************************
Function: TwiRead()
Description:  read multi bytes
Input: SlaveID--Slave Address
          RdAddr--register addr
          Length--read data length
          *RdBuf--data buffer
Output: result:1--OK
               0--Error
Others:
********************************************************************************/
u8 TwiRead(u8 SlaveID, u16 RdAddr, u8 Length, u8 *RdBuf)
{
    // todo 鍚庨潰鍔燾rc,rdbuf澶氬姞涓�涓�
    i2c_read_series(((u16)RdAddr << 8) | Length, 2, (unsigned char *)RdBuf, Length + 1);
    // printf("TwiRead\n");
    // array_printf(RdBuf, Length);
}
#if 0
u8 TwiRead(u8 SlaveID, u16 RdAddr, u8 Length, u8 *RdBuf)
{
    u8 i;
    u8 result = 0;
    u8 TempBuf[46];
    u8 RdCrc = 0;

    TempBuf[0] = SlaveID;
    TempBuf[1] = (u8)RdAddr;
    TempBuf[2] = Length;
    TempBuf[3] = SlaveID | 0x01;

    if (Length > 0)
    {
        TwiStart();

        if (!TwiSendData(SlaveID, 1))
        { // Send Slave ID
            goto RdErr;
        }

        if (!TwiSendData(RdAddr, 0))
        { // Send Read Address(Low 8bit)
            goto RdErr;
        }

        if (!TwiSendData(Length, 0))
        {
            goto RdErr;
        }

        TwiReStart();

        if (TwiSendData(SlaveID | 0x1, 0))
        { // Send Slave ID
            result = 1;
            for (i = 0; i < Length + 1; i++)
            {
                if (i == Length)
                {
                    RdCrc = TwiGetData(0); // Get Data
                }
                else
                {
                    TempBuf[4 + i] = TwiGetData(1); // Get Data
                }
            }

            if (RdCrc != CRC8cal(TempBuf, 4 + Length))
            {
                result = 0;
            }
            else
            {
                for (i = 0; i < Length; i++)
                {
                    *RdBuf = TempBuf[4 + i];
                    RdBuf++;
// 涓嬮潰鐨勯棶棰樺湪浜庯紝濡傛灉浼犺繘鏉ョ殑鏁板�间笉鏄�16浣嶏紝鏄�8浣嶏紝鍙堟湁闂銆�
// 杩樻槸澶栭儴鑷繁鍐欎竴涓ぇ灏忕杞崲鍑芥暟鑷繁鐪嬫儏鍐垫槸鍚﹀鐞�
#if 0
                    //闂鍦ㄤ簬030鐨勫皬绔瓨鍌�
					if(i == Length - 1 && Length%2) {
						*(RdBuf) = TempBuf[4+i];		//褰撳彇濂囨暟涓暟鎹紝鏈�鍚庝竴涓紝娌″舰鎴愬鐨勯偅涓闆堕浂鐨勬暟鎹�
					}
					else {
	                    if(i%2) {
							*(--RdBuf) = TempBuf[4+i];
							RdBuf += 2;
	                    }
						else {
							*(++RdBuf) = TempBuf[4+i];
						}
					}
#endif
                }
            }
        }

    RdErr:
        TwiStop();
    }

    return result;
}
#endif

u8 MTPRead(u8 RdAddr, u8 Length, u8 *RdBuf)
{
    TwiRead(AFE_ID, RdAddr, Length, RdBuf);
    return 1;
}
#if 0
u8 MTPRead(u8 RdAddr, u8 Length, u8 *RdBuf)
{
    u8 result = 1;

    Feed_IWatchDog;

    /*
    if(System_ErrFlag.u8ErrFlag_Com_AFE1) {
        result = 0;
    }
    else {
        result = TwiRead(AFE_ID, RdAddr, Length, RdBuf);
        if(!result) {
            result = TwiRead(AFE_ID, RdAddr, Length, RdBuf);
        }
    }
    */

    result = TwiRead(AFE_ID, RdAddr, Length, RdBuf);
    if (!result)
    {
        result = TwiRead(AFE_ID, RdAddr, Length, RdBuf);
    }

    if (!result)
    {
        System_ERROR_UserCallback(ERROR_AFE1);
    }
    return result;
}
#endif

u8 MTPWriteROM(u8 WrAddr, u8 Length, u8 *WrBuf)
{
    u8 result;
    u8 i;

    TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
    // for (i = 0; i < Length; i++)
    // {
    //     Feed_IWatchDog;
    //     // result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
    //     TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
    //     WrAddr++;
    //     WrBuf++;
    //     Delay1ms(40);
    // }
    // todo 怎么确认硬件i2c成功
}
#if 0
u8 MTPWriteROM(u8 WrAddr, u8 Length, u8 *WrBuf)
{
    u8 result;
    u8 i;

    for (i = 0; i < Length; i++)
    {
        Feed_IWatchDog;
        result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
        if (!result)
        {
            Delay1ms(40);
            result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
            if (!result)
            {
                break;
            }
        }
        WrAddr++;
        WrBuf++;
        Delay1ms(40);
    }

    if (!result)
    {
        System_ERROR_UserCallback(ERROR_AFE1);
    }

    return result;
}
#endif

int g_u32CS_Res_AFE;
void Refresh_Parameters(void)
{
    int i = 0;
    int temp = 0;
    u8 TR = 0;
    u16 AFE_TEMPERATURE[8] = {0}; // 娓╁害锛屾憚姘忓害+40锛岋紙0搴︾殑鍊间负40锛�

    // 璇�309鐨凾R锛岄『渚挎妸AFE榛樿鍊奸厤缃紶鍒癆FE_ROM_PARAMETERS_Struction缁撴瀯浣�(#define绫诲瀷)銆�
    if (MTPRead(0x19, 1, &TR))
    {
        SH367309_Reg_Store.TR_ResRef = 680 + 5 * (TR & 0x7F);
        ucMTPBuffer[25] = TR & 0x7F;

/* 鎶婇粯璁ょ殑鏁版嵁鏀惧湪鍙傛暟缁撴瀯浣撻噷 */
#ifdef _SLEEP_WITH_CURRENT
        // 浼戠湢甯︾數鏆備笖涓嶉渶瑕侀鍏呭姛鑳�
        // 瀹忓畾涔夊皯涓嫭鍙凤紝鍑轰簨浜嗭紝璁＄畻浼樺厛绾ч棶棰�
        ucMTPBuffer[1] = (BYTE_01H_SCONF2) & 0xF3;
#endif
        memcpy((u8 *)&AFE_ROM_PARAMETERS_Struction, ucMTPBuffer, 26);
    }
    g_u32CS_Res_AFE = 2 * 1000 / 2;
    // g_u32CS_Res_AFE = ((u32)g_tParam.other.u16Sys_CS_Res_Num * 1000) / g_tParam.other.u16Sys_CS_Res;

    AFE_ROM_PARAMETERS_Struction.m00H_01H.CN = SeriesNum % 16;
    // AFE_ROM_PARAMETERS_Struction.m00H_01H.OCRA = 1;

#define __CTLC__
#ifdef __CTLC__
    AFE_ROM_PARAMETERS_Struction.m00H_01H.CTLC = (0xff >> 6);
#else
    AFE_ROM_PARAMETERS_Struction.m00H_01H.CTLC = (0x00 >> 6);
#endif
    // AFE_ROM_PARAMETERS_Struction.m00H_01H = AFE_ROM_PARAMETERS_Struction.m00H_01H | 0x0c;
    // todo ctlc閰嶇疆 OCD2閰嶇疆鍐欐

    /* 鍏呯數杩囧帇 */
    AFE_ROM_PARAMETERS_Struction.m02H_03H.OVH = ((AFE_Parameters_RS485_Struction.u16VcellOvp.curValue / 5) >> 8) & 0x3;
    AFE_ROM_PARAMETERS_Struction.m02H_03H.OVL = (AFE_Parameters_RS485_Struction.u16VcellOvp.curValue / 5) & 0x00FF;
    /* 鍏呯數杩囧帇寤舵椂鏃堕棿 */
    temp = AFE_Parameters_RS485_Struction.u16VcellOvp_Filter.curValue * 10;
    AFE_ROM_PARAMETERS_Struction.m02H_03H.OVT = Choose_Right_Value(temp, AFE_OVT_UVT);
    /* 鍏呯數杩囧帇鎭㈠ */
    AFE_ROM_PARAMETERS_Struction.m04H_05H.OVRH = ((AFE_Parameters_RS485_Struction.u16VcellOvp_Rcv.curValue / 5) >> 8) & 0x3;
    AFE_ROM_PARAMETERS_Struction.m04H_05H.OVRL = (AFE_Parameters_RS485_Struction.u16VcellOvp_Rcv.curValue / 5) & 0x00FF;

    /*鏀剧數浣庡帇寤舵椂鏃堕棿 */
    temp = AFE_Parameters_RS485_Struction.u16VcellUvp_Filter.curValue * 10;
    AFE_ROM_PARAMETERS_Struction.m04H_05H.UVT = Choose_Right_Value(temp, AFE_OVT_UVT);
    /* 鏀剧數浣庡帇 */
    AFE_ROM_PARAMETERS_Struction.m06H_07H.UV = (AFE_Parameters_RS485_Struction.u16VcellUvp.curValue / 20) & 0x00FF;
    /* 鏀剧數浣庡帇鎭㈠ */
    AFE_ROM_PARAMETERS_Struction.m06H_07H.UVR = (AFE_Parameters_RS485_Struction.u16VcellUvp_Rcv.curValue / 20) & 0x00FF;

    // temp = AFE_Parameters_RS485_Struction.u16IdsgOcp_Second.curValue * 100 / g_u32CS_Res_AFE; // 褰撳墠瀵瑰簲澶氬皯mv
    // AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD1V = Choose_Right_Value(temp, AFE_OCD1V_OCCV);
    // temp = AFE_Parameters_RS485_Struction.u16IdsgOcp_Filter_Second.curValue * 10; // 褰撳墠瀵瑰簲澶氬皯ms
    // AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD1T = Choose_Right_Value(temp, AFE_OCD1T);
    AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD1V = 0;
    AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD1T = 0;

    /* 鍏呯數杩囨祦 */
    temp = AFE_Parameters_RS485_Struction.u16IchgOcp_Second.curValue * 100 / g_u32CS_Res_AFE; // 褰撳墠瀵瑰簲澶氬皯mv
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.OCCV = Choose_Right_Value(temp, AFE_OCD1V_OCCV);
    /* 鍏呯數杩囨祦婊ゆ尝鏃堕棿 */
    temp = AFE_Parameters_RS485_Struction.u16IchgOcp_Filter_Second.curValue * 10; // 褰撳墠瀵瑰簲澶氬皯ms
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.OCCT = Choose_Right_Value(temp, AFE_OCCT_OCD2T);

    /* 鐭矾寤舵椂 */
    temp = AFE_Parameters_RS485_Struction.u16CBC_DelayT.curValue;
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.SCT = Choose_Right_Value(temp, AFE_SCT);
    /* 鐭矾鐢靛帇 */
    temp = AFE_Parameters_RS485_Struction.u16CBC_Cur_DSG.curValue * 1000 / g_u32CS_Res_AFE; // 褰撳墠瀵瑰簲澶氬皯mv
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.SCV = Choose_Right_Value(temp, AFE_SCV);

    /* 鎵�鏈夌殑娓╁害淇濇姢 */
    AFE_TEMPERATURE[0] = AFE_Parameters_RS485_Struction.u16TChgOTp.curValue / 10;        /* 鍏呯數楂樻俯淇濇姢 */
    AFE_TEMPERATURE[1] = AFE_Parameters_RS485_Struction.u16TChgOTp_Rcv.curValue / 10;    /* 鍏呯數楂樻俯淇濇姢鎭㈠ */
    AFE_TEMPERATURE[2] = AFE_Parameters_RS485_Struction.u16TchgUTp.curValue / 10;        /* 鍏呯數浣庢俯淇濇姢 */
    AFE_TEMPERATURE[3] = AFE_Parameters_RS485_Struction.u16TchgUTp_Rcv.curValue / 10;    /* 鍏呯數浣庢俯淇濇姢鎭㈠ */
    AFE_TEMPERATURE[4] = AFE_Parameters_RS485_Struction.u16TdischgOTp.curValue / 10;     /* 鏀剧數楂樻俯淇濇姢 */
    AFE_TEMPERATURE[5] = AFE_Parameters_RS485_Struction.u16TdischgOTp_Rcv.curValue / 10; /* 鏀剧數楂樻俯淇濇姢鎭㈠ */
    AFE_TEMPERATURE[6] = AFE_Parameters_RS485_Struction.u16TdischgUTp.curValue / 10;     /* 鏀剧數浣庢俯淇濇姢 */
    AFE_TEMPERATURE[7] = AFE_Parameters_RS485_Struction.u16TdischgUTp_Rcv.curValue / 10; /* 鏀剧數浣庢俯淇濇姢鎭㈠ */

    for (i = 0; i < 8; i++)
    {
        temp = iSheldTemp_10K_NTC[AFE_TEMPERATURE[i]];
        *(((u8 *)&AFE_ROM_PARAMETERS_Struction.m11H_19H) + i) = (u8)(((u32)temp << 9) / ((u32)SH367309_Reg_Store.TR_ResRef + temp));
    }
}

/* 姣忔鏁版嵁鏀瑰彉閮借鍙杛om鍙傛暟姣旇緝涓�涓嬶紝閭ｄ釜鍙傛暟鏀瑰彉灏卞啓鍏ラ偅=鍝釜 */
void Write_Parameters(void)
{
    int i = 0;
    u8 temp[26] = {0};
    u8 *P = (u8 *)&AFE_ROM_PARAMETERS_Struction;

    if (MTPRead(0x00, 25, temp))
    {
        for (i = 0; i < 25; i++)
        { // 鏈�鍚庝竴涓猅R涓嶅仛瀵规瘮
            if (temp[i] != P[i])
            {
                MTPWriteROM(i, 1, P + i); // 閲嶅啓EEPROM鐨勫瘎瀛樺櫒锛屼袱娆�
                Delay1ms(40);
            }
        }
    }
}

void AFE_Reset(void)
{
    u8 WrBuf[2];

    WrBuf[0] = 0xC0;
    WrBuf[1] = 0xA5;

    /*
    if(!System_ErrFlag.u8ErrFlag_Com_AFE1) {
        if(!MTPWrite(AFE_ID, 0xEA, 1, WrBuf)) {              //0xEA, 0xC0?A CRC
            MTPWrite(AFE_ID, 0xEA, 1, WrBuf);
        }
        //MTPWrite(0xEA, 1, WrBuf);
    }
    */

    // if (!System_ERROR_UserCallback(ERROR_STATUS_AFE1))
    {
        // if (!MTPWrite(0xEA, 1, WrBuf))
        // { // 0xEA, 0xC0?A CRC
        //     MTPWrite(0xEA, 1, WrBuf);
        // }
        MTPWrite(0xEA, 1, WrBuf);
    }
}

u8 AFE_IsReady(void)
{
    u8 TempCnt = 0, result = 0;
    u8 TempVar;

    while (1)
    {
        Feed_IWatchDog;

        TempVar = 0;
        if (MTPRead(MTP_BFLAG2, 1, &TempVar))
        { // 璇诲彇BLFG2锛屾煡鐪媀ADC鏄惁杞崲瀹屾垚
            if ((TempVar & 0x10) == 0x10)
            {
                break;
            }
        }

        Delay1ms(20);
        if (++TempCnt >= 50)
        {
            // System_ERROR_UserCallback(ERROR_AFE1);
            g_stCellInfoReport.unMdlFault_Third.bits.b1SocLow = 1;
            result = 1;
            break;
        }
    }
    return result;
}
void SH367309_Enable_AFE_Wdt_Cadc_Drivers(void)
{
    SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 寮�鍚疌ADC
    SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 0; // 鍏呯數MOS鐢盇FE纭欢鎺у埗
    SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 1; // 鏀剧數MOS鐢盇FE纭欢鎺у埗
    MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
}

void SH367309_UpdataAfeConfig(void)
{
    u8 isdiff = 0;

    if (AFE_PARAM_WRITE_Flag)
    {
        AFE_PARAM_WRITE_Flag = 0;
        // load_protectParam();
        Refresh_Parameters();
        {
            int i = 0;
            u8 temp[26] = {0};
            u8 *P = (u8 *)&AFE_ROM_PARAMETERS_Struction;
            printf("[!!!]flash afe param111");
            array_printf((unsigned char *)&AFE_ROM_PARAMETERS_Struction, sizeof(AFE_ROM_PARAMETERS_Struction));
            // array_printf((unsigned char*)&AFE_ROM_PARAMETERS_Struction, sizeof(AFE_ROM_PARAMETERS_Struction));

            if (MTPRead(0x00, 25, temp))
            {
                array_printf(temp, sizeof(temp));
                for (i = 0; i < 25; i++)
                { // 鏈�鍚庝竴涓猅R涓嶅仛瀵规瘮
                    if (temp[i] != P[i])
                    {
                        isdiff = 1;
                        break;
                    }
                }
            }
        }

        if (isdiff)
        {
            printf("[!!!]flash afe param222");
            // MCUO_AFE_VPRO = 1; // 杩涘叆鐑у啓妯″紡
            gpio_write(GPIO_PD7, 1);
            Delay1ms(20);
            Feed_IWatchDog;

            Write_Parameters();

            Feed_IWatchDog;
            // MCUO_AFE_VPRO = 0; // 閫�鍑虹儳鍐欐ā寮�
            gpio_write(GPIO_PD7, 0);
            Delay1ms(1);

            /* 姣忔鍐欏畬濡傛灉涓嶆姤閿欓兘瑕佸浣嶄竴涓嬨�傝繖鏍峰啓杩涘幓鐨勫弬鏁版墠鏈夋晥 */
            if (!System_ERROR_UserCallback(ERROR_STATUS_AFE1))
            {
                AFE_Reset(); // Reset IC
                Delay1ms(5);
                AFE_IsReady();
                AFE_ResetFlag = 1;
            }
            SH367309_Enable_AFE_Wdt_Cadc_Drivers();
        }
        else
        {
            printf("[!!!] no need flash");
        }
    }
}

UINT16 GetEndValue(const UINT16 *ptbl, UINT16 tblsize, UINT16 dat)
{
    UINT16 i, t_linenum;
    UINT32 x1 = 0, y1 = 0, x2 = 1, y2 = 1;
    const UINT16 *p;
    UINT16 t_tmp16a, t_tmp16b;
    INT32 t_tmp32a, t_tmp32b;
    UINT32 k, b;
    INT32 ret;
    p = ptbl;

    t_linenum = tblsize - 1;
    for (i = 0; i < tblsize - 2; i = i + 2)
    {
        t_tmp16a = p[i];
        t_tmp16b = p[i + 2];

        if (((dat >= t_tmp16a) && (dat <= t_tmp16b)) || ((dat <= t_tmp16a) && (dat >= t_tmp16b)))
        {
            x1 = t_tmp16a;
            x2 = t_tmp16b;
            y1 = p[i + 1];
            y2 = p[i + 3];
            break;
        }
    }

    if (i >= t_linenum - 1)
    {
        p = ptbl;
        t_tmp16a = p[0];
        t_tmp16b = p[tblsize - 2];

        if (t_tmp16a <= t_tmp16b)
        {
            if (dat >= t_tmp16b)
            {
                t_tmp16a = p[tblsize - 1];
            }
            else
            {
                t_tmp16a = p[1];
            }
        }
        else
        {
            if (dat >= t_tmp16a)
            {
                t_tmp16a = p[1];
            }
            else
            {
                t_tmp16a = p[tblsize - 1];
            }
        }
        return t_tmp16a;
    }
    else
    {
        if (x2 < x1)
        {
            ret = x2;
            x2 = x1;
            x1 = ret;
            ret = y2;
            y2 = y1;
            y1 = ret;
        }

        if (y2 >= y1)
        {
            t_tmp32a = y1 * x2;
            t_tmp32b = y2 * x1;
            ret = dat;
            k = y2 - y1;
            ret = ret * k;
            if (t_tmp32a >= t_tmp32b)
            {
                b = t_tmp32a - t_tmp32b;
                ret = ret + b;
            }
            else
            {
                b = t_tmp32b - t_tmp32a;
                ret = ret - b;
            }
            ret = ret / (x2 - x1);
        }
        else
        {
            t_tmp32a = y1 * x2;
            t_tmp32b = y2 * x1;
            ret = dat;
            k = y1 - y2;
            ret = ret * k;
            b = t_tmp32a - t_tmp32b;
            ret = b - ret;
            ret = ret / (x2 - x1);
        }
        return (ret & 0xffff);
    }
}

UINT16 U16_SwapEndian(UINT16 target)
{
    return (((uint16_t)target & 0xFF00) >> 8) | (((uint16_t)target & 0x00FF) << 8);
}
UINT8 UpdateVoltageFromBqMaximo(void)
{
    UINT8 i, result = 0;
    UINT32 u32temp = 0;

    for (i = 0; i < SeriesNum; i++)
    {
        {
            SH367309_Read_AFE1.u16VCell[i] = ((UINT32)U16_SwapEndian(ram_reg_309.Cell[i]) * 5 >> 5); ////Vcell*5/32
        }
        u32temp = ((UINT32)SH367309_Reg_Store.TR_ResRef * U16_SwapEndian(ram_reg_309.Temp1)) / (32769 - U16_SwapEndian(ram_reg_309.Temp1));
        UPDNLMT16(u32temp, 65535, 0);
        SH367309_Read_AFE1.u16TempBat[0] = GetEndValue(iSheldTemp_10K_AFE, (UINT16)LENGTH_TBLTEMP_AFE_10K, u32temp);
        u32temp = ((UINT32)SH367309_Reg_Store.TR_ResRef * U16_SwapEndian(ram_reg_309.Temp2)) / (32769 - U16_SwapEndian(ram_reg_309.Temp2));
        UPDNLMT16(u32temp, 65535, 0);
        SH367309_Read_AFE1.u16TempBat[1] = GetEndValue(iSheldTemp_10K_AFE, (UINT16)LENGTH_TBLTEMP_AFE_10K, u32temp);
        u32temp = ((UINT32)SH367309_Reg_Store.TR_ResRef * U16_SwapEndian(ram_reg_309.Temp3)) / (32769 - U16_SwapEndian(ram_reg_309.Temp3));
        UPDNLMT16(u32temp, 65535, 0);
        SH367309_Read_AFE1.u16TempBat[2] = GetEndValue(iSheldTemp_10K_AFE, (UINT16)LENGTH_TBLTEMP_AFE_10K, u32temp);
        // 电流要不要加滤波1s除以4，demo是这样的，现在先观察一下
        // SH367309_Read_AFE1.i16Current = (UINT16)((UINT32)U16_SwapEndian(Registers_AFE1.Cadc)*200/(21470*RSENSE));		//TODO
        SH367309_Read_AFE1.u16Current = U16_SwapEndian(ram_reg_309.Cadc);
    }
}

void DataLoad_CellVolt(void)
{
    UINT8 i;
    INT32 t_i32temp;

    for (i = 0; i < SeriesNum; ++i)
    {
        t_i32temp = (UINT32)SH367309_Read_AFE1.u16VCell[SeriesSelect_AFE1[SeriesNum - 1][i]];
        // if (g_tParam.CalibCoefK[VOLT_AFE1] != 1024 || g_tParam.CalibCoefB[VOLT_AFE1] != 0)
        // {
        // 	t_i32temp = ((t_i32temp * g_tParam.CalibCoefK[VOLT_AFE1]) >> 10) + g_tParam.CalibCoefB[VOLT_AFE1];
        // }
        t_i32temp = ((t_i32temp * SYSKDEFAULT) >> 10) + SYSBDEFAULT;
        t_i32temp = t_i32temp > 0 ? t_i32temp : 0;
        g_stCellInfoReport.u16VCell[i] = (UINT16)t_i32temp;
    }

    if (SeriesNum < 32)
    {
        for (i = SeriesNum; i < 32; ++i)
        {
            g_stCellInfoReport.u16VCell[i] = 61001;
        }
    }
}

void DataLoad_CellVoltMaxMinFind(void)
{
    UINT8 i;
    UINT16 t_u16VcellTemp;
    UINT16 t_u16VcellMaxTemp;
    UINT16 t_u16VcellMinTemp;
    UINT8 t_u8VcellMaxPosition;
    UINT8 t_u8VcellMinPosition;
    UINT32 u32VCellTotle;

    t_u16VcellMaxTemp = 0;
    t_u16VcellMinTemp = 0x7FFF;
    t_u8VcellMaxPosition = 0;
    t_u8VcellMinPosition = 0;
    u32VCellTotle = 0;

    for (i = 0; i < SeriesNum; i++)
    {
        t_u16VcellTemp = g_stCellInfoReport.u16VCell[i];
        u32VCellTotle += g_stCellInfoReport.u16VCell[i];
        if (t_u16VcellMaxTemp < t_u16VcellTemp)
        {
            t_u16VcellMaxTemp = t_u16VcellTemp;
            t_u8VcellMaxPosition = i;
        }
        if (t_u16VcellMinTemp > t_u16VcellTemp)
        {
            t_u16VcellMinTemp = t_u16VcellTemp;
            t_u8VcellMinPosition = i;
        }
    }

    // 单片机读总压
    // u32VCellTotle = ((g_i32ADCResult[ADC_VBC]*g_tParam.CalibCoefK[VOLT_VBUS])>>10) + (UINT32)g_tParam.CalibCoefB[VOLT_VBUS]*1000;
    // AFE读总压
    // u32VCellTotle = ((g_stBq769x0_Read_AFE1.u32VBat*g_tParam.CalibCoefK[VOLT_VBUS])>>10) + (UINT32)g_tParam.CalibCoefB[VOLT_VBUS]*1000;
    // 所有单节电池电压加起来
    u32VCellTotle = ((u32VCellTotle * SYSKDEFAULT) >> 10) + (UINT32)SYSBDEFAULT * 1000;

    g_stCellInfoReport.u16VCellTotle = (UINT16)((u32VCellTotle * 1638 >> 14) & 0xFFFF); // 除以10
    g_stCellInfoReport.u16VCellMax = t_u16VcellMaxTemp;                                 // max cell voltage
    g_stCellInfoReport.u16VCellMin = t_u16VcellMinTemp;                                 // min cell voltage
    g_stCellInfoReport.u16VCellDelta = t_u16VcellMaxTemp - t_u16VcellMinTemp;           // delta cell voltage
    g_stCellInfoReport.u16VCellMaxPosition = t_u8VcellMaxPosition + 1;                  // max cell voltage
    g_stCellInfoReport.u16VCellMinPosition = t_u8VcellMinPosition + 1;                  // min cell voltage
}

/*这个是数据溢出的问题，其次是>>这个的优先级和别的符号优先级的问题
  运算符优先级太混乱导致数据溢出的问题
   (UINT16)(t_i32temp/100) 和
    (UINT16)(t_i32temp)/100不一样
*/
void DataLoad_Temperature(void)
{
    UINT8 i;
    INT32 t_i32temp;
    UINT8 Select;

    Select = 2;

    for (i = 0; i < Select; i++)
    {
        t_i32temp = (INT32)SH367309_Read_AFE1.u16TempBat[i] / 10 - 40;
        t_i32temp = ((t_i32temp * SYSKDEFAULT) + SYSBDEFAULT) >> 10;
        g_stCellInfoReport.u16Temperature[i] = (UINT16)(t_i32temp * 10 + 400);
        Monitor_TempBreak(&g_stCellInfoReport.u16Temperature[i]);
    }

#if 0
	// 环境温度1
	t_i32temp = g_i32ADCResult[ADC_TEMP_EV1] / 10 - 40; // 放大1000倍和B值对应的意思
	t_i32temp = ((t_i32temp * SYSKDEFAULT) + SYSBDEFAULT) >> 10;
	g_stCellInfoReport.u16Temperature[ENV_TEMP1] = (UINT16)(t_i32temp * 10 + 400);
	Monitor_TempBreak(&g_stCellInfoReport.u16Temperature[ENV_TEMP1]);

	// 环境温度2
	t_i32temp = g_i32ADCResult[ADC_TEMP_EV2] / 10 - 40; // 放大1000倍和B值对应的意思
	t_i32temp = -40;
	t_i32temp = ((t_i32temp * SYSKDEFAULT) + SYSBDEFAULT) >> 10;
	g_stCellInfoReport.u16Temperature[ENV_TEMP2] = (UINT16)(t_i32temp * 10 + 400);

	// 环境温度3
	t_i32temp = -40;
	t_i32temp = ((t_i32temp * SYSKDEFAULT) + SYSBDEFAULT) >> 10;
	g_stCellInfoReport.u16Temperature[ENV_TEMP3] = (UINT16)(t_i32temp * 10 + 400);
#endif

    // MOS温度为散热片温度
    // 取两者最大值
    // t_i32temp = g_i32ADCResult[ADC_TEMP_MOS1];
    // t_i32temp = t_i32temp / 10 - 40;
    // t_i32temp = ((t_i32temp * SYSKDEFAULT) + SYSBDEFAULT) >> 10;
    // g_stCellInfoReport.u16Temperature[MOS_TEMP1] = (UINT16)(t_i32temp * 10 + 400);
    // Monitor_TempBreak(&g_stCellInfoReport.u16Temperature[MOS_TEMP1]);
}

void DataLoad_TemperatureMaxMinFind(void)
{
    UINT8 i;
    UINT16 t_u16VcellTemp;
    UINT16 t_u16VcellMaxTemp;
    UINT16 t_u16VcellMinTemp;
    t_u16VcellMaxTemp = 0;
    t_u16VcellMinTemp = 0x7FFF;

    // 如果是两个环境温度，则改为8便可
    for (i = 0; i < 7; i++)
    { // 默认只有一个环境温度，纳入计算
        if (g_stCellInfoReport.u16Temperature[i] == 0)
        {             // 这段代码什么意思，断了就不判断吗？
            continue; // 有的，则必定会被赋值，要么-29摄氏度。
        } // 空的，则就是默认刚上电的值0
        t_u16VcellTemp = g_stCellInfoReport.u16Temperature[i];
        if (t_u16VcellMaxTemp < t_u16VcellTemp)
        {
            t_u16VcellMaxTemp = t_u16VcellTemp;
        }
        if (t_u16VcellMinTemp > t_u16VcellTemp)
        {
            t_u16VcellMinTemp = t_u16VcellTemp;
        }
    }

    g_stCellInfoReport.u16TempMax = t_u16VcellMaxTemp; // max temp
    g_stCellInfoReport.u16TempMin = t_u16VcellMinTemp; // min temp
}

void DataLoad_CurrentCali(void)
{
#if 0
	static UINT8 su8_StartUpFlag = 4;

	// todo 预留上位机校准接口，以防万一
	// if (sci_cali_falg)
	// 	DataLoad_CurrentCali_startup();

	if (OffsetValue_CHG)
	{
		su8_StartUpFlag = 4;
	}
	else
	{
		su8_StartUpFlag = 5;
	}

	switch (su8_StartUpFlag)
	{
	// 充电偏置
	case 4:
		if (u32_ChgCur_mA > OffsetValue_CHG)
		{
			u32_ChgCur_mA = u32_ChgCur_mA - OffsetValue_CHG;
		}
		else
		{
			// u32_ChgCur_mA = 0;	//不能先置0啊，不然错了
			u32_DsgCur_mA = u32_DsgCur_mA + OffsetValue_CHG - u32_ChgCur_mA;
			u32_ChgCur_mA = 0;
		}
		break;
	case 5:

		if (u32_DsgCur_mA > OffsetValue_DSG)
		{
			u32_DsgCur_mA = u32_DsgCur_mA - OffsetValue_DSG;
		}
		else
		{
			// u32_DsgCur_mA = 0;
			u32_ChgCur_mA = u32_ChgCur_mA + OffsetValue_DSG - u32_DsgCur_mA;
			u32_DsgCur_mA = 0;
		}
		break;
	default:
		break;
	}
#endif
}

void test_Autocurrent_cycle(void)
{
    static uint8_t step = 0;
#if 1
    static uint16_t CHG_current = 100;
    static uint16_t DSG_current = 100;
#else
    static uint16_t CHG_current = 200;
    static uint16_t DSG_current = 400;
#endif

    switch (step)
    {
    case 0:
        if (g_stCellInfoReport.SocElement.u16Soc < 99)
        {
            step = 1;
            g_stCellInfoReport.u16Ichg = CHG_current;
            g_stCellInfoReport.u16IDischg = 0;
        }
        else
        {
            step = 1;
        }
        break;
    case 1:
    {
        if (g_stCellInfoReport.SocElement.u16Soc >= 99)
        {
            step = 2;
            g_stCellInfoReport.u16Ichg = 0;
            g_stCellInfoReport.u16IDischg = DSG_current;
        }
        break;
    }
    case 2:
        if (g_stCellInfoReport.SocElement.u16Soc <= 1)
        {
            step = 0;
        }
        break;
    default:
        break;
    }
}
void DataLoad_Current(void)
{
    // if ((SH367309_Read_AFE1.u16Current & 0x1000) == 0)
    if ((SH367309_Read_AFE1.u16Current & 0x8000) == 0)
    {
        // u32_ChgCur_mA = (UINT32)SH367309_Read_AFE1.u16Current * 1000 * g_u32CS_Res_AFE / gu32_CurCoefficient; // 默认使用200mV的计算方式
        u32_ChgCur_mA = (UINT32)SH367309_Read_AFE1.u16Current * 200 * g_u32CS_Res_AFE / (21470);
        // t_i32temp = (UINT32)(0xFFFF - SH367309_Read_AFE1.u16Current + 1) * g_u32CS_Res_AFE / (21470) * 200; // mA

        log_i("******************************************\n");
        log_i("AFE value->%d\n", u32_ChgCur_mA);

        u32_DsgCur_mA = 0;
    }
    else
    {
        // u32_DsgCur_mA = (UINT32)(0xFFFF - (SH367309_Read_AFE1.u16Current | 0xE000) + 1) * 1000 * g_u32CS_Res_AFE / gu32_CurCoefficient; // mA
        // u32_DsgCur_mA = (UINT32)(0xFFFF - SH367309_Read_AFE1.u16Current + 1) * 200 * g_u32CS_Res_AFE / (21470); // mA
        u32_DsgCur_mA = (UINT32)(0xFFFF - SH367309_Read_AFE1.u16Current + 1) * g_u32CS_Res_AFE / (21470) * 200; // mA

        log_i("******************************************\n");
        log_i("AFE value->%d\n", u32_DsgCur_mA);

        u32_ChgCur_mA = 0;
    }
    // DataLoad_CurrentCali();
    if (u32_DsgCur_mA > 2000)
    {
        u32_DsgCur_mA = ((u32_DsgCur_mA * SYSKDEFAULT)) + (INT32)SYSBDEFAULT * 1000; // B值是基于A为单位计算出来的
    }
    else
    {
        u32_DsgCur_mA = ((u32_DsgCur_mA * 1024));
    }

    if (u32_ChgCur_mA > 2000)
    {
        u32_ChgCur_mA = ((u32_ChgCur_mA * SYSKDEFAULT)) + (INT32)SYSBDEFAULT * 1000;
    }
    else
    {
        u32_ChgCur_mA = ((u32_ChgCur_mA * 1024));
    }

    // 改为INT32
    u32_ChgCur_mA = u32_ChgCur_mA > 0 ? u32_ChgCur_mA : 0;
    u32_DsgCur_mA = u32_DsgCur_mA > 0 ? u32_DsgCur_mA : 0;

    g_stCellInfoReport.u16Ichg = (UINT16)((u32_ChgCur_mA >> 10) / 100);
    g_stCellInfoReport.u16IDischg = (UINT16)((u32_DsgCur_mA >> 10) / 100);

    if (g_stCellInfoReport.u16Ichg <= 2)
    {
        g_stCellInfoReport.u16Ichg = 0;
    }
    if (g_stCellInfoReport.u16IDischg <= 2)
    {
        g_stCellInfoReport.u16IDischg = 0;
    }

#ifdef __VIRTURE_CURRENT__
    if (sys_time.isdebugenable == 1)
    {
        g_stCellInfoReport.u16Ichg = sys_time.CHG;
        g_stCellInfoReport.u16IDischg = sys_time.DSG;
    }
#endif
}

void FaultWarnRecord2(enum FaultFlag num)
{
    if (num >= 1 && num <= 13)
    {
        if (FaultPoint_First2 >= Record_len)
        {
            FaultPoint_First2 = 0;
        }
        Fault_record_First2[FaultPoint_First2++] = num;
    }
    else if (num >= 14 && num <= 26)
    {
        if (FaultPoint_Second2 >= Record_len)
        {
            FaultPoint_Second2 = 0;
        }
        Fault_record_Second2[FaultPoint_Second2++] = num;
    }
    else
    {
        if (FaultPoint_Third2 >= Record_len)
        {
            FaultPoint_Third2 = 0;
        }
        Fault_record_Third2[FaultPoint_Third2++] = num;
    }
}

void Fault_ChangeToMCU(void)
{
    static UINT8 su8_CellOvp_Flag = 0;
    static UINT8 su8_CellUvp_Flag = 0;
    static UINT8 su8_IdischgOcp1_Flag = 0;
    static UINT8 su8_IdischgOcp2_Flag = 0;
    static UINT8 su8_IchgOcp_Flag = 0;
    static UINT8 su8_CellChgUtp_Flag = 0;
    static UINT8 su8_CellChgOtp_Flag = 0;
    static UINT8 su8_CellDsgUtp_Flag = 0;
    static UINT8 su8_CellDsgOtp_Flag = 0;

    g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp = ram_reg_309.REG_BSTATUS1.bits.OV;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp = ram_reg_309.REG_BSTATUS1.bits.UV;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp = ram_reg_309.REG_BSTATUS1.bits.OCD1 || ram_reg_309.REG_BSTATUS1.bits.OCD1;
    g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp = ram_reg_309.REG_BSTATUS1.bits.OCC;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp = ram_reg_309.REG_BSTATUS2.bits.UTC;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp = ram_reg_309.REG_BSTATUS2.bits.OTC;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp = ram_reg_309.REG_BSTATUS2.bits.UTD;
    g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp = ram_reg_309.REG_BSTATUS2.bits.OTD;
    if (ram_reg_309.REG_BSTATUS1.bits.SC)
        System_ErrFlag.u8ErrFlag_CBC_DSG = 1;
    else
        System_ErrFlag.u8ErrFlag_CBC_DSG = 0;

    switch (su8_CellOvp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS1.bits.OV)
        {
            FaultWarnRecord2(CellOvp_Third);
            su8_CellOvp_Flag = 1;
        }
        break;
    case 1:
        if (!ram_reg_309.REG_BSTATUS1.bits.OV)
        {
            su8_CellOvp_Flag = 0;
        }
        break;
    default:
        break;
    }
    switch (su8_CellUvp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS1.bits.UV)
        {
            FaultWarnRecord2(CellUvp_Third);
            su8_CellUvp_Flag = 1;
        }
        break;
    case 1:
        if (!ram_reg_309.REG_BSTATUS1.bits.UV)
        {
            su8_CellUvp_Flag = 0;
        }
        break;
    default:
        break;
    }

#if 1
    switch (su8_IdischgOcp1_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS1.bits.OCD1)
        {
            // FaultWarnRecord2(IdischgOcp_Second);
            FaultWarnRecord2(IdischgOcp_Third);
            su8_IdischgOcp1_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS1.bits.OCD1)
        {
            su8_IdischgOcp1_Flag = 0;
        }
        break;

    default:
        break;
    }
#endif

    // switch (su8_IdischgOcp2_Flag)
    // {
    // case 0:
    // 	if (g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp)
    // 	{
    // 		FaultWarnRecord2(IdischgOcp_Third);
    // 		su8_IdischgOcp2_Flag = 1;
    // 	}
    // 	break;
    // case 1:
    // 	if (!g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp)
    // 	{
    // 		su8_IdischgOcp2_Flag = 0;
    // 	}
    // 	break;
    // default:
    // 	break;
    // }
#if 1
    switch (su8_IchgOcp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS1.bits.OCC)
        {
            FaultWarnRecord2(IchgOcp_Third);
            su8_IchgOcp_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS1.bits.OCC)
        {
            su8_IchgOcp_Flag = 0;
        }
        break;

    default:
        break;
    }

#else
    // switch (su8_IchgOcp_Flag)
    // {
    // case 0:
    // 	if (g_stCellInfoReport.unMdlFault_Second.bits.b1IchgOcp)
    // 	{
    // 		FaultWarnRecord2(IchgOcp_Second);
    // 		su8_IchgOcp_Flag = 1;
    // 	}
    // 	break;

    // case 1:
    // 	if (!g_stCellInfoReport.unMdlFault_Second.bits.b1IchgOcp)
    // 	{
    // 		su8_IchgOcp_Flag = 0;
    // 	}
    // 	break;

    // default:
    // 	break;
    // }
#endif

    switch (su8_CellChgUtp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS2.bits.UTC)
        {
            FaultWarnRecord2(CellChgUTp_Third);
            su8_CellChgUtp_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS2.bits.UTC)
        {
            su8_CellChgUtp_Flag = 0;
        }
        break;

    default:
        break;
    }

    switch (su8_CellChgOtp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS2.bits.OTC)
        {
            FaultWarnRecord2(CellChgOTp_Third);
            su8_CellChgOtp_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS2.bits.OTC)
        {
            su8_CellChgOtp_Flag = 0;
        }
        break;

    default:
        break;
    }

    switch (su8_CellDsgUtp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS2.bits.UTD)
        {
            FaultWarnRecord2(CellDsgUTp_Third);
            su8_CellDsgUtp_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS2.bits.UTD)
        {
            su8_CellDsgUtp_Flag = 0;
        }
        break;

    default:
        break;
    }

    switch (su8_CellDsgOtp_Flag)
    {
    case 0:
        if (ram_reg_309.REG_BSTATUS2.bits.OTD)
        {
            FaultWarnRecord2(CellDsgOTp_Third);
            su8_CellDsgOtp_Flag = 1;
        }
        break;

    case 1:
        if (!ram_reg_309.REG_BSTATUS2.bits.OTD)
        {
            su8_CellDsgOtp_Flag = 0;
        }
        break;

    default:
        break;
    }
}

void App_AFEGet(void)
{
#define SLAVE_DMA_MODE_OTHER_DEV_WRITE (0x46)
#define SLAVE_DMA_MODE_OTHER_DEV_READ (0x40)
    u8 addr = SLAVE_DMA_MODE_OTHER_DEV_READ;
    u8 len = (0x71 - 0x40 + 1); // 鎵嬪唽璇达細闀垮害涓嶅寘鍚獵RC
    // i2c_master_tx_buff[0] += 1;
    // 825x slave dma mode, sram address(0x40000~0x4FFFF) length should be 3 byte
    // i2c_write_series(SLAVE_DMA_MODE_OTHER_DEV_WRITE, 1, (unsigned char *)i2c_master_tx_buff, DBG_DATA_LEN);
    // WaitMs(100);   //1 S
    // i2c_read_series(((u16)addr << 8) | len, 2, (unsigned char *)i2c_master_rx_buff, len + 1);
    // i2c_read_series(((u16)addr << 8) | len, 2, (unsigned char *)i2c_master_rx_buff, len);
    i2c_read_series(((u16)addr << 8) | len, 2, (unsigned char *)&ram_reg_309, len);

    UpdateVoltageFromBqMaximo();

    DataLoad_CellVolt();
    // DataLoad_CellVolt_Test();
    DataLoad_CellVoltMaxMinFind();
    DataLoad_Temperature();
    DataLoad_TemperatureMaxMinFind();
    DataLoad_Current();
    // test_Autocurrent_cycle();

    SystemStatus.bits.b1Status_MOS_CHG = ram_reg_309.REG_BSTATUS3.bits.CHG_FET;
    SystemStatus.bits.b1Status_MOS_DSG = ram_reg_309.REG_BSTATUS3.bits.DSG_FET;
    Fault_ChangeToMCU();
}
void AFE_Sleep(void)
{
    SH367309_Reg_Store.REG_MTP_CONF.bits.SLEEP = 1;
    MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
}
