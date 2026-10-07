"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_compile_receipt_check():
    print("CHECK compile_receipt_check", flush=True)
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
     with mock.patch.multiple(bms,REPO_ROOT=d,SDK_DIR=sdk,_HERE=here,GEN_DIR=gen,ELF=d/'firmware.elf',MAP=d/'firmware.map',LST=d/'firmware.lst',RAW_BIN=d/'firmware.raw',SOURCE_ORDER_FILE=here/'source_order.txt',LINKER_FILE=sdk/'boot.link',REQUIRED_VENDOR_LIBS=(sdk/'vendor.a',),TL_CHECK_FW2=d/'tool.exe'),mock.patch.object(bms,'_load_source_order_strict',return_value=['sdk/app.c']),mock.patch.object(bms,'_tc32_tool',return_value=str(d/'tool.exe')),mock.patch.object(bms,'_firmware_git_build_id',return_value='0x12345678u'),mock.patch.object(bms,'_firmware_git_dirty',return_value=0):
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
      for name, value in (("BUILD_MODE", "different-mode"), ("PRODUCT", "other-product")):
       with mock.patch.object(bms, name, value):
        try:bms._read_compile_inputs()
        except SystemExit:pass
        else:raise AssertionError('cross-configuration receipt accepted: '+name)
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

def check_monorepo_source_check():
    print("CHECK monorepo_source_check", flush=True)
    """Require every product to compile the same physical common implementation."""
    from pathlib import Path
    import json

    ROOT = Path(__file__).resolve().parents[1]
    products = ("d008", "d011", "d013", "d014")
    common = {p.relative_to(ROOT).as_posix() for p in (ROOT / "bms/core").glob("*.c")}
    sdk_sets = []
    for product in products:
        entries = [line.strip() for line in (ROOT / "bms/products" / product / "sources.txt").read_text().splitlines()
                   if line.strip() and not line.startswith("#")]
        assert len(entries) == len(set(entries)), product + " duplicate translation unit"
        assert common <= set(entries), product + " omitted shared module"
        expected_backend = "dvc1124" if product == "d008" else "sh3673510"
        for relative in entries:
            assert not Path(relative).is_absolute() and ".." not in Path(relative).parts
            assert (ROOT / relative).is_file(), relative
            assert "vendor/ble_sample/" not in relative, relative
            if relative.startswith("bms/afe/"):
                assert relative.startswith("bms/afe/" + expected_backend + "/")
        sdk_sets.append({x for x in entries if not x.startswith("bms/")})
        assert not list((ROOT / "bms/products" / product).glob("*.c")), "Product directories are data only"
    assert all(sdk == sdk_sets[0] for sdk in sdk_sets)
    assert not (ROOT / "bms_tools/source_order.txt").exists()
    baselines = json.loads((ROOT / "bms/products/baselines.json").read_text())
    assert set(baselines) == set(products)
    for product, record in baselines.items():
        for field in ("import_snapshot_commit", "comparison_commit", "recorded_import_commit"):
            assert len(record[field]) == 40 and all(c in "0123456789abcdef" for c in record[field])
        assert isinstance(record["recorded_import_available"], bool)
    # Shared SH code owns register formulas, not GPIO literals or NTC/threshold inputs.
    shared = (ROOT / "bms/afe/sh3673510/sh3673510_project_config.h").read_text()
    assert " GPIO_P" not in shared and "#define SH3673510_HW_DEFAULT_" not in shared
    for product in products[1:]:
        config = (ROOT / "bms/products" / product / "bms_sh3673510_config.h").read_text()
        assert '#include "../sh3673510_defaults.h"' in config
        config += (ROOT / "bms/products/sh3673510_defaults.h").read_text()
        assert "#define SH3673510_BOARD_BAT_NTC1_INDEX" in config
        assert "#define SH3673510_BOARD_SPI_GROUP" in config
        assert "#define SH3673510_HW_DEFAULT_COV_MV" in config
    legacy = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample"
    assert not list(legacy.glob("*.c")), "Old application source copies remain"
    print(f"PASS four products, {len(common)} shared C modules, one SDK, explicit backend selection, no source copies")

