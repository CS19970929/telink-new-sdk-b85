"""验证夹具共用的编译与证据输出；业务算法始终从产品源码编译。"""
from pathlib import Path
import hashlib
import json
import os
import re
import shlex
import subprocess
import tempfile

from project_paths import host_includes

ROOT = Path(__file__).resolve().parents[1]


class ScenarioFailure(AssertionError):
    """保留子进程证据，让策略探针区分业务缺口、崩溃与插桩错误。"""
    def __init__(self, message, result):
        super().__init__(message)
        self.returncode = result.returncode
        self.stdout = result.stdout
        self.stderr = result.stderr


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def without_includes(text):
    # 仅替换编译边界，不修改函数体；报告必须标明这种证据低于原 TU。
    return re.sub(r'^\s*#\s*(?:include[^\n]*|pragma once)', '', text, flags=re.M)


def function(text, signature):
    match = re.search(re.escape(signature) + r'[^;{}]*\{', text)
    if not match:
        raise AssertionError("找不到生产函数：" + signature)
    end = text.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


def compiler_flags():
    flags = ['-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
    if os.environ.get('BMS_SANITIZE') == '1':
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                  '-fno-omit-frame-pointer']
    return flags


def run_c(code, sources=(), flags=(), name='scenario', expect_failure=False, runs=None):
    with tempfile.TemporaryDirectory(prefix='bms-validation-') as directory:
        folder = Path(directory)
        source = folder / (name + '.c')
        source.write_text(code, encoding='utf-8')
        executable = folder / (name + '.exe')
        command = [*shlex.split(os.environ.get('CC', 'cc')), *compiler_flags(),
                   *flags, *host_includes(ROOT), str(source),
                   *(str(ROOT / p) for p in sources), '-o', str(executable)]
        def preserve():
            destination = os.environ.get('BMS_CASE_OUTPUT')
            if destination:
                target = Path(destination)
                target.mkdir(parents=True, exist_ok=True)
                (target / (name + '.c')).write_text(code, encoding='utf-8')
                (target / (name + '-compile.json')).write_text(json.dumps(command, ensure_ascii=False, indent=2), encoding='utf-8')
        try:
            subprocess.run(command, check=True)
        except (OSError, subprocess.CalledProcessError):
            preserve()
            raise
        outputs = []
        for overrides in (runs if runs is not None else [{}]):
            result = subprocess.run([str(executable)], capture_output=True, text=True,
                                    env={**os.environ, **overrides})
            print(result.stdout, end='')
            print(result.stderr, end='')
            if expect_failure:
                if result.returncode != 1 or 'expected=' not in result.stderr or 'actual=' not in result.stderr:
                    preserve()
                    raise ScenarioFailure(f'{name} 没有得到预期业务断言失败，exit={result.returncode}', result)
                outputs.append(result.stdout + result.stderr)
            elif result.returncode:
                preserve()
                raise ScenarioFailure(f'{name} 运行失败，exit={result.returncode}, run={overrides}', result)
            else:
                outputs.append(result.stdout)
        return ''.join(outputs)


def evidence(value):
    print('BMS_EVIDENCE ' + json.dumps(value, ensure_ascii=False, sort_keys=True))


def profile_prefix(product):
    """真实产品头、默认参数与 builder/validator；不替换校验或默认值。"""
    result = '#include <stdint.h>\n#include <string.h>\n#include <stdio.h>\n#include <assert.h>\n'
    result += 'typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;\n'
    result += '#include "bms_protection_params.h"\n#include "bms_afe_backend.h"\n'
    result += '#include "bms_product_conf.h"\n'
    if product == 'd008':
        result += '#include "dvc1124_project_config.h"\n'
    else:
        result += '#include "sh3673510_project_config.h"\n#include "sh3673510_quantize.h"\n'
    result += without_includes(read('bms/core/param.h'))
    result += without_includes(read('bms/core/bms_afe_hw_profile.h'))
    source = read('bms/core/bms_afe_hw_profile.c')
    result += source[source.index('static u16 ms10_to_ms'):source.index('u8 bms_afe_hw_profile_init(void)')]
    return result
