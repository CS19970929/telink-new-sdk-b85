// #include "main.h"
#include "conf.h"
#include "sif_send.h"
#include "Sci_Upper.h"
#include "sh367309_datadeal.h"
// #include "System_Monitor.h"

#include "Sci_Upper.h"
#include <stdint.h>

#include "conf.h"
#include "tl_common.h"
#include "drivers.h"

void sif_send_PRIVATE_PACKETS_CELLVOLTAGE(void);
void sif_send_PRIVATE_PACKETS_BATTARY_CODE_H(void);
void sif_send_PRIVATE_PACKETS_REALTIME_INFO(void);
void sif_send_PUBLIC_PACKETS(void);

extern struct stCell_Info g_stCellInfoReport;

#define sif_turn_off() gpio_write(OWC_TX_PIN, 0);
#define sif_turn_on() gpio_write(OWC_TX_PIN, 1);
#if 1

#define __INTEL__LSB__

#define __TODO__ 0Xaa

#define SIF_VERSION 1
#define SIF_SYNC (2 * (10 + 1))
#define SIF_STOP (2 * 5)
#define SIF_SEND_COUNT 4

// #define sif_turn_off() GPIO_WriteBit(GPIOB, GPIO_Pin_2, 0)
// #define sif_turn_on() GPIO_WriteBit(GPIOB, GPIO_Pin_2, 1)

volatile static uint8_t sif_sync_tosc = 0;
volatile static uint8_t sif_send_tosc = 0;
// volatile static SIF_STATE_E state_mode = SIF_IDLE;
volatile static SIF_STATE_E state_mode = SEND_DATA_COMPLETE;
volatile static int8_t bit_cnt = 7;
volatile static uint8_t byte_cnt = 0;

static uint8_t sif_sendArray[64] = {0};      // 閿熸枻鎷疯閿熸枻鎷烽敓閰电鎷烽敓鏂ゆ嫹閿熸枻鎷�
volatile static uint8_t sif_send_length = 0; // 閿熸枻鎷烽敓鎹风殑绛规嫹閿熸枻鎷�

static uint8_t sif_enable = 0;

#pragma pack(1)
typedef struct
{
    uint8_t id;
    uint8_t ver;
    uint8_t bat_com;
    uint8_t bat_type;
    uint8_t bat_mate;
    uint16_t Rated_voltage;
    uint16_t CAPACITYFACTORY;
    uint8_t soc;
    uint16_t voltage;
    uint16_t current;
    uint8_t max_temp;
    uint8_t min_temp;
    uint8_t mos_temp;
    uint8_t fault;
    uint8_t work_status;
    uint8_t verify;
} PUBLIC_PACKETS_H;
#pragma pack()

#pragma pack(1)
typedef struct
{
    uint8_t id;
    // uint8_t soc;
    uint8_t ver;
    uint8_t len;
    uint8_t soc;
    // uint8_t id;
    uint16_t voltage;
    uint16_t current;
    uint8_t max_temp;
    uint8_t min_temp;
    uint8_t mos_temp;
    uint8_t fault;
    uint8_t work_status;
    uint8_t bms_status;
    uint16_t cycle_times;
    uint16_t u16VCellMax;
    uint16_t u16VCellMin;
    uint8_t u16VCellMaxPosition;
    uint8_t u16VCellMinPosition;
    uint8_t Max_feedback_current;
    uint16_t Request_charging_voltage;
    uint8_t Request_charging_current;
    uint8_t status_charging;
    uint8_t random_key;
    uint32_t result_key;
    uint8_t verify;

} PRIVATE_PACKETS_REALTIME_INFO_H;
#pragma pack()

#define snum 5

#pragma pack(1)
typedef struct
{
    uint8_t id;
    uint8_t ver;
    uint8_t len;
    uint16_t arrVoltage[snum];

    uint8_t verify;

} PRIVATE_PACKETS_CELLVOLTAGE_H;
#pragma pack()

