"""Preprocess real release configuration: positive control plus forbidden options."""
from pathlib import Path
import subprocess,tempfile,importlib.util
ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk'
APP=SDK/'vendor/ble_sample'
spec=importlib.util.spec_from_file_location('bms_policy',ROOT/'bms_tools/bms.py')
bms=importlib.util.module_from_spec(spec);spec.loader.exec_module(bms)
backend=(APP/'bms_afe_backend.h').read_text()
sh='#define BMS_AFE_BACKEND BMS_AFE_BACKEND_SH3673510' in backend
prefix='SH3673510' if sh else 'DVC1124'
with tempfile.TemporaryDirectory(prefix='bms-production-') as d:
 p=Path(d)/'policy.c'
 p.write_text('#include "app_config.h"\n#include "'+('sh3673510_project_config.h' if sh else 'dvc1124_project_config.h')+'"\n')
 base=[bms._tc32_tool('tc32-elf-gcc'),'-E','-x','c','-I'+str(SDK),'-I'+str(APP),'-D__PROJECT_8258_BLE_SAMPLE__=1','-DCHIP_TYPE=CHIP_TYPE_825x','-DBMS_PRODUCTION_BUILD=1']
 cases=[([],True),(['-DDEBUG_GPIO_ENABLE=1'],False),(['-D__TEST_SOC__=1'],False),(['-DTEST_CONN_CURRENT_ENABLE=1'],False),(['-DBMS_DIAG_BUILD_DIRTY=1'],False),(['-DBMS_DIAG_BUILD_ID=0'],False),(['-D'+prefix+'_SW_PROTECT_ENABLE=0'],False),(['-D'+prefix+'_HW_PROTECT_ENABLE=0'],False)]
 if sh:cases.append((['-DD011_DEBUG_LED_ENABLE=1'],False))
 for extra,ok in cases:
  flags=[]
  if not any('BMS_DIAG_BUILD_ID=' in x for x in extra):flags+=['-DBMS_DIAG_BUILD_ID=1']
  if not any('BMS_DIAG_BUILD_DIRTY=' in x for x in extra):flags+=['-DBMS_DIAG_BUILD_DIRTY=0']
  r=subprocess.run(base+flags+extra+[str(p)],capture_output=True,text=True)
  assert (r.returncode==0)==ok,(extra,r.stderr)
print('PASS release positive control and forbidden debug/test/dirty/identity/protection combinations')
