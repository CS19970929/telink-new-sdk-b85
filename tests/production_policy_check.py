"""Preprocess real release configuration: positive control plus forbidden options."""
from pathlib import Path
from project_paths import Sources, host_includes, selected_source
import subprocess,tempfile,importlib.util,os,shlex
ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk'
APP = Sources(ROOT)
spec=importlib.util.spec_from_file_location('bms_policy',ROOT/'bms_tools/bms.py')
bms=importlib.util.module_from_spec(spec);spec.loader.exec_module(bms)
backend=(APP/'bms_afe_backend.h').read_text(encoding='utf-8')
sh=bms.PRODUCT != "d008"
prefix='SH3673510' if sh else 'DVC1124'
with tempfile.TemporaryDirectory(prefix='bms-production-') as d:
 p=Path(d)/'policy.c'
 p.write_text('#include "app_config.h"\n#include "'+('sh3673510_project_config.h' if sh else 'dvc1124_project_config.h')+'"\n')
 base=[*shlex.split(os.environ.get('CC', 'cc')),'-E','-x','c','-I'+str(SDK),*host_includes(ROOT),'-D__PROJECT_8258_BLE_SAMPLE__=1','-DCHIP_TYPE=CHIP_TYPE_825x','-DBMS_PRODUCTION_BUILD=1']
 if not sh:base+=['-DD008_PRODUCT_PROFILE=3']
 cases=[([],True),(['-DBMS_DEBUG_LOG_ENABLE=1'],False),(['-DDEBUG_GPIO_ENABLE=1'],False),(['-D__TEST_SOC__=1'],False),(['-DTEST_CONN_CURRENT_ENABLE=1'],False),(['-DBMS_DIAG_BUILD_DIRTY=1'],False),(['-DBMS_DIAG_BUILD_ID=0'],False),(['-D'+prefix+'_SW_PROTECT_ENABLE=0'],False),(['-D'+prefix+'_HW_PROTECT_ENABLE=0'],False)]
 if sh:cases.append((['-DBMS_BOARD_DEBUG_LED_ENABLE=1'],False))
 for extra,ok in cases:
  flags=[]
  if not any('BMS_DIAG_BUILD_ID=' in x for x in extra):flags+=['-DBMS_DIAG_BUILD_ID=1']
  if not any('BMS_DIAG_BUILD_DIRTY=' in x for x in extra):flags+=['-DBMS_DIAG_BUILD_DIRTY=0']
  r=subprocess.run(base+flags+extra+[str(p)],capture_output=True,text=True)
  assert (r.returncode==0)==ok,(extra,r.stderr)
print('PASS release positive control and forbidden debug/test/dirty/identity/protection combinations')
