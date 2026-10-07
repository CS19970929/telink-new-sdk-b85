#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "drivers.h"
#include "bms_parameters.h"
#include "bms_state.h"
#include "bms_afe.h"
#include "bms_afe_driver.h"
#include "bms_afe_hw_profile.h"
#include "bms_sw_protection.h"
#include "bms_error.h"
#include "bms_storage_platform.h"
#include "bms_diag.h"
#include "sh3673520.h"
#include "sh3673520_port.h"
#include "sh3673510_control.h"
#include "sh3673510_project_config.h"

static uint32_t now_32k, spi_calls;
static uint8_t regs[256], frame[256], reply[256];
static unsigned pos, read_count;
static uint8_t openwire_pending, openwire_odd;
static uint8_t flash[20u * 4096u];
static int bus_failed, flash_cut = -1;
static int corrupt_read_reg = -1;
static uint8_t ready_bits = SH3673520_FLAG2_VADC_MASK | SH3673520_FLAG2_CADC_MASK;
/* This scenario uses the parameter owner, not Modbus ingress. Never fake success. */
u8 bms_afe_hw_write_complete_frame(const u8 *f, u32 n)
{ (void)f; (void)n; abort(); }

/* Independent CRC implementation for the virtual wire peer. */
static uint8_t wire_crc(const uint8_t *data, unsigned n)
{
    uint8_t c = 0;
    for (unsigned i = 0; i < n; ++i) {
        c ^= data[i];
        for (unsigned b = 0; b < 8; ++b)
            c = (uint8_t)((c << 1) ^ ((c & 128u) ? 7u : 0u));
    }
    return c;
}
u32 pm_get_32k_tick(void) { return now_32k; }
u32 clock_time(void) { return now_32k * 500u; }
int clock_time_exceed(u32 t, u32 us) { return (u32)(clock_time()-t) > us*16u; }
uint32_t bms_diag_tick(void) { return now_32k; }
void gpio_set_func(GPIO_PinTypeDef p, unsigned v) { (void)p; (void)v; }
void gpio_write(GPIO_PinTypeDef p, unsigned v) { (void)v; assert(p != GPIO_PB4 && p != GPIO_PB5); }
void gpio_set_input_en(GPIO_PinTypeDef p, unsigned v) { (void)p; (void)v; }
void gpio_set_output_en(GPIO_PinTypeDef p, unsigned v) { (void)p; (void)v; }
int gpio_read(GPIO_PinTypeDef p) { (void)p; return 0; }
void cpu_set_gpio_wakeup(GPIO_PinTypeDef p, unsigned l, unsigned e) { (void)p; (void)l; (void)e; }
void sh3673520_port_delay_us(uint32_t us) { now_32k += us*32u/1000u; }
void sh3673520_port_delay_ms(uint32_t ms) { now_32k += ms*32u; }
sh3673520_port_status_t SH3673520_PortConfigure(sh3673520_spi_group_t g)
{ assert(g == SH3673520_SPI_GROUP_B6_B7_D2_D7); return SH3673520_PORT_OK; }
sh3673520_port_status_t sh3673520_port_init(void) { return SH3673520_PORT_OK; }
sh3673520_port_status_t sh3673520_port_recover(void) { return SH3673520_PORT_OK; }
sh3673520_port_status_t sh3673520_port_begin(void)
{
    ++spi_calls; pos = 0; read_count = 0;
    return bus_failed ? SH3673520_PORT_ERR_SPI : SH3673520_PORT_OK;
}
sh3673520_port_status_t sh3673520_port_xfer(uint8_t tx, uint8_t *rx)
{
    assert(pos < sizeof(frame)); frame[pos] = tx;
    if (pos == 0) *rx = 0xff;
    else if (pos <= 3) *rx = frame[pos-1];
    else if (frame[0] == 2) {
        unsigned i = pos-4;
        assert(i <= read_count);
        *rx = i < read_count ? reply[i] : 0;
        if (i == read_count) {
            uint8_t bytes[256] = {0xff, 2, frame[1], frame[2]};
            memcpy(bytes+4, reply, read_count);
            *rx = wire_crc(bytes, read_count+4);
        }
    } else *rx = 0xa5;
    if (pos == 2 && frame[0] == 2) {
        read_count = tx; assert((unsigned)frame[1]+tx <= sizeof(regs));
        memcpy(reply, regs+frame[1], tx);
        if (corrupt_read_reg >= frame[1] && corrupt_read_reg < (int)frame[1]+tx)
            reply[corrupt_read_reg-frame[1]] ^= 0x80u;
        if (frame[1] <= 0x59 && (unsigned)frame[1]+tx > 0x59)
            regs[0x59] &= (uint8_t)~0x03u; /* FLAG2 VADC/CADC read-clear */
    }
    if (pos == 3 && frame[0] != 2) {
        assert(tx == wire_crc(frame, 3));
        if (frame[0] == 1) {
            if (frame[1] == 0x58 || frame[1] == 0x59) regs[frame[1]] &= frame[2];
            else regs[frame[1]] = frame[2];
            if (frame[1] == SH3673520_REG_SCONF3 && (frame[2] & SH3673520_SCONF3_OWD_TRG_MASK))
                openwire_pending=1;
        } else assert(frame[0] == 0x0b && frame[1] == 0xbb && frame[2] == 0xcc);
    }
    ++pos; return SH3673520_PORT_OK;
}
void sh3673520_port_end(void) { }
static int flash_begin(void *c) { (void)c; return 1; }
static void flash_end(void *c) { (void)c; }
static int flash_read(void *c, uint32_t a, uint8_t *b, uint32_t n)
{ (void)c; assert(a <= sizeof(flash) && n <= sizeof(flash)-a); memcpy(b, flash+a, n); return 1; }
static int flash_program(void *c, uint32_t a, const uint8_t *b, uint32_t n)
{
    (void)c; assert(a <= sizeof(flash) && n <= sizeof(flash)-a);
    for (uint32_t i = 0; i < n; ++i) {
        if (flash_cut == 0) return 0;
        if (flash_cut > 0) --flash_cut;
        assert((flash[a+i] | b[i]) == flash[a+i]); flash[a+i] &= b[i];
    }
    return 1;
}
static int flash_erase(void *c, uint32_t a, uint32_t n)
{ (void)c; assert(a <= sizeof(flash) && n <= sizeof(flash)-a); memset(flash+a, 255, n); return 1; }
const storage_port_t *bms_storage_platform_port(void)
{ static const storage_port_t p = {0, 4096, 4, 255, flash_begin, flash_end, flash_read, flash_program, flash_erase}; return &p; }
int bms_storage_platform_region(bms_storage_domain_t d, storage_region_t *r)
{
    if (d == BMS_STORAGE_DOMAIN_CONFIG) { r->base=0; r->size=4*4096; }
    else if (d == BMS_STORAGE_DOMAIN_STATE) { r->base=4*4096; r->size=8*4096; }
    else if (d == BMS_STORAGE_DOMAIN_EVENT) { r->base=12*4096; r->size=8*4096; }
    else return 0;
    return 1;
}
static void raw16(uint8_t address, uint16_t raw)
{ regs[address] = (uint8_t)(raw >> 8); regs[address+1] = (uint8_t)raw; }
static void step(void)
{
    now_32k += 6400u;
    /* Script an intact harness result one host tick after each OWD trigger.
     * This supplies the hardware result, not a replacement feature decision. */
    if (openwire_pending) {
        openwire_pending=0; openwire_odd^=1;
        regs[SH3673520_REG_SCONF3] &= (uint8_t)~SH3673520_SCONF3_OWD_TRG_MASK;
        regs[SH3673520_REG_FLAG3]=SH3673520_FLAG3_OWD_FLG_MASK |
            (openwire_odd ? SH3673520_FLAG3_OWD_IND_MASK : 0);
    }
    regs[SH3673520_REG_FLAG2] |= ready_bits;
    bms_afe_sample();
}
static void steps(unsigned n) { while (n--) step(); }
static int discharge_on(void) { return !!(regs[SH3673520_REG_SCONF2] & SH3673520_SCONF2_DSGMOS_MASK); }
static int charge_on(void) { return !!(regs[SH3673520_REG_SCONF2] & SH3673520_SCONF2_CHGMOS_MASK); }
static void require_both_off(void) { assert(!charge_on() && !discharge_on()); }
static void request_outputs(void)
{ bms_afe_set_output_enabled(1); bms_afe_set_fets(1,1); }
static void reinitialize(void)
{
    bms_afe_init(); request_outputs(); require_both_off();
    steps(3); require_both_off(); steps(9); assert(bms_afe_samples_qualified());
}
static void trip_discharge(void)
{
    regs[SH3673520_REG_BSTATUS2] = SH3673520_BSTATUS2_LOADON_MASK;
    raw16(0x91, (uint16_t)-5000); steps(2);
    assert(g_stCellInfoReport.u16IDischg > 200);
    assert(g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp);
    assert(!discharge_on()); raw16(0x91, 0);
}
static void release_discharge(void)
{
    regs[SH3673520_REG_BSTATUS2] = SH3673520_BSTATUS2_LOADOFF_MASK;
    steps(4); assert(!g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp);
}
int main(void)
{
    const char *scenario = getenv("BMS_LOOP_CASE");
    const char *image = getenv("BMS_LOOP_FLASH");
    assert(scenario && image);
    memset(flash, 255, sizeof(flash));
    if (!strcmp(scenario, "cold-reboot")) {
        FILE *f = fopen(image, "rb"); assert(f);
        assert(fread(flash, 1, sizeof(flash), f) == sizeof(flash));
        assert(fgetc(f) == EOF); assert(!fclose(f));
    }
    for (unsigned i=0; i<20; ++i) raw16((uint8_t)(0x69+2*i), 21120); /* 3300 mV */
    for (unsigned i=0; i<4; ++i) raw16((uint8_t)(0x5d+2*i), 16384); /* 10 kohm */
    raw16(0x93, 6758); /* 26.398 V */
    if (!strcmp(scenario, "boot-failure")) flash_cut=0;
    bms_diag_init(); bms_parameters_startup(); bms_parameters_init();
    if (!strcmp(scenario, "boot-failure")) {
        assert(!bms_protection_params_valid()); bms_afe_init(); request_outputs();
        steps(15); require_both_off();
        puts("PASS failed startup persistence inhibits requested FETs"); return 0;
    }
    assert(bms_protection_params_valid());
    bms_afe_hw_profile_t startup_profile;
    assert(bms_afe_hw_profile_get(&startup_profile));
    bms_afe_init(); assert(sh3673510_control_ready());
    request_outputs(); steps(3); require_both_off(); steps(9);
    assert(bms_afe_samples_qualified()); assert(discharge_on());
    assert(g_stCellInfoReport.u16VCellMax==3300 && g_stCellInfoReport.u16VCellMin==3300);
    for (unsigned i=8; i<32; ++i) assert(g_stCellInfoReport.u16VCell[i]==61001);
    if (!strcmp(scenario, "cold-reboot")) {
        assert(g_bms_protection_params.u16IdsgOcp_Third==200);
        assert(g_bms_protection_params.u16IdsgOcp_Filter==40);
        /* Only configuration is durable. Never claim a RAM SW latch survived reset. */
        assert(!bms_sw_protection_discharge_blocked());
        puts("PASS new process reloads committed parameters; cold MCU reset is a separate latch boundary"); return 0;
    }
    bms_protection_params_t p = g_bms_protection_params;
    p.u16IdsgOcp_First=100; p.u16IdsgOcp_Second=150; p.u16IdsgOcp_Third=200;
    p.u16IdsgOcp_Rcv=50; p.u16IdsgOcp_Filter=40;
    assert(bms_protection_params_commit(&p));
    bms_protection_params_t invalid=p;
    invalid.u16IdsgOcp_Rcv=201; assert(!bms_protection_params_commit(&invalid));
    assert(!memcmp(&p, &g_bms_protection_params, sizeof(p)));
    bms_protection_params_t interrupted=p;
    interrupted.u16IdsgOcp_Third=210; flash_cut=24;
    assert(!bms_protection_params_commit(&interrupted)); flash_cut=-1;
    assert(!memcmp(&p, &g_bms_protection_params, sizeof(p)));
    bms_error_clear(BMS_ERROR_EEPROM_STORE);
    puts("PASS real product defaults, parameter validation and failed-save atomic publication");
    trip_discharge(); steps(20);
    if (discharge_on()) { fprintf(stderr, "continuous_load_zero_current expected=DSG_OFF actual=DSG_ON\n"); return 1; }
    puts("PASS persistent-load software OCD remains off after zero current");
    /* AFE re-init and feature init must not erase the same SW fault. */
    reinitialize(); assert(!discharge_on());
    assert(g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp);
    regs[SH3673520_REG_FLAG1] |= SH3673520_FLAG1_RST1_MASK;
    step(); require_both_off(); steps(12); assert(!discharge_on());
    assert(g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp);
    assert(bms_afe_sleep()); require_both_off();
    steps(3); require_both_off(); steps(10); assert(!discharge_on());
    puts("PASS SW OCD survives explicit init, AFE reset flag and Sleep/Wake qualification");
    /* One release sample, reattachment, then a full fresh release window. */
    regs[SH3673520_REG_BSTATUS2]=SH3673520_BSTATUS2_LOADOFF_MASK;
    step(); assert(!discharge_on());
    regs[SH3673520_REG_BSTATUS2]=SH3673520_BSTATUS2_LOADON_MASK;
    step(); assert(!discharge_on());
    regs[SH3673520_REG_BSTATUS2]=SH3673520_BSTATUS2_LOADOFF_MASK;
    step(); assert(!discharge_on());
    ready_bits=SH3673520_FLAG2_VADC_MASK; step(); assert(!discharge_on());
    ready_bits=SH3673520_FLAG2_VADC_MASK|SH3673520_FLAG2_CADC_MASK;
    step(); assert(discharge_on());
    puts("PASS reattachment resets recovery; pending CADC pauses without advancing");
    /* A different fault must keep both directions blocked after OCD releases. */
    trip_discharge(); raw16(0x63, 0); step(); require_both_off();
    release_discharge(); require_both_off();
    raw16(0x63, 16384); steps(3); assert(charge_on() && discharge_on());
    puts("PASS MOS NTC invalid holds both FETs off independently of OCD recovery");
    /* Software charging OC needs fresh C+ removal; zero and hysteresis are insufficient. */
    p.u16IchgOcp_First=100; p.u16IchgOcp_Second=150; p.u16IchgOcp_Third=200;
    p.u16IchgOcp_Rcv=50; p.u16IchgOcp_Filter=40;
    assert(bms_protection_params_commit(&p));
    regs[SH3673520_REG_BSTATUS2]=0; raw16(0x95,1024);
    raw16(0x91,5000); steps(3); assert(!charge_on());
    raw16(0x91,0); steps(20); assert(!charge_on());
    reinitialize(); assert(!charge_on());
    raw16(0x95,384); steps(5); assert(!charge_on()); /* 1.5 V hysteresis */
    raw16(0x95,0); steps(2); assert(charge_on());
    puts("PASS software OCC persists at zero current and C+ hysteresis; fresh removal releases");
    /* Simultaneous software OCD and hardware SC: no premature DSG after SW clears. */
    trip_discharge(); regs[SH3673520_REG_FLAG1]|=SH3673520_FLAG1_SC_MASK;
    step(); release_discharge(); assert(!discharge_on());
    steps(12); assert(discharge_on());
    puts("PASS concurrent software OCD and hardware SC retain independent recovery ownership");
    p.u16IdsgOcp_Filter=200; assert(bms_protection_params_commit(&p));
    regs[SH3673520_REG_BSTATUS2]=SH3673520_BSTATUS2_LOADON_MASK;
    raw16(0x91,(uint16_t)-5000); steps(10); assert(!discharge_on());
    raw16(0x91,0); steps(3); assert(!discharge_on());
    regs[SH3673520_REG_BSTATUS2]=SH3673520_BSTATUS2_LOADOFF_MASK;
    unsigned fresh=0;
    for(unsigned i=0; fresh<10; ++i) {
        ready_bits=(i%5==2) ? 2 : 3; if(ready_bits==3) ++fresh;
        step(); assert(!!g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp==(fresh<10));
    }
    ready_bits=3; assert(discharge_on()); p.u16IdsgOcp_Filter=40;
    assert(bms_protection_params_commit(&p));
    trip_discharge(); steps(2);
    regs[SH3673520_REG_BSTATUS2]=SH3673520_BSTATUS2_LOADOFF_MASK;
    step(); bus_failed=1; step(); bus_failed=0; step();
    assert(g_stCellInfoReport.unMdlFault_Third.bits.b1IdischgOcp);
    steps(6); assert(discharge_on());
    puts("PASS 4 Hz CADC eventually recovers; a communication gap discards the partial window");
    /* Failed reconfiguration readback keeps output authorization inhibited. */
    trip_discharge(); corrupt_read_reg=SH3673520_REG_OVL;
    assert(!bms_afe_apply_protection_config()); assert(!bms_afe_samples_qualified());
    corrupt_read_reg=-1; reinitialize(); assert(!discharge_on()); release_discharge();
    /* A real bus failure must stop bus traffic for the existing watchdog window. */
    trip_discharge(); bus_failed=1; steps(2);
    uint32_t calls=spi_calls; steps(174); assert(spi_calls==calls);
    assert(!bms_afe_bus_access_allowed()); bus_failed=0;
    steps(15); assert(bms_afe_samples_qualified() && !discharge_on());
    release_discharge(); assert(discharge_on());
    puts("PASS readback failure and watchdog silence/re-init cannot clear software OCD");
    trip_discharge(); ready_bits=0; steps(3);
    assert(!bms_afe_samples_qualified()); require_both_off();
    ready_bits=3; reinitialize(); assert(!discharge_on()); release_discharge();
    now_32k=UINT32_MAX-3200u; reinitialize(); assert(charge_on() && discharge_on());
    puts("PASS stale ADC inhibits both outputs; sampling qualification handles tick wrap");
    FILE *f=fopen(image,"wb"); assert(f);
    assert(fwrite(flash,1,sizeof(flash),f)==sizeof(flash)); assert(!fclose(f));
    return 0;
}
