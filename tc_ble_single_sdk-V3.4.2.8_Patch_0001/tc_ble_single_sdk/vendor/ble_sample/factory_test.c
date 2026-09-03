#include "factory_test.h"
#include "conf.h"
#include "sci_upper.h"
#include "sh367309_datadeal.h"
#include "app.h"

#include <string.h>

extern struct stCell_Info g_stCellInfoReport;
extern volatile union System_Status SystemStatus;

static u16 s_factory_token;
static u32 s_factory_last_heartbeat_tick;
static u8 s_factory_active;
static u16 s_factory_injection_mask;
static u16 s_factory_cell_mv[32];
static u16 s_factory_pack_cv;
static int16_t s_factory_current_tenth_a;
static u16 s_factory_temp_deci_raw;
static u16 s_factory_mos_temp_deci_raw;
static u16 s_factory_soc_percent;

static u16 factory_crc16(const u8 *data, u32 len)
{
    u16 crc = 0xFFFFu;
    u32 i;
    u8 bit;

    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) ? (u16)((crc >> 1) ^ 0xA001u) : (u16)(crc >> 1);
        }
    }
    return crc;
}

static u16 factory_get_u16be(const u8 *data)
{
    return (u16)(((u16)data[0] << 8) | data[1]);
}

static u32 factory_get_u32be(const u8 *data)
{
    return ((u32)data[0] << 24) | ((u32)data[1] << 16) |
           ((u32)data[2] << 8) | data[3];
}

static void factory_put_u16be(u8 *data, u16 value)
{
    data[0] = (u8)(value >> 8);
    data[1] = (u8)(value & 0xFFu);
}

static u16 factory_remaining_seconds(void)
{
    u32 elapsed_us;

    if (!s_factory_active) {
        return 0u;
    }
    elapsed_us = (u32)(clock_time() - s_factory_last_heartbeat_tick);
    if (elapsed_us >= (u32)FACTORY_TEST_SESSION_TIMEOUT_MS * 1000u) {
        return 0u;
    }
    return (u16)(((u32)FACTORY_TEST_SESSION_TIMEOUT_MS * 1000u - elapsed_us + 999999u) / 1000000u);
}

static u16 factory_new_token(void)
{
    u16 token = (u16)((clock_time() ^ 0x5A3Cu) & 0xFFFFu);
    return token ? token : 1u;
}

static void factory_clear_injection(void)
{
    memset(s_factory_cell_mv, 0, sizeof(s_factory_cell_mv));
    s_factory_injection_mask = 0u;
    s_factory_pack_cv = 0u;
    s_factory_current_tenth_a = 0;
    s_factory_temp_deci_raw = 0u;
    s_factory_mos_temp_deci_raw = 0u;
    s_factory_soc_percent = 0u;
}

void factory_test_clear_session(void)
{
    factory_clear_injection();
    s_factory_token = 0u;
    s_factory_last_heartbeat_tick = 0u;
    s_factory_active = 0u;
}

static void factory_open_session(void)
{
    factory_clear_injection();
    s_factory_token = factory_new_token();
    s_factory_last_heartbeat_tick = clock_time();
    s_factory_active = 1u;
}

static u8 factory_session_valid(u16 token)
{
    if (!s_factory_active || (token == 0u) || (token != s_factory_token)) {
        return 0u;
    }
    if (factory_remaining_seconds() == 0u) {
        factory_test_clear_session();
        return 0u;
    }
    s_factory_last_heartbeat_tick = clock_time();
    return 1u;
}

u8 factory_test_is_active(void)
{
    return s_factory_active;
}

void factory_test_poll(void)
{
    if (s_factory_active && (factory_remaining_seconds() == 0u)) {
        factory_test_clear_session();
    }
}

static u16 factory_input_mask_for_kind(u8 kind)
{
    switch (kind) {
    case FACTORY_INPUT_CELL_MV: return FACTORY_INPUT_MASK_CELL;
    case FACTORY_INPUT_PACK_CV: return FACTORY_INPUT_MASK_PACK;
    case FACTORY_INPUT_CURRENT_TENTH_A: return FACTORY_INPUT_MASK_CURRENT;
    case FACTORY_INPUT_TEMP_DECI_RAW: return FACTORY_INPUT_MASK_TEMP;
    case FACTORY_INPUT_MOS_TEMP_DECI_RAW: return FACTORY_INPUT_MASK_MOS_TEMP;
    case FACTORY_INPUT_SOC_PERCENT: return FACTORY_INPUT_MASK_SOC;
    default: return 0u;
    }
}