#define battery_code_length 10
#pragma pack(1)
typedef struct
{
    uint8_t id;
    uint8_t ver;
    uint8_t len;
    uint8_t arrCode[battery_code_length];

    uint8_t verify;

} PRIVATE_PACKETS_BATTARY_CODE_H;
#pragma pack()

typedef struct
// union PRIVATE_PACKETS_H
{
    // uint8_t arry[100];
    PRIVATE_PACKETS_REALTIME_INFO_H realTimeInfo;
    PRIVATE_PACKETS_CELLVOLTAGE_H vcell;
    PRIVATE_PACKETS_BATTARY_CODE_H code;

} PRIVATE_PACKETS_H;

typedef struct
{
    PUBLIC_PACKETS_H public;
    PRIVATE_PACKETS_H private;

} SIF_REPORT_H;

SIF_REPORT_H sif_report;

uint8_t sum_verify(uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint16_t res = 0;

    for (i = 0; i < length; i++)
    {
        res += data[i];
    }

    return (uint8_t)(res & 0xff);
}

void sif_gpio_init()
// void sif_gpio_init(GPIO_TypeDef *GPIOx, uint16_t _pin_scl, uint16_t _pin_sda)
{
#if 0
    GPIO_InitTypeDef GPIO_InitStructure;

#if 0
	RCC_APB2PeriphClockCmd(RCC_I2C_PORT, ENABLE);	/* 閿熸枻鎷稧PIO鏃堕敓鏂ゆ嫹 */

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_OType_OD;	/* 閿熸枻鎷锋紡閿熸枻鎷烽敓渚ワ吉锟� */
	
	GPIO_InitStructure.GPIO_Pin = PIN_I2C_SCL;
	GPIO_Init(PORT_I2C_SCL, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = PIN_I2C_SDA;
	GPIO_Init(PORT_I2C_SDA, &GPIO_InitStructure);
#endif
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_1;

    GPIO_InitStructure.GPIO_Pin = PIN_I2C_SDA;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    // GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_Init(PORT_I2C_SDA, &GPIO_InitStructure);

    /* 閿熸枻鎷蜂竴閿熸枻鎷峰仠姝㈤敓鑴氱尨鎷�, 閿熸枻鎷蜂綅I2C閿熸枻鎷烽敓鏂ゆ嫹閿熻緝纰夋嫹閿熸枻鎷烽敓鏂ゆ嫹閿熷�熷閿熸枻鎷烽敓鏂ゆ嫹閿熸枻鎷锋ā寮� */
    // i2c_Stop();
    // i2c_Stop1();

    // if (_pin_scl == GPIO_Pin_10)
    //     i2c_Stop1();
    // else if (_pin_scl == GPIO_Pin_8)
    //     i2c_Stop();
#endif
}

void sif_data_init(void)
{
    uint8_t i = 0;

    // length = 1;

#if 0
    for (i = 0; i < sizeof(result); i++)
    {
        result[i] = i;
    }
#else

    sif_sendArray[0] = 0xaa;
    sif_sendArray[1] = 0x00;
    sif_sendArray[2] = 0xff;

    sif_send_length = 3;
#endif
}

void sif_switch(uint8_t open)
{
    if (open)
        sif_enable = 1;
    else
        sif_enable = 0;
}

void sif_send_data_handle(void)
// static void sif_send_data_handle(uint8_t state)
{
    // if (!sif_enable)
    //     return;
    static bool iswakeup = true;
    static uint8_t pubblic_frame3_cnt = 0;
    static uint16_t cnt_60s = 0;
    static uint16_t cnt = 0;
    static uint8_t res;
    uint8_t count = SIF_SEND_COUNT;
    static uint8_t nums = sizeof(uint8_t) * 8;
    static uint8_t is60s = 0;
    uint8_t *p = (uint8_t *)sif_sendArray;

    switch (state_mode)
    {
    case SIF_IDLE:

        sif_turn_on();

        // if (!GPIO_PinInGet(SL_EMLIB_GPIO_INIT_ONEWIRERXWKUP_PORT, SL_EMLIB_GPIO_INIT_ONEWIRERXWKUP_PIN))
        // {
        //     cnt = 0;
        //     state_mode = SIF_IDLE;
        //     iswakeup = true;
        //     cnt_60s = 0;
        //     return;
        // }

        if (++cnt >= 2 * 1000)
        {
            cnt = 0;
            state_mode = SYNC_SIGNAL;
        }
        // sif_turn_off();
        break;
    case SYNC_SIGNAL: // 鍚岄敓鏂ゆ嫹妯″紡

        if (sif_sync_tosc < SIF_SYNC - 1 * 2)
        {
            sif_turn_off();
        }
        else
        {
            sif_turn_on();
        }
        sif_sync_tosc++;
        if (sif_sync_tosc >= SIF_SYNC)
        {
            sif_sync_tosc = 0;
#ifdef __INTEL__LSB__
            bit_cnt = 0;
#else
            bit_cnt = 7;
#endif
            byte_cnt = 0;
            sif_send_tosc = 0;

            // if (iswakeup)
            // {
            //     //todo 閺冭泛绨�甸�涚瑝鐎电櫢绱� 閺佺増宓侀弴瀛樻煀閿涳拷
            //     sif_send_PUBLIC_PACKETS();
            //     state_mode = SEND_PUBLIC;
            // }
            // else
            // {
            //     sif_send_PRIVATE_PACKETS_REALTIME_INFO();
            //     state_mode = SEND_DATA;
            // }
            state_mode = SEND_DATA;

            if (iswakeup)
            {
                // todo 閺冭泛绨�甸�涚瑝鐎电櫢绱� 閺佺増宓侀弴瀛樻煀閿涳拷
                sif_send_PUBLIC_PACKETS();
                ++pubblic_frame3_cnt;
                if (pubblic_frame3_cnt >= 3)
                {
                    pubblic_frame3_cnt = 0;
                    iswakeup = false;
                }
            }
            else
            {

                if (++cnt_60s <= (15))
                // if (++cnt_60s <= (5))
                {
                    sif_send_PRIVATE_PACKETS_REALTIME_INFO();
                }
                else
                {
                    cnt_60s = 0;
                    sif_send_PRIVATE_PACKETS_CELLVOLTAGE();
                }
            }
        }
        break;
    // case SEND_PUBLIC:
    //     break;
    case SEND_DATA: // 閿熸枻鎷烽敓鏂ゆ嫹閿熸枻鎷烽敓鏂ゆ嫹

        // sif_send_PRIVATE_PACKETS_REALTIME_INFO();
        // memset(&sif_report.private.realTimeInfo.id, sizeof(sif_report.private.realTimeInfo));

        sif_send_tosc = sif_send_tosc % count;

#ifdef __INTEL__LSB__
        uint8_t data = (p[byte_cnt] >> bit_cnt) & 0x1;
#else
        uint8_t data = (p[byte_cnt] >> bit_cnt) & 0x1;
#endif

        if (data)
        {
            if (sif_send_tosc == 0)
            {
                sif_turn_off();
                sif_send_tosc++;
            }
            else if (sif_send_tosc == 1)
            {
                sif_turn_on();
                sif_send_tosc++;
            }
            else if (sif_send_tosc == 3)
            {
                sif_send_tosc = 0;
            }
            else
            {
                sif_send_tosc++;
            }
        }
        else
        {
            if (sif_send_tosc == 0)
            {
                sif_turn_off();
                sif_send_tosc++;
            }
            else if (sif_send_tosc == 3)
            // else if (sif_send_tosc == 2)
            {
                sif_turn_on();
                sif_send_tosc = 0;
            }
            else
            {
                sif_send_tosc++;
            }
        }
        if (sif_send_tosc == 0)
        {
#ifdef __INTEL__LSB__
            if (++bit_cnt > 7)
            {
                byte_cnt++;
                bit_cnt = 0;
            }
#else
            if (--bit_cnt < 0)
            {
                byte_cnt++;
                bit_cnt = 7;
            }
#endif
            if (byte_cnt >= sif_send_length)
            {
                state_mode = STOP_SIGNAL;
                break;
            }
        }

        break;
    case STOP_SIGNAL:
    {
        if (sif_sync_tosc++ <= SIF_STOP)
        {
            sif_turn_off();
        }
        else
        {
            state_mode = SIF_IDLE;
            sif_turn_on();

            sif_sync_tosc = 0;
        }

        // sif_sync_tosc++;
        break;
    }
    case SEND_DATA_COMPLETE: // 閿熸枻鎷烽敓鎹峰嚖鎷烽敓鏂ゆ嫹閿熸枻鎷锋閿熸枻鎷烽敓鏂ゆ嫹閿熻鐤氫紮鎷烽敓锟�0

#if 0
        static uint16_t cnt = 0;
        static uint32_t cnt_60s = 0;
        static uint8_t is60s = 0;
        
        // state_mode = 0;

        // if (++cnt_60s >= 2 * 1000 * 60)
        if (++cnt_60s >= 2 * 1000 * 10)
        {
            is60s = 1;
            cnt_60s = 0;
        }

        static uint8_t send_index_private = 0;
        static uint8_t send_status;

        if (is60s)
        {
            // is60s = 0;

            switch (send_index_private)
            {
            case 0:
                sif_send_PRIVATE_PACKETS_CELLVOLTAGE();
                send_index_private = 1;
                break;

            case 1:
                sif_send_PRIVATE_PACKETS_BATTARY_CODE_H();
                send_index_private = 0;
                break;
            default:
                break;
            }
        }
        else
        {
            // sif_send_PRIVATE_PACKETS_REALTIME_INFO();
        }

        // if (++cnt >= 2 * 1000 && !is60s)
        if (++cnt >= 2 * 1000)
        {
            cnt = 0;
            state_mode = SYNC_SIGNAL;
            sif_gpio_init();

            if (!is60s)
            {
                sif_send_PRIVATE_PACKETS_REALTIME_INFO();
                // log_i("sif realtime info");
                // sif_data_init();
            }
            else
            {
                is60s = 0;
                log_w("sif 60s info");
            }
        }
        // sif_turn_off();
        sif_turn_on();
        // length = 0;
        // memset(result, 0, sizeof(result));
#endif
        if (++cnt >= 2 * 1500)
        {
            cnt = 0;
            state_mode = SYNC_SIGNAL;
        }
        // sif_turn_off();
        sif_turn_on();
        break;
    default:
        break;
    }
}

#if 1
#if 1
void sif_send_PUBLIC_PACKETS(void)
{
#if 1

#define __public_protocol_ver__ (0x01)
#define __public_batttery_comply__ (0x01)
#define __public_battery_type__ __TODO__
#define __public_BatteryCoreMaterial__ (0x03)
#define __public_Rated_voltage__ (SeriesNum * 36)
    // #define __public_CapacityFactory__          __TODO__
    // #define __public_         0x01
    // #define __public_         0x01

    sif_report.public.id = 0x01;
    sif_report.public.ver = __public_protocol_ver__;
    sif_report.public.bat_com = __public_batttery_comply__;
    sif_report.public.bat_type = __public_battery_type__;
    sif_report.public.bat_mate = __public_BatteryCoreMaterial__;
    sif_report.public.Rated_voltage = __public_Rated_voltage__;
    sif_report.public.CAPACITYFACTORY = CapacityFactory;
    sif_report.public.soc = g_stCellInfoReport.SocElement.u16Soc * 2;
    sif_report.public.voltage = g_stCellInfoReport.u16VCellTotle / 10;
    uint16_t current = 0;
    if (g_stCellInfoReport.u16IDischg)
    {
        current = (g_stCellInfoReport.u16IDischg / 10 + 500) * 10;
    }
    else
    {
        current = (g_stCellInfoReport.u16Ichg / 10 + 500) * 10;
    }
    sif_report.public.current = current;

    uint16_t temp = 0;
    temp = 40 + g_stCellInfoReport.u16TempMax / 10 - 40;
    sif_report.public.max_temp = temp;
    temp = 40 + g_stCellInfoReport.u16TempMin / 10 - 40;
    sif_report.public.min_temp = temp;
    temp = 40 + g_stCellInfoReport.u16Temperature[MOS_TEMP1] / 10 - 40;
    sif_report.public.mos_temp = g_stCellInfoReport.u16Temperature[MOS_TEMP1];

    {
        // if (!g_stCellInfoReport.unMdlFault_Third.all)
        //     sif_report.public.fault = 0;
        // else if (g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp)
        // {
        //     sif_report.public.fault = 0x01;
        // }
        // else if (g_stCellInfoReport.unMdlFault_Second.bits.b1IdischgOcp)
        // {
        //     sif_report.public.fault = 0x02;
        // }
        // else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp)
        // {
        //     sif_report.public.fault = 0x03;
        // }
        // else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp)
        // {
        //     sif_report.public.fault = 0x04;
        // }
        // else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp)
        // {
        //     sif_report.public.fault = 0x05;
        // }
        // else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp)
        // {
        //     sif_report.public.fault = 0x06;
        // }
        // else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp)
        // {
        //     sif_report.public.fault = 0x07;
        // }
        // else if (g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp)
        // {
        //     sif_report.public.fault = 0x08;
        // }
        // else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp)
        // {
        //     sif_report.public.fault = 0x09;
        // }
        // else if (!SystemStatus.bits.b1Status_MOS_CHG)
        // {
        //     sif_report.public.fault = 0x0a;
        // }
        // else if (!SystemStatus.bits.b1Status_MOS_DSG)
        // {
        //     sif_report.public.fault = 0x0b;
        // }

        uint8_t fault = 0;
        if (ram_reg_309.REG_BSTATUS1.bits.OCD2)
            fault = 0x01;
        if (ram_reg_309.REG_BSTATUS1.bits.OCD1)
            fault = 0x02;
        if (ram_reg_309.REG_BSTATUS2.bits.UTC)
            fault = 0x03;
        if (ram_reg_309.REG_BSTATUS2.bits.OTC)
            fault = 0x04;
        if (ram_reg_309.REG_BSTATUS2.bits.OTD)
            fault = 0x05;
        if (ram_reg_309.REG_BSTATUS1.bits.UV)
            fault = 0x06;
        if (ram_reg_309.REG_BSTATUS1.bits.OV)
            fault = 0x07;
        if (ram_reg_309.REG_BSTATUS1.bits.OCC)
            fault = 0x08;
        if (ram_reg_309.REG_BSTATUS2.bits.UTD)
            fault = 0x09;
        sif_report.public.fault = fault;
    }

    if (g_stCellInfoReport.u16IDischg)
        sif_report.public.work_status = 0x0;
    else if (g_stCellInfoReport.u16Ichg)
        sif_report.public.work_status = 0x1;
    else
        sif_report.public.work_status = 0x2;

    sif_report.public.verify = sum_verify(&sif_report.public, sizeof(sif_report.public) - 1);

    sif_send_length = sizeof(sif_report.public);

    memcpy(&sif_sendArray, &sif_report, sif_send_length);

    // todo
    // sif_send_start();
#endif
}
#endif

#if 0
void sif_send_PUBLIC_PACKETS(void)
{
#if 1

#define __public_protocol_ver__ (0x01)
#define __public_batttery_comply__ (0x01)
#define __public_battery_type__ __TODO__
#define __public_BatteryCoreMaterial__ (0x03)
#define __public_Rated_voltage__ (SNum * 36)
    // #define __public_CapacityFactory__          __TODO__
    // #define __public_         0x01
    // #define __public_         0x01

    sif_report.public.id = 0x01;
    sif_report.public.ver = __public_protocol_ver__;
    sif_report.public.bat_com = __public_batttery_comply__;
    sif_report.public.bat_type = __public_battery_type__;
    sif_report.public.bat_mate = __public_BatteryCoreMaterial__;
    sif_report.public.Rated_voltage = __public_Rated_voltage__;
    sif_report.public.CAPACITYFACTORY = CapacityFactory;
    sif_report.public.soc = g_stCellInfoReport.SocElement.u16Soc * 2;
    sif_report.public.voltage = g_stCellInfoReport.u16VCellTotle / 10;
    uint16_t current = 0;
    // if (g_stCellInfoReport.u16IDischg)
    // {
    //     current = (g_stCellInfoReport.u16IDischg / 10 + 500) * 10;
    // }
    // else
    // {
    //     current = (g_stCellInfoReport.u16Ichg / 10 + 500) * 10;
    // }
    sif_report.public.current = current;

    uint16_t temp = 0;
    temp = 1;
    sif_report.public.max_temp = temp;
    temp = 2;
    sif_report.public.min_temp = temp;
    temp = 3;
    sif_report.public.mos_temp = g_stCellInfoReport.u16Temperature[MOS_TEMP1];

    {
        uint8_t fault = 4;
        sif_report.public.fault = fault;
    }

    sif_report.public.work_status = 5;

    sif_report.public.verify = sum_verify(&sif_report.public, sizeof(sif_report.public) - 1);

    sif_send_length = sizeof(sif_report.public);

    memcpy(&sif_sendArray, &sif_report, sif_send_length);

    // todo
    // sif_send_start();
#endif
}
#endif

void sif_send_PRIVATE_PACKETS_REALTIME_INFO(void)
{
#define __private_protocol_ver__ (0x01)
#define __private_protocol_random_key__ __TODO__
    // #define __private_protocol_             __TODO__

    static uint8_t send_status;

    sif_report.private.realTimeInfo.id = 0x3A;
    sif_report.private.realTimeInfo.ver = __private_protocol_ver__;
    sif_report.private.realTimeInfo.len = sizeof(sif_report.private.realTimeInfo) - 3 - 1;
    sif_report.private.realTimeInfo.soc = g_stCellInfoReport.SocElement.u16Soc * 2;
    // sif_report.private.realTimeInfo.voltage = g_stCellInfoReport.u16VCellTotle / 100 * 10;
    sif_report.private.realTimeInfo.voltage = g_stCellInfoReport.u16VCellTotle / 10;

    uint16_t current = 0;
    if (g_stCellInfoReport.u16IDischg)
    {
        current = (g_stCellInfoReport.u16IDischg / 10 + 500) * 10;
    }
    else
    {
        current = (g_stCellInfoReport.u16Ichg / 10 + 500) * 10;
    }
    sif_report.private.realTimeInfo.current = current;

    uint16_t temp = 0;
#if 0
    if(g_stCellInfoReport.u16TempMax >= 400)
    {
        temp = g_stCellInfoReport.u16TempMax;
        temp = (temp / 10 - 40) + 40;
    }
    else
    {
        temp = g_stCellInfoReport.u16TempMax;
        temp = 40 + temp / 10 - 40;
    }
#endif
    temp = 40 + g_stCellInfoReport.u16TempMax / 10 - 40;
    sif_report.private.realTimeInfo.max_temp = temp;
    temp = 40 + g_stCellInfoReport.u16TempMin / 10 - 40;
    sif_report.private.realTimeInfo.min_temp = temp;
    temp = 40 + g_stCellInfoReport.u16Temperature[MOS_TEMP1] / 10 - 40;
    sif_report.private.realTimeInfo.mos_temp = temp;

    {
#if 0
        if (!g_stCellInfoReport.unMdlFault_Third.all)
            sif_report.public.fault = 0;
        else if (g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp)
        {
            sif_report.public.fault = 0x01;
        }
        else if (g_stCellInfoReport.unMdlFault_Second.bits.b1IdischgOcp)
        {
            sif_report.public.fault = 0x02;
        }
        else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgUtp)
        {
            sif_report.public.fault = 0x03;
        }
        else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp)
        {
            sif_report.public.fault = 0x04;
        }
        else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgOtp)
        {
            sif_report.public.fault = 0x05;
        }
        else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp)
        {
            sif_report.public.fault = 0x06;
        }
        else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp)
        {
            sif_report.public.fault = 0x07;
        }
        else if (g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp)
        {
            sif_report.public.fault = 0x08;
        }
        else if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellDischgUtp)
        {
            sif_report.public.fault = 0x09;
        }
        else if (!SystemStatus.bits.b1Status_MOS_CHG)
        {
            sif_report.public.fault = 0x0a;
        }
        else if (!SystemStatus.bits.b1Status_MOS_DSG)
        {
            sif_report.public.fault = 0x0b;
        }
