"""完整公共 feature 状态机，真实产品能力函数；仅替换采样、总线与 GPIO。"""
import os
import re
from project_paths import selected_source
from validation_support import read, profile_prefix, without_includes, function, run_c, evidence

product = os.environ.get('BMS_PRODUCT', 'd014')
code = profile_prefix(product)
code += '\n#include "bms_features.h"\n#include "bms_board.h"\n#include "bms_error.h"\n#include "bms_state.h"\n#include "bms_sw_protection.h"\n#include "bms_diag.h"\n#include "bms_debug_log.h"\n'
code += re.search(r'typedef struct\s*\{.*?\} bms_user_params_t;', read('bms/core/bms_config_store.h'), re.S).group()
board = selected_source('bms/platform/telink/bms_board.c', product)
for signature in ('uint8_t bms_board_balance_supported(', 'uint8_t bms_board_heater_supported(',
                  'uint8_t bms_board_heater_allowed(', 'uint8_t bms_board_heater_fuse_supported(',
                  'uint16_t bms_board_heater_off_fault_temp_x10(', 'uint16_t bms_board_heater_off_fault_confirm_ms(',
                  'uint8_t bms_board_charge_source_present('):
    code += '\n' + function(board, signature)
code += read('tests/fixtures/features_scenarios.c').replace('/* FEATURES */', without_includes(read('bms/app/bms_features.c')))
raw = run_c(code, ['bms/core/bms_state.c','bms/core/bms_sw_protection.c'],
            ['-Wno-unused-function'], name='features')
evidence({'domain':'features','product':product,'observations':raw.strip(),
          'boundary':'完整 feature 函数体及产品能力、真实错误状态；AFE/温度/GPIO/配置读取为环境替身'})
