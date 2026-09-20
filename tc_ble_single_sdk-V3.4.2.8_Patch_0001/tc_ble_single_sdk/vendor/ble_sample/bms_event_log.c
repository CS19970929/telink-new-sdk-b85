#include "bms_event_log.h"
#include "bms_diag.h"

#include "bms_error.h"
#include "bms_storage_platform.h"
#include "storage_record.h"
#include <string.h>

#define BMS_EVENT_RECORD_MAGIC          0x45565431u /* EVT1 */
#define BMS_EVENT_SCHEMA_VERSION        1u
#define BMS_EVENT_PAYLOAD_BYTES         (2u + (BMS_EVENT_LOG_ENTRY_COUNT * 2u))

typedef struct {
    u8 records[BMS_EVENT_LOG_ENTRY_COUNT][2];
    u16 write_pos;
    u32 interval_s;
    u8 ready;
    u8 event_latched[EVENT_NUM];
    u8 cbc_last;
    storage_record_store_t store;
} bms_event_log_ctx_t;

static bms_event_log_ctx_t g_bms_event_log;

static void bms_event_log_report_store_error(void)
{
    bms_error_raise(BMS_ERROR_EEPROM_STORE);
}

static void bms_event_log_put_u16le(u8 *buf, u16 value)
{
    buf[0] = (u8)(value & 0xFFu);
    buf[1] = (u8)(value >> 8);
}

static u16 bms_event_log_get_u16le(const u8 *buf)
{
    return (u16)((u16)buf[0] | ((u16)buf[1] << 8));
}

static void bms_event_log_clear_runtime_flags(void)
{
    memset(g_bms_event_log.event_latched, 0, sizeof(g_bms_event_log.event_latched));
    g_bms_event_log.cbc_last = 0u;
    g_bms_event_log.interval_s = 0u;
}

static void bms_event_log_reset_ram_only(void)
{
    memset(g_bms_event_log.records, 0, sizeof(g_bms_event_log.records));
    g_bms_event_log.write_pos = 0u;
    bms_event_log_clear_runtime_flags();
}

static void bms_event_log_encode(u8 payload[BMS_EVENT_PAYLOAD_BYTES])
{
    bms_event_log_put_u16le(&payload[0], g_bms_event_log.write_pos);
    memcpy(&payload[2], g_bms_event_log.records, sizeof(g_bms_event_log.records));
}

static int bms_event_log_decode(const u8 payload[BMS_EVENT_PAYLOAD_BYTES])
{
    u16 write_pos = bms_event_log_get_u16le(&payload[0]);
    if (write_pos >= BMS_EVENT_LOG_ENTRY_COUNT) return 0;
    g_bms_event_log.write_pos = write_pos;
    memcpy(g_bms_event_log.records, &payload[2], sizeof(g_bms_event_log.records));
    return 1;
}

static int bms_event_log_write_snapshot(void)
{
    u8 payload[BMS_EVENT_PAYLOAD_BYTES];
    if (!g_bms_event_log.ready) return 0;
    bms_event_log_encode(payload);
    if (!storage_record_save(&g_bms_event_log.store, payload)) {
        bms_event_log_report_store_error();
        return 0;
    }
    return 1;
}

static u8 bms_event_log_map_interval(u32 *seconds)
{
    u8 code;
    u32 value = *seconds;
    if (value <= 60u) code = 171u;
    else if (value <= (3600u * 168u)) code = (u8)((value + 3599u) / 3600u);
    else code = 170u;
    *seconds = 0u;
    return code;
}

static int bms_event_log_append(bms_event_log_id_t event, int startup_event)
{
    u8 old_event;
    u8 old_interval_code;
    u16 old_write_pos;
    u32 old_interval_s;
    u16 pos;

    if (!g_bms_event_log.ready) return 0;
    pos = g_bms_event_log.write_pos;
    if (pos >= BMS_EVENT_LOG_ENTRY_COUNT) pos = 0u;
    old_write_pos = pos;
    old_interval_s = g_bms_event_log.interval_s;
    old_event = g_bms_event_log.records[pos][0];
    old_interval_code = g_bms_event_log.records[pos][1];
    g_bms_event_log.records[pos][0] = (u8)event;
    g_bms_event_log.records[pos][1] = startup_event ? 0u :
        bms_event_log_map_interval(&g_bms_event_log.interval_s);
    pos += 1u;
    if (pos >= BMS_EVENT_LOG_ENTRY_COUNT) pos = 0u;
    g_bms_event_log.write_pos = pos;

    if (!bms_event_log_write_snapshot()) {
        g_bms_event_log.records[old_write_pos][0] = old_event;
        g_bms_event_log.records[old_write_pos][1] = old_interval_code;
        g_bms_event_log.write_pos = old_write_pos;
        g_bms_event_log.interval_s = old_interval_s;
        return 0;
    }
    return 1;
}

static void bms_event_log_track_edge(u8 active, bms_event_log_id_t event)
{
    if ((u32)event >= EVENT_NUM) return;
    if (active) {
        if (!g_bms_event_log.event_latched[event] && bms_event_log_append(event, 0)) {
            g_bms_event_log.event_latched[event] = 1u;
        }
    } else {
        g_bms_event_log.event_latched[event] = 0u;
    }
}

