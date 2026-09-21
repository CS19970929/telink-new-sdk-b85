"""Real fingerprint tests: headers, flags, tool bytes and unchanged mtime."""
from pathlib import Path
import tempfile,importlib.util
from unittest import mock
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('receipt_bms',ROOT/'bms_tools/bms.py')
bms=importlib.util.module_from_spec(spec);spec.loader.exec_module(bms)
with tempfile.TemporaryDirectory(prefix='bms-receipt-') as tmp:
 d=Path(tmp);sdk=d/'sdk';sdk.mkdir();here=d/'bms_tools';here.mkdir();gen=d/'gen';gen.mkdir()
 for p in (sdk/'app.c',sdk/'config.h',sdk/'boot.inc',sdk/'boot.link',sdk/'vendor.a',here/'source_order.txt',here/'build.mk',here/'bms.py',d/'tool.exe',d/'firmware.elf',d/'firmware.map',d/'firmware.lst',d/'firmware.raw'):
  p.write_text('fixture')
 with mock.patch.multiple(bms,REPO_ROOT=d,SDK_DIR=sdk,_HERE=here,GEN_DIR=gen,ELF=d/'firmware.elf',MAP=d/'firmware.map',LST=d/'firmware.lst',RAW_BIN=d/'firmware.raw',SOURCE_ORDER_FILE=here/'source_order.txt',LINKER_FILE=sdk/'boot.link',REQUIRED_VENDOR_LIBS=(sdk/'vendor.a',),TL_CHECK_FW2=d/'tool.exe'),mock.patch.object(bms,'_load_source_order_strict',return_value=['app.c']),mock.patch.object(bms,'_tc32_tool',return_value=str(d/'tool.exe')),mock.patch.object(bms,'_firmware_git_build_id',return_value='0x12345678u'),mock.patch.object(bms,'_firmware_git_dirty',return_value=0):
  bms._write_compile_inputs('-DMODE=1')
  bms._mark_build_complete()
  path=gen/'compile-inputs.json';stamp=path.stat().st_mtime_ns
  receipt=bms._read_compile_inputs()
  bms._write_compile_inputs('-DMODE=1');assert path.stat().st_mtime_ns==stamp
  for target in (sdk/'config.h',sdk/'boot.inc',d/'tool.exe',d/'firmware.elf',d/'firmware.map',d/'firmware.lst',d/'firmware.raw'):
   old=target.read_bytes();target.write_bytes(b'changed')
   try:bms._read_compile_inputs()
   except SystemExit:pass
   else:raise AssertionError('stale input accepted: '+str(target))
   target.write_bytes(old)
  assert bms._read_compile_inputs()==receipt
  bms._write_compile_inputs('-DMODE=0')
  try:bms._read_compile_inputs()
  except SystemExit:pass
  else:raise AssertionError('new inputs blessed stale output')
  bms._mark_build_complete()
  assert bms._read_compile_inputs()['extra_defines']=='-DMODE=0'
  assert bms._read_compile_inputs()!=receipt
  (gen/'build-completed.json').unlink()
  try:bms._read_compile_inputs()
  except SystemExit:pass
  else:raise AssertionError('failed/interrupted build accepted')
print('PASS receipt changes on header/include/tool/flags, stable unchanged timestamp and stale rejection')
