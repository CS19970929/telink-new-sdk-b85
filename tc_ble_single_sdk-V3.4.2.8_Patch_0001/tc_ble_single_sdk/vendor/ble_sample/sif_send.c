#include "sif_send.h"

#include "bms_state.h"
#include "bus_mux.h"
#include "conf.h"
#include "drivers.h"
#include "sh367309_datadeal.h"
#include "tl_common.h"
#include <string.h>

#define SIF_TICK_US             500u
#define SIF_TICKS_PER_MS        2u
#define SIF_SYNC_TICKS          (SIF_TICKS_PER_MS * 11u)
#define SIF_STOP_TICKS          (SIF_TICKS_PER_MS * 5u)
#define SIF_BIT_TICKS           4u
#define SIF_IDLE_TICKS          (SIF_TICKS_PER_MS * 1000u)
#define SIF_INITIAL_IDLE_TICKS  (SIF_TICKS_PER_MS * 1500u)
#define SIF_CELL_FRAME_PERIOD   16u
#define SIF_PUBLIC_FRAME_COUNT  3u
#define SIF_PLACEHOLDER         0xAAu

#define sif_turn_off() gpio_write(OWC_TX_PIN, 0)
#define sif_turn_on()  gpio_write(OWC_TX_PIN, 1)

typedef enum
{
    SIF_IDLE = 0,
    SIF_SYNC_SIGNAL,
    SIF_SEND_DATA,
    SIF_SEND_DATA_COMPLETE,
    SIF_STOP_SIGNAL,
} sif_state_t;

#pragma pack(1)
typedef struct
{
    uint8_t id;
    uint8_t ver;
    uint8_t bat_com;
    uint8_t bat_type;
    uint8_t bat_mate;
    uint16_t rated_voltage;
    uint16_t capacity_factory;
    uint8_t soc;
    uint16_t voltage;
    uint16_t current;
    uint8_t max_temp;
    uint8_t min_temp;
    uint8_t mos_temp;
    uint8_t fault;
    uint8_t work_status;
    uint8_t verify;
} sif_public_packet_t;

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
    uint16_t vcell_max;
    uint16_t vcell_min;
    uint8_t vcell_max_position;
    uint8_t vcell_min_position;
    uint8_t max_feedback_current;
    uint16_t request_charging_voltage;
    uint8_t request_charging_current;
    uint8_t status_charging;
    uint8_t random_key;
    uint32_t result_key;
    uint8_t verify;
} sif_realtime_packet_t;

typedef struct
{
    uint8_t id;
    uint8_t ver;
    uint8_t len;
    uint16_t cell_voltage[SeriesNum];
    uint8_t verify;
} sif_cell_packet_t;
#pragma pack()

typedef union
{
    sif_public_packet_t public_packet;
    sif_realtime_packet_t realtime_packet;
    sif_cell_packet_t cell_packet;
} sif_packet_buffer_t;

extern struct stCell_Info g_stCellInfoReport;

static volatile uint8_t s_sync_tick;
static volatile uint8_t s_bit_tick;
static volatile uint8_t s_bit_index;
static volatile uint8_t s_byte_index;
static volatile sif_state_t s_state = SIF_SEND_DATA_COMPLETE;
static uint8_t s_tx_data[64];
static volatile uint8_t s_tx_length;
static sif_packet_buffer_t s_packet;

static uint8_t sif_checksum(const uint8_t *data, uint16_t length)
{
    uint16_t index;
    uint16_t sum = 0u;

    for (index = 0u; index < length; ++index)
    {
        sum = (uint16_t)(sum + data[index]);
    }

    return (uint8_t)sum;
}

static uint16_t sif_encode_current(void)
{
    if (g_stCellInfoReport.u16IDischg != 0u)
    {
        return (uint16_t)((g_stCellInfoReport.u16IDischg / 10u + 500u) * 10u);
    }

    return (uint16_t)((g_stCellInfoReport.u16Ichg / 10u + 500u) * 10u);
}

static uint8_t sif_encode_fault(void)
{
    uint8_t fault = 0u;

    /* Preserve deployed priority: later active conditions override earlier ones. */
    if (ram_reg_309.REG_BSTATUS1.bits.OCD2) fault = 0x01u;
    if (ram_reg_309.REG_BSTATUS1.bits.OCD1) fault = 0x02u;
    if (ram_reg_309.REG_BSTATUS2.bits.UTC)  fault = 0x03u;
    if (ram_reg_309.REG_BSTATUS2.bits.OTC)  fault = 0x04u;
    if (ram_reg_309.REG_BSTATUS2.bits.OTD)  fault = 0x05u;
    if (ram_reg_309.REG_BSTATUS1.bits.UV)   fault = 0x06u;
    if (ram_reg_309.REG_BSTATUS1.bits.OV)   fault = 0x07u;
    if (ram_reg_309.REG_BSTATUS1.bits.OCC)  fault = 0x08u;
    if (ram_reg_309.REG_BSTATUS2.bits.UTD)  fault = 0x09u;

    return fault;
}