static u8 factory_set_injection(u8 kind, u8 index, u16 value)
{
    u16 mask = factory_input_mask_for_kind(kind);

    if (mask == 0u) {
        return FACTORY_STATUS_UNSUPPORTED;
    }
    if ((kind == FACTORY_INPUT_CELL_MV) && (index >= SeriesNum)) {
        return FACTORY_STATUS_OUT_OF_RANGE;
    }
    if ((kind == FACTORY_INPUT_SOC_PERCENT) && (value > 100u)) {
        return FACTORY_STATUS_OUT_OF_RANGE;
    }
    if ((kind == FACTORY_INPUT_TEMP_DECI_RAW || kind == FACTORY_INPUT_MOS_TEMP_DECI_RAW) &&
        (value > 3000u)) {
        return FACTORY_STATUS_OUT_OF_RANGE;
    }

    if (kind == FACTORY_INPUT_CELL_MV) {
        s_factory_cell_mv[index] = value;
    } else if (kind == FACTORY_INPUT_PACK_CV) {
        s_factory_pack_cv = value;
    } else if (kind == FACTORY_INPUT_CURRENT_TENTH_A) {
        s_factory_current_tenth_a = (int16_t)value;
    } else if (kind == FACTORY_INPUT_TEMP_DECI_RAW) {
        s_factory_temp_deci_raw = value;
    } else if (kind == FACTORY_INPUT_MOS_TEMP_DECI_RAW) {
        s_factory_mos_temp_deci_raw = value;
    } else {
        s_factory_soc_percent = value;
    }
    s_factory_injection_mask |= mask;
    return FACTORY_STATUS_OK;
}

void factory_test_get_effective_measurement(factory_test_effective_measurement_t *measurement)
{
    u8 i;
    u32 pack_mv = 0u;
    u16 cell_value;
    u16 cell_max = 0u;
    u16 cell_min = 0xFFFFu;

    if (measurement == NULL) {
        return;
    }
    memset(measurement, 0, sizeof(*measurement));
    measurement->injection_mask = s_factory_active ? s_factory_injection_mask : 0u;
    for (i = 0u; i < SeriesNum; ++i) {
        cell_value = g_stCellInfoReport.u16VCell[i];
        if (s_factory_active && (s_factory_injection_mask & FACTORY_INPUT_MASK_CELL)) {
            if (s_factory_cell_mv[i] != 0u) {
                cell_value = s_factory_cell_mv[i];
            }
        }
        measurement->cell_mv[i] = cell_value;
        pack_mv += cell_value;
        if (cell_value > cell_max) {
            cell_max = cell_value;
        }
        if (cell_value < cell_min) {
            cell_min = cell_value;
        }
    }
    measurement->cell_max_mv = cell_max;
    measurement->cell_min_mv = cell_min;
    measurement->cell_delta_mv = (u16)(cell_max - cell_min);
    measurement->pack_cv = (s_factory_active && (s_factory_injection_mask & FACTORY_INPUT_MASK_PACK))
                               ? s_factory_pack_cv
                               : (u16)(pack_mv / 10u);

    if (s_factory_active && (s_factory_injection_mask & FACTORY_INPUT_MASK_CURRENT)) {
        if (s_factory_current_tenth_a >= 0) {
            measurement->charge_current_tenth_a = (u16)s_factory_current_tenth_a;
        } else {
            measurement->discharge_current_tenth_a = (u16)(-s_factory_current_tenth_a);
        }
    } else {
        measurement->charge_current_tenth_a = g_stCellInfoReport.u16Ichg;
        measurement->discharge_current_tenth_a = g_stCellInfoReport.u16IDischg;
    }

    measurement->temp_max_deci_raw = g_stCellInfoReport.u16TempMax;
    measurement->temp_min_deci_raw = g_stCellInfoReport.u16TempMin;
    if (s_factory_active && (s_factory_injection_mask & FACTORY_INPUT_MASK_TEMP)) {
        measurement->temp_max_deci_raw = s_factory_temp_deci_raw;
        measurement->temp_min_deci_raw = s_factory_temp_deci_raw;
    }
    measurement->mos_temp_deci_raw = g_stCellInfoReport.u16Temperature[MOS_TEMP1];
    if (s_factory_active && (s_factory_injection_mask & FACTORY_INPUT_MASK_MOS_TEMP)) {
        measurement->mos_temp_deci_raw = s_factory_mos_temp_deci_raw;
    }
    measurement->soc_percent = (s_factory_active && (s_factory_injection_mask & FACTORY_INPUT_MASK_SOC))
                                   ? s_factory_soc_percent
                                   : g_stCellInfoReport.SocElement.u16Soc;
}

static u32 factory_put_status_payload(u8 *rsp, u16 token)
{
    factory_test_effective_measurement_t measurement;
    u32 offset = 0u;

    factory_test_get_effective_measurement(&measurement);
    factory_put_u16be(&rsp[offset], token); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.injection_mask); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.cell_max_mv); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.cell_min_mv); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.cell_delta_mv); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.pack_cv); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.charge_current_tenth_a); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.discharge_current_tenth_a); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.temp_max_deci_raw); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.temp_min_deci_raw); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.mos_temp_deci_raw); offset += 2u;
    factory_put_u16be(&rsp[offset], measurement.soc_percent); offset += 2u;
    factory_put_u16be(&rsp[offset], g_stCellInfoReport.unMdlFault_First.all); offset += 2u;
    factory_put_u16be(&rsp[offset], g_stCellInfoReport.unMdlFault_Second.all); offset += 2u;
    factory_put_u16be(&rsp[offset], g_stCellInfoReport.unMdlFault_Third.all); offset += 2u;
    factory_put_u16be(&rsp[offset], (u16)(SystemStatus.bits.b1Status_MOS_CHG |
                                          ((u16)SystemStatus.bits.b1Status_MOS_DSG << 1))); offset += 2u;
    return offset;
}

