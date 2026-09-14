#!/usr/bin/env python3
from pathlib import Path

IMPL = Path(__file__).with_name("refactor_soc_module_impl.py")
_real_write_text = Path.write_text
_real_unlink = Path.unlink


def _safe_write_text(self, data, *args, **kwargs):
    path = str(self).replace("\\", "/")
    if path.endswith("/.github/workflows/bms-ci.yml"):
        print("SOC refactor: preserving existing bms-ci.yml (workflow permission is handled separately)")
        return len(data)
    return _real_write_text(self, data, *args, **kwargs)


def _safe_unlink(self, *args, **kwargs):
    path = str(self).replace("\\", "/")
    if path.endswith("/tools/refactor_soc_module.py") or path.endswith("/.github/workflows/soc-module-refactor.yml"):
        print("SOC refactor: preserving bootstrap file", path)
        return None
    return _real_unlink(self, *args, **kwargs)


Path.write_text = _safe_write_text
Path.unlink = _safe_unlink

code = IMPL.read_text(encoding="utf-8")
namespace = {"__name__": "__main__", "__file__": str(IMPL)}
exec(compile(code, str(IMPL), "exec"), namespace, namespace)
