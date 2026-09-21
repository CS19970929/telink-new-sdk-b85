"""Run actual software protection with independent voltage/current and temperature gates."""
import os,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MOD=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
source=(MOD/'bms_sw_protection.c').read_text(encoding='utf-8')
source=re.sub(r'^#include[^\n]*','',source,flags=re.M)
params=re.search(r'struct PRT_E2ROM_PARAS \{.*?\n\};',(MOD/'param.h').read_text(encoding='utf-8'),re.S).group(0)
inputs=re.search(r'typedef struct\s*\{.*?bms_sw_protection_inputs_t;',(MOD/'bms_sw_protection.h').read_text(),re.S).group(0)
code = '#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n#include "bms_state.h"\ntypedef uint16_t u16;\n'+params+inputs+"""
struct { struct PRT_E2ROM_PARAS protect; } g_tParam;
struct stCell_Info g_stCellInfoReport;
#define BMS_ERROR_TEMP_BREAK 1
static int broken;
static void bms_error_clear(int x){(void)x;broken=0;}
static void bms_error_raise(int x){(void)x;broken=1;}
static int bms_error_get(int x){(void)x;return broken;}
static int bms_protection_params_valid(void){return 1;}
void bms_fault_history_record(bms_fault_code_t x){(void)x;}
void bms_sw_protection_update_groups(const bms_sw_protection_inputs_t*,uint8_t,uint8_t);
"""+source+"""
int main(void){
 bms_sw_protection_inputs_t in={1,1,1000,1000,1000};
 g_tParam.protect.u16TChgOTp_Third=900;g_tParam.protect.u16TChgOTp_Rcv=800;
 g_tParam.protect.u16TmosOTp_Third=900;g_tParam.protect.u16TmosOTp_Rcv=800;
 g_tParam.protect.u16IchgOcp_Third=100;g_tParam.protect.u16IchgOcp_Rcv=50;
 g_tParam.protect.u16VcellOvp_Third=4000;g_tParam.protect.u16VcellOvp_Rcv=3900;
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
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='d008-temp-groups-') as folder:
 p=Path(folder)/'check.c';p.write_text(code);exe=Path(folder)/'check.exe'
 subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror','-I',str(MOD),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS independent SW VC/TEMP groups, disabled state reset and NTC failure gate')
