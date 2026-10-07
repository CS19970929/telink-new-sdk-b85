/* Reuse only the test wire/Flash/time peer and fixtures; production TUs link unchanged. */
#define main previous_loop_scenarios
#include "tests/fixtures/d014_safety_loop/loop.c"
#undef main
#include "bms_event_log.h"

static void flash_image(const char *path, int write_image)
{
    FILE *f=fopen(path,write_image ? "wb" : "rb"); assert(f);
    if (write_image) assert(fwrite(flash,1,sizeof(flash),f)==sizeof(flash));
    else {
        assert(fread(flash,1,sizeof(flash),f)==sizeof(flash));
        assert(fgetc(f)==EOF);
    }
    assert(!fclose(f));
}

int main(void)
{
    const char *stage=getenv("BMS_RESET_STAGE");
    const char *path=getenv("BMS_RESET_IMAGE");
    const char *direction=getenv("BMS_RESET_DIRECTION");
    const char *history=getenv("BMS_RESET_HISTORY");
    assert(stage && path && direction && history);
    assert(!strcmp(direction,"charge") || !strcmp(direction,"discharge"));
    assert(!strcmp(history,"pending") || !strcmp(history,"durable"));
    int charge=!strcmp(direction,"charge");
    int durable=!strcmp(history,"durable");
    int boot=!strcmp(stage,"boot");
    assert(boot || !strcmp(stage,"seed"));
    memset(flash,255,sizeof(flash));
    if (boot) flash_image(path,0);
    for(unsigned i=0;i<20;++i) raw16((uint8_t)(0x69+2*i),21120);
    for(unsigned i=0;i<4;++i) raw16((uint8_t)(0x5d+2*i),16384);
    raw16(0x93,6758);
    regs[SH3673520_REG_BSTATUS2]=charge ? 0 : SH3673520_BSTATUS2_LOADON_MASK;
    raw16(0x95,charge ? 1024 : 0); /* Connected charger at 4 V C+ in this model. */
    bms_diag_init(); bms_parameters_startup(); bms_parameters_init();
    assert(bms_protection_params_valid());
    bms_afe_hw_profile_t startup_profile;
    assert(bms_afe_hw_profile_get(&startup_profile));
    if (!boot) {
        bms_protection_params_t p=g_bms_protection_params;
        p.u16IchgOcp_First=p.u16IdsgOcp_First=100;
        p.u16IchgOcp_Second=p.u16IdsgOcp_Second=150;
        p.u16IchgOcp_Third=p.u16IdsgOcp_Third=200;
        p.u16IchgOcp_Rcv=p.u16IdsgOcp_Rcv=50;
        p.u16IchgOcp_Filter=p.u16IdsgOcp_Filter=40;
        assert(bms_protection_params_commit(&p));
    }
    bms_afe_init(); request_outputs(); require_both_off();
    if (!boot) {
        steps(12); assert(charge_on() && discharge_on());
        raw16(0x91,charge ? 5000 : (uint16_t)-5000); steps(2);
        assert(charge ? bms_sw_protection_charge_blocked() : bms_sw_protection_discharge_blocked());
        raw16(0x91,0); steps(12);
        assert(!(charge ? charge_on() : discharge_on()));
        bms_event_log_sample_t event={0};
        event.chg_ocp=charge; event.dsg_ocp=!charge;
        bms_event_log_poll_1s(&event);
        if (durable) {
            now_32k+=60u*32000u;
            bms_event_log_poll_1s(&event);
        }
        assert((bms_event_log_read_reg(0)>>8)==(charge ? CHG_OCP : DSG_OCP));
        flash_image(path,1);
        printf("SEED direction=%s history=%s active_oc=1 fet_command=OFF\n",direction,history);
        return 0;
    }
    /* This is a fresh OS process: no production static RAM was manually reset. */
    int recorded=(bms_event_log_read_reg(0)>>8)==(charge ? CHG_OCP : DSG_OCP);
    assert(recorded==durable);
    assert(g_bms_protection_params.u16IdsgOcp_Third==200 && g_bms_protection_params.u16IchgOcp_Third==200);
    unsigned first_on=0;
    for(unsigned i=1;i<=12;++i) {
        step();
        if (!first_on && (charge ? charge_on() : discharge_on())) first_on=i;
    }
    printf("RESET_OBSERVATION direction=%s history=%s recorded=%d first_on_sample=%u current_a10=%u\n",
           direction,history,recorded,first_on,
           charge ? g_bms_report.u16Ichg : g_bms_report.u16IDischg);
    if (first_on) {
        fprintf(stderr,"RESET_POLICY_GAP direction=%s history=%s expected=OFF_UNTIL_PHYSICAL_RELEASE actual=ON\n",direction,history);
        return 1;
    }
    puts("RESET_POLICY_GATE_PASS output remained off with no release evidence");
    return 0;
}