def check_resource_safety_host_check():
    print("CHECK resource_safety_host_check", flush=True)
    """Execute stack scan and resource rejection contracts without target hardware."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import importlib.util, tempfile, subprocess, os, shlex, re
    from unittest import mock
    ROOT=Path(__file__).resolve().parents[1]
    APP = Sources(ROOT)
    spec=importlib.util.spec_from_file_location('resource_bms', ROOT/'bms_tools/bms.py')
    bms=importlib.util.module_from_spec(spec);spec.loader.exec_module(bms)
    with tempfile.TemporaryDirectory(prefix='bms-resource-test-') as tmp:
        d=Path(tmp)
        header=(APP/'bms_stack_monitor.h').read_text()
        src=(APP/'bms_stack_monitor.c').read_text().split('void bms_stack_monitor_poll(void)')[0]
        src=re.sub(r'^#include[^\n]*','',src,flags=re.M)
        src=header+src+r"""
    #include <assert.h>
    int main(void) {
     uint32_t mem[256], cursor=0, i;
     for(i=0;i<256;++i)mem[i]=BMS_STACK_PAINT;
     bms_stack_scan(mem,256,&cursor,&g_bms_stack_watermark.main_free_min_bytes,1);
     assert(cursor==64 && g_bms_stack_watermark.valid_mask==0);
     mem[200]=0;
     for(i=0;i<4;++i)bms_stack_scan(mem,256,&cursor,&g_bms_stack_watermark.main_free_min_bytes,1);
     assert(g_bms_stack_watermark.main_free_min_bytes==800);
     mem[80]=1;
     for(i=0;i<4;++i)bms_stack_scan(mem,256,&cursor,&g_bms_stack_watermark.main_free_min_bytes,1);
     assert(g_bms_stack_watermark.main_free_min_bytes==320);
     mem[80]=BMS_STACK_PAINT;
     for(i=0;i<5;++i)bms_stack_scan(mem,256,&cursor,&g_bms_stack_watermark.main_free_min_bytes,1);
     assert(g_bms_stack_watermark.main_free_min_bytes==320);
     mem[0]=0;
     bms_stack_scan(mem,256,&cursor,&g_bms_stack_watermark.irq_free_min_bytes,2);
     assert(g_bms_stack_watermark.overflow_flags==2);
     mem[0]=BMS_STACK_PAINT;
     for(i=0;i<6;++i)bms_stack_scan(mem,256,&cursor,&g_bms_stack_watermark.irq_free_min_bytes,2);
     assert(g_bms_stack_watermark.overflow_flags==2);
     return 0;
    }
    """
        p=d/'stack.c';p.write_text(src);exe=d/'stack.exe'
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-variable',str(p),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
        def case(ram=0x846000,sram=0x848000,raw=0x1a008,image=0x1a014,missing=False,ok=False,production=False):
            (d/'fw.map').write_text('0x%08x PROVIDE (_ram_use_end_, .)\n'%ram + ('' if missing else '0x%08x PROVIDE (_bin_size_, expr)\n'%raw))
            (d/'fw.lst').write_text('%08x g *ABS* 00000000 __SRAM_SIZE\n'%sram)
            (d/'fw.bin').write_bytes(bytes(image))
            with mock.patch.multiple(bms, MAP=d/'fw.map', LST=d/'fw.lst',BIN=d/'fw.bin',ELF=d/'none',GEN_DIR=d,PRODUCTION=production),mock.patch.object(bms,'_git_provenance',return_value={'commit':'fixture','dirty':False}):
                try: bms.cmd_map(bms.argparse.Namespace())
                except SystemExit:
                    assert not ok
                else: assert ok
        case(ok=True) # real checker alignment, not simply raw+4
        case(raw=0x1cff0,image=0x1cff4,production=True,ok=True)
        case(raw=0x1d000,image=0x1d004,production=True) # below hard 8 KiB release reserve
        case(sram=0x850000)
        case(ram=0x848000-3072)
        case(ram=0x83fffc)
        case(missing=True)
        case(raw=0x1f000,image=0x1f004) # CRC crosses 124 KiB slot
        case(image=0x1a00c) # stale/mismatched BIN
    print('PASS bounded main/IRQ scan, sticky canary, minimum retention and resource hard gates')

if __name__ == "__main__":
    check_compile_receipt_check()
    check_monorepo_source_check()
    check_resource_safety_host_check()
