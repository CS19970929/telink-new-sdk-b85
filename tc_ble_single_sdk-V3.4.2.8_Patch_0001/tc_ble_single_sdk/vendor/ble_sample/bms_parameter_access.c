#include "bms_parameter_access.h"
#include "bms_config_store.h"
#include "bms_state_store.h"
#include "bms_afe_hw_access.h"
#include "bms_features.h"
#include "dvc1124.h"
#include "drivers.h"
#include "runtime.h"
#include "modbus_rtu.h"
#include <string.h>
extern void WriteProID_Default(void);

static u16 s_sequence, s_result, s_sn_generation, s_sn_mask;
static u32 s_sn_tick;
static char s_sn_stage[32];
static u8 s_sn_active;

static u16 word(const u8 *p) { return ((u16)p[0] << 8) | p[1]; }
static u32 dword(const u8 *p) { return (u32)word(p) | ((u32)word(p+2) << 16); }
static u8 finish(u8 result) { s_result=result; if (!result) ++s_sequence; return result; }

static u8 sensitive_factory_write_allowed(void)
{
    if (!bms_afe_hw_access_is_active()) return 0u;
#if BMS_PRODUCTION_BUILD
    return (Runtime_GetMode() == MODE_FACTORY) ? 1u : 0u;
#else
    return 1u;
#endif
}

int bms_parameter_readable(u16 r)
{
    return (r>=0x2E00u && r<=0x2E0Bu) ||
           (r>=0x2E20u && r<=0x2E22u) ||
           (r>=0x2E24u && r<=0x2E2Eu) ||
           (r>=0x2E30u && r<=0x2E3Fu) || r==0x1005u || r==0x2318u || r==0x2319u;
}

u16 bms_parameter_read(u16 r)
{
    bms_user_params_t v;
    bms_config_system_params_t system;
    dvc1124_snapshot_t sample;
    if (r==0x2E00u) return 0xD008u;
    if (r==0x2E01u) return 1u;
    if (r==0x2E02u) return 0x003Fu; /* capacity,SN,heat,calibration,reset,sync State */
    if (r==0x2E03u) return s_result;
    if (r==0x2E04u) return s_sequence;
    if (r==0x2E05u) return 3u; /* Config schema */
    if (r==0x2E06u) return s_sn_generation;
    if (r==0x2E07u) return 1u; /* 1102=3 is forbidden; factory reset uses 2E10=6 */
    if (r==0x2E08u) return CapacityFactory;
    if (r==0x2E09u) return 1u;
    if (r==0x2E0Au) return BMS_HEATER_START_TEMP_X10;
    if (r==0x2E0Bu) return BMS_HEATER_STOP_TEMP_X10;
    if (r==0x1005u) return get_soc_real();
    if (r==0x2319u) return (u16)SOC_Calculate_Element.u32Cycle_times;
    if (r==0x2318u) return bms_config_store_get_system(&system) ? (u16)system.capacity_factory : 0xFFFFu;
    if (r>=0x2E28u && r<=0x2E2Eu) {
        DVC1124_GetSnapshot(&sample);
        if (r==0x2E2Cu) return sample.valid &&
            (u32)(pm_get_32k_tick()-sample.sample_tick_32k)<=BMS_SOC_MAX_SAMPLE_GAP_32K;
        if (r==0x2E2Du) return (u16)sample.sample_tick_32k;
        if (r==0x2E2Eu) return (u16)(sample.sample_tick_32k>>16);
        if (r==0x2E28u) return (u16)sample.raw_current_ma;
        if (r==0x2E29u) return (u16)((u32)sample.raw_current_ma>>16);
        if (r==0x2E2Au) return (u16)sample.current_ma;
        return (u16)((u32)sample.current_ma>>16);
    }
    if (!bms_config_get_user(&v)) return 0xFFFFu;
    if (r==0x2E20u) return v.heater_enable;
    if (r==0x2E21u) return v.heater_start_x10;
    if (r==0x2E22u) return v.heater_stop_x10;
    if (r==0x2E24u) return (u16)v.current_offset_ma;
    if (r==0x2E25u) return (u16)((u32)v.current_offset_ma>>16);
    if (r==0x2E26u) return (u16)v.current_gain_ppm;
    if (r==0x2E27u) return (u16)(v.current_gain_ppm>>16);
    if (r>=0x2E30u && r<=0x2E3Fu) {
        u16 i=(u16)((r-0x2E30u)*2u);
        return ((u16)(u8)v.serial[i]<<8) | (u8)v.serial[i+1u];
    }
    return 0u;
}