static uint8_t sif_encode_work_status(void)
{
    if (g_stCellInfoReport.u16IDischg != 0u) return 0x00u;
    if (g_stCellInfoReport.u16Ichg != 0u) return 0x01u;
    return 0x02u;
}

static void sif_build_public_packet(void)
{
    sif_public_packet_t *packet = &s_packet.public_packet;

    packet->id = 0x01u;
    packet->ver = 0x01u;
    packet->bat_com = 0x01u;
    packet->bat_type = SIF_PLACEHOLDER;
    packet->bat_mate = 0x03u;
    packet->rated_voltage = (uint16_t)(SeriesNum * 36u);
    packet->capacity_factory = CapacityFactory;
    packet->soc = (uint8_t)(g_stCellInfoReport.SocElement.u16Soc * 2u);
    packet->voltage = (uint16_t)(g_stCellInfoReport.u16VCellTotle / 10u);
    packet->current = sif_encode_current();
    packet->max_temp = (uint8_t)(g_stCellInfoReport.u16TempMax / 10u);
    packet->min_temp = (uint8_t)(g_stCellInfoReport.u16TempMin / 10u);
    packet->mos_temp = (uint8_t)g_stCellInfoReport.u16Temperature[MOS_TEMP1];
    packet->fault = sif_encode_fault();
    packet->work_status = sif_encode_work_status();
    packet->verify = sif_checksum((const uint8_t *)packet, (uint16_t)(sizeof(*packet) - 1u));

    s_tx_length = (uint8_t)sizeof(*packet);
    memcpy(s_tx_data, packet, s_tx_length);
}

static void sif_build_realtime_packet(void)
{
    sif_realtime_packet_t *packet = &s_packet.realtime_packet;

    packet->id = 0x3Au;
    packet->ver = 0x01u;
    packet->len = (uint8_t)(sizeof(*packet) - 4u);
    packet->soc = (uint8_t)(g_stCellInfoReport.SocElement.u16Soc * 2u);
    packet->voltage = (uint16_t)(g_stCellInfoReport.u16VCellTotle / 10u);
    packet->current = sif_encode_current();
    packet->max_temp = (uint8_t)(g_stCellInfoReport.u16TempMax / 10u);
    packet->min_temp = (uint8_t)(g_stCellInfoReport.u16TempMin / 10u);
    packet->mos_temp = (uint8_t)(g_stCellInfoReport.u16Temperature[MOS_TEMP1] / 10u);
    packet->fault = sif_encode_fault();
    packet->work_status = sif_encode_work_status();
    packet->bms_status = 0u;
    packet->cycle_times = g_stCellInfoReport.SocElement.u16Cycle_times;
    packet->vcell_max = g_stCellInfoReport.u16VCellMax;
    packet->vcell_min = g_stCellInfoReport.u16VCellMin;
    packet->vcell_max_position = (uint8_t)g_stCellInfoReport.u16VCellMaxPosition;
    packet->vcell_min_position = (uint8_t)g_stCellInfoReport.u16VCellMinPosition;
    packet->max_feedback_current = SIF_PLACEHOLDER;
    packet->request_charging_voltage = (uint16_t)(43u * SeriesNum);
    packet->request_charging_current = 20u;
    packet->status_charging = 0u;
    packet->random_key = SIF_PLACEHOLDER;
    packet->result_key = SIF_PLACEHOLDER;
    packet->verify = sif_checksum((const uint8_t *)packet, (uint16_t)(sizeof(*packet) - 1u));

    s_tx_length = (uint8_t)sizeof(*packet);
    memcpy(s_tx_data, packet, s_tx_length);
}

static void sif_build_cell_packet(void)
{
    uint8_t index;
    sif_cell_packet_t *packet = &s_packet.cell_packet;

    packet->id = 0x3Bu;
    packet->ver = 0x01u;
    packet->len = (uint8_t)(2u * SeriesNum);
    for (index = 0u; index < SeriesNum; ++index)
    {
        packet->cell_voltage[index] = g_stCellInfoReport.u16VCell[index];
    }
    packet->verify = SIF_PLACEHOLDER;

    s_tx_length = (uint8_t)sizeof(*packet);
    memcpy(s_tx_data, packet, s_tx_length);
}

