"""Execute the actual AFE-to-SOC adapter and production SOC direction decision."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample'


def extract(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


header = (APP / 'bms_afe.h').read_text(encoding='utf-8')
soc = (APP / 'SocEnhance.c').read_text(encoding='utf-8')
app = (APP / 'app.c').read_text(encoding='utf-8')
diag = (APP / 'bms_diag.c').read_text(encoding='utf-8')
assert 'sample_valid ? bms_afe_current_to_soc_ma(sample.current_ma) : 0' in app
assert 'update32(194u, (uint32_t)current_ma)' in diag, 'raw protocol sign must stay unchanged'
assert 'update32(196u, (uint32_t)bms_afe_current_to_soc_ma(current_ma))' in diag
code = r'''
#include <stdint.h>
#include <assert.h>
#define BMS_AFE_BACKEND_SH3673510 2
#define BMS_CURRENT_UNRELIABLE_MAX_MA 200u
typedef enum { SOC_INTEGRAL_DIR_NONE, SOC_INTEGRAL_DIR_CHG, SOC_INTEGRAL_DIR_DSG } soc_integral_dir_t;
static uint8_t g_soc_input_valid=1;
static int32_t g_soc_input_current_ma;
static struct { uint16_t current_deadband_ma; } g_soc_config={200};
/* PRODUCTION */
int main(void) {
    uint16_t magnitude;
    int32_t i;
    for(i=-200;i<=200;++i) {
        g_soc_input_current_ma=bms_afe_current_to_soc_ma(i);
        assert(soc_current_direction(&magnitude)==SOC_INTEGRAL_DIR_NONE);
    }
    for(i=201;i<=1000000;i+=137) {
        g_soc_input_current_ma=bms_afe_current_to_soc_ma(i);
        assert(soc_current_direction(&magnitude)==
            (BMS_AFE_BACKEND==2 ? SOC_INTEGRAL_DIR_CHG : SOC_INTEGRAL_DIR_DSG));
        assert(magnitude==(uint16_t)(i/100));
        g_soc_input_current_ma=bms_afe_current_to_soc_ma(-i);
        assert(soc_current_direction(&magnitude)==
            (BMS_AFE_BACKEND==2 ? SOC_INTEGRAL_DIR_DSG : SOC_INTEGRAL_DIR_CHG));
    }
    assert(bms_afe_current_to_soc_ma(INT32_MIN)==(BMS_AFE_BACKEND==2 ? INT32_MAX : INT32_MIN));
    g_soc_input_valid=0;
    assert(soc_current_direction(&magnitude)==SOC_INTEGRAL_DIR_NONE && magnitude==0);
    return 0;
}
'''
code = code.replace('/* PRODUCTION */', extract(header, 'static inline int32_t bms_afe_current_to_soc_ma(') +
                    extract(soc, 'static soc_integral_dir_t soc_current_direction('))
with tempfile.TemporaryDirectory(prefix='sh3510-soc-direction-') as tmp:
    c = Path(tmp) / 'check.c'
    c.write_text(code, encoding='utf-8')
    for backend in (1, 2):
        exe = Path(tmp) / ('check%d.exe' % backend)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-DBMS_AFE_BACKEND=%d' % backend, str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('AFE-to-SOC direction, deadband, invalid input and integer boundaries: PASS')
