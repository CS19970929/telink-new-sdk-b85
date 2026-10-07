"""Run actual software protection with independent voltage/current and temperature gates."""
import os,re,subprocess,tempfile
from pathlib import Path
from project_paths import Sources, host_includes, selected_source
ROOT=Path(__file__).resolve().parents[1]
MOD = Sources(ROOT)
code = r'''
#include <assert.h>
#include "bms_state.h"
#include "bms_sw_protection.h"
#include "bms_protection_params.h"
#include "bms_error.h"
bms_protection_params_t g_bms_protection_params;
struct stCell_Info g_stCellInfoReport;
static int broken;
void bms_error_clear(bms_error_id_t error){(void)error;broken=0;}
void bms_error_raise(bms_error_id_t error){(void)error;broken=1;}
uint8_t bms_error_get(bms_error_id_t error){(void)error;return (uint8_t)broken;}
uint8_t bms_protection_params_valid(void){return 1;}
void bms_fault_history_record(bms_fault_code_t fault){(void)fault;}
int main(void){
 bms_sw_protection_inputs_t in={.battery_temp_valid=1, .mos_temp_valid=1,
  .battery_temp_min=1000, .battery_temp_max=1000, .mos_temp=1000, .mos_temp_required=1};
 g_bms_protection_params.u16TChgOTp_Third=900;g_bms_protection_params.u16TChgOTp_Rcv=800;
 g_bms_protection_params.u16TmosOTp_Third=900;g_bms_protection_params.u16TmosOTp_Rcv=800;
 g_bms_protection_params.u16IchgOcp_Third=100;g_bms_protection_params.u16IchgOcp_Rcv=50;
 g_bms_protection_params.u16VcellOvp_Third=4000;g_bms_protection_params.u16VcellOvp_Rcv=3900;
 g_stCellInfoReport.u16Ichg=200;g_stCellInfoReport.u16VCellMax=4100;
 for(int vc=0;vc<=1;vc++) for(int temp=0;temp<=1;temp++){
  bms_sw_protection_init();bms_sw_protection_update_groups(&in,vc,temp);
  assert(g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp==vc);
  assert(g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp==vc);
  assert(g_stCellInfoReport.unMdlFault_Third.bits.b1CellChgOtp==temp);
  assert(g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp==temp);
 }
 in.battery_temp_valid=0;bms_sw_protection_update_groups(&in,0,1);assert(broken);
 bms_sw_protection_update_groups(&in,1,0);assert(!broken);
 assert(!g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp);
 in.battery_temp_valid=1;bms_sw_protection_update_groups(&in,0,1);
 assert(!g_stCellInfoReport.unMdlFault_Third.bits.b1IchgOcp);
 assert(g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp);
 // Required MOS NTC: healthy, broken, then overtemperature and recovery.
 in.battery_temp_min=in.battery_temp_max=650;in.mos_temp=650;
 bms_sw_protection_init();bms_sw_protection_update_groups(&in,0,1);
 assert(!broken && !bms_sw_protection_charge_blocked() && !bms_sw_protection_discharge_blocked());
 in.mos_temp_valid=0;bms_sw_protection_update_groups(&in,0,1);
 assert(broken && bms_sw_protection_charge_blocked() && bms_sw_protection_discharge_blocked());
 in.mos_temp_valid=1;in.mos_temp=1000;bms_sw_protection_update_groups(&in,0,1);
 assert(!broken && g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp);
 assert(bms_sw_protection_charge_blocked() && bms_sw_protection_discharge_blocked());
 in.mos_temp=650;bms_sw_protection_update_groups(&in,0,1);
 assert(!bms_sw_protection_charge_blocked() && !bms_sw_protection_discharge_blocked());
 // Unfitted MOS NTC: invalid or stale-hot values never participate.
 in.mos_temp_required=0;in.mos_temp_valid=0;
 bms_sw_protection_update_groups(&in,0,1);assert(!broken);
 in.mos_temp_valid=1;in.mos_temp=1600;
 bms_sw_protection_update_groups(&in,0,1);
 assert(!broken && !g_stCellInfoReport.unMdlFault_Third.bits.b1TmosOtp);
 assert(!bms_sw_protection_charge_blocked() && !bms_sw_protection_discharge_blocked());
 // HW-only report flags must not masquerade as a software blocking reason.
 bms_sw_protection_clear();
 g_stCellInfoReport.unMdlFault_Third.bits.b1CellOvp=1;
 g_stCellInfoReport.unMdlFault_Third.bits.b1CellUvp=1;
 assert(!bms_sw_protection_charge_blocked() && !bms_sw_protection_discharge_blocked());
 // SW stays blocked even if a downstream report consumer modifies merged bits.
 in.battery_temp_valid=1;in.mos_temp_required=1;in.mos_temp_valid=1;in.mos_temp=1000;
 bms_sw_protection_update_groups(&in,0,1);
 g_stCellInfoReport.unMdlFault_Third.all=0;
 assert(bms_sw_protection_charge_blocked() && bms_sw_protection_discharge_blocked());
 in.mos_temp=650;bms_sw_protection_update_groups(&in,0,1);
 assert(!bms_sw_protection_charge_blocked() && !bms_sw_protection_discharge_blocked());
 in.battery_temp_valid=0;bms_sw_protection_update_groups(&in,0,1);
 assert(broken && bms_sw_protection_charge_blocked() && bms_sw_protection_discharge_blocked());
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='d008-temp-groups-') as folder:
 p=Path(folder)/'check.c';p.write_text(code);exe=Path(folder)/'check.exe'
 for opt in ('-O2','-Os'):
  subprocess.run([os.environ.get('CC','cc'),opt,'-std=c99','-Wall','-Wextra','-Werror',*host_includes(ROOT),str(p),str(MOD/'bms_sw_protection.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
print('PASS independent SW VC/TEMP groups, disabled state reset and NTC failure gate')
