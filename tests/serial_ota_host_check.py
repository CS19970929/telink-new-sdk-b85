"""真实升级core和Telink端口，RAM Flash故障/掉电模型；不生成固件镜像。"""
from pathlib import Path
import os, re, shlex, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
products = {'d008': 8, 'd011': 11, 'd013': 13, 'd014': 14}
fixture = (ROOT / 'tests/fixtures/serial_ota/check.c').read_text(encoding='utf8')
core = (ROOT / 'bms/core/bms_serial_ota.c').read_text(encoding='utf8')
core = re.sub(r'^#include "bms_(product|diag).h".*$', '', core, flags=re.M)
port = (ROOT / 'bms/platform/telink/bms_serial_ota_telink.c').read_text(encoding='utf8')
port = re.sub(r'^#include.*$', '', port, flags=re.M)
# TC32 的普通 u32 指针写入要求 4 字节对齐；x86 允许未对齐写，必须显式模拟此约束。
port = port.replace('    *base = candidate;',
                    '    assert(((uintptr_t)base & 3u) == 0u);\n    *base = candidate;')
prefix, scenarios = fixture.split('/* PRODUCTION_CODE */')
ble = (ROOT / 'bms/platform/telink/ble_ota.c').read_text(encoding='utf8')
guard = re.search(r'int app_ble_ota_write\(void \*packet\)\n\{.*?\n\}', ble, re.S).group()
for product in ([os.environ['BMS_PRODUCT']] if 'BMS_PRODUCT' in os.environ else products):
    with tempfile.TemporaryDirectory(prefix='serial-ota-') as temp:
        folder = Path(temp)
        source = folder / 'check.c'
        source.write_text('#define BMS_PRODUCT_ID %du\n' % products[product] + prefix + core + port + guard + scenarios, encoding='utf8')
        exe = folder / 'check.exe'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c99', '-fpack-struct', '-fshort-enums', '-Wall', '-Wextra', '-Werror',
            '-I', str(ROOT/'bms/core'), str(source), str(ROOT/'bms/core/bms_crc.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        print('PASS '+product+' serial OTA: real core/port, A/B, bounds, CRC, duplicate/sequence, flash faults, power cuts, timeout/wrap, BLE exclusion')
