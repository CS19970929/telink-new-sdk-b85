#include <stdint.h>

#include "tl_common.h"
#include "drivers.h"
#include "sif_send.h"
#include "bms_state.h"
#include "bus_mux.h"

#define SIF_TIMER_INTERVAL_US 500u

void sif_timer_init(void)
{
    reg_irq_mask |= FLD_IRQ_TMR0_EN;
    reg_tmr0_tick = 0;
    reg_tmr0_capt = SIF_TIMER_INTERVAL_US * CLOCK_SYS_CLOCK_1US;
    reg_tmr_sta = FLD_TMR_STA_TMR0;
    reg_tmr_ctrl |= FLD_TMR0_EN;
    irq_enable();
}

_attribute_ram_code_ void sif_timer_irq_handler(void)
{
    if (reg_tmr_sta & FLD_TMR_STA_TMR0)
    {
        sif_send_data_handle();
        reg_tmr_sta = FLD_TMR_STA_TMR0;
    }
}

#ifdef _FUNC_SIF_

static void sif_send_private_cell_voltages(void);
static void sif_send_private_realtime_info(void);
static void sif_send_public_packet(void);

static inline void sif_turn_off(void)
{
    gpio_write(OWC_TX_PIN, 0);
}

static inline void sif_turn_on(void)
{
    gpio_write(OWC_TX_PIN, 1);
}

#define __TODO__ 0Xaa

#define SIF_VERSION 1
#define SIF_SYNC (2 * (10 + 1))
#define SIF_STOP (2 * 5)
#define SIF_SEND_COUNT 4

volatile static uint8_t sif_sync_tosc = 0;
volatile static uint8_t sif_send_tosc = 0;
volatile static SIF_STATE_E state_mode = SEND_DATA_COMPLETE;
volatile static int8_t bit_cnt = 7;
volatile static uint8_t byte_cnt = 0;

static uint8_t sif_sendArray[64] = {0};
volatile static uint8_t sif_send_length = 0;


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
    uint8_t ver;
    uint8_t len;
    uint8_t soc;
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

#pragma pack(1)
typedef struct
{
    uint8_t id;
    uint8_t ver;
    uint8_t len;
    uint16_t arrVoltage[SeriesNum];

    uint8_t verify;

} PRIVATE_PACKETS_CELLVOLTAGE_H;
#pragma pack()

typedef struct
{
    PRIVATE_PACKETS_REALTIME_INFO_H realTimeInfo;
    PRIVATE_PACKETS_CELLVOLTAGE_H vcell;

} PRIVATE_PACKETS_H;

typedef struct
{
    PUBLIC_PACKETS_H public;
    PRIVATE_PACKETS_H private;

} SIF_REPORT_H;

static SIF_REPORT_H sif_report;

static uint8_t sum_verify(const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint16_t res = 0;

    for (i = 0; i < length; i++)
    {
        res += data[i];
    }

    return (uint8_t)(res & 0xff);
}

static uint8_t sif_fault_code(void)
{
    const bms_fault_bits_t *fault = &g_stCellInfoReport.unMdlFault_Third.bits;
    uint8_t code = 0u;

    /* Preserve the established SIF rule: a later matching fault wins. */
    if (fault->b1IdischgOcp) code = 0x01u;
    if (g_stCellInfoReport.unMdlFault_Second.bits.b1IdischgOcp) code = 0x02u;
    if (fault->b1CellChgUtp) code = 0x03u;
    if (fault->b1CellChgOtp) code = 0x04u;
    if (fault->b1CellDischgOtp) code = 0x05u;
    if (fault->b1CellUvp) code = 0x06u;
    if (fault->b1CellOvp) code = 0x07u;
    if (fault->b1IchgOcp) code = 0x08u;
    if (fault->b1CellDischgUtp) code = 0x09u;
    return code;
}

void sif_send_data_handle(void)
{
    static bool iswakeup = true;
    static uint8_t pubblic_frame3_cnt = 0;
    static uint16_t cnt_60s = 0;
    static uint16_t cnt = 0;
    uint8_t count = SIF_SEND_COUNT;
    const uint8_t *p = (const uint8_t *)sif_sendArray;

    if (BUS_STATE_OWC_TX != bus_mux_get_state())
    {
        cnt = 0;
        state_mode = SIF_IDLE;
        iswakeup = true;
        cnt_60s = 0;
        return;
    }

    switch (state_mode)
    {
    case SIF_IDLE:

        sif_turn_on();

        if (!gpio_read(OWC_RX_PIN))
        {
            bus_mux_return_to_owc_idle();
            cnt = 0;
            state_mode = SIF_IDLE;
            iswakeup = true;
            cnt_60s = 0;
            return;
        }

        if (++cnt >= 2 * 1000)
        {
            cnt = 0;
            state_mode = SYNC_SIGNAL;
        }
        // sif_turn_off();
        break;
    case SYNC_SIGNAL:

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
            bit_cnt = 0;
            byte_cnt = 0;
            sif_send_tosc = 0;

            state_mode = SEND_DATA;

            if (iswakeup)
            {
                sif_send_public_packet();
                ++pubblic_frame3_cnt;
                if (pubblic_frame3_cnt >= 3)
                {
                    pubblic_frame3_cnt = 0;
                    iswakeup = false;
                }
            }
            else
            {

                if (++cnt_60s <= 15)
                {
                    sif_send_private_realtime_info();
                }
                else
                {
                    cnt_60s = 0;
                    sif_send_private_cell_voltages();
                }
            }
        }
        break;
    case SEND_DATA:

        sif_send_tosc = sif_send_tosc % count;

        uint8_t data = (p[byte_cnt] >> bit_cnt) & 0x1;

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
            if (++bit_cnt > 7)
            {
                byte_cnt++;
                bit_cnt = 0;
            }
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

        break;
    }
    case SEND_DATA_COMPLETE:
        if (++cnt >= 2 * 1500)
        {
            cnt = 0;
            state_mode = SYNC_SIGNAL;
        }
        sif_turn_on();
        break;
    default:
        break;
    }

}

