"""Preprocess real release configuration: positive control plus forbidden options."""
from pathlib import Path
from project_paths import Sources, host_includes, selected_source
import subprocess,tempfile,importlib.util,os,shlex,re
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
 p.write_text('#include "bms_diag.h"\n#include "app_config.h"\n#include "'+('sh3673510_project_config.h' if sh else 'bms_product.h')+'"\n')
 base=[*shlex.split(os.environ.get('CC', 'cc')),'-E','-x','c','-I'+str(SDK),*host_includes(ROOT),'-D__PROJECT_8258_BLE_SAMPLE__=1','-DCHIP_TYPE=CHIP_TYPE_825x','-DBMS_PRODUCTION_BUILD=1']
 if not sh:base+=['-DD008_PRODUCT_PROFILE=3']
 cases=[([],True),(['-DBMS_DEBUG_LOG_ENABLE=1'],False),(['-DBMS_DIAG_TRACE_ENABLE=1'],False),(['-DBMS_HOST_TEST=1'],False),(['-DDEBUG_GPIO_ENABLE=1'],False),(['-D__TEST_SOC__=1'],False),(['-DTEST_CONN_CURRENT_ENABLE=1'],False),(['-DBMS_DIAG_BUILD_DIRTY=1'],False),(['-DBMS_DIAG_BUILD_ID=0'],False),(['-D'+prefix+'_SW_PROTECT_ENABLE=0'],False),(['-D'+prefix+'_HW_PROTECT_ENABLE=0'],False)]
 if sh:cases.append((['-DBMS_BOARD_DEBUG_LED_ENABLE=1'],False))
 for extra,ok in cases:
  flags=[]
  if sh and not any('BMS_BOARD_DEBUG_LED_ENABLE=' in x for x in extra):
   flags+=['-DBMS_BOARD_DEBUG_LED_ENABLE=0']
  if not any('BMS_DIAG_BUILD_ID=' in x for x in extra):flags+=['-DBMS_DIAG_BUILD_ID=1']
  if not any('BMS_DIAG_BUILD_DIRTY=' in x for x in extra):flags+=['-DBMS_DIAG_BUILD_DIRTY=0']
  r=subprocess.run(base+flags+extra+[str(p)],capture_output=True,text=True)
  assert (r.returncode==0)==ok,(extra,r.stderr)
print('PASS release positive control and forbidden debug/test/dirty/identity/protection combinations')

# 正负控制在树外夹具中显式设置批准值，不依赖当前产品是否已批准。
# 夹具不会用于生成生产镜像。
with tempfile.TemporaryDirectory(prefix='bms-approval-') as tmp:
 folder=Path(tmp)
 real=(ROOT/'bms/products'/bms.PRODUCT/'bms_product.h').read_text(encoding='utf-8')
 src='#include "bms_release_approval.h"\n'
 command=[*shlex.split(os.environ.get('CC','cc')),'-E','-x','c','-I',str(folder),
          *host_includes(ROOT),'-DBMS_PRODUCTION_BUILD=1','-']
 profiles=(1,2,3) if not sh else (0,)
 for profile in profiles:
  options=['-DD008_PRODUCT_PROFILE='+str(profile)] if not sh else []
  for approved in (False,True):
   fixture=re.sub(r'(?m)^(#define\s+\w+_APPROVED)\s+[01]\b',lambda m:m.group(1)+' '+str(int(approved)),real)
   (folder/'bms_product.h').write_text(fixture,encoding='utf-8')
   result=subprocess.run(command[:-1]+options+command[-1:],input=src,capture_output=True,text=True)
   expected=approved
   assert (result.returncode==0)==expected,(profile,approved,result.stderr)
print('PASS actual release approval gate: pending rejection and isolated signed positive controls')

if not sh:
 with tempfile.TemporaryDirectory(prefix='bms-nmc-approval-') as tmp:
  folder=Path(tmp)
  signed_scd=real.replace('BMS_D008_SCD_POLICY_APPROVED 0','BMS_D008_SCD_POLICY_APPROVED 1').replace('BMS_PRODUCT_RELEASE_APPROVED 0','BMS_PRODUCT_RELEASE_APPROVED 1')
  (folder/'bms_product.h').write_text(signed_scd,encoding='utf-8')
  for profile in (1,2,3):
   command=[*shlex.split(os.environ.get('CC','cc')),'-E','-x','c','-I',str(folder),*host_includes(ROOT),'-DD008_PRODUCT_PROFILE='+str(profile),'-']
   result=subprocess.run(command,input=src,capture_output=True,text=True)
   assert (result.returncode==0)==(profile!=2),(profile,result.stderr)
 print('PASS NMC voltage approval remains independent of signed SCD policy')
