from pathlib import Path
import tempfile,subprocess,os,shlex
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'
with tempfile.TemporaryDirectory(prefix='d008-profile-') as d:
    p=Path(d)/'profile.c'
    for selector,count in ((None,24),(1,24),(2,20),(3,16)):
        p.write_text('#include "d008_product_profile.h"\n#if D008_PRODUCT_CELL_COUNT != %d\n#error inconsistent_profile\n#endif\nint main(void){return 0;}\n'%count)
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-I',str(APP),str(p),'-o',str(Path(d)/'profile.exe')]+([] if selector is None else ['-DD008_PRODUCT_PROFILE=%d'%selector]),check=True)
print('PASS default 24S and explicit 24S/20S/16S identities')