static void sif_send_public_packet(void)
{
#define __public_protocol_ver__ (0x01)
#define __public_batttery_comply__ (0x01)
#define __public_battery_type__ __TODO__
#define __public_BatteryCoreMaterial__ (0x03)
#define __public_Rated_voltage__ (SeriesNum * 36)
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
    temp = g_stCellInfoReport.u16TempMax / 10;
    sif_report.public.max_temp = temp;
    temp = g_stCellInfoReport.u16TempMin / 10;
    sif_report.public.min_temp = temp;
    sif_report.public.mos_temp = g_stCellInfoReport.u16Temperature[MOS_TEMP1];

    sif_report.public.fault = sif_fault_code();

    if (g_stCellInfoReport.u16IDischg)
        sif_report.public.work_status = 0x0;
    else if (g_stCellInfoReport.u16Ichg)
        sif_report.public.work_status = 0x1;
    else
        sif_report.public.work_status = 0x2;

    sif_report.public.verify = sum_verify((const uint8_t *)&sif_report.public, sizeof(sif_report.public) - 1);

    sif_send_length = sizeof(sif_report.public);

    memcpy(&sif_sendArray, &sif_report, sif_send_length);

}

static void sif_send_private_realtime_info(void)
{
#define __private_protocol_ver__ (0x01)
#define __private_protocol_random_key__ __TODO__
    sif_report.private.realTimeInfo.id = 0x3A;
    sif_report.private.realTimeInfo.ver = __private_protocol_ver__;
    sif_report.private.realTimeInfo.len = sizeof(sif_report.private.realTimeInfo) - 3 - 1;
    sif_report.private.realTimeInfo.soc = g_stCellInfoReport.SocElement.u16Soc * 2;
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
    temp = g_stCellInfoReport.u16TempMax / 10;
    sif_report.private.realTimeInfo.max_temp = temp;
    temp = g_stCellInfoReport.u16TempMin / 10;
    sif_report.private.realTimeInfo.min_temp = temp;
    temp = g_stCellInfoReport.u16Temperature[MOS_TEMP1] / 10;
    sif_report.private.realTimeInfo.mos_temp = temp;

    sif_report.private.realTimeInfo.fault = sif_fault_code();

    if (g_stCellInfoReport.u16IDischg)
        sif_report.private.realTimeInfo.work_status = 0x0;
    else if (g_stCellInfoReport.u16Ichg)
        sif_report.private.realTimeInfo.work_status = 0x1;
    else
        sif_report.private.realTimeInfo.work_status = 0x2;

    sif_report.private.realTimeInfo.bms_status = 0u;

    sif_report.private.realTimeInfo.cycle_times = g_stCellInfoReport.SocElement.u16Cycle_times;
    sif_report.private.realTimeInfo.u16VCellMax = g_stCellInfoReport.u16VCellMax;
    sif_report.private.realTimeInfo.u16VCellMin = g_stCellInfoReport.u16VCellMin;
    sif_report.private.realTimeInfo.u16VCellMaxPosition = g_stCellInfoReport.u16VCellMaxPosition;
    sif_report.private.realTimeInfo.u16VCellMinPosition = g_stCellInfoReport.u16VCellMinPosition;
    sif_report.private.realTimeInfo.Max_feedback_current = __TODO__;
    sif_report.private.realTimeInfo.Request_charging_voltage = (43 * SeriesNum);
    sif_report.private.realTimeInfo.Request_charging_current = 20;

    sif_report.private.realTimeInfo.random_key = __TODO__;
    sif_report.private.realTimeInfo.result_key = __TODO__;

    sif_report.private.realTimeInfo.verify = sum_verify((const uint8_t *)&sif_report.private.realTimeInfo, sizeof(sif_report.private.realTimeInfo) - 1);

    sif_send_length = sizeof(sif_report.private.realTimeInfo);

    memcpy(&sif_sendArray, &sif_report.private.realTimeInfo, sif_send_length);
}

static void sif_send_private_cell_voltages(void)
{
    sif_report.private.vcell.id = 0x3B;
    sif_report.private.vcell.ver = 0x01;
    sif_report.private.vcell.len = 2 * SeriesNum;

    for (uint8_t i = 0; i < SeriesNum; i++)
        sif_report.private.vcell.arrVoltage[i] = g_stCellInfoReport.u16VCell[i];

    sif_report.private.vcell.verify = __TODO__;

    sif_send_length = sizeof(sif_report.private.vcell);

    memcpy(&sif_sendArray, &sif_report.private.vcell, sif_send_length);
}

#else
void sif_send_data_handle(void)
{
    /* 当前产品配置未启用 SIF，保留空入口以维持调度接口。 */
}
#endif /* _FUNC_SIF_ */