#endif
        uint8_t fault = 0;
        if (ram_reg_309.REG_BSTATUS1.bits.OCD2)
            fault = 0x01;
        if (ram_reg_309.REG_BSTATUS1.bits.OCD1)
            fault = 0x02;
        if (ram_reg_309.REG_BSTATUS2.bits.UTC)
            fault = 0x03;
        if (ram_reg_309.REG_BSTATUS2.bits.OTC)
            fault = 0x04;
        if (ram_reg_309.REG_BSTATUS2.bits.OTD)
            fault = 0x05;
        if (ram_reg_309.REG_BSTATUS1.bits.UV)
            fault = 0x06;
        if (ram_reg_309.REG_BSTATUS1.bits.OV)
            fault = 0x07;
        if (ram_reg_309.REG_BSTATUS1.bits.OCC)
            fault = 0x08;
        if (ram_reg_309.REG_BSTATUS2.bits.UTD)
            fault = 0x09;
        sif_report.private.realTimeInfo.fault = fault;
    }

    if (g_stCellInfoReport.u16IDischg)
        sif_report.private.realTimeInfo.work_status = 0x0;
    else if (g_stCellInfoReport.u16Ichg)
        sif_report.private.realTimeInfo.work_status = 0x1;
    else
        sif_report.private.realTimeInfo.work_status = 0x2;

    {
        uint8_t bms_status = 0;
        //todo
        // if (!isFault_chg())
        //     bms_status |= 1 << 0;
        // if (0 == GPIO_PinInGet(SL_EMLIB_GPIO_INIT_CHG_5V_WK_PORT, SL_EMLIB_GPIO_INIT_CHG_5V_WK_PIN))
        //     bms_status |= 1 << 2;
        // if (sys_status == s_CHG && !isFault_chg())
        //     bms_status |= 1 << 7;
        // else if (!isFault_dsg())
        //     bms_status |= 1 << 6;

        sif_report.private.realTimeInfo.bms_status = bms_status;
    }

    sif_report.private.realTimeInfo.cycle_times = g_stCellInfoReport.SocElement.u16Cycle_times;
    sif_report.private.realTimeInfo.u16VCellMax = g_stCellInfoReport.u16VCellMax;
    sif_report.private.realTimeInfo.u16VCellMin = g_stCellInfoReport.u16VCellMin;
    sif_report.private.realTimeInfo.u16VCellMaxPosition = g_stCellInfoReport.u16VCellMaxPosition;
    sif_report.private.realTimeInfo.u16VCellMinPosition = g_stCellInfoReport.u16VCellMinPosition;
    sif_report.private.realTimeInfo.Max_feedback_current = __TODO__;
    sif_report.private.realTimeInfo.Request_charging_voltage = (43 * SeriesNum);
    sif_report.private.realTimeInfo.Request_charging_current = 20;

    //todo