static void bms_event_log_track_change(u8 value, bms_event_log_id_t event)
{
    if ((g_bms_event_log.cbc_last != value) && bms_event_log_append(event, 0)) {
        g_bms_event_log.cbc_last = value;
    }
}

int bms_event_log_init(void)
{
    const storage_port_t *port;
    storage_region_t region;
    u8 payload[BMS_EVENT_PAYLOAD_BYTES];
    bms_diag_attempt(BMS_STORAGE_DOMAIN_EVENT);
    memset(&g_bms_event_log, 0, sizeof(g_bms_event_log));
    port = bms_storage_platform_port();
    if (port == 0) { bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_PORT); return 0; }
    if (!bms_storage_platform_region(BMS_STORAGE_DOMAIN_EVENT, &region)) {
        bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_REGION); return 0;
    }
    if (!storage_record_open(&g_bms_event_log.store, port, region,
                             BMS_EVENT_RECORD_MAGIC, BMS_EVENT_SCHEMA_VERSION,
                             BMS_EVENT_PAYLOAD_BYTES)) {
        bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_OPEN); return 0;
    }
    if (!storage_record_load(&g_bms_event_log.store, payload) ||
        !bms_event_log_decode(payload)) {
        bms_event_log_reset_ram_only();
        bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_DEFAULTS);
    } else bms_diag_result(BMS_STORAGE_DOMAIN_EVENT, DIAG_OK);
    g_bms_event_log.ready = 1u;
    bms_event_log_clear_runtime_flags();
    return 1;
}

void bms_event_log_note_startup(void)
{
    if (!g_bms_event_log.ready && !bms_event_log_init()) return;
    (void)bms_event_log_append(BMS_START_UP, 1);
}

void bms_event_log_note_sleep(void)
{
    if (!g_bms_event_log.ready && !bms_event_log_init()) return;
    (void)bms_event_log_append(BMS_SLEEP, 0);
}

void bms_event_log_poll_1s(const bms_event_log_sample_t *sample)
{
    if (sample == 0) return;
    if (!g_bms_event_log.ready && !bms_event_log_init()) return;
    g_bms_event_log.interval_s += 1u;
    bms_event_log_track_edge(sample->balance, BALANCE_OPEN);
    bms_event_log_track_edge(sample->vcell_ovp, VCELL_OVP);
    bms_event_log_track_edge(sample->vbus_ovp, VBUS_OVP);
    bms_event_log_track_edge(sample->chg_ocp, CHG_OCP);
    bms_event_log_track_edge(sample->vcell_uvp, VCELL_UVP);
    bms_event_log_track_edge(sample->vbus_uvp, VBUS_UVP);
    bms_event_log_track_edge(sample->dsg_ocp, DSG_OCP);
    bms_event_log_track_edge(sample->chg_utp, CHG_UTP);
    bms_event_log_track_edge(sample->dsg_utp, DSG_UTP);
    bms_event_log_track_edge(sample->chg_otp, CHG_OTP);
    bms_event_log_track_edge(sample->dsg_otp, DSG_OTP);
    bms_event_log_track_edge(sample->vdelta_op, VDELTA_OP);
    bms_event_log_track_edge(sample->afe2_err, AFE1_ERR);
    bms_event_log_track_change(sample->cbc_err, CBC_ERR);
}

u16 bms_event_log_read_reg(u16 reg)
{
    u16 idx;
    if (reg >= BMS_EVENT_LOG_REG_COUNT) return 0u;
    if (!g_bms_event_log.ready && !bms_event_log_init()) return 0u;
    idx = (u16)(g_bms_event_log.write_pos + BMS_EVENT_LOG_ENTRY_COUNT - 1u - reg);
    while (idx >= BMS_EVENT_LOG_ENTRY_COUNT) idx = (u16)(idx - BMS_EVENT_LOG_ENTRY_COUNT);
    return (u16)(((u16)g_bms_event_log.records[idx][0] << 8) |
                 g_bms_event_log.records[idx][1]);
}

int bms_event_log_factory_reset(void)
{
    u8 old_records[BMS_EVENT_LOG_ENTRY_COUNT][2];
    u16 old_write_pos;
    u32 old_interval_s;
    if (!g_bms_event_log.ready && !bms_event_log_init()) return 0;
    memcpy(old_records, g_bms_event_log.records, sizeof(old_records));
    old_write_pos = g_bms_event_log.write_pos;
    old_interval_s = g_bms_event_log.interval_s;
    bms_event_log_reset_ram_only();
    if (!bms_event_log_write_snapshot()) {
        memcpy(g_bms_event_log.records, old_records, sizeof(old_records));
        g_bms_event_log.write_pos = old_write_pos;
        g_bms_event_log.interval_s = old_interval_s;
        return 0;
    }
    return 1;
}

void test_log_balance_first(void) { bms_event_log_track_edge(1u, BALANCE_OPEN); }

void test_log_app(void)
{
    bms_event_log_track_edge(1u, VCELL_OVP);
    bms_event_log_track_edge(1u, VBUS_OVP);
    bms_event_log_track_edge(1u, CHG_OCP);
    bms_event_log_track_edge(1u, VCELL_UVP);
    bms_event_log_track_edge(1u, VBUS_UVP);
}
