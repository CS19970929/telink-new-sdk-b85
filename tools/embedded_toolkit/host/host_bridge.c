/* Host harness, deliberately NOT compiled by production source_order.txt.
 * Software protection below is the complete current production C source.
 * The final command gate is a conservative reference model, not SH register
 * emulation or a replacement for production common-port/body-diode policy. */
#include "host_bridge.h"
#include "bms_state.h"
#include "bms_error.h"
#include <stdio.h>
#include <string.h>

struct { struct PRT_E2ROM_PARAS protect; } g_tParam;
struct stCell_Info g_stCellInfoReport;
static uint8_t sensor_broken;
static uint8_t short_latched;
static uint8_t sample_started;
static uint32_t last_ms;
static uint8_t release_samples;
static uint8_t valid_samples;

void bms_error_raise(bms_error_id_t id) { (void)id; sensor_broken = 1u; }
void bms_error_clear(bms_error_id_t id) { (void)id; sensor_broken = 0u; }
uint8_t bms_error_get(bms_error_id_t id) { (void)id; return sensor_broken; }
uint8_t bms_protection_params_valid(void) {
    return bms_sw_protection_validate_params(&g_tParam.protect);
}
void bms_fault_history_record(bms_fault_code_t code) { (void)code; }

/* Includes the copied, unmodified production implementation, with only its
 * hardware-coupled param.h replaced by a generated host declaration. */
#include "production_sw.c"

void bms_host_reset(void) {
    memset(&g_stCellInfoReport, 0, sizeof(g_stCellInfoReport));
    short_latched = sample_started = release_samples = valid_samples = 0u;
    last_ms = 0u;
    bms_sw_protection_init();
}

/* Do not expose compiler-dependent legacy bitfield memory as a wire format. */
static uint16_t fault_mask(const bms_fault_bits_t *f) {
    return (uint16_t)((f->b1CellOvp ? 1u : 0u) | (f->b1CellUvp ? 2u : 0u) |
        (f->b1BatOvp ? 4u : 0u) | (f->b1BatUvp ? 8u : 0u) |
        (f->b1IchgOcp ? 16u : 0u) | (f->b1IdischgOcp ? 32u : 0u) |
        (f->b1CellChgOtp ? 64u : 0u) | (f->b1CellChgUtp ? 128u : 0u) |
        (f->b1CellDischgOtp ? 256u : 0u) | (f->b1CellDischgUtp ? 512u : 0u) |
        (f->b1TmosOtp ? 1024u : 0u) | (f->b1VcellDeltaBig ? 2048u : 0u));
}

bms_host_command_t bms_host_step(const bms_host_input_t *in) {
    bms_host_command_t out = {{0u, 0u, 0u}, 0u, 0u, 0u, 0u, 0u};
    bms_sw_protection_inputs_t temp;
    uint8_t healthy;
    if (in == 0 || in->cell_min_mv > in->cell_max_mv ||
        in->battery_min_x10 > in->battery_max_x10 || in->battery_max_x10 > 1450u ||
        in->mos_x10 > 1450u || (in->charge_a10 && in->discharge_a10)) return out;
    /* No catch-up replay of stale samples. Reject duplicate, backward or
     * missing sample times; unsigned subtraction handles uint32 wrap. */
    if (sample_started && (uint32_t)(in->now_ms - last_ms) != 200u) return out;
    sample_started = 1u;
    last_ms = in->now_ms;
    out.accepted = 1u;
    g_stCellInfoReport.u16VCellMin = in->cell_min_mv;
    g_stCellInfoReport.u16VCellMax = in->cell_max_mv;
    g_stCellInfoReport.u16VCellDelta = (uint16_t)(in->cell_max_mv - in->cell_min_mv);
    g_stCellInfoReport.u16VCellTotle = in->pack_10mv;
    g_stCellInfoReport.u16Ichg = in->charge_a10;
    g_stCellInfoReport.u16IDischg = in->discharge_a10;
    temp.battery_temp_valid = in->battery_valid;
    temp.mos_temp_valid = in->mos_valid;
    temp.battery_temp_min = in->battery_min_x10;
    temp.battery_temp_max = in->battery_max_x10;
    temp.mos_temp = in->mos_x10;
    if (in->short_active) short_latched = 1u;
    healthy = (uint8_t)(in->communication_ok && bms_protection_params_valid());
    if (healthy && in->battery_valid && in->mos_valid) {
        if (valid_samples < 3u) ++valid_samples;
    } else valid_samples = 0u;
    if (healthy) bms_sw_protection_update(&temp);
    else bms_sw_protection_clear(); /* new qualification after communication gap */
    if (short_latched && healthy && !in->short_active && in->physical_release) {
        if (release_samples < 10u) ++release_samples;
        if (release_samples >= 10u) short_latched = 0u;
    } else release_samples = 0u;
    out.fault[0] = fault_mask(&g_stCellInfoReport.unMdlFault_First.bits);
    out.fault[1] = fault_mask(&g_stCellInfoReport.unMdlFault_Second.bits);
    out.fault[2] = fault_mask(&g_stCellInfoReport.unMdlFault_Third.bits);
    out.temp_break = sensor_broken;
    out.short_latched = short_latched;
    out.charge_on = (uint8_t)(healthy && valid_samples >= 3u && in->output_enabled && in->charge_request &&
        !in->afe_charge_block && !bms_sw_protection_charge_blocked());
    out.discharge_on = (uint8_t)(healthy && valid_samples >= 3u && in->output_enabled && in->discharge_request &&
        !in->afe_discharge_block && !short_latched && !bms_sw_protection_discharge_blocked());
    return out;
}

