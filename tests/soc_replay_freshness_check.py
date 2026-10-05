"""预先存在的回放 EXE 不能绕过本次源码编译，防止伪绿色。"""
import importlib.util
import os
from pathlib import Path
import tempfile
import time
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('soc_simulator', ROOT/'tools/soc_simulator/soc_simulator.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='soc-stale-') as folder:
    stale = Path(folder)/'old.exe'
    stale.write_bytes(b'previous build from another product')
    future = time.time()+86400
    os.utime(stale,(future,future))
    module.RUNNER = stale
    with patch.object(module.subprocess, 'run') as compile_call:
        module.ensure_runner()
        assert compile_call.call_count == 1, '旧 EXE 时间较新导致完全跳过当前源码编译'
        assert str(stale) not in compile_call.call_args.args[0], '并发产品仍写同一个缓存路径'
print('PASS 每次回放重新编译并使用独立路径')
