/* 仅场景、断言与输入；实际滤波、故障状态、阻断、错误所有者直接链接生产 .c。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "bms_state.h"
#include "bms_error.h"
#include "bms_sw_protection.h"

PARAM_T g_tParam;
static uint8_t params_valid = 1;
uint8_t bms_protection_params_valid(void) { return params_valid; }
static bms_sw_protection_inputs_t input;
static unsigned group, level, step, cases;
static const char *phase;
static uint32_t digest = 2166136261u;

static void expect(unsigned actual, unsigned expected)
{
    ++cases;
    digest = (digest ^ actual) * 16777619u;
    if (actual != expected) {
        fprintf(stderr, "保护失败 group=%u level=%u phase=%s sample=%u expected=%u actual=%u\n",
                group, level + 1u, phase, step, expected, actual);
        exit(1);
    }
}

typedef struct { size_t fields[5]; uint8_t low, charge_block, discharge_block; } rule_t;
#define RULE(n,l,c,d) {{offsetof(struct PRT_E2ROM_PARAS,n##_First), offsetof(struct PRT_E2ROM_PARAS,n##_Second), offsetof(struct PRT_E2ROM_PARAS,n##_Third), offsetof(struct PRT_E2ROM_PARAS,n##_Rcv), offsetof(struct PRT_E2ROM_PARAS,n##_Filter)},l,c,d}
static const rule_t rules[] = {
    RULE(u16VcellOvp,0,1,0), RULE(u16VcellUvp,1,0,1),
    RULE(u16VbusOvp,0,1,0), RULE(u16VbusUvp,1,0,1),
    RULE(u16IchgOcp,0,1,0), RULE(u16IdsgOcp,0,0,1),
    RULE(u16TChgOTp,0,1,0), RULE(u16TchgUTp,1,1,0),
    RULE(u16TdischgOTp,0,0,1), RULE(u16TdischgUTp,1,0,1),
    RULE(u16TmosOTp,0,1,1), RULE(u16VdeltaOvp,0,0,0)
};
#undef RULE
static void parameter(unsigned g, unsigned f, uint16_t value)
{
    memcpy((unsigned char *)&g_tParam.protect + rules[g].fields[f], &value, sizeof(value));
}
static void measurement(unsigned g, uint16_t value)
{
    switch (g) {
    case 0: g_stCellInfoReport.u16VCellMax=value; break;
    case 1: g_stCellInfoReport.u16VCellMin=value; break;
    case 2: case 3: g_stCellInfoReport.u16VCellTotle=value; break;
    case 4: g_stCellInfoReport.u16Ichg=value; break;
    case 5: g_stCellInfoReport.u16IDischg=value; break;
    case 6: case 8: input.battery_temp_max=value; break;
    case 7: case 9: input.battery_temp_min=value; break;
    case 10: input.mos_temp=value; break;
    default: g_stCellInfoReport.u16VCellDelta=value; break;
    }
}
static unsigned active(unsigned g, unsigned l)
{
    const bms_fault_reg_t *f = l==0 ? &g_stCellInfoReport.unMdlFault_First :
        (l==1 ? &g_stCellInfoReport.unMdlFault_Second : &g_stCellInfoReport.unMdlFault_Third);
    switch(g) {
    case 0: return f->bits.b1CellOvp; case 1: return f->bits.b1CellUvp;
    case 2: return f->bits.b1BatOvp; case 3: return f->bits.b1BatUvp;
    case 4: return f->bits.b1IchgOcp; case 5: return f->bits.b1IdischgOcp;
    case 6: return f->bits.b1CellChgOtp; case 7: return f->bits.b1CellChgUtp;
    case 8: return f->bits.b1CellDischgOtp; case 9: return f->bits.b1CellDischgUtp;
    case 10: return f->bits.b1TmosOtp; default: return f->bits.b1VcellDeltaBig;
    }
}
static void reset(void)
{
    memset(&g_tParam,0,sizeof(g_tParam));
    memset(&g_stCellInfoReport,0,sizeof(g_stCellInfoReport));
    input.battery_temp_valid=input.mos_temp_valid=input.mos_temp_required=1;
    input.battery_temp_min=input.battery_temp_max=input.mos_temp=500;
    g_stCellInfoReport.u16Ichg=g_stCellInfoReport.u16IDischg=1;
    params_valid=1; step=0;
    bms_sw_protection_init();
}
static void update(void) { ++step; bms_sw_protection_update(&input); }

int main(void)
{
    static const uint16_t filters[]={0,1,19,20,21,40,100,65535};
    for(group=0;group<12;group++) for(level=0;level<3;level++) {
        for(unsigned k=0;k<sizeof(filters)/sizeof(filters[0]);k++) {
            unsigned count=filters[k] ? ((unsigned)filters[k]+19u)/20u : 1u;
            uint16_t safe=rules[group].low ? 501 : 499;
            uint16_t recover=rules[group].low ? 600 : 400;
            reset(); parameter(group,level,500); parameter(group,3,recover);
            parameter(group,4,filters[k]);
            phase="阈值外侧"; measurement(group,safe);
            for(unsigned n=0;n<count+1;n++){update();expect(active(group,level),0);}
            phase="阈值相等累计"; measurement(group,500);
            for(unsigned n=1;n<=count;n++){update();expect(active(group,level),n==count);}
            phase="阻断方向";
            expect(bms_sw_protection_charge_blocked(),level==2 && rules[group].charge_block);
            expect(bms_sw_protection_discharge_blocked(),level==2 && rules[group].discharge_block);
            phase="恢复边界内侧";
            measurement(group,level==2 ? (rules[group].low ? recover-1 : recover+1) : 500);
            for(unsigned n=0;n<count+1;n++){update();expect(active(group,level),1);}
            phase="连续恢复"; measurement(group,level==2 ? recover : safe);
            for(unsigned n=1;n<=count;n++){update();expect(active(group,level),n!=count);}
            phase="禁用阈值"; parameter(group,level,0);measurement(group,500);update();expect(active(group,level),0);
        }
        reset();parameter(group,level,500);parameter(group,3,rules[group].low?600:400);parameter(group,4,60);
        phase="累计触发允许一个正常样本递减";
        measurement(group,500);update();update();
        measurement(group,rules[group].low?501:499);update();
        measurement(group,500);update();expect(active(group,level),0);update();expect(active(group,level),1);
        phase="恢复必须连续";measurement(group,rules[group].low?600:400);update();update();
        measurement(group,500);update();measurement(group,rules[group].low?600:400);
        update();update();expect(active(group,level),1);update();expect(active(group,level),0);
    }
    /* 排除共享同一个输入且互斥的 pack OV/UV；其他十组可独立施加阈值。 */
    static const unsigned independent[]={0,1,4,5,6,7,8,9,10,11};
    for(unsigned mask=0;mask<1024;mask++) {
        unsigned charge=0,discharge=0;
        reset();phase="多故障组合";level=2;
        for(unsigned b=0;b<10;b++) {
            group=independent[b];parameter(group,2,(mask&(1u<<b))?500:0);
            parameter(group,3,rules[group].low?600:400);measurement(group,500);
            if(mask&(1u<<b)){charge|=rules[group].charge_block;discharge|=rules[group].discharge_block;}
        }
        update();expect(bms_sw_protection_charge_blocked(),charge);expect(bms_sw_protection_discharge_blocked(),discharge);
        for(unsigned b=0;b<10;b++){group=independent[b];expect(active(group,2),!!(mask&(1u<<b)));}
        /* 分次移除参数，只要另一方向故障尚在就不能误恢复。 */
        for(unsigned b=0;b<10;b++) {
            group=independent[b];parameter(group,2,0);update();charge=discharge=0;
            for(unsigned j=b+1;j<10;j++) if(mask&(1u<<j)){charge|=rules[independent[j]].charge_block;discharge|=rules[independent[j]].discharge_block;}
            expect(bms_sw_protection_charge_blocked(),charge);expect(bms_sw_protection_discharge_blocked(),discharge);
        }
    }
    reset();phase="NTC 失效";input.battery_temp_valid=0;update();
    expect(bms_sw_protection_charge_blocked(),1);expect(bms_sw_protection_discharge_blocked(),1);
    input.battery_temp_valid=1;input.mos_temp_valid=0;update();expect(bms_sw_protection_charge_blocked(),1);
    input.mos_temp_required=0;update();expect(bms_sw_protection_charge_blocked(),0);
    printf("assertions=%u digest=%08lx\n",cases,(unsigned long)digest);
    return 0;
}
