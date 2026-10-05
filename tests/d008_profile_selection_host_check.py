from pathlib import Path
from project_paths import Sources, host_includes, selected_source
import tempfile,subprocess,os,shlex
ROOT=Path(__file__).resolve().parents[1]
APP = Sources(ROOT)
with tempfile.TemporaryDirectory(prefix='d008-profile-') as d:
    p=Path(d)/'profile.c'
    for selector,count in ((None,16),(1,24),(2,20),(3,16)):
        p.write_text('#include "d008_product_profile.h"\n#if D008_PRODUCT_CELL_COUNT != %d\n#error inconsistent_profile\n#endif\nint main(void){return 0;}\n'%count)
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99',*host_includes(ROOT),str(p),'-o',str(Path(d)/'profile.exe')]+([] if selector is None else ['-DD008_PRODUCT_PROFILE=%d'%selector]),check=True)
print('PASS default 16S and explicit 24S/20S/16S identities')

with tempfile.TemporaryDirectory(prefix='d008-release-profile-') as d:
    p=Path(d)/'profile.c'
    p.write_text('#include "d008_product_profile.h"\n')
    base=shlex.split(os.environ.get('CC','cc'))+['-E','-DBMS_PRODUCTION_BUILD=1',*host_includes(ROOT),str(p)]
    assert subprocess.run(base,capture_output=True).returncode != 0
    for selector in (1,2,3):
        subprocess.run(base+['-DD008_PRODUCT_PROFILE=%d'%selector],check=True,stdout=subprocess.DEVNULL)
print('PASS production rejects implicit D008 profile; all three explicit profiles accepted')