u8 bms_parameter_write(u16 r, u16 qty, const u8 *data)
{
    bms_user_params_t v;
    bms_config_system_params_t system;
    u16 value, i;
    if (!data || !qty) return finish(3u);
    value=word(data);
    if (r==0x1005u || r==0x2319u) {
        u16 soc=(r==0x1005u) ? value : get_soc_real();
        u32 cycle=(r==0x2319u) ? value : SOC_Calculate_Element.u32Cycle_times;
        if (qty!=1u || soc>100u) return finish(3u);
        if (!bms_state_store_set_soc_cycle(soc, SOC_Calculate_Element.u8DSG_SOC_Int, cycle)) return finish(4u);
        SOC_Calculate_Element.u32Cycle_times=cycle;
        set_soc_param((u8)soc,0u,1u);
        return finish(0u);
    }
    if (r==0x2318u) {
        if (qty!=1u || !value || value>BMS_SOC_CAPACITY_MAX_0P1AH) return finish(3u);
        if (!bms_config_store_get_system(&system)) return finish(4u);
        if (system.capacity_factory==value) return finish(0u);
        system.capacity_factory=value;
        if (!bms_config_store_set_system(&system)) return finish(4u);
        bms_soc_nominal_capacity_changed();
        return finish(0u);
    }
    if (r==0x2E10u) {
        u8 result;
        if (qty!=1u) return finish(3u);
        if (value==1u) return finish(bms_reset_software_parameters());
        if (value==2u) return finish(bms_reset_afe_parameters());
        if (value==3u) {
            if (!bms_config_reset_business()) return finish(4u);
            bms_soc_nominal_capacity_changed(); return finish(0u);
        }
        if (value==6u) {
            if (!bms_afe_hw_access_is_active()) return finish(2u);
            result=Runtime_ReenterFactoryMode() ? 0u : 4u;
            return finish(result);
        }
        return finish(3u);
    }
    if (!bms_config_get_user(&v)) return finish(4u);
    if (r==0x2E20u) {
        if (qty!=3u) return finish(3u);
        v.heater_enable=value; v.heater_start_x10=word(data+2); v.heater_stop_x10=word(data+4);
    } else if (r==0x2E24u) {
        if (qty!=4u) return finish(3u);
        if (!sensitive_factory_write_allowed()) return finish(2u);
        v.current_offset_ma=(int32_t)dword(data); v.current_gain_ppm=dword(data+4);
    } else if (r==0x2E40u) {
        if (qty!=1u || !sensitive_factory_write_allowed()) return finish(2u);
        if (value==0u) {
            if (++s_sn_generation==0u) ++s_sn_generation;
            s_sn_mask=0u; s_sn_active=1u; s_sn_tick=pm_get_32k_tick();
            memset(s_sn_stage,0,sizeof(s_sn_stage)); return finish(0u);
        }
        if (!s_sn_active || value!=s_sn_generation || s_sn_mask!=0xFFFFu ||
            (u32)(pm_get_32k_tick()-s_sn_tick)>60u*32000u) return finish(3u);
        s_sn_active=0u;
        memcpy(v.serial,s_sn_stage,sizeof(v.serial));
    } else if (r>=0x2E50u && r<=0x2E5Fu) {
        if (!sensitive_factory_write_allowed()) return finish(2u);
        if (!s_sn_active || (u32)(pm_get_32k_tick()-s_sn_tick)>60u*32000u ||
            qty>4u || (u32)r+qty>0x2E60u) return finish(3u);
        for (i=0u;i<qty;++i) {
            u16 slot=(u16)(r-0x2E50u+i);
            s_sn_stage[slot*2u]=(char)data[i*2u]; s_sn_stage[slot*2u+1u]=(char)data[i*2u+1u];
            s_sn_mask|=(u16)(1u<<slot);
        }
        return finish(0u); /* staging only, no Flash */
    } else return finish(2u);
    if (!bms_config_user_valid(&v)) return finish(3u);
    if (!bms_config_set_user(&v)) return finish(4u);
    if (r==0x2E40u) WriteProID_Default();
    return finish(0u);
}