void sif_timer_init(void)
{
    reg_irq_mask |= FLD_IRQ_TMR0_EN;
    reg_tmr0_tick = 0;
    reg_tmr0_capt = SIF_TICK_US * CLOCK_SYS_CLOCK_1US;
    reg_tmr_sta = FLD_TMR_STA_TMR0;
    reg_tmr_ctrl |= FLD_TMR0_EN;
    irq_enable();
}

_attribute_ram_code_ void sif_timer_irq_proc(void)
{
    if (reg_tmr_sta & FLD_TMR_STA_TMR0)
    {
        sif_send_data_handle();
        reg_tmr_sta = FLD_TMR_STA_TMR0;
    }
}

void sif_send_data_handle(void)
{
#ifdef _FUNC_SIF_
    static uint8_t first_frames = 1u;
    static uint8_t public_frame_count;
    static uint16_t private_frame_count;
    static uint16_t idle_tick;
    const uint8_t *data = s_tx_data;

    if (bus_mux_get_state() != BUS_STATE_OWC_TX)
    {
        idle_tick = 0u;
        s_state = SIF_IDLE;
        first_frames = 1u;
        private_frame_count = 0u;
        return;
    }

    switch (s_state)
    {
    case SIF_IDLE:
        sif_turn_on();
        if (!gpio_read(OWC_RX_PIN))
        {
            bus_mux_set_state(BUS_STATE_OWC_IDLE);
            idle_tick = 0u;
            s_state = SIF_IDLE;
            first_frames = 1u;
            private_frame_count = 0u;
            return;
        }

        if (++idle_tick >= SIF_IDLE_TICKS)
        {
            idle_tick = 0u;
            s_state = SIF_SYNC_SIGNAL;
        }
        break;

    case SIF_SYNC_SIGNAL:
        if (s_sync_tick < (SIF_SYNC_TICKS - 2u)) sif_turn_off();
        else sif_turn_on();

        if (++s_sync_tick >= SIF_SYNC_TICKS)
        {
            s_sync_tick = 0u;
            s_bit_index = 0u;
            s_byte_index = 0u;
            s_bit_tick = 0u;
            s_state = SIF_SEND_DATA;

            if (first_frames)
            {
                sif_build_public_packet();
                if (++public_frame_count >= SIF_PUBLIC_FRAME_COUNT)
                {
                    public_frame_count = 0u;
                    first_frames = 0u;
                }
            }
            else if (++private_frame_count <= 15u)
            {
                sif_build_realtime_packet();
            }
            else
            {
                private_frame_count = 0u;
                sif_build_cell_packet();
            }
        }
        break;

    case SIF_SEND_DATA:
    {
        uint8_t bit;

        s_bit_tick = (uint8_t)(s_bit_tick % SIF_BIT_TICKS);
        bit = (uint8_t)((data[s_byte_index] >> s_bit_index) & 0x01u);

        if (bit)
        {
            if (s_bit_tick == 0u)
            {
                sif_turn_off();
                ++s_bit_tick;
            }
            else if (s_bit_tick == 1u)
            {
                sif_turn_on();
                ++s_bit_tick;
            }
            else if (s_bit_tick == 3u)
            {
                s_bit_tick = 0u;
            }
            else
            {
                ++s_bit_tick;
            }
        }
        else
        {
            if (s_bit_tick == 0u)
            {
                sif_turn_off();
                ++s_bit_tick;
            }
            else if (s_bit_tick == 3u)
            {
                sif_turn_on();
                s_bit_tick = 0u;
            }
            else
            {
                ++s_bit_tick;
            }
        }

        if (s_bit_tick == 0u)
        {
            if (++s_bit_index > 7u)
            {
                ++s_byte_index;
                s_bit_index = 0u;
            }
            if (s_byte_index >= s_tx_length)
            {
                s_state = SIF_STOP_SIGNAL;
            }
        }
        break;
    }

    case SIF_STOP_SIGNAL:
        if (s_sync_tick++ <= SIF_STOP_TICKS)
        {
            sif_turn_off();
        }
        else
        {
            s_state = SIF_IDLE;
            sif_turn_on();
            s_sync_tick = 0u;
        }
        break;

    case SIF_SEND_DATA_COMPLETE:
        if (++idle_tick >= SIF_INITIAL_IDLE_TICKS)
        {
            idle_tick = 0u;
            s_state = SIF_SYNC_SIGNAL;
        }
        sif_turn_on();
        break;

    default:
        s_state = SIF_IDLE;
        break;
    }
#else
    (void)s_tx_data;
#endif
}