static uint8_t set_parameter(unsigned int id, const unsigned int *v) {
    struct PRT_E2ROM_PARAS *p = &g_tParam.protect;
    if (id >= 12u || v[0] > 65535u || v[1] > 65535u || v[2] > 65535u ||
        v[3] > 65535u || v[4] > 65535u) return 0u;
#define SET_GROUP(index, prefix) case index: \
    p->prefix##_First=(uint16_t)v[0]; p->prefix##_Second=(uint16_t)v[1]; \
    p->prefix##_Third=(uint16_t)v[2]; p->prefix##_Rcv=(uint16_t)v[3]; \
    p->prefix##_Filter=(uint16_t)v[4]; break
    switch (id) {
        SET_GROUP(0, u16VcellOvp); SET_GROUP(1, u16VcellUvp);
        SET_GROUP(2, u16VbusOvp); SET_GROUP(3, u16VbusUvp);
        SET_GROUP(4, u16IchgOcp); SET_GROUP(5, u16IdsgOcp);
        SET_GROUP(6, u16TChgOTp); SET_GROUP(7, u16TchgUTp);
        SET_GROUP(8, u16TdischgOTp); SET_GROUP(9, u16TdischgUTp);
        SET_GROUP(10, u16TmosOTp); SET_GROUP(11, u16VdeltaOvp);
        default: return 0u;
    }
#undef SET_GROUP
    return 1u;
}

int main(void) {
    char line[512];
    unsigned int v[19], id;
    bms_host_input_t in;
    bms_host_command_t out;
    bms_host_reset();
    while (fgets(line, sizeof(line), stdin) != 0) {
        if (line[0] == 'R') { bms_host_reset(); continue; }
        if (line[0] == 'P') {
            if (sscanf(line + 1, "%u %u %u %u %u %u", &id, &v[0], &v[1], &v[2], &v[3], &v[4]) != 6 ||
                !set_parameter(id, v)) return 2;
            continue;
        }
        if (line[0] != 'S' || sscanf(line + 1,
            "%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u",
            &v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9],
            &v[10],&v[11],&v[12],&v[13],&v[14],&v[15],&v[16],&v[17],&v[18]) != 19) return 2;
        /* Driver validates narrowing, flags, sensor encoding and extrema. */
        for (id = 1; id <= 8; ++id) if (v[id] > 65535u) return 2;
        for (id = 9; id < 19; ++id) if (v[id] > 1u) return 2;
        if (v[1] > v[2] || v[6] > v[7] || v[7] > 1450u || v[8] > 1450u) return 2;
        in.now_ms=(uint32_t)v[0]; in.cell_min_mv=(uint16_t)v[1]; in.cell_max_mv=(uint16_t)v[2];
        in.pack_10mv=(uint16_t)v[3]; in.charge_a10=(uint16_t)v[4]; in.discharge_a10=(uint16_t)v[5];
        in.battery_min_x10=(uint16_t)v[6]; in.battery_max_x10=(uint16_t)v[7]; in.mos_x10=(uint16_t)v[8];
        in.battery_valid=(uint8_t)v[9]; in.mos_valid=(uint8_t)v[10]; in.communication_ok=(uint8_t)v[11];
        in.charge_request=(uint8_t)v[12]; in.discharge_request=(uint8_t)v[13]; in.output_enabled=(uint8_t)v[14];
        in.afe_charge_block=(uint8_t)v[15]; in.afe_discharge_block=(uint8_t)v[16];
        in.short_active=(uint8_t)v[17]; in.physical_release=(uint8_t)v[18];
        out=bms_host_step(&in);
        printf("%u %u %u %u %u %u %u %u\n", (unsigned int)out.fault[0], (unsigned int)out.fault[1],
            (unsigned int)out.fault[2], (unsigned int)out.charge_on, (unsigned int)out.discharge_on,
            (unsigned int)out.temp_break, (unsigned int)out.short_latched, (unsigned int)out.accepted);
    }
    return ferror(stdin) ? 2 : 0;
}
