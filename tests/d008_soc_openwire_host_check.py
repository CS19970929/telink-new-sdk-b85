"""Run real SOC and Open-Wire policy together, with bounded AFE mocks.

The firmware sample ABI and replay flags stay unchanged. Temporary C/executable
files use the caller's TEMP directory, never the project tree.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from d008_power_soc_host_check import ROOT, MOD, FIX, source, function


def main():
    fixture = (FIX / 'soc.c').read_text().split('int main(', 1)[0]
    for name in ('bms_features_openwire_active',
                 'bms_features_openwire_sample_active',
                 'bms_features_openwire_suspected'):
        fixture, count = re.subn(r'static uint8_t ' + name +
                                r'\(void\)\{[^\n]+\}',
                                'static uint8_t ' + name + '(void);', fixture)
        assert count == 1
    soc = (('#include "' + (ROOT/'bms/core/bms_soc_eta.c').as_posix() + '"\n') + source('bms_soc_defs.h') + source('bms_diag.h') +
           source('bms_soc.h') + source('bms_soc_profile.h') +
           'static int bms_config_store_set_soc(const bms_soc_config_t *c){return config_store_write_ok;}\n'
           'static int bms_config_store_get_soc(bms_soc_config_t *c){bms_soc_get_default_config(c);return 1;}\n' +
           source('bms_soc.c') + '\n' + '\n'.join(re.findall(r'^#define SOC_LEARNING_(?:TEMP|CURRENT|PACK)_FAULT_MASK[^\n]*', (MOD / 'app.c').read_text(), re.M)) + '\n' + function('app.c', 'static void app_update_soc_from_sample('))
    fixture = fixture.replace('/* PRODUCTION_SOURCE */', soc)
    fixture += '\nvoid bms_diag_trace(uint16_t event, uint32_t a, uint32_t b) {(void)event;(void)a;(void)b;}\n'
    floor = re.search(r'^#define BMS_CURRENT_UNRELIABLE_MAX_MA[^\n]*',
                      (MOD / 'conf.h').read_text(), re.M)
    fixture = fixture.replace('/* CURRENT_FLOOR */', floor.group(0))
    feature = (MOD / 'bms_features.c').read_text()
    feature_type = feature[feature.index('typedef struct'):feature.index('static bms_feature_state_t')]
    afe = (MOD / 'bms_afe.h').read_text()
    afe_types = '\n'.join(re.findall(
        r'typedef (?:struct|enum)\s*\{[^}]*\}\s*(?:bms_afe_diag_state_t|bms_afe_openwire_result_t);',
        afe, re.S))
    features_h = (MOD / 'bms_features.h').read_text()
    constants = '\n'.join(line for line in features_h.splitlines()
                          if line.startswith('#define BMS_OPENWIRE_') or
                          line.startswith('#define BMS_FEATURE_SERVICE_PERIOD_MS'))
    constants += '\n#define BMS_OPENWIRE_FIRST_IDLE_SAMPLES (BMS_OPENWIRE_FIRST_IDLE_MS / BMS_FEATURE_SERVICE_PERIOD_MS)\n'
    constants += '#define BMS_OPENWIRE_PERIOD_SAMPLES (BMS_OPENWIRE_PERIOD_MS / BMS_FEATURE_SERVICE_PERIOD_MS)\n'
    fixture += '\n#define BMS_AFE_FEATURE_MAX_CELLS 24u\n' + afe_types + '\n' + constants
    fixture += '\ntypedef int bms_heater_state_t;\n' + feature_type
    fixture += (Path(__file__).parent / 'fixtures/d008_soc_openwire.c').read_text()
    for signature in ('static uint8_t openwire_eligible(', 'static void service_openwire(',
                      'void bms_features_service(', 'uint8_t bms_features_openwire_active(',
                      'uint8_t bms_features_openwire_sample_active(',
                      'uint8_t bms_features_openwire_suspected('):
        fixture = fixture.replace('/* FEATURE_SOURCE */',
                                  function('bms_features.c', signature) + '\n/* FEATURE_SOURCE */')
    fixture = fixture.replace('/* FEATURE_SOURCE */', '')
    with tempfile.TemporaryDirectory(prefix='d008-soc-openwire-') as directory:
        path = Path(directory) / 'test.c'
        path.write_text(fixture)
        for trace_enabled in (0, 1):
            executable = Path(directory) / f'test-{trace_enabled}.exe'
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                '-Wno-unused-parameter', f'-DBMS_SOC_OCV_TRACE_ENABLE={trace_enabled}',
                str(path), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