#if 0
    if (sys_status == s_CHG)
    {
        // if (g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp || g_stCellInfoReport.unMdlFault_Third.bits.b1BatOvp)
        if (g_stCellInfoReport.SocElement.u16Soc == 100)
            sif_report.private.realTimeInfo.status_charging = 0x01;
        // todo
        //  else if (isillegalCharging)
        //  {
        //      sif_report.private.realTimeInfo.status_charging = 0x02;
        //  }
        else if (isFault_chg())
        {
            sif_report.private.realTimeInfo.status_charging = 0x03;
        }
        else if (!isFault_chg())
        {
            sif_report.private.realTimeInfo.status_charging = 0x04;
        }
    }
#endif

    sif_report.private.realTimeInfo.random_key = __TODO__;
    sif_report.private.realTimeInfo.result_key = __TODO__;

    sif_report.private.realTimeInfo.verify = sum_verify(&sif_report.private.realTimeInfo, sizeof(sif_report.private.realTimeInfo) - 1);

    // memset(&sif_report.private.realTimeInfo.id, 0xff, sizeof(sif_report.private.realTimeInfo));
    // memset(&sif_report.private.realTimeInfo.id, 0x00, sizeof(sif_report.private.realTimeInfo));

    sif_send_length = sizeof(sif_report.private.realTimeInfo);

    {
#if 0
        sif_report.private.realTimeInfo.id = 0x00;
        sif_report.private.realTimeInfo.len = 0xff;
        sif_report.private.realTimeInfo.random_key = 0x00;
        sif_report.private.realTimeInfo.soc = 100;
        // sif_report.private.realTimeInfo.voltage = g_stCellInfoReport.u16VCellTotle / 10;
        sif_report.private.realTimeInfo.voltage = 4000 / 10;
        sif_report.private.realTimeInfo.verify = 0x00;
        sif_report.private.realTimeInfo.result_key = 0xff005500;

#endif
    }

    memcpy(&sif_sendArray, &sif_report.private.realTimeInfo, sif_send_length);
    // todo
    // sif_send_start();
}

