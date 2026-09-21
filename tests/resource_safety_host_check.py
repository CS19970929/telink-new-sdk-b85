"""Execute stack scan and resource rejection contracts without target hardware."""
from pathlib import Path
import importlib.util, tempfile, subprocess, os, shlex, re
from unittest import mock
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
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
    def case(ram=0x846000,sram=0x848000,raw=0x1a008,image=0x1a014,missing=False,ok=False):
        (d/'fw.map').write_text('0x%08x PROVIDE (_ram_use_end_, .)\n'%ram + ('' if missing else '0x%08x PROVIDE (_bin_size_, expr)\n'%raw))
        (d/'fw.lst').write_text('%08x g *ABS* 00000000 __SRAM_SIZE\n'%sram)
        (d/'fw.bin').write_bytes(bytes(image))
        with mock.patch.multiple(bms, MAP=d/'fw.map', LST=d/'fw.lst',BIN=d/'fw.bin',ELF=d/'none',GEN_DIR=d),mock.patch.object(bms,'_git_provenance',return_value={'commit':'fixture','dirty':False}):
            try: bms.cmd_map(bms.argparse.Namespace())
            except SystemExit:
                assert not ok
            else: assert ok
    case(ok=True) # real checker alignment, not simply raw+4
    case(sram=0x850000)
    case(ram=0x848000-3072)
    case(ram=0x83fffc)
    case(missing=True)
    case(raw=0x1f000,image=0x1f004) # CRC crosses 124 KiB slot
    case(image=0x1a00c) # stale/mismatched BIN
print('PASS bounded main/IRQ scan, sticky canary, minimum retention and resource hard gates')