static int factory_response(u8 addr, u8 command, u8 status, u8 *payload, u32 payload_len,
                            u8 *rsp, u32 *rsp_len)
{
    u32 length = 0u;
    u16 crc;

    rsp[length++] = addr;
    rsp[length++] = FACTORY_TEST_MODBUS_FUNC;
    rsp[length++] = command;
    rsp[length++] = status;
    if (payload_len != 0u) {
        memcpy(&rsp[length], payload, payload_len);
        length += payload_len;
    }
    crc = factory_crc16(rsp, length);
    rsp[length++] = (u8)(crc & 0xFFu);
    rsp[length++] = (u8)(crc >> 8);
    *rsp_len = length;
    return 1;
}

int factory_test_modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len)
{
    u8 command;
    u8 status;
    u8 payload[40];
    u16 token;
    u16 crc_received;
    u16 crc_calculated;
    u32 payload_len;

    *rsp_len = 0u;
    if ((req == NULL) || (rsp == NULL) || (req_len < 5u) || (req[0] != FACTORY_TEST_MODBUS_ADDRESS) ||
        (req[1] != FACTORY_TEST_MODBUS_FUNC)) {
        return 0;
    }
    crc_received = (u16)(((u16)req[req_len - 1u] << 8) | req[req_len - 2u]);
    crc_calculated = factory_crc16(req, req_len - 2u);
    if (crc_received != crc_calculated) {
        return 0;
    }

    command = req[2];
    payload_len = 0u;
    if (command == FACTORY_CMD_OPEN) {
        if ((req_len != 9u) || (factory_get_u32be(&req[3]) != FACTORY_TEST_UNLOCK_MAGIC)) {
            return factory_response(req[0], command, FACTORY_STATUS_AUTH_REQUIRED, NULL, 0u, rsp, rsp_len);
        }
        factory_open_session();
        factory_put_u16be(&payload[payload_len], s_factory_token); payload_len += 2u;
        factory_put_u16be(&payload[payload_len], FACTORY_TEST_SESSION_TIMEOUT_MS / 1000u); payload_len += 2u;
        payload[payload_len++] = FACTORY_TEST_PROTOCOL_VERSION;
        payload[payload_len++] = SeriesNum;
        return factory_response(req[0], command, FACTORY_STATUS_OK, payload, payload_len, rsp, rsp_len);
    }

    if (req_len < 7u) {
        return factory_response(req[0], command, FACTORY_STATUS_BAD_REQUEST, NULL, 0u, rsp, rsp_len);
    }
    token = factory_get_u16be(&req[3]);
    if (!factory_session_valid(token)) {
        return factory_response(req[0], command, FACTORY_STATUS_AUTH_REQUIRED, NULL, 0u, rsp, rsp_len);
    }

    switch (command) {
    case FACTORY_CMD_HEARTBEAT:
        if (req_len != 7u) status = FACTORY_STATUS_BAD_REQUEST;
        else status = FACTORY_STATUS_OK;
        factory_put_u16be(&payload[0], s_factory_token);
        factory_put_u16be(&payload[2], factory_remaining_seconds());
        return factory_response(req[0], command, status, payload, 4u, rsp, rsp_len);
    case FACTORY_CMD_INJECT:
        if (req_len != 10u) {
            status = FACTORY_STATUS_BAD_REQUEST;
        } else {
            status = factory_set_injection(req[5], req[6], factory_get_u16be(&req[7]));
        }
        factory_put_u16be(&payload[0], s_factory_token);
        factory_put_u16be(&payload[2], s_factory_injection_mask);
        return factory_response(req[0], command, status, payload, 4u, rsp, rsp_len);
    case FACTORY_CMD_CLEAR:
        if (req_len != 7u) status = FACTORY_STATUS_BAD_REQUEST;
        else {
            factory_clear_injection();
            status = FACTORY_STATUS_OK;
        }
        factory_put_u16be(&payload[0], s_factory_token);
        return factory_response(req[0], command, status, payload, 2u, rsp, rsp_len);
    case FACTORY_CMD_CLOSE:
        if (req_len != 7u) status = FACTORY_STATUS_BAD_REQUEST;
        else status = FACTORY_STATUS_OK;
        factory_test_clear_session();
        return factory_response(req[0], command, status, NULL, 0u, rsp, rsp_len);
    case FACTORY_CMD_STATUS:
        if (req_len != 7u) {
            return factory_response(req[0], command, FACTORY_STATUS_BAD_REQUEST, NULL, 0u, rsp, rsp_len);
        }
        payload_len = factory_put_status_payload(payload, s_factory_token);
        return factory_response(req[0], command, FACTORY_STATUS_OK, payload, payload_len, rsp, rsp_len);
    default:
        return factory_response(req[0], command, FACTORY_STATUS_UNSUPPORTED, NULL, 0u, rsp, rsp_len);
    }
}