void sif_send_PRIVATE_PACKETS_CELLVOLTAGE(void)
{
    // static uint16_t
    sif_report.private.vcell.id = 0x3B;
    sif_report.private.vcell.ver = 0x01;
    sif_report.private.vcell.len = 2 * SeriesNum;

    for (uint8_t i = 0; i < SeriesNum; i++)
        sif_report.private.vcell.arrVoltage[i] = g_stCellInfoReport.u16VCell[i];

    sif_report.private.vcell.verify = __TODO__;

    sif_send_length = sizeof(sif_report.private.vcell);

    memcpy(&sif_sendArray, &sif_report.private.vcell, sif_send_length);

    // todo
    // sif_send_start();
}

void sif_send_PRIVATE_PACKETS_BATTARY_CODE_H(void)
{
    // static uint16_t
    sif_report.private.code.id = 0x3C;
    sif_report.private.code.ver = __TODO__;
    sif_report.private.code.len = battery_code_length;

    for (uint8_t i = 0; i < battery_code_length; i++)
        sif_report.private.code.arrCode[i] = __TODO__;

    sif_report.private.code.verify = __TODO__;

    sif_send_length = sizeof(sif_report.private.code);

    memcpy(&sif_sendArray, &sif_report.private.code, sif_send_length);

    // todo
    // sif_send_start();
}

// void sif_run(void)
// {
//     static uint8_t send_index_private = 0;
//     static uint8_t send_status;

//     if (is60s)
//     {
//         switch (send_index_private)
//         {
//         case 0:
//             sif_send_PRIVATE_PACKETS_CELLVOLTAGE();
//             send_index_private = 1;
//             break;

//         case 1:
//             sif_send_PRIVATE_PACKETS_BATTARY_CODE_H();
//             send_index_private = 0;
//             break;
//         default:
//             break;
//         }
//     }
//     else
//     {
//         sif_send_PRIVATE_PACKETS_REALTIME_INFO();
//     }

//     // todo 500us
//     sif_send_data_handle();
// }

#endif

void sif_sleep_conf(void)
{
    // gpio_conf();
    // nvic_conf();
    // exti_conf();
}
#endif
