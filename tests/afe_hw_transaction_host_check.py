"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_afe_hw_fragment_host_check():
    print("CHECK afe_hw_fragment_host_check", flush=True)
    #!/usr/bin/env python3
    """Execute actual AFE fragment/session code and complete-frame gate with mocked I/O."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import re, os, shlex, subprocess, tempfile
    from validation_support import function
    ROOT=Path(__file__).resolve().parents[1]
    MOD = Sources(ROOT)
    def main():
        strip=lambda name:re.sub(r'^#include[^\n]*','',(MOD/name).read_text(encoding='utf-8'),flags=re.M)
        code=(ROOT/'tests/fixtures/afe_hw_fragments.c').read_text(encoding='utf-8')
        code=code.replace('/* HEADER */', strip('bms_afe_hw_access.h'))
        code=code.replace('/* CRC */',strip('bms_crc.c'))
        code=code.replace('/* ACCESS */',strip('bms_afe_hw_access.c'))
        gate=function((MOD/'modbus_rtu.c').read_text(encoding='utf-8'),'u8 bms_afe_hw_write_complete_frame(')
        code=code.replace('/* GATE */',gate)
        with tempfile.TemporaryDirectory(prefix='afe-fragments-') as d:
            src=Path(d)/'test.c';exe=Path(d)/'test.exe';src.write_text(code,encoding='utf-8')
            subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c99','-Wall','-Wextra','-Werror',*host_includes(ROOT),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    if __name__=='__main__': main()

if __name__ == "__main__":
    check_afe_hw_fragment_host_check()
